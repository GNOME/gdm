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
#include <gnome.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <stdlib.h>
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
static sigset_t mask;
static gboolean greet = FALSE;
static gboolean do_timed_login = FALSE; /* if this is true,
					   login the timed login */
static gboolean do_configurator = FALSE; /* if this is true, login as root
					  * and start the configurator */
static gchar *ParsedAutomaticLogin = NULL;
static gchar *ParsedTimedLogin = NULL;

extern gboolean gdm_first_login;
extern gboolean gdm_emergency_server;
extern pid_t extra_process;
extern int extra_status;

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
extern sigset_t sysmask;
extern gchar *GdmGlobalFaceDir;
extern gboolean GdmBrowser;
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
static void     gdm_slave_session_cleanup (void);
static void     gdm_slave_alrm_handler (int sig);
static void     gdm_slave_term_handler (int sig);
static void     gdm_slave_child_handler (int sig);
static void     gdm_slave_usr2_handler (int sig);
static void     gdm_slave_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static void     gdm_child_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static gint     gdm_slave_exec_script (GdmDisplay *d, const gchar *dir,
				       const char *login, struct passwd *pwent);
static gchar *  gdm_parse_enriched_login (const gchar *s, GdmDisplay *display);


/* Yay thread unsafety */
static gboolean x_error_occured = FALSE;
static gboolean gdm_got_usr2 = FALSE;

/* ignore handlers */
static int
ignore_xerror_handler (Display *disp, XErrorEvent *evt)
{
	x_error_occured = TRUE;
	return 0;
}

void 
gdm_slave_start (GdmDisplay *display)
{  
	time_t first_time;
	int death_count;
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
			gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_init: Error setting up ALRM signal handler"));
	}

	/* Handle a INT/TERM signals from gdm master */
	term.sa_handler = gdm_slave_term_handler;
	term.sa_flags = SA_RESTART;
	sigemptyset (&term.sa_mask);
	sigaddset (&term.sa_mask, SIGTERM);
	sigaddset (&term.sa_mask, SIGINT);

	if ((sigaction (SIGTERM, &term, NULL) < 0) ||
	    (sigaction (SIGINT, &term, NULL) < 0))
		gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_init: Error setting up TERM/INT signal handler"));

	/* Child handler. Keeps an eye on greeter/session */
	child.sa_handler = gdm_slave_child_handler;
	child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigemptyset (&child.sa_mask);
	sigaddset (&child.sa_mask, SIGCHLD);

	if (sigaction (SIGCHLD, &child, NULL) < 0) 
		gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_init: Error setting up CHLD signal handler"));

	/* Handle a USR2 which is ack from master that it received a message */
	usr2.sa_handler = gdm_slave_usr2_handler;
	usr2.sa_flags = SA_RESTART;
	sigemptyset (&usr2.sa_mask);
	sigaddset (&usr2.sa_mask, SIGUSR2);

	if (sigaction (SIGUSR2, &usr2, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up USR2 signal handler"), "gdm_slave_init");

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

	if ( ! gdm_verify_setup_user (display, login, display->name))
		return FALSE;

	/* Run the init script. gdmslave suspends until script
	 * has terminated */
	gdm_slave_exec_script (display, GdmDisplayInit, NULL, NULL);

	gdm_debug ("setup_automatic_session: DisplayInit script finished");

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

		XFree (xscreens);
	} else
#endif
	{
		display->screenx = 0;
		display->screeny = 0;
		display->screenwidth = 0; /* we'll use the gdk size */
		display->screenheight = 0;
	}
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
		    _exit (DISPLAY_ABORT);
	    }
	    gdm_slave_send_num (GDM_SOP_XPID, d->servpid);
    }
    
    ve_setenv ("XAUTHORITY", d->authfile, TRUE);
    ve_setenv ("DISPLAY", d->name, TRUE);

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
    
    while (openretries < maxtries &&
	   d->dsp == NULL) {
	d->dsp = XOpenDisplay (d->name);
	
	if (d->dsp == NULL) {
	    gdm_debug ("gdm_slave_run: Sleeping %d on a retry", 1+openretries*2);
	    sleep (1+openretries*2);
	    openretries++;
	}
    }

    if (SERVER_IS_LOCAL (d)) {
	    gdm_slave_send (GDM_SOP_START_NEXT_LOCAL, FALSE);
    }

    /* something may have gone wrong, try xfailed, if local (non-flexi),
     * the toplevel loop of death will handle us */ 
    if (d->dsp == NULL) {
	    gdm_server_stop (d);
	    if (d->type == TYPE_LOCAL)
		    _exit (DISPLAY_XFAILED);
	    else
		    _exit (DISPLAY_ABORT);
    }

    /* Some sort of a bug foo to make some servers work or whatnot,
     * stolem from xdm sourcecode, perhaps not necessary, but can't hurt */
    {
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
    gdm_screen_init (d);

    /* check log stuff for the server, this is done here
     * because it's really a race */
    if (SERVER_IS_LOCAL (d))
	    gdm_server_checklog (d);

    if (d->use_chooser) {
	    /* this usually doesn't return */
	    gdm_slave_chooser ();  /* Run the chooser */
    } else if ((d->console || GdmAllowRemoteAutoLogin) &&
	       gdm_first_login &&
	       ! ve_string_empty (ParsedAutomaticLogin) &&
	       strcmp (ParsedAutomaticLogin, "root") != 0) {
	    gdm_first_login = FALSE;

	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);
	    gdm_slave_send_string (GDM_SOP_LOGIN, ParsedAutomaticLogin);

	    if (setup_automatic_session (d, ParsedAutomaticLogin)) {
		    gdm_slave_session_start ();
	    }

	    gdm_slave_send_num (GDM_SOP_LOGGED_IN, FALSE);
	    gdm_slave_send_string (GDM_SOP_LOGIN, "");

	    gdm_debug ("gdm_slave_run: Automatic login done");
    } else {
	    if (gdm_first_login)
		    gdm_first_login = FALSE;
	    gdm_slave_greeter ();  /* Start the greeter */

	    do {
		    gdm_slave_wait_for_login (); /* wait for a password */

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
		    gdm_slave_send_string (GDM_SOP_LOGIN, "");

		    if (greet) {
			    gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");
			    gdm_slave_greeter_ctl_no_ret (GDM_RESETOK, "");
		    }
	    } while (greet);
    }
}

