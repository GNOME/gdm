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
GdmDisplay *d;
gchar *login = NULL;
sigset_t mask, omask;
gboolean pingack;
gboolean greet = TRUE;
FILE *greeter;

/* Configuration option variables */
extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmSessDir;
extern gchar *GdmGreeter;
extern gchar *GdmDisplayInit;
extern gchar *GdmPreSession;
extern gchar *GdmPostSession;
extern gchar *GdmSuspend;
extern gchar *GdmDefaultPath;
extern gchar *GdmRootPath;
extern gchar *GdmUserAuthFile;
extern gint GdmUserMaxFile;
extern gint GdmRelaxPerms;
extern gint GdmKillInitClients;
extern gint GdmRetryDelay;
extern sigset_t sysmask;
extern gchar *argdelim;

/* Local prototypes */
static gint     gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt);
static gint     gdm_slave_xioerror_handler (Display *disp);
static void     gdm_slave_greeter (void);
static void     gdm_slave_session_start (void);
static void     gdm_slave_session_stop (void);
static void     gdm_slave_session_cleanup (void);
static void     gdm_slave_term_handler (int sig);
static void     gdm_slave_child_handler (int sig);
static void     gdm_slave_windows_kill (void);
static void     gdm_slave_xsync_handler (int sig);
static gboolean gdm_slave_xsync_ping (void);
static void     gdm_slave_exit (gint status, const gchar *format, ...);
static gint     gdm_slave_exec_script (GdmDisplay*, gchar *dir);


void 
gdm_slave_start (GdmDisplay *display)
{  
    struct sigaction term, child;
    gint openretries = 0;
    
    if (!display)
	return;

    gdm_debug ("gdm_slave_start: Starting slave process for %s", display->name);
    
    d = display;

    if (d->type == TYPE_LOCAL)
	gdm_server_start (d);
    
    gdm_setenv ("XAUTHORITY", d->authfile);
    gdm_setenv ("DISPLAY", d->name);
    
    /* Handle a INT/TERM signals from gdm master */
    term.sa_handler = gdm_slave_term_handler;
    term.sa_flags = SA_RESTART;
    sigemptyset (&term.sa_mask);
    sigaddset (&term.sa_mask, SIGTERM);
    sigaddset (&term.sa_mask, SIGINT);
    
    if ((sigaction (SIGTERM, &term, NULL) < 0) || (sigaction (SIGINT, &term, NULL) < 0))
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_init: Error setting up TERM/INT signal handler"));
    
    /* Child handler. Keeps an eye on greeter/session */
    child.sa_handler = gdm_slave_child_handler;
    child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset (&child.sa_mask);
    
    if (sigaction (SIGCHLD, &child, NULL) < 0) 
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_init: Error setting up CHLD signal handler"));
    
    /* The signals we wish to listen to */
    sigfillset (&mask);
    sigdelset (&mask, SIGINT);
    sigdelset (&mask, SIGTERM);
    sigdelset (&mask, SIGCHLD);
    sigprocmask (SIG_SETMASK, &mask, NULL);
    
    /* X error handlers to avoid the default one (i.e. exit (1)) */
    XSetErrorHandler (gdm_slave_xerror_handler);
    XSetIOErrorHandler (gdm_slave_xioerror_handler);
    
    /* We keep our own (windowless) connection (dsp) open to avoid the
     * X server resetting due to lack of active connections. */

    gdm_debug ("gdm_slave_start: Opening display %s", d->name);
    d->dsp = NULL;
    
    while (openretries < 10 && d->dsp == NULL) {
	d->dsp = XOpenDisplay (d->name);
	
	if (!d->dsp) {
	    gdm_debug ("gdm_slave_start: Sleeping %d on a retry", openretries*2);
	    sleep (openretries*2);
	    openretries++;
	}
    }
    
    if (d->dsp) 
	gdm_slave_greeter();  /* Start the greeter */
    else
	exit (DISPLAY_ABORT);
}


void
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
    
    /* Chat with greeter */
    while (login == NULL) {
	login = gdm_verify_user (d->name);
	
	if (login == NULL) {
	    gdm_slave_greeter_ctl (GDM_RESET, "");
	    sleep (GdmRetryDelay);
	}
    }

    gdm_slave_session_start();
}


