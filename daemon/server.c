/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

extern gchar *GdmDisplayInit;
extern gchar *GdmServAuthDir;
extern gchar *GdmLogDir;
extern gint GdmXdmcp;

extern gchar **gdm_arg_munch (const gchar *p);
extern gboolean gdm_auth_secure_display (GdmDisplay *);
extern void gdm_debug (const gchar *, ...);
extern void gdm_error (const gchar *, ...);
extern gint gdm_display_manage (GdmDisplay *);
extern void gdm_xdmcp_close();

void gdm_server_start (GdmDisplay *d);
void gdm_server_stop (GdmDisplay *d);
void gdm_server_restart (GdmDisplay *d);
void gdm_server_usr1_handler (gint);
void gdm_server_alarm_handler (gint);
GdmDisplay *gdm_server_alloc (gint id, gchar *command);


GdmDisplay *d;
sigset_t mask, omask;


void
gdm_server_start (GdmDisplay *disp)
{
    struct sigaction usr1;
    sigset_t usr1mask;
    gchar *srvcmd = NULL;
    gchar **argv = NULL;
    int logfd;
    
    if (!disp)
	return;
    
    d = disp;
    
    gdm_debug ("gdm_server_start: %s", d->name);
    
    /* Catch USR1 from X server */
    usr1.sa_handler = gdm_server_usr1_handler;
    usr1.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&usr1.sa_mask);
    
    if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	gdm_error (_("gdm_server_start: Error setting up USR1 signal handler"));
	exit (SERVER_ABORT);
    }
    
    sigemptyset (&usr1mask);
    sigaddset (&usr1mask, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &usr1mask, NULL);
    
    /* Log all output from spawned programs to a file */
    logfd = open (g_strconcat (GdmLogDir, "/", d->name, ".log", NULL),
		  O_CREAT|O_TRUNC|O_APPEND|O_WRONLY, 0666);
    
    if (logfd != -1) {
	dup2 (logfd, 1);
	dup2 (logfd, 2);
    }
    else
	gdm_error (_("gdm_server_start: Could not open logfile for display %s!"), d->name);
    
    /* Just in case we have an old server hanging around */
    if (d->servpid) {
	gdm_debug ("gdm_server_start: Old server found (%d). Killing.", d->servpid);
	gdm_server_stop (d);
    }
    
    /* Secure display with cookie */
    gdm_auth_secure_display (d);
    setenv ("DISPLAY", d->name, TRUE);
    
    /* Fork into two processes. Parent remains the gdm process. Child
     * becomes the X server.  
     */
    
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
	    gdm_error (_("gdm_server_start: Error setting USR1 to SIG_IGN"));
	    exit (SERVER_ABORT);
	}
	
	srvcmd = g_strconcat (d->command, " -auth ", GdmServAuthDir, \
			      "/", d->name, ".Xauth ", 
			      d->name, NULL);
	
	gdm_debug ("gdm_server_start: '%s'", srvcmd);
	
	argv = gdm_arg_munch (srvcmd);
	g_free (srvcmd);
	
	setpgid (0, 0);
	
	execv (argv[0], argv);
	
	gdm_error (_("gdm_server_start: Xserver not found: %s"), d->command);
	
	exit (SERVER_ABORT);
	break;
	
    case -1:
	gdm_error (_("gdm_server_start: Can't fork Xserver process!"));
	d->servpid = 0;
	break;
	
    default:
	break;
    }
    
    d->servstat = SERVER_STARTED;
    
    /* Wait for X server to send ready signal */
    pause();
}


void
gdm_server_stop (GdmDisplay *d)
{
    gdm_debug ("gdm_server_stop: Server for %s going down!", d->name);
    
    kill (d->servpid, SIGTERM);
    waitpid (d->servpid, 0, 0);
    d->servpid = 0;
    d->servstat = SERVER_DEAD;
    
    if (unlink (d->authfile) == -1)
	gdm_error (_("gdm_server_stop: Could not unlink auth file: %s!"), strerror (errno));
}


