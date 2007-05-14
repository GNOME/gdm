/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
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
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#ifdef HAVE_XFREE_XINERAMA
#include <X11/extensions/Xinerama.h>
#elif HAVE_SOLARIS_XINERAMA
#include <X11/extensions/xinerama.h>
#endif

#if defined (CAN_USE_SETPENV) && defined (HAVE_USERSEC_H)
#include <usersec.h>
#endif

#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <time.h>

#ifdef HAVE_TSOL
#include <user_attr.h>
#endif

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/get_context_list.h>
#endif /* HAVE_SELINUX */

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

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
#include "cookie.h"
#include "display.h"

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-daemon-config.h"

#include "gdm-socket-protocol.h"

#ifdef WITH_CONSOLE_KIT
#include "gdmconsolekit.h"
#endif

/* Per-slave globals */

static GdmDisplay *d                   = 0;
static gchar *login                    = NULL;
static gboolean greet                  = FALSE;
static gboolean configurator           = FALSE;
static gboolean remanage_asap          = FALSE;
static gboolean got_xfsz_signal        = FALSE;
static gboolean do_timed_login         = FALSE; /* If this is true, login the
                                                   timed login */
static gboolean do_configurator        = FALSE; /* If this is true, login as 
					         * root and start the
                                                 * configurator */
static gboolean do_cancel              = FALSE; /* If this is true, go back to
                                                   username entry & unselect
                                                   face browser (if present) */
static gboolean do_restart_greeter     = FALSE; /* If this is true, whack the
					           greeter and try again */
static gboolean restart_greeter_now    = FALSE; /* Restart_greeter_when the
                                                   SIGCHLD hits */
static gboolean always_restart_greeter = FALSE; /* Always restart greeter when
                                                   the user accepts restarts. */
static gboolean gdm_wait_for_ack       = TRUE;  /* Wait for ack on all messages
                                                   to the daemon */
static int in_session_stop             = 0;
static int in_usr2_signal              = 0;
static gboolean need_to_quit_after_session_stop = FALSE;
static int exit_code_to_use            = DISPLAY_REMANAGE;
static gboolean session_started        = FALSE;
static gboolean greeter_disabled       = FALSE;
static gboolean greeter_no_focus       = FALSE;

static uid_t logged_in_uid             = -1;
static gid_t logged_in_gid             = -1;
static int greeter_fd_out              = -1;
static int greeter_fd_in               = -1;

static gboolean interrupted            = FALSE;
static gchar *ParsedAutomaticLogin     = NULL;
static gchar *ParsedTimedLogin         = NULL;

static int gdm_in_signal               = 0;
static int gdm_normal_runlevel         = -1;
static pid_t extra_process             = 0;
static int extra_status                = 0;

#ifdef HAVE_TSOL
static gboolean have_suntsol_extension = FALSE;
#endif

static int slave_waitpid_r             = -1;
static int slave_waitpid_w             = -1;
static GSList *slave_waitpids          = NULL;

extern gboolean gdm_first_login;

/* The slavepipe (like fifo) connection, this is the write end */
extern int slave_fifo_pipe_fd;

/* wait for a GO in the SOP protocol */
extern gboolean gdm_wait_for_go;

typedef struct {
	pid_t pid;
} GdmWaitPid;

/* Local prototypes */
static gint   gdm_slave_xerror_handler (Display *disp, XErrorEvent *evt);
static gint   gdm_slave_xioerror_handler (Display *disp);
static void   gdm_slave_run (GdmDisplay *display);
static void   gdm_slave_wait_for_login (void);
static void   gdm_slave_greeter (void);
static void   gdm_slave_chooser (void);
static void   gdm_slave_session_start (void);
static void   gdm_slave_session_stop (gboolean run_post_session,
					gboolean no_shutdown_check);
