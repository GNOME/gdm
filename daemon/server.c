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

static const gchar RCSid[]="$Id$";

extern gchar *argdelim;
extern gchar *GdmDisplayInit;
extern gchar *GdmServAuthDir;
extern gchar *GdmLogDir;
extern gint GdmXdmcp;

extern gboolean gdm_auth_secure_display (GdmDisplay *);
extern void gdm_debug (const gchar *, ...);
extern void gdm_error (const gchar *, ...);
extern gint gdm_display_manage (GdmDisplay *);
extern void gdm_xdmcp_close();
extern gint gdm_setenv (gchar *var, gchar *value);

gboolean gdm_server_start (GdmDisplay *d);
void gdm_server_stop (GdmDisplay *d);
void gdm_server_spawn (GdmDisplay *d);
void gdm_server_usr1_handler (gint);
void gdm_server_alarm_handler (gint);
void gdm_server_child_handler (gint);
GdmDisplay *gdm_server_alloc (gint id, gchar *command);


GdmDisplay *d;
sigset_t mask, omask;


/**
 * gdm_server_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X server. Handles retries and fatal errors properly.
 */

gboolean
gdm_server_start (GdmDisplay *disp)
{
    struct sigaction usr1, chld, oldchld, alrm;
    sigset_t mask, omask;
    
    if (!disp)
	return FALSE;
    
    d = disp;
    d->servtries = 0;
    
    gdm_debug ("gdm_server_start: %s", d->name);
    
    /* Create new cookie */
    gdm_auth_secure_display (d);
    gdm_setenv ("DISPLAY", d->name);
    
    /* Catch USR1 from X server */
    usr1.sa_handler = gdm_server_usr1_handler;
    usr1.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&usr1.sa_mask);
    
    if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	gdm_error (_("gdm_server_start: Error setting up USR1 signal handler"));
	return FALSE;
    }
    
    /* Set signal mask */
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &mask, &omask);
    
    /* Is the old server (if any) still alive */
    if (d->servpid && kill (d->servpid, 0) == 0) {
	
	/* Reset X and force auth file reread. XCloseDisplay works on most
	 * servers but just like young Zaphod, we play it safe */
	
	gdm_debug ("gdm_server_start: HUP'ing servpid=%d", d->servpid);
	kill (d->servpid, SIGHUP);
	d->servstat = SERVER_STARTED;
    }
    else /* No server running */
	gdm_server_spawn (d);
	
    /* Set signal mask */
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigaddset (&mask, SIGCHLD);
    sigaddset (&mask, SIGALRM);
    sigprocmask (SIG_UNBLOCK, &mask, &omask);

    /* Wait for X server to send ready signal */
    while (d->servtries < 5) {
	pause();

	switch (d->servstat) {

	case SERVER_TIMEOUT:
	    /* Unset alarm and try again */
	    alarm (0);
	    sigprocmask (SIG_SETMASK, &omask, NULL); 
	    
	    waitpid (d->servpid, 0, WNOHANG);
	    
	    gdm_debug ("gdm_server_alarm_handler: Temporary server failure");
	    gdm_server_spawn (d);
	    
	    break;

	case SERVER_RUNNING:
	    /* Unset alarm */
	    alarm (0);
	    sigprocmask (SIG_SETMASK, &omask, NULL);
	    
	    gdm_debug ("gdm_server_run_session: Starting display %s!", d->name);
	    
	    return TRUE;

	default:
	    break;
	}
    }

    return FALSE;
}


