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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* This is the gdm slave process. gdmslave runs the chooser, greeter
 * and the user's session scripts. */

#include <config.h>
#include <libgnome/libgnome.h>
#include <gtk/gtkmessagedialog.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_LOGINCAP
#include <login_cap.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#ifdef HAVE_LIBXINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>

#include <vicious.h>

#include "gdm.h"
#include "slave.h"
#include "misc.h"
#include "verify.h"
#include "filecheck.h"
#include "auth.h"
#include "server.h"
#include "choose.h"
#include "errorgui.h"

/* Some per slave globals */
static GdmDisplay *d;
static gchar *login = NULL;
static gboolean greet = FALSE;
static gboolean configurator = FALSE;
static gboolean remanage_asap = FALSE;
static gboolean do_timed_login = FALSE; /* if this is true,
					   login the timed login */
static gboolean do_configurator = FALSE; /* if this is true, login as root
					  * and start the configurator */
static gboolean do_restart_greeter = FALSE; /* if this is true, whack the
					       greeter and try again */
static gboolean restart_greeter_now = FALSE; /* restart_greeter_when the
						SIGCHLD hits */
static int check_notifies_immediately = 0; /* check notifies as they come */
static gboolean gdm_wait_for_ack = TRUE; /* wait for ack on all messages to
				      * the daemon */
static gboolean greeter_disabled = FALSE;
static gboolean greeter_no_focus = FALSE;

static gboolean interrupted = FALSE;
static gchar *ParsedAutomaticLogin = NULL;
static gchar *ParsedTimedLogin = NULL;

int greeter_fd_out = -1;
int greeter_fd_in = -1;

extern gboolean gdm_first_login;
extern gboolean gdm_emergency_server;
extern pid_t extra_process;
extern int extra_status;
extern int gdm_in_signal;

/* Configuration option variables */
extern gchar *GdmUser;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmGnomeDefaultSession;
extern gchar *GdmSessDir;
extern gchar *GdmLocaleFile;
extern gchar *GdmAutomaticLogin;
extern gboolean GdmAllowRemoteAutoLogin;
extern gboolean GdmAlwaysRestartServer;
extern gchar *GdmConfigurator;
extern gboolean GdmConfigAvailable;
extern gboolean GdmSystemMenu;
extern gint GdmXineramaScreen;
extern gchar *GdmGreeter;
extern gchar *GdmRemoteGreeter;
extern gchar *GdmChooser;
extern gchar *GdmDisplayInit;
extern gchar *GdmPreSession;
extern gchar *GdmPostSession;
extern gchar *GdmSuspend;
extern gchar *GdmDefaultPath;
extern gchar *GdmRootPath;
extern gchar *GdmUserAuthFile;
extern gchar *GdmServAuthDir;
extern gchar *GdmDefaultLocale;
extern gchar *GdmTimedLogin;
extern gint GdmTimedLoginDelay;
extern gint GdmUserMaxFile;
extern gint GdmSessionMaxFile;
extern gint GdmRelaxPerms;
extern gboolean GdmKillInitClients;
extern gint GdmPingInterval;
extern gint GdmRetryDelay;
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmGlobalFaceDir;
extern gboolean GdmDebug;


/* Local prototypes */
static gint     gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt);
static gint     gdm_slave_xioerror_handler (Display *disp);
static void	gdm_slave_run (GdmDisplay *display);
static void	gdm_slave_wait_for_login (void);
static void     gdm_slave_greeter (void);
static void     gdm_slave_chooser (void);
static void     gdm_slave_session_start (void);
static void     gdm_slave_session_stop (pid_t sesspid);
static void     gdm_slave_alrm_handler (int sig);
static void     gdm_slave_term_handler (int sig);
static void     gdm_slave_child_handler (int sig);
static void     gdm_slave_usr2_handler (int sig);
static void     gdm_slave_quick_exit (gint status);
static void     gdm_slave_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static void     gdm_child_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static gint     gdm_slave_exec_script (GdmDisplay *d, const gchar *dir,
				       const char *login, struct passwd *pwent,
				       gboolean pass_stdout, 
				       gboolean set_parent);
static gchar *  gdm_parse_enriched_login (const gchar *s, GdmDisplay *display);
static void	gdm_slave_handle_notify (const char *msg);
static void	create_temp_auth_file (void);
static void	set_xnest_parent_stuff (void);

/* Yay thread unsafety */
static gboolean x_error_occured = FALSE;
static gboolean gdm_got_ack = FALSE;
static GList *unhandled_notifies = NULL;

static void
check_notifies_now (void)
{
	GList *list, *li;

	if (unhandled_notifies == NULL)
		return;
       
	gdm_sigusr2_block_push ();
	list = unhandled_notifies;
	unhandled_notifies = NULL;
	gdm_sigusr2_block_pop ();

	for (li = list; li != NULL; li = li->next) {
		char *s = li->data;
		li->data = NULL;

		gdm_slave_handle_notify (s);

		g_free (s);
	}
	g_list_free (list);
}

static void
gdm_slave_desensitize_config (void)
{
	if (configurator &&
	    d->dsp != NULL) {
		gulong foo = 1;
		Atom atom = XInternAtom (d->dsp,
					 "_GDM_SETUP_INSENSITIVE",
					 False);
		XChangeProperty (d->dsp,
				 DefaultRootWindow (d->dsp),
				 atom,
				 XA_CARDINAL, 32, PropModeReplace,
				 (unsigned char *) &foo, 1);
		XSync (d->dsp, False);
	}

}

static void
gdm_slave_sensitize_config (void)
{
	if (d->dsp != NULL) {
		XDeleteProperty (d->dsp,
				 DefaultRootWindow (d->dsp),
				 XInternAtom (d->dsp,
					      "_GDM_SETUP_INSENSITIVE",
					      False));
		XSync (d->dsp, False);
	}
}

/* ignore handlers */
static int
ignore_xerror_handler (Display *disp, XErrorEvent *evt)
{
	x_error_occured = TRUE;
	return 0;
}

static void
whack_greeter_fds (void)
{
	if (greeter_fd_out > 0)
		close (greeter_fd_out);
	greeter_fd_out = -1;
	if (greeter_fd_in > 0)
		close (greeter_fd_in);
	greeter_fd_in = -1;
}


void 
gdm_slave_start (GdmDisplay *display)
{  
	time_t first_time;
	int death_count;
	static sigset_t mask;
	struct sigaction alrm, term, child, usr2;

	if (!display)
		return;

	gdm_debug ("gdm_slave_start: Starting slave process for %s", display->name);
	if (display->type == TYPE_XDMCP &&
	    GdmPingInterval > 0) {
		/* Handle a ALRM signals from our ping alarms */
		alrm.sa_handler = gdm_slave_alrm_handler;
		alrm.sa_flags = SA_RESTART | SA_NODEFER;
		sigemptyset (&alrm.sa_mask);
		sigaddset (&alrm.sa_mask, SIGALRM);

		if (sigaction (SIGALRM, &alrm, NULL) < 0)
			gdm_slave_exit (DISPLAY_ABORT,
					_("%s: Error setting up ALRM signal handler: %s"),
					"gdm_slave_start", g_strerror (errno));
	}

	/* Handle a INT/TERM signals from gdm master */
	term.sa_handler = gdm_slave_term_handler;
	term.sa_flags = SA_RESTART;
	sigemptyset (&term.sa_mask);
	sigaddset (&term.sa_mask, SIGTERM);
	sigaddset (&term.sa_mask, SIGINT);

	if ((sigaction (SIGTERM, &term, NULL) < 0) ||
	    (sigaction (SIGINT, &term, NULL) < 0))
		gdm_slave_exit (DISPLAY_ABORT,
				_("%s: Error setting up TERM/INT signal handler: %s"),
				"gdm_slave_start", g_strerror (errno));

	/* Child handler. Keeps an eye on greeter/session */
	child.sa_handler = gdm_slave_child_handler;
	child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigemptyset (&child.sa_mask);
	sigaddset (&child.sa_mask, SIGCHLD);

	if (sigaction (SIGCHLD, &child, NULL) < 0) 
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up CHLD signal handler: %s"),
				"gdm_slave_start", g_strerror (errno));

	/* Handle a USR2 which is ack from master that it received a message */
	usr2.sa_handler = gdm_slave_usr2_handler;
	usr2.sa_flags = SA_RESTART;
	sigemptyset (&usr2.sa_mask);
	sigaddset (&usr2.sa_mask, SIGUSR2);

	if (sigaction (SIGUSR2, &usr2, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up USR2 signal handler: %s"),
				"gdm_slave_start", g_strerror (errno));

	/* The signals we wish to listen to */
	sigfillset (&mask);
	sigdelset (&mask, SIGINT);
	sigdelset (&mask, SIGTERM);
	sigdelset (&mask, SIGCHLD);
	sigdelset (&mask, SIGUSR2);
	if (display->type == TYPE_XDMCP &&
	    GdmPingInterval > 0) {
		sigdelset (&mask, SIGALRM);
	}
	sigprocmask (SIG_SETMASK, &mask, NULL);

	first_time = time (NULL);
	death_count = 0;

	for (;;) {
		time_t the_time;

		check_notifies_now ();

		gdm_debug ("gdm_slave_start: Loop Thingie");
		gdm_slave_run (display);

		/* remote and flexi only run once */
		if (display->type != TYPE_LOCAL)
			break;

		the_time = time (NULL);

		death_count ++;

		if ((the_time - first_time) <= 0 ||
		    (the_time - first_time) > 60) {
			first_time = the_time;
			death_count = 0;
		} else if (death_count > 6) {
			/* exitting the loop will cause an
			 * abort actually */
			break;
		}

		gdm_debug ("gdm_slave_start: Reinitializing things");


		if (GdmAlwaysRestartServer) {
			/* Whack the server if we want to restart it next time
			 * we run gdm_slave_run */
			gdm_server_stop (display);
			gdm_slave_send_num (GDM_SOP_XPID, 0);
		} else {
			/* OK about to start again so rebake our cookies and reinit
			 * the server */
			if ( ! gdm_auth_secure_display (d))
				break;
			gdm_slave_send_string (GDM_SOP_COOKIE, d->cookie);

			gdm_server_reinit (d);
		}
	}
}

static gboolean
setup_automatic_session (GdmDisplay *display, const char *name)
{
	g_free (login);
	login = g_strdup (name);

	greet = FALSE;
	gdm_debug ("setup_automatic_session: Automatic login: %s", login);

	/* Run the init script. gdmslave suspends until script
	 * has terminated */
	gdm_slave_exec_script (display, GdmDisplayInit, NULL, NULL,
			       FALSE /* pass_stdout */,
			       TRUE /* set_parent */);

	gdm_debug ("setup_automatic_session: DisplayInit script finished");

	if ( ! gdm_verify_setup_user (display, login, display->name))
		return FALSE;

	gdm_debug ("setup_automatic_session: Automatic login successful");

	return TRUE;
}

static void 
gdm_screen_init (GdmDisplay *display) 
{
#ifdef HAVE_LIBXINERAMA
	int (* old_xerror_handler) (Display *, XErrorEvent *);
	gboolean have_xinerama = FALSE;

	x_error_occured = FALSE;
	old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);

	have_xinerama = XineramaIsActive (display->dsp);

	XSync (display->dsp, False);
	XSetErrorHandler (old_xerror_handler);

	if (x_error_occured)
		have_xinerama = FALSE;

	if (have_xinerama) {
		int screen_num;
		XineramaScreenInfo *xscreens =
			XineramaQueryScreens (display->dsp,
					      &screen_num);


		if (screen_num <= 0)
			gdm_fail ("Xinerama active, but <= 0 screens?");

		if (screen_num <= GdmXineramaScreen)
			GdmXineramaScreen = 0;

		display->screenx = xscreens[GdmXineramaScreen].x_org;
		display->screeny = xscreens[GdmXineramaScreen].y_org;
		display->screenwidth = xscreens[GdmXineramaScreen].width;
		display->screenheight = xscreens[GdmXineramaScreen].height;

		display->lrh_offsetx =
			DisplayWidth (display->dsp,
				      DefaultScreen (display->dsp))
			- (display->screenx + display->screenwidth);
		display->lrh_offsety =
			DisplayHeight (display->dsp,
				       DefaultScreen (display->dsp))
			- (display->screeny + display->screenheight);

		XFree (xscreens);
	} else
#endif
	{
		display->screenx = 0;
		display->screeny = 0;
		display->screenwidth = 0; /* we'll use the gdk size */
		display->screenheight = 0;

		display->lrh_offsetx = 0;
		display->lrh_offsety = 0;
	}
}

static void
gdm_slave_whack_greeter (void)
{
	gdm_sigchld_block_push ();

	/* do what you do when you quit, this will hang until the
	 * greeter decides to print an STX\n and die, meaning it can do some
	 * last minute cleanup */
	gdm_slave_greeter_ctl_no_ret (GDM_QUIT, "");

	greet = FALSE;

	/* Wait for the greeter to really die, the check is just
	 * being very anal, the pid is always set to something */
	if (d->greetpid > 0)
		ve_waitpid_no_signal (d->greetpid, 0, 0); 
	d->greetpid = 0;

	whack_greeter_fds ();

	gdm_slave_send_num (GDM_SOP_GREETPID, 0);

	gdm_slave_whack_temp_auth_file ();

	gdm_sigchld_block_pop ();
}


static gboolean do_xfailed_on_xio_error = FALSE;