void
gdm_slave_session_start ()
{
    gchar *cfgdir, *sesspath;
    struct stat statbuf;
    struct passwd *pwent;
    gchar *session, *language, *usrsess, *usrlang;
    gboolean savesess = FALSE, savelang = FALSE, usrcfgok = FALSE, authok = FALSE;
    gint i;

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
    usrcfgok = gdm_file_check ("gdm_slave_greeter", pwent->pw_uid, cfgdir, "gdm", 
				TRUE, GdmUserMaxFile, GdmRelaxPerms);
    g_free (cfgdir);

    if (usrcfgok) {
	gchar *cfgstr;

	cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/last", NULL);
	usrsess = gnome_config_get_string (cfgstr);
	g_free (cfgstr);

	cfgstr = g_strconcat ("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
	usrlang = gnome_config_get_string (cfgstr);
	g_free (cfgstr);
    } 
    else {
	usrsess = "";
	usrlang = "";
    }

    setegid (GdmGroupId);
    seteuid (0);
    
    session = gdm_slave_greeter_ctl (GDM_SESS, usrsess);
    language = gdm_slave_greeter_ctl (GDM_LANG, usrlang);
    
    if (strlen (gdm_slave_greeter_ctl (GDM_SSESS, "")))
	savesess = TRUE;
    
    if (strlen (gdm_slave_greeter_ctl (GDM_SLANG, "")))
	savelang = TRUE;
    
    gdm_debug ("gdm_slave_session_start: Authentication completed. Whacking greeter");
    
    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);
    sigprocmask (SIG_BLOCK, &mask, &omask);  
    
    greet = FALSE;

    /* Kill greeter and wait for it to die */
    kill (d->greetpid, SIGINT);
    waitpid (d->greetpid, 0, 0); 
    d->greetpid = 0;
    
    sigprocmask (SIG_SETMASK, &omask, NULL);
    
    if (GdmKillInitClients)
	gdm_slave_windows_kill();
 
    /* Prepare user session */
    gdm_setenv ("DISPLAY", d->name);
    gdm_setenv ("LOGNAME", login);
    gdm_setenv ("USER", login);
    gdm_setenv ("USERNAME", login);
    gdm_setenv ("HOME", pwent->pw_dir);
    gdm_setenv ("GDMSESSION", session);
    gdm_setenv ("SHELL", pwent->pw_shell);
    gdm_setenv ("MAIL", NULL);	/* Unset $MAIL for broken shells */

    /* Special PATH for root */
    if (pwent->pw_uid == 0)
	gdm_setenv ("PATH", GdmRootPath);
    else
	gdm_setenv ("PATH", GdmDefaultPath);

    /* Set locale */
    if (!strcasecmp (language, "english"))
	gdm_setenv ("LANG", "C");
    else
	gdm_setenv ("LANG", language);
    
    /* If script fails reset X server and restart greeter */
    if (gdm_slave_exec_script (d, GdmPreSession) != EXIT_SUCCESS) 
	gdm_slave_exit (DISPLAY_REMANAGE,
			_("gdm_slave_session_start: Execution of PreSession script returned > 0. Aborting."));

    /* Setup cookie -- We need this information during cleanup, thus
     * cookie handling is done before fork()ing */

    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    authok = gdm_auth_user_add (d, pwent->pw_uid, pwent->pw_dir);

    setegid (GdmGroupId);
    seteuid (0);
    
    if (!authok) {
	gdm_slave_session_stop();
	gdm_slave_session_cleanup();
	
	exit (DISPLAY_REMANAGE);
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
	
	execl (sesspath, NULL);
	
	gdm_error (_("gdm_slave_session_start: Could not start session `%s'"), sesspath);
	
	gdm_slave_session_stop();
	gdm_slave_session_cleanup();
	
	exit (DISPLAY_REMANAGE);
	
    default:
	break;
    }
    
    /* Wait for the user's session to exit */
    waitpid (d->sesspid, 0, 0);

    gdm_slave_session_stop();
    gdm_slave_session_cleanup();
}


void
gdm_slave_session_stop ()
{
    struct passwd *pwent;

    gdm_debug ("gdm_slave_session_stop: %s on %s", login, d->name);
    
    kill (- (d->sesspid), SIGTERM);
    
    gdm_verify_cleanup();
    
    pwent = getpwnam (login);	/* PAM overwrites our pwent */

    if (!pwent)
	return;
    
    /* Remove display from ~user/.Xauthority */
    setegid (pwent->pw_gid);
    seteuid (pwent->pw_uid);

    gdm_auth_user_remove (d, pwent->pw_uid);
    
    setegid (GdmGroupId);
    seteuid (0);
}


void
gdm_slave_session_cleanup (void)
{
    gdm_debug ("gdm_slave_session_cleanup: %s on %s", login, d->name);
    
    /* Execute post session script */
    gdm_debug ("gdm_slave_session_cleanup: Running post session script");
    gdm_slave_exec_script (d, GdmPostSession);
	
    if (d->dsp && gdm_slave_xsync_ping()) {
	
	/* Cleanup */
	gdm_debug ("gdm_slave_session_cleanup: Killing windows");
	gdm_slave_windows_kill();
	
	XCloseDisplay (d->dsp);
    }
    
    exit (DISPLAY_REMANAGE);
}


static void
gdm_slave_term_handler (int sig)
{
    gdm_debug ("gdm_slave_term_handler: %s got TERM signal", d->name);
    
    if (d->greetpid) {
	gdm_debug ("gdm_slave_term_handler: Whacking greeter");
	kill (d->greetpid, SIGINT);
	waitpid (d->greetpid, 0, 0); 
	d->greetpid = 0;
    } 
    else if (login)
	gdm_slave_session_stop();
    
    gdm_debug ("gdm_slave_term_handler: Whacking client connections");
    gdm_slave_windows_kill();
    XCloseDisplay (d->dsp);

    exit (DISPLAY_ABORT);
}


