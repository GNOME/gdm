/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* This file contains functions for controlling local X servers */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>
#include <X11/Xlib.h>

#include "gdm.h"
#include "server.h"
#include "misc.h"
#include "xdmcp.h"
#include "display.h"
#include "auth.h"

static const gchar RCSid[]="$Id$";

#define SERVER_WAIT_ALARM 10


/* Local prototypes */
static void gdm_server_spawn (GdmDisplay *d);
static void gdm_server_usr1_handler (gint);
static void gdm_server_alarm_handler (gint);
static void gdm_server_child_handler (gint);

/* Configuration options */
extern gchar *argdelim;
extern gchar *GdmDisplayInit;
extern gchar *GdmServAuthDir;
extern gchar *GdmLogDir;
extern gboolean GdmXdmcp;
extern sigset_t sysmask;

/* Global vars */
static GdmDisplay *d = NULL;

/**
 * gdm_server_reinit:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Reinit the display, basically sends a HUP signal
 * but only if the display exists
 */

void
gdm_server_reinit (GdmDisplay *disp)
{
	gboolean had_connection = FALSE;

	if (disp == NULL ||
	    disp->servpid <= 0)
		return;

	/* Kill our connection */
	if (disp->dsp != NULL) {
		XCloseDisplay (disp->dsp);
		disp->dsp = NULL;
		had_connection = TRUE;
	}

	gdm_debug ("gdm_server_reinit: Server for %s is about to be reinitialized!", disp->name);

	kill (disp->servpid, SIGHUP);

	if (had_connection)
		disp->dsp = XOpenDisplay (disp->name);
}

/**
 * gdm_server_stop:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Stops a local X server, but only if it exists
 */

void
gdm_server_stop (GdmDisplay *disp)
{
    if (disp == NULL ||
	disp->servpid <= 0)
	return;

    gdm_debug ("gdm_server_stop: Server for %s going down!", disp->name);

    /* Kill our connection */
    if (disp->dsp != NULL) {
	XCloseDisplay (disp->dsp);
	disp->dsp = NULL;
    }
    
    disp->servstat = SERVER_DEAD;

    if (disp->servpid != 0) {
	    if (kill (disp->servpid, SIGTERM) == 0)
		    waitpid (disp->servpid, 0, 0);
	    disp->servpid = 0;
    }

    unlink (disp->authfile);
}

/**
 * gdm_server_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X server. Handles retries and fatal errors properly.
 */

gboolean
gdm_server_start (GdmDisplay *disp)
{
    struct sigaction usr1, chld, alrm;
    struct sigaction old_usr1, old_chld, old_alrm;
    sigset_t mask, oldmask;
    gboolean retvalue;
    
    if (!disp)
	return FALSE;
    
    d = disp;

    /* if an X server exists, wipe it */
    gdm_server_stop (d);

    gdm_debug ("gdm_server_start: %s", d->name);

    /* Create new cookie */
    gdm_auth_secure_display (d);
    gdm_setenv ("DISPLAY", d->name);
    
    /* Catch USR1 from X server */
    usr1.sa_handler = gdm_server_usr1_handler;
    usr1.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&usr1.sa_mask);
    
    if (sigaction (SIGUSR1, &usr1, &old_usr1) < 0) {
	gdm_error (_("gdm_server_start: Error setting up USR1 signal handler"));
	return FALSE;
    }

    /* Catch CHLD from X server */
    chld.sa_handler = gdm_server_child_handler;
    chld.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&chld.sa_mask);
    
    if (sigaction (SIGCHLD, &chld, &old_chld) < 0) {
	gdm_error (_("gdm_server_start: Error setting up CHLD signal handler"));
	sigaction (SIGUSR1, &old_usr1, NULL);
	return FALSE;
    }

    /* Catch ALRM from X server */
    alrm.sa_handler = gdm_server_alarm_handler;
    alrm.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&alrm.sa_mask);
    
    if (sigaction (SIGALRM, &alrm, &old_alrm) < 0) {
	gdm_error (_("gdm_server_start: Error setting up ALRM signal handler"));
	sigaction (SIGUSR1, &old_usr1, NULL);
	sigaction (SIGCHLD, &old_chld, NULL);
	return FALSE;
    }
    
    /* Set signal mask */
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigaddset (&mask, SIGCHLD);
    sigaddset (&mask, SIGALRM);
    sigprocmask (SIG_UNBLOCK, &mask, &oldmask);
    
    /* We add a timeout in case the X server fails to start. This
     * might happen because X servers take a while to die, close their
     * sockets etc. If the old X server isn't completely dead, the new
     * one will fail and we'll hang here forever */

    alarm (SERVER_WAIT_ALARM);

    d->servstat = SERVER_DEAD;
    
    /* fork X server process */
    gdm_server_spawn (d);

    retvalue = FALSE;

    /* Wait for X server to send ready signal */
    if (d->servstat == SERVER_STARTED)
	    gdm_run ();

    /* Unset alarm */
    alarm (0);

    switch (d->servstat) {

    case SERVER_TIMEOUT:
	    gdm_debug ("gdm_server_start: Temporary server failure (%d)", d->name);
	    break;

    case SERVER_ABORT:
	    gdm_debug ("gdm_server_start: Server %s died during startup!", d->name);
	    break;

    case SERVER_RUNNING:
	    gdm_debug ("gdm_server_start: Completed %s!", d->name);

	    retvalue = TRUE;
	    goto spawn_done;

    default:
	    break;
    }

    /* bad things are happening */
    if (d->servpid > 0) {
	    if (kill (d->servpid, SIGTERM) == 0)
		    waitpid (d->servpid, NULL, 0);
	    d->servpid = 0;
    }
    _exit (DISPLAY_REMANAGE);