static void 
gdm_slave_run (GdmDisplay *display)
{  
    gint openretries = 0;
    gint maxtries = 0;
    
    d = display;

    if (d->sleep_before_run > 0) {
	    gdm_debug ("gdm_slave_run: Sleeping %d seconds before server start", d->sleep_before_run);
	    sleep (d->sleep_before_run);
	    d->sleep_before_run = 0;

	    check_notifies_now ();
    }

    /* set it before we run the server, it may be that we're using
     * the XOpenDisplay to find out if a server is ready (as with Xnest) */
    d->dsp = NULL;

    /* if this is local display start a server if one doesn't
     * exist */
    if (SERVER_IS_LOCAL (d) &&
	d->servpid <= 0) {
	    if ( ! gdm_server_start (d,
				     FALSE /* treat_as_flexi */,
				     20 /* min_flexi_disp */,
				     5 /* flexi_retries */)) {
		    /* We're really not sure what is going on,
		     * so we throw up our hands and tell the user
		     * that we've given up.  The error is likely something
		     * internal. */
		    gdm_text_message_dialog
			    (_("I could not start the X\n"
			       "server (your graphical environment)\n"
			       "due to some internal error.\n"
			       "Please contact your system administrator\n"
			       "or check your syslog to diagnose.\n"
			       "In the meantime this display will be\n"
			       "disabled.  Please restart gdm when\n"
			       "the problem is corrected."));
		    gdm_slave_quick_exit (DISPLAY_ABORT);
	    }
	    gdm_slave_send_num (GDM_SOP_XPID, d->servpid);

	    check_notifies_now ();
    }

    /* We can use d->handled from now on on this display,
     * since the lookup was done in server start */
    
    gnome_setenv ("XAUTHORITY", d->authfile, TRUE);
    gnome_setenv ("DISPLAY", d->name, TRUE);

    if (d->handled) {
	    /* Now the display name and hostname is final */
	    if ( ! ve_string_empty (GdmAutomaticLogin)) {
		    g_free (ParsedAutomaticLogin);
		    ParsedAutomaticLogin = gdm_parse_enriched_login (GdmAutomaticLogin,
								     display);
	    }

	    if ( ! ve_string_empty (GdmTimedLogin)) {
		    g_free (ParsedTimedLogin);
		    ParsedTimedLogin = gdm_parse_enriched_login (GdmTimedLogin,
								 display);
	    }
    }
    
    /* X error handlers to avoid the default one (i.e. exit (1)) */
    do_xfailed_on_xio_error = TRUE;
    XSetErrorHandler (gdm_slave_xerror_handler);
    XSetIOErrorHandler (gdm_slave_xioerror_handler);
    
    /* We keep our own (windowless) connection (dsp) open to avoid the
     * X server resetting due to lack of active connections. */

    gdm_debug ("gdm_slave_run: Opening display %s", d->name);

    /* if local then the the server should be ready for openning, so
     * don't try so long before killing it and trying again */
    if (SERVER_IS_LOCAL (d))
	    maxtries = 2;
    else
	    maxtries = 10;
    
    while (d->handled &&
	   openretries < maxtries &&
	   d->dsp == NULL) {
	d->dsp = XOpenDisplay (d->name);
	
	if (d->dsp == NULL) {
	    gdm_debug ("gdm_slave_run: Sleeping %d on a retry", 1+openretries*2);
	    sleep (1+openretries*2);
	    openretries++;
	}
    }

    /* Set the busy cursor */
    if (d->dsp != NULL) {
	    Cursor xcursor = XCreateFontCursor (d->dsp, GDK_WATCH);
	    XDefineCursor (d->dsp,
			   DefaultRootWindow (d->dsp),
			   xcursor);
	    XFreeCursor (d->dsp, xcursor);
	    XSync (d->dsp, False);
    }

    /* Just a race avoiding sleep, probably not necessary though,
     * but doesn't hurt anything */
    if ( ! d->handled)
	    sleep (1);

    if (SERVER_IS_LOCAL (d)) {
	    gdm_slave_send (GDM_SOP_START_NEXT_LOCAL, FALSE);
    }

    check_notifies_now ();

    /* something may have gone wrong, try xfailed, if local (non-flexi),
     * the toplevel loop of death will handle us */ 
    if (d->handled && d->dsp == NULL) {
	    if (d->type == TYPE_LOCAL)
		    gdm_slave_quick_exit (DISPLAY_XFAILED);
	    else
		    gdm_slave_quick_exit (DISPLAY_ABORT);
    }

    /* Some sort of a bug foo to make some servers work or whatnot,
     * stolen from xdm sourcecode, perhaps not necessary, but can't hurt */
    if (d->handled) {
	    Display *dsp = XOpenDisplay (d->name);
	    if (dsp != NULL)
		    XCloseDisplay (dsp);
    }

    /* OK from now on it's really the user whacking us most likely,
     * we have already started up well */
    do_xfailed_on_xio_error = FALSE;

    /* If XDMCP setup pinging */
    if (d->type == TYPE_XDMCP &&
	GdmPingInterval > 0) {
	    alarm (GdmPingInterval * 60);
    }

    /* checkout xinerama */
    if (d->handled)
	    gdm_screen_init (d);

    /* check log stuff for the server, this is done here
     * because it's really a race */
    if (SERVER_IS_LOCAL (d))
	    gdm_server_checklog (d);

    if ( ! d->handled) {
	    /* yay, we now wait for the server to die,
	     * which will in fact just exit, so
	     * this code is a little bit too anal */
	    while (d->servpid > 0) {
		    select (0, NULL, NULL, NULL, NULL);
	    }
	    return;
    } else if (d->use_chooser) {
	    /* this usually doesn't return */
	    gdm_slave_chooser ();  /* Run the chooser */
	    return;
    } else if (d->type == TYPE_LOCAL &&
	       gdm_first_login &&
	       ! ve_string_empty (ParsedAutomaticLogin) &&
	       strcmp (ParsedAutomaticLogin, "root") != 0) {
	    gdm_first_login = FALSE;

	    d->logged_in = TRUE;
	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);
	    gdm_slave_send_string (GDM_SOP_LOGIN, ParsedAutomaticLogin);

	    if (setup_automatic_session (d, ParsedAutomaticLogin)) {
		    gdm_slave_session_start ();
	    }

	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, FALSE);
	    d->logged_in = FALSE;
	    gdm_slave_send_string (GDM_SOP_LOGIN, "");

	    gdm_debug ("gdm_slave_run: Automatic login done");
	    
	    if (remanage_asap) {
		    gdm_slave_quick_exit (DISPLAY_REMANAGE);
	    }

	    /* return to gdm_slave_start so that the server
	     * can be reinitted and all that kind of fun stuff. */
	    return;
    }

    if (gdm_first_login)
	    gdm_first_login = FALSE;

    do {
	    check_notifies_now ();

	    if ( ! greet) {
		    gdm_slave_greeter ();  /* Start the greeter */
		    greeter_no_focus = FALSE;
		    greeter_disabled = FALSE;
	    }

	    gdm_slave_wait_for_login (); /* wait for a password */

	    d->logged_in = TRUE;
	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);

	    if (do_timed_login) {
		    /* timed out into a timed login */
		    do_timed_login = FALSE;
		    if (setup_automatic_session (d, ParsedTimedLogin)) {
			    gdm_slave_send_string (GDM_SOP_LOGIN,
						   ParsedTimedLogin);
			    gdm_slave_session_start ();
		    }
	    } else {
		    gdm_slave_send_string (GDM_SOP_LOGIN, login);
		    gdm_slave_session_start ();
	    }

	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, FALSE);
	    d->logged_in = FALSE;
	    gdm_slave_send_string (GDM_SOP_LOGIN, "");

	    if (remanage_asap) {
		    gdm_slave_quick_exit (DISPLAY_REMANAGE);
	    }

	    if (greet) {
		    greeter_no_focus = FALSE;
		    gdm_slave_greeter_ctl_no_ret (GDM_FOCUS, "");
		    greeter_disabled = FALSE;
		    gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");
		    gdm_slave_greeter_ctl_no_ret (GDM_RESETOK, "");
	    }
	    /* Note that greet is only true if the above was no 'login',
	     * so no need to reinit the server nor rebake cookies
	     * nor such nonsense */
    } while (greet);
}

/* A hack really, this will wait around until the first mapped window
 * with this class and focus it */
static void
focus_first_x_window (const char *class_res_name)
{
	pid_t pid;
	Display *disp;
	XWindowAttributes attribs = { 0, };

	pid = fork ();
	if (pid < 0) {
		gdm_error (_("focus_first_x_window: cannot fork"));
		return;
	}
	/* parent */
	if (pid > 0) {
		return;
	}

	gdm_unset_signals ();

	closelog ();

	gdm_close_all_descriptors (0 /* from */, -1 /* except */);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	openlog ("gdm", LOG_PID, LOG_DAEMON);

	disp = XOpenDisplay (d->name);
	if (disp == NULL) {
		gdm_error (_("focus_first_x_window: cannot open display %s"),
			   d->name);
		_exit (0);
	}

	XSetInputFocus (disp, PointerRoot, RevertToPointerRoot, CurrentTime);

	/* set event mask for events on root window */
	XGetWindowAttributes (disp,
			      DefaultRootWindow (disp),
			      &attribs);
	XSelectInput (disp,
		      DefaultRootWindow (disp),
		      attribs.your_event_mask |
		      SubstructureNotifyMask);
	
	for (;;) {
		XEvent event = { 0, };
		XClassHint hint = { NULL, NULL };

		XNextEvent (disp, &event);

		if (event.type == MapNotify &&
		    XGetClassHint (disp,
				   event.xmap.window,
				   &hint) &&
		    hint.res_name != NULL &&
		    strcmp (hint.res_name, class_res_name) == 0) {
			Window root_return;
			int x_return, y_return;
			unsigned int width_return = 0, height_return = 0;
			unsigned int border_width_return;
			unsigned int depth_return;

			XGetGeometry (disp, event.xmap.window,
				      &root_return, &x_return,
				      &y_return, &width_return,
				      &height_return, &border_width_return,
				      &depth_return);
			XWarpPointer (disp, None, event.xmap.window,
				      0, 0, 0, 0,
				      width_return / 2,
				      height_return / 2);
			XSync (disp, False);
			XCloseDisplay (disp);

			_exit (0);
		}
	}
}

static void
run_config (GdmDisplay *display, struct passwd *pwent)
{
	pid_t pid;

	/* Set the busy cursor */
	if (d->dsp != NULL) {
		Cursor xcursor = XCreateFontCursor (d->dsp, GDK_WATCH);
		XDefineCursor (d->dsp,
			       DefaultRootWindow (d->dsp),
			       xcursor);
		XFreeCursor (d->dsp, xcursor);
		XSync (d->dsp, False);
	}

	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->sesspid = fork ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	if (pid < 0) {
		/* return left pointer */
		Cursor xcursor;

		/* can't fork, damnit */
		display->sesspid = 0;
	       
		xcursor = XCreateFontCursor (d->dsp, GDK_LEFT_PTR);
		XDefineCursor (d->dsp,
			       DefaultRootWindow (d->dsp),
			       xcursor);
		XFreeCursor (d->dsp, xcursor);
		XSync (d->dsp, False);

		return;
	}

	if (pid == 0) {
		char **argv;
		/* child */

		setsid ();

		gdm_unset_signals ();

		setuid (0);
		setgid (0);

		/* setup environment */
		/* FIXME: clearing environment is likely fairly stupid,
		 * is there any reason we should do it anyway? */
		/* gdm_clearenv_no_lang (); */

		/* root here */
		gnome_setenv ("XAUTHORITY", display->authfile, TRUE);
		gnome_setenv ("DISPLAY", display->name, TRUE);
		gnome_setenv ("LOGNAME", "root", TRUE);
		gnome_setenv ("USER", "root", TRUE);
		gnome_setenv ("USERNAME", "root", TRUE);
		gnome_setenv ("HOME", pwent->pw_dir, TRUE);
		gnome_setenv ("SHELL", pwent->pw_shell, TRUE);
		gnome_setenv ("PATH", GdmRootPath, TRUE);
		gnome_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		closelog ();

		gdm_close_all_descriptors (0 /* from */, -1 /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		if (chdir ("/root") != 0)
			chdir ("/");

		/* exec the configurator */
		argv = ve_split (GdmConfigurator);
		if (argv != NULL &&
		    argv[0] != NULL &&
		    access (argv[0], X_OK) == 0)
			execv (argv[0], argv);

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Could not execute the configuration\n"
				 "program.  Make sure it's path is set\n"
				 "correctly in the configuration file.\n"
				 "I will attempt to start it from the\n"
				 "default location."));

		argv = ve_split
			(EXPANDED_BINDIR
			 "/gdmsetup --disable-sound --disable-crash-dialog");
		if (access (argv[0], X_OK) == 0)
			execv (argv[0], argv);

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Could not execute the configuration\n"
				 "program.  Make sure it's path is set\n"
				 "correctly in the configuration file."));

		_exit (0);
	} else {
		configurator = TRUE;
		/* XXX: is this a race if we don't push a sigchld block?
		 * but then we don't get a signal for restarting the greeter */
		/* wait for the config proggie to die */
		if (d->sesspid > 0)
			/* must use the pid var here since sesspid might get
			 * zeroed between the check and here by sigchld
			 * handler */
			ve_waitpid_no_signal (pid, 0, 0);
		display->sesspid = 0;
		configurator = FALSE;
	}
}

