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
#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
#include <sched.h>
#endif
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
#ifdef HAVE_XFREE_XINERAMA
#include <X11/extensions/Xinerama.h>
#elif HAVE_SOLARIS_XINERAMA
#include <X11/extensions/xinerama.h>
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
#include "getvt.h"
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
static gboolean gdm_wait_for_ack = TRUE; /* wait for ack on all messages to
				      * the daemon */
static int in_session_stop = 0;
static int in_usr2_signal = 0;
static gboolean need_to_quit_after_session_stop = FALSE;
static int exit_code_to_use = DISPLAY_REMANAGE;
static gboolean session_started = FALSE;
static gboolean greeter_disabled = FALSE;
static gboolean greeter_no_focus = FALSE;

static gboolean interrupted = FALSE;
static gchar *ParsedAutomaticLogin = NULL;
static gchar *ParsedTimedLogin = NULL;

static int greeter_fd_out = -1;
static int greeter_fd_in = -1;

typedef struct {
	int fd_r;
	int fd_w;
	pid_t pid;
} GdmWaitPid;

static GSList *slave_waitpids = NULL;

extern gboolean gdm_first_login;
extern gboolean gdm_emergency_server;
extern pid_t extra_process;
extern int extra_status;
extern int gdm_in_signal;
extern int gdm_normal_runlevel;

extern int slave_fifo_pipe_fd; /* the slavepipe (like fifo) connection, this is the write end */

/* Configuration option variables */
extern gchar *GdmUser;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;
extern gchar *GdmSessDir;
extern gchar *GdmXsession;
extern gchar *GdmDefaultSession;
extern gchar *GdmLocaleFile;
extern gchar *GdmAutomaticLogin;
extern gboolean GdmAllowRemoteAutoLogin;
extern gboolean GdmAlwaysRestartServer;
extern gboolean GdmAddGtkModules;
extern gboolean GdmDoubleLoginWarning;
extern gchar *GdmConfigurator;
extern gboolean GdmConfigAvailable;
extern gboolean GdmChooserButton;
extern gboolean GdmSystemMenu;
extern gint GdmXineramaScreen;
extern gchar *GdmGreeter;
extern gchar *GdmRemoteGreeter;
extern gchar *GdmGtkModulesList;
extern gchar *GdmChooser;
extern gchar *GdmDisplayInit;
extern gchar *GdmPostLogin;
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
extern gint GdmRelaxPerms;
extern gboolean GdmKillInitClients;
extern gint GdmPingInterval;
extern gint GdmRetryDelay;
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmGlobalFaceDir;
extern gboolean GdmDebug;
extern gboolean GdmDisallowTCP;


/* Local prototypes */
static gint     gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt);
static gint     gdm_slave_xioerror_handler (Display *disp);
static void	gdm_slave_run (GdmDisplay *display);
static void	gdm_slave_wait_for_login (void);
static void     gdm_slave_greeter (void);
static void     gdm_slave_chooser (void);
static void     gdm_slave_session_start (void);
static void     gdm_slave_session_stop (gboolean run_post_session,
					gboolean no_shutdown_check);
static void     gdm_slave_alrm_handler (int sig);
static void     gdm_slave_term_handler (int sig);
static void     gdm_slave_usr2_handler (int sig);
static void     gdm_slave_quick_exit (gint status);
static void     gdm_slave_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static void     gdm_child_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static gint     gdm_slave_exec_script (GdmDisplay *d, const gchar *dir,
				       const char *login, struct passwd *pwent,
				       gboolean pass_stdout, 
				       gboolean set_parent);
static gchar *  gdm_parse_enriched_login (const gchar *s, GdmDisplay *display);
static void	gdm_slave_handle_usr2_message (void);
static void	gdm_slave_handle_notify (const char *msg);
static void	create_temp_auth_file (void);
static void	set_xnest_parent_stuff (void);
static void	check_notifies_now (void);
static void	restart_the_greeter (void);

/* Yay thread unsafety */
static gboolean x_error_occured = FALSE;
static gboolean gdm_got_ack = FALSE;
static char * gdm_ack_response = NULL;
static GList *unhandled_notifies = NULL;


/* for signals that want to exit */
static Jmp_buf slave_start_jmp;
static gboolean return_to_slave_start_jmp = FALSE;
static gboolean already_in_slave_start_jmp = FALSE;
static char *slave_start_jmp_error_to_print = NULL;
enum {
	JMP_FIRST_RUN = 0,
	JMP_SESSION_STOP_AND_QUIT = 1,
	JMP_JUST_QUIT_QUICKLY = 2
};
#define SIGNAL_EXIT_WITH_JMP(d,how) \
   {											\
	if ((d)->slavepid == getpid () && return_to_slave_start_jmp) {			\
		already_in_slave_start_jmp = TRUE;					\
		Longjmp (slave_start_jmp, how);						\
	} else {									\
		/* evil! how this this happen */					\
		if (slave_start_jmp_error_to_print != NULL)				\
			gdm_error (slave_start_jmp_error_to_print);			\
		gdm_error ("Bad (very very VERY bad!) things happening in signal");	\
		_exit (DISPLAY_REMANAGE);						\
	}										\
   }

/* notify all waitpids, make waitpids check notifies */
static void
slave_waitpid_notify_all (void)
{
	GSList *li;

	if (slave_waitpids == NULL)
		return;

	gdm_sigchld_block_push ();

	for (li = slave_waitpids; li != NULL; li = li->next) {
		GdmWaitPid *wp = li->data;
		if (wp->fd_w >= 0) {
			IGNORE_EINTR (write (wp->fd_w, "N", 1));
		}
	}

	gdm_sigchld_block_pop ();
}

/* make sure to wrap this call with sigchld blocks */
static GdmWaitPid *
slave_waitpid_setpid (pid_t pid)
{
	int p[2];
	GdmWaitPid *wp;

	if (pid <= 1)
		return NULL;

	wp = g_new0 (GdmWaitPid, 1);
	wp->pid = pid;

	if (pipe (p) < 0) {
		gdm_error ("slave_waitpid_setpid: cannot create pipe, trying to wing it");

		wp->fd_r = -1;
		wp->fd_w = -1;
	} else {
		wp->fd_r = p[0];
		wp->fd_w = p[1];
	}

	slave_waitpids = g_slist_prepend (slave_waitpids, wp);
	return wp;
}

/* must call slave_waitpid_setpid before calling this */
static void
slave_waitpid (GdmWaitPid *wp)
{
	if (wp == NULL)
		return;

	gdm_debug ("slave_waitpid: waiting on %d", (int)wp->pid);

	if (wp->fd_r < 0) {
		gdm_error ("slave_waitpid: no pipe, trying to wing it");

		/* This is a real stupid fallback for a real stupid case */
		while (wp->pid > 1) {
			struct timeval tv;
			/* Wait 5 seconds. */
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			select (0, NULL, NULL, NULL, &tv);
			/* don't want to use sleep since we're using alarm
			   for pinging */
			check_notifies_now ();
		}
		check_notifies_now ();
	} else {
		do {
			char buf[1];
			fd_set rfds;
			int ret;

			FD_ZERO (&rfds);
			FD_SET (wp->fd_r, &rfds);

			ret = select (wp->fd_r+1, &rfds, NULL, NULL, NULL);
			if (ret == 1)
				IGNORE_EINTR (read (wp->fd_r, buf, 1));
			check_notifies_now ();
		} while (wp->pid > 1);
		check_notifies_now ();
	}

	gdm_sigchld_block_push ();

	IGNORE_EINTR (close (wp->fd_r));
	IGNORE_EINTR (close (wp->fd_w));
	wp->fd_r = -1;
	wp->fd_w = -1;
	wp->pid = -1;

	slave_waitpids = g_slist_remove (slave_waitpids, wp);
	g_free (wp);

	gdm_sigchld_block_pop ();

	gdm_debug ("slave_waitpid: done_waiting");
}

