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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <syslog.h>

#include "gdm.h"
#include "slave.h"
#include "misc.h"
#include "verify.h"
#include "filecheck.h"
#include "auth.h"
#include "server.h"


static const gchar RCSid[]="$Id$";

/* Some per slave globals */
static GdmDisplay *d;
static gchar *login = NULL;
static sigset_t mask;
static gboolean greet = FALSE;
static FILE *greeter;
static pid_t last_killed_pid = 0;
gboolean do_timed_login = FALSE; /* if this is true, login the timed login */
gboolean do_configurator = FALSE; /* if this is true, login as root
				   * and start the configurator */

extern gboolean gdm_first_login;

/* Configuration option variables */
extern gchar *GdmUser;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmGnomeDefaultSession;
extern gchar *GdmSessDir;
extern gchar *GdmAutomaticLogin;
extern gchar *GdmConfigurator;
extern gboolean GdmConfigAvailable;
extern gboolean GdmSystemMenu;
extern gchar *GdmGreeter;
extern gchar *GdmDisplayInit;
extern gchar *GdmPreSession;
extern gchar *GdmPostSession;
extern gchar *GdmSuspend;
extern gchar *GdmDefaultPath;
extern gchar *GdmRootPath;
extern gchar *GdmUserAuthFile;
extern gchar *GdmDefaultLocale;
extern gchar *GdmTimedLogin;
extern gint GdmTimedLoginDelay;
extern gint GdmUserMaxFile;
extern gint GdmSessionMaxFile;
extern gint GdmRelaxPerms;
extern gboolean GdmKillInitClients;
extern gint GdmRetryDelay;
extern gboolean GdmAllowRoot;
extern sigset_t sysmask;
extern gchar *argdelim;


/* Local prototypes */
static gint     gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt);
static gint     gdm_slave_xioerror_handler (Display *disp);
static void	gdm_slave_run (GdmDisplay *display);
static void	gdm_slave_wait_for_login (void);
static void     gdm_slave_greeter (void);
static void     gdm_slave_session_start (void);
static void     gdm_slave_session_stop (pid_t sesspid);
static void     gdm_slave_session_cleanup (void);
static void     gdm_slave_term_handler (int sig);
static void     gdm_slave_child_handler (int sig);
static void     gdm_slave_exit (gint status, const gchar *format, ...);
static gint     gdm_slave_exec_script (GdmDisplay*, gchar *dir);


void 
gdm_slave_start (GdmDisplay *display)
{  
	time_t first_time;
	int death_count;
	struct sigaction term, child;

	if (!display)
		return;

	gdm_debug ("gdm_slave_start: Starting slave process for %s", display->name);
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

	/* The signals we wish to listen to */
	sigfillset (&mask);
	sigdelset (&mask, SIGINT);
	sigdelset (&mask, SIGTERM);
	sigdelset (&mask, SIGCHLD);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	first_time = time (NULL);
	death_count = 0;

	for (;;) {
		time_t the_time;

		gdm_debug ("gdm_slave_start: Loop Thingie");
		gdm_slave_run (display);

		if (d->type != TYPE_LOCAL)
			break;

		the_time = time (NULL);

		death_count ++;

		if ((the_time - first_time) <= 0 ||
		    (the_time - first_time) > 40) {
			first_time = the_time;
			death_count = 0;
		} else if (death_count > 6) {
			/* exitting the loop will cause an
			 * abort actually */
			break;
		}
	}
}

static void
setup_automatic_session (GdmDisplay *display, const char *name)
{
	g_free (login);
	login = g_strdup (name);

	greet = FALSE;
	gdm_debug ("gdm_slave_start: Automatic login: %s", login);

	gdm_verify_setup_user (login, display->name);

	/* Run the init script. gdmslave suspends until script
	 * has terminated */
	gdm_slave_exec_script (display, GdmDisplayInit);

	gdm_debug ("gdm_slave_start: DisplayInit script finished");
}