/* Called on every SIGCHLD */
static void 
gdm_slave_child_handler (int sig)
{
    gint status;
    pid_t pid;
    
    while ((pid = waitpid (-1, &status, WNOHANG)) > 0) {
	gdm_debug ("gdm_slave_child_handler: %d died", pid);
	
	if (WIFEXITED (status))
	    gdm_debug ("gdm_slave_child_handler: %d returned %d", pid, WEXITSTATUS (status));
	
	if (pid == d->greetpid && greet)
	    if (WIFEXITED (status))
		exit (WEXITSTATUS (status));
	    else {
		if (d && d->dsp)
		    XCloseDisplay (d->dsp);
		
		exit (DISPLAY_REMANAGE);
	    }
	
	if (pid && pid == d->sesspid) {
	    gdm_slave_session_stop();
	    gdm_slave_session_cleanup();
	}
    }
}


/* Only kills clients, not connections. This keeps the server alive */
static void
gdm_slave_windows_kill (void)
{
    Window root, parent, *children;
    gint child, screen, nchildren;
    Display *disp = NULL;
    
    disp=XOpenDisplay (d->name);
    
    if (!disp) {
	gdm_debug ("gdm_slave_windows_kill: Could not open display %s", d->name);
	return;
    }
    
    gdm_debug ("gdm_slave_windows_kill: Killing windows on %s", d->name);
    
    gdm_setenv ("XAUTHORITY", d->authfile);
    gdm_setenv ("DISPLAY", d->name);
    
    for (screen = 0 ; screen < ScreenCount (disp) ; screen++) {
	
	nchildren = 0;
	
	while (XQueryTree (disp, RootWindow (disp, screen), &root, &parent,
			   &children, &nchildren) && nchildren>0) {
	    
	    for (child = 0 ; child < nchildren ; child++) {
		gdm_debug ("gdm_slave_windows_kill: Killing child 0x%x", children[child]);
		XKillClient (disp, children[child]);
	    }
	    
	    XFree (children);
	}
	
    }
    
    XSync (disp, FALSE);
}


/* Minor X faults */
static gint
gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt)
{
    gdm_debug ("gdm_slave_windows_kill_error_handler: X error - display doesn't respond");
    pingack = FALSE;
    return (TRUE);
}


/* We respond to fatal errors by restarting the display */
static gint
gdm_slave_xioerror_handler (Display *disp)
{
    gdm_debug ("gdm_slave_xioerror_handler: I/O error for display %s", d->name);
    
    if (login)
	gdm_slave_session_stop();
    
    gdm_error (_("gdm_slave_windows_kill_ioerror_handler: Fatal X error - Restarting %s"), d->name);
    
    exit (DISPLAY_REMANAGE);
}


static void
gdm_slave_xsync_handler (int sig)
{
    gdm_debug ("gdm_slave_xsync_handler: Xping failed for display %s", d->name);
    pingack = FALSE;
}


static gboolean
gdm_slave_xsync_ping (void)
{
    struct sigaction sigalarm;
    sigset_t mask, omask;
    
    gdm_debug ("gdm_slave_xsync_ping: Pinging %s", d->name);
    
    pingack = TRUE;
    
    XSetErrorHandler (gdm_slave_xerror_handler);
    XSetIOErrorHandler (gdm_slave_xioerror_handler);
    
    sigalarm.sa_handler = gdm_slave_xsync_handler;
    sigalarm.sa_flags = 0;
    sigemptyset (&sigalarm.sa_mask);
    
    if (sigaction (SIGALRM, &sigalarm, NULL) < 0)
	gdm_slave_exit (DISPLAY_ABORT, _("gdm_slave_xsync_ping: Error setting up ALARM signal handler"));
    
    sigemptyset (&mask);
    sigaddset (&mask, SIGALRM);
    sigprocmask (SIG_UNBLOCK, &mask, &omask);
    
    alarm (10);
    
    XSync (d->dsp, TRUE);
    
    alarm (0);
    sigprocmask (SIG_SETMASK, &omask, NULL);
    
    gdm_debug ("gdm_slave_xsync_ping: %s returned %d", d->name, pingack);
    
    return (pingack);
}


gchar * 
gdm_slave_greeter_ctl (gchar cmd, gchar *str)
{
    gchar buf[FIELD_SIZE];
    guchar c;

    if (str)
	g_print ("%c%c%s\n", STX, cmd, str);
    else
	g_print ("%c%c\n", STX, cmd);

    /* Skip random junk that might have accumulated */
    do { c = getc (greeter); } while (c && c != STX);
    
    fgets (buf, FIELD_SIZE-1, greeter);
    
    if (strlen (buf)) {
	buf[strlen (buf)-1] = '\0';
	return (g_strndup (buf, strlen (buf)));
    }
    else
	return (NULL);
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

    /* Kill children where applicable */
    if (d->greetpid)
	kill (SIGTERM, d->greetpid);

    if (d->chooserpid)
	kill (SIGTERM, d->chooserpid);

    if (d->sesspid)
	kill (SIGTERM, d->sesspid);

    if (d->servpid)
	kill (SIGTERM, d->servpid);

    exit (status);
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