static void
check_notifies_now (void)
{
	GList *list, *li;

	if (restart_greeter_now &&
	    do_restart_greeter) {
		do_restart_greeter = FALSE;
		restart_the_greeter ();
	}

	while (unhandled_notifies != NULL) {
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

	if (restart_greeter_now &&
	    do_restart_greeter) {
		do_restart_greeter = FALSE;
		restart_the_greeter ();
	}
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
		IGNORE_EINTR (close (greeter_fd_out));
	greeter_fd_out = -1;
	if (greeter_fd_in > 0)
		IGNORE_EINTR (close (greeter_fd_in));
	greeter_fd_in = -1;
}

static void
term_session_stop_and_quit (void)
{
	gdm_in_signal = 0;
	already_in_slave_start_jmp = TRUE;
	gdm_wait_for_ack = FALSE;
	need_to_quit_after_session_stop = TRUE;

	if (slave_start_jmp_error_to_print != NULL)
		gdm_error (slave_start_jmp_error_to_print);
	slave_start_jmp_error_to_print = NULL;

	/* only if we're not hanging in session stop and getting a
	   TERM signal again */
	if (in_session_stop == 0 && session_started)
		gdm_slave_session_stop (d->logged_in && login != NULL,
					TRUE /* no_shutdown_check */);

	gdm_debug ("term_session_stop_and_quit: Final cleanup");

	/* Well now we're just going to kill
	 * everything including the X server,
	 * so no need doing XCloseDisplay which
	 * may just get us an XIOError */
	d->dsp = NULL;

	gdm_slave_quick_exit (exit_code_to_use);
}

static void
term_quit (void)
{
	gdm_in_signal = 0;
	already_in_slave_start_jmp = TRUE;
	gdm_wait_for_ack = FALSE;
	need_to_quit_after_session_stop = TRUE;

	if (slave_start_jmp_error_to_print != NULL)
		gdm_error (slave_start_jmp_error_to_print);
	slave_start_jmp_error_to_print = NULL;

	gdm_debug ("term_quit: Final cleanup");

	/* Well now we're just going to kill
	 * everything including the X server,
	 * so no need doing XCloseDisplay which
	 * may just get us an XIOError */
	d->dsp = NULL;

	gdm_slave_quick_exit (exit_code_to_use);
}

void 
gdm_slave_start (GdmDisplay *display)
{  
	time_t first_time;
	int death_count;
	struct sigaction alrm, term, child, usr2;
	sigset_t mask;

	/* Ignore SIGUSR1/SIGPIPE, and especially ignore it
	   before the Setjmp */
	gdm_signal_ignore (SIGUSR1);
	gdm_signal_ignore (SIGPIPE);

	/* ignore power failures, up to user processes to
	 * handle things correctly */
#ifdef SIGPWR
	gdm_signal_ignore (SIGPWR);
#endif

	/* The signals we wish to listen to */
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigaddset (&mask, SIGTERM);
	sigaddset (&mask, SIGCHLD);
	sigaddset (&mask, SIGUSR2);
	sigaddset (&mask, SIGUSR1); /* normally we ignore USR1 */
	if (display->type == TYPE_XDMCP &&
	    GdmPingInterval > 0) {
		sigaddset (&mask, SIGALRM);
	}
	/* must set signal mask before the Setjmp as it will be
	   restored, and we're only interested in catching the above signals */
	sigprocmask (SIG_UNBLOCK, &mask, NULL);


	if (display == NULL) {
		/* saaay ... what? */
		_exit (DISPLAY_REMANAGE);
	}

	gdm_debug ("gdm_slave_start: Starting slave process for %s", display->name);

	switch (Setjmp (slave_start_jmp)) {
	case JMP_FIRST_RUN:
		return_to_slave_start_jmp = TRUE;
		break;
	case JMP_SESSION_STOP_AND_QUIT:
		term_session_stop_and_quit ();
		/* huh? should never get here */
		_exit (DISPLAY_REMANAGE);
	default:
	case JMP_JUST_QUIT_QUICKLY:
		term_quit ();
		/* huh? should never get here */
		_exit (DISPLAY_REMANAGE);
	}

	if (display->type == TYPE_XDMCP &&
	    GdmPingInterval > 0) {
		/* Handle a ALRM signals from our ping alarms */
		alrm.sa_handler = gdm_slave_alrm_handler;
		alrm.sa_flags = SA_RESTART | SA_NODEFER;
		sigemptyset (&alrm.sa_mask);
		sigaddset (&alrm.sa_mask, SIGALRM);

		if (sigaction (SIGALRM, &alrm, NULL) < 0)
			gdm_slave_exit (DISPLAY_ABORT,
					_("%s: Error setting up %s signal handler: %s"),
					"gdm_slave_start", "ALRM", strerror (errno));
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
				_("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "TERM/INT", strerror (errno));

	/* Child handler. Keeps an eye on greeter/session */
	child.sa_handler = gdm_slave_child_handler;
	child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigemptyset (&child.sa_mask);
	sigaddset (&child.sa_mask, SIGCHLD);

	if (sigaction (SIGCHLD, &child, NULL) < 0) 
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "CHLD", strerror (errno));

	/* Handle a USR2 which is ack from master that it received a message */
	usr2.sa_handler = gdm_slave_usr2_handler;
	usr2.sa_flags = SA_RESTART;
	sigemptyset (&usr2.sa_mask);
	sigaddset (&usr2.sa_mask, SIGUSR2);

	if (sigaction (SIGUSR2, &usr2, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "USR2", strerror (errno));

	first_time = time (NULL);
	death_count = 0;

	for (;;) {
		time_t the_time;

		check_notifies_now ();

		gdm_debug ("gdm_slave_start: Loop Thingie");
		gdm_slave_run (display);

		/* remote and flexi only run once */
		if (display->type != TYPE_LOCAL ||
		    ! parent_exists ()) {
			gdm_server_stop (display);
			gdm_slave_send_num (GDM_SOP_XPID, 0);
			gdm_slave_quick_exit (DISPLAY_REMANAGE);
		}

		the_time = time (NULL);

		death_count ++;

		if ((the_time - first_time) <= 0 ||
		    (the_time - first_time) > 60) {
			first_time = the_time;
			death_count = 0;
		} else if (death_count > 6) {
			gdm_slave_quick_exit (DISPLAY_ABORT);
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
			if ( ! gdm_auth_secure_display (d)) {
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
			gdm_slave_send_string (GDM_SOP_COOKIE, d->cookie);

			if ( ! gdm_server_reinit (d)) {
				gdm_error ("Error reinitilizing server");
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
		}
	}
	/* very very very evil, should never break, we can't return from
	   here sanely */
	_exit (DISPLAY_ABORT);
}

static gboolean
setup_automatic_session (GdmDisplay *display, const char *name)
{
	char *new_login;
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

	new_login = NULL;
	if ( ! gdm_verify_setup_user (display, login,
				      display->name, &new_login))
		return FALSE;

	if (new_login != NULL) {
		g_free (login);
		login = g_strdup (new_login);
	}

	gdm_debug ("setup_automatic_session: Automatic login successful");

	return TRUE;
}

static void 
gdm_screen_init (GdmDisplay *display) 
{
#ifdef HAVE_XFREE_XINERAMA
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
#elif HAVE_SOLARIS_XINERAMA
 /* This code from GDK, Copyright (C) 2002 Sun Microsystems */
 	int opcode;
	int firstevent;
	int firsterror;
	int n_monitors = 0;

	gboolean have_xinerama = FALSE;
	have_xinerama = XQueryExtension (display->dsp,
			"XINERAMA",
			&opcode,
			&firstevent,
			&firsterror);

	if (have_xinerama) {
	
		int result;
		XRectangle monitors[MAXFRAMEBUFFERS];
		unsigned char  hints[16];
		
		result = XineramaGetInfo (display->dsp, 0, monitors, hints, &n_monitors);
		/* Yes I know it should be Success but the current implementation 
		 * returns the num of monitor
		 */
		if (result <= 0)
			gdm_fail ("Xinerama active, but <= 0 screens?");

		if (n_monitors <= GdmXineramaScreen)
			GdmXineramaScreen = 0;

		display->screenx = monitors[GdmXineramaScreen].x;
		display->screeny = monitors[GdmXineramaScreen].y;
		display->screenwidth = monitors[GdmXineramaScreen].width;
		display->screenheight = monitors[GdmXineramaScreen].height;

		display->lrh_offsetx =
			DisplayWidth (display->dsp,
				      DefaultScreen (display->dsp))
			- (display->screenx + display->screenwidth);
		display->lrh_offsety =
			DisplayHeight (display->dsp,
				       DefaultScreen (display->dsp))
			- (display->screeny + display->screenheight);

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
	GdmWaitPid *wp;

	gdm_sigchld_block_push ();

	/* do what you do when you quit, this will hang until the
	 * greeter decides to print an STX\n and die, meaning it can do some
	 * last minute cleanup */
	gdm_slave_greeter_ctl_no_ret (GDM_QUIT, "");

	greet = FALSE;

	wp = slave_waitpid_setpid (d->greetpid);
	gdm_sigchld_block_pop ();

	slave_waitpid (wp);

	d->greetpid = 0;

	whack_greeter_fds ();

	gdm_slave_send_num (GDM_SOP_GREETPID, 0);

	gdm_slave_whack_temp_auth_file ();
}

gboolean
gdm_slave_check_user_wants_to_log_in (const char *user)
{
	gboolean loggedin = FALSE;
	int vt = -1;
	int i;
	char **vec;
	char *msg;
	int r;
	char *but[4];

	if ( ! GdmDoubleLoginWarning ||
	    /* always ignore root here, this is mostly a special case
	     * since a root login may not be a real login, such as the
	     config stuff, and people shouldn't log in as root anyway */
	    strcmp (user, gdm_root_user ()) == 0)
		return TRUE;

	gdm_slave_send_string (GDM_SOP_QUERYLOGIN, user);
	if (gdm_ack_response == NULL)
	       return TRUE;	
	vec = g_strsplit (gdm_ack_response, ",", -1);
	if (vec == NULL)
		return TRUE;

	for (i = 0; vec[i] != NULL && vec[i+1] != NULL; i += 2) {
		int ii;
		loggedin = TRUE;
		if (d->console && vt < 0 && sscanf (vec[i+1], "%d", &ii) == 1)
			vt = ii;
	}

	g_strfreev (vec);

	if ( ! loggedin)
		return TRUE;

	but[0] = _("Log in anyway");
	if (vt >= 0) {
		msg = _("You are already logged in.  "
			"You can log in anyway, return to your "
			"previous login session, or abort this "
			"login");
		but[1] = _("Return to previous login");
		but[2] = _("Abort login");
		but[3] = NULL;
	} else {
		msg = _("You are already logged in.  "
			"You can log in anyway or abort this "
			"login");
		but[1] = _("Abort login");
		but[2] = NULL;
	}

	if (greet)
		gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");

	r = gdm_failsafe_ask_buttons (d, msg, but);

	if (greet)
		gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");

	if (r <= 0)
		return TRUE;

	if (vt >= 0) {
		if (r == 2) /* Abort */
			return FALSE;

		/* Must be that r == 1, that is
		   return to previous login */

		if (d->type == TYPE_FLEXI) {
			gdm_slave_whack_greeter ();
			gdm_server_stop (d);
			gdm_slave_send_num (GDM_SOP_XPID, 0);

			/* wait for a few seconds to avoid any vt changing race
			 */
			gdm_sleep_no_signal (1);

			gdm_change_vt (vt);

			/* we are no longer needed so just die.
			   REMANAGE == ABORT here really */
			gdm_slave_quick_exit (DISPLAY_REMANAGE);
		}

		gdm_change_vt (vt);

		/* abort this login attempt */
		return FALSE;
	} else {
		if (r == 1) /* Abort */
			return FALSE;
		else
			return TRUE;
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
	    gdm_sleep_no_signal (d->sleep_before_run);
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
    
    ve_setenv ("DISPLAY", d->name, TRUE);
    ve_unsetenv ("XAUTHORITY"); /* just in case it's set */

    gdm_auth_set_local_auth (d);

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
	   d->dsp == NULL &&
	   ( ! SERVER_IS_LOCAL (d) || d->servpid > 1)) {
	d->dsp = XOpenDisplay (d->name);
	
	if (d->dsp == NULL) {
	    gdm_debug ("gdm_slave_run: Sleeping %d on a retry", 1+openretries*2);
	    gdm_sleep_no_signal (1+openretries*2);
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
	    gdm_sleep_no_signal (1);

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

    /* OK from now on it's really the user whacking us most likely,
     * we have already started up well */
    do_xfailed_on_xio_error = FALSE;

    /* If XDMCP setup pinging */
    if (d->type == TYPE_XDMCP &&
	GdmPingInterval > 0) {
	    alarm (GdmPingInterval);
    }

    /* checkout xinerama */
    if (d->handled)
	    gdm_screen_init (d);

    /* check log stuff for the server, this is done here
     * because it's really a race */
    if (SERVER_IS_LOCAL (d))
	    gdm_server_checklog (d);

    if ( ! d->handled) {
	    /* yay, we now wait for the server to die */
	    while (d->servpid > 0) {
		    pause ();
	    }
	    gdm_slave_quick_exit (DISPLAY_REMANAGE);
    } else if (d->use_chooser) {
	    /* this usually doesn't return */
	    gdm_slave_chooser ();  /* Run the chooser */
	    return;
    } else if (d->type == TYPE_LOCAL &&
	       gdm_first_login &&
	       ! ve_string_empty (ParsedAutomaticLogin) &&
	       strcmp (ParsedAutomaticLogin, gdm_root_user ()) != 0) {
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

    /* If XDMCP stop pinging */
    if (d->type == TYPE_XDMCP)
	    alarm (0);
}

/* A hack really, this will wait around until the first mapped window
 * with this class and focus it */
static void
focus_first_x_window (const char *class_res_name)
{
	pid_t pid;
	Display *disp;
	int p[2];
	XWindowAttributes attribs = { 0, };

	if (pipe (p) < 0) {
		p[0] = -1;
		p[1] = -1;
	}

	pid = fork ();
	if (pid < 0) {
		gdm_error (_("%s: cannot fork"), "focus_first_x_window");
		return;
	}
	/* parent */
	if (pid > 0) {
		/* Wait for this subprocess to start-up */
		if (p[0] >= 0) {
			fd_set rfds;
			struct timeval tv;

			IGNORE_EINTR (close (p[1]));

			FD_ZERO(&rfds);
			FD_SET(p[0], &rfds);

			/* Wait up to 2 seconds. */
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			select(p[0]+1, &rfds, NULL, NULL, &tv);

			IGNORE_EINTR (close (p[0]));
		}
		return;
	}

	gdm_unset_signals ();

	closelog ();

	gdm_close_all_descriptors (0 /* from */, p[1] /* except */, -1 /* except2 */);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	openlog ("gdm", LOG_PID, LOG_DAEMON);

	/* just in case it's set */
	ve_unsetenv ("XAUTHORITY");

	gdm_auth_set_local_auth (d);

	disp = XOpenDisplay (d->name);
	if (disp == NULL) {
		gdm_error (_("%s: cannot open display %s"),
			   "focus_first_x_window",
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

	if (p[1] >= 0) {
		IGNORE_EINTR (write (p[1], "!", 1));
		IGNORE_EINTR (close (p[1]));
	}

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
	if (pid == 0)
		gdm_unset_signals ();
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
		ve_setenv ("XAUTHORITY", GDM_AUTHFILE (display), TRUE);
		ve_setenv ("DISPLAY", display->name, TRUE);
		ve_setenv ("LOGNAME", pwent->pw_name, TRUE);
		ve_setenv ("USER", pwent->pw_name, TRUE);
		ve_setenv ("USERNAME", pwent->pw_name, TRUE);
		ve_setenv ("HOME", pwent->pw_dir, TRUE);
		ve_setenv ("SHELL", pwent->pw_shell, TRUE);
		ve_setenv ("PATH", GdmRootPath, TRUE);
		ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		closelog ();

		gdm_close_all_descriptors (0 /* from */, -1 /* except */, -1 /* except2 */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		IGNORE_EINTR (chdir (pwent->pw_dir));
		if (errno != 0)
			IGNORE_EINTR (chdir ("/"));

		/* exec the configurator */
		argv = ve_split (GdmConfigurator);
		if (argv != NULL &&
		    argv[0] != NULL &&
		    access (argv[0], X_OK) == 0)
			IGNORE_EINTR (execv (argv[0], argv));

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Could not execute the configuration "
				 "program.  Make sure it's path is set "
				 "correctly in the configuration file.  "
				 "I will attempt to start it from the "
				 "default location."));

		argv = ve_split
			(EXPANDED_BINDIR
			 "/gdmsetup --disable-sound --disable-crash-dialog");
		if (access (argv[0], X_OK) == 0)
			IGNORE_EINTR (execv (argv[0], argv));

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Could not execute the configuration "
				 "program.  Make sure it's path is set "
				 "correctly in the configuration file."));

		_exit (0);
	} else {
		GdmWaitPid *wp;
		
		configurator = TRUE;

		gdm_sigchld_block_push ();
		wp = slave_waitpid_setpid (display->sesspid);
		gdm_sigchld_block_pop ();

		slave_waitpid (wp);

		display->sesspid = 0;
		configurator = FALSE;

		/* this will clean up the sensitivity property */
		gdm_slave_sensitize_config ();
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
		GdmWaitPid *wp;

		gdm_sigchld_block_push ();

		gdm_slave_greeter_ctl_no_ret (GDM_SAVEDIE, "");

		greet = FALSE;

		wp = slave_waitpid_setpid (d->greetpid);

		gdm_sigchld_block_pop ();

		slave_waitpid (wp);

		d->greetpid = 0;

		whack_greeter_fds ();

		gdm_slave_send_num (GDM_SOP_GREETPID, 0);
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

			pwent = getpwuid (0);
			if (pwent == NULL) {
				/* what? no "root" ?? */
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, pwent->pw_name);
			login = gdm_verify_user (d,
						 pwent->pw_name,
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
				gdm_debug ("gdm_slave_wait_for_login: No login/Bad login");
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
			pwent = getpwuid (0);

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
			restart_greeter_now = TRUE;

			gdm_debug ("gdm_slave_wait_for_login: Running GDM Configurator ...");
			run_config (d, pwent);
			gdm_debug ("gdm_slave_wait_for_login: GDM Configurator finished ...");

			restart_greeter_now = FALSE;

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
			gdm_debug ("gdm_slave_wait_for_login: No login/Bad login");
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
	int max_write;
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
		int i, r;

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
				    cfgdir, "gdm", TRUE, TRUE, GdmUserMaxFile,
				    GdmRelaxPerms)) {
			VeConfig *cfg;
			char *cfgfile;

			cfgfile = g_strconcat (pwent->pw_dir, "/.gnome2/gdm", NULL);
			cfg = ve_config_new (cfgfile);
			g_free (cfgfile);
			picfile = ve_config_get_string (cfg, "face/picture=");
			ve_config_destroy (cfg);

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
						       dir, base, TRUE, TRUE, GdmUserMaxFile,
						       GdmRelaxPerms)) {
					g_free (picfile);
					picfile = NULL;
				}

				g_free (base);
				g_free (dir);
			}
		}
		g_free (cfgdir);

		if (picfile == NULL) {
			picfile = g_build_filename (pwent->pw_dir, ".face", NULL);
			if (access (picfile, R_OK) != 0) {
				g_free (picfile);
				picfile = NULL;
			} else if ( ! gdm_file_check ("run_pictures", pwent->pw_uid,
						      pwent->pw_dir, ".face", TRUE, TRUE, GdmUserMaxFile,
						      GdmRelaxPerms)) {
				g_free (picfile);

				seteuid (0);
				setegid (GdmGroupId);

				gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
				continue;
			}
		}

		/* Nothing found yet, try the old location */
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
					       picdir, "photo", TRUE, TRUE, GdmUserMaxFile,
					       GdmRelaxPerms)) {
				g_free (picdir);

				seteuid (0);
				setegid (GdmGroupId);

				gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
				continue;
			}
			g_free (picdir);
		}

		IGNORE_EINTR (r = stat (picfile, &s));
		if (r < 0) {
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
			fclose (fp);
			g_free (ret);
			seteuid (0);
			setegid (GdmGroupId);
			continue;
		}
		g_free (ret);

		gdm_fdprintf (greeter_fd_out, "%c", STX);

#ifdef PIPE_BUF
		max_write = MIN (PIPE_BUF, sizeof (buf));
#else
		/* apparently Hurd doesn't have PIPE_BUF */
		max_write = fpathconf (greeter_fd_out, _PC_PIPE_BUF);
		/* could return -1 if no limit */
		if (max_write > 0)
			max_write = MIN (max_write, sizeof (buf));
		else
			max_write = sizeof (buf);
#endif

		i = 0;
		while ((bytes = fread (buf, sizeof (char),
				       max_write, fp)) > 0) {
			int written;

			/* write until we succeed in writing something */
			IGNORE_EINTR (written = write (greeter_fd_out, buf, bytes));
			if (written < 0 && errno == EPIPE) {
				/* something very, very bad has happened */
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}

			/* write until we succeed in writing everything */
			while (written < bytes) {
				int n;
				IGNORE_EINTR (n = write (greeter_fd_out, &buf[written], bytes-written));
				if (n < 0 && errno == EPIPE) {
					/* something very, very bad has happened */
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				}
				if (n > 0)
					written += n;
			}

			/* we have written bytes btyes if it likes it or not */
			i += bytes;
		}

		fclose (fp);

		/* eek, this "could" happen, so just send some garbage */
		while (i < s.st_size) {
			bytes = MIN (sizeof (buf), s.st_size - i);
			errno = 0;
			bytes = write (greeter_fd_out, buf, bytes);
			if (bytes < 0 && errno == EPIPE) {
				/* something very, very bad has happened */
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
			if (bytes > 0)
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

	IGNORE_EINTR (unlink (name));
	authfd = open (name, O_EXCL|O_TRUNC|O_WRONLY|O_CREAT, 0600);

	if (authfd < 0) {
		IGNORE_EINTR (close (fromfd));
		seteuid (old);
		g_free (name);
		return NULL;
	}

	/* Make it owned by the user that Xnest is started as */
	IGNORE_EINTR (fchown (authfd, touid, -1));

	for (;;) {
		int written, n;
		IGNORE_EINTR (bytes = read (fromfd, buf, sizeof (buf)));

		/* EOF */
		if (bytes == 0)
			break;

		written = 0;
		do {
			IGNORE_EINTR (n = write (authfd, &buf[written], bytes-written));
			if (n < 0) {
				/*Error writing*/
				IGNORE_EINTR (close (fromfd));
				IGNORE_EINTR (close (authfd));
				setuid (old);
				g_free (name);
				return NULL;
			}
			written += n;
		} while (written < bytes);
	}

	IGNORE_EINTR (close (fromfd));
	IGNORE_EINTR (close (authfd));

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
	gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't init pipe to gdmgreeter"),
			"gdm_slave_greeter");

    /* hackish ain't it */
    create_temp_auth_file ();
    
    /* Fork. Parent is gdmslave, child is greeter process. */
    gdm_sigchld_block_push ();
    gdm_sigterm_block_push ();
    greet = TRUE;
    pid = d->greetpid = fork ();
    if (pid == 0)
	    gdm_unset_signals ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();

    switch (pid) {
	
    case 0:
	setsid ();

	gdm_unset_signals ();

	/* Plumbing */
	IGNORE_EINTR (close (pipe1[1]));
	IGNORE_EINTR (close (pipe2[0]));

	IGNORE_EINTR (dup2 (pipe1[0], STDIN_FILENO));
	IGNORE_EINTR (dup2 (pipe2[1], STDOUT_FILENO));

	closelog ();

	gdm_close_all_descriptors (2 /* from */, -1 /* except */, -1 /* except2 */);

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

	ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
	ve_setenv ("DISPLAY", d->name, TRUE);

	/* hackish ain't it */
	set_xnest_parent_stuff ();

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
		    g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
			ve_setenv ("HOME", pwent->pw_dir, TRUE);
		else
			ve_setenv ("HOME", ve_sure_string (GdmServAuthDir), TRUE); /* Hack */
		ve_setenv ("SHELL", pwent->pw_shell, TRUE);
	} else {
		ve_setenv ("HOME", ve_sure_string (GdmServAuthDir), TRUE); /* Hack */
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
			       GTK_MESSAGE_ERROR,
			       _("No servers were defined in the "
				 "configuration file and XDMCP was "
				 "disabled.  This can only be a "
				 "configuration error.  So I have started "
				 "a single server for you.  You should "
				 "log in and fix the configuration.  "
				 "Note that automatic and timed logins "
				 "are disabled now."));
		ve_unsetenv ("GDM_TIMED_LOGIN_OK");
	}

	if (d->failsafe_xserver) {
		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("I could not start the regular X "
				 "server (your graphical environment) "
				 "and so this is a failsafe X server.  "
				 "You should log in and properly "
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

	if (d->try_different_greeter) {
		/* FIXME: we should also really be able to do standalone failsafe
		   login, but that requires some work and is perhaps an overkill. */
		/* This should handle mostly the case where gdmgreeter is crashing
		   and we'd want to start gdmlogin for the user so that at least
		   something works instead of a flickering screen */
		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("The greeter program appears to be crashing.\n"
				 "I will attempt to use a different one."));
		if (strstr (argv[0], "gdmlogin") != NULL) {
			/* in case it is gdmlogin that's crashing
			   try the graphical greeter for luck */
			argv = g_new0 (char *, 2);
			argv[0] = EXPANDED_BINDIR "/gdmgreeter";
			argv[1] = NULL;
		} else {
			/* in all other cases, try the gdmlogin (standard greeter)
			   proggie */
			argv = g_new0 (char *, 2);
			argv[0] = EXPANDED_BINDIR "/gdmlogin";
			argv[1] = NULL;
		}
	}

	if (GdmAddGtkModules &&
	    !(ve_string_empty(GdmGtkModulesList)) &&
	    /* don't add modules if we're trying to prevent crashes,
	       perhaps it's the modules causing the problem in the first place */
	    ! d->try_different_greeter) {
		gchar *modules =  g_strdup_printf("--gtk-module=%s", GdmGtkModulesList);
		IGNORE_EINTR (execl (argv[0], argv[0], modules, NULL));
		/* Something went wrong */
		gdm_error (_("%s: Cannot start greeter with gtk modules: %s. Trying without modules"),
			   "gdm_slave_greeter",
			   GdmGtkModulesList);
		g_free(modules);
	}
	IGNORE_EINTR (execv (argv[0], argv));

	gdm_error (_("%s: Cannot start greeter trying default: %s"),
		   "gdm_slave_greeter",
		   EXPANDED_BINDIR "/gdmlogin");

	ve_setenv ("GDM_WHACKED_GREETER_CONFIG", "true", TRUE);

	argv = g_new0 (char *, 2);
	argv[0] = EXPANDED_BINDIR "/gdmlogin";
	argv[1] = NULL;
	IGNORE_EINTR (execv (argv[0], argv));

	gdm_error_box (d,
		       GTK_MESSAGE_ERROR,
		       _("Cannot start the greeter program, "
			 "you will not be able to log in.  "
			 "This display will be disabled.  "
			 "Try logging in by other means and "
			 "editing the configuration file"));
	
	/* If no greeter we really have to disable the display */
	gdm_child_exit (DISPLAY_ABORT, _("%s: Error starting greeter on display %s"), "gdm_slave_greeter", d->name);
	
    case -1:
	d->greetpid = 0;
	gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't fork gdmgreeter process"), "gdm_slave_greeter");
	
    default:
	IGNORE_EINTR (close (pipe1[0]));
	IGNORE_EINTR (close (pipe2[1]));

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

	/* Evil!, all this for debugging? */
	if (GdmDebug && gdm_in_signal == 0) {
		if (strncmp (str, GDM_SOP_COOKIE " ",
			     strlen (GDM_SOP_COOKIE " ")) == 0) {
			char *s = g_strndup
				(str, strlen (GDM_SOP_COOKIE " XXXX XX"));
			/* cut off most of the cookie for "security" */
			gdm_debug ("Sending %s...", s);
			g_free (s);
		} else {
			gdm_debug ("Sending %s", str);
		}
	}

	if (wait_for_ack) {
		gdm_got_ack = FALSE;
		g_free (gdm_ack_response);
		gdm_ack_response = NULL;
	}

	/* ensure this is sent from the actual slave with the pipe always, this is anal I know */
	if (d->slavepid == getpid ()) {
		fd = slave_fifo_pipe_fd;
	} else {
		fd = -1;
	}

	if (fd < 0) {
		/* FIXME: This is not likely to ever be used, remove
		   at some point.  Other then slaves shouldn't be using
		   these functions.  And if the pipe creation failed
		   in main daemon just abort the main daemon.  */
		/* Use the fifo as a fallback only now that we have a pipe */
		fifopath = g_build_filename (GdmServAuthDir, ".gdmfifo", NULL);
		old = geteuid ();
		if (old != 0)
			seteuid (0);
#ifdef O_NOFOLLOW
		fd = open (fifopath, O_WRONLY|O_NOFOLLOW);
#else
		fd = open (fifopath, O_WRONLY);
#endif
		if (old != 0)
			seteuid (old);
		g_free (fifopath);
	}

	/* eek */
	if (fd < 0) {
		if (gdm_in_signal == 0)
			gdm_error (_("%s: Can't open fifo!"), "gdm_slave_send");
		return;
	}

	gdm_fdprintf (fd, "\n%s\n", str);

	if (fd != slave_fifo_pipe_fd) {
		IGNORE_EINTR (close (fd));
	}

#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
	if (wait_for_ack && ! gdm_got_ack) {
		/* let the other process do its stuff */
		sched_yield ();
	}
#endif

	for (i = 0;
	     wait_for_ack &&
	     ! gdm_got_ack &&
	     parent_exists () &&
	     i < 10;
	     i++) {
		if (in_usr2_signal > 0) {
			fd_set rfds;
			struct timeval tv;

			FD_ZERO (&rfds);
			FD_SET (d->slave_notify_fd, &rfds);

			/* Wait up to 1 second. */
			tv.tv_sec = 1;
			tv.tv_usec = 0;

			if (select (d->slave_notify_fd+1, &rfds, NULL, NULL, &tv) > 0) {
				gdm_slave_handle_usr2_message ();
			}
		} else {
			struct timeval tv;
			/* Wait 1 second. */
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			select (0, NULL, NULL, NULL, &tv);
			/* don't want to use sleep since we're using alarm
			   for pinging */
		}
	}

	if (wait_for_ack  &&
	    ! gdm_got_ack &&
	    gdm_in_signal == 0) {
		if (strncmp (str, GDM_SOP_COOKIE " ",
			     strlen (GDM_SOP_COOKIE " ")) == 0) {
			char *s = g_strndup
				(str, strlen (GDM_SOP_COOKIE " XXXX XX"));
			/* cut off most of the cookie for "security" */
			gdm_debug ("Timeout occured for sending message %s...", s);
			g_free (s);
		} else {
			gdm_debug ("Timeout occured for sending message %s", str);
		}
	}
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
	GdmHostent *host;
	struct in_addr ia;
	char *str;

	host = gdm_gethostbyname (hostname);

	if (host->addrs == NULL) {
		gdm_error ("Cannot get address of host '%s'", hostname);
		gdm_hostent_free (host);
		return;
	}
	/* take first address */
	ia = host->addrs[0];
	gdm_hostent_free (host);

	gdm_debug ("Sending chosen host address (%s) %s",
		   hostname, inet_ntoa (ia));

	str = g_strdup_printf ("%s %d %s", GDM_SOP_CHOSEN,
			       disp->indirect_id,
			       inet_ntoa (ia));

	gdm_slave_send (str, FALSE);

	g_free (str);
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
	GdmWaitPid *wp;

	gdm_debug ("gdm_slave_chooser: Running chooser on %s", d->name);

	/* Open a pipe for chooser communications */
	if (pipe (p) < 0)
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't init pipe to gdmchooser"), "gdm_slave_chooser");

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (d, GdmDisplayInit, NULL, NULL,
			       FALSE /* pass_stdout */,
			       TRUE /* set_parent */);

	/* Fork. Parent is gdmslave, child is greeter process. */
	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->chooserpid = fork ();
	if (pid == 0)
		gdm_unset_signals ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	switch (pid) {

	case 0:
		setsid ();

		gdm_unset_signals ();

		/* Plumbing */
		IGNORE_EINTR (close (p[0]));

		if (p[1] != STDOUT_FILENO) 
			IGNORE_EINTR (dup2 (p[1], STDOUT_FILENO));

		closelog ();

		IGNORE_EINTR (close (0));
		gdm_close_all_descriptors (2 /* from */, -1 /* except */, -1 /* except2 */);

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

		ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		ve_setenv ("DISPLAY", d->name, TRUE);

		ve_setenv ("LOGNAME", GdmUser, TRUE);
		ve_setenv ("USER", GdmUser, TRUE);
		ve_setenv ("USERNAME", GdmUser, TRUE);

		ve_setenv ("GDM_VERSION", VERSION, TRUE);

		pwent = getpwnam (GdmUser);
		if (pwent != NULL) {
			/* Note that usually this doesn't exist */
			if (g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
				ve_setenv ("HOME", pwent->pw_dir, TRUE);
			else
				ve_setenv ("HOME", ve_sure_string (GdmServAuthDir), TRUE); /* Hack */
			ve_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			ve_setenv ("HOME", ve_sure_string (GdmServAuthDir), TRUE); /* Hack */
			ve_setenv ("SHELL", "/bin/sh", TRUE);
		}
		ve_setenv ("PATH", GdmDefaultPath, TRUE);
		ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);

		argv = ve_split (GdmChooser);
		IGNORE_EINTR (execv (argv[0], argv));

		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Cannot start the chooser program, "
				 "you will probably not be able to log in.  "
				 "Please contact the system administrator."));

		gdm_child_exit (DISPLAY_REMANAGE, _("%s: Error starting chooser on display %s"), "gdm_slave_chooser", d->name);

	case -1:
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't fork gdmchooser process"), "gdm_slave_chooser");

	default:
		gdm_debug ("gdm_slave_chooser: Chooser on pid %d", d->chooserpid);
		gdm_slave_send_num (GDM_SOP_CHOOSERPID, d->chooserpid);

		IGNORE_EINTR (close (p[1]));

		/* wait for the chooser to die */

		gdm_sigchld_block_push ();
		wp = slave_waitpid_setpid (d->chooserpid);
		gdm_sigchld_block_pop ();

		slave_waitpid (wp);

		d->chooserpid = 0;
		gdm_slave_send_num (GDM_SOP_CHOOSERPID, 0);

		/* Note: Nothing affecting the chooser needs update
		 * from notifies */

		IGNORE_EINTR (bytes = read (p[0], buf, sizeof(buf)-1));
		if (bytes > 0) {
			IGNORE_EINTR (close (p[0]));

			if (buf[bytes-1] == '\n')
				buf[bytes-1] ='\0';
			else
				buf[bytes] ='\0';
			if (d->type == TYPE_XDMCP) {
				send_chosen_host (d, buf);
				gdm_slave_quick_exit (DISPLAY_CHOSEN);
			} else {
				gdm_debug ("Sending locally chosen host %s", buf);
				gdm_slave_send_string (GDM_SOP_CHOSEN_LOCAL, buf);
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
		}

		IGNORE_EINTR (close (p[0]));

		gdm_slave_quick_exit (DISPLAY_REMANAGE);
		break;
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

	file = g_build_filename (GdmSessDir, session_name, NULL);
	if (access (file, F_OK) == 0) {
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
		"Default.desktop",
		"default.desktop",
		"Gnome.desktop",
		"gnome.desktop",
		"GNOME.desktop",
		"kde.desktop",
		"KDE.desktop",
		"failsafe.desktop",
		"Failsafe.desktop",
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
get_session_exec (const char *desktop)
{
	char *file;
	VeConfig *cfg;
	char *exec;

	file = g_build_filename (GdmSessDir, desktop, NULL);
	cfg = ve_config_get (file);
	g_free (file);
	exec = ve_config_get_string (cfg, "Desktop Entry/Exec");
	return exec;
}

static char *
find_prog (const char *name)
{
	char *path;
	int i;
	char *try[] = {
		"/usr/bin/X11/",
		"/usr/X11R6/bin/",
		"/opt/X11R6/bin/",
		"/usr/bin/",
		"/usr/openwin/bin/",
		"/usr/local/bin/",
		"/opt/gnome/bin/",
		EXPANDED_BINDIR "/",
		NULL
	};

	path = g_find_program_in_path (name);
	if (path != NULL &&
	    access (path, X_OK) == 0) {
		return path;
	}
	g_free (path);
	for (i = 0; try[i] != NULL; i++) {
		path = g_strconcat (try[i], name, NULL);
		if (access (path, X_OK) == 0) {
			return path;
		}
		g_free (path);
	}
	return NULL;
}

static void
session_child_run (struct passwd *pwent,
		   gboolean failsafe,
		   const char *home_dir,
		   gboolean home_dir_ok,
		   const char *session,
		   const char *save_session,
		   const char *language,
		   const char *gnome_session,
		   gboolean usrcfgok,
		   gboolean savesess,
		   gboolean savelang)
{
	int logfd;
	char *exec;
	const char *shell = NULL;
	VeConfig *dmrc = NULL;
	char *argv[4];

	gdm_unset_signals ();
	if (setsid() < 0)
		/* should never happen */
		gdm_error (_("%s: setsid() failed: %s!"),
			   "session_child_run", strerror(errno));

	ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);

	/* Here we setup our 0,1,2 descriptors, we do it here
	 * nowdays rather then later on so that we get errors even
	 * from the PreSession script */
        /* Log all output from session programs to a file,
	 * unless in failsafe mode which needs to work when there is
	 * no diskspace as well */
	if ( ! failsafe && home_dir_ok) {
		char *filename = g_build_filename (home_dir,
						   ".xsession-errors",
						   NULL);
		uid_t old = geteuid ();
		uid_t oldg = getegid ();

		setegid (pwent->pw_gid);
		seteuid (pwent->pw_uid);
		/* unlink to be anal */
		IGNORE_EINTR (unlink (filename));
		logfd = open (filename, O_EXCL|O_CREAT|O_TRUNC|O_WRONLY, 0644);
		seteuid (old);
		setegid (oldg);

		g_free (filename);

		if (logfd != -1) {
			IGNORE_EINTR (dup2 (logfd, 1));
			IGNORE_EINTR (dup2 (logfd, 2));
			IGNORE_EINTR (close (logfd));
		} else {
			IGNORE_EINTR (close (1));
			IGNORE_EINTR (close (2));
			gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
			gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
			gdm_error (_("%s: Could not open ~/.xsession-errors"),
				   "run_session_child");
		}
	} else {
		IGNORE_EINTR (close (1));
		IGNORE_EINTR (close (2));
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
	}

	IGNORE_EINTR (close (0));
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

	/* Set this for the PreSession script */
	/* compatibility */
	ve_setenv ("GDMSESSION", session, TRUE);

	ve_setenv ("DESKTOP_SESSION", session, TRUE);

	/* Run the PreSession script */
	if (gdm_slave_exec_script (d, GdmPreSession,
				   login, pwent,
				   TRUE /* pass_stdout */,
				   TRUE /* set_parent */) != EXIT_SUCCESS &&
	    /* ignore errors in failsafe modes */
	    ! failsafe) 
		/* If script fails reset X server and restart greeter */
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Execution of PreSession script returned > 0. Aborting."), "gdm_slave_session_start");

	gdm_clearenv ();

	/* Prepare user session */
	ve_setenv ("XAUTHORITY", d->userauth, TRUE);
	ve_setenv ("DISPLAY", d->name, TRUE);
	ve_setenv ("LOGNAME", login, TRUE);
	ve_setenv ("USER", login, TRUE);
	ve_setenv ("USERNAME", login, TRUE);
	ve_setenv ("HOME", home_dir, TRUE);
	ve_setenv ("GDMSESSION", session, TRUE);
	ve_setenv ("DESKTOP_SESSION", session, TRUE);
	ve_setenv ("SHELL", pwent->pw_shell, TRUE);
	ve_unsetenv ("MAIL");	/* Unset $MAIL for broken shells */

	if (gnome_session != NULL)
		ve_setenv ("GDM_GNOME_SESSION", gnome_session, TRUE);

	/* Special PATH for root */
	if (pwent->pw_uid == 0)
		ve_setenv ("PATH", GdmRootPath, TRUE);
	else
		ve_setenv ("PATH", GdmDefaultPath, TRUE);

	/* Eeeeek, this no lookie as a correct language code,
	 * just use the system default */
	if ( ! ve_string_empty (language) &&
	     ! ve_locale_exists (language)) {
		char *msg = g_strdup_printf (_("Language %s does not exist, using %s"),
					     language, _("System default"));
		gdm_error_box (d, GTK_MESSAGE_ERROR, msg);
		language = NULL;
	}

	/* Now still as root make the system authfile not readable by others,
	   and therefore not by the gdm user */
	IGNORE_EINTR (chmod (GDM_AUTHFILE (d), 0640));

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

	IGNORE_EINTR (chdir (home_dir));
	if (errno != 0) {
		IGNORE_EINTR (chdir ("/"));
	}

#ifdef HAVE_LOGINCAP
	if (setusercontext (NULL, pwent, pwent->pw_uid,
			    LOGIN_SETLOGIN | LOGIN_SETPATH |
			    LOGIN_SETPRIORITY | LOGIN_SETRESOURCES |
			    LOGIN_SETUMASK | LOGIN_SETUSER |
			    LOGIN_SETENV) < 0)
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: setusercontext() failed for %s. "
				  "Aborting."), "gdm_slave_session_start",
				login);
#else
	if (setuid (pwent->pw_uid) < 0) 
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Could not become %s. Aborting."), "gdm_slave_session_start", login);
#endif

	/* Only force GDM_LANG to something if there is other then
	 * system default selected.  Else let the session do whatever it
	 * does since we're using sys default */
	if ( ! ve_string_empty (language)) {
		ve_setenv ("LANG", language, TRUE);
		ve_setenv ("GDM_LANG", language, TRUE);
	}

	/* just in case there is some weirdness going on */
	IGNORE_EINTR (chdir (home_dir));
	
	if (usrcfgok && savesess && home_dir_ok) {
		gchar *cfgstr = g_build_filename (home_dir, ".dmrc", NULL);
		if (dmrc == NULL)
			dmrc = ve_config_new (cfgstr);
		ve_config_set_string (dmrc, "Desktop/Session",
				      ve_sure_string (save_session));
		g_free (cfgstr);
	}
	
	if (usrcfgok && savelang && home_dir_ok) {
		gchar *cfgstr = g_build_filename (home_dir, ".dmrc", NULL);
		if (dmrc == NULL)
			dmrc = ve_config_new (cfgstr);
		if (ve_string_empty (language))
			/* we chose the system default language so wipe the
			 * lang key */
			ve_config_delete_key (dmrc, "Desktop/Language");
		else
			ve_config_set_string (dmrc, "Desktop/Language",
					      language);
		g_free (cfgstr);
	}

	if (dmrc != NULL) {
		ve_config_save (dmrc, FALSE);
		ve_config_destroy (dmrc);
		dmrc = NULL;
	}

	closelog ();

	gdm_close_all_descriptors (3 /* from */, -1 /* except */, -1 /* except2 */);

	openlog ("gdm", LOG_PID, LOG_DAEMON);
	
	argv[0] = NULL;
	argv[1] = NULL;
	argv[2] = NULL;
	argv[3] = NULL;

	exec = NULL;
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) != 0 &&
	    strcmp (session, GDM_SESSION_FAILSAFE_GNOME) != 0) {
		exec = get_session_exec (session);
		if (exec == NULL) {
			gdm_error (_("%s: No Exec line in the session file: %s, starting failsafe GNOME"),
				   "gdm_slave_session_start",
				   session);
			session = GDM_SESSION_FAILSAFE_GNOME;
			gdm_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("The session you selected does not look valid.  I will run the GNOME failsafe session for you."));
		} else {
			/* HACK!, if failsafe, we really wish to run the
			   internal one */
			if (strcmp (exec, "failsafe") == 0) {
				session = GDM_SESSION_FAILSAFE_XTERM;
				exec = NULL;
			}
		}
	}

	if (exec != NULL) {
		/* cannot be possibly failsafe */
		if (access (GdmXsession, X_OK) != 0) {
			gdm_error (_("%s: Cannot find or run the base Xsession script, will try GNOME failsafe"),
				   "gdm_slave_session_start");
			session = GDM_SESSION_FAILSAFE_GNOME;
			gdm_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("Cannot find or run the base session script, will try the GNOME failsafe session for you."));
		} else {
			/* This is where everything is OK, and note that
			   we really DON'T care about leaks, we are going to
			   exec in just a bit */
			argv[0] = GdmXsession;
			argv[1] = exec;
			argv[2] = NULL;
		}
	}

	if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0) {
		argv[0] = find_prog ("gnome-session");
		if (argv[0] == NULL) {
			/* yaikes */
			gdm_error (_("%s: gnome-session not found for a failsafe GNOME session, trying xterm"),
				   "gdm_slave_session_start");
			session = GDM_SESSION_FAILSAFE_XTERM;
			gdm_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("Could not find the GNOME installation, "
				   "will try running the \"Failsafe xterm\" "
				   "session."));
		} else {
			argv[1] = "--failsafe";
			argv[2] = NULL;
			gdm_error_box
				(d, GTK_MESSAGE_INFO,
				 _("This is the Failsafe Gnome session.  "
				   "You will be logged into the 'Default' "
				   "session of Gnome with no startup scripts "
				   "run.  This is only to fix problems in "
				   "your installation."));
		}
		failsafe = TRUE;
	}

	/* an if and not an else, we could have done a fall-through
	 * to here in the above code if we can't find gnome-session */
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0) {
		argv[0] = find_prog ("xterm");
		if (argv[0] == NULL) {
			gdm_error_box (d, GTK_MESSAGE_ERROR,
				       _("Cannot find \"xterm\" to start "
					 "a failsafe session."));
			/* nyah nyah nyah nyah nyah */
			_exit (0);
		} else {
			argv[1] = "-geometry";
			argv[2] = g_strdup_printf ("80x24-%d-%d",
						   d->lrh_offsetx,
						   d->lrh_offsety);
			argv[3] = NULL;
			gdm_error_box
				(d, GTK_MESSAGE_INFO,
				 _("This is the Failsafe xterm session.  "
				   "You will be logged into a terminal "
				   "console so that you may fix your system "
				   "if you cannot log in any other way.  "
				   "To exit the terminal emulator, type "
				   "'exit' and an enter into the window."));
			focus_first_x_window ("xterm");
		}
		failsafe = TRUE;
	} 

	gdm_debug ("Running %s %s %s for %s on %s",
		   argv[0],
		   ve_sure_string (argv[1]),
		   ve_sure_string (argv[2]),
		   login, d->name);

	if ( ! ve_string_empty (pwent->pw_shell)) {
		shell = pwent->pw_shell;
	} else {
		shell = "/bin/sh";
	}

	/* just a stupid test */
	if (strcmp (shell, "/sbin/nologin") == 0 ||
	    strcmp (shell, "/bin/false") == 0 ||
	    strcmp (shell, "/bin/true") == 0) {
		gdm_error (_("%s: User not allowed to log in"),
			   "gdm_slave_session_start");
		gdm_error_box (d, GTK_MESSAGE_ERROR,
			       _("The system administrator has "
				 "disabled your account."));
		/* ends as if nothing bad happened */
		_exit (0);
	}

	IGNORE_EINTR (execv (argv[0], argv));

	/* will go to .xsession-errors */
	fprintf (stderr, _("%s: Could not exec %s %s %s"), 
		 "gdm_slave_session_start",
		 argv[0],
		 ve_sure_string (argv[1]),
		 ve_sure_string (argv[2]));
	gdm_error ( _("%s: Could not exec %s %s %s"), 
		 "gdm_slave_session_start",
		 argv[0],
		 ve_sure_string (argv[1]),
		 ve_sure_string (argv[2]));

	/* if we can't read and exec the session, then make a nice
	 * error dialog */
	gdm_error_box
		(d, GTK_MESSAGE_ERROR,
		 /* we can't really be any more specific */
		 _("Cannot start the session due to some "
		   "internal error."));
	
	/* ends as if nothing bad happened */
	_exit (0);
}

