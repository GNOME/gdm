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
#include <libgnome/libgnome.h>
#include <syslog.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <vicious.h>

#include "gdm.h"
#include "gdm-net.h"
#include "server.h"
#include "display.h"
#include "slave.h"
#include "misc.h"
#include "xdmcp.h"
#include "choose.h"
#include "auth.h"
#include "gdm-net.h"

/* External vars */
extern gboolean GdmXdmcp;
extern gint xdmcp_sessions;
extern gint flexi_servers;
extern gint xdmcp_pending;
extern gboolean gdm_in_final_cleanup;
extern GSList *displays;
extern GdmConnection *fifoconn;
extern GdmConnection *pipeconn;
extern GdmConnection *unixconn;
extern int slave_fifo_pipe_fd; /* the slavepipe (like fifo) connection, this is the write end */

static gboolean
gdm_display_check_loop (GdmDisplay *disp)
{
  time_t now;
  time_t since_last;
  time_t since_loop;
  
  now = time (NULL);

  if (disp->disabled)
    return FALSE;

  gdm_debug ("loop check: last_start %ld, last_loop %ld, now: %ld, retry_count: %d", (long)disp->last_start_time, (long) disp->last_loop_start_time, (long) now, disp->retry_count);
  
  if (disp->last_loop_start_time > now || disp->last_loop_start_time == 0)
    {
      /* Reset everything if this is the first time in this
       * function, or if the system clock got reset backward.
       */
      disp->last_loop_start_time = now;
      disp->last_start_time = now;
      disp->retry_count = 1;

      gdm_debug ("Resetting counts for loop of death detection");
      
      return TRUE;
    }

  since_loop = now - disp->last_loop_start_time;
  since_last = now - disp->last_start_time;

  /* If it's been at least 1.5 minutes since the last startup loop
   * attempt, then we reset everything.  Or if the last startup was more then
   * 30 seconds ago, then it was likely a successful session.
   */

  if (since_loop >= 90 || since_last >= 30)
    {
      disp->last_loop_start_time = now;
      disp->last_start_time = now;
      disp->retry_count = 1;

      gdm_debug ("Resetting counts for loop of death detection, 90 seconds elapsed since loop started or session lasted more then 30 seconds.");
      
      return TRUE;
    }

  /* If we've tried too many times we bail out. i.e. this means we
   * tried too many times in the 90-second period.
   */
  if (disp->retry_count >= 6) {
	  /* This means we have no clue what's happening,
	   * it's not X server crashing as we would have
	   * cought that elsewhere.  Things are just
	   * not working out, so tell the user.
	   * However this may have been caused by a malicious local user
	   * zapping the display repeatedly, that shouldn't cause gdm
	   * to stop working completely so just wait for 2 minutes,
	   * that should give people ample time to stop gdm if needed,
	   * or just wait for the stupid malicious user to get bored
	   * and go away */
	  char *s = g_strdup_printf (_("The display server has been shut down "
				       "about 6 times in the last 90 seconds, "
				       "it is likely that something bad is "
				       "going on.  I will wait for 2 minutes "
				       "before trying again on display %s."),
				       disp->name);
	  /* only display a dialog box if this is a local display */
	  if (disp->type == TYPE_LOCAL ||
	      disp->type == TYPE_FLEXI) {
		  gdm_text_message_dialog (s);
	  }
	  gdm_error ("%s", s);
	  g_free (s);

	  /* Wait 2 minutes */
	  disp->sleep_before_run = 120;
	  /* well, "last" start time will really be in the future */
	  disp->last_start_time = now + disp->sleep_before_run;

	  disp->retry_count = 1;
	  /* this will reset stuff in the next run (after this
	     "after-two-minutes" server starts) */
	  disp->last_loop_start_time = 0;
	  
	  return TRUE;
  }
  
  /* At least 8 seconds between start attempts, but only after
   * the second start attempt, so you can try to kill gdm from the console
   * in these gaps.
   */
  if (disp->retry_count > 2 && since_last < 8)
    {
      gdm_debug ("Will sleep %ld seconds before next X server restart attempt",
                 (long)(8 - since_last));
      now = time (NULL) + 8 - since_last;
      disp->sleep_before_run = 8 - since_last;
      /* well, "last" start time will really be in the future */
      disp->last_start_time = now + disp->sleep_before_run;
    }
  else
    {
      disp->sleep_before_run = 0;
      disp->last_start_time = now;
    }

  disp->retry_count ++;

  return TRUE;
}

