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
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <X11/Xlib.h>

#include <vicious.h>

#include "gdm.h"
#include "server.h"
#include "misc.h"
#include "xdmcp.h"
#include "display.h"
#include "auth.h"
#include "slave.h"

#define SERVER_WAIT_ALARM 10


/* Local prototypes */
static void gdm_server_spawn (GdmDisplay *d);
static void gdm_server_usr1_handler (gint);
static void gdm_server_alarm_handler (gint);
static void gdm_server_child_handler (gint);

/* Configuration options */
extern gchar *GdmDisplayInit;
extern gchar *GdmServAuthDir;
extern gchar *GdmLogDir;
extern gchar *GdmStandardXServer;
extern gboolean GdmXdmcp;
extern sigset_t sysmask;
extern gint high_display_num;
extern pid_t extra_process;
extern int extra_status;

/* Global vars */
static GdmDisplay *d = NULL;

/* Wipe cookie files */
void
gdm_server_wipe_cookies (GdmDisplay *disp)
{
	if ( ! ve_string_empty (disp->authfile))
		unlink (disp->authfile);
	g_free (disp->authfile);
	disp->authfile = NULL;
	if ( ! ve_string_empty (disp->authfile_gdm))
		unlink (disp->authfile_gdm);
	g_free (disp->authfile_gdm);
	disp->authfile_gdm = NULL;
}

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
	if (disp == NULL)
		return;

	/* Kill our connection if one existed */
	if (disp->dsp != NULL) {
		XCloseDisplay (disp->dsp);
		disp->dsp = NULL;
	}

	if (disp->servpid <= 0)
		return;

	gdm_debug ("gdm_server_reinit: Server for %s is about to be reinitialized!", disp->name);

	kill (disp->servpid, SIGHUP);
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
    if (disp == NULL)
	return;

    /* Kill our connection if one existed */
    if (disp->dsp != NULL) {
	    XCloseDisplay (disp->dsp);
	    disp->dsp = NULL;
    }

    if (disp->servpid <= 0)
	    return;

    gdm_debug ("gdm_server_stop: Server for %s going down!", disp->name);

    disp->servstat = SERVER_DEAD;

    if (disp->servpid != 0) {
	    pid_t servpid = disp->servpid;

	    /* avoid SIGCHLD race */
	    disp->servpid = 0;

	    gdm_debug ("gdm_server_stop: Killing server pid %d", (int)servpid);

	    gdm_sigchld_block_push ();
	    if (servpid > 0 &&
		kill (servpid, SIGTERM) == 0)
		    waitpid (servpid, 0, 0);
	    gdm_sigchld_block_pop ();


	    gdm_debug ("gdm_server_stop: Server pid %d dead", (int)servpid);
    }

    gdm_server_wipe_cookies (disp);
}

static gboolean
busy_ask_user (GdmDisplay *disp)
{
    /* if we have "open" we can talk to the user */
    if (access (EXPANDED_SBINDIR "/gdmopen", X_OK) == 0) {
	    char *error = g_strdup_printf
		    (_("There already appears to be an X server "
		       "running on display %s.  Should I try another "
		       "display number?  If you answer no, I will "
		       "attempt to start the server on %s again.%s"),
		     disp->name,
		     disp->name,
#ifdef __linux__
		     _("  (You can change consoles by pressing Ctrl-Alt "
		       "plus a function key, such as Ctrl-Alt-F7 to go "
		       "to console 7.  X servers usually run on consoles "
		       "7 and higher.)")
#else /* ! __linux__ */
		     /* no info for non linux users */
		     ""
#endif /* __linux__ */
		     );
	    gboolean ret = TRUE;
	    /* default ret to TRUE */
	    if ( ! gdm_text_yesno_dialog (error, &ret))
		    ret = TRUE;
	    g_free (error);
	    return ret;
    } else {
	    /* Well we'll just try another display number */
	    return TRUE;
    }
}