static void
gdm_slave_session_start (void)
{
    struct passwd *pwent;
    char *save_session = NULL, *session = NULL, *language = NULL, *usrsess, *usrlang;
    char *gnome_session = NULL;
    gboolean savesess = FALSE, savelang = FALSE;
    gboolean usrcfgok = FALSE, authok = FALSE;
    const char *home_dir = NULL;
    gboolean home_dir_ok = FALSE;
    time_t session_start_time, end_time; 
    gboolean failsafe = FALSE;
    pid_t pid;
    GdmWaitPid *wp;
    uid_t uid;
    gid_t gid;

    gdm_debug ("gdm_slave_session_start: Attempting session for user '%s'",
	       login);

    pwent = getpwnam (login);

    if (pwent == NULL)  {
	    /* This is sort of an "assert", this should NEVER happen */
	    if (greet)
		    gdm_slave_whack_greeter();
	    gdm_slave_exit (DISPLAY_REMANAGE,
			    _("%s: User passed auth but getpwnam(%s) failed!"), "gdm_slave_session_start", login);
    }

    uid = pwent->pw_uid;
    gid = pwent->pw_gid;

    /* Run the PostLogin script */
    if (gdm_slave_exec_script (d, GdmPostLogin,
			       login, pwent,
			       TRUE /* pass_stdout */,
			       TRUE /* set_parent */) != EXIT_SUCCESS &&
	/* ignore errors in failsafe modes */
	! failsafe) {
	    gdm_verify_cleanup (d);
	    gdm_error (_("%s: Execution of PostLogin script returned > 0. Aborting."), "gdm_slave_session_start");
	    /* script failed so just try again */
	    return;
		
    }

    if (pwent->pw_dir == NULL ||
	! g_file_test (pwent->pw_dir, G_FILE_TEST_IS_DIR)) {
	    char *msg = g_strdup_printf (
		     _("Your home directory is listed as:\n'%s'\n"
		       "but it does not appear to exist.  "
		       "Do you want to log in with the / (root) "
		       "directory as your home directory?\n\n"
		       "It is unlikely anything will work unless "
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
		    session_started = FALSE;
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
	    /* Sanity check on ~user/.dmrc */
	    usrcfgok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
				       home_dir, ".dmrc", TRUE, FALSE,
				       GdmUserMaxFile, GdmRelaxPerms);
    } else {
	    usrcfgok = FALSE;
    }

    if (usrcfgok) {
	gchar *cfgfile = g_build_filename (home_dir, ".dmrc", NULL);
	VeConfig *cfg = ve_config_new (cfgfile);
	g_free (cfgfile);

	usrsess = ve_config_get_string (cfg, "Desktop/Session");
	if (usrsess == NULL)
		usrsess = g_strdup ("");
	usrlang = ve_config_get_string (cfg, "Desktop/Language");
	if (usrlang == NULL)
		usrlang = g_strdup ("");

	ve_config_destroy (cfg);
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

	    gdm_debug ("gdm_slave_session_start: Authentication completed. Whacking greeter");

	    gdm_slave_whack_greeter ();
    }

    /* Ensure some sanity in this world */
    gdm_ensure_sanity ();

    if (GdmKillInitClients)
	    gdm_server_whack_clients (d);

    /* Now that we will set up the user authorization we will
       need to run session_stop to whack it */
    session_started = TRUE;

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

	    gdm_error_box (d,
			   GTK_MESSAGE_ERROR,
			   _("GDM could not write to your authorization "
			     "file.  This could mean that you are out of "
			     "disk space or that your home directory could "
			     "not be opened for writing.  In any case, it "
			     "is not possible to log in.  Please contact "
			     "your system administrator"));

	    gdm_slave_session_stop (FALSE /* run_post_session */,
				    FALSE /* no_shutdown_check */);

	    gdm_slave_quick_exit (DISPLAY_REMANAGE);
    }

    if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
	g_ascii_strcasecmp (session, "Failsafe") == 0 /* hack */ ||
	g_ascii_strcasecmp (session, "Failsafe.desktop") == 0 /* hack */)
	    failsafe = TRUE;

    if ( ! failsafe) {
	    char *exec = get_session_exec (session);
	    if ( ! ve_string_empty (exec) &&
		strcmp (exec, "failsafe") == 0)
		    failsafe = TRUE;
	    g_free (exec);
    }

    /* Write out the Xservers file */
    gdm_slave_send_num (GDM_SOP_WRITE_X_SERVERS, 0 /* bogus */);

    if (d->dsp != NULL) {
	    Cursor xcursor;

	    XSetInputFocus (d->dsp, PointerRoot,
			    RevertToPointerRoot, CurrentTime);

	    /* return left pointer */
	    xcursor = XCreateFontCursor (d->dsp, GDK_LEFT_PTR);
	    XDefineCursor (d->dsp,
			   DefaultRootWindow (d->dsp),
			   xcursor);
	    XFreeCursor (d->dsp, xcursor);
	    XSync (d->dsp, False);
    }

    /* don't completely rely on this, the user
     * could reset time or do other crazy things */
    session_start_time = time (NULL);

    /* Start user process */
    gdm_sigchld_block_push ();
    gdm_sigterm_block_push ();
    pid = d->sesspid = fork ();
    if (pid == 0)
	    gdm_unset_signals ();
    gdm_sigterm_block_pop ();
    gdm_sigchld_block_pop ();

    switch (pid) {
	
    case -1:
	gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Error forking user session"), "gdm_slave_session_start");
	
    case 0:
	/* Never returns */
	session_child_run (pwent,
			   failsafe,
			   home_dir,
			   home_dir_ok,
			   session,
			   save_session,
			   language,
			   gnome_session,
			   usrcfgok,
			   savesess,
			   savelang);
	g_assert_not_reached ();
	
    default:
	break;
    }

    /* We must be root for this, and we are, but just to make sure */
    seteuid (0);
    setegid (GdmGroupId);
    /* Reset all the process limits, pam may have set some up for our process and that
       is quite evil.  But pam is generally evil, so this is to be expected. */
    gdm_reset_limits ();

    g_free (session);
    g_free (save_session);
    g_free (language);
    g_free (gnome_session);

    gdm_slave_send_num (GDM_SOP_SESSPID, pid);

    gdm_sigchld_block_push ();
    wp = slave_waitpid_setpid (d->sesspid);
    gdm_sigchld_block_pop ();

    slave_waitpid (wp);

    d->sesspid = 0;

    /* Now still as root make the system authfile readable by others,
       and therefore by the gdm user */
    IGNORE_EINTR (chmod (GDM_AUTHFILE (d), 0644));

    end_time = time (NULL);

    gdm_debug ("Session: start_time: %ld end_time: %ld",
	       (long)session_start_time, (long)end_time);

    if  ((/* sanity */ end_time >= session_start_time) &&
	 (end_time - 10 <= session_start_time) &&
	 /* only if the X server still exist! */
	 d->servpid > 1) {
	    char *errfile = g_build_filename (home_dir, ".xsession-errors", NULL);
	    gdm_debug ("Session less than 10 seconds!");

	    /* FIXME: perhaps do some checking to display a better error,
	     * such as gnome-session missing and such things. */
	    gdm_error_box_full (d,
				GTK_MESSAGE_WARNING,
				_("Your session only lasted less than "
				  "10 seconds.  If you have not logged out "
				  "yourself, this could mean that there is "
				  "some installation problem or that you may "
				  "be out of diskspace.  Try logging in with "
				  "one of the failsafe sessions to see if you "
				  "can fix this problem."),
				(home_dir_ok && ! failsafe) ?
			       	  _("View details (~/.xsession-errors file)") :
				  NULL,
				errfile,
				uid, gid);
	    g_free (errfile);
    }

    gdm_slave_session_stop (pid != 0 /* run_post_session */,
			    FALSE /* no_shutdown_check */);

    gdm_debug ("gdm_slave_session_start: Session ended OK (now all finished)");
}


