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

#include "gdm.h"
#include "display.h"
#include "slave.h"
#include "misc.h"
#include "xdmcp.h"

static const gchar RCSid[]="$Id$";

/* External vars */
extern gint GdmXdmcp;
extern gint sessions;
extern GSList *displays;


/**
 * gdm_display_manage:
 * @d: Pointer to a GdmDisplay struct
 *
 * Manage (Initialize and start login session) display
 */

gint 
gdm_display_manage (GdmDisplay *d)
{
    sigset_t mask, omask;

    if (!d) 
	return FALSE;

    gdm_debug ("gdm_display_manage: Managing %s", d->name);

    /* If we have an old slave process hanging around, kill it */
    if (d->slavepid) {
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &mask, &omask); 

	kill (d->slavepid, SIGINT);
	waitpid (d->slavepid, 0, 0);

	sigprocmask (SIG_SETMASK, &omask, NULL);
    }

    /* Fork slave process */
    switch (d->slavepid = fork()) {

    case 0:

	setpgid (0, 0);

	/* Close XDMCP fd in slave process */
	if (GdmXdmcp)
	    gdm_xdmcp_close ();

	if (d->type == TYPE_LOCAL)
	    gdm_slave_start (d);

	if (d->type == TYPE_XDMCP && d->dispstat == XDMCP_MANAGED)
	    gdm_slave_start (d);

	break;

    case -1:
	gdm_error (_("gdm_display_manage: Failed forking gdm slave process for %d"), d->name);

	return FALSE;

    default:
	gdm_debug ("gdm_display_manage: Forked slave: %d", d->slavepid);
	break;
    }

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

    gdm_debug ("gdm_display_unmanage: Stopping %s", d->name);

    /* Kill slave and all its children */
    if (d->slavepid) {
	kill (-(d->slavepid), SIGTERM);
	waitpid (d->slavepid, 0, 0);
	d->slavepid = 0;
    }
    
    if (d->type == TYPE_LOCAL)
	d->dispstat = DISPLAY_DEAD;
    else /* TYPE_XDMCP */
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
    if (!d)
	return;

    if (d->type == TYPE_XDMCP) {
	displays = g_slist_remove (displays, d);
	sessions--;
	d->type = -1;
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

    if (d->auths) {
	GSList *tmpauth = d->auths;

	while (tmpauth && tmpauth->data) {
	    XauDisposeAuth ((Xauth *) tmpauth->data);
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
    GSList *list = displays;
    GdmDisplay *d = NULL;

    /* Find slave in display list */
    while (list && list->data) {
	d = list->data;

	if (pid == d->slavepid) {
	    return d;

	    list = list->next;
	}
    }
     
    /* Slave not found */
    return NULL;
}


/* EOF */