spawn_done:

    sigprocmask (SIG_SETMASK, &oldmask, NULL);

    /* restore default handlers */
    sigaction (SIGUSR1, &old_usr1, NULL);
    sigaction (SIGCHLD, &old_chld, NULL);
    sigaction (SIGALRM, &old_alrm, NULL);

    return retvalue;
}


/**
 * gdm_server_spawn:
 * @disp: Pointer to a GdmDisplay structure
 *
 * forks an actual X server process
 */

static void 
gdm_server_spawn (GdmDisplay *d)
{
    struct sigaction usr1, dfl_signal;
    sigset_t mask;
    gchar *srvcmd = NULL;
    gchar **argv = NULL;
    int logfd;

    if (!d)
	return;

    d->servstat = SERVER_STARTED;

    /* eek, some previous copy, just wipe it */
    if (d->servpid > 0) {
	    if (kill (d->servpid, SIGTERM) == 0)
		    waitpid (d->servpid, NULL, 0);
    }

    /* Fork into two processes. Parent remains the gdm process. Child
     * becomes the X server. */
    
    switch (d->servpid = fork()) {
	
    case 0:
        alarm (0);

	/* Close the XDMCP fd inherited by the daemon process */
	if (GdmXdmcp)
	    gdm_xdmcp_close();

        /* Log all output from spawned programs to a file */
	logfd = open (g_strconcat (GdmLogDir, "/", d->name, ".log", NULL),
		      O_CREAT|O_TRUNC|O_APPEND|O_WRONLY, 0666);

	if (logfd != -1) {
		dup2 (logfd, 1);
		dup2 (logfd, 2);
        } else {
		gdm_error (_("gdm_server_spawn: Could not open logfile for display %s!"), d->name);
	}

	
	/* The X server expects USR1 to be SIG_IGN */
	usr1.sa_handler = SIG_IGN;
	usr1.sa_flags = SA_RESTART;
	sigemptyset (&usr1.sa_mask);

	if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	    gdm_error (_("gdm_server_spawn: Error setting USR1 to SIG_IGN"));
	    _exit (SERVER_ABORT);
	}

	/* And HUP and TERM should be at default */
	dfl_signal.sa_handler = SIG_IGN;
	dfl_signal.sa_flags = SA_RESTART;
	sigemptyset (&dfl_signal.sa_mask);

	if (sigaction (SIGHUP, &dfl_signal, NULL) < 0) {
	    gdm_error (_("gdm_server_spawn: Error setting HUP to SIG_DFL"));
	    _exit (SERVER_ABORT);
	}
	if (sigaction (SIGTERM, &dfl_signal, NULL) < 0) {
	    gdm_error (_("gdm_server_spawn: Error setting TERM to SIG_DFL"));
	    _exit (SERVER_ABORT);
	}

	/* unblock HUP/TERM/USR1 so that we can control the
	 * X server */
	sigprocmask (SIG_SETMASK, &sysmask, NULL);
	sigemptyset (&mask);
	sigaddset (&mask, SIGUSR1);
	sigaddset (&mask, SIGHUP);
	sigaddset (&mask, SIGTERM);
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
	
	srvcmd = g_strconcat (d->command, " -auth ", GdmServAuthDir, \
			      "/", d->name, ".Xauth ", 
			      d->name, NULL);
	
	gdm_debug ("gdm_server_spawn: '%s'", srvcmd);
	
	argv = g_strsplit (srvcmd, argdelim, MAX_ARGS);
	g_free (srvcmd);
	
	setpgid (0, 0);
	execv (argv[0], argv);
	
	gdm_error (_("gdm_server_spawn: Xserver not found: %s"), d->command);
	
	_exit (SERVER_ABORT);
	
    case -1:
	gdm_error (_("gdm_server_spawn: Can't fork Xserver process!"));
	d->servpid = 0;
	d->servstat = SERVER_DEAD;

	break;
	
    default:
	break;
    }
}


