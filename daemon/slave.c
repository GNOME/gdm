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

/* Global vars */
static GdmDisplay *d;
static gchar *login = NULL;
static sigset_t mask;
static gboolean greet = FALSE;
static FILE *greeter;
static pid_t last_killed_pid = 0;

extern gboolean gdm_first_login;

/* Configuration option variables */
extern gchar *GdmUser;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmSessDir;
extern gchar *GdmAutomaticLogin;
extern gchar *GdmGreeter;
extern gchar *GdmDisplayInit;
extern gchar *GdmPreSession;
extern gchar *GdmPostSession;
extern gchar *GdmSuspend;
extern gchar *GdmDefaultPath;
extern gchar *GdmRootPath;
extern gchar *GdmUserAuthFile;
extern gchar *GdmDefaultLocale;
extern gint GdmUserMaxFile;
extern gint GdmRelaxPerms;
extern gboolean GdmKillInitClients;
extern gint GdmRetryDelay;
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
static gchar*   gdm_get_user_shell(void);

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
	if (gdm_first_login &&
	    GdmAutomaticLogin != NULL &&
	    GdmAutomaticLogin[0] != '\0' &&
	    strcmp (GdmAutomaticLogin, "root") != 0) {
		gdm_first_login = FALSE;

		g_free (login);
		login = g_strdup (GdmAutomaticLogin);

		greet = FALSE;
		gdm_debug ("gdm_slave_start: Automatic login: %s", login);

		gdm_verify_setup_user (login, d->name);

		/* Run the init script. gdmslave suspends until script
		 * has terminated */
		gdm_slave_exec_script (d, GdmDisplayInit);

		gdm_debug ("gdm_slave_start: DisplayInit script finished");

		gdm_slave_session_start();

		gdm_debug ("gdm_slave_start: Automatic login done");
	} else {
		if (gdm_first_login)
			gdm_first_login = FALSE;
		gdm_slave_greeter ();  /* Start the greeter */
		gdm_slave_wait_for_login (); /* wait for a password */
		gdm_slave_session_start ();
	}
    } else {
	gdm_server_stop (d);
	_exit (DISPLAY_ABORT);
    }
}