static void 
gdm_slave_run (GdmDisplay *display)
{  
    gint openretries = 0;
    
    d = display;

    if (d->sleep_before_run > 0) {
	    gdm_debug ("gdm_slave_run: Sleeping %d seconds before server start", d->sleep_before_run);
	    sleep (d->sleep_before_run);
	    d->sleep_before_run = 0;
    }

    /* if this is local display start a server if one doesn't
     * exist */
    if (d->type == TYPE_LOCAL &&
	d->servpid <= 0)
	    gdm_server_start (d);
    
    gdm_setenv ("XAUTHORITY", d->authfile);
    gdm_setenv ("DISPLAY", d->name);
    
    /* X error handlers to avoid the default one (i.e. exit (1)) */
    XSetErrorHandler (gdm_slave_xerror_handler);
    XSetIOErrorHandler (gdm_slave_xioerror_handler);
    
    /* We keep our own (windowless) connection (dsp) open to avoid the
     * X server resetting due to lack of active connections. */

    gdm_debug ("gdm_slave_start: Opening display %s", d->name);
    d->dsp = NULL;
    
    while (openretries < 10 &&
	   d->dsp == NULL) {
	d->dsp = XOpenDisplay (d->name);
	
	if (d->dsp == NULL) {
	    gdm_debug ("gdm_slave_start: Sleeping %d on a retry", openretries*2);
	    sleep (openretries*2);
	    openretries++;
	}
    }

    if (d->dsp != NULL) {
	if (d->type == TYPE_LOCAL &&
	    gdm_first_login &&
	    ! gdm_string_empty (GdmAutomaticLogin) &&
	    strcmp (GdmAutomaticLogin, "root") != 0) {
		gdm_first_login = FALSE;

		setup_automatic_session (d, GdmAutomaticLogin);

		gdm_slave_session_start();

		gdm_debug ("gdm_slave_start: Automatic login done");
	} else {
		if (gdm_first_login)
			gdm_first_login = FALSE;
		gdm_slave_greeter ();  /* Start the greeter */
		gdm_slave_wait_for_login (); /* wait for a password */
		if (do_timed_login) {
			/* timed out into a timed login */
			do_timed_login = FALSE;
			setup_automatic_session (d, GdmTimedLogin);
		}
		gdm_slave_session_start ();
	}
    } else {
	gdm_server_stop (d);
	_exit (DISPLAY_ABORT);
    }
}

static void
gdm_slave_whack_greeter (void)
{
	sigset_t tmask, omask;

	sigemptyset (&tmask);
	sigaddset (&tmask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &tmask, &omask);  

	/* do what you do when you quit, this will hang until the
	 * greeter decides to print an STX\n, meaning it can do some
	 * last minute cleanup */
	gdm_slave_greeter_ctl_no_ret (GDM_QUIT, "");

	greet = FALSE;

	/* Kill greeter and wait for it to die */
	if (d->greetpid > 0 &&
	    kill (d->greetpid, SIGINT) == 0)
		waitpid (d->greetpid, 0, 0); 
	d->greetpid = 0;

	sigprocmask (SIG_SETMASK, &omask, NULL);
}