/* Stop any in progress sessions */
static void
gdm_slave_session_stop (gboolean run_post_session,
			gboolean no_shutdown_check)
{
    struct passwd *pwent;
    char *x_servers_file;
    char *local_login;

    in_session_stop ++;

    session_started = FALSE;

    local_login = login;
    login = NULL;

    seteuid (0);
    setegid (0);

    gdm_slave_send_num (GDM_SOP_SESSPID, 0);

    /* Now still as root make the system authfile not readable by others,
       and therefore not by the gdm user */
    if (GDM_AUTHFILE (d) != NULL)
	    IGNORE_EINTR (chmod (GDM_AUTHFILE (d), 0640));

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
	    /* Execute post session script */
	    gdm_debug ("gdm_slave_session_stop: Running post session script");
	    gdm_slave_exec_script (d, GdmPostSession, local_login, pwent,
				   FALSE /* pass_stdout */,
				   TRUE /* set_parent */);
    }

    IGNORE_EINTR (unlink (x_servers_file));
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

    in_session_stop --;

    if (need_to_quit_after_session_stop) {
	    gdm_debug ("gdm_slave_session_stop: Final cleanup");

	    gdm_slave_quick_exit (exit_code_to_use);
    }

#ifdef __linux__
    /* If on linux and the runlevel is 0 or 6 and not the runlevel that
       we were started in, then we are rebooting or halting.
       Probably the user selected shutdown or reboot from the logout
       menu.  In this case we can really just sleep for a few seconds and
       basically wait to be killed.  I'll set the default for 30 seconds
       and let people yell at me if this breaks something.  It shouldn't.
       In fact it should fix things so that the login screen is not brought
       up again and then whacked.  Waiting is safer then DISPLAY_ABORT,
       since if we really do get this wrong, then at the worst case the
       user will wait for a few moments. */
    if ( ! need_to_quit_after_session_stop &&
	 ! no_shutdown_check &&
	access ("/sbin/runlevel", X_OK) == 0) {
	    char ign;
	    int rnl;
	    FILE *fp = fopen ("/sbin/runlevel", "r");
	    if (fscanf (fp, "%c %d", &ign, &rnl) == 2 &&
		(rnl == 0 || rnl == 6) &&
		rnl != gdm_normal_runlevel) {
		    /* this is a stupid loop, but we may be getting signals,
		       so we don't want to just do sleep (30) */
		    time_t c = time (NULL);
		    gdm_info (_("GDM detected a shutdown or reboot "
				"in progress."));
		    fclose (fp);
		    while (c + 30 >= time (NULL)) {
			    struct timeval tv;
			    /* Wait 30 seconds. */
			    tv.tv_sec = 30;
			    tv.tv_usec = 0;
			    select (0, NULL, NULL, NULL, &tv);
			    /* don't want to use sleep since we're using alarm
			       for pinging */
		    }
		    /* hmm, didn't get TERM, weird */
	    } else {
		    fclose (fp);
	    }
    }