static void
gdm_slave_wait_for_login (void)
{
	g_free (login);
	login = NULL;

	/* Chat with greeter */
	while (login == NULL) {
		gdm_debug ("gdm_slave_wait_for_login: In loop");
		login = gdm_verify_user (d->name);

		if (login == NULL) {
			gdm_debug ("gdm_slave_wait_for_login: No login/Bad login");
			sleep (GdmRetryDelay);
			g_free (gdm_slave_greeter_ctl (GDM_RESET, ""));
		}
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
	
	gdm_setenv ("XAUTHORITY", d->authfile);
	gdm_setenv ("DISPLAY", d->name);
	gdm_setenv ("HOME", "/"); /* Hack */
	gdm_setenv ("PATH", GdmDefaultPath);

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
gdm_slave_session_start (void)
{
    gchar *cfgdir, *sesspath;
    struct stat statbuf;
    struct passwd *pwent;
    gchar *session = NULL, *language = NULL, *usrsess, *usrlang;
    gboolean savesess = FALSE, savelang = FALSE, usrcfgok = FALSE, authok = FALSE;
    gint i;
    char *shell;
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
    } 
    else {
	usrsess = g_strdup ("");
	usrlang = g_strdup ("");
    }

    seteuid (0);
    setegid (GdmGroupId);

    if (greet) {
	    session = gdm_slave_greeter_ctl (GDM_SESS, usrsess);
	    language = gdm_slave_greeter_ctl (GDM_LANG, usrlang);
    }

    g_free (usrsess);
    g_free (usrlang);
    
    if (session == NULL ||
	session[0] == '\0') {
	    g_free (session);
	    session = g_strdup ("Default");
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

    if (greet) {
	    if (strlen (gdm_slave_greeter_ctl (GDM_SSESS, "")) > 0)
		    savesess = TRUE;

	    if (strlen (gdm_slave_greeter_ctl (GDM_SLANG, "")) > 0)
		    savelang = TRUE;

	    gdm_debug (_("gdm_slave_session_start: Authentication completed. Whacking greeter"));

	    /* do what you do when you quit, this will hang until the
	     * greeter decides to print an STX\n, meaning it can do some
	     * last minute cleanup */
	    gdm_slave_greeter_ctl (GDM_QUIT, "");

	    greet = FALSE;

	    /* Kill greeter and wait for it to die */
	    if (kill (d->greetpid, SIGINT) == 0)
		    waitpid (d->greetpid, 0, 0); 
	    d->greetpid = 0;
    }

    if (GdmKillInitClients)
	    gdm_server_whack_clients (d);

    /* Prepare user session */
    gdm_setenv ("DISPLAY", d->name);
    gdm_setenv ("LOGNAME", login);
    gdm_setenv ("USER", login);
    gdm_setenv ("USERNAME", login);
    gdm_setenv ("HOME", pwent->pw_dir);
    gdm_setenv ("GDMSESSION", session);
    gdm_setenv ("SHELL", pwent->pw_shell);
    gdm_unsetenv ("MAIL");	/* Unset $MAIL for broken shells */

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
    
    /* If script fails reset X server and restart greeter */
    if (gdm_slave_exec_script (d, GdmPreSession) != EXIT_SUCCESS) 
	gdm_slave_exit (DISPLAY_REMANAGE,
			_("gdm_slave_session_start: Execution of PreSession script returned > 0. Aborting."));

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
	    gnome_config_set_string (cfgstr, session);
	    gnome_config_sync();
	    g_free (cfgstr);
	}
	
	if (usrcfgok && savelang) {
	    gchar *cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
	    gnome_config_set_string (cfgstr, language);
	    gnome_config_sync();
	    g_free (cfgstr);
	}
	
	sesspath = g_strconcat (GdmSessDir, "/", session, NULL);
	
	gdm_debug ("Running %s for %s on %s", sesspath, login, d->name);

	for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
	    close(i);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
	open("/dev/null", O_RDWR); /* open stdout - fd 1 */
	open("/dev/null", O_RDWR); /* open stderr - fd 2 */
	
	/* Restore sigmask inherited from init */
	sigprocmask (SIG_SETMASK, &sysmask, NULL);
	
 	shell = gdm_get_user_shell ();
  
 	execl (shell, "-", "-c", sesspath, NULL);
	
	gdm_error (_("gdm_slave_session_start: Could not start session `%s'"), sesspath);

 	g_free (shell);
 	g_free (sesspath);
	
	gdm_slave_session_stop (0);
	gdm_slave_session_cleanup ();
	
	gdm_server_stop (d);
	_exit (DISPLAY_REMANAGE);
	
    default:
	break;
    }

    g_free (session);
    g_free (language);

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

static gchar*
gdm_get_user_shell(void)
{
	struct passwd *pw;
	int i;
	/*char *shell;*/
	static char *shells [] = {
		"/bin/bash", "/bin/zsh", "/bin/tcsh", "/bin/ksh",
		"/bin/csh", "/bin/sh", 0
	};

#if 0
	if ((shell = getenv ("SHELL"))){
		return g_strconcat (shell, NULL);
	}
#endif
	pw = getpwuid(getuid());
	if (pw && pw->pw_shell) {
		return g_strdup (pw->pw_shell);
	} 

	for (i = 0; shells [i]; i++) {
		if (g_file_exists (shells [i])){
			return g_strdup (shells[i]);
		}
	}
	
	return g_strdup("/bin/sh");
}

static void
gdm_slave_term_handler (int sig)
{
    gdm_debug ("gdm_slave_term_handler: %s got TERM signal", d->name);

    if (d->greetpid != 0) {
	gdm_debug ("gdm_slave_term_handler: Whacking greeter");
	if (kill (d->greetpid, SIGINT) == 0)
		waitpid (d->greetpid, 0, 0); 
	d->greetpid = 0;
    } 
    else if (login)
	gdm_slave_session_stop (d->sesspid);
    
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

gchar * 
gdm_slave_greeter_ctl (gchar cmd, const gchar *str)
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
    
    if (strlen (buf)) {
	buf[strlen (buf)-1] = '\0';
	return g_strdup (buf);
    }
    else
	return NULL;
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
	gdm_setenv ("PATH", GdmRootPath);
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


/* EOF */
