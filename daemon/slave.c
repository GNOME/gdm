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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* This is the gdm slave process. gdmslave runs the chooser, greeter
 * and the user's session scripts. 
 */

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

#include "gdm.h"

static const gchar RCSid[]="$Id$";

extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmSessDir;
extern gchar *GdmGreeter;
extern gchar *GdmDisplayInit;
extern gchar *GdmPreSession;
extern gchar *GdmPostSession;
extern gchar *GdmSuspend;
extern gchar *GdmDefaultPath;
extern gint  GdmKillInitClients;
extern gint  GdmRetryDelay;
extern sigset_t sysmask;

extern gboolean gdm_file_check(gchar *caller, uid_t user, gchar *dir, gchar *file, gboolean absentok);
extern gchar **gdm_arg_munch(const gchar *p);
extern gint gdm_exec_script(GdmDisplay*, gchar *dir);
extern void gdm_abort(const char*, ...);
extern void gdm_error(const char*, ...);
extern void gdm_debug(const char*, ...);
extern void gdm_remanage(const char*, ...);
extern void gdm_verify_cleanup (void);
extern void gdm_auth_user_add(GdmDisplay *d, gchar *home);
extern void gdm_auth_user_remove(GdmDisplay *d, gchar *home);
extern void gdm_exec_command(gchar *cmd);
extern void gdm_putenv(gchar *s);
extern gchar *gdm_verify_user (gchar *display);

void        gdm_slave_start(GdmDisplay *d);
static gint gdm_slave_xerror_handler(Display *disp, XErrorEvent *evt);
static gint gdm_slave_xioerror_handler(Display *disp);
static void gdm_slave_greeter(void);
static void gdm_slave_session_start(gchar *login, gchar *session, gboolean savesess, gchar *lang, gboolean savelang);
static void gdm_slave_session_stop(void);
static void gdm_slave_session_cleanup(void);
static void gdm_slave_term_handler(int sig);
static void gdm_slave_child_handler(int sig);
static void gdm_slave_windows_kill(void);
static void gdm_slave_xsync_handler(int sig);
static gboolean gdm_slave_xsync_ping(void);
gchar       *gdm_slave_greeter_ctl(gchar cmd, gchar *str);


GdmDisplay *d;
struct passwd *pwent;
sigset_t mask, omask;
gboolean pingack;
gboolean greet=TRUE;
FILE *greeter;


void 
gdm_slave_start(GdmDisplay *display)
{    
    struct sigaction term, child;
    gint openretries=0;

    gdm_debug("gdm_slave_start: Starting slave process for %s", display->name);

    d=display;

    gdm_putenv(g_strconcat("XAUTHORITY=", d->auth, NULL));
    gdm_putenv(g_strconcat("DISPLAY=", d->name, NULL));

    /* Handle a INT/TERM signals from gdm master */
    term.sa_handler = gdm_slave_term_handler;
    term.sa_flags = SA_RESTART;
    sigemptyset(&term.sa_mask);
    sigaddset(&term.sa_mask, SIGTERM);
    sigaddset(&term.sa_mask, SIGINT);
    
    if((sigaction(SIGTERM, &term, NULL) < 0) || (sigaction(SIGINT, &term, NULL) < 0))
	gdm_abort(_("gdm_slave_init: Error setting up TERM/INT signal handler"));

    /* Child handler. Keeps an eye on greeter/session */
    child.sa_handler = gdm_slave_child_handler;
    child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset(&child.sa_mask);
    
    if(sigaction(SIGCHLD, &child, NULL) < 0) 
	gdm_abort(_("gdm_slave_init: Error setting up CHLD signal handler"));

    /* The signals we wish to listen to */
    sigfillset(&mask);
    sigdelset(&mask, SIGINT);
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGCHLD);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    /* X error handlers to avoid the default ones: exit(1) */
    XSetErrorHandler(gdm_slave_xerror_handler);
    XSetIOErrorHandler(gdm_slave_xioerror_handler);

    /* We keep our own (windowless) connection (dsp) open to avoid the
     * X server resetting due to lack of active connections. */

    gdm_debug("gdm_slave_start: Opening display %s", d->name);
    d->dsp=NULL;

    while(openretries < 10 && d->dsp==NULL) {
	d->dsp=XOpenDisplay(d->name);

	if(!d->dsp) {
	    gdm_debug("gdm_slave_start: Sleeping %d on a retry", openretries*2);
	    sleep(openretries*2);
	    openretries++;
	}
    }
    
    if(d->dsp) 
	gdm_slave_greeter();    /* Start the greeter */
    else
	exit(DISPLAY_ABORT);
}