#endif /* __linux__ */
}

static void
gdm_slave_term_handler (int sig)
{
	static gboolean got_term_before = FALSE;

	gdm_in_signal++;
	gdm_wait_for_ack = FALSE;

	gdm_debug ("gdm_slave_term_handler: %s got TERM/INT signal", d->name);

	exit_code_to_use = DISPLAY_ABORT;
	need_to_quit_after_session_stop = TRUE;

	if (already_in_slave_start_jmp ||
	    (got_term_before && in_session_stop > 0)) {
		gdm_sigchld_block_push ();
		/* be very very very nasty to the extra process if the user is really
		   trying to get rid of us */
		if (extra_process > 1)
			kill (-(extra_process), SIGKILL);
		/* also be very nasty to the X server at this stage */
		if (d->servpid > 1)
			kill (d->servpid, SIGKILL);
		gdm_sigchld_block_pop ();
		gdm_in_signal--;
		got_term_before = TRUE;
		/* we're already quitting, just a matter of killing all the processes */
		return;
	}
	got_term_before = TRUE;

	/* just in case this was set to something else, like during
	 * server reinit */
	XSetIOErrorHandler (gdm_slave_xioerror_handler);

	if (in_session_stop > 0) {
		/* the need_to_quit_after_session_stop is now set so things will
		   work out right */
		gdm_in_signal--;
		return;
	}

	if (session_started) {
		SIGNAL_EXIT_WITH_JMP (d, JMP_SESSION_STOP_AND_QUIT);
	} else {
		SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
	}

	/* never reached */
	gdm_in_signal--;
}