static void
restart_the_greeter (void)
{
	gdm_slave_desensitize_config ();

	/* no login */
	g_free (login);
	login = NULL;

	/* No restart it */
	if (greet) {
		gdm_sigchld_block_push ();

		gdm_slave_greeter_ctl_no_ret (GDM_SAVEDIE, "");

		greet = FALSE;

		/* Wait for the greeter to really die, the check is just
		 * being very anal, the pid is always set to something */
		if (d->greetpid > 0)
			ve_waitpid_no_signal (d->greetpid, 0, 0); 
		d->greetpid = 0;

		whack_greeter_fds ();

		gdm_slave_send_num (GDM_SOP_GREETPID, 0);

		gdm_sigchld_block_pop ();
	}
	gdm_slave_greeter ();

	if (greeter_disabled)
		gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");

	if (greeter_no_focus)
		gdm_slave_greeter_ctl_no_ret (GDM_NOFOCUS, "");

	gdm_slave_sensitize_config ();
}

static void
gdm_slave_wait_for_login (void)
{
	g_free (login);
	login = NULL;

	/* Chat with greeter */
	while (login == NULL) {
		/* init to a sane value */
		do_timed_login = FALSE;
		do_configurator = FALSE;

		if (do_restart_greeter) {
			do_restart_greeter = FALSE;
			restart_the_greeter ();
		}

		/* We are NOT interrupted yet */
		interrupted = FALSE;

		check_notifies_now ();

		/* just for paranoia's sake */
		seteuid (0);
		setegid (0);

		gdm_debug ("gdm_slave_wait_for_login: In loop");
		login = gdm_verify_user (d,
					 NULL /* username*/,
					 d->name,
					 d->console);
		gdm_debug ("gdm_slave_wait_for_login: end verify for '%s'",
			   ve_sure_string (login));

		/* Complex, make sure to always handle the do_configurator
		 * do_timed_login and do_restart_greeter after any call
		 * to gdm_verify_user */

		if (do_restart_greeter) {
			do_restart_greeter = FALSE;
			restart_the_greeter ();
			continue;
		}

		check_notifies_now ();

		if (do_configurator) {
			struct passwd *pwent;
			gboolean oldAllowRoot;

			do_configurator = FALSE;
			g_free (login);
			login = NULL;
			/* clear any error */
			gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, "");
			gdm_slave_greeter_ctl_no_ret
				(GDM_MSG,
				 _("Enter the root password\n"
				   "to run the configuration."));

			/* we always allow root for this */
			oldAllowRoot = GdmAllowRoot;
			GdmAllowRoot = TRUE;
			gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, "root");
			login = gdm_verify_user (d,
						 "root",
						 d->name,
						 d->console);
			GdmAllowRoot = oldAllowRoot;

			if (do_restart_greeter) {
				do_restart_greeter = FALSE;
				restart_the_greeter ();
				continue;
			}

			check_notifies_now ();

			/* the wanker can't remember his password */
			if (login == NULL) {
				gdm_debug (_("gdm_slave_wait_for_login: No login/Bad login"));
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			/* wipe the login */
			g_free (login);
			login = NULL;

			/* note that this can still fall through to
			 * the timed login if the user doesn't type in the
			 * password fast enough and there is timed login
			 * enabled */
			if (do_timed_login) {
				break;
			}

			/* the user is a wanker */
			if (do_configurator) {
				do_configurator = FALSE;
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			/* okey dokey, we're root */

			/* get the root pwent */
			pwent = getpwnam ("root");

			if (pwent == NULL) {
				/* what? no "root" ??, this is not possible
				 * since we logged in, but I'm paranoid */
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			d->logged_in = TRUE;
			gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);
			/* Note: nobody really logged in */
			gdm_slave_send_string (GDM_SOP_LOGIN, "");

			/* disable the login screen, we don't want people to
			 * log in in the meantime */
			gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");
			greeter_disabled = TRUE;

			/* make the login screen not focusable */
			gdm_slave_greeter_ctl_no_ret (GDM_NOFOCUS, "");
			greeter_no_focus = TRUE;

			check_notifies_now ();
			check_notifies_immediately++;
			restart_greeter_now = TRUE;

			run_config (d, pwent);

			restart_greeter_now = FALSE;
			check_notifies_immediately--;

			gdm_verify_cleanup (d);

			gdm_slave_send_num (GDM_SOP_LOGGED_IN, FALSE);
			d->logged_in = FALSE;

			if (remanage_asap) {
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}

			greeter_no_focus = FALSE;
			gdm_slave_greeter_ctl_no_ret (GDM_FOCUS, "");

			greeter_disabled = FALSE;
			gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");
			gdm_slave_greeter_ctl_no_ret (GDM_RESETOK, "");
			continue;
		}

		/* the user timed out into a timed login during the
		 * conversation */
		if (do_timed_login) {
			break;
		}

		if (login == NULL) {
			gdm_debug (_("gdm_slave_wait_for_login: No login/Bad login"));
			gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
		}
	}

	/* the user timed out into a timed login during the
	 * conversation */
	if (do_timed_login) {
		g_free (login);
		login = NULL;
		/* timed login is automatic, thus no need for greeter,
		 * we'll take default values */
		gdm_slave_whack_greeter();

		gdm_debug ("gdm_slave_wait_for_login: Timed Login");
	}

	gdm_debug ("gdm_slave_wait_for_login: got_login for '%s'",
		   ve_sure_string (login));
}

/* If path starts with a "trusted" directory, don't sanity check things */
/* This is really somewhat "outdated" as we now really want things in
 * the picture dir or in ~/.gnome2/photo */
static gboolean
is_in_trusted_pic_dir (const char *path)
{
	/* our own pixmap dir is trusted */
	if (strncmp (path, EXPANDED_PIXMAPDIR, sizeof (EXPANDED_PIXMAPDIR)) == 0)
		return TRUE;

	return FALSE;
}