void
gdm_server_restart (GdmDisplay *d)
{
    sigset_t usr1mask;
    struct sigaction usr1, sigalarm;
    
    gdm_debug ("gdm_server_restart: Server for %s restarting!", d->name);
    
    if (d->servpid && kill (d->servpid, 0) < 0) {
	gdm_debug ("gdm_server_restart: Old server for %s still alive. Killing!", d->name);
	gdm_server_stop (d);
	gdm_server_start (d);
    }
    
    /* Create new cookie */
    gdm_auth_secure_display (d);
    setenv ("DISPLAY", d->name, TRUE);
    
    /* Catch USR1 from X server */
    usr1.sa_handler = gdm_server_usr1_handler;
    usr1.sa_flags = SA_RESTART|SA_RESETHAND;
    sigemptyset (&usr1.sa_mask);
    
    if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	gdm_error (_("gdm_server_start: Error setting up USR1 signal handler"));
	exit (SERVER_ABORT);
    }
    
    sigemptyset (&usr1mask);
    sigaddset (&usr1mask, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &usr1mask, NULL);
    
    /* Reset X and force auth file reread. XCloseDisplay works on most
     * servers but just like young Zaphod, we play it safe */

    gdm_debug ("gdm_server_restart: Servpid=%d", d->servpid);
    kill (d->servpid, SIGHUP);
    
    d->servstat=SERVER_STARTED;
    
    /* We add a timeout in case the X server fails to start. This
     * might happen because X servers take a while to die, close their
     * sockets etc. If the old X server isn't completely dead, the new
     * one will fail and we'll hang here forever */
    
    sigalarm.sa_handler = gdm_server_alarm_handler;
    sigalarm.sa_flags = 0;
    sigemptyset (&sigalarm.sa_mask);
    
    if (sigaction (SIGALRM, &sigalarm, NULL) < 0) {
	gdm_error (_("gdm_server_restart: Error setting up ALARM signal handler"));
	return;
    }
    
    sigemptyset (&mask);
    sigaddset (&mask, SIGALRM);
    sigprocmask (SIG_UNBLOCK, &mask, &omask);
    
    alarm (10);
    
    /* Wait for X server to send ready signal */
    pause();
}


void 
gdm_server_alarm_handler (gint signal)
{
    /* Unset alarm and try again */
    alarm (0);
    sigprocmask (SIG_SETMASK, &omask, NULL); 
    
    waitpid (d->servpid, 0, WNOHANG);
    
    gdm_debug ("gdm_server_alarm_handler: Temporary server failure");
    gdm_server_restart (d);
}


void
gdm_server_usr1_handler (gint sig)
{
    sigset_t usr1mask;
    
    /* Unset alarm */
    alarm (0);
    sigprocmask (SIG_SETMASK, &omask, NULL);
    
    gdm_debug ("gdm_server_usr1_handler: Starting display %s!", d->name);
    
    d->servstat = SERVER_RUNNING;
    
    /* Block USR1 */
    sigemptyset (&usr1mask);
    sigaddset (&usr1mask, SIGUSR1);
    sigprocmask (SIG_BLOCK, &usr1mask, NULL);
    
    gdm_display_manage (d);
}


GdmDisplay * 
gdm_server_alloc (gint id, gchar *command)
{
    gchar *dname = g_new0 (gchar, 1024);
    gchar *hostname = g_new0 (gchar, 1024);
    GdmDisplay *d = g_new0 (GdmDisplay, 1);
    
    if (gethostname (hostname, 1023) == -1)
	return (NULL);

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
    d->type = DISPLAY_LOCAL;
    d->sessionid = 0;
    d->acctime = 0;
    d->dsp = NULL;
    
    g_free (dname);
    g_free (hostname);

    return (d);
}


/* EOF */