/* called on alarms to ping */
static void
gdm_slave_alrm_handler (int sig)
{
	static gboolean in_ping = FALSE;

	if (already_in_slave_start_jmp)
		return;

	gdm_in_signal++;

	gdm_debug ("gdm_slave_alrm_handler: %s got ARLM signal, "
		   "to ping display", d->name);

	if (d->dsp == NULL) {
		gdm_in_signal --;
		/* huh? */
		return;
	}

	if (in_ping) {
		slave_start_jmp_error_to_print = 
			g_strdup_printf (_("Ping to %s failed, whacking display!"),
					 d->name);
		need_to_quit_after_session_stop = TRUE;
		exit_code_to_use = DISPLAY_REMANAGE;

		if (session_started) {
			SIGNAL_EXIT_WITH_JMP (d, JMP_SESSION_STOP_AND_QUIT);
		} else {
			SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
		}
	}

	in_ping = TRUE;

	/* schedule next alarm */
	alarm (GdmPingInterval);

	XSync (d->dsp, True);

	in_ping = FALSE;

	gdm_in_signal --;
}

/* Called on every SIGCHLD */
void 
gdm_slave_child_handler (int sig)
{
    gint status;
    pid_t pid;
    uid_t old;

    if (already_in_slave_start_jmp)
	    return;

    gdm_in_signal++;

    gdm_debug ("gdm_slave_child_handler");

    old = geteuid ();
    if (old != 0)
	    seteuid (0);
    
    while ((pid = waitpid (-1, &status, WNOHANG)) > 0) {
        GSList *li;

	gdm_debug ("gdm_slave_child_handler: %d died", pid);

	for (li = slave_waitpids; li != NULL; li = li->next) {
		GdmWaitPid *wp = li->data;
		if (wp->pid == pid) {
			wp->pid = -1;
			if (wp->fd_w >= 0) {
				IGNORE_EINTR (write (wp->fd_w, "!", 1));
			}
		}
	}
	
	if (WIFEXITED (status))
	    gdm_debug ("gdm_slave_child_handler: %d returned %d",
		       (int)pid, (int)WEXITSTATUS (status));
	if (WIFSIGNALED (status))
	    gdm_debug ("gdm_slave_child_handler: %d died of %d",
		       (int)pid, (int)WTERMSIG (status));

	if (pid == d->greetpid && greet) {
		if (WIFEXITED (status) &&
		    WEXITSTATUS (status) == DISPLAY_RESTARTGREETER) {
			/* FIXME: shouldn't do this from
			   a signal handler */
			/*gdm_slave_desensitize_config ();*/

			greet = FALSE;
			d->greetpid = 0;
			whack_greeter_fds ();
			gdm_slave_send_num (GDM_SOP_GREETPID, 0);

			do_restart_greeter = TRUE;
			if (restart_greeter_now) {
				slave_waitpid_notify_all ();
			} else {
				interrupted = TRUE;
			}
			continue;
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
		     WEXITSTATUS (status) == DISPLAY_RUN_CHOOSER ||
		     WEXITSTATUS (status) == DISPLAY_RESTARTGDM ||
		     WEXITSTATUS (status) == DISPLAY_GREETERFAILED)) {
			exit_code_to_use = WEXITSTATUS (status);
			SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
		} else {
			if (WIFSIGNALED (status) &&
			    (WTERMSIG (status) == SIGSEGV ||
			     WTERMSIG (status) == SIGABRT ||
			     WTERMSIG (status) == SIGPIPE ||
			     WTERMSIG (status) == SIGBUS)) {
				exit_code_to_use = DISPLAY_GREETERFAILED;
				SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
			} else {
				exit_code_to_use = DISPLAY_REMANAGE;
				SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
			}
		}
	} else if (pid != 0 && pid == d->sesspid) {
		d->sesspid = 0;
	} else if (pid != 0 && pid == d->chooserpid) {
		d->chooserpid = 0;
	} else if (pid != 0 && pid == d->servpid) {
		d->servstat = SERVER_DEAD;
		d->servpid = 0;
		gdm_server_whack_lockfile (d);
		gdm_server_wipe_cookies (d);
		gdm_slave_whack_temp_auth_file ();

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
    if (old != 0)
	    seteuid (old);

    gdm_in_signal--;
}

static void
gdm_slave_handle_usr2_message (void)
{
	char buf[256];
	size_t count;
	char **vec;
	int i;

	IGNORE_EINTR (count = read (d->slave_notify_fd, buf, sizeof (buf) -1));
	if (count <= 0) {
		return;
	}

	buf[count] = '\0';

	vec = g_strsplit (buf, "\n", -1);
	if (vec == NULL) {
		return;
	}

	for (i = 0; vec[i] != NULL; i++) {
		char *s = vec[i];
		if (s[0] == GDM_SLAVE_NOTIFY_ACK) {
			gdm_got_ack = TRUE;
			g_free (gdm_ack_response);
			if (s[1] != '\0')
				gdm_ack_response = g_strdup (&s[1]);
			else
				gdm_ack_response = NULL;
		} else if (s[0] == GDM_SLAVE_NOTIFY_KEY) {
			slave_waitpid_notify_all ();
			unhandled_notifies =
				g_list_append (unhandled_notifies,
					       g_strdup (&s[1]));
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
						if (gdm_in_signal > 0) {
							exit_code_to_use = DISPLAY_REMANAGE;
							SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
						} else {
							/* FIXME: are we ever not in signal here? */
							gdm_slave_quick_exit (DISPLAY_REMANAGE);
						}
					} else {
						remanage_asap = TRUE;
					}
				}
			}
		}
	}

	g_strfreev (vec);
}