static void   gdm_slave_alrm_handler (int sig);
static void   gdm_slave_term_handler (int sig);
static void   gdm_slave_usr2_handler (int sig);
static void   gdm_slave_quick_exit (gint status);
static void   gdm_slave_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static void   gdm_child_exit (gint status, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
static gint   gdm_slave_exec_script (GdmDisplay *d, const gchar *dir,
				       const char *login, struct passwd *pwent,
				       gboolean pass_stdout);
static gchar *gdm_parse_enriched_login (const gchar *s, GdmDisplay *display);
static void   gdm_slave_handle_usr2_message (void);
static void   gdm_slave_handle_notify (const char *msg);
static void   create_temp_auth_file (void);
static void   set_xnest_parent_stuff (void);
static void   check_notifies_now (void);
static void   restart_the_greeter (void);

#ifdef HAVE_TSOL
static gboolean gdm_can_i_assume_root_role (struct passwd *pwent);
#endif

gboolean gdm_is_user_valid (const char *username);

/* Yay thread unsafety */
static gboolean x_error_occurred = FALSE;
static gboolean gdm_got_ack = FALSE;
static char * gdm_ack_response = NULL;
char * gdm_ack_question_response = NULL;
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
#define DEFAULT_LANGUAGE "Default"
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

/* Notify all waitpids, make waitpids check notifies */
static void
slave_waitpid_notify (void)
{
	/* we're in no slave waitpids */
	if (slave_waitpids == NULL)
		return;

	gdm_sigchld_block_push ();

	if (slave_waitpid_w >= 0)
		VE_IGNORE_EINTR (write (slave_waitpid_w, "N", 1));

	gdm_sigchld_block_pop ();
}

/* Make sure to wrap this call with sigchld blocks */
static GdmWaitPid *
slave_waitpid_setpid (pid_t pid)
{
	int p[2];
	GdmWaitPid *wp;

	if G_UNLIKELY (pid <= 1)
		return NULL;

	wp = g_new0 (GdmWaitPid, 1);
	wp->pid = pid;

	if (slave_waitpid_r < 0) {
		if G_UNLIKELY (pipe (p) < 0) {
			gdm_error ("slave_waitpid_setpid: cannot create pipe, trying to wing it");
		} else {
			slave_waitpid_r = p[0];
			slave_waitpid_w = p[1];
		}
	}

	slave_waitpids = g_slist_prepend (slave_waitpids, wp);
	return wp;
}

static void
run_session_output (gboolean read_until_eof)
{
	char buf[256];
	int r, written;
	uid_t old;
	gid_t oldg;

	old = geteuid ();
	oldg = getegid ();

	/* make sure we can set the gid */
	NEVER_FAILS_seteuid (0);

	/* make sure we are the user when we do this,
	   for purposes of file limits and all that kind of
	   stuff */
	if G_LIKELY (logged_in_gid >= 0) {
		if G_UNLIKELY (setegid (logged_in_gid) != 0) {
			gdm_error (_("Can't set EGID to user GID"));
			NEVER_FAILS_root_set_euid_egid (old, oldg);
			return;
		}
	}

	if G_LIKELY (logged_in_uid >= 0) {
		if G_UNLIKELY (seteuid (logged_in_uid) != 0) {
			gdm_error (_("Can't set EUID to user UID"));
			NEVER_FAILS_root_set_euid_egid (old, oldg);
			return;
		}
	}

	/* the fd is non-blocking */
	for (;;) {
		VE_IGNORE_EINTR (r = read (d->session_output_fd, buf, sizeof (buf)));

		/* EOF */
		if G_UNLIKELY (r == 0) {
			VE_IGNORE_EINTR (close (d->session_output_fd));
			d->session_output_fd = -1;
			VE_IGNORE_EINTR (close (d->xsession_errors_fd));
			d->xsession_errors_fd = -1;
			break;
		}

		/* Nothing to read */
		if (r < 0 && errno == EAGAIN)
			break;

		/* some evil error */
		if G_UNLIKELY (r < 0) {
			gdm_error ("error reading from session output, closing the pipe");
			VE_IGNORE_EINTR (close (d->session_output_fd));
			d->session_output_fd = -1;
			VE_IGNORE_EINTR (close (d->xsession_errors_fd));
			d->xsession_errors_fd = -1;
			break;
		}

		if G_UNLIKELY (d->xsession_errors_bytes >= MAX_XSESSION_ERRORS_BYTES ||
			       got_xfsz_signal)
			continue;

		/* write until we succeed in writing something */
		VE_IGNORE_EINTR (written = write (d->xsession_errors_fd, buf, r));
		if G_UNLIKELY (written < 0 || got_xfsz_signal) {
			/* evil! */
			break;
		}

		/* write until we succeed in writing everything */
		while G_UNLIKELY (written < r) {
			int n;
			VE_IGNORE_EINTR (n = write (d->xsession_errors_fd, &buf[written], r-written));
			if G_UNLIKELY (n < 0 || got_xfsz_signal) {
				/* evil! */
				break;
			}
			written += n;
		}

		d->xsession_errors_bytes += r;

		if G_UNLIKELY (d->xsession_errors_bytes >= MAX_XSESSION_ERRORS_BYTES &&
			       ! got_xfsz_signal) {
			VE_IGNORE_EINTR (write (d->xsession_errors_fd,
						"\n...Too much output, ignoring rest...\n",
						strlen ("\n...Too much output, ignoring rest...\n")));
		}

		/* there wasn't more then buf available, so no need to try reading
		 * again, unless we really want to */
		if (r < sizeof (buf) && ! read_until_eof)
			break;
	}

	NEVER_FAILS_root_set_euid_egid (old, oldg);
}

static void
run_chooser_output (void)
{
	char *bf;

	if G_UNLIKELY (d->chooser_output_fd < 0)
		return;

	/* the fd is non-blocking */
	do {
		bf = gdm_fdgets (d->chooser_output_fd);
		if (bf != NULL) {
			g_free (d->chooser_last_line);
			d->chooser_last_line = bf;
		}
	} while (bf != NULL);
}

#define TIME_UNSET_P(tv) ((tv)->tv_sec == 0 && (tv)->tv_usec == 0)

/* Try to touch an authfb auth file every 12 hours.  That way if it's
 * in /tmp it doesn't get whacked by tmpwatch */
#define TRY_TO_TOUCH_TIME (60*60*12)

static struct timeval *
min_time_to_wait (struct timeval *tv)
{
	if (d->authfb) {
		time_t ct = time (NULL);
		time_t sec_to_wait;

		if (d->last_auth_touch + TRY_TO_TOUCH_TIME + 5 <= ct)
			sec_to_wait = 5;
		else
			sec_to_wait = (d->last_auth_touch + TRY_TO_TOUCH_TIME) - ct;

		if (TIME_UNSET_P (tv) ||
		    sec_to_wait < tv->tv_sec)
			tv->tv_sec = sec_to_wait;
	}
	if (TIME_UNSET_P (tv))
		return NULL;
	else
		return tv;
}

static void
try_to_touch_fb_userauth (void)
{
	if (d->authfb && d->userauth != NULL && logged_in_uid >= 0) {
		time_t ct = time (NULL);

		if (d->last_auth_touch + TRY_TO_TOUCH_TIME <= ct) {
			uid_t old;
			gid_t oldg;

			old = geteuid ();
			oldg = getegid ();

			NEVER_FAILS_seteuid (0);

			/* make sure we are the user when we do this,
			   for purposes of file limits and all that kind of
			   stuff */
			if G_LIKELY (logged_in_gid >= 0) {
				if G_UNLIKELY (setegid (logged_in_gid) != 0) {
					gdm_error ("Can't set GID to user GID");
					NEVER_FAILS_root_set_euid_egid (old, oldg);
					return;
				}
			}

			if G_LIKELY (logged_in_uid >= 0) {
				if G_UNLIKELY (seteuid (logged_in_uid) != 0) {
					gdm_error ("Can't set UID to user UID");
					NEVER_FAILS_root_set_euid_egid (old, oldg);
					return;
				}
			}

			/* This will "touch" the file */
			utime (d->userauth, NULL);

			NEVER_FAILS_root_set_euid_egid (old, oldg);

			d->last_auth_touch = ct;
		}
	}
}

/* must call slave_waitpid_setpid before calling this */
static void
slave_waitpid (GdmWaitPid *wp)
{
	if G_UNLIKELY (wp == NULL)
		return;

	gdm_debug ("slave_waitpid: waiting on %d", (int)wp->pid);

	if G_UNLIKELY (slave_waitpid_r < 0) {
		gdm_error ("slave_waitpid: no pipe, trying to wing it");

		/* This is a real stupid fallback for a real stupid case */
		while (wp->pid > 1) {
			struct timeval tv;
			/* Wait 5 seconds. */
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			select (0, NULL, NULL, NULL, min_time_to_wait (&tv));
			/* don't want to use sleep since we're using alarm
			   for pinging */

			/* try to touch an fb auth file */
			try_to_touch_fb_userauth ();

			if (d->session_output_fd >= 0)
				run_session_output (FALSE /* read_until_eof */);
			if (d->chooser_output_fd >= 0)
				run_chooser_output ();
			check_notifies_now ();
		}
		check_notifies_now ();
	} else {
		gboolean read_session_output = TRUE;

		do {
			char buf[1];
			fd_set rfds;
			int ret;
			struct timeval tv;
			int maxfd;

			FD_ZERO (&rfds);
			FD_SET (slave_waitpid_r, &rfds);
			if (read_session_output &&
			    d->session_output_fd >= 0)
				FD_SET (d->session_output_fd, &rfds);
			if (d->chooser_output_fd >= 0)
				FD_SET (d->chooser_output_fd, &rfds);

			/* unset time */
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			maxfd = MAX (slave_waitpid_r, d->session_output_fd);
			maxfd = MAX (maxfd, d->chooser_output_fd);

			ret = select (maxfd + 1, &rfds, NULL, NULL, min_time_to_wait (&tv));

			/* try to touch an fb auth file */
			try_to_touch_fb_userauth ();

			if (ret > 0) {
			       	if (FD_ISSET (slave_waitpid_r, &rfds)) {
					VE_IGNORE_EINTR (read (slave_waitpid_r, buf, 1));
				}
				if (d->session_output_fd >= 0 &&
				    FD_ISSET (d->session_output_fd, &rfds)) {
					run_session_output (FALSE /* read_until_eof */);
				}
				if (d->chooser_output_fd >= 0 &&
				    FD_ISSET (d->chooser_output_fd, &rfds)) {
					run_chooser_output ();
				}
			} else if (errno == EBADF) {
				read_session_output = FALSE;
			}
			check_notifies_now ();
		} while (wp->pid > 1);
		check_notifies_now ();
	}

	gdm_sigchld_block_push ();

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
	x_error_occurred = TRUE;
	return 0;
}

static void
whack_greeter_fds (void)
{
	if (greeter_fd_out > 0)
		VE_IGNORE_EINTR (close (greeter_fd_out));
	greeter_fd_out = -1;
	if (greeter_fd_in > 0)
		VE_IGNORE_EINTR (close (greeter_fd_in));
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

static gboolean
parent_exists (void)
{
	pid_t ppid = getppid ();
	static gboolean parent_dead = FALSE; /* once dead, always dead */

	if G_UNLIKELY (parent_dead ||
		       ppid <= 1 ||
		       kill (ppid, 0) < 0) {
		parent_dead = TRUE;
		return FALSE;
	}
	return TRUE;
}

#ifdef SIGXFSZ
static void
gdm_slave_xfsz_handler (int signal)
{
	gdm_in_signal++;

	/* in places where we care we can check
	 * and stop writing */
	got_xfsz_signal = TRUE;

	/* whack self ASAP */
	remanage_asap = TRUE;

	gdm_in_signal--;
}
#endif /* SIGXFSZ */


static int
get_runlevel (void)
{
	int rl;

	rl = -1;
#ifdef __linux__
	/* on linux we get our current runlevel, for use later
	 * to detect a shutdown going on, and not mess up. */
	if (g_access ("/sbin/runlevel", X_OK) == 0) {
		char ign;
		int rnl;
		FILE *fp = popen ("/sbin/runlevel", "r");
		if (fp != NULL) {
			if (fscanf (fp, "%c %d", &ign, &rnl) == 2) {
				rl = rnl;
			}
			pclose (fp);
		}
	}
#endif /* __linux__ */
	return rl;
}

void
gdm_slave_start (GdmDisplay *display)
{
	time_t first_time;
	int death_count;
	struct sigaction alrm, term, child, usr2;
#ifdef SIGXFSZ
	struct sigaction xfsz;
#endif /* SIGXFSZ */
	sigset_t mask;
	int pinginterval = gdm_daemon_config_get_value_int (GDM_KEY_PING_INTERVAL);

	/*
	 * Set d global to display before setting signal handlers,
	 * since the signal handlers use the d value.  Avoids a 
	 * race condition.  It is also set again in gdm_slave_run
	 * since it is called in a loop.
	 */
	d = display;

	gdm_normal_runlevel = get_runlevel ();

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
	if ( ! SERVER_IS_LOCAL (display) && pinginterval > 0) {
		sigaddset (&mask, SIGALRM);
	}
	/* must set signal mask before the Setjmp as it will be
	   restored, and we're only interested in catching the above signals */
	sigprocmask (SIG_UNBLOCK, &mask, NULL);


	if G_UNLIKELY (display == NULL) {
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

	if ( ! SERVER_IS_LOCAL (display) && pinginterval > 0) {
		/* Handle a ALRM signals from our ping alarms */
		alrm.sa_handler = gdm_slave_alrm_handler;
		alrm.sa_flags = SA_RESTART | SA_NODEFER;
		sigemptyset (&alrm.sa_mask);
		sigaddset (&alrm.sa_mask, SIGALRM);

		if G_UNLIKELY (sigaction (SIGALRM, &alrm, NULL) < 0)
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

	if G_UNLIKELY ((sigaction (SIGTERM, &term, NULL) < 0) ||
		       (sigaction (SIGINT, &term, NULL) < 0))
		gdm_slave_exit (DISPLAY_ABORT,
				_("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "TERM/INT", strerror (errno));

	/* Child handler. Keeps an eye on greeter/session */
	child.sa_handler = gdm_slave_child_handler;
	child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigemptyset (&child.sa_mask);
	sigaddset (&child.sa_mask, SIGCHLD);

	if G_UNLIKELY (sigaction (SIGCHLD, &child, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "CHLD", strerror (errno));

	/* Handle a USR2 which is ack from master that it received a message */
	usr2.sa_handler = gdm_slave_usr2_handler;
	usr2.sa_flags = SA_RESTART;
	sigemptyset (&usr2.sa_mask);
	sigaddset (&usr2.sa_mask, SIGUSR2);

	if G_UNLIKELY (sigaction (SIGUSR2, &usr2, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT, _("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "USR2", strerror (errno));

#ifdef SIGXFSZ
	/* handle the filesize signal */
	xfsz.sa_handler = gdm_slave_xfsz_handler;
	xfsz.sa_flags = SA_RESTART;
	sigemptyset (&xfsz.sa_mask);
	sigaddset (&xfsz.sa_mask, SIGXFSZ);

	if G_UNLIKELY (sigaction (SIGXFSZ, &xfsz, NULL) < 0)
		gdm_slave_exit (DISPLAY_ABORT,
				_("%s: Error setting up %s signal handler: %s"),
				"gdm_slave_start", "XFSZ", strerror (errno));
#endif /* SIGXFSZ */

	first_time = time (NULL);
	death_count = 0;

	for (;;) {
		time_t the_time;

		check_notifies_now ();

		gdm_debug ("gdm_slave_start: Loop Thingie");
		gdm_slave_run (display);

		/* remote and flexi only run once */
		if (display->type != TYPE_STATIC ||
		    ! parent_exists ()) {
			gdm_server_stop (display);
			gdm_slave_send_num (GDM_SOP_XPID, 0);
			gdm_slave_quick_exit (DISPLAY_REMANAGE);
		}

		the_time = time (NULL);

		death_count++;

		if ((the_time - first_time) <= 0 ||
		    (the_time - first_time) > 60) {
			first_time = the_time;
			death_count = 0;
		} else if G_UNLIKELY (death_count > 6) {
			gdm_slave_quick_exit (DISPLAY_ABORT);
		}

		gdm_debug ("gdm_slave_start: Reinitializing things");

		/* Whack the server because we want to restart it next
		 * time we run gdm_slave_run */
		gdm_server_stop (display);
		gdm_slave_send_num (GDM_SOP_XPID, 0);
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
	gdm_slave_exec_script (display, gdm_daemon_config_get_value_string (GDM_KEY_DISPLAY_INIT_DIR),
			       NULL, NULL, FALSE /* pass_stdout */);

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

#ifdef HAVE_TSOL
static void
gdm_tsol_init (GdmDisplay *display)
{

 	int opcode;
	int firstevent;
	int firsterror;

	have_suntsol_extension = XQueryExtension (display->dsp,
						  "SUN_TSOL",
						  &opcode,
						  &firstevent,
						  &firsterror);
}
#endif

static void
gdm_screen_init (GdmDisplay *display)
{
#ifdef HAVE_XFREE_XINERAMA
	int (* old_xerror_handler) (Display *, XErrorEvent *);
	gboolean have_xinerama = FALSE;

	x_error_occurred = FALSE;
	old_xerror_handler = XSetErrorHandler (ignore_xerror_handler);

	have_xinerama = XineramaIsActive (display->dsp);

	XSync (display->dsp, False);
	XSetErrorHandler (old_xerror_handler);

	if (x_error_occurred)
		have_xinerama = FALSE;

	if (have_xinerama) {
		int screen_num;
		int xineramascreen;
		XineramaScreenInfo *xscreens =
			XineramaQueryScreens (display->dsp,
					      &screen_num);


		if G_UNLIKELY (screen_num <= 0)
			gdm_fail ("Xinerama active, but <= 0 screens?");

		if (screen_num <= gdm_daemon_config_get_value_int (GDM_KEY_XINERAMA_SCREEN))
			gdm_daemon_config_set_value_int (GDM_KEY_XINERAMA_SCREEN, 0);

		xineramascreen = gdm_daemon_config_get_value_int (GDM_KEY_XINERAMA_SCREEN);

		display->screenx = xscreens[xineramascreen].x_org;
		display->screeny = xscreens[xineramascreen].y_org;
		display->screenwidth = xscreens[xineramascreen].width;
		display->screenheight = xscreens[xineramascreen].height;

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
		int xineramascreen;

		result = XineramaGetInfo (display->dsp, 0, monitors, hints, &n_monitors);
		/* Yes I know it should be Success but the current implementation
		 * returns the num of monitor
		 */
		if G_UNLIKELY (result <= 0)
			gdm_fail ("Xinerama active, but <= 0 screens?");

		if (n_monitors <= gdm_daemon_config_get_value_int (GDM_KEY_XINERAMA_SCREEN))
			gdm_daemon_config_set_value_int (GDM_KEY_XINERAMA_SCREEN, 0);

		xineramascreen = gdm_daemon_config_get_value_int (GDM_KEY_XINERAMA_SCREEN);
		display->screenx = monitors[xineramascreen].x;
		display->screeny = monitors[xineramascreen].y;
		display->screenwidth = monitors[xineramascreen].width;
		display->screenheight = monitors[xineramascreen].height;

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

static void
wait_for_display_to_die (Display    *display,
			 const char *display_name)
{
	fd_set rfds;
	int    fd;

	gdm_debug ("wait_for_display_to_die: waiting for display '%s' to die",
		   display_name);

	fd = ConnectionNumber (display);

	FD_ZERO (&rfds);
	FD_SET (fd, &rfds);

	while (1) {
		char           buf[256];
		struct timeval tv;
		int            n;

		tv.tv_sec  = 5;
		tv.tv_usec = 0;

		n = select (fd + 1, &rfds, NULL, NULL, &tv);
		if (G_LIKELY (n == 0)) {
			XSync (display, True);
		} else if (n > 0) {
			VE_IGNORE_EINTR (n = read (fd, buf, sizeof (buf)));
			if (n <= 0)
				break;
		} else if (errno != EINTR) {
			break;
		}

		FD_CLR (fd, &rfds);
	}

	gdm_debug ("wait_for_display_to_die: '%s' dead", display_name);
}

static int
ask_migrate (const char *migrate_to)
{
	int   r;
	char *msg;
	char *but[4];
	char *askbuttons_msg;

	/*
	 * If migratable and ALWAYS_LOGIN_CURRENT_SESSION is true, then avoid 
	 * the dialog.
	 */
	if (migrate_to != NULL &&
	    gdm_daemon_config_get_value_bool (GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION)) {
		return 1;
	}

	/*
	 * Avoid dialog if DOUBLE_LOGIN_WARNING is false.  In this case
	 * ALWAYS_LOGIN_CURRENT_SESSION is false, so assume new session.
	 */
	if (!gdm_daemon_config_get_value_bool (GDM_KEY_DOUBLE_LOGIN_WARNING)) {
		return 0;
	}

	but[0] = _("Log in anyway");
	if (migrate_to != NULL) {
		msg = _("You are already logged in.  "
			"You can log in anyway, return to your "
			"previous login session, or abort this "
			"login");
		but[1] = _("Return to previous login");
		but[2] = _("Abort login");
		but[3] = "NIL";
	} else {
		msg = _("You are already logged in.  "
			"You can log in anyway or abort this "
			"login");
		but[1] = _("Abort login");
		but[2] = "NIL";
		but[3] = "NIL";
	}

	if (greet)
		gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");

	askbuttons_msg = g_strdup_printf ("askbuttons_msg=%s$$options_msg1=%s$$options_msg2=%s$$options_msg3=%s$$options_msg4=%s", msg, but[0], but[1], but[2], but[3]);


	gdm_slave_send_string (GDM_SOP_SHOW_ASKBUTTONS_DIALOG, askbuttons_msg);

	r = atoi (gdm_ack_response);

	g_free (askbuttons_msg);
	g_free (gdm_ack_response);
	gdm_ack_response = NULL;

	if (greet)
		gdm_slave_greeter_ctl_no_ret (GDM_ENABLE, "");

	return r;
}

gboolean
gdm_slave_check_user_wants_to_log_in (const char *user)
{
	gboolean loggedin = FALSE;
	int i;
	char **vec;
	char *migrate_to = NULL;

	/* always ignore root here, this is mostly a special case
	 * since a root login may not be a real login, such as the
	 * config stuff, and people shouldn't log in as root anyway
	 */
	if (strcmp (user, gdm_root_user ()) == 0)
		return TRUE;

	gdm_slave_send_string (GDM_SOP_QUERYLOGIN, user);
	if G_LIKELY (ve_string_empty (gdm_ack_response))
		return TRUE;
	vec = g_strsplit (gdm_ack_response, ",", -1);
	if (vec == NULL)
		return TRUE;

	gdm_debug ("QUERYLOGIN response: %s\n", gdm_ack_response);

	for (i = 0; vec[i] != NULL && vec[i+1] != NULL; i += 2) {
		int ii;
		loggedin = TRUE;
		if (sscanf (vec[i+1], "%d", &ii) == 1 && ii == 1) {
			migrate_to = g_strdup (vec[i]);
			break;
		}
	}

	g_strfreev (vec);

	if ( ! loggedin)
		return TRUE;

	if (d->type != TYPE_XDMCP_PROXY) {
		int r;

		r = ask_migrate (migrate_to);

		if (r <= 0) {
			g_free (migrate_to);
			return TRUE;
		}

		/*
		 * migrate_to should never be NULL here, since
		 * ask_migrate will always return 0 or 1 if migrate_to
		 * is NULL.
		 */
		if (migrate_to == NULL ||
		    (migrate_to != NULL && r == 2)) {
			g_free (migrate_to);
			return FALSE;
		}

		/* Must be that r == 1, that is return to previous login */

		if (d->type == TYPE_FLEXI) {
			gdm_slave_whack_greeter ();
			gdm_server_stop (d);
			gdm_slave_send_num (GDM_SOP_XPID, 0);

			/* wait for a few seconds to avoid any vt changing race
			 */
			gdm_sleep_no_signal (1);

#ifdef WITH_CONSOLE_KIT
			unlock_ck_session (user, migrate_to);
#endif

			gdm_slave_send_string (GDM_SOP_MIGRATE, migrate_to);
			g_free (migrate_to);

			/* we are no longer needed so just die.
			   REMANAGE == ABORT here really */
			gdm_slave_quick_exit (DISPLAY_REMANAGE);
		}

		gdm_slave_send_string (GDM_SOP_MIGRATE, migrate_to);
		g_free (migrate_to);
	} else {
		Display *parent_dsp;

		if (migrate_to == NULL)
			return TRUE;

		gdm_slave_send_string (GDM_SOP_MIGRATE, migrate_to);
		g_free (migrate_to);

		/*
		 * We must stay running and hold open our connection to the
		 * parent display because with XDMCP the Xserver resets when
		 * the initial X client closes its connection (rather than
		 * when *all* X clients have closed their connection)
		 */

		gdm_slave_whack_greeter ();

		parent_dsp = d->parent_dsp;
		d->parent_dsp = NULL;
		gdm_server_stop (d);

		gdm_slave_send_num (GDM_SOP_XPID, 0);

		gdm_debug ("Slave not exiting in order to hold open the connection to the parent display");

		wait_for_display_to_die (d->parent_dsp, d->parent_disp);

		gdm_slave_quick_exit (DISPLAY_ABORT);
	}

	/* abort this login attempt */
	return FALSE;
}

static gboolean do_xfailed_on_xio_error = FALSE;

static void
gdm_slave_run (GdmDisplay *display)
{
	gint openretries = 0;
	gint maxtries = 0;
	gint pinginterval = gdm_daemon_config_get_value_int (GDM_KEY_PING_INTERVAL);

	gdm_reset_locale ();

	/* Reset d since gdm_slave_run is called in a loop */
	d = display;

	gdm_random_tick ();

	if (d->sleep_before_run > 0) {
		gdm_debug ("gdm_slave_run: Sleeping %d seconds before server start", d->sleep_before_run);
		gdm_sleep_no_signal (d->sleep_before_run);
		d->sleep_before_run = 0;

		check_notifies_now ();
	}

	/*
	 * Set it before we run the server, it may be that we're using
	 * the XOpenDisplay to find out if a server is ready (as with
	 * nested display)
	 */
	d->dsp = NULL;

	/* if this is local display start a server if one doesn't
	 * exist */
	if (SERVER_IS_LOCAL (d) &&
	    d->servpid <= 0) {
		if G_UNLIKELY ( ! gdm_server_start (d,
						    TRUE /* try_again_if_busy */,
						    FALSE /* treat_as_flexi */,
						    20 /* min_flexi_disp */,
						    5 /* flexi_retries */)) {
			/* We're really not sure what is going on,
			 * so we throw up our hands and tell the user
			 * that we've given up.  The error is likely something
			 * internal. */
			gdm_text_message_dialog
				(C_(N_("Could not start the X\n"
				       "server (your graphical environment)\n"
				       "due to some internal error.\n"
				       "Please contact your system administrator\n"
				       "or check your syslog to diagnose.\n"
				       "In the meantime this display will be\n"
				       "disabled.  Please restart GDM when\n"
				       "the problem is corrected.")));
			gdm_slave_quick_exit (DISPLAY_ABORT);
		}
		gdm_slave_send_num (GDM_SOP_XPID, d->servpid);

		check_notifies_now ();
	}

	/* We can use d->handled from now on on this display,
	 * since the lookup was done in server start */

	g_setenv ("DISPLAY", d->name, TRUE);
	g_unsetenv ("XAUTHORITY"); /* just in case it's set */

	gdm_auth_set_local_auth (d);

	if (d->handled) {
		/* Now the display name and hostname is final */

		const char *automaticlogin = gdm_daemon_config_get_value_string (GDM_KEY_AUTOMATIC_LOGIN);
		const char *timedlogin     = gdm_daemon_config_get_value_string (GDM_KEY_TIMED_LOGIN);

		if (gdm_daemon_config_get_value_bool (GDM_KEY_AUTOMATIC_LOGIN_ENABLE) &&
		    ! ve_string_empty (automaticlogin)) {
			g_free (ParsedAutomaticLogin);
			ParsedAutomaticLogin = gdm_parse_enriched_login (automaticlogin,
									 display);
		}

		if (gdm_daemon_config_get_value_bool (GDM_KEY_TIMED_LOGIN_ENABLE) &&
		    ! ve_string_empty (timedlogin)) {
			g_free (ParsedTimedLogin);
			ParsedTimedLogin = gdm_parse_enriched_login (timedlogin,
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

		gdm_sigchld_block_push ();
		d->dsp = XOpenDisplay (d->name);
		gdm_sigchld_block_pop ();

		if G_UNLIKELY (d->dsp == NULL) {
			gdm_debug ("gdm_slave_run: Sleeping %d on a retry", 1+openretries*2);
			gdm_sleep_no_signal (1+openretries*2);
			openretries++;
		}
	}

	/* Really this will only be useful for the first local server,
	   since that's the only time this can really be on */
	while G_UNLIKELY (gdm_wait_for_go) {
		struct timeval tv;
		/* Wait 1 second. */
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		select (0, NULL, NULL, NULL, &tv);
		/* don't want to use sleep since we're using alarm
		   for pinging */
		check_notifies_now ();
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
	if G_UNLIKELY (d->handled && d->dsp == NULL) {
		if (d->type == TYPE_STATIC)
			gdm_slave_quick_exit (DISPLAY_XFAILED);
		else
			gdm_slave_quick_exit (DISPLAY_ABORT);
	}

	/* OK from now on it's really the user whacking us most likely,
	 * we have already started up well */
	do_xfailed_on_xio_error = FALSE;

	/* If XDMCP setup pinging */
	if ( ! SERVER_IS_LOCAL (d) && pinginterval > 0) {
		alarm (pinginterval);
	}

	/* checkout xinerama */
	if (d->handled)
		gdm_screen_init (d);

#ifdef HAVE_TSOL
	/* Check out Solaris Trusted Xserver extension */
	if (d->handled)
		gdm_tsol_init (d);
#endif

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
	} else if (d->type == TYPE_STATIC &&
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
		logged_in_uid = -1;
		logged_in_gid = -1;

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
		logged_in_uid = -1;
		logged_in_gid = -1;

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
	if ( ! SERVER_IS_LOCAL (d))
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

	if G_UNLIKELY (pipe (p) < 0) {
		p[0] = -1;
		p[1] = -1;
	}

	g_debug ("Forking process to focus first X11 window");

	pid = fork ();
	if G_UNLIKELY (pid < 0) {
		if (p[0] != -1)
			VE_IGNORE_EINTR (close (p[0]));
		if (p[1] != -1)
			VE_IGNORE_EINTR (close (p[1]));
		gdm_error (_("%s: cannot fork"), "focus_first_x_window");
		return;
	}
	/* parent */
	if (pid > 0) {
		/* Wait for this subprocess to start-up */
		if (p[0] >= 0) {
			fd_set rfds;
			struct timeval tv;

			VE_IGNORE_EINTR (close (p[1]));

			FD_ZERO(&rfds);
			FD_SET(p[0], &rfds);

			/* Wait up to 2 seconds. */
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			select (p[0]+1, &rfds, NULL, NULL, &tv);

			VE_IGNORE_EINTR (close (p[0]));
		}
		return;
	}

	gdm_unset_signals ();

	gdm_log_shutdown ();

	gdm_close_all_descriptors (0 /* from */, p[1] /* except */, -1 /* except2 */);

	/* No error checking here - if it's messed the best response
         * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	gdm_log_init ();

	/* just in case it's set */
	g_unsetenv ("XAUTHORITY");

	gdm_auth_set_local_auth (d);

	gdm_sigchld_block_push ();
	disp = XOpenDisplay (d->name);
	gdm_sigchld_block_pop ();
	if G_UNLIKELY (disp == NULL) {
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

	if G_LIKELY (p[1] >= 0) {
		VE_IGNORE_EINTR (write (p[1], "!", 1));
		VE_IGNORE_EINTR (close (p[1]));
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

	/* Lets check if custom.conf exists. If not there
	   is no point in launching gdmsetup as it will fail.
	   We don't need to worry about defaults.conf as
	   the daemon wont start without it
	*/
	if (gdm_daemon_config_get_custom_config_file () == NULL) {
		gdm_errorgui_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Could not access configuration file (custom.conf). "
				 "Make sure that the file exists before launching "
				 " login manager config utility."));
		return;
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

	g_debug ("Forking GDM configuration process");

	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();
	pid = d->sesspid = fork ();
	if (pid == 0)
		gdm_unset_signals ();
	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	if G_UNLIKELY (pid < 0) {
		/* Return left pointer */
		Cursor xcursor;

		/* Can't fork */
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
		char **argv = NULL;
		const char *s;

		/* child */

		setsid ();

		gdm_unset_signals ();

		setuid (0);
		setgid (0);
		gdm_desetuid ();

		/* setup environment */
		gdm_restoreenv ();
		gdm_reset_locale ();

		/* root here */
		g_setenv ("XAUTHORITY", GDM_AUTHFILE (display), TRUE);
		g_setenv ("DISPLAY", display->name, TRUE);
		g_setenv ("LOGNAME", pwent->pw_name, TRUE);
		g_setenv ("USER", pwent->pw_name, TRUE);
		g_setenv ("USERNAME", pwent->pw_name, TRUE);
		g_setenv ("HOME", pwent->pw_dir, TRUE);
		g_setenv ("SHELL", pwent->pw_shell, TRUE);
		g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);
		g_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
		if ( ! ve_string_empty (display->theme_name))
			g_setenv ("GDM_GTK_THEME", display->theme_name, TRUE);

		gdm_log_shutdown ();

		gdm_close_all_descriptors (0 /* from */, slave_fifo_pipe_fd /* except */, d->slave_notify_fd /* except2 */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		gdm_log_init ();

		VE_IGNORE_EINTR (g_chdir (pwent->pw_dir));
		if G_UNLIKELY (errno != 0)
			VE_IGNORE_EINTR (g_chdir ("/"));

		/* exec the configurator */
		s = gdm_daemon_config_get_value_string (GDM_KEY_CONFIGURATOR);
		if (s != NULL) {
			g_shell_parse_argv (s, NULL, &argv, NULL);
		}

		if G_LIKELY (argv != NULL &&
			     argv[0] != NULL &&
			     g_access (argv[0], X_OK) == 0) {
			VE_IGNORE_EINTR (execv (argv[0], argv));
		}

		g_strfreev (argv);

		gdm_errorgui_error_box (d,
					GTK_MESSAGE_ERROR,
					_("Could not execute the configuration "
					  "application.  Make sure its path is set "
					  "correctly in the configuration file.  "
					  "Attempting to start it from the default "
					  "location."));
		s = LIBEXECDIR "/gdmsetup --disable-sound --disable-crash-dialog";
		argv = NULL;
		g_shell_parse_argv (s, NULL, &argv, NULL);

		if (g_access (argv[0], X_OK) == 0) {
			VE_IGNORE_EINTR (execv (argv[0], argv));
		}

		g_strfreev (argv);

		gdm_errorgui_error_box (d,
					GTK_MESSAGE_ERROR,
					_("Could not execute the configuration "
					  "application.  Make sure its path is set "
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
	do_restart_greeter = FALSE;

	gdm_slave_desensitize_config ();

	/* no login */
	g_free (login);
	login = NULL;

	/* Now restart it */
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

static gboolean
play_login_sound (const char *sound_file)
{
	const char *soundprogram = gdm_daemon_config_get_value_string (GDM_KEY_SOUND_PROGRAM);
	pid_t pid;

	if (ve_string_empty (soundprogram) ||
	    ve_string_empty (sound_file) ||
	    g_access (soundprogram, X_OK) != 0 ||
	    g_access (sound_file, F_OK) != 0)
		return FALSE;

	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();

	g_debug ("Forking sound program: %s", soundprogram);

	pid = fork ();
	if (pid == 0)
		gdm_unset_signals ();

	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	if (pid == 0) {
		setsid ();
		seteuid (0);
		setegid (0);
		execl (soundprogram,
		       soundprogram,
		       sound_file,
		       NULL);
		_exit (0);
	}

	return TRUE;
}

static void
gdm_slave_wait_for_login (void)
{
	const char *successsound;
	char *username;
	g_free (login);
	login = NULL;

	/* Chat with greeter */
	while (login == NULL) {
		/* init to a sane value */
		do_timed_login = FALSE;
		do_configurator = FALSE;
		do_cancel = FALSE;

		if G_UNLIKELY (do_restart_greeter) {
			do_restart_greeter = FALSE;
			restart_the_greeter ();
		}

		/* We are NOT interrupted yet */
		interrupted = FALSE;

		check_notifies_now ();

		/* just for paranoia's sake */
		NEVER_FAILS_root_set_euid_egid (0, 0);

		gdm_debug ("gdm_slave_wait_for_login: In loop");
		username = d->preset_user;
		d->preset_user = NULL;
		login = gdm_verify_user (d /* the display */,
					 username /* username */,
					 d->name /* display name */,
					 d->attached /* display attached? */,
					 TRUE /* allow retry */);
		g_free (username);

		gdm_debug ("gdm_slave_wait_for_login: end verify for '%s'",
			   ve_sure_string (login));

		/* Complex, make sure to always handle the do_configurator
		 * do_timed_login and do_restart_greeter after any call
		 * to gdm_verify_user */

		if G_UNLIKELY (do_restart_greeter) {
			g_free (login);
			login = NULL;
			do_restart_greeter = FALSE;
			restart_the_greeter ();
			continue;
		}

		check_notifies_now ();

		if G_UNLIKELY (do_configurator) {
			struct passwd *pwent;
			gboolean oldAllowRoot;

			do_configurator = FALSE;
			g_free (login);
			login = NULL;
			/* clear any error */
			gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, "");
			gdm_slave_greeter_ctl_no_ret
				(GDM_MSG,
				 _("You must authenticate as root to run configuration."));

			/* we always allow root for this */
			oldAllowRoot = gdm_daemon_config_get_value_bool (GDM_KEY_ALLOW_ROOT);
			gdm_daemon_config_set_value_bool (GDM_KEY_ALLOW_ROOT, TRUE);

			pwent = getpwuid (0);
			if G_UNLIKELY (pwent == NULL) {
				/* what? no "root" ?? */
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, pwent->pw_name);
			login = gdm_verify_user (d,
						 pwent->pw_name,
						 d->name,
						 d->attached,
						 FALSE);
			gdm_daemon_config_set_value_bool (GDM_KEY_ALLOW_ROOT, oldAllowRoot);

			/* Clear message */
			gdm_slave_greeter_ctl_no_ret (GDM_MSG, "");

			if G_UNLIKELY (do_restart_greeter) {
				g_free (login);
				login = NULL;
				do_restart_greeter = FALSE;
				restart_the_greeter ();
				continue;
			}

			check_notifies_now ();

			/* The user can't remember his password */
			if (login == NULL) {
				gdm_debug ("gdm_slave_wait_for_login: No login/Bad login");
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			/* Wipe the login */
			g_free (login);
			login = NULL;

			/* Note that this can still fall through to
			 * the timed login if the user doesn't type in the
			 * password fast enough and there is timed login
			 * enabled */
			if (do_timed_login) {
				break;
			}

			if G_UNLIKELY (do_configurator) {
				do_configurator = FALSE;
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			/* Now running as root */

			/* Get the root pwent */
			pwent = getpwuid (0);

			if G_UNLIKELY (pwent == NULL) {
				/* What?  No "root" ??  This is not possible
				 * since we logged in, but I'm paranoid */
				gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");
				continue;
			}

			d->logged_in = TRUE;
			logged_in_uid = 0;
			logged_in_gid = 0;
			gdm_slave_send_num (GDM_SOP_LOGGED_IN, TRUE);
			/* Note: nobody really logged in */
			gdm_slave_send_string (GDM_SOP_LOGIN, "");

			/* Disable the login screen, we don't want people to
			 * log in in the meantime */
			gdm_slave_greeter_ctl_no_ret (GDM_DISABLE, "");
			greeter_disabled = TRUE;

			/* Make the login screen not focusable */
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
			logged_in_uid = -1;
			logged_in_gid = -1;

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

		/* The user timed out into a timed login during the
		 * conversation */
		if (do_timed_login) {
			break;
		}

		if (login == NULL) {
			const char *failuresound = gdm_daemon_config_get_value_string (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

			gdm_debug ("gdm_slave_wait_for_login: No login/Bad login");
			gdm_slave_greeter_ctl_no_ret (GDM_RESET, "");

			/* Play sounds if specified for a failed login */
			if (d->attached && failuresound &&
			    gdm_daemon_config_get_value_bool (GDM_KEY_SOUND_ON_LOGIN_FAILURE) &&
			    ! play_login_sound (failuresound)) {
				gdm_error (_("Login sound requested on non-local display or the play "
					     "software cannot be run or the sound does not exist."));
			}
		}
	}

	/* The user timed out into a timed login during the conversation */
	if (do_timed_login) {
		g_free (login);
		login = NULL;
		/* timed login is automatic, thus no need for greeter,
		 * we'll take default values */
		gdm_slave_whack_greeter ();

		gdm_debug ("gdm_slave_wait_for_login: Timed Login");
	}

	successsound = gdm_daemon_config_get_value_string (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
	/* Play sounds if specified for a successful login */
	if (login != NULL && successsound &&
	    gdm_daemon_config_get_value_bool (GDM_KEY_SOUND_ON_LOGIN_SUCCESS) &&
	    d->attached &&
	    ! play_login_sound (successsound)) {
		gdm_error (_("Login sound requested on non-local display or the play software "
			     "cannot be run or the sound does not exist."));
	}

	gdm_debug ("gdm_slave_wait_for_login: got_login for '%s'",
		   ve_sure_string (login));


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
	FILE *fp;

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
		if G_UNLIKELY (pwent == NULL) {
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		picfile = NULL;

		NEVER_FAILS_seteuid (0);
		if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
			       seteuid (pwent->pw_uid) != 0) {
			NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		picfile = gdm_daemon_config_get_facefile_from_home (pwent->pw_dir, pwent->pw_uid);

		if (! picfile)
			picfile = gdm_daemon_config_get_facefile_from_global (pwent->pw_name, pwent->pw_uid);

		if (! picfile) {
			NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());
			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		VE_IGNORE_EINTR (r = g_stat (picfile, &s));
		if G_UNLIKELY (r < 0 || s.st_size > gdm_daemon_config_get_value_int (GDM_KEY_USER_MAX_FILE)) {
			NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		VE_IGNORE_EINTR (fp = fopen (picfile, "r"));
		g_free (picfile);
		if G_UNLIKELY (fp == NULL) {
			NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());

			gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "");
			continue;
		}

		tmp = g_strdup_printf ("buffer:%d", (int)s.st_size);
		ret = gdm_slave_greeter_ctl (GDM_READPIC, tmp);
		g_free (tmp);

		if G_UNLIKELY (ret == NULL || strcmp (ret, "OK") != 0) {
			VE_IGNORE_EINTR (fclose (fp));
			g_free (ret);

			NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());

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
		while (i < s.st_size) {
			int written;

			VE_IGNORE_EINTR (bytes = fread (buf, sizeof (char),
							max_write, fp));

			if (bytes <= 0)
				break;

			if G_UNLIKELY (i + bytes > s.st_size)
				bytes = s.st_size - i;

			/* write until we succeed in writing something */
			VE_IGNORE_EINTR (written = write (greeter_fd_out, buf, bytes));
			if G_UNLIKELY (written < 0 &&
				       (errno == EPIPE || errno == EBADF)) {
				/* something very, very bad has happened */
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}

			if G_UNLIKELY (written < 0)
				written = 0;

			/* write until we succeed in writing everything */
			while (written < bytes) {
				int n;
				VE_IGNORE_EINTR (n = write (greeter_fd_out, &buf[written], bytes-written));
				if G_UNLIKELY (n < 0 &&
					       (errno == EPIPE || errno == EBADF)) {
					/* something very, very bad has happened */
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				} else if G_LIKELY (n > 0) {
					written += n;
				}
			}

			/* we have written bytes bytes if it likes it or not */
			i += bytes;
		}

		VE_IGNORE_EINTR (fclose (fp));

		/* eek, this "could" happen, so just send some garbage */
		while G_UNLIKELY (i < s.st_size) {
			bytes = MIN (sizeof (buf), s.st_size - i);
			errno = 0;
			bytes = write (greeter_fd_out, buf, bytes);
			if G_UNLIKELY (bytes < 0 && (errno == EPIPE || errno == EBADF)) {
				/* something very, very bad has happened */
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
			if (bytes > 0)
				i += bytes;
		}

		gdm_slave_greeter_ctl_no_ret (GDM_READPIC, "done");

		NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());
	}
	g_free (response); /* not reached */
}

/* hakish, copy file (owned by fromuid) to a temp file owned by touid */
static char *
copy_auth_file (uid_t fromuid, uid_t touid, const char *file)
{
	uid_t old = geteuid ();
	gid_t oldg = getegid ();
	char *name;
	int authfd;
	int fromfd;
	int bytes;
	char buf[2048];
	int cnt;

	NEVER_FAILS_seteuid (0);
	NEVER_FAILS_setegid (gdm_daemon_config_get_gdmgid ());

	if G_UNLIKELY (seteuid (fromuid) != 0) {
		NEVER_FAILS_root_set_euid_egid (old, oldg);
		return NULL;
	}

	if ( ! gdm_auth_file_check ("copy_auth_file", fromuid,
				    file, FALSE /* absentok */, NULL)) {
		NEVER_FAILS_root_set_euid_egid (old, oldg);
		return NULL;
	}

	do {
		errno = 0;
		fromfd = open (file, O_RDONLY
#ifdef O_NOCTTY
			       |O_NOCTTY
#endif
#ifdef O_NOFOLLOW
			       |O_NOFOLLOW
#endif
			       );
	} while G_UNLIKELY (errno == EINTR);

	if G_UNLIKELY (fromfd < 0) {
		NEVER_FAILS_root_set_euid_egid (old, oldg);
		return NULL;
	}

	NEVER_FAILS_root_set_euid_egid (0, 0);

	name = gdm_make_filename (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR),
				  d->name, ".XnestAuth");

	VE_IGNORE_EINTR (g_unlink (name));
	VE_IGNORE_EINTR (authfd = open (name, O_EXCL|O_TRUNC|O_WRONLY|O_CREAT, 0600));

	if G_UNLIKELY (authfd < 0) {
		VE_IGNORE_EINTR (close (fromfd));
		NEVER_FAILS_root_set_euid_egid (old, oldg);
		g_free (name);
		return NULL;
	}

	VE_IGNORE_EINTR (fchown (authfd, touid, -1));

	cnt = 0;
	for (;;) {
		int written, n;
		VE_IGNORE_EINTR (bytes = read (fromfd, buf, sizeof (buf)));

		/* EOF */
		if (bytes == 0)
			break;

		if G_UNLIKELY (bytes < 0) {
			/* Error reading */
			gdm_error ("Error reading %s: %s", file, strerror (errno));
			VE_IGNORE_EINTR (close (fromfd));
			VE_IGNORE_EINTR (close (authfd));
			NEVER_FAILS_root_set_euid_egid (old, oldg);
			g_free (name);
			return NULL;
		}

		written = 0;
		do {
			VE_IGNORE_EINTR (n = write (authfd, &buf[written], bytes-written));
			if G_UNLIKELY (n < 0) {
				/* Error writing */
				gdm_error ("Error writing %s: %s", name, strerror (errno));
				VE_IGNORE_EINTR (close (fromfd));
				VE_IGNORE_EINTR (close (authfd));
				NEVER_FAILS_root_set_euid_egid (old, oldg);
				g_free (name);
				return NULL;
			}
			written += n;
		} while (written < bytes);

		cnt = cnt + written;
		/* this should never occur (we check above)
		   but we're paranoid) */
		if G_UNLIKELY (cnt > gdm_daemon_config_get_value_int (GDM_KEY_USER_MAX_FILE))
			return NULL;
	}

	VE_IGNORE_EINTR (close (fromfd));
	VE_IGNORE_EINTR (close (authfd));

	NEVER_FAILS_root_set_euid_egid (old, oldg);

	return name;
}

static void
exec_command (const char *command, const char *extra_arg)
{
	char **argv;
	int argc;

	if (! g_shell_parse_argv (command, &argc, &argv, NULL)) {
		return;
	}

	if (argv == NULL ||
	    ve_string_empty (argv[0]))
		return;

	if (g_access (argv[0], X_OK) != 0)
		return;

	if (extra_arg != NULL) {

		argv           = g_renew (char *, argv, argc + 2);
		argv[argc]     = g_strdup (extra_arg);
		argv[argc + 1] = NULL;
	}

	VE_IGNORE_EINTR (execv (argv[0], argv));
	g_strfreev (argv);
}

static void
gdm_slave_greeter (void)
{
	gint pipe1[2], pipe2[2];
	struct passwd *pwent;
	pid_t pid;
	const char *command;
	const char *defaultpath;
	const char *gdmuser;
	const char *moduleslist;
	const char *gdmlang;

	gdm_debug ("gdm_slave_greeter: Running greeter on %s", d->name);

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (d, gdm_daemon_config_get_value_string (GDM_KEY_DISPLAY_INIT_DIR),
			       NULL, NULL, FALSE /* pass_stdout */);

	/* Open a pipe for greeter communications */
	if G_UNLIKELY (pipe (pipe1) < 0)
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't init pipe to gdmgreeter"),
				"gdm_slave_greeter");
	if G_UNLIKELY (pipe (pipe2) < 0) {
		VE_IGNORE_EINTR (close (pipe1[0]));
		VE_IGNORE_EINTR (close (pipe1[1]));
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't init pipe to gdmgreeter"),
				"gdm_slave_greeter");
	}

	/* hackish ain't it */
	create_temp_auth_file ();

	if (d->attached)
		command = gdm_daemon_config_get_value_string (GDM_KEY_GREETER);
	else
		command = gdm_daemon_config_get_value_string (GDM_KEY_REMOTE_GREETER);

	g_debug ("Forking greeter process: %s", command);

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
		VE_IGNORE_EINTR (close (pipe1[1]));
		VE_IGNORE_EINTR (close (pipe2[0]));

		VE_IGNORE_EINTR (dup2 (pipe1[0], STDIN_FILENO));
		VE_IGNORE_EINTR (dup2 (pipe2[1], STDOUT_FILENO));

		gdm_log_shutdown ();

		gdm_close_all_descriptors (2 /* from */, slave_fifo_pipe_fd/* except */, d->slave_notify_fd/* except2 */);

		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		gdm_log_init ();

		if G_UNLIKELY (setgid (gdm_daemon_config_get_gdmgid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set groupid to %d"),
					"gdm_slave_greeter", gdm_daemon_config_get_gdmgid ());

		gdmuser = gdm_daemon_config_get_value_string (GDM_KEY_USER);
		if G_UNLIKELY (initgroups (gdmuser, gdm_daemon_config_get_gdmgid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: initgroups () failed for %s"),
					"gdm_slave_greeter", gdmuser);

		if G_UNLIKELY (setuid (gdm_daemon_config_get_gdmuid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set userid to %d"),
					"gdm_slave_greeter", gdm_daemon_config_get_gdmuid ());

		gdm_restoreenv ();
		gdm_reset_locale ();

		g_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		g_setenv ("DISPLAY", d->name, TRUE);

		/* hackish ain't it */
		set_xnest_parent_stuff ();

		g_setenv ("LOGNAME", gdmuser, TRUE);
		g_setenv ("USER", gdmuser, TRUE);
		g_setenv ("USERNAME", gdmuser, TRUE);
		g_setenv ("GDM_GREETER_PROTOCOL_VERSION",
			  GDM_GREETER_PROTOCOL_VERSION, TRUE);
		g_setenv ("GDM_VERSION", VERSION, TRUE);

		pwent = getpwnam (gdmuser);
		if G_LIKELY (pwent != NULL) {
			/* Note that usually this doesn't exist */
			if (pwent->pw_dir != NULL &&
			    g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
				g_setenv ("HOME", pwent->pw_dir, TRUE);
			else
				g_setenv ("HOME",
					  ve_sure_string (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR)),
					  TRUE); /* Hack */
			g_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			g_setenv ("HOME",
				  ve_sure_string (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR)),
				  TRUE); /* Hack */
			g_setenv ("SHELL", "/bin/sh", TRUE);
		}

		defaultpath = gdm_daemon_config_get_value_string (GDM_KEY_PATH);
		if (ve_string_empty (g_getenv ("PATH"))) {
			g_setenv ("PATH", defaultpath, TRUE);
		} else if ( ! ve_string_empty (defaultpath)) {
			gchar *temp_string = g_strconcat (g_getenv ("PATH"),
							  ":", defaultpath, NULL);
			g_setenv ("PATH", temp_string, TRUE);
			g_free (temp_string);
		}
		g_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
		if ( ! ve_string_empty (d->theme_name))
			g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);

		if (gdm_daemon_config_get_value_bool (GDM_KEY_DEBUG_GESTURES)) {
			g_setenv ("GDM_DEBUG_GESTURES", "true", TRUE);
		}

		/* Note that this is just informative, the slave will not listen to
		 * the greeter even if it does something it shouldn't on a non-local
		 * display so it's not a security risk */
		if (d->attached) {
			g_setenv ("GDM_IS_LOCAL", "yes", TRUE);
		} else {
			g_unsetenv ("GDM_IS_LOCAL");
		}

		/* this is again informal only, if the greeter does time out it will
		 * not actually login a user if it's not enabled for this display */
		if (d->timed_login_ok) {
			if (ParsedTimedLogin == NULL)
				g_setenv ("GDM_TIMED_LOGIN_OK", " ", TRUE);
			else
				g_setenv ("GDM_TIMED_LOGIN_OK", ParsedTimedLogin, TRUE);
		} else {
			g_unsetenv ("GDM_TIMED_LOGIN_OK");
		}

		if (SERVER_IS_FLEXI (d)) {
			g_setenv ("GDM_FLEXI_SERVER", "yes", TRUE);
		} else {
			g_unsetenv ("GDM_FLEXI_SERVER");
		}

		if G_UNLIKELY (d->is_emergency_server) {
			gdm_errorgui_error_box (d,
						GTK_MESSAGE_ERROR,
						_("No servers were defined in the "
						  "configuration file and XDMCP was "
						  "disabled.  This can only be a "
						  "configuration error.  GDM has started "
						  "a single server for you.  You should "
						  "log in and fix the configuration.  "
						  "Note that automatic and timed logins "
						  "are disabled now."));
			g_unsetenv ("GDM_TIMED_LOGIN_OK");
		}

		if G_UNLIKELY (d->failsafe_xserver) {
			gdm_errorgui_error_box (d,
						GTK_MESSAGE_ERROR,
						_("Could not start the regular X "
						  "server (your graphical environment) "
						  "and so this is a failsafe X server.  "
						  "You should log in and properly "
						  "configure the X server."));
		}

		if G_UNLIKELY (d->busy_display) {
			char *msg = g_strdup_printf
				(_("The specified display number was busy, so "
				   "this server was started on display %s."),
				 d->name);
			gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR, msg);
			g_free (msg);
		}

		if G_UNLIKELY (d->try_different_greeter) {
			/* FIXME: we should also really be able to do standalone failsafe
			   login, but that requires some work and is perhaps an overkill. */
			/* This should handle mostly the case where gdmgreeter is crashing
			   and we'd want to start gdmlogin for the user so that at least
			   something works instead of a flickering screen */
			gdm_errorgui_error_box (d,
				       GTK_MESSAGE_ERROR,
				       _("The greeter application appears to be crashing. "
					 "Attempting to use a different one."));
			if (strstr (command, "gdmlogin") != NULL) {
				/* in case it is gdmlogin that's crashing
				   try the themed greeter for luck */
				command = LIBEXECDIR "/gdmgreeter";
			} else {
				/* in all other cases, try the gdmlogin (standard greeter)
				   proggie */
				command = LIBEXECDIR "/gdmlogin";
			}
		}

		moduleslist = gdm_daemon_config_get_value_string (GDM_KEY_GTK_MODULES_LIST);

		if (gdm_daemon_config_get_value_bool (GDM_KEY_ADD_GTK_MODULES) &&
		    ! ve_string_empty (moduleslist) &&
		    /* don't add modules if we're trying to prevent crashes,
		       perhaps it's the modules causing the problem in the first place */
		    ! d->try_different_greeter) {
			gchar *modules = g_strdup_printf ("--gtk-module=%s", moduleslist);
			exec_command (command, modules);
			/* Something went wrong */
			gdm_error (_("%s: Cannot start greeter with gtk modules: %s. Trying without modules"),
				   "gdm_slave_greeter",
				   moduleslist);
			g_free (modules);
		}
		exec_command (command, NULL);

		gdm_error (_("%s: Cannot start greeter trying default: %s"),
			   "gdm_slave_greeter",
			   LIBEXECDIR "/gdmlogin");

		g_setenv ("GDM_WHACKED_GREETER_CONFIG", "true", TRUE);

		exec_command (LIBEXECDIR "/gdmlogin", NULL);

		VE_IGNORE_EINTR (execl (LIBEXECDIR "/gdmlogin", LIBEXECDIR "/gdmlogin", NULL));

		gdm_errorgui_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Cannot start the greeter application; "
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
		VE_IGNORE_EINTR (close (pipe1[0]));
		VE_IGNORE_EINTR (close (pipe2[1]));

		whack_greeter_fds ();

		greeter_fd_out = pipe1[1];
		greeter_fd_in = pipe2[0];

		gdm_debug ("gdm_slave_greeter: Greeter on pid %d", (int)pid);

		gdm_slave_send_num (GDM_SOP_GREETPID, d->greetpid);
		run_pictures (); /* Append pictures to greeter if browsing is on */

		if (always_restart_greeter)
			gdm_slave_greeter_ctl_no_ret (GDM_ALWAYS_RESTART, "Y");
		else
			gdm_slave_greeter_ctl_no_ret (GDM_ALWAYS_RESTART, "N");
		gdmlang = g_getenv ("GDM_LANG");
		if (gdmlang)
			gdm_slave_greeter_ctl_no_ret (GDM_SETLANG, gdmlang);


		check_notifies_now ();
		break;
	}
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

	if (wait_for_ack) {
		gdm_got_ack = FALSE;
		g_free (gdm_ack_response);
		gdm_ack_response = NULL;
	}

	/* ensure this is sent from the actual slave with the pipe always, this is anal I know */
	if (G_LIKELY (d->slavepid == getppid ()) || G_LIKELY (d->slavepid == getpid ())) {
		fd = slave_fifo_pipe_fd;
	} else {
		fd = -1;
	}

	if G_UNLIKELY (fd < 0) {
		/* FIXME: This is not likely to ever be used, remove
		   at some point.  Other then slaves shouldn't be using
		   these functions.  And if the pipe creation failed
		   in main daemon just abort the main daemon.  */
		/* Use the fifo as a fallback only now that we have a pipe */
		fifopath = g_build_filename (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR),
					     ".gdmfifo", NULL);
		old = geteuid ();
		if (old != 0)
			seteuid (0);
#ifdef O_NOFOLLOW
		VE_IGNORE_EINTR (fd = open (fifopath, O_WRONLY|O_NOFOLLOW));
#else
		VE_IGNORE_EINTR (fd = open (fifopath, O_WRONLY));
#endif
		if (old != 0)
			seteuid (old);
		g_free (fifopath);
	}

	/* eek */
	if G_UNLIKELY (fd < 0) {
		if (gdm_in_signal == 0)
			gdm_error (_("%s: Can't open fifo!"), "gdm_slave_send");
		return;
	}

	gdm_fdprintf (fd, "\n%s\n", str);

	if G_UNLIKELY (fd != slave_fifo_pipe_fd) {
		VE_IGNORE_EINTR (close (fd));
	}

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
	if (wait_for_ack && ! gdm_got_ack) {
		/* let the other process do its stuff */
		sched_yield ();
	}
#endif

	/* Wait till you get a response from the daemon */
	if (strncmp (str, "opcode="GDM_SOP_SHOW_ERROR_DIALOG,
		     strlen ("opcode="GDM_SOP_SHOW_ERROR_DIALOG)) == 0 ||
	    strncmp (str, "opcode="GDM_SOP_SHOW_YESNO_DIALOG,
		     strlen ("opcode="GDM_SOP_SHOW_YESNO_DIALOG)) == 0 ||
	    strncmp (str, "opcode="GDM_SOP_SHOW_QUESTION_DIALOG,
		     strlen ("opcode="GDM_SOP_SHOW_QUESTION_DIALOG)) == 0 ||
	    strncmp (str, "opcode="GDM_SOP_SHOW_ASKBUTTONS_DIALOG,
		     strlen ("opcode="GDM_SOP_SHOW_ASKBUTTONS_DIALOG)) == 0) {

		for (; wait_for_ack && !gdm_got_ack ; ) {
			fd_set rfds;

			FD_ZERO (&rfds);
			FD_SET (d->slave_notify_fd, &rfds);

			if (select (d->slave_notify_fd+1, &rfds, NULL, NULL, NULL) > 0) {
				gdm_slave_handle_usr2_message ();
			}
		}
        } else {
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
	}

	if G_UNLIKELY (wait_for_ack  &&
		       ! gdm_got_ack &&
		       gdm_in_signal == 0) {
		if (strncmp (str, GDM_SOP_COOKIE " ",
			     strlen (GDM_SOP_COOKIE " ")) == 0) {
			char *s = g_strndup
				(str, strlen (GDM_SOP_COOKIE " XXXX XX"));
			/* cut off most of the cookie for "security" */
			gdm_debug ("Timeout occurred for sending message %s...", s);
			g_free (s);
		} else {
			gdm_debug ("Timeout occurred for sending message %s", str);
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

	if G_UNLIKELY (gdm_daemon_config_get_value_bool (GDM_KEY_DEBUG) && gdm_in_signal == 0) {
		gdm_debug ("Sending %s == <secret> for slave %ld",
			   opcode,
			   (long)getpid ());
	}

	if (strcmp (opcode, GDM_SOP_SHOW_ERROR_DIALOG) == 0 ||
	    strcmp (opcode, GDM_SOP_SHOW_YESNO_DIALOG) == 0 ||
	    strcmp (opcode, GDM_SOP_SHOW_QUESTION_DIALOG) == 0 ||
	    strcmp (opcode, GDM_SOP_SHOW_ASKBUTTONS_DIALOG) == 0) {
		msg = g_strdup_printf ("opcode=%s$$pid=%ld$$%s", opcode,
				       (long)d->slavepid, ve_sure_string (str));
	} else {
		msg = g_strdup_printf ("%s %ld %s", opcode,
				       (long)getpid (), ve_sure_string (str));
	}

	gdm_slave_send (msg, TRUE);

	g_free (msg);
}

static void
send_chosen_host (GdmDisplay *disp,
		  const char *hostname)
{
	GdmHostent *hostent;
	struct sockaddr_storage ss;
	char *str = NULL;
	char *host;

	hostent = gdm_gethostbyname (hostname);

	if G_UNLIKELY (hostent->addrs == NULL) {
		gdm_error ("Cannot get address of host '%s'", hostname);
		gdm_hostent_free (hostent);
		return;
	}

	/* take first address */
	memcpy (&ss, &hostent->addrs[0], sizeof (struct sockaddr_storage));

	gdm_address_get_info (&ss, &host, NULL);
	gdm_hostent_free (hostent);

	gdm_debug ("Sending chosen host address (%s) %s", hostname, host);
	str = g_strdup_printf ("%s %d %s", GDM_SOP_CHOSEN, disp->indirect_id, host);
	gdm_slave_send (str, FALSE);

	g_free (str);
}


static void
gdm_slave_chooser (void)
{
	gint p[2];
	struct passwd *pwent;
	pid_t pid;
	GdmWaitPid *wp;
	const char *defaultpath;
	const char *gdmuser;
	const char *moduleslist;

	gdm_debug ("gdm_slave_chooser: Running chooser on %s", d->name);

	/* Open a pipe for chooser communications */
	if G_UNLIKELY (pipe (p) < 0)
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't init pipe to gdmchooser"), "gdm_slave_chooser");

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (d, gdm_daemon_config_get_value_string (GDM_KEY_DISPLAY_INIT_DIR),
			       NULL, NULL, FALSE /* pass_stdout */);

	g_debug ("Forking chooser process: %s", gdm_daemon_config_get_value_string (GDM_KEY_CHOOSER));

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
		VE_IGNORE_EINTR (close (p[0]));

		if (p[1] != STDOUT_FILENO)
			VE_IGNORE_EINTR (dup2 (p[1], STDOUT_FILENO));

		gdm_log_shutdown ();

		VE_IGNORE_EINTR (close (0));
		gdm_close_all_descriptors (2 /* from */, slave_fifo_pipe_fd /* except */, d->slave_notify_fd /* except2 */);

		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		gdm_log_init ();

		if G_UNLIKELY (setgid (gdm_daemon_config_get_gdmgid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set groupid to %d"),
					"gdm_slave_chooser", gdm_daemon_config_get_gdmgid ());

		gdmuser = gdm_daemon_config_get_value_string (GDM_KEY_USER);
		if G_UNLIKELY (initgroups (gdmuser, gdm_daemon_config_get_gdmgid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: initgroups () failed for %s"),
					"gdm_slave_chooser", gdmuser);

		if G_UNLIKELY (setuid (gdm_daemon_config_get_gdmuid ()) < 0)
			gdm_child_exit (DISPLAY_ABORT,
					_("%s: Couldn't set userid to %d"),
					"gdm_slave_chooser", gdm_daemon_config_get_gdmuid ());

		gdm_restoreenv ();
		gdm_reset_locale ();

		g_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		g_setenv ("DISPLAY", d->name, TRUE);

		g_setenv ("LOGNAME", gdmuser, TRUE);
		g_setenv ("USER", gdmuser, TRUE);
		g_setenv ("USERNAME", gdmuser, TRUE);

		g_setenv ("GDM_VERSION", VERSION, TRUE);

		pwent = getpwnam (gdmuser);
		if G_LIKELY (pwent != NULL) {
			/* Note that usually this doesn't exist */
			if (g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS))
				g_setenv ("HOME", pwent->pw_dir, TRUE);
			else
				g_setenv ("HOME",
					  ve_sure_string (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR)),
					  TRUE); /* Hack */
			g_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			g_setenv ("HOME",
				  ve_sure_string (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR)),
				  TRUE); /* Hack */
			g_setenv ("SHELL", "/bin/sh", TRUE);
		}

		defaultpath = gdm_daemon_config_get_value_string (GDM_KEY_PATH);
		if (ve_string_empty (g_getenv ("PATH"))) {
			g_setenv ("PATH", defaultpath, TRUE);
		} else if ( ! ve_string_empty (defaultpath)) {
			gchar *temp_string = g_strconcat (g_getenv ("PATH"),
							  ":", defaultpath, NULL);
			g_setenv ("PATH", temp_string, TRUE);
			g_free (temp_string);
		}
		g_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
		if ( ! ve_string_empty (d->theme_name))
			g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);

		moduleslist = gdm_daemon_config_get_value_string (GDM_KEY_GTK_MODULES_LIST);
		if (gdm_daemon_config_get_value_bool (GDM_KEY_ADD_GTK_MODULES) &&
		    ! ve_string_empty (moduleslist)) {
			char *modules = g_strdup_printf ("--gtk-module=%s", moduleslist);
			exec_command (gdm_daemon_config_get_value_string (GDM_KEY_CHOOSER), modules);
		}

		exec_command (gdm_daemon_config_get_value_string (GDM_KEY_CHOOSER), NULL);

		gdm_errorgui_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("Cannot start the chooser application. "
				 "You will probably not be able to log in.  "
				 "Please contact the system administrator."));

		gdm_child_exit (DISPLAY_REMANAGE, _("%s: Error starting chooser on display %s"), "gdm_slave_chooser", d->name);

	case -1:
		gdm_slave_exit (DISPLAY_REMANAGE, _("%s: Can't fork gdmchooser process"), "gdm_slave_chooser");

	default:
		gdm_debug ("gdm_slave_chooser: Chooser on pid %d", d->chooserpid);
		gdm_slave_send_num (GDM_SOP_CHOOSERPID, d->chooserpid);

		VE_IGNORE_EINTR (close (p[1]));

		g_free (d->chooser_last_line);
		d->chooser_last_line = NULL;
		d->chooser_output_fd = p[0];
		/* make the output read fd non-blocking */
		fcntl (d->chooser_output_fd, F_SETFL, O_NONBLOCK);

		/* wait for the chooser to die */

		gdm_sigchld_block_push ();
		wp = slave_waitpid_setpid (d->chooserpid);
		gdm_sigchld_block_pop ();

		slave_waitpid (wp);

		d->chooserpid = 0;
		gdm_slave_send_num (GDM_SOP_CHOOSERPID, 0);

		/* Note: Nothing affecting the chooser needs update
		 * from notifies, plus we are exitting right now */

		run_chooser_output ();
		VE_IGNORE_EINTR (close (d->chooser_output_fd));
		d->chooser_output_fd = -1;

		if (d->chooser_last_line != NULL) {
			char *host = d->chooser_last_line;
			d->chooser_last_line = NULL;

			if (SERVER_IS_XDMCP (d)) {
				send_chosen_host (d, host);
				gdm_slave_quick_exit (DISPLAY_CHOSEN);
			} else {
				gdm_debug ("Sending locally chosen host %s", host);
				gdm_slave_send_string (GDM_SOP_CHOSEN_LOCAL, host);
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			}
		}

		gdm_slave_quick_exit (DISPLAY_REMANAGE);
		break;
	}
}

gboolean
gdm_is_session_magic (const char *session_name)
{
	return (strcmp (session_name, GDM_SESSION_DEFAULT) == 0 ||
		strcmp (session_name, GDM_SESSION_CUSTOM) == 0 ||
		strcmp (session_name, GDM_SESSION_FAILSAFE) == 0);
}

/* Note that this does check TryExec! while normally we don't check
 * it */
static gboolean
is_session_ok (const char *session_name)
{
	char *exec;
	gboolean ret = TRUE;

	/* these are always OK */
	if (strcmp (session_name, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	    strcmp (session_name, GDM_SESSION_FAILSAFE_XTERM) == 0)
		return TRUE;

	if (ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR)))
		return gdm_is_session_magic (session_name);

	exec = gdm_daemon_config_get_session_exec (session_name, TRUE /* check_try_exec */);
	if (exec == NULL)
		ret = FALSE;
	g_free (exec);
	return ret;
}

static char *
find_a_session (void)
{
	char *try[] = {
		"Default",
		"default",
		"Gnome",
		"gnome",
		"GNOME",
		"Custom",
		"custom",
		"kde",
		"KDE",
		"failsafe",
		"Failsafe",
		NULL
	};
	int i;
	char *session;
	const char *defaultsession = gdm_daemon_config_get_value_string (GDM_KEY_DEFAULT_SESSION);

	if (!ve_string_empty (defaultsession) &&
	    is_session_ok (defaultsession))
		session = g_strdup (defaultsession);
	else
		session = NULL;

	for (i = 0; try[i] != NULL && session == NULL; i++) {
		if (is_session_ok (try[i]))
			session = g_strdup (try[i]);
	}
	return session;
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
		BINDIR "/",
		NULL
	};

	path = g_find_program_in_path (name);
	if (path != NULL &&
	    g_access (path, X_OK) == 0) {
		return path;
	}
	g_free (path);
	for (i = 0; try[i] != NULL; i++) {
		path = g_strconcat (try[i], name, NULL);
		if (g_access (path, X_OK) == 0) {
			return path;
		}
		g_free (path);
	}
	return NULL;
}

static gboolean
wipe_xsession_errors (struct passwd *pwent,
		      const char *home_dir,
		      gboolean home_dir_ok)
{
	gboolean wiped_something = FALSE;
	DIR *dir;
	struct dirent *ent;
	uid_t old = geteuid ();
	uid_t oldg = getegid ();

	seteuid (0);
	if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
		       seteuid (pwent->pw_uid) != 0) {
		NEVER_FAILS_root_set_euid_egid (old, oldg);
		return FALSE;
	}

	if G_LIKELY (home_dir_ok) {
		char *filename = g_build_filename (home_dir,
						   ".xsession-errors",
						   NULL);
		if (g_access (filename, F_OK) == 0) {
			wiped_something = TRUE;
			VE_IGNORE_EINTR (g_unlink (filename));
		}
		g_free (filename);
	}

	VE_IGNORE_EINTR (dir = opendir ("/tmp"));
	if G_LIKELY (dir != NULL) {
		char *prefix = g_strdup_printf ("xses-%s.", pwent->pw_name);
		int prefixlen = strlen (prefix);
		VE_IGNORE_EINTR (ent = readdir (dir));
		while (ent != NULL) {
			if (strncmp (ent->d_name, prefix, prefixlen) == 0) {
				char *filename = g_strdup_printf ("/tmp/%s",
								  ent->d_name);
				wiped_something = TRUE;
				VE_IGNORE_EINTR (g_unlink (filename));
				g_free (filename);
			}
			VE_IGNORE_EINTR (ent = readdir (dir));
		}
		VE_IGNORE_EINTR (closedir (dir));
		g_free (prefix);
	}

	NEVER_FAILS_root_set_euid_egid (old, oldg);

	return wiped_something;
}

static int
open_xsession_errors (struct passwd *pwent,
		      gboolean failsafe,
		      const char *home_dir,
		      gboolean home_dir_ok)
{
	int logfd = -1;

	g_free (d->xsession_errors_filename);
	d->xsession_errors_filename = NULL;

        /* Log all output from session programs to a file,
	 * unless in failsafe mode which needs to work when there is
	 * no diskspace as well */
	if G_LIKELY ( ! failsafe && home_dir_ok) {
		char *filename = g_build_filename (home_dir,
						   ".xsession-errors",
						   NULL);
		uid_t old = geteuid ();
		uid_t oldg = getegid ();

		seteuid (0);
		if G_LIKELY (setegid (pwent->pw_gid) == 0 &&
			     seteuid (pwent->pw_uid) == 0) {
			/* unlink to be anal */
			VE_IGNORE_EINTR (g_unlink (filename));
			VE_IGNORE_EINTR (logfd = open (filename, O_EXCL|O_CREAT|O_TRUNC|O_WRONLY, 0644));
		}
		NEVER_FAILS_root_set_euid_egid (old, oldg);

		if G_UNLIKELY (logfd < 0) {
			gdm_error (_("%s: Could not open ~/.xsession-errors"),
				   "run_session_child");
			g_free (filename);
		} else {
			d->xsession_errors_filename = filename;
		}
	}

	/* let's try an alternative */
	if G_UNLIKELY (logfd < 0) {
		mode_t oldmode;

		char *filename = g_strdup_printf ("/tmp/xses-%s.XXXXXX",
						  pwent->pw_name);
		uid_t old = geteuid ();
		uid_t oldg = getegid ();

		seteuid (0);
		if G_LIKELY (setegid (pwent->pw_gid) == 0 &&
			     seteuid (pwent->pw_uid) == 0) {
			oldmode = umask (077);
			logfd = mkstemp (filename);
			umask (oldmode);
		}

		NEVER_FAILS_root_set_euid_egid (old, oldg);

		if G_LIKELY (logfd >= 0) {
			d->xsession_errors_filename = filename;
		} else {
			g_free (filename);
		}
	}

	return logfd;
}

#ifdef HAVE_SELINUX
/* This should be run just before we exec the user session */
static gboolean
gdm_selinux_setup (const char *login)
{
	security_context_t scontext;
	int ret=-1;
	char *seuser=NULL;
	char *level=NULL;

	/* If selinux is not enabled, then we don't do anything */
	if (is_selinux_enabled () <= 0)
		return TRUE;

	if (getseuserbyname(login, &seuser, &level) == 0)
		ret=get_default_context_with_level(seuser, level, 0, &scontext);

	if (ret < 0) {
		gdm_error ("SELinux gdm login: unable to obtain default security context for %s.", login);
		/* note that this will be run when the .xsession-errors
		   is already being logged, so we can use stderr */
		gdm_fdprintf (2, "SELinux gdm login: unable to obtain default security context for %s.", login);
 		return (security_getenforce()==0);
	}

	gdm_assert (scontext != NULL);

	if (setexeccon (scontext) != 0) {
		gdm_error ("SELinux gdm login: unable to set executable context %s.",
			   (char *)scontext);
		gdm_fdprintf (2, "SELinux gdm login: unable to set executable context %s.",
			      (char *)scontext);
		freecon (scontext);
		return (security_getenforce()==0);
	}

	freecon (scontext);

	return TRUE;
}
#endif /* HAVE_SELINUX */

static void
session_child_run (struct passwd *pwent,
		   int logfd,
		   gboolean failsafe,
		   const char *home_dir,
		   gboolean home_dir_ok,
#ifdef WITH_CONSOLE_KIT
		   const char *ck_session_cookie,
#endif
		   const char *session,
		   const char *save_session,
		   const char *language,
		   const char *gnome_session,
		   gboolean usrcfgok,
		   gboolean savesess,
		   gboolean savelang)
{
	char *sessionexec = NULL;
	GString *fullexec = NULL;
	const char *shell = NULL;
	const char *greeter;
	gint result;
	gchar **argv = NULL;

#ifdef CAN_USE_SETPENV
	extern char **newenv;
	int i;
#endif

	gdm_unset_signals ();
	if G_UNLIKELY (setsid () < 0)
		/* should never happen */
		gdm_error (_("%s: setsid () failed: %s!"),
			   "session_child_run", strerror (errno));

	g_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);

	/* Here we setup our 0,1,2 descriptors, we do it here
	 * nowdays rather then later on so that we get errors even
	 * from the PreSession script */
	if G_LIKELY (logfd >= 0) {
		VE_IGNORE_EINTR (dup2 (logfd, 1));
		VE_IGNORE_EINTR (dup2 (logfd, 2));
		VE_IGNORE_EINTR (close (logfd));
	} else {
		VE_IGNORE_EINTR (close (1));
		VE_IGNORE_EINTR (close (2));
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
	}

	VE_IGNORE_EINTR (close (0));
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

	/* Set this for the PreSession script */
	/* compatibility */
	g_setenv ("GDMSESSION", session, TRUE);

	g_setenv ("DESKTOP_SESSION", session, TRUE);

	/* Determine default greeter type so the PreSession */
	/* script can set the appropriate background color. */
	if (d->attached) {
		greeter = gdm_daemon_config_get_value_string (GDM_KEY_GREETER);
	} else {
		greeter = gdm_daemon_config_get_value_string (GDM_KEY_REMOTE_GREETER);
	}

	if (strstr (greeter, "gdmlogin") != NULL) {
		g_setenv ("GDM_GREETER_TYPE", "PLAIN", TRUE);
	} else if (strstr (greeter, "gdmgreeter") != NULL) {
		g_setenv ("GDM_GREETER_TYPE", "THEMED", TRUE);
	} else {
		/* huh? */
		g_setenv ("GDM_GREETER_TYPE", "unknown", TRUE);
	}

	/* Run the PreSession script */
	if G_UNLIKELY (gdm_slave_exec_script (d, gdm_daemon_config_get_value_string (GDM_KEY_PRESESSION),
                                              pwent->pw_name, pwent,
					      TRUE /* pass_stdout */) != EXIT_SUCCESS &&
		       /* ignore errors in failsafe modes */
		       ! failsafe)
		/* If script fails reset X server and restart greeter */
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Execution of PreSession script returned > 0. Aborting."), "session_child_run");

	ve_clearenv ();

	/* Prepare user session */
	g_setenv ("XAUTHORITY", d->userauth, TRUE);
	g_setenv ("DISPLAY", d->name, TRUE);
	g_setenv ("LOGNAME", pwent->pw_name, TRUE);
	g_setenv ("USER", pwent->pw_name, TRUE);
	g_setenv ("USERNAME", pwent->pw_name, TRUE);
	g_setenv ("HOME", home_dir, TRUE);
#ifdef WITH_CONSOLE_KIT
	if (ck_session_cookie != NULL) {
		g_setenv ("XDG_SESSION_COOKIE", ck_session_cookie, TRUE);
	}
#endif
	g_setenv ("PWD", home_dir, TRUE);
	g_setenv ("GDMSESSION", session, TRUE);
	g_setenv ("DESKTOP_SESSION", session, TRUE);
	g_setenv ("SHELL", pwent->pw_shell, TRUE);

	if (d->type == TYPE_STATIC) {
		g_setenv ("GDM_XSERVER_LOCATION", "local", TRUE);
	} else if (d->type == TYPE_XDMCP) {
		g_setenv ("GDM_XSERVER_LOCATION", "xdmcp", TRUE);
	} else if (d->type == TYPE_FLEXI) {
		g_setenv ("GDM_XSERVER_LOCATION", "flexi", TRUE);
	} else if (d->type == TYPE_FLEXI_XNEST) {
		g_setenv ("GDM_XSERVER_LOCATION", "flexi-xnest", TRUE);
	} else if (d->type == TYPE_XDMCP_PROXY) {
		g_setenv ("GDM_XSERVER_LOCATION", "xdmcp-proxy", TRUE);
	} else {
		/* huh? */
		g_setenv ("GDM_XSERVER_LOCATION", "unknown", TRUE);
	}

	if (gnome_session != NULL)
		g_setenv ("GDM_GNOME_SESSION", gnome_session, TRUE);

	/* Special PATH for root */
	if (pwent->pw_uid == 0)
		g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);
	else
		g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_PATH), TRUE);

	/*
	 * Install GDM desktop files to a non-default desktop file 
	 * location (/usr/share/gdm/applications) and GDM appends 
	 * this directory to the end of the XDG_DATA_DIR environment
	 * variable.  This way, GDM menu choices never appear if
	 * using a different display manager.
	 */
	{
		const char *old_system_data_dirs;
		char *new_system_data_dirs;

		old_system_data_dirs = g_getenv ("XDG_DATA_DIRS") ?
				       g_getenv ("XDG_DATA_DIRS") :
				       "/usr/local/share/:/usr/share/";

		new_system_data_dirs = g_build_path (":",
			 old_system_data_dirs, DATADIR "/gdm/", NULL);

		g_setenv ("XDG_DATA_DIRS", new_system_data_dirs, TRUE);

		g_free (new_system_data_dirs);
	}

	/* Eeeeek, this no lookie as a correct language code,
	 * just use the system default */
	if G_UNLIKELY ( ! ve_string_empty (language) &&
			! ve_locale_exists (language)) {
		char *msg = g_strdup_printf (_("Language %s does not exist; using %s"),
					     language, _("System default"));
		gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR, msg);
		language = NULL;
		g_free (msg);
	}

	/* Now still as root make the system authfile not readable by others,
	   and therefore not by the gdm user */
	VE_IGNORE_EINTR (g_chmod (GDM_AUTHFILE (d), 0640));

	setpgid (0, 0);

	umask (022);

	/* setup the verify env vars */
	if G_UNLIKELY ( ! gdm_verify_setup_env (d))
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Could not setup environment for %s. "
				  "Aborting."),
				"session_child_run", login);

        /* setup euid/egid to the correct user,
         * not to leave the egid around.  It's
         * ok to gdm_fail here */
        NEVER_FAILS_root_set_euid_egid (pwent->pw_uid, pwent->pw_gid);

	VE_IGNORE_EINTR (result = g_chdir (home_dir));
	if G_UNLIKELY (result != 0) {
		VE_IGNORE_EINTR (g_chdir ("/"));
		NEVER_FAILS_root_set_euid_egid (0, 0);
	} else if (pwent->pw_uid != 0) {
                /* sanitize .ICEauthority to be of the correct
                 * permissions, if it exists */
                struct stat s0, s1, s2;
                gint        s0_ret, s1_ret, s2_ret;
                gint        iceauth_fd;

		NEVER_FAILS_root_set_euid_egid (0, 0);

                iceauth_fd = open (".ICEauthority", O_RDONLY);

                s0_ret = stat (home_dir, &s0);
                s1_ret = lstat (".ICEauthority", &s1);
                s2_ret = fstat (iceauth_fd, &s2);

                if (iceauth_fd >= 0 &&
                    s0_ret == 0 &&
                    s0.st_uid == pwent->pw_uid &&
                    s1_ret == 0 &&
                    s2_ret == 0 &&
                    S_ISREG (s1.st_mode) &&
                    s1.st_ino == s2.st_ino &&
                    s1.st_dev == s2.st_dev &&
                    s1.st_uid == s2.st_uid &&
                    s1.st_gid == s2.st_gid &&
                    s1.st_mode == s2.st_mode &&
                    (s1.st_uid != pwent->pw_uid ||
                     s1.st_gid != pwent->pw_gid ||
                     (s1.st_mode & (S_IRWXG|S_IRWXO)) ||
                     !(s1.st_mode & S_IRWXU))) {
                        /* This may not work on NFS, but oh well, there
                         * this is beyond our help, but it's unlikely
                         * that it got screwed up when NFS was used
                         * in the first place */

                        /* only if we own the current directory */
                        fchown (iceauth_fd,
                                pwent->pw_uid,
                                pwent->pw_gid);
                        fchmod (iceauth_fd, S_IRUSR | S_IWUSR);
                }

                if (iceauth_fd >= 0)
                        close (iceauth_fd);
        }

	NEVER_FAILS_setegid (pwent->pw_gid);
#ifdef HAVE_LOGINCAP
	if (setusercontext (NULL, pwent, pwent->pw_uid,
			    LOGIN_SETLOGIN | LOGIN_SETPATH |
			    LOGIN_SETPRIORITY | LOGIN_SETRESOURCES |
			    LOGIN_SETUMASK | LOGIN_SETUSER |
			    LOGIN_SETENV) < 0)
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: setusercontext () failed for %s. "
				  "Aborting."), "session_child_run",
				login);
#else
	if G_UNLIKELY (setuid (pwent->pw_uid) < 0)
		gdm_child_exit (DISPLAY_REMANAGE,
				_("%s: Could not become %s. Aborting."), "session_child_run", login);
#endif

	/* Only force GDM_LANG to something if there is other then
	 * system default selected.  Else let the session do whatever it
	 * does since we're using sys default */
	if ( ! ve_string_empty (language)) {
		g_setenv ("LANG", language, TRUE);
		g_setenv ("GDM_LANG", language, TRUE);
	}

	/* just in case there is some weirdness going on */
	VE_IGNORE_EINTR (g_chdir (home_dir));

        if (usrcfgok && home_dir_ok)
		gdm_daemon_config_set_user_session_lang (savesess, savelang, home_dir, save_session, language);

	gdm_log_shutdown ();

	gdm_close_all_descriptors (3 /* from */, slave_fifo_pipe_fd /* except */, d->slave_notify_fd /* except2 */);

	gdm_log_init ();

	sessionexec = NULL;
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) != 0 &&
	    strcmp (session, GDM_SESSION_FAILSAFE_GNOME) != 0) {

		sessionexec = gdm_daemon_config_get_session_exec (session,
			FALSE /* check_try_exec */);

		if G_UNLIKELY (sessionexec == NULL) {
			gchar *msg = g_strdup_printf (
						      _("No Exec line in the session file: %s.  Running the GNOME failsafe session instead"),
						      session);

			gdm_error (_("%s: %s"), "session_child_run", msg);
			gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR, msg);
			g_free (msg);

			session = GDM_SESSION_FAILSAFE_GNOME;
		} else {
			/* HACK!, if failsafe, we really wish to run the
			   internal one */
			if (strcmp (sessionexec, "failsafe") == 0) {
				session = GDM_SESSION_FAILSAFE_XTERM;
				sessionexec    = NULL;
			}
		}
	}

	fullexec = g_string_new (NULL);

#ifdef HAVE_CTRUN
	g_string_append (fullexec, "/usr/bin/ctrun -l child -i none ");
#endif

	if (sessionexec != NULL) {
		const char *basexsession = gdm_daemon_config_get_value_string (GDM_KEY_BASE_XSESSION);

		/* cannot be possibly failsafe */
		if G_UNLIKELY (g_access (basexsession, X_OK) != 0) {
			gdm_error (_("%s: Cannot find or run the base Xsession script.  Running the GNOME failsafe session instead."),
				   "session_child_run");
			session = GDM_SESSION_FAILSAFE_GNOME;
			sessionexec = NULL;
			gdm_errorgui_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("Cannot find or run the base session script.  Running the GNOME failsafe session instead."));
		} else {
			/*
			 * This is where the session is OK, and note that
			 * we really DON'T care about leaks, we are going to
			 * exec in just a bit
			 */
			g_string_append (fullexec, basexsession);
			g_string_append (fullexec, " ");

#ifdef HAVE_TSOL
			if (have_suntsol_extension)
				g_string_append (fullexec, "/usr/bin/tsoljdslabel ");
#endif
			g_string_append (fullexec, sessionexec);
		}
	}

	if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0) {
		gchar *test_exec = NULL;

#ifdef HAVE_TSOL
		/*
		 * Trusted Path will be preserved as long as the sys admin
		 * doesn't put anything stupid in gdm.conf
		 */
		if (have_suntsol_extension == TRUE)
			g_string_append (fullexec, "/usr/bin/tsoljdslabel ");
#endif

		test_exec = find_prog ("gnome-session");
		if G_UNLIKELY (test_exec == NULL) {
			/* yaikes */
			gdm_error (_("%s: gnome-session not found for a failsafe GNOME session, trying xterm"),
				   "session_child_run");
			session = GDM_SESSION_FAILSAFE_XTERM;
			gdm_errorgui_error_box
				(d, GTK_MESSAGE_ERROR,
				 _("Could not find the GNOME installation, "
				   "will try running the \"Failsafe xterm\" "
				   "session."));
		} else {
			g_string_append (fullexec, test_exec);
			g_string_append (fullexec, " --failsafe");
			gdm_errorgui_error_box
				(d, GTK_MESSAGE_INFO,
				 _("This is the Failsafe GNOME session.  "
				   "You will be logged into the 'Default' "
				   "session of GNOME without the startup scripts "
				   "being run.  This should be used to fix problems "
				   "in your installation."));
		}
		failsafe = TRUE;
	}

	/* This is an if and not an else, we could have done a fall-through
	 * to here in the above code if we can't find gnome-session */
	if (strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0) {
		gchar *test_exec;
		gchar *geometry = g_strdup_printf (" -geometry 80x24-%d-%d",
						   d->lrh_offsetx,
						   d->lrh_offsety);
		test_exec = find_prog ("xterm");
		if (test_exec == NULL) {
			gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR,
				       _("Cannot find \"xterm\" to start "
					 "a failsafe session."));
			/* nyah nyah nyah nyah nyah */
			/* 66 means no "session crashed" examine .xsession-errors dialog */
			_exit (66);
		} else {
			gchar *failsafe_msg = NULL;
			g_string_append (fullexec, test_exec);
			g_string_append (fullexec, geometry);

			failsafe_msg = _("This is the Failsafe xterm session.  "
					 "You will be logged into a terminal "
					 "console so that you may fix your system "
					 "if you cannot log in any other way.  "
					 "To exit the terminal emulator, type "
					 "'exit' and an enter into the window.");
#ifdef HAVE_TSOL
			if (have_suntsol_extension) {
				/*
				 * In a Solaris Trusted Extensions environment, failsafe
				 * xterms should be restricted to the root user, or
				 * users who have the root role.  This is necessary to
				 * prevent normal users and evil terrorists bypassing
				 * their assigned clearance and getting direct access
				 * to the global zone.
				 */
				if (pwent->pw_uid != 0 &&
				    gdm_can_i_assume_root_role (pwent) == TRUE) {
					g_string_append (fullexec, " -C -e su");
					failsafe_msg =  _("This is the Failsafe xterm session.  "
							  "You will be logged into a terminal "
							  "console and be prompted to enter the "
							  "password for root so that you may fix "
							  "your system if you cannot log in any "
							  "other way. To exit the terminal "
							  "emulator, type 'exit' and an enter "
							  "into the window.");
				} else {
					/* Normal user without root role - get lost */
					gdm_errorgui_error_box
						(d, GTK_MESSAGE_INFO,
						 _("The failsafe session is restricted to "
						   "users who have been assigned the root "
						   "role. If you cannot log in any other "
						   "way please contact your system "
						   "administrator"));
					_exit (66);
				}
			}
#endif /* HAVE_TSOL */

			gdm_errorgui_error_box (d, GTK_MESSAGE_INFO, failsafe_msg);
			focus_first_x_window ("xterm");
		}
		g_free (geometry);
		failsafe = TRUE;
	}

	gdm_debug ("Running %s for %s on %s", fullexec->str, login, d->name);

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
			   "session_child_run");
		gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR,
			       _("The system administrator has "
				 "disabled your account."));
		/* ends as if nothing bad happened */
		/* 66 means no "session crashed" examine .xsession-errors
		   dialog */
		_exit (66);
	}