static void
gdm_slave_whack_greeter (void)
{
	gdm_sigchld_block_push ();

	greet = FALSE;

	/* do what you do when you quit, this will hang until the
	 * greeter decides to print an STX\n and die, meaning it can do some
	 * last minute cleanup */
	gdm_slave_greeter_ctl_no_ret (GDM_QUIT, "");

	/* Wait for the greeter to really die, the check is just
	 * being very anal, the pid is always set to something */
	if (d->greetpid > 0)
		waitpid (d->greetpid, 0, 0); 
	d->greetpid = 0;

	gdm_slave_send_num (GDM_SOP_GREETPID, 0);

	gdm_sigchld_block_pop ();
}

/* A hack really, this will wait around until the first mapped window
 * with this class and focus it */
static void
focus_first_x_window (const char *class_res_name)
{
	pid_t pid;
	Display *disp;
	XWindowAttributes attribs = { 0, };
	int i;

	pid = fork ();
	if (pid < 0) {
		gdm_error (_("focus_first_x_window: cannot fork"));
		return;
	}
	/* parent */
	if (pid > 0) {
		return;
	}

	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

	disp = XOpenDisplay (d->name);
	if (disp == NULL) {
		gdm_error (_("focus_first_x_window: cannot open display %s"),
			   d->name);
		_exit (0);
	}

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

	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->sesspid = fork ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	if (pid < 0) {
		/* can't fork, damnit */
		display->sesspid = 0;
		return;
	}
	if (pid == 0) {
		int i;
		char **argv;
		/* child */

		setuid (0);
		setgid (0);

		/* setup environment */
		gdm_clearenv_no_lang ();

		/* root here */
		ve_setenv ("XAUTHORITY", display->authfile, TRUE);
		ve_setenv ("DISPLAY", display->name, TRUE);
		ve_setenv ("LOGNAME", "root", TRUE);
		ve_setenv ("USER", "root", TRUE);
		ve_setenv ("USERNAME", "root", TRUE);
		ve_setenv ("HOME", pwent->pw_dir, TRUE);
		ve_setenv ("SHELL", pwent->pw_shell, TRUE);
		ve_setenv ("PATH", GdmRootPath, TRUE);
		ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		/* exec the configurator */
		argv = ve_split (GdmConfigurator);
		if (argv != NULL &&
		    argv[0] != NULL &&
		    access (argv[0], X_OK) == 0)
			execv (argv[0], argv);

		gdm_error_box (d,
			       GNOME_MESSAGE_BOX_ERROR,
			       _("Could not execute the configuration\n"
				 "program.  Make sure it's path is set\n"
				 "correctly in the configuration file.\n"
				 "I will attempt to start it from the\n"
				 "default location."));

		argv = ve_split
			(EXPANDED_GDMCONFIGDIR
			 "/gdmconfig --disable-sound --disable-crash-dialog");
		if (access (argv[0], X_OK) == 0)
			execv (argv[0], argv);

		gdm_error_box (d,
			       GNOME_MESSAGE_BOX_ERROR,
			       _("Could not execute the configuration\n"
				 "program.  Make sure it's path is set\n"
				 "correctly in the configuration file."));

		_exit (0);
	} else {
		gdm_sigchld_block_push ();
		/* wait for the config proggie to die */
		if (d->sesspid > 0)
			waitpid (d->sesspid, 0, 0);
		display->sesspid = 0;
		gdm_sigchld_block_pop ();
	}
}