/* This is VERY evil! */
static void
run_pictures (void)
{
	char *response;
	char buf[1024];
	size_t bytes;
	struct passwd *pwent;
	char *picfile;
	char *picdir;
	FILE *fp;
	char *cfgdir;

	response = NULL;
	for (;;) {
		struct stat s;
		char *tmp, *ret;
		int i;

		g_free (response);
		response = gdm_slave_greeter_ctl (GDM_NEEDPIC, "");
		if (ve_string_empty (response)) {
			g_free (response);
			return;
		}

		pwent = getpwnam (response);
		if (pwent == NULL) {
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		picfile = NULL;

		setegid (pwent->pw_gid);
		seteuid (pwent->pw_uid);

		/* Sanity check on ~user/.gnome2/gdm */
		cfgdir = g_strconcat (pwent->pw_dir, "/.gnome2/gdm", NULL);
		if (gdm_file_check ("run_pictures", pwent->pw_uid,
				    cfgdir, "gdm", TRUE, GdmUserMaxFile,
				    GdmRelaxPerms)) {
			char *cfgstr;

			cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome2/gdm=/face/picture", NULL);
			picfile = gnome_config_get_string (cfgstr);
			g_free (cfgstr);

			/* must exist and be absolute (note that this check
			 * catches empty strings)*/
			if (picfile != NULL &&
			    (picfile[0] != '/' ||
			     access (picfile, R_OK) != 0)) {
				g_free (picfile);
				picfile = NULL;
			}

			if (picfile != NULL) {
				char *dir;
				char *base;

				/* if in trusted dir, just use it */
				if (is_in_trusted_pic_dir (picfile)) {
					seteuid (0);
					setegid (GdmGroupId);

					g_free (cfgdir);

					gdm_slave_greeter_ctl_no_ret (GDM_READPIC,
								      picfile);
					g_free (picfile);
					continue;
				}

				/* if not in trusted dir, check it out */
				dir = g_path_get_dirname (picfile);
				base = g_path_get_basename (picfile);

				/* Note that strict permissions checking is done
				 * on this file.  Even if it may not even be owned by the
				 * user.  This setting should ONLY point to pics in trusted
				 * dirs. */
				if ( ! gdm_file_check ("run_pictures", pwent->pw_uid,
						       dir, base, TRUE, GdmUserMaxFile,
						       GdmRelaxPerms)) {
					g_free (picfile);
					picfile = NULL;
				}

				g_free (base);
				g_free (dir);
			}
		}
		g_free (cfgdir);

		/* Nothing found yet */
		if (picfile == NULL) {
			picfile = g_strconcat (pwent->pw_dir, "/.gnome2/photo", NULL);
			picdir = g_strconcat (pwent->pw_dir, "/.gnome2", NULL);
			if (access (picfile, F_OK) != 0) {
				g_free (picfile);
				picfile = g_strconcat (pwent->pw_dir, "/.gnome/photo", NULL);
				g_free (picdir);
				picdir = g_strconcat (pwent->pw_dir, "/.gnome", NULL);
			}
			if (access (picfile, F_OK) != 0) {
				seteuid (0);
				setegid (GdmGroupId);

				g_free (picfile);
				g_free (picdir);
				picfile = g_strconcat (GdmGlobalFaceDir, "/",
						       response, NULL);

				if (access (picfile, R_OK) == 0) {
					gdm_slave_greeter_ctl_no_ret (GDM_READPIC,
								      picfile);
					g_free (picfile);
					continue;
				}

				g_free (picfile);
				picfile = g_strconcat (GdmGlobalFaceDir, "/",
						       response, ".png", NULL);

				if (access (picfile, R_OK) == 0) {
					gdm_slave_greeter_ctl_no_ret (GDM_READPIC,
								      picfile);
					g_free (picfile);
					continue;
				}

				gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
				g_free (picfile);
				continue;
			}

			/* Sanity check on ~user/.gnome[2]/photo */
			if ( ! gdm_file_check ("run_pictures", pwent->pw_uid,
					       picdir, "photo", TRUE, GdmUserMaxFile,
					       GdmRelaxPerms)) {
				g_free (picdir);

				seteuid (0);
				setegid (GdmGroupId);

				gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
				continue;
			}
			g_free (picdir);
		}

		if (stat (picfile, &s) < 0) {
			seteuid (0);
			setegid (GdmGroupId);

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		fp = fopen (picfile, "r");
		g_free (picfile);
		if (fp == NULL) {
			seteuid (0);
			setegid (GdmGroupId);

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		tmp = g_strdup_printf ("buffer:%d", (int)s.st_size);
		ret = gdm_slave_greeter_ctl (GDM_READPIC, tmp);
		g_free (tmp);

		if (ret == NULL || strcmp (ret, "OK") != 0) {
			g_free (ret);
			seteuid (0);
			setegid (GdmGroupId);
			continue;
		}
		g_free (ret);

		gdm_fdprintf (greeter_fd_out, "%c", STX);

		i = 0;
		while ((bytes = fread (buf, sizeof (char),
				       sizeof (buf), fp)) > 0) {
			write (greeter_fd_out, buf, bytes);
			i += bytes;
		}

		fclose (fp);

		/* eek, this "could" happen, so just send some garbage */
		while (i < s.st_size) {
			bytes = MIN (sizeof (buf), s.st_size - i);
			write (STDOUT_FILENO, buf, bytes);
			i += bytes;
		}
			
		gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "done");

		seteuid (0);
		setegid (GdmGroupId);
	}
	g_free (response);
}

/* hakish, copy file (owned by fromuid) to a temp file owned by touid */
static char *
copy_auth_file (uid_t fromuid, uid_t touid, const char *file)
{
	uid_t old = geteuid ();
	char *name;
	int authfd;
	int fromfd;
	int bytes;
	char buf[2048];

	seteuid (fromuid);

	fromfd = open (file, O_RDONLY);

	if (fromfd < 0) {
		seteuid (old);
		return NULL;
	}

	seteuid (0);

	name = g_strconcat (GdmServAuthDir, "/", d->name, ".XnestAuth", NULL);

	authfd = open (name, O_TRUNC|O_WRONLY|O_CREAT, 0600);

	if (authfd < 0) {
		seteuid (old);
		g_free (name);
		return NULL;
	}

	/* Make it owned by the user that Xnest is started as */
	fchown (authfd, touid, -1);

	while ((bytes = read (fromfd, buf, sizeof (buf))) > 0) {
		write (authfd, buf, bytes);
	}

	close (fromfd);
	close (authfd);

	seteuid (old);

	return name;
}

static void
gdm_slave_greeter (void)
{
    gint pipe1[2], pipe2[2];  
    gchar **argv;
    struct passwd *pwent;
    pid_t pid;
    
    gdm_debug ("gdm_slave_greeter: Running greeter on %s", d->name);
    
    /* Run the init script. gdmslave suspends until script has terminated */
    gdm_slave_exec_script (d, GdmDisplayInit, NULL, NULL,
			   FALSE /* pass_stdout */,
			   TRUE /* set_parent */);

    /* Open a pipe for greeter communications */
    if (pipe (pipe1) < 0 || pipe (pipe2) < 0) 
	gdm_slave_exit (DISPLAY_ABORT, _("%s: Can't init pipe to gdmgreeter"),
			"gdm_slave_greeter");

    /* hackish ain't it */
    create_temp_auth_file ();
    
    /* Fork. Parent is gdmslave, child is greeter process. */
    gdm_sigchld_block_push ();
    gdm_sigterm_block_push ();
    greet = TRUE;
    pid = d->greetpid = fork ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();

    switch (pid) {
	
    case 0:
	setsid ();

	gdm_unset_signals ();

	/* Plumbing */
	close (pipe1[1]);
	close (pipe2[0]);

	dup2 (pipe1[0], STDIN_FILENO);
	dup2 (pipe2[1], STDOUT_FILENO);

	closelog ();

	gdm_close_all_descriptors (2 /* from */, -1 /* except */);

	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	openlog ("gdm", LOG_PID, LOG_DAEMON);
	
	if (setgid (GdmGroupId) < 0) 
	    gdm_child_exit (DISPLAY_ABORT,
			    _("%s: Couldn't set groupid to %d"),
			    "gdm_slave_greeter", GdmGroupId);

	if (initgroups (GdmUser, GdmGroupId) < 0)
            gdm_child_exit (DISPLAY_ABORT,
			    _("%s: initgroups() failed for %s"),
			    "gdm_slave_greeter", GdmUser);
	
	if (setuid (GdmUserId) < 0) 
	    gdm_child_exit (DISPLAY_ABORT,
			    _("%s: Couldn't set userid to %d"),
			    "gdm_slave_greeter", GdmUserId);
	
	/* FIXME: clearing environment is likely fairly stupid,
	 * is there any reason we should do it anyway? */
	/* gdm_clearenv_no_lang (); */

	gnome_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
	gnome_setenv ("DISPLAY", d->name, TRUE);

	/* hackish ain't it */
	set_xnest_parent_stuff ();

	gnome_setenv ("LOGNAME", GdmUser, TRUE);
	gnome_setenv ("USER", GdmUser, TRUE);
	gnome_setenv ("USERNAME", GdmUser, TRUE);
	gnome_setenv ("GDM_GREETER_PROTOCOL_VERSION",
		      GDM_GREETER_PROTOCOL_VERSION, TRUE);
	gnome_setenv ("GDM_VERSION", VERSION, TRUE);

	pwent = getpwnam (GdmUser);
	if (pwent != NULL) {
		/* Note that usually this doesn't exist */
		if (pwent->pw_dir != NULL &&
		    g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
			gnome_setenv ("HOME", pwent->pw_dir, TRUE);
		else
			gnome_setenv ("HOME", "/", TRUE); /* Hack */
		gnome_setenv ("SHELL", pwent->pw_shell, TRUE);
	} else {
		gnome_setenv ("HOME", "/", TRUE); /* Hack */
		gnome_setenv ("SHELL", "/bin/sh", TRUE);
	}
	gnome_setenv ("PATH", GdmDefaultPath, TRUE);
	gnome_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

	/* Note that this is just informative, the slave will not listen to
	 * the greeter even if it does something it shouldn't on a non-local
	 * display so it's not a security risk */
	if (d->console) {
		gnome_setenv ("GDM_IS_LOCAL", "yes", TRUE);
	} else {
		gnome_unsetenv ("GDM_IS_LOCAL");
	}

	/* this is again informal only, if the greeter does time out it will
	 * not actually login a user if it's not enabled for this display */
	if (d->timed_login_ok) {
		if(ParsedTimedLogin == NULL)
			gnome_setenv ("GDM_TIMED_LOGIN_OK", " ", TRUE);
		else
			gnome_setenv ("GDM_TIMED_LOGIN_OK", ParsedTimedLogin, TRUE);
	} else {
		gnome_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	if (d->type == TYPE_FLEXI) {
		gnome_setenv ("GDM_FLEXI_SERVER", "yes", TRUE);
	} else if (d->type == TYPE_FLEXI_XNEST) {
		gnome_setenv ("GDM_FLEXI_SERVER", "Xnest", TRUE);
	} else {
		gnome_unsetenv ("GDM_FLEXI_SERVER");
	}

	if(gdm_emergency_server) {
		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("No servers were defined in the\n"
				 "configuration file and XDMCP was\n"
				 "disabled.  This can only be a\n"
				 "configuration error.  So I have started\n"
				 "a single server for you.  You should\n"
				 "log in and fix the configuration.\n"
				 "Note that automatic and timed logins\n"
				 "are disabled now."));
		gnome_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	if (d->failsafe_xserver) {
		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("I could not start the regular X\n"
				 "server (your graphical environment)\n"
				 "and so this is a failsafe X server.\n"
				 "You should log in and properly\n"
				 "configure the X server."));
	}

	if (d->busy_display) {
		char *msg = g_strdup_printf
			(_("The specified display number was busy, so "
			   "this server was started on display %s."),
			 d->name);
		gdm_error_box (d, GTK_MESSAGE_ERROR, msg);
		g_free (msg);
	}

	if (d->console)
		argv = ve_split (GdmGreeter);
	else
		argv = ve_split (GdmRemoteGreeter);
	execv (argv[0], argv);

	gdm_error (_("gdm_slave_greeter: Cannot start greeter trying default: %s"),
		   EXPANDED_BINDIR "/gdmlogin");

	gnome_setenv ("GDM_WHACKED_GREETER_CONFIG", "true", TRUE);

	argv = g_new0 (char *, 2);
	argv[0] = EXPANDED_BINDIR "/gdmlogin";
	argv[1] = NULL;
	execv (argv[0], argv);

	gdm_error_box (d,
		       GTK_MESSAGE_ERROR,
		       _("Cannot start the greeter program,\n"
			 "you will not be able to log in.\n"
			 "This display will be disabled.\n"
			 "Try logging in by other means and\n"
			 "editing the configuration file"));
	
	gdm_child_exit (DISPLAY_ABORT, _("%s: Error starting greeter on display %s"), "gdm_slave_greeter", d->name);
	
    case -1:
	d->greetpid = 0;
	gdm_slave_exit (DISPLAY_ABORT, _("%s: Can't fork gdmgreeter process"), "gdm_slave_greeter");
	
    default:
	close (pipe1[0]);
	close (pipe2[1]);

	fcntl(pipe1[1], F_SETFD, fcntl(pipe1[1], F_GETFD, 0) | FD_CLOEXEC);
	fcntl(pipe2[0], F_SETFD, fcntl(pipe2[0], F_GETFD, 0) | FD_CLOEXEC);

	whack_greeter_fds ();

	greeter_fd_out = pipe1[1];
	greeter_fd_in = pipe2[0];
	
	gdm_debug ("gdm_slave_greeter: Greeter on pid %d", (int)pid);

	gdm_slave_send_num (GDM_SOP_GREETPID, d->greetpid);

	run_pictures (); /* Append pictures to greeter if browsing is on */

	check_notifies_now ();
	break;
    }
}

static gboolean
parent_exists (void)
{
	pid_t ppid = getppid ();

	if (ppid <= 1 ||
	    kill (ppid, 0) < 0)
		return FALSE;
	return TRUE;
}

/* This should not call anything that could cause a syslog in case we
 * are in a signal */
void
gdm_slave_send (const char *str, gboolean wait_for_ack)
{
	int fd;
	char *fifopath;
	int i;
	uid_t old;

	if ( ! gdm_wait_for_ack)
		wait_for_ack = FALSE;

	if (gdm_in_signal == 0)
		gdm_debug ("Sending %s", str);

	if (wait_for_ack)
		gdm_got_ack = FALSE;

	fifopath = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
	old = geteuid ();
	if (old != 0)
		seteuid (0);
	fd = open (fifopath, O_WRONLY);
	if (old != 0)
		seteuid (old);
	g_free (fifopath);

	/* eek */
	if (fd < 0) {
		if (gdm_in_signal == 0)
			gdm_error (_("%s: Can't open fifo!"), "gdm_slave_send");
		return;
	}

	gdm_fdprintf (fd, "\n%s\n", str);

	close (fd);

	for (i = 0;
	     parent_exists () &&
	     wait_for_ack &&
	     ! gdm_got_ack &&
	     i < 10;
	     i++) {
		sleep (1);
	}

	if (wait_for_ack  &&
	    ! gdm_got_ack &&
	    gdm_in_signal == 0)
		gdm_error ("Timeout occured for sending message %s", str);
}

void
gdm_slave_send_num (const char *opcode, long num)
{
	char *msg;

	if (gdm_in_signal == 0)
		gdm_debug ("Sending %s == %ld for slave %ld",
			   opcode,
			   (long)num,
			   (long)getpid ());

	msg = g_strdup_printf ("%s %ld %ld", opcode,
			       (long)getpid (), (long)num);

	gdm_slave_send (msg, TRUE);

	g_free (msg);
}

void
gdm_slave_send_string (const char *opcode, const char *str)
{
	char *msg;

	/* Evil!, all this for debugging? */
	if (GdmDebug && gdm_in_signal == 0) {
		if (strcmp (opcode, GDM_SOP_COOKIE) == 0)
			gdm_debug ("Sending %s == <secret> for slave %ld",
				   opcode,
				   (long)getpid ());
		else
			gdm_debug ("Sending %s == %s for slave %ld",
				   opcode,
				   ve_sure_string (str),
				   (long)getpid ());
	}

	msg = g_strdup_printf ("%s %ld %s", opcode,
			       (long)getpid (), ve_sure_string (str));

	gdm_slave_send (msg, TRUE);

	g_free (msg);
}

static void
send_chosen_host (GdmDisplay *disp, const char *hostname)
{
	int fd;
	char *fifopath;
	struct hostent *host;
	uid_t old;

	host = gethostbyname (hostname);

	if (host == NULL) {
		gdm_error ("Cannot get address of host '%s'", hostname);
		return;
	}

	gdm_debug ("Sending chosen host address (%s) %s",
		   hostname, inet_ntoa (*(struct in_addr *)host->h_addr_list[0]));

	fifopath = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);

	old = geteuid ();
	if (old != 0)
		seteuid (0);
	fd = open (fifopath, O_WRONLY);
	if (old != 0)
		seteuid (old);

	g_free (fifopath);

	/* eek */
	if (fd < 0) {
		gdm_error (_("%s: Can't open fifo!"), "send_chosen_host");
		return;
	}

	gdm_fdprintf (fd, "\n%s %d %s\n", GDM_SOP_CHOSEN,
		      disp->indirect_id,
		      inet_ntoa (*((struct in_addr *)host->h_addr_list[0])));

	close (fd);
}


static void
gdm_slave_chooser (void)
{
	gint p[2];
	gchar **argv;
	struct passwd *pwent;
	char buf[1024];
	size_t bytes;
	pid_t pid;

	gdm_debug ("gdm_slave_chooser: Running chooser on %s", d->name);

	/* Open a pipe for chooser communications */
	if (pipe (p) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_chooser: Can't init pipe to gdmchooser"));

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (d, GdmDisplayInit, NULL, NULL,
			       FALSE /* pass_stdout */,
			       TRUE /* set_parent */);

	/* Fork. Parent is gdmslave, child is greeter process. */
	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->chooserpid = fork ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	switch (pid) {

	case 0:
		setsid ();

		gdm_unset_signals ();

		/* Plumbing */
		close (p[0]);

		if (p[1] != STDOUT_FILENO) 
			dup2 (p[1], STDOUT_FILENO);

		closelog ();

		close (0);
		gdm_close_all_descriptors (2 /* from */, -1 /* except */);

		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		if (setgid (GdmGroupId) < 0) 
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set groupid to %d"),
					"gdm_slave_chooser", GdmGroupId);

		if (initgroups (GdmUser, GdmGroupId) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: initgroups() failed for %s"),
					"gdm_slave_chooser", GdmUser);

		if (setuid (GdmUserId) < 0) 
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set userid to %d"),
					"gdm_slave_chooser", GdmUserId);

		/* FIXME: clearing environment is likely fairly stupid,
		 * is there any reason we should do it anyway? */
		/* gdm_clearenv_no_lang (); */

		gnome_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		gnome_setenv ("DISPLAY", d->name, TRUE);

		gnome_setenv ("LOGNAME", GdmUser, TRUE);
		gnome_setenv ("USER", GdmUser, TRUE);
		gnome_setenv ("USERNAME", GdmUser, TRUE);

		gnome_setenv ("GDM_VERSION", VERSION, TRUE);

		pwent = getpwnam (GdmUser);
		if (pwent != NULL) {
			/* Note that usually this doesn't exist */
			if (g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
				gnome_setenv ("HOME", pwent->pw_dir, TRUE);
			else
				gnome_setenv ("HOME", "/", TRUE); /* Hack */
			gnome_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			gnome_setenv ("HOME", "/", TRUE); /* Hack */
			gnome_setenv ("SHELL", "/bin/sh", TRUE);
		}
		gnome_setenv ("PATH", GdmDefaultPath, TRUE);
		gnome_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		argv = ve_split (GdmChooser);
		execv (argv[0], argv);

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Cannot start the chooser program,\n"
				 "you will not be able to log in.\n"
				 "Please contact the system administrator.\n"));

		gdm_child_exit (DISPLAY_ABORT, _("gdm_slave_chooser: Error starting chooser on display %s"), d->name);

	case -1:
		gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_chooser: Can't fork gdmchooser process"));

	default:
		gdm_debug ("gdm_slave_chooser: Chooser on pid %d", d->chooserpid);
		gdm_slave_send_num (GDM_SOP_CHOOSERPID, d->chooserpid);

		close (p[1]);

		fcntl(p[0], F_SETFD, fcntl(p[0], F_GETFD, 0) | FD_CLOEXEC);

		gdm_sigchld_block_push ();
		/* wait for the chooser to die */
		if (d->chooserpid > 0)
			ve_waitpid_no_signal (d->chooserpid, 0, 0);
		d->chooserpid  = 0;
		gdm_sigchld_block_pop ();


		gdm_slave_send_num (GDM_SOP_CHOOSERPID, 0);

		/* Note: Nothing affecting the chooser needs update
		 * from notifies */

		bytes = read (p[0], buf, sizeof(buf)-1);
		if (bytes > 0) {
			close (p[0]);

			if (buf[bytes-1] == '\n')
				buf[bytes-1] ='\0';
			else
				buf[bytes] ='\0';
			send_chosen_host (d, buf);

			gdm_slave_quick_exit (DISPLAY_CHOSEN);
		}

		close (p[0]);
		break;
	}
}