#ifdef CAN_USE_SETPENV
	/* Call the function setpenv which instanciates the extern variable "newenv" */
	setpenv (login, (PENV_INIT | PENV_NOEXEC), NULL, NULL);

	/* Add the content of the "newenv" variable to the environment */
	for (i=0; newenv != NULL && newenv[i] != NULL; i++) {
		char *env_str = g_strdup (newenv[i]);
		char *p = strchr (env_str, '=');
		if (p != NULL) {
			/* Add a NULL byte to terminate the variable name */
			p[0] = '\0';
			/* Add the variable to the env */
			g_setenv (env_str, &p[1], TRUE);
		}
		g_free (env_str);
	}
#endif

#ifdef HAVE_SELINUX
	if ( ! gdm_selinux_setup (pwent->pw_name)) {
		/* 66 means no "session crashed" examine .xsession-errors
		   dialog */
		gdm_errorgui_error_box (d, GTK_MESSAGE_ERROR,
			       _("Error! Unable to set executable context."));
		_exit (66);
	}
#endif

        g_shell_parse_argv (fullexec->str, NULL, &argv, NULL);
	VE_IGNORE_EINTR (execv (argv[0], argv));
	g_strfreev (argv);

	/* will go to .xsession-errors */
	fprintf (stderr, _("%s: Could not exec %s"),
		 "session_child_run", fullexec->str);
	gdm_error ( _("%s: Could not exec %s"),
		    "session_child_run", fullexec->str);
	g_string_free (fullexec, TRUE);

	/* if we can't read and exec the session, then make a nice
	 * error dialog */
	gdm_errorgui_error_box
		(d, GTK_MESSAGE_ERROR,
		 /* we can't really be any more specific */
		 _("Cannot start the session due to some "
		   "internal error."));

	/* ends as if nothing bad happened */
	_exit (0);
}