static void
gdm_slave_usr2_handler (int sig)
{
	gdm_in_signal++;
	in_usr2_signal++;

	gdm_debug ("gdm_slave_usr2_handler: %s got USR2 signal", d->name);

	gdm_slave_handle_usr2_message ();

	in_usr2_signal--;
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
	if (already_in_slave_start_jmp) {
		/* eki eki eki, this is not good,
		   should only happen if we get some io error after
		   we have gotten a SIGTERM */
		SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
	}

	gdm_in_signal++;

	/* Display is all gone */
	d->dsp = NULL;

	if ((d->type == TYPE_LOCAL ||
	     d->type == TYPE_FLEXI) &&
	    (do_xfailed_on_xio_error ||
	     d->starttime + 5 >= time (NULL))) {
		exit_code_to_use = DISPLAY_XFAILED;
	} else {
		exit_code_to_use = DISPLAY_REMANAGE;
	}

	slave_start_jmp_error_to_print =
		g_strdup_printf (_("%s: Fatal X error - Restarting %s"), 
				 "gdm_slave_xioerror_handler", d->name);

	need_to_quit_after_session_stop = TRUE;

	if (session_started) {
		SIGNAL_EXIT_WITH_JMP (d, JMP_SESSION_STOP_AND_QUIT);
	} else {
		SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
	}

	/* never reached */
	gdm_in_signal--;

	return 0;
}