static void
run_config (GdmDisplay *display, struct passwd *pwent)
{
	display->sesspid = fork ();
	if (display->sesspid < 0) {
		/* can't fork, damnit */
		display->sesspid = 0;
		return;
	}

	if (display->sesspid == 0) {
		int i;
		char **argv;
		/* child */

		/* setup environment */
		gdm_setenv ("XAUTHORITY", display->authfile);
		gdm_setenv ("DISPLAY", display->name);
		gdm_setenv ("LOGNAME", "root");
		gdm_setenv ("USER", "root");
		gdm_setenv ("USERNAME", "root");
		gdm_setenv ("HOME", pwent->pw_dir);
		gdm_setenv ("SHELL", pwent->pw_shell);

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open("/dev/null", O_RDWR); /* open stderr - fd 2 */

		/* exec the configurator */
		argv = g_strsplit (GdmConfigurator, " ", MAX_ARGS);	
		execv (argv[0], argv);

		/* ignore errors */
		_exit (0);
	} else {
		waitpid (display->sesspid, 0, 0);
		display->sesspid = 0;
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
		gdm_debug ("gdm_slave_wait_for_login: In loop");
		login = gdm_verify_user (NULL /* username*/,
					 d->name,
					 d->type == TYPE_LOCAL);
		/* Complex, make sure to always handle the do_configurator
		 * and do_timed_login after any call to gdm_verify_user */

		if (do_configurator) {
			struct passwd *pwent;
			gboolean oldAllowRoot;

			do_configurator = FALSE;
			g_free (login);
			login = NULL;
			gdm_slave_greeter_ctl_no_ret
				(GDM_MSGERR,
				 _("Enter the root password\n"
				   "to run the configuration."));

			/* we always allow root for this */
			oldAllowRoot = GdmAllowRoot;
			GdmAllowRoot = TRUE;
			gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, "root");
			login = gdm_verify_user ("root",
						 d->name,
						 d->type == TYPE_LOCAL);
			GdmAllowRoot = oldAllowRoot;

			/* the wanker can't remember his password */
			if (login == NULL) {
				gdm_debug (_("gdm_slave_wait_for_login: No login/Bad login"));
				sleep (GdmRetryDelay);
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

			/* disable the login screen, we don't want people to
			 * log in in the meantime */
			gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");

			run_config (d, pwent);
			/* note that we may never get here as the configurator
			 * may have sighupped the main gdm server and with it
			 * wiped us */

			gdm_verify_cleanup ();

			gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");
			gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
			continue;
		}

		/* the user timed out into a timed login during the
		 * conversation */
		if (do_timed_login) {
			break;
		}

		if (login == NULL) {
			gdm_debug (_("gdm_slave_wait_for_login: No login/Bad login"));
			sleep (GdmRetryDelay);
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
	}
}


static void
gdm_slave_greeter (void)
{
    gint pipe1[2], pipe2[2];  
    gchar **argv;
    
    gdm_debug ("gdm_slave_greeter: Running greeter on %s", d->name);
    
    /* Run the init script. gdmslave suspends until script has terminated */
    gdm_slave_exec_script (d, GdmDisplayInit);

    /* Open a pipe for greeter communications */
    if (pipe (pipe1) < 0 || pipe (pipe2) < 0) 
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Can't init pipe to gdmgreeter"));
    
    greet = TRUE;

    /* Fork. Parent is gdmslave, child is greeter process. */
    last_killed_pid = 0; /* race condition wrapper,
			  * it could be that we recieve sigchld before
			  * we can set greetpid.  eek! */
    switch (d->greetpid = fork()) {
	
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
	
	if (setgid (GdmGroupId) < 0) 
	    gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Couldn't set groupid to %d"), GdmGroupId);

	if (initgroups (GdmUser, GdmGroupId) < 0)
            gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: initgroups() failed for %s"), GdmUser);
	
	if (setuid (GdmUserId) < 0) 
	    gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Couldn't set userid to %d"), GdmUserId);
	
	gdm_clearenv_no_lang ();
	gdm_setenv ("XAUTHORITY", d->authfile);
	gdm_setenv ("DISPLAY", d->name);
	gdm_setenv ("HOME", "/"); /* Hack */
	gdm_setenv ("PATH", GdmDefaultPath);

	/* Note that this is just informative, the slave will not listen to
	 * the greeter even if it does something it shouldn't on a non-local
	 * display so it's not a security risk */
	if (d->type == TYPE_LOCAL) {
		gdm_setenv ("GDM_IS_LOCAL", "yes");
	} else {
		gdm_unsetenv ("GDM_IS_LOCAL");
	}

	/* this is again informal only, if the greeter does time out it will
	 * not actually login a user if it's not enabled for this display */
	if (d->timed_login_ok) {
		gdm_setenv ("GDM_TIMED_LOGIN_OK", "yes");
	} else {
		gdm_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	argv = g_strsplit (GdmGreeter, argdelim, MAX_ARGS);
	execv (argv[0], argv);
	
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Error starting greeter on display %s"), d->name);
	
    case -1:
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_greeter: Can't fork gdmgreeter process"));
	
    default:
	if (last_killed_pid == d->greetpid) {
		/* foo, this is a bad case really.  We always remanage since
		 * we assume that the greeter died, we should probably store
		 * the status.  But this race is not likely to happen
		 * normally. */
		gdm_server_stop (d);
		_exit (DISPLAY_REMANAGE);
	}
	close (pipe1[0]);
	close (pipe2[1]);

	fcntl(pipe1[1], F_SETFD, fcntl(pipe1[1], F_GETFD, 0) | FD_CLOEXEC);
	fcntl(pipe2[0], F_SETFD, fcntl(pipe2[2], F_GETFD, 0) | FD_CLOEXEC);

	if (pipe1[1] != STDOUT_FILENO) 
	    dup2 (pipe1[1], STDOUT_FILENO);
	
	if (pipe2[0] != STDIN_FILENO) 
	    dup2 (pipe2[0], STDIN_FILENO);
	
	greeter = fdopen (STDIN_FILENO, "r");
	
	gdm_debug ("gdm_slave_greeter: Greeter on pid %d", d->greetpid);
	break;
    }
}

static void
read_sessions (FILE *fp, GString *sessions, const char *def, gboolean *got_def)
{
	char buf[FIELD_SIZE];

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p;
		if (buf[0] != '[')
			continue;
		p = strrchr (buf, ']');
		if (p == NULL)
			continue;
		*p = '\0';

		if (strcmp (&buf[1], "Trash") == 0 ||
		    strcmp (&buf[1], "Chooser") == 0 ||
		    strcmp (&buf[1], "Warner") == 0)
			continue;

		if (strcmp (&buf[1], def) == 0)
			*got_def = TRUE;

		g_string_append_c (sessions, '\n');
		g_string_append (sessions, &buf[1]);
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
			def = g_strdup ("Gnome");

		g_free (cfgstr);
	} else {
		def = g_strdup ("Gnome");
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

	if (gdm_string_empty (GdmSessDir))
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

/* A hack really, this pretends to be a standalone gtk program */
/* this should only be called once forked and all thingies are closed */
static void
run_error_dialog (const char *error)
{
	char *argv_s[] = { "error", NULL };
	char **argv = argv_s;
	int argc = 1;
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *button;

	gtk_init (&argc, &argv);

	dialog = gtk_dialog_new ();

	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC(gtk_main_quit),
			    NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Cannot start session"));

	label = gtk_label_new (error);

	gtk_container_set_border_width
		(GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 10);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label,
			    TRUE, TRUE, 0);

	button = gtk_button_new_with_label (_("OK"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area),
			    button, TRUE, TRUE, 0);

	gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
				   GTK_SIGNAL_FUNC (gtk_widget_destroy), 
				   GTK_OBJECT (dialog));

	gtk_widget_show_all (dialog);

	gtk_main ();
}

static void
gdm_slave_session_start (void)
{
    char *cfgdir, *sesspath;
    struct stat statbuf;
    struct passwd *pwent;
    char *save_session = NULL, *session = NULL, *language = NULL, *usrsess, *usrlang;
    char *gnome_session = NULL;
    gboolean savesess = FALSE, savelang = FALSE, usrcfgok = FALSE, authok = FALSE;
    int i;
    const char *shell = NULL;
    pid_t sesspid;

    pwent = getpwnam (login);

    if (!pwent) 
	gdm_slave_exit (DISPLAY_REMANAGE,
			_("gdm_slave_session_start: User passed auth but getpwnam(%s) failed!"), login);

    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    /* Check if ~user/.gnome exists. Create it otherwise. */
    cfgdir = g_strconcat (pwent->pw_dir, "/.gnome", NULL);
    
    if (stat (cfgdir, &statbuf) == -1) {
	mkdir (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	chmod (cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
    }
    
    /* Sanity check on ~user/.gnome/gdm */
    usrcfgok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
			       cfgdir, "gdm", TRUE, GdmUserMaxFile,
			       GdmRelaxPerms);
    g_free (cfgdir);

    if (usrcfgok) {
	gchar *cfgstr;

	cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/last", NULL);
	usrsess = gnome_config_get_string (cfgstr);
	if (usrsess == NULL)
		usrsess = g_strdup ("");
	g_free (cfgstr);

	cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
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
    
    if (session == NULL ||
	session[0] == '\0') {
	    g_free (session);
	    session = find_a_session ();
	    if (session == NULL) {
		    /* we're running out of options */
		    session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	    }
    }

    if (language == NULL ||
	language[0] == '\0') {
	    const char *lang = g_getenv ("LANG");

	    g_free (language);

	    if (lang != NULL &&
		lang[0] != '\0') 
		    language = g_strdup (lang);
	    else
		    language = g_strdup (GdmDefaultLocale);
	    savelang = TRUE;
    }

    /* save this session as the users session */
    save_session = g_strdup (session);

    if (greet) {
	    char *ret = gdm_slave_greeter_ctl (GDM_SSESS, "");
	    if (ret != NULL && ret[0] != '\0')
		    savesess = TRUE;
	    g_free (ret);

	    ret = gdm_slave_greeter_ctl (GDM_SLANG, "");
	    if (ret != NULL && ret[0] != '\0')
		    savelang = TRUE;
	    g_free (ret);

	    if (strcmp (session, "Gnome Chooser") == 0) {
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
	    }

	    gdm_debug (_("gdm_slave_session_start: Authentication completed. Whacking greeter"));

	    /* do what you do when you quit, this will hang until the
	     * greeter decides to print an STX\n, meaning it can do some
	     * last minute cleanup */
	    gdm_slave_greeter_ctl_no_ret (GDM_QUIT, "");

	    greet = FALSE;

	    /* Kill greeter and wait for it to die */
	    if (kill (d->greetpid, SIGINT) == 0)
		    waitpid (d->greetpid, 0, 0); 
	    d->greetpid = 0;
    }

    if (GdmKillInitClients)
	    gdm_server_whack_clients (d);

    /* setup some env for PreSession script */
    gdm_setenv ("DISPLAY", d->name);
    gdm_setenv ("LOGNAME", login);
    gdm_setenv ("USER", login);
    gdm_setenv ("USERNAME", login);
    gdm_setenv ("HOME", pwent->pw_dir);
    gdm_setenv ("SHELL", pwent->pw_shell);

    /* If script fails reset X server and restart greeter */
    if (gdm_slave_exec_script (d, GdmPreSession) != EXIT_SUCCESS) 
	gdm_slave_exit (DISPLAY_REMANAGE,
			_("gdm_slave_session_start: Execution of PreSession script returned > 0. Aborting."));

    /* set things back to moi, for lack of confusion */
    gdm_setenv ("LOGNAME", GdmUser);
    gdm_setenv ("USER", GdmUser);
    gdm_setenv ("USERNAME", GdmUser);
    gdm_setenv ("HOME", "/");
    gdm_setenv ("SHELL", "/bin/sh");

    /* Setup cookie -- We need this information during cleanup, thus
     * cookie handling is done before fork()ing */

    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    authok = gdm_auth_user_add (d, pwent->pw_uid, pwent->pw_dir);

    seteuid (0);
    setegid (GdmGroupId);
    
    if ( ! authok) {
	gdm_debug ("gdm_slave_session_start: Auth not OK");
	gdm_slave_session_stop (0);
	gdm_slave_session_cleanup ();
	
	gdm_server_stop (d);
	_exit (DISPLAY_REMANAGE);
    } 

    /* Start user process */
    switch (d->sesspid = fork()) {
	
    case -1:
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_session_start: Error forking user session"));
	
    case 0:

	gdm_clearenv ();

	/* Prepare user session */
	gdm_setenv ("XAUTHORITY", d->userauth);
	gdm_setenv ("DISPLAY", d->name);
	gdm_setenv ("LOGNAME", login);
	gdm_setenv ("USER", login);
	gdm_setenv ("USERNAME", login);
	gdm_setenv ("HOME", pwent->pw_dir);
	gdm_setenv ("GDMSESSION", session);
	gdm_setenv ("SHELL", pwent->pw_shell);
	gdm_unsetenv ("MAIL");	/* Unset $MAIL for broken shells */

	if (gnome_session != NULL)
		gdm_setenv ("GDM_GNOME_SESSION", gnome_session);

	/* Special PATH for root */
	if (pwent->pw_uid == 0)
		gdm_setenv ("PATH", GdmRootPath);
	else
		gdm_setenv ("PATH", GdmDefaultPath);

	/* Set locale */
	if (strcasecmp (language, "english") == 0) {
		gdm_setenv ("LANG", "C");
		gdm_setenv ("GDM_LANG", "C");
	} else {
		gdm_setenv ("LANG", language);
		gdm_setenv ("GDM_LANG", language);
	}
    
	/* setup the verify env vars */
	gdm_verify_env_setup ();
    
	setpgid (0, 0);
	
	umask (022);
	
	if (setgid (pwent->pw_gid) < 0) 
	    gdm_slave_exit (DISPLAY_REMANAGE,
			    _("gdm_slave_session_start: Could not setgid %d. Aborting."), pwent->pw_gid);
	
	if (initgroups (login, pwent->pw_gid) < 0)
	    gdm_slave_exit (DISPLAY_REMANAGE,
			    _("gdm_slave_session_start: initgroups() failed for %s. Aborting."), login);
	
	if (setuid (pwent->pw_uid) < 0) 
	    gdm_slave_exit (DISPLAY_REMANAGE,
			    _("gdm_slave_session_start: Could not become %s. Aborting."), login);
	
	chdir (pwent->pw_dir);
	
	if (usrcfgok && savesess) {
	    gchar *cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/last", NULL);
	    gnome_config_set_string (cfgstr, save_session);
	    gnome_config_sync();
	    g_free (cfgstr);
	}
	
	if (usrcfgok && savelang) {
	    gchar *cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
	    gnome_config_set_string (cfgstr, language);
	    gnome_config_sync();
	    g_free (cfgstr);
	}

	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open("/dev/null", O_RDWR); /* open stderr - fd 2 */
	
	/* Restore sigmask inherited from init */
	sigprocmask (SIG_SETMASK, &sysmask, NULL);
	

	/* If "Gnome Chooser" is still set as a session,
	 * just change that to "Gnome", since "Gnome Chooser" is a
	 * fake */
	if (strcmp (session, "Gnome Chooser") == 0) {
		g_free (session);
		session = g_strdup ("Gnome");
	}
	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open("/dev/null", O_RDWR); /* open stderr - fd 2 */
	
	/* Restore sigmask inherited from init */
	sigprocmask (SIG_SETMASK, &sysmask, NULL);
	
	sesspath = NULL;

	if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0) {
		sesspath = gnome_is_program_in_path ("gnome-session");
		if (sesspath == NULL) {
			if (g_file_exists ("/usr/bin/X11/gnome-session")) {
				sesspath = g_strdup ("/usr/bin/X11/gnome-session");
			} else if (g_file_exists ("/usr/X11R6/bin/gnome-session")) {
				sesspath = g_strdup ("/usr/X11R6/bin/gnome-session");
			} else if (g_file_exists ("/usr/bin/gnome-session")) {
				sesspath = g_strdup ("/usr/bin/gnome-session");
			} else if (g_file_exists ("/usr/local/bin/gnome-session")) {
				sesspath = g_strdup ("/usr/local/bin/gnome-session");
			} else {
				/* yaikes */
				gdm_error (_("gdm_slave_session_start: gnome-session not found for a failsafe gnome session, trying xterm"));
				g_free (session);
				session = g_strdup (GDM_SESSION_FAILSAFE_XTERM);
			}
		}
	}

	/* an if and not an else, we could have done a fall-through
	 * to here in the above code if we can't find gnome-session */
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0) {
		sesspath = gnome_is_program_in_path ("xterm");
		if (sesspath == NULL) {
			if (g_file_exists ("/usr/bin/X11/xterm")) {
				sesspath = g_strdup ("/usr/bin/X11/xterm");
			} else if (g_file_exists ("/usr/X11R6/bin/xterm")) {
				sesspath = g_strdup ("/usr/X11R6/bin/xterm");
			} else if (g_file_exists ("/usr/bin/xterm")) {
				sesspath = g_strdup ("/usr/bin/xterm");
			} else {
				run_error_dialog
					(_("Cannot find \"xterm\" to start "
					   "a failsafe session."));
				/* nyah nyah nyah nyah nyah */
				_exit (0);
			}
		}
	} 

	if (sesspath == NULL) {
		if (GdmSessDir != NULL)
			sesspath = g_strconcat (GdmSessDir, "/", session, NULL);
		else
			sesspath = g_strdup ("/Eeeeek! Eeeeek!");
	}
	
	gdm_debug (_("Running %s for %s on %s"),
		   sesspath, login, d->name);

	if (pwent->pw_shell != NULL &&
	    pwent->pw_shell[0] != '\0') {
		shell = pwent->pw_shell;
	} else {
		shell = "/bin/sh";
	}

	/* just a stupid test, the below would fail, but this gives a better
	 * message */
	if (strcmp (shell, "/bin/false") == 0) {
		gdm_error (_("gdm_slave_session_start: User not allowed to log in"));
		run_error_dialog (_("The system administrator has\n"
				    "disabled your account."));
	} else if (access (sesspath, X_OK|R_OK) != 0) {
		gdm_error (_("gdm_slave_session_start: Could not find session `%s'"), sesspath);
		/* if we can't read and exec the session, then make a nice
		 * error dialog */
		run_error_dialog
			(_("Cannot start the session, most likely the\n"
			   "session does not exist.  Please select from\n"
			   "the list of available sessions in the login\n"
			   "dialog window."));
	} else {
		execl (shell, "-", "-c", sesspath, NULL);

		gdm_error (_("gdm_slave_session_start: Could not start session `%s'"), sesspath);
		run_error_dialog
			(_("Cannot start your shell.  It could be that the\n"
			   "system administrator has disabled your login.\n"
			   "It could also indicate an error with your account.\n"));
	}
	
	/* ends as if nothing bad happened */
	_exit (0);
	
    default:
	break;
    }

    g_free (session);
    g_free (save_session);
    g_free (language);
    g_free (gnome_session);

    sesspid = d->sesspid;

    /* Wait for the user's session to exit, but by this time the
     * session might have ended, so check for 0 */
    if (d->sesspid > 0)
	    waitpid (d->sesspid, NULL, 0);
    d->sesspid = 0;

    gdm_debug ("gdm_slave_session_start: Session ended OK");

    gdm_slave_session_stop (sesspid);
    gdm_slave_session_cleanup ();
}


static void
gdm_slave_session_stop (pid_t sesspid)
{
    struct passwd *pwent;
    char *local_login;

    local_login = login;
    login = NULL;

    gdm_debug ("gdm_slave_session_stop: %s on %s", local_login, d->name);

    if (sesspid > 0)
	    kill (- (sesspid), SIGTERM);
    
    gdm_verify_cleanup();
    
    pwent = getpwnam (local_login);	/* PAM overwrites our pwent */

    g_free (local_login);

    if (!pwent)
	return;
    
    /* Remove display from ~user/.Xauthority */
    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    gdm_auth_user_remove (d, pwent->pw_uid);

    seteuid (0);
    setegid (GdmGroupId);
}

static void
gdm_slave_session_cleanup (void)
{
    gdm_debug ("gdm_slave_session_cleanup: %s on %s", login, d->name);

    /* kill login */
    g_free (login);
    login = NULL;
    
    /* Execute post session script */
    gdm_debug ("gdm_slave_session_cleanup: Running post session script");
    gdm_slave_exec_script (d, GdmPostSession);

    /* Cleanup */
    gdm_debug ("gdm_slave_session_cleanup: Killing windows");
    gdm_server_reinit (d);
}

static void
gdm_slave_term_handler (int sig)
{
    gdm_debug ("gdm_slave_term_handler: %s got TERM signal", d->name);

    if (d->greetpid != 0) {
	gdm_debug ("gdm_slave_term_handler: Whacking greeter");
	if (kill (d->greetpid, sig) == 0)
		waitpid (d->greetpid, 0, 0); 
	d->greetpid = 0;
    } else if (login) {
	gdm_slave_session_stop (d->sesspid);
	gdm_slave_session_cleanup ();
    }
    
    gdm_debug ("gdm_slave_term_handler: Whacking server");

    gdm_server_stop (d);
    gdm_verify_cleanup();
    _exit (DISPLAY_ABORT);
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
		/* if greet is TRUE, then the greeter died outside of our
		 * control really */
		gdm_server_stop (d);
		gdm_verify_cleanup ();
		if (WIFEXITED (status)) {
			_exit (WEXITSTATUS (status));
		} else {
			_exit (DISPLAY_REMANAGE);
		}
	} else if (pid != 0 && pid == d->sesspid) {
		d->sesspid = 0;
	} else if (pid != 0 && pid == d->servpid) {
		d->servstat = SERVER_DEAD;
		d->servpid = 0;
		unlink (d->authfile);
	} else if (pid != 0) {
		last_killed_pid = pid;
	}
    }
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
    gdm_debug ("gdm_slave_xioerror_handler: I/O error for display %s", d->name);
    
    if (login)
	gdm_slave_session_stop (d->sesspid);
    
    gdm_error (_("gdm_slave_xioerror_handler: Fatal X error - Restarting %s"), d->name);

    gdm_server_stop (d);
    gdm_verify_cleanup ();
    _exit (DISPLAY_REMANAGE);
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
	    c = getc (greeter);
    } while (c && c != STX);
    
    fgets (buf, FIELD_SIZE-1, greeter);

    /* don't forget to flush */
    fflush (greeter);
    
    if (strlen (buf)) {
	buf[strlen (buf)-1] = '\0';
	return g_strdup (buf);
    }
    else
	return NULL;
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
    
    syslog (LOG_ERR, s);
    
    g_free (s);

    gdm_server_stop (d);
    gdm_verify_cleanup ();

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

    if (d->servpid != 0)
	kill (d->servpid, SIGTERM);
    d->servpid = 0;

    _exit (status);
}


