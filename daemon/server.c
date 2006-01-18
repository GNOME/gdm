/* GDM - The GNOME Display Manager
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

#include "config.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <X11/Xlib.h>

#include "gdm.h"
#include "server.h"
#include "misc.h"
#include "xdmcp.h"
#include "display.h"
#include "auth.h"
#include "slave.h"
#include "getvt.h"
#include "gdmconfig.h"

#define SERVER_WAIT_ALARM 10

/* Local prototypes */
static void gdm_server_spawn (GdmDisplay *d, const char *vtarg);
static void gdm_server_usr1_handler (gint);
static void gdm_server_child_handler (gint);
static char * get_font_path (const char *display);

extern pid_t extra_process;
extern int extra_status;
extern int gdm_in_signal;

/* Global vars */
static GdmDisplay *d = NULL;
static gboolean server_signal_notified = FALSE;
static int server_signal_pipe[2];

static void do_server_wait (GdmDisplay *d);
static gboolean setup_server_wait (GdmDisplay *d);

void
gdm_server_whack_lockfile (GdmDisplay *disp)
{
	    char buf[256];

	    /* X seems to be sometimes broken with its lock files and
	       doesn't seem to remove them always, and if you manage
	       to get into the weird state where the old pid now
	       corresponds to some new pid, X will just die with
	       a stupid error. */

	    /* Yes there could be a race here if another X server starts
	       at this exact instant.  Oh well such is life.  Very unlikely
	       to happen though as we should really be the only ones
	       trying to start X servers, and we aren't starting an
	       X server on this display yet. */

	    /* if lock file exists and it is our process, whack it! */
	    g_snprintf (buf, sizeof (buf), "/tmp/.X%d-lock", disp->dispnum);
	    VE_IGNORE_EINTR (g_unlink (buf));

	    /* whack the unix socket as well */
	    g_snprintf (buf, sizeof (buf),
			"/tmp/.X11-unix/X%d", disp->dispnum);
	    VE_IGNORE_EINTR (g_unlink (buf));
}


/* Wipe cookie files */
void
gdm_server_wipe_cookies (GdmDisplay *disp)
{
	if ( ! ve_string_empty (disp->authfile)) {
		VE_IGNORE_EINTR (g_unlink (disp->authfile));
	}
	g_free (disp->authfile);
	disp->authfile = NULL;
	if ( ! ve_string_empty (disp->authfile_gdm)) {
		VE_IGNORE_EINTR (g_unlink (disp->authfile_gdm));
	}
	g_free (disp->authfile_gdm);
	disp->authfile_gdm = NULL;
}

static Jmp_buf reinitjmp;

/* ignore handlers */
static int
ignore_xerror_handler (Display *disp, XErrorEvent *evt)
{
	return 0;
}

static int
jumpback_xioerror_handler (Display *disp)
{
	Longjmp (reinitjmp, 1);
}

#ifdef HAVE_FBCONSOLE
#define FBCONSOLE "/usr/openwin/bin/fbconsole"

static void
gdm_exec_fbconsole (GdmDisplay *disp)
{
        pid_t pid;
        char *argv[6];

        argv[0] = FBCONSOLE;
        argv[1] = "-d";
        argv[2] = disp->name;
        argv[3] = NULL;

        pid = fork ();
        if (pid == 0) {
                gdm_close_all_descriptors (0 /* from */, -1 /* except */, -1 /* except2 */)
;
                VE_IGNORE_EINTR (execv (argv[0], argv));
        }
        if (pid == -1) {
                gdm_error (_("Can not start fallback console"));
        }
}
#endif

/**
 * gdm_server_reinit:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Reinit the display, basically sends a HUP signal
 * but only if the display exists
 */