static void
read_sessions (FILE *fp, GString *sessions, const char *def, gboolean *got_def)
{
	char buf[FIELD_SIZE];

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p, *session;
		if (buf[0] != '[')
			continue;
		p = strrchr (buf, ']');
		if (p == NULL)
			continue;
		*p = '\0';

		session = &buf[1];

		if (strcmp (session, "Trash") == 0 ||
		    strcmp (session, "Chooser") == 0 ||
		    strcmp (session, "Warner") == 0)
			continue;

		if (strcmp (session, def) == 0)
			*got_def = TRUE;

		g_string_append_c (sessions, '\n');
		g_string_append (sessions, session);
	}
}

static char *
gdm_get_sessions (struct passwd *pwent)
{
	gboolean session_ok;
	gboolean options_ok;
	char *cfgdir;
	GString *sessions = g_string_new (NULL);
	char *def;
	gboolean got_def = FALSE;
	int session_relax_perms;

	setegid (pwent->pw_gid);
	seteuid (pwent->pw_uid);

	/* we know this exists already, code has already created it,
	 * when checking the gsm saved options */
	cfgdir = g_strconcat (pwent->pw_dir, "/.gnome2", NULL);

	/* We cannot be absolutely strict about the session
	 * permissions, since by default they will be writable
	 * by group and there's nothing we can do about it.  So
	 * we relax the permission checking in this case */
	session_relax_perms = GdmRelaxPerms;
	if (session_relax_perms == 0)
		session_relax_perms = 1;

	/* Sanity check on ~user/.gnome2/session */
	session_ok = gdm_file_check ("gdm_get_sessions", pwent->pw_uid,
				     cfgdir, "session",
				     TRUE, GdmSessionMaxFile,
				     session_relax_perms);
	/* Sanity check on ~user/.gnome2/session-options */
	options_ok = gdm_file_check ("gdm_get_sessions", pwent->pw_uid,
				     cfgdir, "session-options",
				     TRUE, GdmUserMaxFile,
				     session_relax_perms);

	g_free (cfgdir);

	if (options_ok) {
		char *cfgstr;

		cfgstr = g_strconcat
			("=", pwent->pw_dir,
			 "/.gnome2/session-options=/Options/CurrentSession",
			 NULL);
		def = gnome_config_get_string (cfgstr);
		if (def == NULL)
			def = g_strdup ("Default");

		g_free (cfgstr);
	} else {
		def = g_strdup ("Default");
	}

	/* the currently selected comes first (it will come later
	 * as well, the first position just selects the session) */
	g_string_append (sessions, def);

	got_def = FALSE;
	if (session_ok) {
		char *sessfile = g_strconcat (pwent->pw_dir,
					      "/.gnome2/session", NULL);
		FILE *fp = fopen (sessfile, "r");
		if (fp == NULL &&
		    GdmGnomeDefaultSession != NULL) {
			fp = fopen (GdmGnomeDefaultSession, "r");
		}
		if (fp != NULL) {
			read_sessions (fp, sessions, def, &got_def);
			fclose (fp);
		}
		g_free (sessfile);
	}

	if ( ! got_def) {
		g_string_append_c (sessions, '\n');
		g_string_append (sessions, def);
	}

	g_free (def);

	seteuid (0);
	setegid (GdmGroupId);

	{
		char *ret = sessions->str;
		g_string_free (sessions, FALSE);
		return ret;
	}
}

static gboolean
is_session_ok (const char *session_name)
{
	char *file;

	/* these are always OK */
	if (strcmp (session_name, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	    strcmp (session_name, GDM_SESSION_FAILSAFE_XTERM) == 0)
		return TRUE;

	if (ve_string_empty (GdmSessDir))
		return FALSE;

	file = g_strconcat (GdmSessDir, "/", session_name, NULL);
	if (access (file, X_OK) == 0) {
		g_free (file);
		return TRUE;
	}
	g_free (file);
	return FALSE;
}

static char *
find_a_session (void)
{
	char *try[] = {
		"Gnome",
		"gnome",
		"GNOME",
		"Default",
		"default",
		"Xsession",
		"Failsafe",
		"failsafe",
		NULL
	};
	int i;
	char *session;

	session = NULL;
	for (i = 0; try[i] != NULL && session == NULL; i ++) {
		if (is_session_ok (try[i]))
			session = g_strdup (try[i]);
	}
	return session;
}

static char *
find_prog (const char *name, const char *args, char **retpath)
{
	char *ret;
	char *path;
	int i;
	char *try[] = {
		"/usr/bin/X11/",
		"/usr/X11R6/bin/",
		"/opt/X11R6/bin/",
		"/usr/bin/",
		"/usr/local/bin/",
		EXPANDED_BINDIR "/",
		NULL
	};

	path = g_find_program_in_path (name);
	if (path != NULL &&
	    access (path, X_OK) == 0) {
		ret = g_strdup_printf ("%s %s", path, args);
		*retpath = path;
		return ret;
	}
	g_free (path);
	for (i = 0; try[i] != NULL; i++) {
		path = g_strconcat (try[i], name, NULL);
		if (access (path, X_OK) == 0) {
			ret = g_strdup_printf ("%s %s", path, args);
			*retpath = path;
			return ret;
		}
		g_free (path);
	}
	*retpath = NULL;
	return NULL;
}

static char *
dequote (const char *in)
{
	GString *str;
	char *out;
	const char *p;

	str = g_string_new (NULL);

	for (p = in; *p != '\0'; p++) {
		if (*p == '\'')
			g_string_append (str, "'\\''");
		else
			g_string_append_c (str, *p);
	}

	out = str->str;
	g_string_free (str, FALSE);
	return out;
}

static void
session_child_run (struct passwd *pwent,
		   const char *home_dir,
		   gboolean home_dir_ok,
		   const char *session,
		   const char *save_session,
		   const char *language,
		   const char *gnome_session,
		   gboolean usrcfgok,
		   gboolean savesess,
		   gboolean savelang,
		   gboolean sessoptok,
		   gboolean savegnomesess)
{
	int logfd;
	gboolean failsafe = FALSE;
	char *sesspath, *sessexec;
	gboolean need_config_sync = FALSE;
	const char *shell = NULL;
	Display *disp;

	gdm_unset_signals ();

	gnome_setenv ("XAUTHORITY", d->authfile, TRUE);

	disp = XOpenDisplay (d->name);
	if (disp != NULL) {
		Cursor xcursor;
			
		XSetInputFocus (disp, PointerRoot,
				RevertToPointerRoot, CurrentTime);

		/* return left pointer */
		xcursor = XCreateFontCursor (disp, GDK_LEFT_PTR);
		XDefineCursor (disp,
			       DefaultRootWindow (disp),
			       xcursor);
		XFreeCursor (disp, xcursor);
		XSync (disp, False);

		XCloseDisplay (disp);
	}

	if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	    strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
	    /* hack */
	    g_ascii_strcasecmp (session, "Failsafe") == 0) {
		failsafe = TRUE;
	}

	/* Here we setup our 0,1,2 descriptors, we do it here
	 * nowdays rather then later on so that we get errors even
	 * from the PreSession script */
        /* Log all output from session programs to a file,
	 * unless in failsafe mode which needs to work when there is
	 * no diskspace as well */
	if ( ! failsafe && home_dir_ok) {
		char *filename = g_strconcat (home_dir,
					      "/.xsession-errors",
					      NULL);
		uid_t old = geteuid ();
		uid_t oldg = getegid ();

		/* unlink the filename to be anal (as root to get rid of
		 * possible old versions with root ownership) */
		unlink (filename);

		setegid (pwent->pw_gid);
		seteuid (pwent->pw_uid);
		logfd = open (filename, O_CREAT|O_TRUNC|O_WRONLY, 0644);
		seteuid (old);
		setegid (oldg);

		g_free (filename);

		if (logfd != -1) {
			dup2 (logfd, 1);
			dup2 (logfd, 2);
			close (logfd);
		} else {
			close (1);
			close (2);
			gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
			gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
			gdm_error (_("%s: Could not open ~/.xsession-errors"),
				   "run_session_child");
		}
	} else {
		close (1);
		close (2);
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
	}

	close (0);
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

	/* Run the PreSession script */
	if (gdm_slave_exec_script (d, GdmPreSession,
				   login, pwent,
				   TRUE /* pass_stdout */,
				   TRUE /* set_parent */) != EXIT_SUCCESS &&
	    /* ignore errors in failsafe modes */
	    ! failsafe) 
		/* If script fails reset X server and restart greeter */
		gdm_child_exit (DISPLAY_REMANAGE,
				_("gdm_slave_session_start: Execution of PreSession script returned > 0. Aborting."));

	gdm_clearenv ();

	if (setsid() < 0)
		/* should never happen */
		gdm_error (_("%s: setsid() failed: %s!"),
			   "session_child_run", strerror(errno));

	/* Prepare user session */
	gnome_setenv ("XAUTHORITY", d->userauth, TRUE);
	gnome_setenv ("DISPLAY", d->name, TRUE);
	gnome_setenv ("LOGNAME", login, TRUE);
	gnome_setenv ("USER", login, TRUE);
	gnome_setenv ("USERNAME", login, TRUE);
	gnome_setenv ("HOME", home_dir, TRUE);
	gnome_setenv ("GDMSESSION", session, TRUE);
	gnome_setenv ("SHELL", pwent->pw_shell, TRUE);
	gnome_unsetenv ("MAIL");	/* Unset $MAIL for broken shells */

	if (gnome_session != NULL)
		gnome_setenv ("GDM_GNOME_SESSION", gnome_session, TRUE);

	/* Special PATH for root */
	if (pwent->pw_uid == 0)
		gnome_setenv ("PATH", GdmRootPath, TRUE);
	else
		gnome_setenv ("PATH", GdmDefaultPath, TRUE);

	/* Eeeeek, this no lookie as a correct language code,
	 * just use the system default */
	if ( ! ve_string_empty (language) &&
	     ! ve_locale_exists (language)) {
		char *msg = g_strdup_printf (_("Language %s does not exist, using %s"),
					     language, _("System default"));
		gdm_error_box (d, GTK_MESSAGE_ERROR, msg);
		language = NULL;
	}

	setpgid (0, 0);
	
	umask (022);
	
	/* setup the verify env vars */
	if ( ! gdm_verify_setup_env (d))
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Could not setup environment for %s. "
				  "Aborting."),
				"gdm_slave_session_start", login);

	/* setup egid to the correct group,
	 * not to leave the egid around */
	setegid (pwent->pw_gid);

#ifdef HAVE_LOGINCAP
	if (setusercontext (NULL, pwent, pwent->pw_uid,
			    LOGIN_SETLOGIN | LOGIN_SETPATH |
			    LOGIN_SETPRIORITY | LOGIN_SETRESOURCES |
			    LOGIN_SETUMASK | LOGIN_SETUSER) < 0)
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: setusercontext() failed for %s. "
				  "Aborting."), "gdm_slave_session_start",
				login);
#else
	if (setuid (pwent->pw_uid) < 0) 
		gdm_child_exit (DISPLAY_REMANAGE,
				_("gdm_slave_session_start: Could not become %s. Aborting."), login);