void 
gdm_server_spawn (GdmDisplay *d)
{
    struct sigaction usr1, sigalarm, chld, ochld;
    gchar *srvcmd = NULL;
    gchar **argv = NULL;
    int logfd;

    if (!d)
	return;

    d->servstat = SERVER_STARTED;

    /* Catch CHLD from X server */
    chld.sa_handler = gdm_server_child_handler;
    chld.sa_flags = 0;
    sigemptyset (&chld.sa_mask);
    
    if (sigaction (SIGCHLD, &chld, &ochld) < 0) {
	gdm_error (_("gdm_server_start: Error setting up CHLD signal handler"));
	return;
    }

    /* If the old server is still alive, kill it */
    if (d->servpid && kill (d->servpid, 0) == 0)
	gdm_server_stop (d);

    /* Log all output from spawned programs to a file */
    logfd = open (g_strconcat (GdmLogDir, "/", d->name, ".log", NULL),
		  O_CREAT|O_TRUNC|O_APPEND|O_WRONLY, 0666);
    
    if (logfd != -1) {
	dup2 (logfd, 1);
	dup2 (logfd, 2);
    }
    else
	gdm_error (_("gdm_server_spawn: Could not open logfile for display %s!"), d->name);
    
    /* We add a timeout in case the X server fails to start. This
     * might happen because X servers take a while to die, close their
     * sockets etc. If the old X server isn't completely dead, the new
     * one will fail and we'll hang here forever */

    alarm (10);

    /* Fork into two processes. Parent remains the gdm process. Child
     * becomes the X server. */
    
    switch (d->servpid = fork()) {
	
    case 0:
	/* Close the XDMCP fd inherited by the daemon process */
	if (GdmXdmcp)
	    gdm_xdmcp_close();
	
	/* The X server expects USR1 to be SIG_IGN */
	usr1.sa_handler = SIG_IGN;
	usr1.sa_flags = SA_RESTART;
	sigemptyset (&usr1.sa_mask);
	
	if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	    gdm_error (_("gdm_server_spawn: Error setting USR1 to SIG_IGN"));
	    exit (SERVER_ABORT);
	}
	
	srvcmd = g_strconcat (d->command, " -auth ", GdmServAuthDir, \
			      "/", d->name, ".Xauth ", 
			      d->name, NULL);
	
	gdm_debug ("gdm_server_spawn: '%s'", srvcmd);
	
	argv = g_strsplit (srvcmd, argdelim, MAX_ARGS);
	g_free (srvcmd);
	
	setpgid (0, 0);
	execv (argv[0], argv);
	
	gdm_error (_("gdm_server_spawn: Xserver not found: %s"), d->command);
	
	exit (SERVER_ABORT);
	
    case -1:
	gdm_error (_("gdm_server_spawn: Can't fork Xserver process!"));
	d->servpid = 0;
	d->servstat = SERVER_DEAD;
	break;
	
    default:
	break;
    }
}


void
gdm_server_stop (GdmDisplay *d)
{
    gdm_debug ("gdm_server_stop: Server for %s going down!", d->name);
    
    d->servstat = SERVER_DEAD;

    kill (d->servpid, SIGTERM);
    waitpid (d->servpid, 0, 0);

    d->servpid = 0;
    unlink (d->authfile);
}


void
gdm_server_usr1_handler (gint sig)
{
    d->servstat = SERVER_RUNNING; /* Server ready to accept connections */
}


void 
gdm_server_alarm_handler (gint signal)
{
    d->servstat = SERVER_TIMEOUT; /* Server didn't start */
}


void 
gdm_server_child_handler (gint signal)
{
    d->servstat = SERVER_ABORT;	/* Server died unexpectedly */
}


GdmDisplay * 
gdm_server_alloc (gint id, gchar *command)
{
    gchar *dname = g_new0 (gchar, 1024);
    gchar *hostname = g_new0 (gchar, 1024);
    GdmDisplay *d = g_new0 (GdmDisplay, 1);
    
    if (gethostname (hostname, 1023) == -1)
	return NULL;

    sprintf (dname, ":%d", id);  
    d->authfile = NULL;
    d->auths = NULL;
    d->userauth = NULL;
    d->command = g_strdup (command);
    d->cookie = NULL;
    d->dispstat = DISPLAY_DEAD;
    d->greetpid = 0;
    d->name = g_strdup (dname);
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
    
    g_free (dname);
    g_free (hostname);

    return d;
}


/* EOF */