static gboolean
check_for_interruption (const char *msg)
{
	/* Hell yeah we were interrupted, the greeter died */
	if (msg == NULL) {
		interrupted = TRUE;
		return TRUE;
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
                            strcmp (ParsedTimedLogin, gdm_root_user ()) != 0 &&
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
		case GDM_INTERRUPT_SUSPEND:
			if (d->console &&
			    GdmSystemMenu &&
			    ! ve_string_empty (GdmSuspend)) {
				gdm_slave_send (GDM_SOP_SUSPEND_MACHINE,
						FALSE /* wait_for_ack */);
			}
			/* Not interrupted, continue reading input,
			 * just proxy this to the master server */
			return TRUE;
		case GDM_INTERRUPT_SELECT_USER:
			gdm_verify_select_user (&msg[2]);
			break;
		default:
			break;
		}

		/* this was an interruption, if it wasn't
		 * handled then the user will just get an error as if he
		 * entered an invalid login or passward.  Seriously BEL
		 * cannot be part of a login/password really */
		interrupted = TRUE;
		return TRUE;
	}
	return FALSE;
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

    if ( ! ve_string_empty (str)) {
	    gdm_fdprintf (greeter_fd_out, "%c%c%s\n", STX, cmd, str);
    } else {
	    gdm_fdprintf (greeter_fd_out, "%c%c\n", STX, cmd);
    }

#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
    /* let the other process (greeter) do its stuff */
    sched_yield ();
#endif

    do {
      /* Skip random junk that might have accumulated */
      do {
	    c = gdm_fdgetc (greeter_fd_in);
      } while (c != EOF && c != STX);
    
      if (c == EOF ||
	  (buf = gdm_fdgets (greeter_fd_in)) == NULL) {
	      interrupted = TRUE;
	      /* things don't seem well with the greeter, it probably died */
	      return NULL;
      }
    } while (check_for_interruption (buf) && ! interrupted);

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
	    gdm_debug ("gdm_slave_quick_exit: Will kill everything from the display");

	    /* just in case we do get the XIOError,
	       don't run session_stop since we've
	       requested a quick exit */
	    session_started = FALSE;

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

	    if (extra_process > 1)
		    kill (-(extra_process), SIGTERM);
	    extra_process = 0;

	    gdm_verify_cleanup (d);
	    gdm_server_stop (d);

	    if (d->servpid > 1)
		    kill (d->servpid, SIGTERM);
	    d->servpid = 0;

	    gdm_debug ("gdm_slave_quick_exit: Killed everything from the display");
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
		IGNORE_EINTR (unlink (d->xnest_temp_auth_file));
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
			IGNORE_EINTR (unlink (d->xnest_temp_auth_file));
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
		ve_setenv ("GDM_PARENT_DISPLAY", d->xnest_disp, TRUE);
		if (d->xnest_temp_auth_file != NULL) {
			ve_setenv ("GDM_PARENT_XAUTHORITY",
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

    script = g_build_filename (dir, d->name, NULL);
    if (access (script, R_OK|X_OK) != 0) {
	    g_free (script);
	    script = NULL;
    }
    if (script == NULL &&
	! ve_string_empty (d->hostname)) {
	    script = g_build_filename (dir, d->hostname, NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL &&
	d->type == TYPE_XDMCP) {
	    script = g_build_filename (dir, "XDMCP", NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL &&
	SERVER_IS_FLEXI (d)) {
	    script = g_build_filename (dir, "Flexi", NULL);
	    if (access (script, R_OK|X_OK) != 0) {
		    g_free (script);
		    script = NULL;
	    }
    }
    if (script == NULL) {
	    script = g_build_filename (dir, "Default", NULL);
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

	IGNORE_EINTR (close (0));
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

	if ( ! pass_stdout) {
		IGNORE_EINTR (close (1));
		IGNORE_EINTR (close (2));
		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
	}

	gdm_close_all_descriptors (3 /* from */, -1 /* except */, -1 /* except2 */);

	openlog ("gdm", LOG_PID, LOG_DAEMON);

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
		if (ve_string_empty (pwent->pw_dir)) {
			ve_setenv ("HOME", "/", TRUE);
			ve_setenv ("PWD", "/", TRUE);
			IGNORE_EINTR (chdir ("/"));
		} else {
			ve_setenv ("HOME", pwent->pw_dir, TRUE);
			ve_setenv ("PWD", pwent->pw_dir, TRUE);
			IGNORE_EINTR (chdir (pwent->pw_dir));
			if (errno != 0) {
				IGNORE_EINTR (chdir ("/"));
				ve_setenv ("PWD", "/", TRUE);
			}
		}
	        ve_setenv ("SHELL", pwent->pw_shell, TRUE);
        } else {
	        ve_setenv ("HOME", "/", TRUE);
		ve_setenv ("PWD", "/", TRUE);
		IGNORE_EINTR (chdir ("/"));
	        ve_setenv ("SHELL", "/bin/sh", TRUE);
        }

	if (set_parent)
		set_xnest_parent_stuff ();

	/* some env for use with the Pre and Post scripts */
	x_servers_file = g_strconcat (GdmServAuthDir,
				      "/", d->name, ".Xservers", NULL);
	ve_setenv ("X_SERVERS", x_servers_file, TRUE);
	g_free (x_servers_file);
	if (d->type == TYPE_XDMCP)
		ve_setenv ("REMOTE_HOST", d->hostname, TRUE);

	/* Runs as root */
	if (GDM_AUTHFILE (d) != NULL)
		ve_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
	else
		ve_unsetenv ("XAUTHORITY");
        ve_setenv ("DISPLAY", d->name, TRUE);
	ve_setenv ("PATH", GdmRootPath, TRUE);
	ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
	ve_unsetenv ("MAIL");
	argv = ve_split (script);
	IGNORE_EINTR (execv (argv[0], argv));
	syslog (LOG_ERR, _("%s: Failed starting: %s"), "gdm_slave_exec_script",
		script);
	_exit (EXIT_SUCCESS);
	    
    case -1:
	if (set_parent)
		gdm_slave_whack_temp_auth_file ();
	g_free (script);
	syslog (LOG_ERR, _("%s: Can't fork script process!"), "gdm_slave_exec_script");
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
    gchar cmd, in_buffer[20];
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
        gdm_error (_("%s: Failed creating pipe"),
		   "gdm_parse_enriched_login");
      } else {
	pid = gdm_fork_extra ();

        switch (pid) {
	    
        case 0:
	    /* The child will write the username to stdout based on the DISPLAY
	       environment variable. */

            IGNORE_EINTR (close (pipe1[0]));
            if(pipe1[1] != STDOUT_FILENO) 
	      IGNORE_EINTR (dup2 (pipe1[1], STDOUT_FILENO));

	    closelog ();

	    gdm_close_all_descriptors (3 /* from */, pipe1[1] /* except */, -1 /* except2 */);

	    openlog ("gdm", LOG_PID, LOG_DAEMON);

	    /* runs as root */
	    if (GDM_AUTHFILE (display) != NULL)
		    ve_setenv ("XAUTHORITY", GDM_AUTHFILE (display), TRUE);
	    else
		    ve_unsetenv ("XAUTHORITY");
	    ve_setenv ("DISPLAY", display->name, TRUE);
	    if (display->type == TYPE_XDMCP)
		    ve_setenv ("REMOTE_HOST", display->hostname, TRUE);
	    ve_setenv ("PATH", GdmRootPath, TRUE);
	    ve_setenv ("SHELL", "/bin/sh", TRUE);
	    ve_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
	    ve_unsetenv ("MAIL");

	    argv = ve_split (str->str);
	    IGNORE_EINTR (execv (argv[0], argv));
	    gdm_error (_("%s: Failed executing: %s"),
		       "gdm_parse_enriched_login",
		       str->str);
	    _exit (EXIT_SUCCESS);
	    
        case -1:
	    gdm_error (_("%s: Can't fork script process!"),
		       "gdm_parse_enriched_login");
            IGNORE_EINTR (close (pipe1[0]));
            IGNORE_EINTR (close (pipe1[1]));
	    break;
	
        default:
	    /* The parent reads username from the pipe a chunk at a time */
            IGNORE_EINTR (close (pipe1[1]));
            g_string_truncate (str, 0);
	    do {
		    IGNORE_EINTR (in_buffer_len = read (pipe1[0], in_buffer,
							sizeof(in_buffer) - 1));
		    if (in_buffer_len > 0) {
			    in_buffer[in_buffer_len] = '\0';
			    g_string_append (str, in_buffer);
		    }
            } while (in_buffer_len > 0);

            if(str->len > 0 && str->str[str->len - 1] == '\n')
              g_string_truncate(str, str->len - 1);

            IGNORE_EINTR (close(pipe1[0]));

	    gdm_wait_for_extra (NULL);
        }
      }
    }

    return g_string_free (str, FALSE);
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
	} else if (sscanf (msg, GDM_NOTIFY_CHOOSER_BUTTON " %d", &val) == 1) {
		GdmChooserButton = val;
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_RETRYDELAY " %d", &val) == 1) {
		GdmRetryDelay = val;
	} else if (sscanf (msg, GDM_NOTIFY_DISALLOWTCP " %d", &val) == 1) {
		GdmDisallowTCP = val;
		remanage_asap = TRUE;
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