static void
gdm_slave_wait_for_login (void)
{
	g_free (login);
	login = NULL;

	/* init to a sane value */
	do_timed_login = FALSE;
	do_configurator = FALSE;

	/* Chat with greeter */
	while (login == NULL) {
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
		 * and do_timed_login after any call to gdm_verify_user */

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

			gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);
			/* Note: nobody really logged in */
			gdm_slave_send_string (GDM_SOP_LOGIN, "");

			/* disable the login screen, we don't want people to
			 * log in in the meantime */
			gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");

			run_config (d, pwent);
			/* note that we may never get here as the configurator
			 * may have sighupped the main gdm server and with it
			 * wiped us */

			gdm_verify_cleanup (d);

			gdm_slave_send_num (GDM_SOP_LOGGED_IN, FALSE);

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
static gboolean
is_in_trusted_pic_dir (const char *path)
{
	char *globalpix;

	/* our own pixmap dir is trusted */
	if (strncmp (path, EXPANDED_PIXMAPDIR, sizeof (EXPANDED_PIXMAPDIR)) == 0)
		return TRUE;

	/* gnome'skpixmap dir is trusted */
	globalpix = gnome_unconditional_pixmap_file ("");
	if (strncmp (path, globalpix, strlen (globalpix)) == 0) {
		g_free (globalpix);
		return TRUE;
	}
	g_free (globalpix);

	return FALSE;
}

/* This is VERY evil! */
static void
run_pictures (void)
{
	char *response;
	int tempfd;
	char tempname[256];
	char buf[1024];
	size_t bytes;
	struct passwd *pwent;
	char *picfile;
	FILE *fp;
	char *cfgdir;

	response = NULL;
	for (;;) {
		g_free (response);
		response = gdm_slave_greeter_ctl (GDM_NEEDPIC, "");
		if (ve_string_empty (response)) {
			g_free (response);
			return;
		}
		
		/* don't quit, we don't want to further confuse a confused
		 * greeter, just don't send it pics */
		if ( ! GdmBrowser) {
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		pwent = getpwnam (response);
		if (pwent == NULL) {
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		picfile = NULL;

		setegid (pwent->pw_gid);
		seteuid (pwent->pw_uid);

		/* Sanity check on ~user/.gnome/gdm */
		cfgdir = g_strconcat (pwent->pw_dir, "/.gnome/gdm", NULL);
		if (gdm_file_check ("run_pictures", pwent->pw_uid,
				    cfgdir, "gdm", TRUE, GdmUserMaxFile,
				    GdmRelaxPerms)) {
			char *cfgstr;

			cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/face/picture", NULL);
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
				dir = g_dirname (picfile);

				/* Note that strict permissions checking is done
				 * on this file.  Even if it may not even be owned by the
				 * user.  This setting should ONLY point to pics in trusted
				 * dirs. */
				if ( ! gdm_file_check ("run_pictures", pwent->pw_uid,
						       dir, g_basename (picfile), TRUE, GdmUserMaxFile,
						       GdmRelaxPerms)) {
					g_free (picfile);
					picfile = NULL;
				}

				g_free (dir);
			}
		}
		g_free (cfgdir);

		/* Nothing found yet */
		if (picfile == NULL) {
			picfile = g_strconcat (pwent->pw_dir, "/.gnome/photo", NULL);
			if (access (picfile, F_OK) != 0) {
				seteuid (0);
				setegid (GdmGroupId);

				g_free (picfile);
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
			g_free (picfile);

			/* Sanity check on ~user/.gnome/photo */
			picfile = g_strconcat (pwent->pw_dir, "/.gnome", NULL);
			if ( ! gdm_file_check ("run_pictures", pwent->pw_uid,
					       picfile, "photo", TRUE, GdmUserMaxFile,
					       GdmRelaxPerms)) {
				g_free (picfile);

				seteuid (0);
				setegid (GdmGroupId);

				gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
				continue;
			}
			g_free (picfile);
			picfile = g_strconcat (pwent->pw_dir, "/.gnome/photo", NULL);
		}

		fp = fopen (picfile, "r");
		g_free (picfile);
		if (fp == NULL) {
			seteuid (0);
			setegid (GdmGroupId);

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		strcpy (tempname, "/tmp/gdm-user-picture-XXXXXX");
		tempfd = mkstemp (tempname);

		if (tempfd < 0) {
			fclose (fp);

			seteuid (0);
			setegid (GdmGroupId);

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		fchmod (tempfd, 0644);

		while ((bytes = fread (buf, sizeof (char),
				       sizeof (buf), fp)) > 0) {
			write (tempfd, buf, bytes);
		}

		fclose (fp);
		close (tempfd);

		gdm_slave_greeter_ctl_no_ret (GDM_READPIC, tempname);

		unlink (tempname);

		seteuid (0);
		setegid (GdmGroupId);
	}
	g_free (response);
}

static void
gdm_slave_greeter (void)
{
    gint pipe1[2], pipe2[2];  
    gchar **argv;
    struct passwd *pwent;
    int i;
    pid_t pid;
    
    gdm_debug ("gdm_slave_greeter: Running greeter on %s", d->name);
    
    /* Run the init script. gdmslave suspends until script has terminated */
    gdm_slave_exec_script (d, GdmDisplayInit, NULL, NULL);

    /* Open a pipe for greeter communications */
    if (pipe (pipe1) < 0 || pipe (pipe2) < 0) 
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Can't init pipe to gdmgreeter"));
    
    /* Fork. Parent is gdmslave, child is greeter process. */
    gdm_sigchld_block_push ();
    gdm_sigterm_block_push ();
    greet = TRUE;
    pid = d->greetpid = fork ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();

    switch (pid) {
	
    case 0:
	sigfillset (&mask);
	sigdelset (&mask, SIGINT);
	sigdelset (&mask, SIGTERM);
	sigdelset (&mask, SIGHUP);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	/* Plumbing */
	close (pipe1[1]);
	close (pipe2[0]);
	
	if (pipe1[0] != STDIN_FILENO) 
	    dup2 (pipe1[0], STDIN_FILENO);
	
	if (pipe2[1] != STDOUT_FILENO) 
	    dup2 (pipe2[1], STDOUT_FILENO);

	for (i = 2; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	open ("/dev/null", O_RDWR); /* open stderr - fd 2 */
	
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
	
	gdm_clearenv_no_lang ();
	ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
	ve_setenv ("DISPLAY", d->name, TRUE);

	ve_setenv ("LOGNAME", GdmUser, TRUE);
	ve_setenv ("USER", GdmUser, TRUE);
	ve_setenv ("USERNAME", GdmUser, TRUE);
	ve_setenv ("GDM_GREETER_PROTOCOL_VERSION",
		   GDM_GREETER_PROTOCOL_VERSION, TRUE);
	ve_setenv ("GDM_VERSION", VERSION, TRUE);

	pwent = getpwnam (GdmUser);
	if (pwent != NULL) {
		/* Note that usually this doesn't exist */
		if (pwent->pw_dir != NULL &&
		    g_file_exists (pwent->pw_dir))
			ve_setenv ("HOME", pwent->pw_dir, TRUE);
		else
			ve_setenv ("HOME", "/", TRUE); /* Hack */
		ve_setenv ("SHELL", pwent->pw_shell, TRUE);
	} else {
		ve_setenv ("HOME", "/", TRUE); /* Hack */
		ve_setenv ("SHELL", "/bin/sh", TRUE);
	}
	ve_setenv ("PATH", GdmDefaultPath, TRUE);
	ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

	/* Note that this is just informative, the slave will not listen to
	 * the greeter even if it does something it shouldn't on a non-local
	 * display so it's not a security risk */
	if (d->console) {
		ve_setenv ("GDM_IS_LOCAL", "yes", TRUE);
	} else {
		ve_unsetenv ("GDM_IS_LOCAL");
	}

	/* this is again informal only, if the greeter does time out it will
	 * not actually login a user if it's not enabled for this display */
	if (d->timed_login_ok) {
		if(ParsedTimedLogin == NULL)
			ve_setenv ("GDM_TIMED_LOGIN_OK", " ", TRUE);
		else
			ve_setenv ("GDM_TIMED_LOGIN_OK", ParsedTimedLogin, TRUE);
	} else {
		ve_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	if (d->type == TYPE_FLEXI) {
		ve_setenv ("GDM_FLEXI_SERVER", "yes", TRUE);
	} else if (d->type == TYPE_FLEXI_XNEST) {
		ve_setenv ("GDM_FLEXI_SERVER", "Xnest", TRUE);
	} else {
		ve_unsetenv ("GDM_FLEXI_SERVER");
	}

	if(gdm_emergency_server) {
		gdm_error_box (d,
			       GNOME_MESSAGE_BOX_ERROR,
			       _("No servers were defined in the\n"
				 "configuration file and xdmcp was\n"
				 "disabled.  This can only be a\n"
				 "configuration error.  So I have started\n"
				 "a single server for you.  You should\n"
				 "log in and fix the configuration.\n"
				 "Note that automatic and timed logins\n"
				 "are disabled now."));
		ve_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	if (d->failsafe_xserver) {
		gdm_error_box (d,
			       GNOME_MESSAGE_BOX_ERROR,
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
		gdm_error_box (d, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
	}

	argv = ve_split (GdmGreeter);
	execv (argv[0], argv);

	gdm_error (_("gdm_slave_greeter: Cannot start greeter trying default: %s"),
		   EXPANDED_BINDIR
		   "/gdmlogin --disable-sound --disable-crash-dialog");

	ve_setenv ("GDM_WHACKED_GREETER_CONFIG", "true", TRUE);

	argv = ve_split (EXPANDED_BINDIR
			 "/gdmlogin --disable-sound --disable-crash-dialog");
	execv (argv[0], argv);

	gdm_error_box (d,
		       GNOME_MESSAGE_BOX_ERROR,
		       _("Cannot start the greeter program,\n"
			 "you will not be able to log in.\n"
			 "This display will be disabled.\n"
			 "Try logging in by other means and\n"
			 "editing the configuration file"));
	
	gdm_child_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Error starting greeter on display %s"), d->name);
	
    case -1:
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Can't fork gdmgreeter process"));
	
    default:
	close (pipe1[0]);
	close (pipe2[1]);

	fcntl(pipe1[1], F_SETFD, fcntl(pipe1[1], F_GETFD, 0) | FD_CLOEXEC);
	fcntl(pipe2[0], F_SETFD, fcntl(pipe2[0], F_GETFD, 0) | FD_CLOEXEC);

	/* flush our input before we change the piping */
	fflush (stdin);

	if (pipe1[1] != STDOUT_FILENO) 
	    dup2 (pipe1[1], STDOUT_FILENO);
	
	if (pipe2[0] != STDIN_FILENO) 
	    dup2 (pipe2[0], STDIN_FILENO);

	close (pipe1[1]);
	close (pipe2[0]);
	
	gdm_debug ("gdm_slave_greeter: Greeter on pid %d", (int)pid);

	gdm_slave_send_num (GDM_SOP_GREETPID, d->greetpid);

	run_pictures (); /* Append pictures to greeter if browsing is on */
	break;
    }
}

static gboolean
parent_exists (void)
{
	pid_t ppid = getppid ();

	if (ppid <= 0 ||
	    kill (ppid, 0) < 0)
		return FALSE;
	return TRUE;
}

void
gdm_slave_send (const char *str, gboolean wait_for_usr2)
{
	char *msg;
	int fd;
	char *fifopath;
	int i;

	gdm_debug ("Sending %s", str);

	if (wait_for_usr2)
		gdm_got_usr2 = FALSE;

	fifopath = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
	fd = open (fifopath, O_WRONLY);
	g_free (fifopath);

	/* eek */
	if (fd < 0) {
		gdm_error (_("%s: Can't open fifo!"), "gdm_slave_send");
		return;
	}

	msg = g_strdup_printf ("\n%s\n", str);

	write (fd, msg, strlen (msg));

	g_free (msg);

	close (fd);

	for (i = 0;
	     parent_exists () &&
	     wait_for_usr2 &&
	     ! gdm_got_usr2 &&
	     i < 10;
	     i++) {
		sleep (1);
	}
}

void
gdm_slave_send_num (const char *opcode, long num)
{
	char *msg;

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
	if (GdmDebug) {
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
	char *msg;
	int fd;
	char *fifopath;
	struct hostent *host;

	host = gethostbyname (hostname);

	if (host == NULL) {
		gdm_error ("Cannot get address of host '%s'", hostname);
		return;
	}

	gdm_debug ("Sending chosen host address (%s) %s",
		   hostname, inet_ntoa (*(struct in_addr *)host->h_addr_list[0]));

	fifopath = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);

	fd = open (fifopath, O_WRONLY);

	g_free (fifopath);

	/* eek */
	if (fd < 0) {
		gdm_error (_("%s: Can't open fifo!"), "send_chosen_host");
		return;
	}

	msg = g_strdup_printf ("\n%s %d %s\n", GDM_SOP_CHOSEN,
			       disp->indirect_id,
			       inet_ntoa (*((struct in_addr *)host->h_addr_list[0])));

	write (fd, msg, strlen (msg));

	g_free (msg);

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
	int i;
	pid_t pid;

	gdm_debug ("gdm_slave_chooser: Running chooser on %s", d->name);

	/* Open a pipe for chooser communications */
	if (pipe (p) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_chooser: Can't init pipe to gdmchooser"));

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (d, GdmDisplayInit, NULL, NULL);

	/* Fork. Parent is gdmslave, child is greeter process. */
	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->chooserpid = fork ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	switch (pid) {

	case 0:
		sigfillset (&mask);
		sigdelset (&mask, SIGINT);
		sigdelset (&mask, SIGTERM);
		sigdelset (&mask, SIGHUP);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Plumbing */
		close (p[0]);

		if (p[1] != STDOUT_FILENO) 
			dup2 (p[1], STDOUT_FILENO);

		close (0);
		for (i = 2; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

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

		gdm_clearenv_no_lang ();
		ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		ve_setenv ("DISPLAY", d->name, TRUE);

		ve_setenv ("LOGNAME", GdmUser, TRUE);
		ve_setenv ("USER", GdmUser, TRUE);
		ve_setenv ("USERNAME", GdmUser, TRUE);

		ve_setenv ("GDM_VERSION", VERSION, TRUE);

		pwent = getpwnam (GdmUser);
		if (pwent != NULL) {
			/* Note that usually this doesn't exist */
			if (g_file_exists (pwent->pw_dir))
				ve_setenv ("HOME", pwent->pw_dir, TRUE);
			else
				ve_setenv ("HOME", "/", TRUE); /* Hack */
			ve_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			ve_setenv ("HOME", "/", TRUE); /* Hack */
			ve_setenv ("SHELL", "/bin/sh", TRUE);
		}
		ve_setenv ("PATH", GdmDefaultPath, TRUE);
		ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		argv = ve_split (GdmChooser);
		execv (argv[0], argv);

		gdm_error_box (d,
			       GNOME_MESSAGE_BOX_ERROR,
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
			waitpid (d->chooserpid, 0, 0);
		d->chooserpid  = 0;
		gdm_sigchld_block_pop ();

		gdm_slave_send_num (GDM_SOP_CHOOSERPID, 0);

		bytes = read (p[0], buf, sizeof(buf)-1);
		if (bytes > 0) {
			close (p[0]);

			if (buf[bytes-1] == '\n')
				buf[bytes-1] ='\0';
			else
				buf[bytes] ='\0';
			send_chosen_host (d, buf);

			_exit (DISPLAY_CHOSEN);
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
	cfgdir = g_strconcat (pwent->pw_dir, "/.gnome", NULL);

	/* We cannot be absolutely strict about the session
	 * permissions, since by default they will be writable
	 * by group and there's nothing we can do about it.  So
	 * we relax the permission checking in this case */
	session_relax_perms = GdmRelaxPerms;
	if (session_relax_perms == 0)
		session_relax_perms = 1;

	/* Sanity check on ~user/.gnome/session */
	session_ok = gdm_file_check ("gdm_get_sessions", pwent->pw_uid,
				     cfgdir, "session",
				     TRUE, GdmSessionMaxFile,
				     session_relax_perms);
	/* Sanity check on ~user/.gnome/session-options */
	options_ok = gdm_file_check ("gdm_get_sessions", pwent->pw_uid,
				     cfgdir, "session-options",
				     TRUE, GdmUserMaxFile,
				     session_relax_perms);

	g_free (cfgdir);

	if (options_ok) {
		char *cfgstr;

		cfgstr = g_strconcat
			("=", pwent->pw_dir,
			 "/.gnome/session-options=/Options/CurrentSession",
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
					      "/.gnome/session", NULL);
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
		"/usr/bin/",
		"/usr/local/bin/",
		EXPANDED_BINDIR "/",
		NULL
	};

	path = gnome_is_program_in_path (name);
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

/* this is for the unforunate case when something went seriously wrong, the
 * sysadmin's a wanker or the user has an old language setting */
static char *
unaliaslang (const char *origlang)
{
	FILE *langlist;
	char curline[256];

	if (ve_string_empty (GdmLocaleFile))
		return g_strdup (origlang);

	langlist = fopen (GdmLocaleFile, "r");

	if (langlist == NULL)
		return g_strdup (origlang);

	while (fgets (curline, sizeof (curline), langlist)) {
		char *name;
		char *lang;

		if (curline[0] <= ' ' ||
		    curline[0] == '#')
			continue;

		name = strtok (curline, " \t\r\n");
		if (name == NULL)
			continue;

		lang = strtok (NULL, " \t\r\n");
		if (lang == NULL)
			continue;

		if (ve_strcasecmp_no_locale (name, origlang) == 0) {
			fclose (langlist);
			return g_strdup (lang);
		}
	}

	fclose (langlist);

	return g_strdup (origlang);

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
	int i;
	char *sesspath, *sessexec;
	gboolean need_config_sync = FALSE;
	const char *shell = NULL;

	ve_clearenv ();

	/* Prepare user session */
	ve_setenv ("XAUTHORITY", d->userauth, TRUE);
	ve_setenv ("DISPLAY", d->name, TRUE);
	ve_setenv ("LOGNAME", login, TRUE);
	ve_setenv ("USER", login, TRUE);
	ve_setenv ("USERNAME", login, TRUE);
	ve_setenv ("HOME", home_dir, TRUE);
	ve_setenv ("GDMSESSION", session, TRUE);
	ve_setenv ("SHELL", pwent->pw_shell, TRUE);
	ve_unsetenv ("MAIL");	/* Unset $MAIL for broken shells */

	if (gnome_session != NULL)
		ve_setenv ("GDM_GNOME_SESSION", gnome_session, TRUE);

	/* Special PATH for root */
	if (pwent->pw_uid == 0)
		ve_setenv ("PATH", GdmRootPath, TRUE);
	else
		ve_setenv ("PATH", GdmDefaultPath, TRUE);

	/* Eeeeek, this no lookie as a correct language code, let's
	 * try unaliasing it */
	if (strlen (language) < 3 ||
	    language[2] != '_') {
		language = unaliaslang (language);
	}

	/* Set locale */
	ve_setenv ("LANG", language, TRUE);
	ve_setenv ("GDM_LANG", language, TRUE);
    
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

	if (setuid (pwent->pw_uid) < 0) 
		gdm_child_exit (DISPLAY_REMANAGE,
				_("gdm_slave_session_start: Could not become %s. Aborting."), login);
	
	chdir (home_dir);

	/* anality, make sure nothing is in memory for gnome_config
	 * to write */
	gnome_config_drop_all ();
	
	if (usrcfgok && savesess) {
		gchar *cfgstr = g_strconcat ("=", home_dir,
					     "/.gnome/gdm=/session/last", NULL);
		gnome_config_set_string (cfgstr, save_session);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}
	
	if (usrcfgok && savelang) {
		gchar *cfgstr = g_strconcat ("=", home_dir,
					     "/.gnome/gdm=/session/lang", NULL);
		gnome_config_set_string (cfgstr, language);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}

	if (sessoptok &&
	    savegnomesess &&
	    gnome_session != NULL) {
		gchar *cfgstr = g_strconcat ("=", home_dir, "/.gnome/session-options=/Options/CurrentSession", NULL);
		gnome_config_set_string (cfgstr, gnome_session);
		need_config_sync = TRUE;
		g_free (cfgstr);
	}

	if (need_config_sync) {
		gnome_config_sync ();
		gnome_config_drop_all ();
	}

	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close (i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open ("/dev/null", O_RDWR); /* open stderr - fd 2 */
	
	/* Restore sigmask inherited from init */
	sigprocmask (SIG_SETMASK, &sysmask, NULL);

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
				(d, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not find the GNOME installation,\n"
				   "will try running the \"Failsafe xterm\"\n"
				   "session."));
		} else {
			gdm_error_box
				(d, GNOME_MESSAGE_BOX_INFO,
				 _("This is the Failsafe Gnome session.\n"
				   "You will be logged into the 'Default'\n"
				   "session of Gnome with no startup scripts\n"
				   "run.  This is only to fix problems in\n"
				   "your installation."));
		}
	}

	/* an if and not an else, we could have done a fall-through
	 * to here in the above code if we can't find gnome-session */
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0) {
		char *params = g_strdup_printf ("-geometry 80x24+%d+%d",
						d->screenx, d->screeny);
		sesspath = find_prog ("xterm",
				      params,
				      &sessexec);
		g_free (params);
		if (sesspath == NULL) {
			gdm_error_box (d, GNOME_MESSAGE_BOX_ERROR,
				       _("Cannot find \"xterm\" to start "
					 "a failsafe session."));
			/* nyah nyah nyah nyah nyah */
			_exit (0);
		} else {
			gdm_error_box
				(d, GNOME_MESSAGE_BOX_INFO,
				 _("This is the Failsafe xterm session.\n"
				   "You will be logged into a terminal\n"
				   "console so that you may fix your system\n"
				   "if you cannot log in any other way.\n"
				   "To exit the terminal emulator, type\n"
				   "'exit' and an enter into the window."));
			focus_first_x_window ("xterm");
		}
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
		gdm_error_box (d, GNOME_MESSAGE_BOX_ERROR,
			       _("The system administrator has\n"
				 "disabled your account."));
	} else if (access (sessexec != NULL ? sessexec : sesspath, X_OK) != 0) {
		gdm_error (_("gdm_slave_session_start: Could not find/run session `%s'"), sesspath);
		/* if we can't read and exec the session, then make a nice
		 * error dialog */
		gdm_error_box
			(d, GNOME_MESSAGE_BOX_ERROR,
			 _("Cannot start the session, most likely the\n"
			   "session does not exist.  Please select from\n"
			   "the list of available sessions in the login\n"
			   "dialog window."));
	} else {
		char *exec = g_strconcat ("exec ", sesspath, NULL);
		execl (shell, "-", "-c", exec, NULL);

		gdm_error (_("gdm_slave_session_start: Could not start session `%s'"), sesspath);
		gdm_error_box
			(d, GNOME_MESSAGE_BOX_ERROR,
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
	! g_file_test (pwent->pw_dir, G_FILE_TEST_ISDIR)) {
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
	    /* Check if ~user/.gnome exists. Create it otherwise. */
	    cfgdir = g_strconcat (home_dir, "/.gnome", NULL);

	    if (stat (cfgdir, &statbuf) == -1) {
		    mkdir (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
		    chmod (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	    }

	    /* Sanity check on ~user/.gnome/gdm */
	    usrcfgok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
				       cfgdir, "gdm", TRUE, GdmUserMaxFile,
				       GdmRelaxPerms);
	    /* Sanity check on ~user/.gnome/session-options */
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

	cfgstr = g_strconcat ("=", home_dir, "/.gnome/gdm=/session/last", NULL);
	usrsess = gnome_config_get_string (cfgstr);
	if (usrsess == NULL)
		usrsess = g_strdup ("");
	g_free (cfgstr);

	cfgstr = g_strconcat ("=", home_dir, "/.gnome/gdm=/session/lang", NULL);
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
	    const char *lang = g_getenv ("LANG");

	    g_free (language);

	    if (lang != NULL &&
		lang[0] != '\0') 
		    language = g_strdup (lang);
	    else
		    language = g_strdup (GdmDefaultLocale);
	    savelang = TRUE;

	    if (ve_string_empty (language)) {
		    g_free (language);
		    language = g_strdup ("C");
	    }
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

	    gdm_debug (_("gdm_slave_session_start: Authentication completed. Whacking greeter"));

	    gdm_slave_whack_greeter ();
    }

    /* Ensure some sanity in this world */
    gdm_ensure_sanity ();

    if (GdmKillInitClients)
	    gdm_server_whack_clients (d);

    /* setup some env for PreSession script */
    ve_setenv ("DISPLAY", d->name, TRUE);

    /* If script fails reset X server and restart greeter */
    if (gdm_slave_exec_script (d, GdmPreSession,
			       login, pwent) != EXIT_SUCCESS) 
	gdm_slave_exit (DISPLAY_REMANAGE,
			_("gdm_slave_session_start: Execution of PreSession script returned > 0. Aborting."));

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
	gdm_slave_session_stop (0);
	gdm_slave_session_cleanup ();
	
	gdm_server_stop (d);
	gdm_verify_cleanup (d);

	_exit (DISPLAY_REMANAGE);
    } 

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

	    d->sesspid = 0;

	    gdm_sigchld_block_pop ();

	    waitpid (pid, NULL, 0);
    } else {
	    gdm_sigchld_block_pop ();
    }

    gdm_debug ("gdm_slave_session_start: Session ended OK");

    gdm_slave_session_stop (pid);
    gdm_slave_session_cleanup ();
}


static void
gdm_slave_session_stop (pid_t sesspid)
{
    struct passwd *pwent;
    char *local_login;

    local_login = login;
    login = NULL;

    seteuid (0);
    setegid (0);

    gdm_slave_send_num (GDM_SOP_SESSPID, 0);

    gdm_debug ("gdm_slave_session_stop: %s on %s", local_login, d->name);

    if (sesspid > 0)
	    kill (- (sesspid), SIGTERM);

    gdm_verify_cleanup (d);
    
    pwent = getpwnam (local_login);	/* PAM overwrites our pwent */

    /* Execute post session script */
    gdm_debug ("gdm_slave_session_cleanup: Running post session script");
    gdm_slave_exec_script (d, GdmPostSession, local_login, pwent);

    if (pwent == NULL) {
	    return;
    }

    g_free (local_login);
    
    /* Remove display from ~user/.Xauthority */
    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    gdm_auth_user_remove (d, pwent->pw_uid);

    seteuid (0);
    setegid (0);
}

static void
gdm_slave_session_cleanup (void)
{
    gdm_debug ("gdm_slave_session_cleanup: on %s", d->name);

    /* kill login if it still exists */
    g_free (login);
    login = NULL;
    
    /* things are going to be killed, so ignore errors */
    XSetErrorHandler (ignore_xerror_handler);

    /* Cleanup */
    gdm_debug ("gdm_slave_session_cleanup: Severing connection");
    if (d->dsp != NULL) {
	    XCloseDisplay (d->dsp);
	    d->dsp = NULL;
    }
}

static void
gdm_slave_term_handler (int sig)
{
	sigset_t tmask;

	gdm_debug ("gdm_slave_term_handler: %s got TERM/INT signal", d->name);

	/* just for paranoia's sake */
	seteuid (0);
	setegid (0);

	sigemptyset (&tmask);
	sigaddset (&tmask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &tmask, NULL);  

	if (extra_process > 1) {
		/* we sigterm extra processes, and we
		 * don't wait */
		kill (extra_process, SIGTERM);
		extra_process = -1;
	}

	if (d->greetpid != 0) {
		pid_t pid = d->greetpid;
		d->greetpid = 0;
		greet = FALSE;
		gdm_debug ("gdm_slave_term_handler: Whacking greeter");
		if (kill (pid, sig) == 0)
			waitpid (pid, 0, 0); 
	} else if (login != NULL) {
		gdm_slave_session_stop (d->sesspid);
		gdm_slave_session_cleanup ();
	}

	if (d->chooserpid != 0) {
		pid_t pid = d->chooserpid;
		d->chooserpid = 0;
		gdm_debug ("gdm_slave_term_handler: Whacking chooser");
		if (kill (pid, sig) == 0)
			waitpid (pid, 0, 0); 
	}

	gdm_debug ("gdm_slave_term_handler: Whacking server");

	gdm_server_stop (d);
	gdm_verify_cleanup (d);
	_exit (DISPLAY_ABORT);
}

/* called on alarms to ping */
static void
gdm_slave_alrm_handler (int sig)
{
	static gboolean in_ping = FALSE;

	gdm_debug ("gdm_slave_alrm_handler: %s got ARLM signal, "
		   "to ping display", d->name);

	if (d->dsp == NULL) {
		/* huh? */
		return;
	}

	if (in_ping) {
		/* darn, the last ping didn't succeed, wipe this display */

		if (login != NULL) {
			gdm_slave_session_stop (d->sesspid);
			gdm_slave_session_cleanup ();
		}

		gdm_slave_exit (DISPLAY_REMANAGE, 
				_("Ping to %s failed, whacking display!"),
				d->name);
	}

	in_ping = TRUE;

	/* schedule next alarm */
	alarm (GdmPingInterval * 60);

	XSync (d->dsp, True);

	in_ping = FALSE;
}

/* Called on every SIGCHLD */
static void 
gdm_slave_child_handler (int sig)
{
    gint status;
    pid_t pid;

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
		/* just for paranoia's sake */
		seteuid (0);
		setegid (0);

		/* if greet is TRUE, then the greeter died outside of our
		 * control really, so clean up and die, something is wrong */
		gdm_server_stop (d);
		gdm_verify_cleanup (d);

		/* The greeter is only allowed to pass back these
		 * exit codes, else we'll just remanage */
		if (WIFEXITED (status) &&
		    (WEXITSTATUS (status) == DISPLAY_ABORT ||
		     WEXITSTATUS (status) == DISPLAY_REBOOT ||
		     WEXITSTATUS (status) == DISPLAY_HALT ||
		     WEXITSTATUS (status) == DISPLAY_SUSPEND ||
		     WEXITSTATUS (status) == DISPLAY_RESTARTGDM)) {
			_exit (WEXITSTATUS (status));
		} else {
			_exit (DISPLAY_REMANAGE);
		}
	} else if (pid != 0 && pid == d->sesspid) {
		d->sesspid = 0;
	} else if (pid != 0 && pid == d->chooserpid) {
		d->chooserpid = 0;
	} else if (pid != 0 && pid == d->servpid) {
		d->servstat = SERVER_DEAD;
		d->servpid = 0;
		gdm_server_wipe_cookies (d);
	} else if (pid == extra_process) {
		/* an extra process died, yay! */
		extra_process = -1;
	    	extra_status = status;
	}
    }
}

static void
gdm_slave_usr2_handler (int sig)
{
	gdm_debug ("gdm_slave_usr2_handler: %s got USR2 signal", d->name);
	
	gdm_got_usr2 = TRUE;
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
	sigset_t tmask;

	gdm_debug ("gdm_slave_xioerror_handler: I/O error for display %s", d->name);

	/* just for paranoia's sake */
	seteuid (0);
	setegid (0);

	sigemptyset (&tmask);
	sigaddset (&tmask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &tmask, NULL);  

	if (d->greetpid != 0) {
		pid_t pid = d->greetpid;
		d->greetpid = 0;
		greet = FALSE;
		if (kill (pid, SIGINT) == 0)
			waitpid (pid, 0, 0); 
	} else if (login != NULL) {
		gdm_slave_session_stop (d->sesspid);
		gdm_slave_session_cleanup ();
	}

	if (d->chooserpid != 0) {
		pid_t pid = d->chooserpid;
		d->chooserpid = 0;
		if (kill (pid, SIGINT) == 0)
			waitpid (pid, 0, 0); 
	}
    
	gdm_error (_("gdm_slave_xioerror_handler: Fatal X error - Restarting %s"), d->name);

	gdm_server_stop (d);
	gdm_verify_cleanup (d);

	if ((d->type == TYPE_LOCAL ||
	     d->type == TYPE_FLEXI) &&
	    (do_xfailed_on_xio_error ||
	     d->starttime + 5 >= time (NULL))) {
		_exit (DISPLAY_XFAILED);
	} else {
		_exit (DISPLAY_REMANAGE);
	}
}

char * 
gdm_slave_greeter_ctl (char cmd, const char *str)
{
    gchar buf[FIELD_SIZE];
    guchar c;

    if (str)
	g_print ("%c%c%s\n", STX, cmd, str);
    else
	g_print ("%c%c\n", STX, cmd);

    /* Skip random junk that might have accumulated */
    do {
	    c = getc (stdin);
    } while (c && c != STX);
    
    if (fgets (buf, FIELD_SIZE-1, stdin) == NULL) {
	    /* things don't seem well with the greeter, it probably died */
	    return NULL;
    }

    /* don't forget to flush */
    fflush (stdin);
    
    if (buf[0] != '\0') {
	    int len = strlen (buf);
	    if (buf[len-1] == '\n')
		    buf[len-1] = '\0';
	    return g_strdup (buf);
    } else {
	    return NULL;
    }
}

void
gdm_slave_greeter_ctl_no_ret (char cmd, const char *str)
{
	g_free (gdm_slave_greeter_ctl (cmd, str));
}


static void 
gdm_slave_exit (gint status, const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    
    g_free (s);

    /* just for paranoia's sake */
    seteuid (0);
    setegid (0);

    if (d != NULL) {
	    sigset_t tmask;

	    sigemptyset (&tmask);
	    sigaddset (&tmask, SIGCHLD);
	    sigprocmask (SIG_BLOCK, &tmask, NULL);  

	    /* Kill children where applicable */
	    if (d->greetpid != 0)
		    kill (d->greetpid, SIGTERM);
	    d->greetpid = 0;

	    if (d->chooserpid != 0)
		    kill (d->chooserpid, SIGTERM);
	    d->chooserpid = 0;

	    if (d->sesspid != 0)
		    kill (-(d->sesspid), SIGTERM);
	    d->sesspid = 0;

	    gdm_server_stop (d);
	    gdm_verify_cleanup (d);

	    if (d->servpid != 0)
		    kill (d->servpid, SIGTERM);
	    d->servpid = 0;
    }

    _exit (status);
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

static gint
gdm_slave_exec_script (GdmDisplay *d, const gchar *dir, const char *login,
		       struct passwd *pwent)
{
    pid_t pid;
    gchar *script, *defscript;
    const char *scr;
    gchar **argv;
    gint status;
    int i;

    if (!d || !dir)
	return EXIT_SUCCESS;

    script = g_strconcat (dir, "/", d->name, NULL);
    defscript = g_strconcat (dir, "/Default", NULL);

    if (! access (script, R_OK|X_OK)) {
	    scr = script;
    } else if (! access (defscript, R_OK|X_OK))  {
	    scr = defscript;
    } else {
	    g_free (script);
	    g_free (defscript);
	    return EXIT_SUCCESS;
    }

    pid = gdm_fork_extra ();

    switch (pid) {
	    
    case 0:
        for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close (i);

        /* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
        open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
        open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
        open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

        if (login != NULL) {
	        ve_setenv ("LOGNAME", login, TRUE);
	        ve_setenv ("USER", login, TRUE);
	        ve_setenv ("USERNAME", login, TRUE);
        } else {
	        ve_setenv ("LOGNAME", GdmUser, TRUE);
	        ve_setenv ("USER", GdmUser, TRUE);
	        ve_setenv ("USERNAME", GdmUser, TRUE);
        }
        if (pwent != NULL) {
		if (ve_string_empty (pwent->pw_dir))
			ve_setenv ("HOME", "/", TRUE);
		else
			ve_setenv ("HOME", pwent->pw_dir, TRUE);
	        ve_setenv ("SHELL", pwent->pw_shell, TRUE);
        } else {
	        ve_setenv ("HOME", "/", TRUE);
	        ve_setenv ("SHELL", "/bin/sh", TRUE);
        }

	/* Runs as root, uses server authfile */
	ve_setenv ("XAUTHORITY", d->authfile, TRUE);
        ve_setenv ("DISPLAY", d->name, TRUE);
	ve_setenv ("PATH", GdmRootPath, TRUE);
	ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
	ve_unsetenv ("MAIL");
	argv = ve_split (scr);
	execv (argv[0], argv);
	syslog (LOG_ERR, _("gdm_slave_exec_script: Failed starting: %s"), scr);
	_exit (EXIT_SUCCESS);
	    
    case -1:
	g_free (script);
	g_free (defscript);
	syslog (LOG_ERR, _("gdm_slave_exec_script: Can't fork script process!"));
	return EXIT_SUCCESS;
	
    default:
	gdm_wait_for_extra (&status);

	g_free (script);
	g_free (defscript);

	if (WIFEXITED (status))
	    return WEXITSTATUS (status);
	else
	    return EXIT_SUCCESS;
    }
}

gboolean
gdm_slave_greeter_check_interruption (const char *msg)
{
	if (msg != NULL &&
	    msg[0] == BEL) {
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
				return TRUE;
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

		/* Return true, this was an interruption, if it wasn't
		 * handled then the user will just get an error as if he
		 * entered an invalid login or passward.  Seriously BEL
		 * cannot be part of a login/password really */
		return TRUE;
	}
	return FALSE;
}

gboolean
gdm_slave_should_complain (void)
{
	if (do_timed_login ||
	    do_configurator)
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

	    /* runs as root */
	    ve_setenv ("XAUTHORITY", display->authfile, TRUE);
	    ve_setenv ("DISPLAY", display->name, TRUE);
	    ve_setenv ("PATH", GdmRootPath, TRUE);
	    ve_unsetenv ("MAIL");

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

/* EOF */