#endif

	/* Only force GDM_LANG to something if there is other then
	 * system default selected.  Else let the session do whatever it
	 * does since we're using sys default */
	if ( ! ve_string_empty (language)) {
		gnome_setenv ("LANG", language, TRUE);
		gnome_setenv ("GDM_LANG", language, TRUE);
	}
	
	chdir (home_dir);

	/* anality, make sure nothing is in memory for gnome_config
	 * to write */
	gnome_config_drop_all ();
	
	if (usrcfgok && savesess && home_dir_ok) {
		gchar *cfgstr = g_strconcat ("=", home_dir,
					     "/.gnome2/gdm=/session/last", NULL);
		gnome_config_set_string (cfgstr, save_session);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}
	
	if (usrcfgok && savelang && home_dir_ok) {
		gchar *cfgstr = g_strconcat ("=", home_dir,
					     "/.gnome2/gdm=/session/lang", NULL);
		/* we chose the system default language so wipe the lang key */
		if (ve_string_empty (language))
			gnome_config_clean_key (cfgstr);
		else
			gnome_config_set_string (cfgstr, language);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}

	if (sessoptok &&
	    savegnomesess &&
	    gnome_session != NULL &&
	    home_dir_ok) {
		gchar *cfgstr = g_strconcat ("=", home_dir, "/.gnome2/session-options=/Options/CurrentSession", NULL);
		gnome_config_set_string (cfgstr, gnome_session);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}

	if (need_config_sync) {
		gnome_config_sync ();
		gnome_config_drop_all ();
	}

	closelog ();

	gdm_close_all_descriptors (3 /* from */, -1 /* except */);

	openlog ("gdm", LOG_PID, LOG_DAEMON);
	
	/* If "Gnome Chooser" is still set as a session,
	 * just change that to "Gnome", since "Gnome Chooser" is a
	 * fake */
	if (strcmp (session, GDM_SESSION_GNOME_CHOOSER) == 0) {
		session = "Gnome";
	}
	
	sesspath = NULL;
	sessexec = NULL;

	if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0) {
		sesspath = find_prog ("gnome-session",
				      "--failsafe",
				      &sessexec);
		if (sesspath == NULL) {
			/* yaikes */
			gdm_error (_("gdm_slave_session_start: gnome-session not found for a failsafe gnome session, trying xterm"));
			session = GDM_SESSION_FAILSAFE_XTERM;
			gdm_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("Could not find the GNOME installation,\n"
				   "will try running the \"Failsafe xterm\"\n"
				   "session."));
		} else {
			gdm_error_box
				(d, GTK_MESSAGE_INFO,
				 _("This is the Failsafe Gnome session.\n"
				   "You will be logged into the 'Default'\n"
				   "session of Gnome with no startup scripts\n"
				   "run.  This is only to fix problems in\n"
				   "your installation."));
		}
		failsafe = TRUE;
	}

	/* an if and not an else, we could have done a fall-through
	 * to here in the above code if we can't find gnome-session */
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0) {
		char *params = g_strdup_printf ("-geometry 80x24-%d-%d",
						d->lrh_offsetx,
						d->lrh_offsety);
		sesspath = find_prog ("xterm",
				      params,
				      &sessexec);
		g_free (params);
		if (sesspath == NULL) {
			gdm_error_box (d, GTK_MESSAGE_ERROR,
				       _("Cannot find \"xterm\" to start "
					 "a failsafe session."));
			/* nyah nyah nyah nyah nyah */
			_exit (0);
		} else {
			gdm_error_box
				(d, GTK_MESSAGE_INFO,
				 _("This is the Failsafe xterm session.\n"
				   "You will be logged into a terminal\n"
				   "console so that you may fix your system\n"
				   "if you cannot log in any other way.\n"
				   "To exit the terminal emulator, type\n"
				   "'exit' and an enter into the window."));
			focus_first_x_window ("xterm");
		}
		failsafe = TRUE;
	} 

	/* hack */
	if (strcmp (session, "Failsafe") == 0) {
		failsafe = TRUE;
	}

	if (sesspath == NULL) {
		if (GdmSessDir != NULL) {
			sesspath = g_strconcat
				("'", GdmSessDir, "/",
				 dequote (session), "'", NULL);
			sessexec = g_strconcat (GdmSessDir, "/",
						session, NULL);
		} else {
			sesspath = g_strdup ("'/Eeeeek! Eeeeek!'");
		}
	}
	
	gdm_debug (_("Running %s for %s on %s"),
		   sesspath, login, d->name);

	if ( ! ve_string_empty (pwent->pw_shell)) {
		shell = pwent->pw_shell;
	} else {
		shell = "/bin/sh";
	}

	/* just a stupid test, the below would fail, but this gives a better
	 * message */
	if (strcmp (shell, "/sbin/nologin") == 0 ||
	    strcmp (shell, "/bin/false") == 0 ||
	    strcmp (shell, "/bin/true") == 0) {
		gdm_error (_("gdm_slave_session_start: User not allowed to log in"));
		gdm_error_box (d, GTK_MESSAGE_ERROR,
			       _("The system administrator has\n"
				 "disabled your account."));
	} else if (access (sessexec != NULL ? sessexec : sesspath, X_OK) != 0) {
		gdm_error (_("gdm_slave_session_start: Could not find/run session `%s'"), sesspath);
		/* if we can't read and exec the session, then make a nice
		 * error dialog */
		gdm_error_box
			(d, GTK_MESSAGE_ERROR,
			 _("Cannot start the session, most likely the\n"
			   "session does not exist.  Please select from\n"
			   "the list of available sessions in the login\n"
			   "dialog window."));
	} else {
		char *exec = g_strconcat ("exec ", sesspath, NULL);
		char *shellbase = g_path_get_basename (shell);
		/* FIXME: this is a hack currently this is all screwed up
		 * so I won't run the users shell unless it's one of the
		 * "listed" ones, I'll just run bash or sh,
		 * that's a bit evil but in the end it works out better in
		 * fact.  In the future we will do our own login setup
		 * stuff */
		if (strcmp (shellbase, "sh") != 0 &&
		    strcmp (shellbase, "bash") != 0 &&
		    strcmp (shellbase, "tcsh") != 0 &&
		    strcmp (shellbase, "ksh") != 0 &&
		    strcmp (shellbase, "pdksh") != 0 &&
		    strcmp (shellbase, "zsh") != 0 &&
		    strcmp (shellbase, "csh") != 0 &&
		    strcmp (shellbase, "ash") != 0 &&
		    strcmp (shellbase, "bsh") != 0 &&
		    strcmp (shellbase, "bash2") != 0) {
			if (access ("/bin/bash", R_OK|X_OK) == 0)
				shell = "/bin/bash";
			else if (access ("/bin/bash2", R_OK|X_OK) == 0)
				shell = "/bin/bash2";
			else
				shell = "/bin/sh";
		}
		execl (shell, "-", "-c", exec, NULL);
		/* nutcase fallback */
		execl ("/bin/sh", "-", "-c", exec, NULL);

		gdm_error (_("gdm_slave_session_start: Could not start session `%s'"), sesspath);
		gdm_error_box
			(d, GTK_MESSAGE_ERROR,
			 _("Cannot start your shell.  It could be that the\n"
			   "system administrator has disabled your login.\n"
			   "It could also indicate an error with your account.\n"));
	}
	
	/* ends as if nothing bad happened */
	_exit (0);
}

static void
gdm_slave_session_start (void)
{
    struct stat statbuf;
    struct passwd *pwent;
    char *save_session = NULL, *session = NULL, *language = NULL, *usrsess, *usrlang;
    char *gnome_session = NULL;
    gboolean savesess = FALSE, savelang = FALSE, savegnomesess = FALSE;
    gboolean usrcfgok = FALSE, sessoptok = FALSE, authok = FALSE;
    const char *home_dir = NULL;
    gboolean home_dir_ok = FALSE;
    time_t session_start_time, end_time; 
    gboolean failsafe = FALSE;
    pid_t pid;

    gdm_debug ("gdm_slave_session_start: Attempting session for user '%s'",
	       login);

    pwent = getpwnam (login);

    if (pwent == NULL)  {
	    /* This is sort of an "assert", this should NEVER happen */
	    if (greet)
		    gdm_slave_whack_greeter();
	    gdm_slave_exit (DISPLAY_REMANAGE,
			    _("gdm_slave_session_start: User passed auth but getpwnam(%s) failed!"), login);
    }

    if (pwent->pw_dir == NULL ||
	! g_file_test (pwent->pw_dir, G_FILE_TEST_IS_DIR)) {
	    char *msg = g_strdup_printf (
		     _("Your home directory is listed as:\n'%s'\n"
		       "but it does not appear to exist.\n"
		       "Do you want to log in with the root\n"
		       "directory as your home directory?\n\n"
		       "It is unlikely anything will work unless\n"
		       "you use a failsafe session."),
		     ve_sure_string (pwent->pw_dir));

	    gdm_error (_("%s: Home directory for %s: '%s' does not exist!"),
		       "gdm_slave_session_start",
		       login,
		       ve_sure_string (pwent->pw_dir));

	    /* Does the user want to piss off or try to do stupid crap? */
	    if ( ! gdm_failsafe_yesno (d, msg)) {
		    g_free (msg);
		    gdm_verify_cleanup (d);
		    return;
	    }

	    g_free (msg);

	    home_dir_ok = FALSE;
	    home_dir = "/";
    } else {
	    home_dir_ok = TRUE;
	    home_dir = pwent->pw_dir;
    }

    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    if (home_dir_ok) {
	    char *cfgdir;
	    /* Check if ~user/.gnome2 exists. Create it otherwise. */
	    cfgdir = g_strconcat (home_dir, "/.gnome2", NULL);

	    if (stat (cfgdir, &statbuf) == -1) {
		    mkdir (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
		    chmod (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	    }

	    /* Sanity check on ~user/.gnome2/gdm */
	    usrcfgok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
				       cfgdir, "gdm", TRUE, GdmUserMaxFile,
				       GdmRelaxPerms);
	    /* Sanity check on ~user/.gnome2/session-options */
	    sessoptok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
					cfgdir, "session-options", TRUE, GdmUserMaxFile,
					/* We cannot be absolutely strict about the
					 * session permissions, since by default they
					 * will be writable by group and there's
					 * nothing we can do about it.  So we relax
					 * the permission checking in this case */
					GdmRelaxPerms == 0 ? 1 : GdmRelaxPerms);
	    g_free (cfgdir);
    } else {
	    usrcfgok = FALSE;
	    sessoptok = FALSE;
    }

    if (usrcfgok) {
	gchar *cfgstr;

	cfgstr = g_strconcat ("=", home_dir, "/.gnome2/gdm=/session/last", NULL);
	usrsess = gnome_config_get_string (cfgstr);
	if (usrsess == NULL)
		usrsess = g_strdup ("");
	g_free (cfgstr);

	cfgstr = g_strconcat ("=", home_dir, "/.gnome2/gdm=/session/lang", NULL);
	usrlang = gnome_config_get_string (cfgstr);
	if (usrlang == NULL)
		usrlang = g_strdup ("");
	g_free (cfgstr);
    } else {
	usrsess = g_strdup ("");
	usrlang = g_strdup ("");
    }

    seteuid (0);
    setegid (GdmGroupId);

    if (greet) {
	    session = gdm_slave_greeter_ctl (GDM_SESS, usrsess);
	    language = gdm_slave_greeter_ctl (GDM_LANG, usrlang);
    } else {
	    session = g_strdup (usrsess);
	    language = g_strdup (usrlang);
    }

    g_free (usrsess);
    g_free (usrlang);
    
    if (ve_string_empty (session)) {
	    g_free (session);
	    session = find_a_session ();
	    if (session == NULL) {
		    /* we're running out of options */
		    session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	    }
    }

    if (ve_string_empty (language)) {
	    g_free (language);
	    language = NULL;
    }

    /* save this session as the users session */
    save_session = g_strdup (session);

    if (greet) {
	    char *ret = gdm_slave_greeter_ctl (GDM_SSESS, "");
	    if ( ! ve_string_empty (ret))
		    savesess = TRUE;
	    g_free (ret);

	    ret = gdm_slave_greeter_ctl (GDM_SLANG, "");
	    if ( ! ve_string_empty (ret))
		    savelang = TRUE;
	    g_free (ret);

	    if (strcmp (session, GDM_SESSION_GNOME_CHOOSER) == 0) {
		    char *sessions = gdm_get_sessions (pwent);
		    ret = gdm_slave_greeter_ctl (GDM_GNOMESESS, sessions);
		    g_free (sessions);

		    if (ret != NULL && ret[0] != '\0') {
			    gnome_session = ret;
			    ret = NULL;
			    g_free (session);
			    session = g_strdup ("Gnome");
		    }
		    g_free (ret);

		    ret = gdm_slave_greeter_ctl (GDM_SGNOMESESS, "");
		    if ( ! ve_string_empty (ret)) {
			    savegnomesess = TRUE;
		    }
		    g_free (ret);
	    }

	    gdm_debug ("gdm_slave_session_start: Authentication completed. Whacking greeter");

	    gdm_slave_whack_greeter ();
    }

    /* Ensure some sanity in this world */
    gdm_ensure_sanity ();

    if (GdmKillInitClients)
	    gdm_server_whack_clients (d);

    /* Setup cookie -- We need this information during cleanup, thus
     * cookie handling is done before fork()ing */

    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    authok = gdm_auth_user_add (d, pwent->pw_uid,
				/* Only pass the home_dir if
				 * it was ok */
				home_dir_ok ? home_dir : NULL);

    seteuid (0);
    setegid (GdmGroupId);
    
    if ( ! authok) {
	    gdm_debug ("gdm_slave_session_start: Auth not OK");

	    gnome_setenv ("XAUTHORITY", d->authfile, TRUE);

	    gdm_error_box (d,
			   GTK_MESSAGE_ERROR,
			   _("GDM could not write to your authorization\n"
			     "file.  This could mean that you are out of\n"
			     "disk space or that your home directory could\n"
			     "not be opened for writing.  In any case, it\n"
			     "is not possible to log in.  Please contact\n"
			     "your system administrator"));

	    gdm_slave_session_stop (FALSE /* run_post_session */);

	    gdm_slave_quick_exit (DISPLAY_REMANAGE);
    }

    if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
	g_ascii_strcasecmp (session, "Failsafe") == 0 /* hack */)
	    failsafe = TRUE;

    /* Write out the Xservers file */
    gdm_slave_send_num (GDM_SOP_WRITE_X_SERVERS, 0 /* bogus */);

    /* don't completely rely on this, the user
     * could reset time or do other crazy things */
    session_start_time = time (NULL);

    /* Start user process */
    gdm_sigchld_block_push ();
    gdm_sigterm_block_push ();
    pid = d->sesspid = fork ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();

    switch (pid) {
	
    case -1:
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_session_start: Error forking user session"));
	
    case 0:
	/* Never returns */
	session_child_run (pwent,
			   home_dir,
			   home_dir_ok,
			   session,
			   save_session,
			   language,
			   gnome_session,
			   usrcfgok,
			   savesess,
			   savelang,
			   sessoptok,
			   savegnomesess);
	g_assert_not_reached ();
	
    default:
	break;
    }

    g_free (session);
    g_free (save_session);
    g_free (language);
    g_free (gnome_session);

    /* we block sigchld now that we're here */
    gdm_sigchld_block_push ();

    if (d->sesspid > 0) {
	    gdm_slave_send_num (GDM_SOP_SESSPID, pid);

	    gdm_sigchld_block_pop ();

	    ve_waitpid_no_signal (pid, NULL, 0);

	    d->sesspid = 0;
    } else {
	    gdm_sigchld_block_pop ();
    }

    end_time = time (NULL);

    gdm_debug ("Session: start_time: %ld end_time: %ld",
	       (long)session_start_time, (long)end_time);

    if  ((/* sanity */ end_time >= session_start_time) &&
	 (end_time - 10 <= session_start_time)) {
	    char *errfile = g_strconcat (home_dir, "/.xsession-errors", NULL);
	    gdm_debug ("Session less then 10 seconds!");

	    gnome_setenv ("XAUTHORITY", d->authfile, TRUE);

	    /* FIXME: perhaps do some checking to display a better error,
	     * such as gnome-session missing and such things. */
	    gdm_error_box_full (d,
				GTK_MESSAGE_WARNING,
				_("Your session only lasted less then\n"
				  "10 seconds.  If you have not logged out\n"
				  "yourself, this could mean that there is\n"
				  "some installation problem or that you may\n"
				  "be out of diskspace.  Try logging in with\n"
				  "one of the failsafe sessions to see if you\n"
				  "can fix this problem."),
				(home_dir_ok && ! failsafe) ?
			       	  _("View details (~/.xsession-errors file)") :
				  NULL,
				errfile);
	    g_free (errfile);
    }

    gdm_debug ("gdm_slave_session_start: Session ended OK");

    gdm_slave_session_stop (pid != 0);
}