gboolean
gdm_server_reinit (GdmDisplay *disp)
{
	if (disp == NULL)
		return FALSE;

	if (disp->servpid <= 0) {
		/* Kill our connection if one existed, likely to result
		 * in some bizzaro error right now */
		if (disp->dsp != NULL) {
			XCloseDisplay (disp->dsp);
			disp->dsp = NULL;
		}
		return FALSE;
	}

	gdm_debug ("gdm_server_reinit: Server for %s is about to be reinitialized!", disp->name);

	if ( ! setup_server_wait (disp))
		return FALSE;

	d->servstat = SERVER_PENDING;

	if (disp->dsp != NULL) {
		/* static because of the Setjmp */
		static int (*old_xerror_handler)(Display *, XErrorEvent *) = NULL;
		static int (*old_xioerror_handler)(Display *) = NULL;

		old_xerror_handler = NULL;
		old_xioerror_handler = NULL;

		/* Do note the interaction of this Setjmp and the signal
	   	   handlers and the Setjmp in slave.c */

		/* Long live Setjmp, DIE DIE DIE XSetIOErrorHandler */

		if (Setjmp (reinitjmp) == 0)  {
			/* come here and we'll whack the server and wait to get
			   an xio error */
			old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);
			old_xioerror_handler = XSetIOErrorHandler (jumpback_xioerror_handler);

			/* Now whack the server with a SIGHUP */
			gdm_sigchld_block_push ();
			if (disp->servpid > 1)
				kill (disp->servpid, SIGHUP);
			else
				d->servstat = SERVER_DEAD;
			gdm_sigchld_block_pop ();

			/* the server is dead, weird */
			if (disp->dsp != NULL) {
				XCloseDisplay (disp->dsp);
				disp->dsp = NULL;
			}
		}
		/* no more display */
		disp->dsp = NULL;
		XSetErrorHandler (old_xerror_handler);
		XSetIOErrorHandler (old_xioerror_handler);
	} else {
		/* Now whack the server with a SIGHUP */
		gdm_sigchld_block_push ();
		if (disp->servpid > 1)
			kill (disp->servpid, SIGHUP);
		else
			d->servstat = SERVER_DEAD;
		gdm_sigchld_block_pop ();
	}

	/* Wait for the SIGUSR1 */
	do_server_wait (d);

	if (d->servstat == SERVER_RUNNING) {
#ifdef HAVE_FBCONSOLE
		gdm_exec_fbconsole (d);
#endif
		return TRUE;
        } else {
		/* if something really REALLY screwed up, then whack the
		   lockfiles for safety */
		gdm_server_whack_lockfile (d);
		return FALSE;
	}
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
    static gboolean waiting_for_server = FALSE;
    int old_servstat;

    if (disp == NULL)
	return;

    /* Kill our connection if one existed */
    if (disp->dsp != NULL) {
	    /* on XDMCP servers first kill everything in sight */
	    if (disp->type == TYPE_XDMCP)
		    gdm_server_whack_clients (disp->dsp);
	    XCloseDisplay (disp->dsp);
	    disp->dsp = NULL;
    }

    /* Kill our parent connection if one existed */
    if (disp->parent_dsp != NULL) {
	    /* on XDMCP servers first kill everything in sight */
	    if (disp->type == TYPE_XDMCP_PROXY)
		    gdm_server_whack_clients (disp->parent_dsp);
	    XCloseDisplay (disp->parent_dsp);
	    disp->parent_dsp = NULL;
    }

    if (disp->servpid <= 0)
	    return;

    gdm_debug ("gdm_server_stop: Server for %s going down!", disp->name);

    old_servstat = disp->servstat;
    disp->servstat = SERVER_DEAD;

    if (disp->servpid > 0) {
	    pid_t servpid;

	    gdm_debug ("gdm_server_stop: Killing server pid %d",
		       (int)disp->servpid);

	    /* avoid SIGCHLD race */
	    gdm_sigchld_block_push ();
	    servpid = disp->servpid;

	    if (waiting_for_server) {
		    gdm_error ("gdm_server_stop: Some problem killing server, whacking with SIGKILL");
		    if (disp->servpid > 1)
			    kill (disp->servpid, SIGKILL);

	    } else {
		    if (disp->servpid > 1 &&
			kill (disp->servpid, SIGTERM) == 0) {
			    waiting_for_server = TRUE;
			    ve_waitpid_no_signal (disp->servpid, NULL, 0);
			    waiting_for_server = FALSE;
		    }
	    }
	    disp->servpid = 0;

	    gdm_sigchld_block_pop ();

	    if (old_servstat == SERVER_RUNNING)
		    gdm_server_whack_lockfile (disp);

	    gdm_debug ("gdm_server_stop: Server pid %d dead", (int)servpid);

	    /* just in case we restart again wait at least
	       one sec to avoid races */
	    if (d->sleep_before_run < 1)
		    d->sleep_before_run = 1;
    }

    gdm_server_wipe_cookies (disp);

    gdm_slave_whack_temp_auth_file ();
}