static gint
gdm_slave_exec_script (GdmDisplay *d, gchar *dir)
{
    pid_t pid;
    gchar *script, *defscript, *scr;
    gchar **argv;
    gint status;

    if (!d || !dir)
	return EXIT_SUCCESS;

    script = g_strconcat (dir, "/", d->name, NULL);
    defscript = g_strconcat (dir, "/Default", NULL);

    if (! access (script, R_OK|X_OK))
	scr = script;
    else if (! access (defscript, R_OK|X_OK)) 
	scr = defscript;
    else
	return EXIT_SUCCESS;

    switch (pid = fork()) {
	    
    case 0:
	gdm_setenv ("XAUTHORITY", d->authfile);
        gdm_setenv ("DISPLAY", d->name);
	gdm_setenv ("PATH", GdmRootPath);
	gdm_unsetenv ("MAIL");
	argv = g_strsplit (scr, argdelim, MAX_ARGS);
	execv (argv[0], argv);
	syslog (LOG_ERR, _("gdm_slave_exec_script: Failed starting: %s"), scr);
	return EXIT_SUCCESS;
	    
    case -1:
	syslog (LOG_ERR, _("gdm_slave_exec_script: Can't fork script process!"));
	return EXIT_SUCCESS;
	
    default:
	waitpid (pid, &status, 0);	/* Wait for script to finish */

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
			if (d->type == TYPE_LOCAL &&
			    d->timed_login_ok &&
			    ! gdm_string_empty (GdmTimedLogin) &&
			    GdmTimedLoginDelay > 0) {
				do_timed_login = TRUE;
				return TRUE;
			}
			break;
		case GDM_INTERRUPT_CONFIGURE:
			if (d->type == TYPE_LOCAL &&
			    GdmConfigAvailable &&
			    GdmSystemMenu &&
			    ! gdm_string_empty (GdmConfigurator)) {
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

/* EOF */