/* Stop any in progress sessions */
static void
gdm_slave_session_stop (gboolean run_post_session)
{
    struct passwd *pwent;
    char *x_servers_file;
    char *local_login;

    local_login = login;
    login = NULL;

    seteuid (0);
    setegid (0);

    gdm_slave_send_num (GDM_SOP_SESSPID, 0);

    gdm_debug ("gdm_slave_session_stop: %s on %s", local_login, d->name);

    /* Note we use the info in the structure here since if we get passed
     * a 0 that means the process is already dead.
     * FIXME: Maybe we should waitpid here, note make sure this will
     * not create a hang! */
    gdm_sigchld_block_push ();
    if (d->sesspid > 1)
	    kill (- (d->sesspid), SIGTERM);
    gdm_sigchld_block_pop ();

    gdm_verify_cleanup (d);
    
    if (local_login == NULL)
	    pwent = NULL;
    else
	    pwent = getpwnam (local_login);	/* PAM overwrites our pwent */

    x_servers_file = g_strconcat (GdmServAuthDir,
				  "/", d->name, ".Xservers", NULL);

    /* if there was a session that ran, run the PostSession script */
    if (run_post_session) {
	    /* setup some env for PostSession script */
	    gnome_setenv ("DISPLAY", d->name, TRUE);
	    gnome_setenv ("XAUTHORITY", d->authfile, TRUE);

	    gnome_setenv ("X_SERVERS", x_servers_file, TRUE);
	    if (d->type == TYPE_XDMCP)
		    gnome_setenv ("REMOTE_HOST", d->hostname, TRUE);

	    /* Execute post session script */
	    gdm_debug ("gdm_slave_session_stop: Running post session script");
	    gdm_slave_exec_script (d, GdmPostSession, local_login, pwent,
				   FALSE /* pass_stdout */,
				   TRUE /* set_parent */);

	    gnome_unsetenv ("X_SERVERS");
	    if (d->type == TYPE_XDMCP)
		    gnome_unsetenv ("REMOTE_HOST");
    }

    unlink (x_servers_file);
    g_free (x_servers_file);

    g_free (local_login);

    if (pwent != NULL) {
	    /* Remove display from ~user/.Xauthority */
	    setegid (pwent->pw_gid);
	    seteuid (pwent->pw_uid);

	    gdm_auth_user_remove (d, pwent->pw_uid);

	    seteuid (0);
	    setegid (0);
    }

    /* things are going to be killed, so ignore errors */
    XSetErrorHandler (ignore_xerror_handler);

    /* Cleanup */
    gdm_debug ("gdm_slave_session_stop: Severing connection");
    if (d->dsp != NULL) {
	    XCloseDisplay (d->dsp);
	    d->dsp = NULL;
    }
}

static void
gdm_slave_term_handler (int sig)
{
	gdm_in_signal++;
	gdm_wait_for_ack = FALSE;

	gdm_debug ("gdm_slave_term_handler: %s got TERM/INT signal", d->name);

	gdm_slave_session_stop (d->logged_in && login != NULL);

	gdm_debug ("gdm_slave_term_handler: Final cleanup");

	gdm_slave_quick_exit (DISPLAY_ABORT);
}

/* called on alarms to ping */
static void
gdm_slave_alrm_handler (int sig)
{
	static gboolean in_ping = FALSE;

	gdm_in_signal++;

	gdm_debug ("gdm_slave_alrm_handler: %s got ARLM signal, "
		   "to ping display", d->name);

	if (d->dsp == NULL) {
		gdm_in_signal --;
		/* huh? */
		return;
	}

	if (in_ping) {
		/* darn, the last ping didn't succeed, wipe this display */
		gdm_slave_session_stop (d->logged_in && login != NULL);

		gdm_slave_exit (DISPLAY_REMANAGE, 
				_("Ping to %s failed, whacking display!"),
				d->name);
	}

	in_ping = TRUE;

	/* schedule next alarm */
	alarm (GdmPingInterval * 60);

	XSync (d->dsp, True);

	in_ping = FALSE;

	gdm_in_signal --;
}

/* Called on every SIGCHLD */
static void 
gdm_slave_child_handler (int sig)
{
    gint status;
    pid_t pid;

    gdm_in_signal++;

    gdm_debug ("gdm_slave_child_handler");
    
    while ((pid = waitpid (-1, &status, WNOHANG)) > 0) {
	gdm_debug ("gdm_slave_child_handler: %d died", pid);
	
	if (WIFEXITED (status))
	    gdm_debug ("gdm_slave_child_handler: %d returned %d",
		       (int)pid, (int)WEXITSTATUS (status));
	if (WIFSIGNALED (status))
	    gdm_debug ("gdm_slave_child_handler: %d died of %d",
		       (int)pid, (int)WTERMSIG (status));

	if (pid == d->greetpid && greet) {
		if (WIFEXITED (status) &&
		    WEXITSTATUS (status) == DISPLAY_RESTARTGREETER) {
			gdm_slave_desensitize_config ();

			greet = FALSE;
			d->greetpid = 0;
			whack_greeter_fds ();
			gdm_slave_send_num (GDM_SOP_GREETPID, 0);

			if (restart_greeter_now) {
				do_restart_greeter = FALSE;
				restart_the_greeter ();
			} else {
				interrupted = TRUE;
				do_restart_greeter = TRUE;
			}
			gdm_in_signal--;
			return;
		}

		whack_greeter_fds ();

		/* if greet is TRUE, then the greeter died outside of our
		 * control really, so clean up and die, something is wrong
		 * The greeter is only allowed to pass back these
		 * exit codes, else we'll just remanage */
		if (WIFEXITED (status) &&
		    (WEXITSTATUS (status) == DISPLAY_ABORT ||
		     WEXITSTATUS (status) == DISPLAY_REBOOT ||
		     WEXITSTATUS (status) == DISPLAY_HALT ||
		     WEXITSTATUS (status) == DISPLAY_SUSPEND ||
		     WEXITSTATUS (status) == DISPLAY_RESTARTGDM)) {
			gdm_slave_quick_exit (WEXITSTATUS (status));
		} else {
			gdm_slave_quick_exit (DISPLAY_REMANAGE);
		}
	} else if (pid != 0 && pid == d->sesspid) {
		d->sesspid = 0;
	} else if (pid != 0 && pid == d->chooserpid) {
		d->chooserpid = 0;
	} else if (pid != 0 && pid == d->servpid) {
		d->servstat = SERVER_DEAD;
		d->servpid = 0;
		gdm_server_wipe_cookies (d);
		gdm_slave_whack_temp_auth_file ();

		/* if not handled there is no need for further formalities,
		 * we just have to die */
		if ( ! d->handled)
			gdm_slave_quick_exit (DISPLAY_REMANAGE);

		gdm_slave_send_num (GDM_SOP_XPID, 0);

		/* whack the session good */
		if (d->sesspid > 1) {
			gdm_slave_send_num (GDM_SOP_SESSPID, 0);
			kill (- (d->sesspid), SIGTERM);
		}
		if (d->greetpid > 1) {
			gdm_slave_send_num (GDM_SOP_GREETPID, 0);
			kill (d->greetpid, SIGTERM);
		}
		if (d->chooserpid > 1) {
			gdm_slave_send_num (GDM_SOP_CHOOSERPID, 0);
			kill (d->chooserpid, SIGTERM);
		}
	} else if (pid == extra_process) {
		/* an extra process died, yay! */
		extra_process = 0;
	    	extra_status = status;
	}
    }
    gdm_in_signal--;
}

static void
gdm_slave_usr2_handler (int sig)
{
	char buf[2048];
	size_t count;
	char **vec;
	int i;

	gdm_in_signal++;

	gdm_debug ("gdm_slave_usr2_handler: %s got USR2 signal", d->name);

	count = read (d->slave_notify_fd, buf, sizeof (buf) -1);
	if (count <= 0) {
		gdm_in_signal--;
		return;
	}

	buf[count] = '\0';

	vec = g_strsplit (buf, "\n", -1);
	if (vec == NULL) {
		gdm_in_signal--;
		return;
	}

	for (i = 0; vec[i] != NULL; i++) {
		char *s = vec[i];
		if (s[0] == GDM_SLAVE_NOTIFY_ACK) {
			gdm_got_ack = TRUE;
		} else if (s[0] == GDM_SLAVE_NOTIFY_KEY) {
			if (check_notifies_immediately > 0) {
				gdm_slave_handle_notify (&s[1]);
			} else {
				unhandled_notifies =
					g_list_append (unhandled_notifies,
						       g_strdup (&s[1]));
			}
		} else if (s[0] == GDM_SLAVE_NOTIFY_COMMAND) {
			if (strcmp (&s[1], GDM_NOTIFY_DIRTY_SERVERS) == 0) {
				/* never restart flexi servers
				 * they whack themselves */
				if (d->type != TYPE_FLEXI_XNEST &&
				    d->type != TYPE_FLEXI)
					remanage_asap = TRUE;
			} else if (strcmp (&s[1], GDM_NOTIFY_SOFT_RESTART_SERVERS) == 0) {
				/* never restart flexi servers,
				 * they whack themselves */
				/* FIXME: here we should handle actual
				 * restarts of flexi servers, but it probably
				 * doesn't matter */
				if (d->type != TYPE_FLEXI_XNEST &&
				    d->type != TYPE_FLEXI) {
					if ( ! d->logged_in) {
						gdm_slave_quick_exit (DISPLAY_REMANAGE);
					} else {
						remanage_asap = TRUE;
					}
				}
			}
		}
	}

	g_strfreev (vec);

	gdm_in_signal--;
}

/* Minor X faults */
static gint
gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt)
{
    gdm_debug ("gdm_slave_xerror_handler: X error - display doesn't respond");
    return (0);
}

/* We respond to fatal errors by restarting the display */
static gint
gdm_slave_xioerror_handler (Display *disp)
{
	/* Display is all gone */
	d->dsp = NULL;

	gdm_debug ("gdm_slave_xioerror_handler: I/O error for display %s", d->name);

	gdm_slave_session_stop (d->logged_in && login != NULL);

	gdm_error (_("gdm_slave_xioerror_handler: Fatal X error - Restarting %s"), d->name);

	if ((d->type == TYPE_LOCAL ||
	     d->type == TYPE_FLEXI) &&
	    (do_xfailed_on_xio_error ||
	     d->starttime + 5 >= time (NULL))) {
		gdm_slave_quick_exit (DISPLAY_XFAILED);
	} else {
		gdm_slave_quick_exit (DISPLAY_REMANAGE);
	}

	return 0;
}

static void
check_for_interruption (const char *msg)
{
	/* Hell yeah we were interrupted, the greeter died */
	if (msg == NULL) {
		interrupted = TRUE;
		return;
	}

	if (msg[0] == BEL) {
		/* Different interruptions come here */
		/* Note that we don't want to actually do anything.  We want
		 * to just set some flag and go on and schedule it after we
		 * dump out of the login in the main login checking loop */
		switch (msg[1]) {
		case GDM_INTERRUPT_TIMED_LOGIN:
			/* only allow timed login if display is local,
			 * it is allowed for this display (it's only allowed
			 * for the first local display) and if it's set up
			 * correctly */
			if ((d->console || GdmAllowRemoteAutoLogin) 
                            && d->timed_login_ok &&
			    ! ve_string_empty (ParsedTimedLogin) &&
                            strcmp (ParsedTimedLogin, "root") != 0 &&
			    GdmTimedLoginDelay > 0) {
				do_timed_login = TRUE;
			}
			break;
		case GDM_INTERRUPT_CONFIGURE:
			if (d->console &&
			    GdmConfigAvailable &&
			    GdmSystemMenu &&
			    ! ve_string_empty (GdmConfigurator)) {
				do_configurator = TRUE;
			}
			break;
		default:
			break;
		}

		/* this was an interruption, if it wasn't
		 * handled then the user will just get an error as if he
		 * entered an invalid login or passward.  Seriously BEL
		 * cannot be part of a login/password really */
		interrupted = TRUE;
	}
}


char * 
gdm_slave_greeter_ctl (char cmd, const char *str)
{
    char *buf = NULL;
    int c;

    /* There is no spoon^H^H^H^H^Hgreeter */
    if ( ! greet)
	    return NULL;

    check_notifies_now ();
    check_notifies_immediately ++;

    if ( ! ve_string_empty (str)) {
	    gdm_fdprintf (greeter_fd_out, "%c%c%s\n", STX, cmd, str);
    } else {
	    gdm_fdprintf (greeter_fd_out, "%c%c\n", STX, cmd);
    }

    /* Skip random junk that might have accumulated */
    do {
	    c = gdm_fdgetc (greeter_fd_in);
    } while (c != EOF && c != STX);
    
    if (c == EOF ||
	(buf = gdm_fdgets (greeter_fd_in)) == NULL) {
	    check_notifies_immediately --;
	    interrupted = TRUE;
	    /* things don't seem well with the greeter, it probably died */
	    return NULL;
    }

    check_notifies_immediately --;

    check_for_interruption (buf);

    if ( ! ve_string_empty (buf)) {
	    return buf;
    } else {
	    g_free (buf);
	    return NULL;
    }
}