static void
finish_session_output (gboolean do_read)
{
	if G_LIKELY (d->session_output_fd >= 0)  {
		if (do_read)
			run_session_output (TRUE /* read_until_eof */);
		if (d->session_output_fd >= 0)  {
			VE_IGNORE_EINTR (close (d->session_output_fd));
			d->session_output_fd = -1;
		}
		if (d->xsession_errors_fd >= 0)  {
			VE_IGNORE_EINTR (close (d->xsession_errors_fd));
			d->xsession_errors_fd = -1;
		}
	}
}

static void
gdm_slave_session_start (void)
{
	struct passwd *pwent;
	const char *home_dir = NULL;
	char *save_session = NULL, *session = NULL, *language = NULL, *usrsess, *usrlang;
	char *gnome_session = NULL;
#ifdef WITH_CONSOLE_KIT
	char *ck_session_cookie;
#endif
	char *tmp;
	gboolean savesess = FALSE, savelang = FALSE;
	gboolean usrcfgok = FALSE, authok = FALSE;
	gboolean home_dir_ok = FALSE;
	gboolean failsafe = FALSE;
	time_t session_start_time, end_time; 
	pid_t pid;
	GdmWaitPid *wp;
	uid_t uid;
	gid_t gid;
	int logpipe[2];
	int logfilefd;

	gdm_debug ("gdm_slave_session_start: Attempting session for user '%s'",
		   login);

	pwent = getpwnam (login);

	if G_UNLIKELY (pwent == NULL)  {
		/* This is sort of an "assert", this should NEVER happen */
		if (greet)
			gdm_slave_whack_greeter ();
		gdm_slave_exit (DISPLAY_REMANAGE,
				_("%s: User passed auth but getpwnam (%s) failed!"), "gdm_slave_session_start", login);
	}

	logged_in_uid = uid = pwent->pw_uid;
	logged_in_gid = gid = pwent->pw_gid;

	/* Run the PostLogin script */
	if G_UNLIKELY (gdm_slave_exec_script (d, gdm_daemon_config_get_value_string (GDM_KEY_POSTLOGIN),
					      login, pwent,
					      TRUE /* pass_stdout */) != EXIT_SUCCESS &&
		       /* ignore errors in failsafe modes */
		       ! failsafe) {
		gdm_verify_cleanup (d);
		gdm_error (_("%s: Execution of PostLogin script returned > 0. Aborting."), "gdm_slave_session_start");
		/* script failed so just try again */
		return;
	}

	/*
	 * Set euid, gid to user before testing for user's $HOME since root
	 * does not always have access to the user's $HOME directory.
	 */
	if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
		       seteuid (pwent->pw_uid) != 0) {
		gdm_error ("Cannot set effective user/group id");
		gdm_verify_cleanup (d);
		session_started = FALSE;
		return;
	}

	if G_UNLIKELY (pwent->pw_dir == NULL ||
		       ! g_file_test (pwent->pw_dir, G_FILE_TEST_IS_DIR)) {
		char *yesno_msg;
		char *msg = g_strdup_printf (
					     _("Your home directory is listed as: '%s' "
					       "but it does not appear to exist.  "
					       "Do you want to log in with the / (root) "
					       "directory as your home directory? "
					       "It is unlikely anything will work unless "
					       "you use a failsafe session."),
					     ve_sure_string (pwent->pw_dir));

		/* Set euid, egid to root:gdm to manage user interaction */
		seteuid (0);
		setegid (gdm_daemon_config_get_gdmgid ());

		gdm_error (_("%s: Home directory for %s: '%s' does not exist!"),
			   "gdm_slave_session_start",
			   login,
			   ve_sure_string (pwent->pw_dir));

		/* Check what the user wants to do */
		yesno_msg = g_strdup_printf ("yesno_msg=%s", msg);
		gdm_slave_send_string (GDM_SOP_SHOW_YESNO_DIALOG, yesno_msg);

		g_free (yesno_msg);

		if (strcmp (gdm_ack_response, "no") == 0) {
			gdm_verify_cleanup (d);
			session_started = FALSE;

			g_free (msg);
			g_free (gdm_ack_response);
			gdm_ack_response = NULL;
			return;
		}

		g_free (msg);
		g_free (gdm_ack_response);
		gdm_ack_response = NULL;

		/* Reset euid, egid back to user */
		if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
			       seteuid (pwent->pw_uid) != 0) {
			gdm_error ("Cannot set effective user/group id");
			gdm_verify_cleanup (d);
			session_started = FALSE;
			return;
		}

		home_dir_ok = FALSE;
		home_dir = "/";
	} else {
		home_dir_ok = TRUE;
		home_dir = pwent->pw_dir;
	}

	if G_LIKELY (home_dir_ok) {
		/* Sanity check on ~user/.dmrc */
		usrcfgok = gdm_file_check ("gdm_slave_session_start", pwent->pw_uid,
					   home_dir, ".dmrc", TRUE, FALSE,
					   gdm_daemon_config_get_value_int (GDM_KEY_USER_MAX_FILE),
					   gdm_daemon_config_get_value_int (GDM_KEY_RELAX_PERM));
	} else {
		usrcfgok = FALSE;
	}

	if G_LIKELY (usrcfgok) {
		gdm_daemon_config_get_user_session_lang (&usrsess, &usrlang, home_dir, &savesess);
	} else {
		/* This won't get displayed if the .dmrc file simply doesn't
		 * exist since we pass absentok=TRUE when we call gdm_file_check
		 */
		gdm_errorgui_error_box (d,
			       GTK_MESSAGE_WARNING,
			       _("User's $HOME/.dmrc file is being ignored.  "
				 "This prevents the default session "
				 "and language from being saved.  File "
				 "should be owned by user and have 644 "
				 "permissions.  User's $HOME directory "
				 "must be owned by user and not writable "
				 "by other users."));
		usrsess = g_strdup ("");
		usrlang = g_strdup ("");
	}

	NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());

	if (greet) {
		tmp = gdm_ensure_extension (usrsess, ".desktop");
		session = gdm_slave_greeter_ctl (GDM_SESS, tmp);
		g_free (tmp);

		if (session != NULL &&
		    strcmp (session, GDM_RESPONSE_CANCEL) == 0) {
			gdm_debug ("User canceled login");
			gdm_verify_cleanup (d);
			session_started = FALSE;
			g_free (usrlang);
			return;
		}

		language = gdm_slave_greeter_ctl (GDM_LANG, usrlang);
		if (language != NULL &&
		    strcmp (language, GDM_RESPONSE_CANCEL) == 0) {
			gdm_debug ("User canceled login");
			gdm_verify_cleanup (d);
			session_started = FALSE;
			g_free (usrlang);
			return;
		}
	} else {
		session = g_strdup (usrsess);
		language = g_strdup (usrlang);
	}

	tmp = gdm_strip_extension (session, ".desktop");
	g_free (session);
	session = tmp;

	if (ve_string_empty (session)) {
		g_free (session);
		session = find_a_session ();
		if (session == NULL) {
			/* we're running out of options */
			session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
		}
	}

	if G_LIKELY (ve_string_empty (language)) {
		g_free (language);
		language = NULL;
	}

	g_free (usrsess);

	gdm_debug ("Initial setting: session: '%s' language: '%s'\n",
		   session, ve_sure_string (language));

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

	if (gdm_daemon_config_get_value_bool (GDM_KEY_KILL_INIT_CLIENTS))
		gdm_server_whack_clients (d->dsp);

	/*
	 * If the desktop file specifies that there are special Xserver
	 * arguments to use, then restart the Xserver with them.
	 */
	d->xserver_session_args = gdm_daemon_config_get_session_xserver_args (session);
	if (d->xserver_session_args) {
		gdm_server_stop (d);
		gdm_slave_send_num (GDM_SOP_XPID, 0);
		gdm_server_start (d, TRUE, FALSE, 20, 5);
		gdm_slave_send_num (GDM_SOP_XPID, d->servpid);
		g_free (d->xserver_session_args);
		d->xserver_session_args = NULL;
	}

	/* Now that we will set up the user authorization we will
	   need to run session_stop to whack it */
	session_started = TRUE;

	/* Setup cookie -- We need this information during cleanup, thus
	 * cookie handling is done before fork()ing */

	if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
		       seteuid (pwent->pw_uid) != 0) {
		gdm_error ("Cannot set effective user/group id");
		gdm_slave_quick_exit (DISPLAY_REMANAGE);
	}

	authok = gdm_auth_user_add (d, pwent->pw_uid,
				    /* Only pass the home_dir if
				     * it was ok */
				    home_dir_ok ? home_dir : NULL);

	/* FIXME: this should be smarter and only do this on out-of-diskspace
	 * errors */
	if G_UNLIKELY ( ! authok && home_dir_ok) {
		/* try wiping the .xsession-errors file (and perhaps other things)
		   in an attempt to gain disk space */
		if (wipe_xsession_errors (pwent, home_dir, home_dir_ok)) {
			gdm_error ("Tried wiping some old user session errors files "
				   "to make disk space and will try adding user auth "
				   "files again");
			/* Try again */
			authok = gdm_auth_user_add (d, pwent->pw_uid,
						    /* Only pass the home_dir if
						     * it was ok */
						    home_dir_ok ? home_dir : NULL);
		}
	}

	NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());

	if G_UNLIKELY ( ! authok) {
		gdm_debug ("gdm_slave_session_start: Auth not OK");

		gdm_errorgui_error_box (d,
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

	if G_UNLIKELY (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
		       strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
		       g_ascii_strcasecmp (session, "failsafe") == 0 /* hack */)
		failsafe = TRUE;

	if G_LIKELY ( ! failsafe) {
		char *exec = gdm_daemon_config_get_session_exec (session, FALSE /* check_try_exec */);
		if ( ! ve_string_empty (exec) &&
		     strcmp (exec, "failsafe") == 0)
			failsafe = TRUE;
		g_free (exec);
	}

	/* Write out the Xservers file */
	gdm_slave_send_num (GDM_SOP_WRITE_X_SERVERS, 0 /* bogus */);

	if G_LIKELY (d->dsp != NULL) {
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

	/* Init the ~/.xsession-errors stuff */
	d->xsession_errors_bytes = 0;
	d->xsession_errors_fd = -1;
	d->session_output_fd = -1;

	logfilefd = open_xsession_errors (pwent,
					  failsafe,
					  home_dir,
					  home_dir_ok);
	if G_UNLIKELY (logfilefd < 0 ||
		       pipe (logpipe) != 0) {
		if (logfilefd >= 0)
			VE_IGNORE_EINTR (close (logfilefd));
		logfilefd = -1;
	}

	/* don't completely rely on this, the user
	 * could reset time or do other crazy things */
	session_start_time = time (NULL);

#ifdef WITH_CONSOLE_KIT
	ck_session_cookie = open_ck_session (pwent, d, session);
#endif

	g_debug ("Forking user session");

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
		{
			const char *gdm_system_locale;
			const char *lang;
			gboolean    has_language;

			has_language = (language != NULL) && (language[0] != '\0');

			gdm_system_locale = setlocale (LC_CTYPE, NULL);
			if ((gdm_system_locale != NULL) && (!has_language)) {
				lang = gdm_system_locale;
			} else {
				lang = language;
			}

			if G_LIKELY (logfilefd >= 0) {
				VE_IGNORE_EINTR (close (logpipe[0]));
			}
			/* Never returns */
			session_child_run (pwent,
					   logpipe[1],
					   failsafe,
					   home_dir,
					   home_dir_ok,
#ifdef WITH_CONSOLE_KIT
					   ck_session_cookie,
#endif
					   session,
					   save_session,
					   lang,
					   gnome_session,
					   usrcfgok,
					   savesess,
					   savelang);
			g_assert_not_reached ();
		}

	default:
		always_restart_greeter = FALSE;
		if (!savelang && language && strcmp (usrlang, language)) {
			const char *gdm_system_locale;

			gdm_system_locale = setlocale (LC_CTYPE, NULL);

			if (gdm_system_locale != NULL) {
				g_setenv ("LANG", gdm_system_locale, TRUE);
				setlocale (LC_ALL, "");
				g_unsetenv ("GDM_LANG");
				/* for "GDM_LANG" */
				gdm_clearenv_no_lang ();
				gdm_saveenv ();
			}
			gdm_slave_greeter_ctl_no_ret (GDM_SETLANG, DEFAULT_LANGUAGE);
		}
		break;
	}

	/* this clears internal cache */
	gdm_daemon_config_get_session_exec (NULL, FALSE);

	if G_LIKELY (logfilefd >= 0)  {
		d->xsession_errors_fd = logfilefd;
		d->session_output_fd = logpipe[0];
		/* make the output read fd non-blocking */
		fcntl (d->session_output_fd, F_SETFL, O_NONBLOCK);
		VE_IGNORE_EINTR (close (logpipe[1]));
	}

	/* We must be root for this, and we are, but just to make sure */
	NEVER_FAILS_root_set_euid_egid (0, gdm_daemon_config_get_gdmgid ());
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

	/* finish reading the session output if any of it is still there */
	finish_session_output (TRUE);

	/* Now still as root make the system authfile readable by others,
	   and therefore by the gdm user */
	VE_IGNORE_EINTR (g_chmod (GDM_AUTHFILE (d), 0644));

	end_time = time (NULL);

	gdm_debug ("Session: start_time: %ld end_time: %ld",
		   (long)session_start_time, (long)end_time);

	/* 66 is a very magical number signifying failure in GDM */
	if G_UNLIKELY ((d->last_sess_status != 66) &&
		       (/* sanity */ end_time >= session_start_time) &&
		       (end_time - 10 <= session_start_time) &&
		       /* only if the X server still exist! */
		       d->servpid > 1) {
		char *msg_string;
		char *error_msg =
			_("Your session only lasted less than "
			  "10 seconds.  If you have not logged out "
			  "yourself, this could mean that there is "
			  "some installation problem or that you may "
			  "be out of diskspace.  Try logging in with "
			  "one of the failsafe sessions to see if you "
			  "can fix this problem.");

		/* FIXME: perhaps do some checking to display a better error,
		 * such as gnome-session missing and such things. */
		gdm_debug ("Session less than 10 seconds!");
		msg_string = g_strdup_printf ("type=%d$$error_msg=%s$$details_label=%s$$details_file=%s$$uid=%d$$gid=%d",
					      GTK_MESSAGE_WARNING,error_msg, 
					      (d->xsession_errors_filename != NULL) ?
					      _("View details (~/.xsession-errors file)") :
					      NULL,
					      d->xsession_errors_filename,
					      0, 0);

		gdm_slave_send_string (GDM_SOP_SHOW_ERROR_DIALOG, msg_string);

		g_free (msg_string);

	}

#ifdef WITH_CONSOLE_KIT
	if (ck_session_cookie != NULL) {
		close_ck_session (ck_session_cookie);
		g_free (ck_session_cookie);
	}
#endif

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

	in_session_stop++;

	session_started = FALSE;

	local_login = login;
	login = NULL;

	/* don't use NEVER_FAILS_ here this can be called from places
	   kind of exiting and it's ok if this doesn't work (when shouldn't
	   it work anyway? */
	seteuid (0);
	setegid (0);

	gdm_slave_send_num (GDM_SOP_SESSPID, 0);

	/* Now still as root make the system authfile not readable by others,
	   and therefore not by the gdm user */
	if (GDM_AUTHFILE (d) != NULL) {
		VE_IGNORE_EINTR (g_chmod (GDM_AUTHFILE (d), 0640));
	}

	gdm_debug ("gdm_slave_session_stop: %s on %s", local_login, d->name);

	/* Note we use the info in the structure here since if we get passed
	 * a 0 that means the process is already dead.
	 * FIXME: Maybe we should waitpid here, note make sure this will
	 * not create a hang! */
	gdm_sigchld_block_push ();
	if (d->sesspid > 1)
		kill (- (d->sesspid), SIGTERM);
	gdm_sigchld_block_pop ();

	finish_session_output (run_post_session /* do_read */);

	if (local_login == NULL)
		pwent = NULL;
	else
		pwent = getpwnam (local_login);	/* PAM overwrites our pwent */

	x_servers_file = gdm_make_filename (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR),
					    d->name, ".Xservers");

	/* if there was a session that ran, run the PostSession script */
	if (run_post_session) {
		/* Execute post session script */
		gdm_debug ("gdm_slave_session_stop: Running post session script");
		gdm_slave_exec_script (d, gdm_daemon_config_get_value_string (GDM_KEY_POSTSESSION), local_login, pwent,
				       FALSE /* pass_stdout */);
	}

	VE_IGNORE_EINTR (g_unlink (x_servers_file));
	g_free (x_servers_file);

	g_free (local_login);

	if (pwent != NULL) {
		seteuid (0); /* paranoia */
		/* Remove display from ~user/.Xauthority */
		if G_LIKELY (setegid (pwent->pw_gid) == 0 &&
			     seteuid (pwent->pw_uid) == 0) {
			gdm_auth_user_remove (d, pwent->pw_uid);
		}

		/* don't use NEVER_FAILS_ here this can be called from places
		   kind of exiting and it's ok if this doesn't work (when shouldn't
		   it work anyway? */
		seteuid (0);
		setegid (0);
	}

	logged_in_uid = -1;
	logged_in_gid = -1;

	/* things are going to be killed, so ignore errors */
	XSetErrorHandler (ignore_xerror_handler);

	gdm_verify_cleanup (d);

	in_session_stop --;

	if (need_to_quit_after_session_stop) {
		gdm_debug ("gdm_slave_session_stop: Final cleanup");

		gdm_slave_quick_exit (exit_code_to_use);
	}

#ifdef __linux__
	/* If on Linux and the runlevel is 0 or 6 and not the runlevel that
	   we were started in, then we are restarting or halting the machine.
	   Probably the user selected halt or restart from the logout
	   menu.  In this case we can really just sleep for a few seconds and
	   basically wait to be killed.  I will set the default for 30 seconds
	   and let people yell at me if this breaks something.  It shouldn't.
	   In fact it should fix things so that the login screen is not brought
	   up again and then whacked.  Waiting is safer then DISPLAY_ABORT,
	   since if we really do get this wrong, then at the worst case the
	   user will wait for a few moments. */
	if ( ! need_to_quit_after_session_stop &&
	     ! no_shutdown_check &&
	     g_access ("/sbin/runlevel", X_OK) == 0) {
		int rnl = get_runlevel ();
		if ((rnl == 0 || rnl == 6) && rnl != gdm_normal_runlevel) {
			/* this is a stupid loop, but we may be getting signals,
			   so we don't want to just do sleep (30) */
			time_t c = time (NULL);
			gdm_info (_("GDM detected a halt or restart "
				    "in progress."));
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

	if G_UNLIKELY (already_in_slave_start_jmp)
		return;

	gdm_in_signal++;

	if G_UNLIKELY (d->dsp == NULL) {
		gdm_in_signal --;
		/* huh? */
		return;
	}

	if G_UNLIKELY (in_ping) {
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
	alarm (gdm_daemon_config_get_value_int (GDM_KEY_PING_INTERVAL));

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

	if G_UNLIKELY (already_in_slave_start_jmp)
		return;

	gdm_in_signal++;

	old = geteuid ();
	if (old != 0)
		seteuid (0);

	while ((pid = waitpid (-1, &status, WNOHANG)) > 0) {
		GSList *li;

		for (li = slave_waitpids; li != NULL; li = li->next) {
			GdmWaitPid *wp = li->data;
			if (wp->pid == pid) {
				wp->pid = -1;
				if (slave_waitpid_w >= 0) {
					VE_IGNORE_EINTR (write (slave_waitpid_w, "!", 1));
				}
			}
		}

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
					slave_waitpid_notify ();
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
					/* weird error return, interpret as failure */
					if (WIFEXITED (status) &&
					    WEXITSTATUS (status) == 1)
						exit_code_to_use = DISPLAY_GREETERFAILED;
					SIGNAL_EXIT_WITH_JMP (d, JMP_JUST_QUIT_QUICKLY);
				}
			}
		} else if (pid != 0 && pid == d->sesspid) {
			d->sesspid = 0;
			if (WIFEXITED (status))
				d->last_sess_status = WEXITSTATUS (status);
			else
				d->last_sess_status = -1;
		} else if (pid != 0 && pid == d->chooserpid) {
			d->chooserpid = 0;
		} else if (pid != 0 && pid == d->servpid) {
			if (d->servstat == SERVER_RUNNING)
				gdm_server_whack_lockfile (d);
			d->servstat = SERVER_DEAD;
			d->servpid = 0;
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

			/* just in case we restart again wait at least
			   one sec to avoid races */
			if (d->sleep_before_run < 1)
				d->sleep_before_run = 1;
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
	ssize_t count;
	char **vec;
	int i;

	VE_IGNORE_EINTR (count = read (d->slave_notify_fd, buf, sizeof (buf) -1));
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
			slave_waitpid_notify ();
			unhandled_notifies =
				g_list_append (unhandled_notifies,
					       g_strdup (&s[1]));
		} else if (s[0] == GDM_SLAVE_NOTIFY_COMMAND) {
			if (strcmp (&s[1], GDM_NOTIFY_DIRTY_SERVERS) == 0) {
				/* never restart flexi servers
				 * they whack themselves */
				if (!SERVER_IS_FLEXI (d))
					remanage_asap = TRUE;
			} else if (strcmp (&s[1], GDM_NOTIFY_SOFT_RESTART_SERVERS) == 0) {
				/* never restart flexi servers,
				 * they whack themselves */
				/* FIXME: here we should handle actual
				 * restarts of flexi servers, but it probably
				 * doesn't matter */
				if (!SERVER_IS_FLEXI (d)) {
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
			} else if (strcmp (&s[1], GDM_NOTIFY_GO) == 0) {
				gdm_wait_for_go = FALSE;
			} else if (strcmp (&s[1], GDM_NOTIFY_TWIDDLE_POINTER) == 0) {
				gdm_twiddle_pointer (d);
			}
		} else if (s[0] == GDM_SLAVE_NOTIFY_RESPONSE) {
			gdm_got_ack = TRUE;
			if (gdm_ack_response)
				g_free (gdm_ack_response);

			if (s[1] == GDM_SLAVE_NOTIFY_YESNO_RESPONSE) {
				if (s[2] == '0') {
					gdm_ack_response =  g_strdup ("no");
				} else {
					gdm_ack_response =  g_strdup ("yes");
				}
			} else if (s[1] == GDM_SLAVE_NOTIFY_ASKBUTTONS_RESPONSE) {
				gdm_ack_response = g_strdup (&s[2]);
			} else if (s[1] == GDM_SLAVE_NOTIFY_QUESTION_RESPONSE) {
				gdm_ack_question_response = g_strdup (&s[2]);
			} else if (s[1] == GDM_SLAVE_NOTIFY_ERROR_RESPONSE) {
				if (s[2] != '\0') {
					gdm_ack_response = g_strdup (&s[2]);
				} else {
					gdm_ack_response = NULL;
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

/* We usually respond to fatal errors by restarting the display */
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

	if ((d->type == TYPE_STATIC ||
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

/* return true for "there was an interruption received",
   and interrupted will be TRUE if we are actually interrupted from doing what
   we want.  If FALSE is returned, just continue on as we would normally */
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
			if ((d->attached || gdm_daemon_config_get_value_bool (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN))
                            && d->timed_login_ok &&
			    ! ve_string_empty (ParsedTimedLogin) &&
                            strcmp (ParsedTimedLogin, gdm_root_user ()) != 0 &&
			    gdm_daemon_config_get_value_int (GDM_KEY_TIMED_LOGIN_DELAY) > 0) {
				do_timed_login = TRUE;
			}
			break;
		case GDM_INTERRUPT_CONFIGURE:
			if (d->attached &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_CONFIG_AVAILABLE, d->name) &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_SYSTEM_MENU, d->name) &&
			    ! ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_CONFIGURATOR))) {
				do_configurator = TRUE;
			}
			break;
		case GDM_INTERRUPT_SUSPEND:
			if (d->attached &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_SYSTEM_MENU, d->name) &&
			    ! ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_SUSPEND))) {
			    	gchar *msg = g_strdup_printf ("%s %ld", 
							      GDM_SOP_SUSPEND_MACHINE,
							      (long)getpid ());

				gdm_slave_send (msg, FALSE /* wait_for_ack */);
				g_free (msg);
			}
			/* Not interrupted, continue reading input,
			 * just proxy this to the master server */
			return TRUE;
		case GDM_INTERRUPT_LOGIN_SOUND:
			if (d->attached &&
			    ! play_login_sound (gdm_daemon_config_get_value_string (GDM_KEY_SOUND_ON_LOGIN_FILE))) {
				gdm_error (_("Login sound requested on non-local display or the play software "
					     "cannot be run or the sound does not exist"));
			}
			return TRUE;
		case GDM_INTERRUPT_SELECT_USER:
			gdm_verify_select_user (&msg[2]);
			break;
		case GDM_INTERRUPT_CANCEL:
			do_cancel = TRUE;
			break;
		case GDM_INTERRUPT_CUSTOM_CMD:
			if (d->attached &&
			    ! ve_string_empty (&msg[2])) {
				gchar *message = g_strdup_printf ("%s %ld %s", 
								  GDM_SOP_CUSTOM_CMD,
								  (long)getpid (), &msg[2]);

				gdm_slave_send (message, TRUE);
				g_free (message);
			}
			return TRUE;
		case GDM_INTERRUPT_THEME:
			g_free (d->theme_name);
			d->theme_name = NULL;
			if ( ! ve_string_empty (&msg[2]))
				d->theme_name = g_strdup (&msg[2]);
			gdm_slave_send_string (GDM_SOP_CHOSEN_THEME, &msg[2]);
			return TRUE;
		case GDM_INTERRUPT_SELECT_LANG:
			if (msg + 2) {
				const char *locale;
				const char *gdm_system_locale;

				locale = (gchar*)(msg + 3);
				gdm_system_locale = setlocale (LC_CTYPE, NULL);

				always_restart_greeter = (gboolean)(*(msg + 2));
				ve_clearenv ();
				if (!strcmp (locale, DEFAULT_LANGUAGE)) {
					locale = gdm_system_locale;
				}
				g_setenv ("GDM_LANG", locale, TRUE);
				g_setenv ("LANG", locale, TRUE);
				g_unsetenv ("LC_ALL");
				g_unsetenv ("LC_MESSAGES");
				setlocale (LC_ALL, "");
				setlocale (LC_MESSAGES, "");
				gdm_saveenv ();

				do_restart_greeter = TRUE;
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
	if G_UNLIKELY ( ! greet)
		return NULL;

	check_notifies_now ();

	if ( ! ve_string_empty (str)) {
		gdm_fdprintf (greeter_fd_out, "%c%c%s\n", STX, cmd, str);
	} else {
		gdm_fdprintf (greeter_fd_out, "%c%c\n", STX, cmd);
	}

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
	/* let the other process (greeter) do its stuff */
	sched_yield ();
#endif

	do {
		g_free (buf);
		buf = NULL;
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

	/* user responses take kind of random amount of time */
	gdm_random_tick ();

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
	/* don't use NEVER_FAILS_ here this can be called from places
	   kind of exiting and it's ok if this doesn't work (when shouldn't
	   it work anyway? */
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

	g_error ("%s", s);

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
	if (d->parent_temp_auth_file != NULL) {
		VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
	}
	g_free (d->parent_temp_auth_file);
	d->parent_temp_auth_file = NULL;
	if (old != 0)
		seteuid (old);
}

static void
create_temp_auth_file (void)
{
	if (d->type == TYPE_FLEXI_XNEST &&
	    d->parent_auth_file != NULL) {
		if (d->parent_temp_auth_file != NULL) {
			VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
		}
		g_free (d->parent_temp_auth_file);
		d->parent_temp_auth_file =
			copy_auth_file (d->server_uid,
					gdm_daemon_config_get_gdmuid (),
					d->parent_auth_file);
	}
}

static void
set_xnest_parent_stuff (void)
{
	if (d->type == TYPE_FLEXI_XNEST) {
		g_setenv ("GDM_PARENT_DISPLAY", d->parent_disp, TRUE);
		if (d->parent_temp_auth_file != NULL) {
			g_setenv ("GDM_PARENT_XAUTHORITY",
				  d->parent_temp_auth_file, TRUE);
			g_free (d->parent_temp_auth_file);
			d->parent_temp_auth_file = NULL;
		}
	}
}

static gint
gdm_slave_exec_script (GdmDisplay *d,
		       const gchar *dir,
		       const char *login,
		       struct passwd *pwent,
		       gboolean pass_stdout)
{
	pid_t pid;
	char *script;
	char *ctrun;
	gchar **argv = NULL;
	gint status;
	char *x_servers_file;

	if G_UNLIKELY (!d || ve_string_empty (dir))
		return EXIT_SUCCESS;

	script = g_build_filename (dir, d->name, NULL);
	if (g_access (script, R_OK|X_OK) != 0) {
		g_free (script);
		script = NULL;
	}
	if (script == NULL &&
	    ! ve_string_empty (d->hostname)) {
		script = g_build_filename (dir, d->hostname, NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}
	if (script == NULL &&
	    SERVER_IS_XDMCP (d)) {
		script = g_build_filename (dir, "XDMCP", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}
	if (script == NULL &&
	    SERVER_IS_FLEXI (d)) {
		script = g_build_filename (dir, "Flexi", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}
	if (script == NULL) {
		script = g_build_filename (dir, "Default", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}

	if (script == NULL) {
		return EXIT_SUCCESS;
	}

	create_temp_auth_file ();

	g_debug ("Forking extra process: %s", script);

	extra_process = pid = gdm_fork_extra ();

	switch (pid) {
	case 0:
		gdm_log_shutdown ();

		VE_IGNORE_EINTR (close (0));
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */

		if ( ! pass_stdout) {
			VE_IGNORE_EINTR (close (1));
			VE_IGNORE_EINTR (close (2));
			/* No error checking here - if it's messed the best response
			 * is to ignore & try to continue */
			gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
			gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
		}

		gdm_close_all_descriptors (3 /* from */, -1 /* except */, -1 /* except2 */);

		gdm_log_init ();

		if (login != NULL) {
			g_setenv ("LOGNAME", login, TRUE);
			g_setenv ("USER", login, TRUE);
			g_setenv ("USERNAME", login, TRUE);
		} else {
			const char *gdmuser = gdm_daemon_config_get_value_string (GDM_KEY_USER);
			g_setenv ("LOGNAME", gdmuser, TRUE);
			g_setenv ("USER", gdmuser, TRUE);
			g_setenv ("USERNAME", gdmuser, TRUE);
		}
		if (pwent != NULL) {
			if (ve_string_empty (pwent->pw_dir)) {
				g_setenv ("HOME", "/", TRUE);
				g_setenv ("PWD", "/", TRUE);
				VE_IGNORE_EINTR (g_chdir ("/"));
			} else {
				g_setenv ("HOME", pwent->pw_dir, TRUE);
				g_setenv ("PWD", pwent->pw_dir, TRUE);
				VE_IGNORE_EINTR (g_chdir (pwent->pw_dir));
				if (errno != 0) {
					VE_IGNORE_EINTR (g_chdir ("/"));
					g_setenv ("PWD", "/", TRUE);
				}
			}
			g_setenv ("SHELL", pwent->pw_shell, TRUE);
		} else {
			g_setenv ("HOME", "/", TRUE);
			g_setenv ("PWD", "/", TRUE);
			VE_IGNORE_EINTR (g_chdir ("/"));
			g_setenv ("SHELL", "/bin/sh", TRUE);
		}

		set_xnest_parent_stuff ();

		/* some env for use with the Pre and Post scripts */
		x_servers_file = gdm_make_filename (gdm_daemon_config_get_value_string (GDM_KEY_SERV_AUTHDIR),
						    d->name, ".Xservers");
		g_setenv ("X_SERVERS", x_servers_file, TRUE);
		g_free (x_servers_file);
		if (SERVER_IS_XDMCP (d))
			g_setenv ("REMOTE_HOST", d->hostname, TRUE);

		/* Runs as root */
		if (GDM_AUTHFILE (d) != NULL)
			g_setenv ("XAUTHORITY", GDM_AUTHFILE (d), TRUE);
		else
			g_unsetenv ("XAUTHORITY");
		g_setenv ("DISPLAY", d->name, TRUE);
		g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);
		g_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
		if ( ! ve_string_empty (d->theme_name))
			g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);

#ifdef HAVE_CTRUN
		ctrun = g_strdup_printf (
			"/bin/sh -c \"/usr/bin/ctrun -l child -i none %s\"",
			script);
		g_shell_parse_argv (ctrun, NULL, &argv, NULL);
		g_free (ctrun);
#else
		g_shell_parse_argv (script, NULL, &argv, NULL);
#endif

		VE_IGNORE_EINTR (execv (argv[0], argv));
		g_strfreev (argv);
		g_error (_("%s: Failed starting: %s"),
			 "gdm_slave_exec_script",
			 script);
		_exit (EXIT_SUCCESS);

	case -1:
		gdm_slave_whack_temp_auth_file ();
		g_free (script);
		g_error (_("%s: Can't fork script process!"), "gdm_slave_exec_script");
		return EXIT_SUCCESS;

	default:
		gdm_wait_for_extra (extra_process, &status);

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
gdm_slave_action_pending (void)
{
	if (do_timed_login ||
	    do_configurator ||
	    do_restart_greeter ||
	    do_cancel)
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
	gchar **argv = NULL;
	pid_t pid;

	if (s == NULL)
		return (NULL);

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

	if (str->len > 0 && str->str[str->len - 1] == '|') {
		g_string_truncate (str, str->len - 1);
		if G_UNLIKELY (pipe (pipe1) < 0) {
			gdm_error (_("%s: Failed creating pipe"),
				   "gdm_parse_enriched_login");
		} else {
			g_debug ("Forking extra process: %s", str->str);

			extra_process = pid = gdm_fork_extra ();

			switch (pid) {
			case 0:
				/* The child will write the username to stdout based on the DISPLAY
				   environment variable. */

				VE_IGNORE_EINTR (close (pipe1[0]));
				if G_LIKELY (pipe1[1] != STDOUT_FILENO)  {
					VE_IGNORE_EINTR (dup2 (pipe1[1], STDOUT_FILENO));
				}

				gdm_log_shutdown ();

				gdm_close_all_descriptors (3 /* from */, pipe1[1] /* except */, -1 /* except2 */);

				gdm_log_init ();

				/* runs as root */
				if (GDM_AUTHFILE (display) != NULL)
					g_setenv ("XAUTHORITY", GDM_AUTHFILE (display), TRUE);
				else
					g_unsetenv ("XAUTHORITY");
				g_setenv ("DISPLAY", display->name, TRUE);
				if (SERVER_IS_XDMCP (display))
					g_setenv ("REMOTE_HOST", display->hostname, TRUE);
				g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);
				g_setenv ("SHELL", "/bin/sh", TRUE);
				g_setenv ("RUNNING_UNDER_GDM", "true", TRUE);
				if ( ! ve_string_empty (d->theme_name))
					g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);

				g_shell_parse_argv (str->str, NULL, &argv, NULL);

				VE_IGNORE_EINTR (execv (argv[0], argv));
				g_strfreev (argv);
				gdm_error (_("%s: Failed executing: %s"),
					   "gdm_parse_enriched_login",
					   str->str);
				_exit (EXIT_SUCCESS);

			case -1:
				gdm_error (_("%s: Can't fork script process!"),
					   "gdm_parse_enriched_login");
				VE_IGNORE_EINTR (close (pipe1[0]));
				VE_IGNORE_EINTR (close (pipe1[1]));
				break;

			default:
				/* The parent reads username from the pipe a chunk at a time */
				VE_IGNORE_EINTR (close (pipe1[1]));
				g_string_truncate (str, 0);
				do {
					VE_IGNORE_EINTR (in_buffer_len = read (pipe1[0], in_buffer,
									       sizeof (in_buffer) - 1));
					if (in_buffer_len > 0) {
						in_buffer[in_buffer_len] = '\0';
						g_string_append (str, in_buffer);
					}
				} while (in_buffer_len > 0);

				if (str->len > 0 && str->str[str->len - 1] == '\n')
					g_string_truncate (str, str->len - 1);

				VE_IGNORE_EINTR (close (pipe1[0]));

				gdm_wait_for_extra (extra_process, NULL);
			}
		}
	}

	if (!ve_string_empty(str->str) && gdm_is_user_valid(str->str))
		return g_string_free (str, FALSE);
	else
		{
			/* "If an empty or otherwise invalid username is returned [by the script]
			 *  automatic login [and timed login] is not performed." -- GDM manual 
			 */
			/* fixme: also turn off automatic login */
			gdm_daemon_config_set_value_bool(GDM_KEY_TIMED_LOGIN_ENABLE, FALSE);
			d->timed_login_ok = FALSE;
			do_timed_login = FALSE;
			g_string_free(str, TRUE);
			return NULL;
		}
}

static void
gdm_slave_handle_notify (const char *msg)
{
	int val;

	gdm_debug ("Handling slave notify: '%s'", msg);

	if (sscanf (msg, GDM_NOTIFY_ALLOW_ROOT " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_ALLOW_ROOT, val);
	} else if (sscanf (msg, GDM_NOTIFY_ALLOW_REMOTE_ROOT " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_ALLOW_REMOTE_ROOT, val);
	} else if (sscanf (msg, GDM_NOTIFY_ALLOW_REMOTE_AUTOLOGIN " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN, val);
	} else if (sscanf (msg, GDM_NOTIFY_SYSTEM_MENU " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_SYSTEM_MENU, val);
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_CONFIG_AVAILABLE " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_CONFIG_AVAILABLE, val);
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_CHOOSER_BUTTON " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_CHOOSER_BUTTON, val);
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (sscanf (msg, GDM_NOTIFY_RETRY_DELAY " %d", &val) == 1) {
		gdm_daemon_config_set_value_int (GDM_KEY_RETRY_DELAY, val);
	} else if (sscanf (msg, GDM_NOTIFY_DISALLOW_TCP " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_DISALLOW_TCP, val);
		remanage_asap = TRUE;
	} else if (strncmp (msg, GDM_NOTIFY_GREETER " ",
			    strlen (GDM_NOTIFY_GREETER) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_GREETER, ((gchar *)&msg[strlen (GDM_NOTIFY_GREETER) + 1]));

		if (d->attached) {
			do_restart_greeter = TRUE;
			if (restart_greeter_now) {
				; /* will get restarted later */
			} else if (d->type == TYPE_STATIC) {
				/* FIXME: can't handle flexi servers like this
				 * without going all cranky */
				if ( ! d->logged_in) {
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				} else {
					remanage_asap = TRUE;
				}
			}
		}
	} else if (strncmp (msg, GDM_NOTIFY_CUSTOM_CMD_TEMPLATE,
			    strlen (GDM_NOTIFY_CUSTOM_CMD_TEMPLATE)) == 0) {
    	        if (sscanf (msg, GDM_NOTIFY_CUSTOM_CMD_TEMPLATE "%d", &val) == 1) {
			gchar * key_string = g_strdup_printf("%s%d=", GDM_KEY_CUSTOM_CMD_TEMPLATE, val);
			/* This assumes that the number of commands is < 100, i.e two digits
			   if that is not the case then this will fail */
			gdm_daemon_config_set_value_string (key_string, ((gchar *)&msg[strlen (GDM_NOTIFY_CUSTOM_CMD_TEMPLATE) + 2]));
			g_free(key_string);

			if (d->attached) {
				do_restart_greeter = TRUE;
				if (restart_greeter_now) {
					; /* will get restarted later */
				} else if (d->type == TYPE_STATIC) {
					/* FIXME: can't handle flexi servers like this
					 * without going all cranky */
					if ( ! d->logged_in) {
						gdm_slave_quick_exit (DISPLAY_REMANAGE);
					} else {
						remanage_asap = TRUE;
					}
				}
			}
		}
	} else if (strncmp (msg, GDM_NOTIFY_REMOTE_GREETER " ",
			    strlen (GDM_NOTIFY_REMOTE_GREETER) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_REMOTE_GREETER,
						    (gchar *)(&msg[strlen (GDM_NOTIFY_REMOTE_GREETER) + 1]));
		if ( ! d->attached) {
			do_restart_greeter = TRUE;
			if (restart_greeter_now) {
				; /* will get restarted later */
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
	} else if ((strncmp (msg, GDM_NOTIFY_TIMED_LOGIN " ",
			     strlen (GDM_NOTIFY_TIMED_LOGIN) + 1) == 0) ||
	           (strncmp (msg, GDM_NOTIFY_TIMED_LOGIN_DELAY " ",
			     strlen (GDM_NOTIFY_TIMED_LOGIN_DELAY) + 1) == 0) ||
	           (strncmp (msg, GDM_NOTIFY_TIMED_LOGIN_ENABLE " ",
			     strlen (GDM_NOTIFY_TIMED_LOGIN_ENABLE) + 1) == 0)) {
		do_restart_greeter = TRUE;
		/* FIXME: this is fairly nasty, we should handle this nicer   */
		/* FIXME: can't handle flexi servers without going all cranky */
		if (d->type == TYPE_STATIC || d->type == TYPE_XDMCP) {
			if ( ! d->logged_in) {
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			} else {
				remanage_asap = TRUE;
			}
		}
	} else if (strncmp (msg, GDM_NOTIFY_SOUND_ON_LOGIN_FILE " ",
			    strlen (GDM_NOTIFY_SOUND_ON_LOGIN_FILE) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_SOUND_ON_LOGIN_FILE,
						    (gchar *)(&msg[strlen (GDM_NOTIFY_SOUND_ON_LOGIN_FILE) + 1]));
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (strncmp (msg, GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE " ",
			    strlen (GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE,
						    (gchar *)(&msg[strlen (GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE) + 1]));
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (strncmp (msg, GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE " ",
			    strlen (GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE,
						    (gchar *)(&msg[strlen (GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE) + 1]));
		if (d->greetpid > 1)
			kill (d->greetpid, SIGHUP);
	} else if (strncmp (msg, GDM_NOTIFY_GTK_MODULES_LIST " ",
			    strlen (GDM_NOTIFY_GTK_MODULES_LIST) + 1) == 0) {
		gdm_daemon_config_set_value_string (GDM_KEY_GTK_MODULES_LIST,
						    (gchar *)(&msg[strlen (GDM_NOTIFY_GTK_MODULES_LIST) + 1]));

		if (gdm_daemon_config_get_value_bool (GDM_KEY_ADD_GTK_MODULES)) {
			do_restart_greeter = TRUE;
			if (restart_greeter_now) {
				; /* will get restarted later */
			} else if (d->type == TYPE_STATIC) {
				/* FIXME: can't handle flexi servers like this
				 * without going all cranky */
				if ( ! d->logged_in) {
					gdm_slave_quick_exit (DISPLAY_REMANAGE);
				} else {
					remanage_asap = TRUE;
				}
			}
		}
	} else if (sscanf (msg, GDM_NOTIFY_ADD_GTK_MODULES " %d", &val) == 1) {
		gdm_daemon_config_set_value_bool (GDM_KEY_ADD_GTK_MODULES, val);

		do_restart_greeter = TRUE;
		if (restart_greeter_now) {
			; /* will get restarted later */
		} else if (d->type == TYPE_STATIC) {
			/* FIXME: can't handle flexi servers like this
			 * without going all cranky */
			if ( ! d->logged_in) {
				gdm_slave_quick_exit (DISPLAY_REMANAGE);
			} else {
				remanage_asap = TRUE;
			}
		}
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

#ifdef HAVE_TSOL
static gboolean
gdm_can_i_assume_root_role (struct passwd *pwent)
{
	userattr_t *uattr = NULL;
	char *freeroles = NULL;
	char *roles = NULL;
	char *role = NULL;

	uattr = getuseruid (pwent->pw_uid);
	if G_UNLIKELY (uattr == NULL)
		return FALSE;

	freeroles = roles = g_strdup (kva_match (uattr->attr, USERATTR_ROLES_KW));
	if (roles == NULL) {
		return FALSE;
	}

	while ((role = strtok (roles, ",")) != NULL) {
		roles = NULL;
		if (!strncmp (role, "root", 4)) {
			g_free (freeroles);
			g_free (role);
			return TRUE;
		}
	}
	g_free (freeroles);
	g_free (role);
	return FALSE;
}
#endif /* HAVE_TSOL */


/* gdm_is_user_valid() mostly copied from gui/gdmuser.c */
gboolean
gdm_is_user_valid (const char *username)
{
	return (NULL != getpwnam (username));
}