static gboolean
busy_ask_user (GdmDisplay *disp)
{
    /* if we have "open" we can talk to the user */
    if (g_access (EXPANDED_LIBEXECDIR "/gdmopen", X_OK) == 0) {
	    char *error = g_strdup_printf
		    (C_(N_("There already appears to be an X server "
			   "running on display %s.  Should another "
			   "display number by tried?  Answering no will "
			   "cause GDM to attempt starting the server "
			   "on %s again.%s")),
		     disp->name,
		     disp->name,
#ifdef __linux__
		     C_(N_("  (You can change consoles by pressing Ctrl-Alt "
			   "plus a function key, such as Ctrl-Alt-F7 to go "
			   "to console 7.  X servers usually run on consoles "
			   "7 and higher.)"))
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

/* Checks only output, no XFree86 v4 logfile */
static gboolean
display_parent_no_connect (GdmDisplay *disp)
{
	char *logname = gdm_make_filename (gdm_get_value_string (GDM_KEY_LOG_DIR), d->name, ".log");
	FILE *fp;
	char buf[256];
	char *getsret;

	VE_IGNORE_EINTR (fp = fopen (logname, "r"));
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	for (;;) {
		VE_IGNORE_EINTR (getsret = fgets (buf, sizeof (buf), fp));
		if (getsret == NULL) {
			VE_IGNORE_EINTR (fclose (fp));
			return FALSE;
		}
		/* Note: this is probably XFree86 specific, and perhaps even
		 * version 3 specific (I don't have xfree v4 to test this),
		 * of course additions are welcome to make this more robust */
		if (strstr (buf, "Unable to open display \"") == buf) {
			gdm_error (_("Display '%s' cannot be opened by Xnest"),
				   ve_sure_string (disp->parent_disp));
			VE_IGNORE_EINTR (fclose (fp));
			return TRUE;
		}
	}
}

static gboolean
display_busy (GdmDisplay *disp)
{
	char *logname = gdm_make_filename (gdm_get_value_string (GDM_KEY_LOG_DIR), d->name, ".log");
	FILE *fp;
	char buf[256];
	char *getsret;

	VE_IGNORE_EINTR (fp = fopen (logname, "r"));
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	for (;;) {
		VE_IGNORE_EINTR (getsret = fgets (buf, sizeof (buf), fp));
		if (getsret == NULL) {
			VE_IGNORE_EINTR (fclose (fp));
			return FALSE;
		}
		/* Note: this is probably XFree86 specific */
		if (strstr (buf, "Server is already active for display")
		    == buf) {
			gdm_error (_("Display %s is busy. There is another "
				     "X server running already."),
				   disp->name);
			VE_IGNORE_EINTR (fclose (fp));
			return TRUE;
		}
	}
}

/* if we find 'Log file: "foo"' switch fp to foo and
   return TRUE */
/* Note: assumes buf is of size 256 and is writable */ 
static gboolean
open_another_logfile (char buf[256], FILE **fp)
{
	if (strncmp (&buf[5], "Log file: \"", strlen ("Log file: \"")) == 0) {
		FILE *ffp;
		char *fname = &buf[5+strlen ("Log file: \"")];
		char *p = strchr (fname, '"');
		if (p == NULL)
			return FALSE;
		*p = '\0';
		VE_IGNORE_EINTR (ffp = fopen (fname, "r"));
		if (ffp == NULL)
			return FALSE;
		VE_IGNORE_EINTR (fclose (*fp));
		*fp = ffp;
		return TRUE;
	}
	return FALSE;
}

static int
display_vt (GdmDisplay *disp)
{
	char *logname = gdm_make_filename (gdm_get_value_string (GDM_KEY_LOG_DIR), d->name, ".log");
	FILE *fp;
	char buf[256];
	gboolean switched = FALSE;
	char *getsret;

	VE_IGNORE_EINTR (fp = fopen (logname, "r"));
	g_free (logname);

	if (fp == NULL)
		return FALSE;

	for (;;) {
		int vt;
		char *p;

		VE_IGNORE_EINTR (getsret = fgets (buf, sizeof (buf), fp));
		if (getsret == NULL) {
			VE_IGNORE_EINTR (fclose (fp));
			return -1;
		}

		if ( ! switched &&
		     /* this is XFree v4 specific */
		    open_another_logfile (buf, &fp)) {
			switched = TRUE;
			continue;
		} 
		/* Note: this is probably XFree86 specific (works with
		 * both v3 and v4 though */
		p = strstr (buf, "using VT number ");
		if (p != NULL &&
		    sscanf (p, "using VT number %d", &vt) == 1) {
			VE_IGNORE_EINTR (fclose (fp));
			return vt;
		}
	}
}

static struct sigaction old_svr_wait_chld;
static sigset_t old_svr_wait_mask;

static gboolean
setup_server_wait (GdmDisplay *d)
{
    struct sigaction usr1, chld;
    sigset_t mask;

    if (pipe (server_signal_pipe) != 0) {
	    gdm_error (_("%s: Error opening a pipe: %s"),
		       "setup_server_wait", strerror (errno));
	    return FALSE; 
    }
    server_signal_notified = FALSE;

    /* Catch USR1 from X server */
    usr1.sa_handler = gdm_server_usr1_handler;
    usr1.sa_flags = SA_RESTART;
    sigemptyset (&usr1.sa_mask);

    if (sigaction (SIGUSR1, &usr1, NULL) < 0) {
	    gdm_error (_("%s: Error setting up %s signal handler: %s"),
		       "gdm_server_start", "USR1", strerror (errno));
	    VE_IGNORE_EINTR (close (server_signal_pipe[0]));
	    VE_IGNORE_EINTR (close (server_signal_pipe[1]));
	    return FALSE;
    }

    /* Catch CHLD from X server */
    chld.sa_handler = gdm_server_child_handler;
    chld.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset (&chld.sa_mask);

    if (sigaction (SIGCHLD, &chld, &old_svr_wait_chld) < 0) {
	    gdm_error (_("%s: Error setting up %s signal handler: %s"),
		       "gdm_server_start", "CHLD", strerror (errno));
	    gdm_signal_ignore (SIGUSR1);
	    VE_IGNORE_EINTR (close (server_signal_pipe[0]));
	    VE_IGNORE_EINTR (close (server_signal_pipe[1]));
	    return FALSE;
    }

    /* Set signal mask */
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigaddset (&mask, SIGCHLD);
    sigprocmask (SIG_UNBLOCK, &mask, &old_svr_wait_mask);

    return TRUE;
}

static void
do_server_wait (GdmDisplay *d)
{
    /* Wait for X server to send ready signal */
    if (d->servstat == SERVER_PENDING) {
	    if (d->server_uid != 0 && ! d->handled && ! d->chosen_hostname) {
		    /* FIXME: If not handled, we just don't know, so
		     * just wait a few seconds and hope things just work,
		     * fortunately there is no such case yet and probably
		     * never will, but just for code anality's sake */
		    gdm_sleep_no_signal (5);
	    } else if (d->server_uid != 0) {
		    int i;

		    /* FIXME: This is not likely to work in reinit,
		       but we never reinit Xnest servers nowdays,
		       so that's fine */

		    /* if we're running the server as a non-root, we can't
		     * use USR1 of course, so try openning the display 
		     * as a test, but the */

		    /* just in case it's set */
		    g_unsetenv ("XAUTHORITY");

		    gdm_auth_set_local_auth (d);

		    for (i = 0;
			 d->dsp == NULL &&
			 d->servstat == SERVER_PENDING &&
			 i < SERVER_WAIT_ALARM;
			 i++) {
			    d->dsp = XOpenDisplay (d->name);
			    if (d->dsp == NULL)
				    gdm_sleep_no_signal (1);
			    else
				    d->servstat = SERVER_RUNNING;
		    }
		    if (d->dsp == NULL &&
			/* Note: we could have still gotten a SIGCHLD */
			d->servstat == SERVER_PENDING) {
			    d->servstat = SERVER_TIMEOUT;
		    }
	    } else {
		    time_t t = time (NULL);

		    gdm_debug ("do_server_wait: Before mainloop waiting for server");

		    do {
			    fd_set rfds;
			    struct timeval tv;

			    /* Wait up to SERVER_WAIT_ALARM seconds. */
			    tv.tv_sec = MAX (1, SERVER_WAIT_ALARM - (time (NULL) - t));
			    tv.tv_usec = 0;

			    FD_ZERO (&rfds);
			    FD_SET (server_signal_pipe[0], &rfds);

			    if (select (server_signal_pipe[0]+1, &rfds, NULL, NULL, &tv) > 0) {
				    char buf[4];
				    /* read the Yay! */
				    VE_IGNORE_EINTR (read (server_signal_pipe[0], buf, 4));
			    }
			    if ( ! server_signal_notified &&
				t + SERVER_WAIT_ALARM < time (NULL)) {
				    gdm_debug ("do_server_wait: Server timeout");
				    d->servstat = SERVER_TIMEOUT;
				    server_signal_notified = TRUE;
			    }
			    if (d->servpid <= 1) {
				    d->servstat = SERVER_ABORT;
				    server_signal_notified = TRUE;
			    }
		    } while ( ! server_signal_notified);

		    gdm_debug ("gdm_server_start: After mainloop waiting for server");
	    }
    }

    /* restore default handlers */
    gdm_signal_ignore (SIGUSR1);
    sigaction (SIGCHLD, &old_svr_wait_chld, NULL);
    sigprocmask (SIG_SETMASK, &old_svr_wait_mask, NULL);

    VE_IGNORE_EINTR (close (server_signal_pipe[0]));
    VE_IGNORE_EINTR (close (server_signal_pipe[1]));

    if (d->servpid <= 1) {
	    d->servstat = SERVER_ABORT;
    }

    if (d->servstat != SERVER_RUNNING) {
	    /* bad things are happening */
	    if (d->servpid > 0) {
		    pid_t pid;

		    d->dsp = NULL;

		    gdm_sigchld_block_push ();
		    pid = d->servpid;
		    d->servpid = 0;
		    if (pid > 1 &&
			kill (pid, SIGTERM) == 0)
			    ve_waitpid_no_signal (pid, NULL, 0);
		    gdm_sigchld_block_pop ();
	    }

	    /* We will rebake cookies anyway, so wipe these */
	    gdm_server_wipe_cookies (d);
    }
}

/* We keep a connection (parent_dsp) open with the parent X server
 * before running a proxy on it to prevent the X server resetting
 * as we open and close other connections.
 * Note that XDMCP servers, by default, reset when the seed X
 * connection closes whereas usually the X server only quits when
 * all X connections have closed.
 */
static gboolean
connect_to_parent (GdmDisplay *d)
{
	int maxtries;
	int openretries;

	gdm_debug ("gdm_server_start: Connecting to parent display \'%s\'",
		   d->parent_disp);

	d->parent_dsp = NULL;

	maxtries = SERVER_IS_XDMCP (d) ? 10 : 2;

	openretries = 0;
	while (openretries < maxtries &&
	       d->parent_dsp == NULL) {
		d->parent_dsp = XOpenDisplay (d->parent_disp);

		if G_UNLIKELY (d->parent_dsp == NULL) {
			gdm_debug ("gdm_server_start: Sleeping %d on a retry", 1+openretries*2);
			gdm_sleep_no_signal (1+openretries*2);
			openretries++;
		}
	}

	if (d->parent_dsp == NULL)
		gdm_error (_("%s: failed to connect to parent display \'%s\'"),
			   "gdm_server_start", d->parent_disp);

	return d->parent_dsp != NULL;
}

/**
 * gdm_server_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X server. Handles retries and fatal errors properly.
 */

gboolean
gdm_server_start (GdmDisplay *disp,
		  gboolean try_again_if_busy /* only affects non-flexi servers */,
		  gboolean treat_as_flexi,
		  int min_flexi_disp,
		  int flexi_retries)
{
    int flexi_disp = 20;
    char *vtarg = NULL;
    int vtfd = -1, vt = -1;
    
    if (disp == NULL)
	    return FALSE;

    d = disp;

    /* if an X server exists, wipe it */
    gdm_server_stop (d);

    /* First clear the VT number */
    if (d->type == TYPE_STATIC ||
	d->type == TYPE_FLEXI) {
	    d->vt = -1;
	    gdm_slave_send_num (GDM_SOP_VT_NUM, -1);
    }

    if (SERVER_IS_FLEXI (d) ||
	treat_as_flexi) {
	    flexi_disp = gdm_get_free_display
		    (MAX (gdm_get_high_display_num () + 1, min_flexi_disp) /* start */,
		     d->server_uid /* server uid */);

	    g_free (d->name);
	    d->name = g_strdup_printf (":%d", flexi_disp);
	    d->dispnum = flexi_disp;

	    gdm_slave_send_num (GDM_SOP_DISP_NUM, flexi_disp);
    }

    if (d->type == TYPE_XDMCP_PROXY &&
	! connect_to_parent (d))
	    return FALSE;

    gdm_debug ("gdm_server_start: %s", d->name);

    /* Create new cookie */
    if ( ! gdm_auth_secure_display (d)) 
	    return FALSE;
    gdm_slave_send_string (GDM_SOP_COOKIE, d->cookie);
    gdm_slave_send_string (GDM_SOP_AUTHFILE, d->authfile);
    g_setenv ("DISPLAY", d->name, TRUE);

    if ( ! setup_server_wait (d))
	    return FALSE;

    d->servstat = SERVER_DEAD;

    if (d->type == TYPE_STATIC ||
	d->type == TYPE_FLEXI) {
	    vtarg = gdm_get_empty_vt_argument (&vtfd, &vt);
    }

    /* fork X server process */
    gdm_server_spawn (d, vtarg);

    /* we can now use d->handled since that's set up above */
    do_server_wait (d);

    /* If we were holding a vt open for the server, close it now as it has
     * already taken the bait. */
    if (vtfd > 0) {
	    VE_IGNORE_EINTR (close (vtfd));
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

	    if (SERVER_IS_FLEXI (d))
		    gdm_slave_send_num (GDM_SOP_FLEXI_OK, 0 /* bogus */);
	    if (d->type == TYPE_STATIC ||
		d->type == TYPE_FLEXI) {
		    if (vt >= 0)
			    d->vt = vt;

		    if (d->vt < 0)
			    d->vt = display_vt (d);
		    if (d->vt >= 0)
			    gdm_slave_send_num (GDM_SOP_VT_NUM, d->vt);
	    }

#ifdef HAVE_FBCONSOLE
            gdm_exec_fbconsole (d);
#endif

	    return TRUE;
    default:
	    break;
    }

    if (SERVER_IS_PROXY (disp) &&
	display_parent_no_connect (disp)) {
	    gdm_slave_send_num (GDM_SOP_FLEXI_ERR,
				5 /* proxy can't connect */);
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
		    return gdm_server_start (d, FALSE /*try_again_if_busy */,
					     treat_as_flexi,
					     flexi_disp + 1,
					     flexi_retries - 1);
	    } else {
		    if (try_again_if_busy) {
			    gdm_debug ("%s: Display %s busy.  Trying once again "
				       "(after 2 sec delay)",
				       "gdm_server_start", d->name);
			    gdm_sleep_no_signal (2);
			    return gdm_server_start (d,
						     FALSE /* try_again_if_busy */,
						     treat_as_flexi,
						     flexi_disp,
						     flexi_retries);
		    }
		    if (busy_ask_user (disp)) {
			    gdm_error (_("%s: Display %s busy.  Trying "
					 "another display number."),
				       "gdm_server_start",
				       d->name);
			    d->busy_display = TRUE;
			    return gdm_server_start (d,
						     FALSE /*try_again_if_busy */,
						     TRUE /* treat as flexi */,
						     gdm_get_high_display_num () + 1,
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
	if (d->vt < 0 &&
	    (d->type == TYPE_STATIC ||
	     d->type == TYPE_FLEXI)) {
		d->vt = display_vt (d);
		if (d->vt >= 0)
			gdm_slave_send_num (GDM_SOP_VT_NUM, d->vt);
	}
}

/* somewhat safer rename (safer if the log dir is unsafe), may in fact
   lose the file though, it guarantees that a is gone, but not that
   b exists */
static void
safer_rename (const char *a, const char *b)
{
	errno = 0;
	if (link (a, b) < 0) {
		if (errno == EEXIST) {
			VE_IGNORE_EINTR (g_unlink (a));
			return;
		} 
		VE_IGNORE_EINTR (g_unlink (b));
		/* likely this system doesn't support hard links */
		g_rename (a, b);
		VE_IGNORE_EINTR (g_unlink (a));
		return;
	}
	VE_IGNORE_EINTR (g_unlink (a));
}

static void
rotate_logs (const char *dname)
{
	gchar *logdir = gdm_get_value_string (GDM_KEY_LOG_DIR);

	/* I'm too lazy to write a loop */
	char *fname4 = gdm_make_filename (logdir, dname, ".log.4");
	char *fname3 = gdm_make_filename (logdir, dname, ".log.3");
	char *fname2 = gdm_make_filename (logdir, dname, ".log.2");
	char *fname1 = gdm_make_filename (logdir, dname, ".log.1");
	char *fname = gdm_make_filename (logdir, dname, ".log");

	/* Rotate the logs (keep 4 last) */
	VE_IGNORE_EINTR (g_unlink (fname4));
	safer_rename (fname3, fname4);
	safer_rename (fname2, fname3);
	safer_rename (fname1, fname2);
	safer_rename (fname, fname1);

	g_free (fname4);
	g_free (fname3);
	g_free (fname2);
	g_free (fname1);
	g_free (fname);
}

GdmXserver *
gdm_server_resolve (GdmDisplay *disp)
{
	char *bin;
	GdmXserver *svr = NULL;

	bin = ve_first_word (disp->command);
	if (bin != NULL && bin[0] != '/') {
		svr = gdm_find_xserver (bin);
	}
	g_free (bin);
	return svr;
}


char **
gdm_server_resolve_command_line (GdmDisplay *disp,
				 gboolean resolve_flags,
				 const char *vtarg)
{
	char *bin;
	char **argv;
	int len;
	int i;
	gboolean gotvtarg = FALSE;
	gboolean query_in_arglist = FALSE;

	bin = ve_first_word (disp->command);
	if (bin == NULL) {
		gdm_error (_("Invalid server command '%s'"), disp->command);
		argv = ve_split (gdm_get_value_string (GDM_KEY_STANDARD_XSERVER));
	} else if (bin[0] != '/') {
		GdmXserver *svr = gdm_find_xserver (bin);
		if (svr == NULL) {
			gdm_error (_("Server name '%s' not found; "
				     "using standard server"), bin);
			argv = ve_split (gdm_get_value_string (GDM_KEY_STANDARD_XSERVER));
		} else {
			char **svr_command =
				ve_split (ve_sure_string (svr->command));
			argv = ve_split (disp->command);
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

			if (resolve_flags) {
				/* Setup the handled function */
				disp->handled = svr->handled;
				/* never make use_chooser FALSE,
				   it may have been set temporarily for
				   us by the master */
				if (svr->chooser)
					disp->use_chooser = TRUE;
				disp->priority = svr->priority;
			}
		}
	} else {
		argv = ve_split (disp->command);
	}

	for (len = 0; argv != NULL && argv[len] != NULL; len++) {
		char *arg = argv[len];
		/* HACK! Not to add vt argument to servers that already force
		 * allocation.  Mostly for backwards compat only */
		if (strncmp (arg, "vt", 2) == 0 &&
		    isdigit (arg[2]) &&
		    (arg[3] == '\0' ||
		     (isdigit (arg[3]) && arg[4] == '\0')))
			gotvtarg = TRUE;
		if (strcmp (arg, "-query") == 0 ||
		    strcmp (arg, "-indirect") == 0)
			query_in_arglist = TRUE;
	}

	argv = g_renew (char *, argv, len + 10);
	for (i = len - 1; i >= 1; i--) {
		argv[i+1] = argv[i];
	}
	/* server number is the FIRST argument, before any others */
	argv[1] = g_strdup (disp->name);
	len++;

	if (disp->authfile != NULL) {
		argv[len++] = g_strdup ("-auth");
		argv[len++] = g_strdup (disp->authfile);
	}

	if (resolve_flags && disp->chosen_hostname) {
		/* this display is NOT handled */
		disp->handled = FALSE;
		/* never ever ever use chooser here */
		disp->use_chooser = FALSE;
		disp->priority = 0;
		/* run just one session */
		argv[len++] = g_strdup ("-terminate");
		argv[len++] = g_strdup ("-query");
		argv[len++] = g_strdup (disp->chosen_hostname);
		query_in_arglist = TRUE;
	}

	if (resolve_flags && gdm_get_value_bool (GDM_KEY_DISALLOW_TCP) && ! query_in_arglist) {
		argv[len++] = g_strdup ("-nolisten");
		argv[len++] = g_strdup ("tcp");
		d->tcp_disallowed = TRUE;
	}

	if (vtarg != NULL &&
	    ! gotvtarg) {
		argv[len++] = g_strdup (vtarg);
	}

	argv[len++] = NULL;

	g_free (bin);

	return argv;
}

/**
 * gdm_server_spawn:
 * @disp: Pointer to a GdmDisplay structure
 *
 * forks an actual X server process
 *
 * Note that we can only use d->handled once we call this function
 * since otherwise the server might not yet be looked up yet.
 */

static void 
gdm_server_spawn (GdmDisplay *d, const char *vtarg)
{
    struct sigaction ign_signal;
    sigset_t mask;
    gchar **argv = NULL;
    char *logfile;
    int logfd;
    char *command;
    pid_t pid;

    if (d == NULL ||
	ve_string_empty (d->command)) {
	    return;
    }

    d->servstat = SERVER_PENDING;

    gdm_sigchld_block_push ();

    /* eek, some previous copy, just wipe it */
    if (d->servpid > 0) {
	    pid_t pid = d->servpid;
	    d->servpid = 0;
	    if (pid > 1 &&
		kill (pid, SIGTERM) == 0)
		    ve_waitpid_no_signal (pid, NULL, 0);
    }

    /* Figure out the server command */
    argv = gdm_server_resolve_command_line (d,
					    TRUE /* resolve flags */,
					    vtarg);

    command = g_strjoinv (" ", argv);

    /* Fork into two processes. Parent remains the gdm process. Child
     * becomes the X server. */

    gdm_sigterm_block_push ();
    pid = d->servpid = fork ();
    if (pid == 0)
	    gdm_unset_signals ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();
    
    switch (pid) {
	
    case 0:
	/* the pops whacked mask again */
        gdm_unset_signals ();

	closelog ();

	/* close things */
	gdm_close_all_descriptors (0 /* from */, -1 /* except */, -1 /* except2 */);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	openlog ("gdm", LOG_PID, LOG_DAEMON);

	/* Rotate the X server logs */
	rotate_logs (d->name);

        /* Log all output from spawned programs to a file */
	logfile = gdm_make_filename (gdm_get_value_string (GDM_KEY_LOG_DIR), d->name, ".log");
	VE_IGNORE_EINTR (g_unlink (logfile));
	VE_IGNORE_EINTR (logfd = open (logfile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL, 0644));

	if (logfd != -1) {
		VE_IGNORE_EINTR (dup2 (logfd, 1));
		VE_IGNORE_EINTR (dup2 (logfd, 2));
		close (logfd);
        } else {
		gdm_error (_("%s: Could not open logfile for display %s!"),
			   "gdm_server_spawn", d->name);
	}

	/* The X server expects USR1/TTIN/TTOU to be SIG_IGN */
	ign_signal.sa_handler = SIG_IGN;
	ign_signal.sa_flags = SA_RESTART;
	sigemptyset (&ign_signal.sa_mask);

	if (d->server_uid == 0) {
		/* only set this if we can actually listen */
		if (sigaction (SIGUSR1, &ign_signal, NULL) < 0) {
			gdm_error (_("%s: Error setting %s to %s"),
				   "gdm_server_spawn", "USR1", "SIG_IGN");
			_exit (SERVER_ABORT);
		}
	}
	if (sigaction (SIGTTIN, &ign_signal, NULL) < 0) {
		gdm_error (_("%s: Error setting %s to %s"),
			   "gdm_server_spawn", "TTIN", "SIG_IGN");
		_exit (SERVER_ABORT);
	}
	if (sigaction (SIGTTOU, &ign_signal, NULL) < 0) {
		gdm_error (_("%s: Error setting %s to %s"),
			   "gdm_server_spawn", "TTOU", "SIG_IGN");
		_exit (SERVER_ABORT);
	}

	/* And HUP and TERM are at SIG_DFL from gdm_unset_signals,
	   we also have an empty mask and all that fun stuff */

	/* unblock signals (especially HUP/TERM/USR1) so that we
	 * can control the X server */
	sigemptyset (&mask);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	if (SERVER_IS_PROXY (d)) {
		int argc = ve_vector_len (argv);

		g_unsetenv ("DISPLAY");
		if (d->parent_auth_file != NULL)
			g_setenv ("XAUTHORITY", d->parent_auth_file, TRUE);
		else
			g_unsetenv ("XAUTHORITY");

		if (d->type == TYPE_FLEXI_XNEST) {
			char *font_path = NULL;
			/* Add -fp with the current font path, but only if not
			 * already among the arguments */
			if (strstr (command, "-fp") == NULL)
				font_path = get_font_path (d->parent_disp);
			if (font_path != NULL) {
				argv = g_renew (char *, argv, argc + 2);
				argv[argc++] = "-fp";
				argv[argc++] = font_path;
				command = g_strconcat (command, " -fp ",
						       font_path, NULL);
			}
		}

		argv = g_renew (char *, argv, argc + 3);
		argv[argc++] = "-display";
		argv[argc++] = d->parent_disp;
		argv[argc++] = NULL;
		command = g_strconcat (command, " -display ",
				       d->parent_disp, NULL);
	}

	if (argv[0] == NULL) {
		gdm_error (_("%s: Empty server command for display %s"),
			   "gdm_server_spawn",
			   d->name);
		_exit (SERVER_ABORT);
	}

	gdm_debug ("gdm_server_spawn: '%s'", command);
	
	if (d->priority != 0) {
		if (setpriority (PRIO_PROCESS, 0, d->priority)) {
			gdm_error (_("%s: Server priority couldn't be set to %d: %s"),
				   "gdm_server_spawn", d->priority,
				   strerror (errno));
		}
	}

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
		    g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
			g_setenv ("HOME", pwent->pw_dir, TRUE);
		else
			g_setenv ("HOME", "/", TRUE); /* Hack */
		g_setenv ("SHELL", pwent->pw_shell, TRUE);
		g_unsetenv ("MAIL");

		if (setgid (pwent->pw_gid) < 0)  {
			gdm_error (_("%s: Couldn't set groupid to %d"), 
				   "gdm_server_spawn", (int)pwent->pw_gid);
			_exit (SERVER_ABORT);
		}

		if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
			gdm_error (_("%s: initgroups () failed for %s"),
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

#if sun
    {
        /* Remove old communication pipe, if present */
        char old_pipe[MAXPATHLEN];

        sprintf (old_pipe, "%s/%d", SDTLOGIN_DIR, d->name);
        g_unlink (old_pipe);
    }
#endif

	VE_IGNORE_EINTR (execv (argv[0], argv));

	gdm_fdprintf (2, "GDM: Xserver not found: %s\n"
		      "Error: Command could not be executed!\n"
		      "Please install the X server or correct "
		      "GDM configuration and restart GDM.",
		      command);

	gdm_error (_("%s: Xserver not found: %s"), 
		   "gdm_server_spawn", command);
	
	_exit (SERVER_ABORT);
	
    case -1:
	g_strfreev (argv);
	g_free (command);
	gdm_error (_("%s: Can't fork Xserver process!"),
		   "gdm_server_spawn");
	d->servpid = 0;
	d->servstat = SERVER_DEAD;

	break;
	
    default:
	g_strfreev (argv);
	g_free (command);
	gdm_debug ("%s: Forked server on pid %d", 
		   "gdm_server_spawn", (int)pid);
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
    gdm_in_signal++;

    d->servstat = SERVER_RUNNING; /* Server ready to accept connections */
    d->starttime = time (NULL);

    server_signal_notified = TRUE;
    /* this will quit the select */
    VE_IGNORE_EINTR (write (server_signal_pipe[1], "Yay!", 4));

    gdm_in_signal--;
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
	gdm_in_signal++;

	/* go to the main child handler */
	gdm_slave_child_handler (signal);

	/* this will quit the select */
	VE_IGNORE_EINTR (write (server_signal_pipe[1], "Yay!", 4));

	gdm_in_signal--;
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
    
    hostname[1023] = '\0';
    if (gethostname (hostname, 1023) == -1)
	    strcmp (hostname, "localhost.localdomain");

    d = g_new0 (GdmDisplay, 1);

    d->logout_action = GDM_LOGOUT_ACTION_NONE;

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
    memset (&(d->addr), 0, sizeof (d->addr));
    d->dispnum = id;
    d->servpid = 0;
    d->servstat = SERVER_DEAD;
    d->sesspid = 0;
    d->slavepid = 0;
    d->type = TYPE_STATIC;
    d->attached = TRUE;
    d->sessionid = 0;
    d->acctime = 0;
    d->dsp = NULL;
    d->screenx = 0; /* xinerama offset */
    d->screeny = 0;

    d->handled = TRUE;
    d->tcp_disallowed = FALSE;

    d->priority = 0;
    d->vt = -1;

    d->x_servers_order = -1;

    d->last_loop_start_time = 0;
    d->last_start_time = 0;
    d->retry_count = 0;
    d->sleep_before_run = 0;
    d->login = NULL;

    d->timed_login_ok = FALSE;

    d->slave_notify_fd = -1;
    d->master_notify_fd = -1;

    d->xsession_errors_bytes = 0;
    d->xsession_errors_fd = -1;
    d->session_output_fd = -1;

    d->chooser_output_fd = -1;
    d->chooser_last_line = NULL;

    d->theme_name = NULL;
    
    return d;
}

void
gdm_server_whack_clients (Display *dsp)
{
	int i, screen_count;
	int (* old_xerror_handler) (Display *, XErrorEvent *);

	if (dsp == NULL)
		return;

	old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);

	XGrabServer (dsp);

	screen_count = ScreenCount (dsp);

	for (i = 0; i < screen_count; i++) {
		Window root_ret, parent_ret;
		Window *childs = NULL;
		unsigned int childs_count = 0;
		Window root = RootWindow (dsp, i);

		while (XQueryTree (dsp, root, &root_ret, &parent_ret,
				   &childs, &childs_count) &&
		       childs_count > 0) {
			int ii;

			for (ii = 0; ii < childs_count; ii++) {
				XKillClient (dsp, childs[ii]);
			}

			XFree (childs);
		}
	}

	XUngrabServer (dsp);

	XSync (dsp, False);
	XSetErrorHandler (old_xerror_handler);
}

static char *
get_font_path (const char *display)
{
	Display *disp;
	char **font_path;
	int n_fonts;
	int i;
	GString *gs;

	disp = XOpenDisplay (display);
	if (disp == NULL)
		return NULL;

	font_path = XGetFontPath (disp, &n_fonts);
	if (font_path == NULL) {
		XCloseDisplay (disp);
		return NULL;
	}

	gs = g_string_new (NULL);
	for (i = 0; i < n_fonts; i++) {
		if (i != 0)
			g_string_append_c (gs, ',');
		g_string_append (gs, font_path[i]);
	}

	XFreeFontPath (font_path);

	XCloseDisplay (disp);

	return g_string_free (gs, FALSE);
}

/* EOF */