void
gdm_slave_greeter_ctl_no_ret (char cmd, const char *str)
{
	g_free (gdm_slave_greeter_ctl (cmd, str));
}

static void 
gdm_slave_quick_exit (gint status)
{
    /* just for paranoia's sake */
    seteuid (0);
    setegid (0);

    if (d != NULL) {
	    /* Well now we're just going to kill
	     * everything including the X server,
	     * so no need doing XCloseDisplay which
	     * may just get us an XIOError */
	    d->dsp = NULL;

	    /* No need to send the PIDS to the daemon
	     * since we'll just exit cleanly */

	    /* Push and never pop */
	    gdm_sigchld_block_push ();

	    /* Kill children where applicable */
	    if (d->greetpid > 1)
		    kill (d->greetpid, SIGTERM);
	    d->greetpid = 0;

	    if (d->chooserpid > 1)
		    kill (d->chooserpid, SIGTERM);
	    d->chooserpid = 0;

	    if (d->sesspid > 1)
		    kill (-(d->sesspid), SIGTERM);
	    d->sesspid = 0;

	    gdm_server_stop (d);
	    gdm_verify_cleanup (d);

	    if (d->servpid > 1)
		    kill (d->servpid, SIGTERM);
	    d->servpid = 0;

	    if (extra_process > 1)
		    kill (-(extra_process), SIGTERM);
	    extra_process = 0;
    }

    _exit (status);
}

static void 
gdm_slave_exit (gint status, const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    gdm_error ("%s", s);
    
    g_free (s);

    gdm_slave_quick_exit (status);
}

static void 
gdm_child_exit (gint status, const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    
    g_free (s);

    _exit (status);
}

void
gdm_slave_whack_temp_auth_file (void)
{
	uid_t old;

	old = geteuid ();
	if (old != 0)
		seteuid (0);
	if (d->xnest_temp_auth_file != NULL)
		unlink (d->xnest_temp_auth_file);
	g_free (d->xnest_temp_auth_file);
	d->xnest_temp_auth_file = NULL;
	if (old != 0)
		seteuid (old);
}

static void
create_temp_auth_file (void)
{
	if (d->type == TYPE_FLEXI_XNEST &&
	    d->xnest_auth_file != NULL) {
		if (d->xnest_temp_auth_file != NULL)
			unlink (d->xnest_temp_auth_file);
		g_free (d->xnest_temp_auth_file);
		d->xnest_temp_auth_file =
			copy_auth_file (d->server_uid,
					GdmUserId,
					d->xnest_auth_file);
	}
}

static void
set_xnest_parent_stuff (void)
{
	if (d->type == TYPE_FLEXI_XNEST) {
		gnome_setenv ("GDM_PARENT_DISPLAY", d->xnest_disp, TRUE);
		if (d->xnest_temp_auth_file != NULL) {
			gnome_setenv ("GDM_PARENT_XAUTHORITY",
				      d->xnest_temp_auth_file, TRUE);
			g_free (d->xnest_temp_auth_file);
			d->xnest_temp_auth_file = NULL;
		}
	}
}

static gint
gdm_slave_exec_script (GdmDisplay *d, const gchar *dir, const char *login,
		       struct passwd *pwent, gboolean pass_stdout,
		       gboolean set_parent)
{
    pid_t pid;
    char *script;
    gchar **argv;
    gint status;
    char *x_servers_file;

    if (!d || ve_string_empty (dir))
	return EXIT_SUCCESS;

    script = g_strconcat (dir, "/", d->name, NULL);
    if (access (script, R_OK|X_OK) != 0) {
	    g_free (script);
	    script = NULL;
    }
    if (script == NULL &&
	! ve_string_empty (d->hostname)) {
	    script = g_strconcat (dir, "/", d->hostname, NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL &&
	d->type == TYPE_XDMCP) {
	    script = g_strconcat (dir, "/XDMCP", NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL &&
	SERVER_IS_FLEXI (d)) {
	    script = g_strconcat (dir, "/Flexi", NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL) {
	    script = g_strconcat (dir, "/Default", NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    
    if (script == NULL) {
	    return EXIT_SUCCESS;
    }

    if (set_parent)
	    create_temp_auth_file ();

    pid = gdm_fork_extra ();

    switch (pid) {
	    
    case 0:
        closelog ();

	close (0);
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

	if ( ! pass_stdout) {
		close (1);
		close (2);
		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
	}

	gdm_close_all_descriptors (3 /* from */, -1 /* except */);

	openlog ("gdm", LOG_PID, LOG_DAEMON);

        if (login != NULL) {
	        gnome_setenv ("LOGNAME", login, TRUE);
	        gnome_setenv ("USER", login, TRUE);
	        gnome_setenv ("USERNAME", login, TRUE);
        } else {
	        gnome_setenv ("LOGNAME", GdmUser, TRUE);
	        gnome_setenv ("USER", GdmUser, TRUE);
	        gnome_setenv ("USERNAME", GdmUser, TRUE);
        }
        if (pwent != NULL) {
		if (ve_string_empty (pwent->pw_dir))
			gnome_setenv ("HOME", "/", TRUE);
		else
			gnome_setenv ("HOME", pwent->pw_dir, TRUE);
	        gnome_setenv ("SHELL", pwent->pw_shell, TRUE);
        } else {
	        gnome_setenv ("HOME", "/", TRUE);
	        gnome_setenv ("SHELL", "/bin/sh", TRUE);
        }

	if (set_parent)
		set_xnest_parent_stuff ();

	/* some env for use with the Pre and Post scripts */
	x_servers_file = g_strconcat (GdmServAuthDir,
				      "/", d->name, ".Xservers", NULL);
	gnome_setenv ("X_SERVERS", x_servers_file, TRUE);
	g_free (x_servers_file);
	if (d->type == TYPE_XDMCP)
		gnome_setenv ("REMOTE_HOST", d->hostname, TRUE);

	/* Runs as root, uses server authfile */
	gnome_setenv ("XAUTHORITY", d->authfile, TRUE);
        gnome_setenv ("DISPLAY", d->name, TRUE);
	gnome_setenv ("PATH", GdmRootPath, TRUE);
	gnome_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
	gnome_unsetenv ("MAIL");
	argv = ve_split (script);
	execv (argv[0], argv);
	syslog (LOG_ERR, _("gdm_slave_exec_script: Failed starting: %s"),
		script);
	_exit (EXIT_SUCCESS);
	    
    case -1:
	if (set_parent)
		gdm_slave_whack_temp_auth_file ();
	g_free (script);
	syslog (LOG_ERR, _("gdm_slave_exec_script: Can't fork script process!"));
	return EXIT_SUCCESS;
	
    default:
	gdm_wait_for_extra (&status);

	if (set_parent)
		gdm_slave_whack_temp_auth_file ();

	g_free (script);

	if (WIFEXITED (status))
	    return WEXITSTATUS (status);
	else
	    return EXIT_SUCCESS;
    }
}

gboolean
gdm_slave_greeter_check_interruption (void)
{
	if (interrupted) {
		/* no longer interrupted */
		interrupted = FALSE;
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
gdm_slave_should_complain (void)
{
	if (do_timed_login ||
	    do_configurator ||
	    do_restart_greeter)
		return FALSE;
	return TRUE;
}

/* The user name for automatic/timed login may be parameterized by 
   host/display. */

static gchar *
gdm_parse_enriched_login (const gchar *s, GdmDisplay *display)
{
    gchar cmd, *buffer, in_buffer[20];
    GString *str;
    gint pipe1[2], in_buffer_len;  
    gchar **argv;
    pid_t pid;

    if (s == NULL)
	return(NULL);

    str = g_string_new (NULL);

    while (s[0] != '\0') {

	if (s[0] == '%' && s[1] != 0) {
		cmd = s[1];
		s++;

		switch (cmd) {

		case 'h': 
			g_string_append (str, display->hostname);
			break;

		case 'd': 
			g_string_append (str, display->name);
			break;

		case '%':
			g_string_append_c (str, '%');
			break;

		default:
			break;
		};
	} else {
		g_string_append_c (str, *s);
	}
	s++;
    }

    /* Sometimes it is not convenient to use the display or hostname as
       user name. A script may be used to generate the automatic/timed
       login name based on the display/host by ending the name with the
       pipe symbol '|'. */

    if(str->len > 0 && str->str[str->len - 1] == '|') {
      g_string_truncate(str, str->len - 1);
      if (pipe (pipe1) < 0) {
        gdm_error (_("gdm_parse_enriched_login: Failed creating pipe"));
      } else {
	pid = gdm_fork_extra ();

        switch (pid) {
	    
        case 0:
	    /* The child will write the username to stdout based on the DISPLAY
	       environment variable. */

            close (pipe1[0]);
            if(pipe1[1] != STDOUT_FILENO) 
	      dup2 (pipe1[1], STDOUT_FILENO);

	    closelog ();

	    gdm_close_all_descriptors (3 /* from */, pipe1[1] /* except */);

	    openlog ("gdm", LOG_PID, LOG_DAEMON);

	    /* runs as root */
	    gnome_setenv ("XAUTHORITY", display->authfile, TRUE);
	    gnome_setenv ("DISPLAY", display->name, TRUE);
	    gnome_setenv ("PATH", GdmRootPath, TRUE);
	    gnome_unsetenv ("MAIL");

	    argv = ve_split (str->str);
	    execv (argv[0], argv);
	    gdm_error (_("gdm_parse_enriched_login: Failed executing: %s"),
		    str->str);
	    _exit (EXIT_SUCCESS);
	    
        case -1:
	    gdm_error (_("gdm_parse_enriched_login: Can't fork script process!"));
            close (pipe1[0]);
            close (pipe1[1]);
	    break;
	
        default:
	    /* The parent reads username from the pipe a chunk at a time */
            close(pipe1[1]);
            g_string_truncate(str,0);
            while((in_buffer_len = read(pipe1[0],in_buffer,
				        sizeof(in_buffer)/sizeof(char)- 1)) > 0) {
	      in_buffer[in_buffer_len] = '\000';
              g_string_append(str,in_buffer);
            }

            if(str->len > 0 && str->str[str->len - 1] == '\n')
              g_string_truncate(str, str->len - 1);

            close(pipe1[0]);

	    gdm_wait_for_extra (NULL);
        }
      }
    }

    buffer = str->str;
    g_string_free (str, FALSE);

    return buffer;
}

static void
gdm_slave_handle_notify (const char *msg)
{
	int val;

	gdm_debug ("Handling slave notify: '%s'", msg);

	if (sscanf (msg, GDM_NOTIFY_ALLOWROOT " %d", &val) == 1) {
		GdmAllowRoot = val;
	} else if (sscanf (msg, GDM_NOTIFY_ALLOWREMOTEROOT " %d", &val) == 1) {
		GdmAllowRemoteRoot = val;
	} else if (sscanf (msg, GDM_NOTIFY_ALLOWREMOTEAUTOLOGIN " %d", &val) == 1) {
		GdmAllowRemoteAutoLogin = val;
	} else if (sscanf (msg, GDM_NOTIFY_SYSMENU " %d", &val) == 1) {
		GdmSystemMenu = val;
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_CONFIG_AVAILABLE " %d", &val) == 1) {
		GdmConfigAvailable = val;
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_RETRYDELAY " %d", &val) == 1) {
		GdmRetryDelay = val;
	} else if (strncmp (msg, GDM_NOTIFY_GREETER " ",
			    strlen (GDM_NOTIFY_GREETER) + 1) == 0) {
		g_free (GdmGreeter);
		GdmGreeter = g_strdup (&msg[strlen (GDM_NOTIFY_GREETER) + 1]);

		if (d->console) {
			if (restart_greeter_now) {
				restart_the_greeter ();
			} else if (d->type == TYPE_LOCAL) {
				/* FIXME: can't handle flexi servers like this
				 * without going all cranky */
				if ( ! d->logged_in) {
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				} else {
					remanage_asap = TRUE;
				}
			}
		}
	} else if (strncmp (msg, GDM_NOTIFY_REMOTEGREETER " ",
			    strlen (GDM_NOTIFY_REMOTEGREETER) + 1) == 0) {
		g_free (GdmRemoteGreeter);
		GdmRemoteGreeter = g_strdup
			(&msg[strlen (GDM_NOTIFY_REMOTEGREETER) + 1]);
		if ( ! d->console) {
			if (restart_greeter_now) {
				restart_the_greeter ();
			} else if (d->type == TYPE_XDMCP) {
				/* FIXME: can't handle flexi servers like this
				 * without going all cranky */
				if ( ! d->logged_in) {
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				} else {
					remanage_asap = TRUE;
				}
			}
		}
	} else if (strncmp (msg, GDM_NOTIFY_TIMED_LOGIN " ",
			    strlen (GDM_NOTIFY_TIMED_LOGIN) + 1) == 0) {
		/* FIXME: this is fairly nasty, we should handle this nicer */
		/* FIXME: can't handle flexi servers without going all cranky */
		if (d->type == TYPE_LOCAL || d->type == TYPE_XDMCP) {
			if ( ! d->logged_in) {
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			} else {
				remanage_asap = TRUE;
			}
		}
	} else if (sscanf (msg, GDM_NOTIFY_TIMED_LOGIN_DELAY " %d", &val) == 1) {
		GdmTimedLoginDelay = val;
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	}
}

/* do cleanup but only if we are a slave, if we're not a slave, just
 * return FALSE */
gboolean
gdm_slave_final_cleanup (void)
{
	if (getpid () != d->slavepid)
		return FALSE;
	gdm_debug ("slave killing self");
	gdm_slave_term_handler (SIGTERM);
	return TRUE;
}

/* EOF */