static gboolean
display_xnest_no_connect (GdmDisplay *disp)
{
	char *logname = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);
	FILE *fp;
	char buf[256];

	fp = fopen (logname, "r");
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		/* Note: this is probably XFree86 specific, and perhaps even
		 * version 3 specific (I don't have xfree v4 to test this),
		 * of course additions are welcome to make this more robust */
		if (strstr (buf, "Unable to open display \"") == buf) {
			gdm_error (_("Display '%s' cannot be opened by Xnest"),
				   ve_sure_string (disp->xnest_disp));
			fclose (fp);
			return TRUE;
		}
	}

	fclose (fp);
	return FALSE;
}

static gboolean
display_busy (GdmDisplay *disp)
{
	char *logname = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);
	FILE *fp;
	char buf[256];

	fp = fopen (logname, "r");
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		/* Note: this is probably XFree86 specific, and perhaps even
		 * version 3 specific (I don't have xfree v4 to test this),
		 * of course additions are welcome to make this more robust */
		if (strstr (buf, "Server is already active for display")
		    == buf) {
			gdm_error (_("Display %s is busy, there is another "
				     "X server already running"),
				   disp->name);
			fclose (fp);
			return TRUE;
		}
	}

	fclose (fp);
	return FALSE;
}

#ifdef __linux__
static int
display_vt (GdmDisplay *disp)
{
	char *logname = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);
	FILE *fp;
	char buf[256];

	fp = fopen (logname, "r");
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		int vt;
		/* Note: this is probably XFree86 specific, and perhaps even
		 * version 3 specific (I don't have xfree v4 to test this),
		 * of course additions are welcome to make this more robust */
		if (sscanf (buf, "(using VT number %d)", &vt) == 1) {
			fclose (fp);
			return vt;
		}
	}
	fclose (fp);
	return -1;
}
#endif

/**
 * gdm_server_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X server. Handles retries and fatal errors properly.
 */