/**
 * gdm_server_usr1_handler:
 * @sig: Signal value
 *
 * Received when the server is ready to accept connections
 */

static void
gdm_server_usr1_handler (gint sig)
{
    d->servstat = SERVER_RUNNING; /* Server ready to accept connections */

    gdm_debug ("gdm_server_usr1_handler: Got SIGUSR1, server running");

    gdm_quit ();
}


/**
 * gdm_server_alarm_handler:
 * @sig: Signal value
 *
 * Server start timeout handler
 */

static void 
gdm_server_alarm_handler (gint signal)
{
    d->servstat = SERVER_TIMEOUT; /* Server didn't start */

    gdm_debug ("gdm_server_alarm_handler: Got SIGALRM, server abort");

    gdm_quit ();
}


/**
 * gdm_server_child_handler:
 * @sig: Signal value
 *
 * Received when server died during startup
 */

static void 
gdm_server_child_handler (gint signal)
{
    gdm_debug ("gdm_server_child_handler: Got SIGCHLD, server abort");

    d->servstat = SERVER_ABORT;	/* Server died unexpectedly */
    d->servpid = 0;

    gdm_quit ();
}


/**
 * gdm_server_alloc:
 * @id: Local display number
 * @command: Command line for starting the X server
 *
 * Allocate display structure for a local X server
 */

GdmDisplay * 
gdm_server_alloc (gint id, const gchar *command)
{
    gchar *hostname = g_new0 (gchar, 1024);
    GdmDisplay *d = g_new0 (GdmDisplay, 1);
    
    if (gethostname (hostname, 1023) == -1)
	return NULL;

    d->authfile = NULL;
    d->auths = NULL;
    d->userauth = NULL;
    d->command = g_strdup (command);
    d->cookie = NULL;
    d->dispstat = DISPLAY_DEAD;
    d->greetpid = 0;
    d->name = g_strdup_printf (":%d", id);  
    d->hostname = g_strdup (hostname);
    d->dispnum = id;
    d->servpid = 0;
    d->servstat = SERVER_DEAD;
    d->sessionid = 0;
    d->sesspid = 0;
    d->slavepid = 0;
    d->type = TYPE_LOCAL;
    d->sessionid = 0;
    d->acctime = 0;
    d->dsp = NULL;

    d->last_start_time = 0;
    d->retry_count = 0;
    d->disabled = FALSE;
    d->sleep_before_run = 0;

    d->timed_login_ok = FALSE;
    
    g_free (hostname);

    return d;
}

/* ignore handlers */
static gint
ignore_xerror_handler (Display *disp, XErrorEvent *evt)
{
    return 0;
}

static gint
ignore_xioerror_handler (Display *disp)
{
    return 0;
}

void
gdm_server_whack_clients (GdmDisplay *disp)
{
	int i, screen_count;
	gint (* old_xerror_handler) (Display *, XErrorEvent *);
	gint (* old_xioerror_handler) (Display *);

	if (disp == NULL ||
	    disp->dsp == NULL)
		return;

	old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);
	old_xioerror_handler = XSetIOErrorHandler (ignore_xioerror_handler);

	screen_count = ScreenCount (disp->dsp);

	for (i = 0; i < screen_count; i++) {
		Window root_ret, parent_ret;
		Window *childs = NULL;
		unsigned int childs_count = 0;
		Window root = RootWindow (disp->dsp, i);

		while (XQueryTree (disp->dsp, root, &root_ret, &parent_ret,
				   &childs, &childs_count) &&
		       childs_count > 0) {
			int ii;

			for (ii = 0; ii < childs_count; ii++) {
				XKillClient (disp->dsp, childs[ii]);
			}

			XFree (childs);
		}
	}

	XSetErrorHandler (old_xerror_handler);
	XSetIOErrorHandler (old_xioerror_handler);
}


/* EOF */
