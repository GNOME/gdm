/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <gnome.h>
#include <syslog.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/Xauth.h>
#include <sys/types.h>
#include <fcntl.h>

#include "gdm.h"
#include "gdm-net.h"
#include "server.h"
#include "display.h"
#include "slave.h"
#include "misc.h"
#include "xdmcp.h"
#include "choose.h"
#include "gdm-net.h"

/* External vars */
extern gboolean GdmXdmcp;
extern gint sessions;
extern gint flexi_servers;
extern gint pending;
extern GSList *displays;
extern GdmConnection *fifoconn;
extern GdmConnection *unixconn;

static gboolean
gdm_display_check_loop (GdmDisplay *disp)
{
  time_t now;
  time_t since_last;
  
  now = time (NULL);

  if (disp->disabled)
    return FALSE;
  
  if (disp->last_start_time > now || disp->last_start_time == 0)
    {
      /* Reset everything if this is the first time in this
       * function, or if the system clock got reset backward.
       */
      disp->last_start_time = now;
      disp->retry_count = 1;

      gdm_debug ("Resetting counts for loop of death detection");
      
      return TRUE;
    }

  since_last = now - disp->last_start_time;

  /* If it's been at least 1.5 minutes since the last startup
   * attempt, then we reset everything.
   */

  if (since_last >= 90)
    {
      disp->last_start_time = now;
      disp->retry_count = 1;

      gdm_debug ("Resetting counts for loop of death detection, 90 seconds elapsed.");
      
      return TRUE;
    }

  /* If we've tried too many times we bail out. i.e. this means we
   * tried too many times in the 90-second period.
   */
  if (disp->retry_count > 4) {
	  /* This means we have no clue what's happening,
	   * it's not X server crashing as we would have
	   * cought that elsewhere.  Things are just
	   * not working out, so tell the user */
	  char *s = g_strdup_printf (_("Failed to start the display server "
				       "several times in a short time period; "
				       "disabling display %s"), disp->name);
	  /* only display a dialog box if this is a local display */
	  if (disp->type == TYPE_LOCAL ||
	      disp->type == TYPE_FLEXI)
		  gdm_text_message_dialog (s);
	  gdm_error ("%s", s);
	  g_free (s);

	  disp->disabled = TRUE;

	  gdm_debug ("Failed to start X server after several retries; aborting.");

	  return FALSE;
  }
  
  /* At least 8 seconds between start attempts,
   * so you can try to kill gdm from the console
   * in these gaps.
   */
  if (since_last < 8)
    {
      gdm_debug ("Will sleep %ld seconds before next X server restart attempt",
                 (long)(8 - since_last));
      now = time (NULL) + 8 - since_last;
      disp->sleep_before_run = 8 - since_last;
    }
  else
    {
      disp->sleep_before_run = 0;
    }

  disp->retry_count += 1;
  disp->last_start_time = now;

  return TRUE;
}


/**
 * gdm_display_manage:
 * @d: Pointer to a GdmDisplay struct
 *
 * Manage (Initialize and start login session) display
 */

gboolean 
gdm_display_manage (GdmDisplay *d)
{
    pid_t pid;
    int i;

    if (!d) 
	return FALSE;

    gdm_debug ("gdm_display_manage: Managing %s", d->name);

    if ( ! gdm_display_check_loop (d))
	    return FALSE;

    gdm_sigchld_block_push ();

    /* If we have an old slave process hanging around, kill it */
    if (d->slavepid) {
	    if (kill (d->slavepid, SIGINT) == 0)
		    waitpid (d->slavepid, 0, 0);
	    d->slavepid = 0;
    }

    /* Fork slave process */
    gdm_sigterm_block_push ();
    pid = d->slavepid = fork ();
    gdm_sigterm_block_push ();

    gdm_sigchld_block_pop ();

    switch (pid) {

    case 0:
	setpgid (0, 0);

	/* Close XDMCP fd in slave process */
	if (GdmXdmcp)
	    gdm_xdmcp_close ();

	gdm_connection_close (fifoconn);
	fifoconn = NULL;
	gdm_connection_close (unixconn);
	unixconn = NULL;

	/* Close everything */
	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open("/dev/null", O_RDWR); /* open stderr - fd 2 */

	if (d->type == TYPE_LOCAL) {
	    gdm_slave_start (d);
	    /* if we ever return, bad things are happening */
	    gdm_server_stop (d);
	    _exit (DISPLAY_ABORT);
	} else if (SERVER_IS_FLEXI (d)) {
	    gdm_slave_start (d);
	    gdm_server_stop (d);
	    /* we expect to return after the session finishes */
	    _exit (DISPLAY_REMANAGE);
	} else if (d->type == TYPE_XDMCP &&
		   d->dispstat == XDMCP_MANAGED) {
	    gdm_slave_start (d);
	    gdm_server_stop (d);
	    /* we expect to return after the session finishes */
	    _exit (DISPLAY_REMANAGE);
	}

	/* yaikes, how did we ever get here, though I suppose
	 * it could be possible if XMDCP thing wasn't really set up */
	gdm_server_stop (d);
	_exit (DISPLAY_ABORT);

	break;

    case -1:
	gdm_error (_("gdm_display_manage: Failed forking gdm slave process for %s"), d->name);

	return FALSE;

    default:
	gdm_debug ("gdm_display_manage: Forked slave: %d",
		   (int)pid);
	break;
    }

    if (SERVER_IS_LOCAL (d)) {
	    d->dispstat = DISPLAY_ALIVE;
    }

    /* reset sleep to 0 */
    d->sleep_before_run = 0;

    return TRUE;
}