static void
gdm_slave_greeter(void)
{
    gint pipe1[2], pipe2[2];    
    gchar *login=NULL, *session, *language;
    gboolean savesess=FALSE, savelang=FALSE;
    gchar **argv;
    
    gdm_debug("gdm_slave_greeter: Running greeter on %s", d->name);

    /* Run the init script. gdmslave suspends until script has terminated */
    gdm_exec_script(d, GdmDisplayInit);

    /* Open a pipe for greeter communications */
    if(pipe(pipe1) < 0 || pipe(pipe2) < 0) 
	gdm_abort(_("gdm_slave_greeter: Can't init pipe to gdmgreeter"));

    greet=TRUE;
    
    /* Fork. Parent is gdmslave, child is greeter process. */
    switch(d->greetpid=fork()) {
	
    case 0:
	sigfillset(&mask);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTERM);
	sigdelset(&mask, SIGHUP);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	/* Plumbing */
	close(pipe1[1]);
	close(pipe2[0]);

	if(pipe1[0] != STDIN_FILENO) 
	    dup2(pipe1[0], STDIN_FILENO);

	if(pipe2[1] != STDOUT_FILENO) 
	    dup2(pipe2[1], STDOUT_FILENO);

        if(setgid(GdmGroupId) < 0) 
	    gdm_abort(_("gdm_slave_greeter: Couldn't set groupid to %d"), GdmGroupId);

        if(setuid(GdmUserId) < 0) 
	    gdm_abort(_("gdm_slave_greeter: Couldn't set userid to %d"), GdmUserId);

	gdm_putenv(g_strconcat("XAUTHORITY=", d->auth, NULL));
	gdm_putenv(g_strconcat("DISPLAY=", d->name, NULL));
	gdm_putenv(g_strconcat("HOME=/", NULL)); /* Hack */
	gdm_putenv(g_strconcat("PATH=", GdmDefaultPath, NULL));

	argv=gdm_arg_munch(GdmGreeter);
	execv(argv[0], argv);

	gdm_abort(_("gdm_slave_greeter: Error starting greeter on display %s"), d->name);
	
    case -1:
	gdm_abort(_("gdm_slave_greeter: Can't fork gdmgreeter process"));
	
    default:
	close(pipe1[0]);
	close(pipe2[1]);
	
	if(pipe1[1] != STDOUT_FILENO) 
	    dup2(pipe1[1], STDOUT_FILENO);
	
	if(pipe2[0] != STDIN_FILENO) 
	    dup2(pipe2[0], STDIN_FILENO);
	
	greeter=fdopen(STDIN_FILENO, "r");

	gdm_debug("gdm_slave_greeter: Greeter on pid %d", d->greetpid);
	break;
    }

    /* Chat with greeter */
    while(login==NULL) {
	login=gdm_verify_user(d->name);

	if(login==NULL) {
	    gdm_slave_greeter_ctl(GDM_RESET, "");
	    sleep(GdmRetryDelay);
	}
    }

    session= gdm_slave_greeter_ctl(GDM_SESS, "");
    language=gdm_slave_greeter_ctl(GDM_LANG, "");

    if(strlen(gdm_slave_greeter_ctl(GDM_SSESS, "")))
	savesess=TRUE;

    if(strlen(gdm_slave_greeter_ctl(GDM_SLANG, "")))
	savelang=TRUE;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &omask);    

    greet=FALSE;

    /* Kill greeter and wait for it to die */
    kill(d->greetpid, SIGINT);
    waitpid(d->greetpid, 0, 0); 
    d->greetpid=0;

    sigprocmask(SIG_UNBLOCK, &omask, NULL);
    
    if(GdmKillInitClients)
	gdm_slave_windows_kill();
    
    gdm_slave_session_start(login, session, savesess, language, savelang);
}