gboolean
gdm_server_start (GdmDisplay *disp, gboolean treat_as_flexi,
		  int min_flexi_disp, int flexi_retries)
{
    struct sigaction usr1, chld, alrm;
    struct sigaction old_usr1, old_chld, old_alrm;
    sigset_t mask, oldmask;
    int flexi_disp = 20;
    
    if (disp == NULL)
	    return FALSE;

    d = disp;

    /* if an X server exists, wipe it */
    gdm_server_stop (d);

    /* On linux first clear the VT number */
#ifdef __linux__
    if (d->type == TYPE_LOCAL ||
	d->type == TYPE_FLEXI) {
	    d->vt = -1;
	    gdm_slave_send_num (GDM_SOP_VT_NUM, -1);
    }
#endif

    if (SERVER_IS_FLEXI (d) ||
	treat_as_flexi) {
	    flexi_disp = gdm_get_free_display
		    (MAX (high_display_num + 1, min_flexi_disp) /* start */,
		     d->server_uid /* server uid */);

	    g_free (d->name);
	    d->name = g_strdup_printf (":%d", flexi_disp);
	    d->dispnum = flexi_disp;

	    gdm_slave_send_num (GDM_SOP_DISP_NUM, flexi_disp);
    }


    gdm_debug ("gdm_server_start: %s", d->name);

    /* Create new cookie */
    if ( ! gdm_auth_secure_display (d))
	    return FALSE;
    gdm_slave_send_string (GDM_SOP_COOKIE, d->cookie);
    ve_setenv ("DISPLAY", d->name, TRUE);

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

    /* Only do alarm if esrver will be run as root */
    if (d->server_uid == 0) {
	    alarm (SERVER_WAIT_ALARM);
    }

    d->servstat = SERVER_DEAD;

    /* fork X server process */
    gdm_server_spawn (d);

    /* Wait for X server to send ready signal */
    if (d->servstat == SERVER_STARTED) {
	    if (d->server_uid != 0) {
		    int i;

		    /* if we're running the server as a non-root, we can't
		     * use USR1 of course, so try openning the display 
		     * as a test, but the */

		    ve_setenv ("XAUTHORITY", d->authfile, TRUE);
		    for (i = 0;
			 d->dsp == NULL &&
			 d->servstat == SERVER_STARTED &&
			 i < SERVER_WAIT_ALARM;
			 i++) {
			    d->dsp = XOpenDisplay (d->name);
			    if (d->dsp == NULL)
				    sleep (1);
			    else
				    d->servstat = SERVER_RUNNING;
		    }
		    ve_unsetenv ("XAUTHORITY");
		    if (d->dsp == NULL &&
			/* Note: we could have still gotten a SIGCHLD */
			d->servstat == SERVER_STARTED) {
			    d->servstat = SERVER_TIMEOUT;
		    }
	    } else {
		    gdm_run ();
	    }
    }

    /* Unset alarm */
    if (d->server_uid == 0) {
	    alarm (0);
    }

    switch (d->servstat) {

    case SERVER_TIMEOUT:
	    gdm_debug ("gdm_server_start: Temporary server failure (%s)", d->name);
	    break;

    case SERVER_ABORT:
	    gdm_debug ("gdm_server_start: Server %s died during startup!", d->name);
	    break;

    case SERVER_RUNNING:
	    gdm_debug ("gdm_server_start: Completed %s!", d->name);

	    sigprocmask (SIG_SETMASK, &oldmask, NULL);

	    /* restore default handlers */
	    sigaction (SIGUSR1, &old_usr1, NULL);
	    sigaction (SIGCHLD, &old_chld, NULL);
	    sigaction (SIGALRM, &old_alrm, NULL);

	    if (SERVER_IS_FLEXI (d))
		    gdm_slave_send_num (GDM_SOP_FLEXI_OK, 0 /* bogus */);
#ifdef __linux__
	    if (d->type == TYPE_LOCAL ||
		d->type == TYPE_FLEXI) {
		    d->vt = display_vt (d);
		    if (d->vt >= 0)
			    gdm_slave_send_num (GDM_SOP_VT_NUM, d->vt);
	    }
#endif

	    return TRUE;
    default:
	    break;
    }

    /* bad things are happening */
    if (d->servpid > 0) {
	    pid_t pid = d->servpid;
	    d->servpid = 0;
	    gdm_sigchld_block_push ();
	    if (pid > 0 &&
		kill (pid, SIGTERM) == 0)
		    waitpid (pid, NULL, 0);
	    gdm_sigchld_block_pop ();
    }

    /* We will rebake cookies anyway, so wipe these */
    gdm_server_wipe_cookies (disp);

    sigprocmask (SIG_SETMASK, &oldmask, NULL);

    /* restore default handlers */
    sigaction (SIGUSR1, &old_usr1, NULL);
    sigaction (SIGCHLD, &old_chld, NULL);
    sigaction (SIGALRM, &old_alrm, NULL);

    if (disp->type == TYPE_FLEXI_XNEST &&
	display_xnest_no_connect (disp)) {
	    gdm_slave_send_num (GDM_SOP_FLEXI_ERR,
				5 /* Xnest can't connect */);
	    _exit (DISPLAY_REMANAGE);
    }

    /* if this was a busy fail, that is, there is already
     * a server on that display, we'll display an error and after
     * this we'll exit with DISPLAY_REMANAGE to try again if the
     * user wants to, or abort this display */
    if (display_busy (disp)) {
	    if (SERVER_IS_FLEXI (disp) ||
		treat_as_flexi) {
		    /* for flexi displays, try again a few times with different
		     * display numbers */
		    if (flexi_retries <= 0) {
			    /* Send X too busy */
			    gdm_error (_("%s: Cannot find a free "
					 "display number"),
				       "gdm_server_start");
			    if (SERVER_IS_FLEXI (disp)) {
				    gdm_slave_send_num (GDM_SOP_FLEXI_ERR,
							4 /* X too busy */);
			    }
			    /* eki eki */
			    _exit (DISPLAY_REMANAGE);
		    }
		    return gdm_server_start (d, treat_as_flexi,
					     flexi_disp + 1,
					     flexi_retries - 1);
	    } else {
		    if (busy_ask_user (disp)) {
			    gdm_error (_("%s: Display %s busy.  Trying "
					 "another display number."),
				       "gdm_server_start",
				       d->name);
			    d->busy_display = TRUE;
			    return gdm_server_start (d,
						     TRUE /* treat as flexi */,
						     high_display_num + 1,
						     flexi_retries - 1);
		    }
		    _exit (DISPLAY_REMANAGE);
	    }
    }

    _exit (DISPLAY_XFAILED);

    return FALSE;
}