static void
whack_old_slave (GdmDisplay *d)
{
    time_t t = time (NULL);
    gboolean waitsleep = TRUE;
    /* if we have DISPLAY_DEAD set, then this has already been killed */
    if (d->dispstat == DISPLAY_DEAD)
	    waitsleep = FALSE;
    /* Kill slave */
    if (d->slavepid > 1 &&
	(d->dispstat == DISPLAY_DEAD || kill (d->slavepid, SIGTERM) == 0)) {
	    int exitstatus;
	    int ret;
wait_again:
		
	    if (waitsleep)
		    /* wait for some signal, yes this is a race */
		    sleep (10);
	    waitsleep = TRUE;
	    errno = 0;
	    ret = waitpid (d->slavepid, &exitstatus, WNOHANG);
	    if (ret <= 0) {
		    /* rekill the slave to tell it to
		       hurry up and die if we're getting
		       killed ourselves */
		    if (ve_signal_was_notified (SIGTERM) ||
			ve_signal_was_notified (SIGINT) ||
			ve_signal_was_notified (SIGHUP) ||
			t + 10 <= time (NULL)) {
			    gdm_debug ("whack_old_slave: GOT ANOTHER SIGTERM (or it was 10 secs already), killing slave again");
			    t = time (NULL);
			    kill (d->slavepid, SIGTERM);
			    goto wait_again;
		    } else if (ret < 0 && errno == EINTR) {
			    goto wait_again;
		    }
	    }

	    if (WIFSIGNALED (exitstatus)) {
		    gdm_debug ("whack_old_slave: Slave crashed (signal %d), killing its children",
			       (int)WTERMSIG (exitstatus));

		    if (d->sesspid > 1)
			    kill (-(d->sesspid), SIGTERM);
		    d->sesspid = 0;
		    if (d->greetpid > 1)
			    kill (-(d->greetpid), SIGTERM);
		    d->greetpid = 0;
		    if (d->chooserpid > 1)
			    kill (-(d->chooserpid), SIGTERM);
		    d->chooserpid = 0;
		    if (d->servpid > 1)
			    kill (d->servpid, SIGTERM);
		    d->servpid = 0;
	    }
    }
    d->slavepid = 0;
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
    int fds[2];

    if (!d) 
	return FALSE;

    gdm_debug ("gdm_display_manage: Managing %s", d->name);

    if (pipe (fds) < 0) {
	    gdm_error (_("%s: Cannot create pipe"), "gdm_display_manage");
    }

    if ( ! gdm_display_check_loop (d))
	    return FALSE;

    if (d->slavepid != 0)
	    gdm_debug ("gdm_display_manage: Old slave pid is %d", (int)d->slavepid);

    /* If we have an old slave process hanging around, kill it */
    /* This shouldn't be a normal code path however, so it doesn't matter
     * that we are hanging */
    whack_old_slave (d);

    d->managetime = time (NULL);

    /* Fork slave process */
    pid = d->slavepid = fork ();

    switch (pid) {

    case 0:
	setpgid (0, 0);

	/* Make the slave it's own leader.  This 1) makes killing -pid of
	 * the daemon work more sanely because the daemon can whack the
	 * slave much better itself */
	setsid ();

	/* In the child setup empty mask and set all signals to
	 * default values, we'll make them more fun later */
	gdm_unset_signals ();

	d->slavepid = getpid ();

	/* Close XDMCP fd in slave process */
	if (GdmXdmcp)
	    gdm_xdmcp_close ();

	gdm_connection_close (fifoconn);
	fifoconn = NULL;
	gdm_connection_close (pipeconn);
	pipeconn = NULL;
	gdm_connection_close (unixconn);
	unixconn = NULL;

	closelog ();

	/* Close everything */
	gdm_close_all_descriptors (0 /* from */, fds[0] /* except */, slave_fifo_pipe_fd /* except2 */);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	openlog ("gdm", LOG_PID, LOG_DAEMON);

	d->slave_notify_fd = fds[0];

	gdm_slave_start (d);
	/* should never retern */

	/* yaikes, how did we ever get here? */
	gdm_server_stop (d);
	_exit (DISPLAY_REMANAGE);

	break;

    case -1:
	d->slavepid = 0;
	gdm_error (_("%s: Failed forking gdm slave process for %s"),
		   "gdm_display_manage",
		   d->name);

	return FALSE;

    default:
	gdm_debug ("gdm_display_manage: Forked slave: %d",
		   (int)pid);
	d->master_notify_fd = fds[1];
	IGNORE_EINTR (close (fds[0]));
	break;
    }

    /* invalidate chosen hostname */
    g_free (d->chosen_hostname);
    d->chosen_hostname = NULL;

    /* use_chooser can only be temporary, if you want it permanent you set it up
       in the server definition with "chooser=true" and it will get set up during
       server command line resolution */
    d->use_chooser = FALSE;

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

    /* whack connections about this display */
    if (unixconn != NULL)
      gdm_kill_subconnections_with_display (unixconn, d);

    /* Kill slave, this may in fact hang for a bit at least until the
     * slave dies, which should be ASAP though */
    whack_old_slave (d);
    
    d->dispstat = DISPLAY_DEAD;
    if (d->type != TYPE_LOCAL)
	gdm_display_dispose (d);

    gdm_debug ("gdm_display_unmanage: Display stopped");
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

    if (d->slave_notify_fd >= 0) {
	    IGNORE_EINTR (close (d->slave_notify_fd));
	    d->slave_notify_fd = -1;
    }

    if (d->master_notify_fd >= 0) {
	    IGNORE_EINTR (close (d->master_notify_fd));
	    d->master_notify_fd = -1;
    }

    displays = g_slist_remove (displays, d);

    if (SERVER_IS_FLEXI (d))
	    flexi_servers --;

    if (d->type == TYPE_XDMCP) {
	if (d->dispstat == XDMCP_MANAGED)
		xdmcp_sessions--;
	else if (d->dispstat == XDMCP_PENDING)
		xdmcp_pending--;

	d->type = -1;
    }

    if (d->name) {
	gdm_debug ("gdm_display_dispose: Disposing %s", d->name);
	g_free (d->name);
	d->name = NULL;
    }
    
    g_free (d->chosen_hostname);
    d->chosen_hostname = NULL;

    g_free (d->hostname);
    d->hostname = NULL;

    g_free (d->addrs);
    d->addrs = NULL;
    d->addr_count = 0;

    g_free (d->authfile);
    d->authfile = NULL;

    g_free (d->authfile_gdm);
    d->authfile_gdm = NULL;

    if (d->xnest_temp_auth_file != NULL) {
	    IGNORE_EINTR (unlink (d->xnest_temp_auth_file));
    }
    g_free (d->xnest_temp_auth_file);
    d->xnest_temp_auth_file = NULL;

    if (d->auths) {
	    gdm_auth_free_auth_list (d->auths);
	    d->auths = NULL;
    }

    if (d->local_auths) {
	    gdm_auth_free_auth_list (d->local_auths);
	    d->local_auths = NULL;
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

    g_free (d->xsession_errors_filename);
    d->xsession_errors_filename = NULL;

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