/**
 * gdm_display_unmanage:
 * @d: Pointer to a GdmDisplay struct
 *
 * Stop services for a display
 */

void
gdm_display_unmanage (GdmDisplay *d)
{
    if (!d)
	return;

    gdm_debug ("gdm_display_unmanage: Stopping %s (slave pid: %d)",
	       d->name, (int)d->slavepid);

    /* Kill slave */
    if (d->slavepid != 0) {
	if (kill (d->slavepid, SIGTERM) == 0)
		waitpid (d->slavepid, 0, 0);
	d->slavepid = 0;
    }
    
    if (d->type == TYPE_LOCAL)
	d->dispstat = DISPLAY_DEAD;
    else /* TYPE_XDMCP,TYPE_FLEXI,TYPE_FLEXI_XNEST */
	gdm_display_dispose (d);
}


/**
 * gdm_display_dispose:
 * @d: Pointer to a GdmDisplay struct
 *
 * Deallocate display and all its resources
 */

void
gdm_display_dispose (GdmDisplay *d)
{
    if (d == NULL)
	return;

    if (d->socket_conn != NULL) {
	    GdmConnection *conn = d->socket_conn;
	    d->socket_conn = NULL;
	    gdm_connection_set_close_notify (conn, NULL, NULL);
    }

    displays = g_slist_remove (displays, d);

    if (SERVER_IS_FLEXI (d))
	    flexi_servers --;

    if (d->type == TYPE_XDMCP) {
	sessions--;
	d->type = -1;

	if (d->dispstat == XDMCP_PENDING)
		pending--;
    }

    if (d->name) {
	gdm_debug ("gdm_display_dispose: Disposing %s", d->name);
	g_free (d->name);
	d->name = NULL;
    }

    g_free (d->hostname);
    d->hostname = NULL;

    g_free (d->authfile);
    d->authfile = NULL;

    g_free (d->authfile_gdm);
    d->authfile_gdm = NULL;

    if (d->auths) {
	GSList *tmpauth = d->auths;

	while (tmpauth && tmpauth->data) {
	    XauDisposeAuth ((Xauth *) tmpauth->data);
	    tmpauth->data = NULL;
	    tmpauth = tmpauth->next;
	}

	g_slist_free (d->auths);
	d->auths = NULL;
    }

    g_free (d->userauth);
    d->userauth = NULL;

    g_free (d->command);
    d->command = NULL;

    g_free (d->cookie);
    d->cookie = NULL;

    g_free (d->bcookie);
    d->bcookie = NULL;

    if (d->indirect_id > 0)
	    gdm_choose_indirect_dispose_empty_id (d->indirect_id);
    d->indirect_id = 0;

    g_free (d->xnest_disp);
    d->xnest_disp = NULL;

    g_free (d->xnest_auth_file);
    d->xnest_auth_file = NULL;

    g_free (d->login);
    d->login = NULL;

    g_free (d);
}


/**
 * gdm_display_lookup:
 * @pid: pid of slave process to look up
 *
 * Return the display managed by pid
 */

GdmDisplay *
gdm_display_lookup (pid_t pid)
{
    GSList *li;

    /* Find slave in display list */
    for (li = displays; li != NULL; li = li->next) {
	    GdmDisplay *d = li->data;

	    if (d != NULL &&
		pid == d->slavepid)
		    return d;
    }
     
    /* Slave not found */
    return NULL;
}


/* EOF */