static void 
gdm_slave_session_start(gchar *login, gchar *session, gboolean savesess, gchar *lang, gboolean savelang)
{
    gchar *sessdir, *cfgdir, *cfgstr;
    struct stat statbuf;

    gdm_debug("gdm_slave_session_start: %s on %s", login, d->name);

    pwent=getpwnam(login);

    /* If the user doesn't exist, reset the X server and restart greeter */
    if(!pwent) 
	gdm_remanage(_("gdm_slave_session_init: User '%s' not found. Aborting."), login);

    gdm_putenv(g_strconcat("XAUTHORITY=", pwent->pw_dir, "/.Xauthority", NULL));
    gdm_putenv(g_strconcat("DISPLAY=", d->name, NULL));
    gdm_putenv(g_strconcat("LOGNAME=", login, NULL));
    gdm_putenv(g_strconcat("USER=", login, NULL));
    gdm_putenv(g_strconcat("USERNAME=", login, NULL));
    gdm_putenv(g_strconcat("HOME=", pwent->pw_dir, NULL));
    gdm_putenv(g_strconcat("GDMSESSION=", session, NULL));
    gdm_putenv(g_strconcat("SHELL=", pwent->pw_shell, NULL));
    gdm_putenv(g_strconcat("PATH=", GdmDefaultPath, NULL));

    if(!strcasecmp(lang, "english"))
	putenv("LANG=C");
    else
	gdm_putenv(g_strconcat("LANG=", lang, NULL));

    /* If script fails reset X server and restart greeter */
    if(gdm_exec_script(d, GdmPreSession) != EXIT_SUCCESS) 
	gdm_remanage(_("gdm_slave_session_init: Execution of PreSession script returned > 0. Aborting."));

    switch(d->sesspid=fork()) {
	
    case -1:
	gdm_abort(_("gdm_slave_session_init: Error forking user session"));
	
    case 0:
	setpgid(0, 0);

	umask(022);

	if(setgid(pwent->pw_gid) < 0) 
	    gdm_remanage(_("gdm_slave_session_init: Could not setgid %d. Aborting."), pwent->pw_gid);

	if(initgroups(login, pwent->pw_gid) < 0)
	    gdm_remanage(_("gdm_slave_session_init: initgroups() failed for %s. Aborting."), login);

	if(setuid(pwent->pw_uid) < 0) 
	    gdm_remanage(_("gdm_slave_session_init: Could not become %s. Aborting."), login);

	chdir(pwent->pw_dir);

	/* Run sanity check on ~user/.Xauthority and setup cookie */
	if(!gdm_file_check("gdm_slave_session_init", pwent->pw_uid, pwent->pw_dir, ".Xauthority", TRUE)) {
	    XCloseDisplay(d->dsp);
	    exit(DISPLAY_REMANAGE);
	}

	gdm_auth_user_add(d, pwent->pw_dir);

	/* Check if ~user/.gnome exists. Create it otherwise. */
	cfgdir=g_strconcat(pwent->pw_dir, "/.gnome", NULL);

	if(stat(cfgdir, &statbuf) == -1) { /* FIXME: Maybe I need to be a bit paranoid here! */
	    mkdir(cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	    chmod(cfgdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	}
	    
	/* Sanity check on ~user/.gnome/gdm */
	if(!gdm_file_check("gdm_slave_session_init", pwent->pw_uid, cfgdir, "gdm", TRUE)) {
	    gdm_slave_session_cleanup();
	    XCloseDisplay(d->dsp);
	    exit(DISPLAY_REMANAGE);
	}

	g_free(cfgdir);

	if(savesess) {
	    /* libgnome sets home to ~root, so we have to write the path ourselves */
	    cfgstr = g_strconcat("=", pwent->pw_dir, "/.gnome/gdm=/session/last", NULL);
	    gnome_config_set_string(cfgstr, session);
	    gnome_config_sync();
	    g_free(cfgstr);
	}

	if(savelang) {
	    cfgstr = g_strconcat("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
	    gnome_config_set_string(cfgstr, lang);
	    gnome_config_sync();
	    g_free(cfgstr);
	}

	sessdir = g_strconcat(GdmSessDir, "/", session, NULL);

	gdm_debug("Running %s for %s on %s", sessdir, login, d->name);

	/* Restore system inherited sigmask */
	sigprocmask(SIG_SETMASK, &sysmask, NULL);

	execl(sessdir, NULL);
	
	gdm_error(_("gdm_slave_session_init: Could not start session `%s'"), sessdir);

	gdm_slave_session_stop();
	gdm_slave_session_cleanup();

	exit(DISPLAY_REMANAGE);
	
    default:
	break;
    }

    waitpid(d->sesspid, 0, 0);

    gdm_slave_session_stop();
    gdm_slave_session_cleanup();
}


static void
gdm_slave_session_stop(void)
{
    gdm_debug("gdm_slave_session_stop: %s on %s", pwent->pw_name, d->name);

    kill(-(d->sesspid), SIGTERM);

    gdm_verify_cleanup();

    /* Remove display from ~user/.Xauthority */
    setegid(pwent->pw_gid);
    seteuid(pwent->pw_uid);

    if(gdm_file_check("gdm_slave_session_stop", pwent->pw_uid, pwent->pw_dir, ".Xauthority", FALSE)) 
	gdm_auth_user_remove(d, pwent->pw_dir);

    setegid(GdmGroupId);
    seteuid(0);
}


/* Only executed if display is still alive */
static void
gdm_slave_session_cleanup(void)
{
    gdm_debug("gdm_slave_session_cleanup: %s on %s", pwent->pw_name, d->name);

    if(d->dsp && gdm_slave_xsync_ping()) {

	/* Execute post session script */
	gdm_debug("gdm_slave_session_cleanup: Running post session script");
	gdm_exec_script(d, GdmPostSession);

	/* Cleanup */
	gdm_debug("gdm_slave_session_cleanup: Killing windows");
	gdm_slave_windows_kill();

	XCloseDisplay(d->dsp);
    }

    exit(DISPLAY_REMANAGE);
}


static void
gdm_slave_term_handler(int sig)
{
    gdm_debug("gdm_slave_term_handler: %s got TERM signal", d->name);

    if(d->greetpid) {
	gdm_debug("gdm_slave_term_handler: Whacking greeter");
	kill(d->greetpid, SIGINT);
	waitpid(d->greetpid, 0, 0); 
	d->greetpid=0;
    } 
    else if(pwent)
	gdm_slave_session_stop();

    gdm_debug("gdm_slave_term_handler: Whacking client connections");
    gdm_slave_windows_kill();
    XCloseDisplay(d->dsp);
    exit(DISPLAY_ABORT);
}


/* Called on every SIGCHLD */
static void 
gdm_slave_child_handler(int sig)
{
    gint status;
    pid_t pid;

    while((pid=waitpid(-1, &status, WNOHANG)) > 0) {
	gdm_debug("gdm_slave_child_handler: %d died", pid);

	if(WIFEXITED(status))
	    gdm_debug("gdm_slave_child_handler: %d returned %d", pid, WEXITSTATUS(status));

	if(pid==d->greetpid && greet)
	    if(WIFEXITED(status))
		exit(WEXITSTATUS(status));
	    else {
		if(d && d->dsp)
		    XCloseDisplay(d->dsp);

		exit(DISPLAY_REMANAGE);
	    }
	
	if(pid==d->sesspid) {
	    gdm_slave_session_stop();
	    gdm_slave_session_cleanup();
	}
    }
}


/* Only kills clients, not connections. This keeps the server alive */
static void
gdm_slave_windows_kill(void)
{
    Window root, parent, *children;
    gint child, screen, nchildren;
    Display *disp=NULL;

    disp=XOpenDisplay(d->name);

    if(!disp) {
	gdm_debug("gdm_slave_windows_kill: Could not open display %s", d->name);
	return;
    }

    gdm_debug("gdm_slave_windows_kill: Killing windows on %s", d->name);

    gdm_putenv(g_strconcat("XAUTHORITY=", d->auth, NULL));
    gdm_putenv(g_strconcat("DISPLAY=", d->name, NULL));

    for(screen=0 ; screen<ScreenCount(disp) ; screen++) {

	nchildren=0;

	while(XQueryTree(disp, RootWindow(disp, screen), &root, &parent,
			 &children, &nchildren) && nchildren>0) {
	    
	    for(child=0 ; child<nchildren ; child++) {
		gdm_debug("gdm_slave_windows_kill: Killing child 0x%x", children[child]);
		XKillClient(disp, children[child]);
	    }
	    
	    XFree(children);
	}

    }

    XSync(disp, FALSE);
}
 

/* Minor X faults */
static gint
gdm_slave_xerror_handler(Display *disp, XErrorEvent *evt)
{
    gdm_debug("gdm_slave_windows_kill_error_handler: Blam");
    pingack=FALSE;
    return(TRUE);
}


/* We respond to fatal errors by restarting the display */
static gint
gdm_slave_xioerror_handler(Display *disp)
{
    gdm_debug("gdm_slave_xioerror_handler: I/O error for display %s", d->name);

    if(pwent)
	gdm_slave_session_stop();

    gdm_error(_("gdm_slave_windows_kill_ioerror_handler: Fatal X error - Restarting %s"), d->name);

    exit(DISPLAY_RESERVER);
}


static void
gdm_slave_xsync_handler(int sig)
{
    gdm_debug("gdm_slave_xsync_handler: Xping failed for display %s", d->name);
    pingack=FALSE;
}


static gboolean
gdm_slave_xsync_ping(void)
{
    struct sigaction sigalarm;
    sigset_t mask, omask;

    gdm_debug("gdm_slave_xsync_ping: Pinging %s", d->name);

    pingack=TRUE;

    XSetErrorHandler(gdm_slave_xerror_handler);
    XSetIOErrorHandler(gdm_slave_xioerror_handler);

    sigalarm.sa_handler = gdm_slave_xsync_handler;
    sigalarm.sa_flags = 0;
    sigemptyset(&sigalarm.sa_mask);
    
    if(sigaction(SIGALRM, &sigalarm, NULL) < 0)
	gdm_abort(_("gdm_slave_xsync_ping: Error setting up ALARM signal handler"));

    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &mask, &omask);

    alarm(10);

    XSync(d->dsp, TRUE);

    alarm(0);
    sigprocmask(SIG_SETMASK, &omask, NULL);

    gdm_debug("gdm_slave_xsync_ping: %s returned %d", d->name, pingack);

    return(pingack);
}


gchar * 
gdm_slave_greeter_ctl(gchar cmd, gchar *str)
{
    gchar buf[FIELD_SIZE];

    g_print("%c%s\n", cmd, str);

    fgets(buf, FIELD_SIZE-1, greeter);

    if(strlen(buf)) {
	buf[strlen(buf)-1]='\0';
	return(g_strndup(buf, strlen(buf)));
    }
    else
	return(NULL);
}

/* EOF */