/* Do things that require checking the log,
 * we really do need to get called a bit later, after all init is done
 * as things aren't written to disk before that */
void
gdm_server_checklog (GdmDisplay *disp)
{
#ifdef __linux__
	if (d->vt < 0 &&
	    (d->type == TYPE_LOCAL ||
	     d->type == TYPE_FLEXI)) {
		d->vt = display_vt (d);
		if (d->vt >= 0)
			gdm_slave_send_num (GDM_SOP_VT_NUM, d->vt);
	}
#endif
}

static void
rotate_logs (const char *dname)
{
	/* I'm too lazy to write a loop damnit */
	char *fname4 = g_strconcat (GdmLogDir, "/", dname, ".log.4", NULL);
	char *fname3 = g_strconcat (GdmLogDir, "/", dname, ".log.3", NULL);
	char *fname2 = g_strconcat (GdmLogDir, "/", dname, ".log.2", NULL);
	char *fname1 = g_strconcat (GdmLogDir, "/", dname, ".log.1", NULL);
	char *fname = g_strconcat (GdmLogDir, "/", dname, ".log", NULL);

	/* Rotate the logs (keep 4 last) */
	unlink (fname4);
	rename (fname3, fname4);
	rename (fname2, fname3);
	rename (fname1, fname2);
	rename (fname, fname1);

	g_free (fname4);
	g_free (fname3);
	g_free (fname2);
	g_free (fname1);
	g_free (fname);
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
    struct sigaction ign_signal, dfl_signal;
    sigset_t mask;
    gchar *srvcmd = NULL;
    gchar **argv = NULL;
    int logfd;
    int len, i;
    const char *command;
    char *bin;
    pid_t pid;

    if (d == NULL ||
	ve_string_empty (d->command)) {
	    return;
    }

    d->servstat = SERVER_STARTED;

    gdm_sigchld_block_push ();

    /* eek, some previous copy, just wipe it */
    if (d->servpid > 0) {
	    pid_t pid = d->servpid;
	    d->servpid = 0;
	    if (kill (pid, SIGTERM) == 0)
		    waitpid (pid, NULL, 0);
    }

    /* Fork into two processes. Parent remains the gdm process. Child
     * becomes the X server. */

    gdm_sigterm_block_push ();
    pid = d->servpid = fork ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();
    
    switch (pid) {
	
    case 0:
        alarm (0);

	/* Close the XDMCP fd inherited by the daemon process */
	if (GdmXdmcp)
		gdm_xdmcp_close();

	/* close things */
	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
		close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

	/* Rotate the X server logs */
	rotate_logs (d->name);

        /* Log all output from spawned programs to a file */
	logfd = open (g_strconcat (GdmLogDir, "/", d->name, ".log", NULL),
		      O_CREAT|O_TRUNC|O_WRONLY, 0644);

	if (logfd != -1) {
		dup2 (logfd, 1);
		dup2 (logfd, 2);
        } else {
		gdm_error (_("gdm_server_spawn: Could not open logfile for display %s!"), d->name);
	}

	
	/* The X server expects USR1/TTIN/TTOU to be SIG_IGN */
	ign_signal.sa_handler = SIG_IGN;
	ign_signal.sa_flags = SA_RESTART;
	sigemptyset (&ign_signal.sa_mask);

	if (sigaction (SIGUSR1, &ign_signal, NULL) < 0) {
		gdm_error (_("gdm_server_spawn: Error setting USR1 to SIG_IGN"));
		_exit (SERVER_ABORT);
	}
	if (sigaction (SIGTTIN, &ign_signal, NULL) < 0) {
		gdm_error (_("gdm_server_spawn: Error setting TTIN to SIG_IGN"));
		_exit (SERVER_ABORT);
	}
	if (sigaction (SIGTTOU, &ign_signal, NULL) < 0) {
		gdm_error (_("gdm_server_spawn: Error setting TTOU to SIG_IGN"));
		_exit (SERVER_ABORT);
	}

	/* And HUP and TERM should be at default */
	dfl_signal.sa_handler = SIG_DFL;
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

	if (d->type == TYPE_FLEXI_XNEST) {
		ve_setenv ("DISPLAY", d->xnest_disp, TRUE);
		if (d->xnest_auth_file != NULL)
			ve_setenv ("XAUTHORITY", d->xnest_auth_file, TRUE);
		else
			ve_unsetenv ("XAUTHORITY");
	}

	bin = ve_first_word (d->command);
	if (bin == NULL) {
		gdm_error (_("Invalid server command '%s'"), d->command);
		argv = ve_split (GdmStandardXServer);
		command = GdmStandardXServer;
	} else if (bin[0] != '/') {
		GdmXServer *svr = gdm_find_x_server (bin);
		if (svr == NULL) {
			gdm_error (_("Server name '%s' not found, "
				     "using standard server"), bin);
			argv = ve_split (GdmStandardXServer);
			command = GdmStandardXServer;
		} else {
			char **svr_command =
				ve_split (ve_sure_string (svr->command));
			argv = ve_split (d->command);
			if (argv[0] == NULL || argv[1] == NULL) {
				g_strfreev (argv);
				argv = svr_command;
			} else {
				char **old_argv = argv;
				argv = ve_vector_merge (svr_command,
							&old_argv[1]);
				g_strfreev (svr_command);
				g_strfreev (old_argv);
			}
			/* this is only for information only,
			 * so doesn't include whole command line */
			command = svr->command;
		}
	} else {
		command = d->command;
		argv = ve_split (d->command);
	}
	g_free (bin);

	for (len = 0; argv != NULL && argv[len] != NULL; len++)
		;

	if (len == 0) {
		gdm_error (_("%s: Empty server command for display %s"),
			   "gdm_server_spawn",
			   d->name);
		_exit (SERVER_ABORT);
	}

	argv = g_renew (char *, argv, len + 4);
	for (i = len - 1; i >= 1; i--) {
		argv[i+1] = argv[i];
	}
	/* server number is the FIRST argument, before any others */
	argv[1] = d->name;
	argv[len+1] = "-auth";
	argv[len+2] = d->authfile;
	argv[len+3] = NULL;

	srvcmd = g_strjoinv (" ", argv);
	gdm_debug ("gdm_server_spawn: '%s'", srvcmd);
	g_free (srvcmd);
	
	setpgid (0, 0);

	if (d->server_uid != 0) {
		struct passwd *pwent;
		pwent = getpwuid (d->server_uid);
		if (pwent == NULL) {
			gdm_error (_("%s: Server was to be spawned by uid %d but "
				     "that user doesn't exist"),
				   "gdm_server_spawn",
				   (int)d->server_uid);
			_exit (SERVER_ABORT);
		}
		if (pwent->pw_dir != NULL &&
		    g_file_exists (pwent->pw_dir))
			ve_setenv ("HOME", pwent->pw_dir, TRUE);
		else
			ve_setenv ("HOME", "/", TRUE); /* Hack */
		ve_setenv ("SHELL", pwent->pw_shell, TRUE);
		ve_unsetenv ("MAIL");

		if (setgid (pwent->pw_gid) < 0)  {
			gdm_error (_("%s: Couldn't set groupid to %d"), 
				   "gdm_server_spawn", (int)pwent->pw_gid);
			_exit (SERVER_ABORT);
		}

		if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
			gdm_error (_("%s: initgroups() failed for %s"),
				   "gdm_server_spawn", pwent->pw_name);
			_exit (SERVER_ABORT);
		}

		if (setuid (d->server_uid) < 0)  {
			gdm_error (_("%s: Couldn't set userid to %d"),
				   "gdm_server_spawn", (int)d->server_uid);
			_exit (SERVER_ABORT);
		}
	} else {
		gid_t groups[1] = { 0 };
		if (setgid (0) < 0)  {
			gdm_error (_("%s: Couldn't set groupid to 0"), 
				   "gdm_server_spawn");
			/* Don't error out, it's not fatal, if it fails we'll
			 * just still be */
		}
		/* this will get rid of any suplementary groups etc... */
		setgroups (1, groups);
	}

	execv (argv[0], argv);
	
	gdm_error (_("gdm_server_spawn: Xserver not found: %s"), command);
	
	_exit (SERVER_ABORT);
	
    case -1:
	gdm_error (_("gdm_server_spawn: Can't fork Xserver process!"));
	d->servpid = 0;
	d->servstat = SERVER_DEAD;

	break;
	
    default:
	gdm_debug ("gdm_server_spawn: Forked server on pid %d", (int)pid);
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
    d->starttime = time (NULL);

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
gdm_server_child_handler (int signal)
{
	int status;
	pid_t pid;

	gdm_debug ("gdm_server_child_handler: Got SIGCHLD");

	while ((pid = waitpid (-1, &status, WNOHANG)) > 0) {
		gdm_debug ("gdm_server_child_handler: %d died", pid);

		if (WIFEXITED (status))
			gdm_debug ("gdm_server_child_handler: %d returned %d",
				   (int)pid, (int)WEXITSTATUS (status));
		if (WIFSIGNALED (status))
			gdm_debug ("gdm_server_child_handler: %d died of %d",
				   (int)pid, (int)WTERMSIG (status));

		if (pid == d->servpid) {
			gdm_debug ("gdm_server_child_handler: Got SIGCHLD from server, "
				   "server abort");

			d->servstat = SERVER_ABORT;	/* Server died unexpectedly */
			d->servpid = 0;

			gdm_quit ();
		} else if (pid == extra_process) {
			/* an extra process died, yay! */
			extra_process = -1;
			extra_status = status;
		}
	}
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
    gchar hostname[1024];
    GdmDisplay *d;
    
    if (gethostname (hostname, 1023) == -1)
	return NULL;

    d = g_new0 (GdmDisplay, 1);

    d->authfile = NULL;
    d->authfile_gdm = NULL;
    d->auths = NULL;
    d->userauth = NULL;
    d->command = g_strdup (command);
    d->cookie = NULL;
    d->dispstat = DISPLAY_UNBORN;
    d->greetpid = 0;
    d->name = g_strdup_printf (":%d", id);  
    d->hostname = g_strdup (hostname);
    /* Not really used for not XDMCP */
    memset (&(d->addr), 0, sizeof (struct in_addr));
    d->dispnum = id;
    d->servpid = 0;
    d->servstat = SERVER_DEAD;
    d->sessionid = 0;
    d->sesspid = 0;
    d->slavepid = 0;
    d->type = TYPE_LOCAL;
    d->console = TRUE;
    d->sessionid = 0;
    d->acctime = 0;
    d->dsp = NULL;
    d->screenx = 0; /* xinerama offset */
    d->screeny = 0;

#ifdef __linux__
    d->vt = -1;
#endif

    d->last_start_time = 0;
    d->retry_count = 0;
    d->disabled = FALSE;
    d->sleep_before_run = 0;
    d->login = NULL;

    d->timed_login_ok = FALSE;
    
    return d;
}

/* ignore handlers */
static int
ignore_xerror_handler (Display *disp, XErrorEvent *evt)
{
	return 0;
}

void
gdm_server_whack_clients (GdmDisplay *disp)
{
	int i, screen_count;
	int (* old_xerror_handler) (Display *, XErrorEvent *);

	if (disp == NULL ||
	    disp->dsp == NULL)
		return;

	old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);

	XGrabServer (disp->dsp);

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

	XUngrabServer (disp->dsp);

	XSync (disp->dsp, False);
	XSetErrorHandler (old_xerror_handler);
}


/* EOF */
