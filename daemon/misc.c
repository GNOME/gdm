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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include <libgnome/libgnome.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <grp.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"

/* Configuration option variables */
extern gchar *GdmPidFile;
extern gchar *GdmLocalNoPasswordUsers;
extern gboolean GdmDebug;
extern GSList *displays;
extern int gdm_xdmcpfd;

extern char **environ;

extern pid_t extra_process;
extern int extra_status;
extern pid_t gdm_main_pid;
extern gboolean preserve_ld_vars;
extern int gdm_in_signal;

extern char *gdm_charset;

static void
do_syslog (int type, const char *s)
{
	if (gdm_in_signal > 0) {
		char *m = g_strdup_printf (GDM_SOP_SYSLOG " %ld %d %s",
					   (long)getpid (), type, s);
		gdm_slave_send (m, FALSE);
		g_free (m);
	} else {
		syslog (type, "%s", s);
	}
}


/**
 * gdm_fail:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Logs fatal error condition and aborts master daemon.  Also sleeps
 * for 30 seconds to avoid looping if gdm is started by init.  
 */

void 
gdm_fail (const gchar *format, ...)
{
    va_list args;
    char *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* Log to both syslog and stderr */
    do_syslog (LOG_CRIT, s);
    if (getpid () == gdm_main_pid) {
	    gdm_fdprintf (2, "%s\n", s);
    }

    g_free (s);

    /* If main process do final cleanup to kill all processes */
    if (getpid () == gdm_main_pid) {
	    gdm_final_cleanup ();
    } else if ( ! gdm_slave_final_cleanup ()) {
	    /* If we weren't even a slave do some random cleanup only */
	    /* FIXME: is this all fine? */
	    gdm_sigchld_block_push ();
	    if (extra_process > 1 && extra_process != getpid ()) {
		    /* we sigterm extra processes, and we
		     * don't wait */
		    kill (-(extra_process), SIGTERM);
		    extra_process = 0;
	    }
	    gdm_sigchld_block_pop ();
    }

    closelog ();

    /* Slow down respawning if we're started from init */
    if (getppid() == 1)
	sleep (30);

    exit (EXIT_FAILURE);
}


/**
 * gdm_info:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log non-fatal information to syslog
 */

void 
gdm_info (const gchar *format, ...)
{
    va_list args;
    char *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    do_syslog (LOG_INFO, s);
    
    g_free (s);
}


/**
 * gdm_error:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log non-fatal error condition to syslog
 */

void 
gdm_error (const gchar *format, ...)
{
    va_list args;
    char *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    do_syslog (LOG_ERR, s);
    
    g_free (s);
}


/**
 * gdm_debug:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log debug information to syslog if debugging is enabled.
 */

void 
gdm_debug (const gchar *format, ...)
{
    va_list args;
    char *s;

    if ( ! GdmDebug) 
	return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    do_syslog (LOG_ERR, s);	/* Maybe should be LOG_DEBUG, but then normally
				 * you wouldn't get it in the log.  ??? */
    
    g_free (s);
}

void 
gdm_fdprintf (int fd, const gchar *format, ...)
{
	va_list args;
	gchar *s;

	va_start (args, format);
	s = g_strdup_vprintf (format, args);
	va_end (args);

	write (fd, s, strlen (s));

	g_free (s);
}

/* clear environment, but keep the i18n ones,
 * note that this leaks memory so only use before exec
 * (keep LD_* if preserve_ld_vars is true) */
void
gdm_clearenv_no_lang (void)
{
	int i;
	GList *li, *envs = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		char *env = environ[i];
		if (strncmp (env, "LC_", 3) == 0 ||
		    strncmp (env, "LANG", 4) == 0 ||
		    strncmp (env, "LINGUAS", 7) == 0)
			envs = g_list_prepend (envs, g_strdup (env));
		if (preserve_ld_vars &&
		    strncmp (env, "LD_", 3) == 0)
			envs = g_list_prepend (envs, g_strdup (env));
	}

	gnome_clearenv ();

	for (li = envs; li != NULL; li = li->next) {
		putenv (li->data);
	}

	g_list_free (envs);
}

/* clear environment completely
 * note that this leaks memory so only use before exec
 * (keep LD_* if preserve_ld_vars is true) */
void
gdm_clearenv (void)
{
	int i;
	GList *li, *envs = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		char *env = environ[i];
		if (preserve_ld_vars &&
		    strncmp (env, "LD_", 3) == 0)
			envs = g_list_prepend (envs, g_strdup (env));
	}

	gnome_clearenv ();

	for (li = envs; li != NULL; li = li->next) {
		putenv (li->data);
	}

	g_list_free (envs);
}

static GList *stored_env = NULL;

void
gdm_saveenv (void)
{
	int i;

	g_list_foreach (stored_env, (GFunc)g_free, NULL);
	g_list_free (stored_env);
	stored_env = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		char *env = environ[i];
		stored_env = g_list_prepend (stored_env, g_strdup (env));
	}
}

/* leaks */
void
gdm_restoreenv (void)
{
	GList *li;

	gnome_clearenv ();

	for (li = stored_env; li != NULL; li = li->next) {
		putenv (g_strdup (li->data));
	}
}

/* Evil function to figure out which display number is free */
int
gdm_get_free_display (int start, uid_t server_uid)
{
	int sock;
	int i;
	struct sockaddr_in serv_addr = {0}; 

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	/* Cap this at 3000, I'm not sure we can ever seriously
	 * go that far */
	for (i = start; i < 3000; i ++) {
		GSList *li;
		struct stat s;
		char buf[256];

		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *dsp = li->data;
			if (SERVER_IS_LOCAL (dsp) &&
			    dsp->dispnum == i)
				break;
		}
		if (li != NULL) {
			/* found one */
			continue;
		}

		sock = socket (AF_INET, SOCK_STREAM, 0);

		serv_addr.sin_port = htons (6000 + i);

		errno = 0;
		if (connect (sock, (struct sockaddr *)&serv_addr,
			     sizeof (serv_addr)) >= 0 ||
		    errno != ECONNREFUSED) {
			close (sock);
			continue;
		}
		close (sock);

		/* if starting as root, we'll be able to overwrite any
		 * stale sockets or lock files, but a user may not be
		 * able to */
		if (server_uid > 0) {
			g_snprintf (buf, sizeof (buf),
				    "/tmp/.X11-unix/X%d", i);
			if (stat (buf, &s) == 0 &&
			    s.st_uid != server_uid) {
				continue;
			}

			g_snprintf (buf, sizeof (buf),
				    "/tmp/.%d-lock", i);
			if (stat (buf, &s) == 0 &&
			    s.st_uid != server_uid) {
				continue;
			}
		}

		return i;
	}

	return -1;
}

gboolean
gdm_text_message_dialog (const char *msg)
{
	char *dialog; /* do we have dialog?*/

	if (access (EXPANDED_SBINDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	dialog = g_find_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("gdialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("whiptail");
	if (dialog != NULL) {
		char *argv[7];

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = dialog;
		argv[2] = "--msgbox";
		argv[3] = (char *)msg;
		argv[4] = "11";
		argv[5] = "70";
		argv[6] = NULL;

		/* make sure gdialog wouldn't get confused */
		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (dialog);
			return FALSE;
		}

		g_free (dialog);
	} else {
		char *argv[5];

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = "/bin/sh";
		argv[2] = "-c";
		argv[3] = g_strdup_printf
			("clear ; "
			 "echo \"%s\" ; read ; clear",
			 msg);
		argv[4] = NULL;

		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (argv[3]);
			return FALSE;
		}
		g_free (argv[3]);
	}
	return TRUE;
}

gboolean
gdm_text_yesno_dialog (const char *msg, gboolean *ret)
{
	char *dialog; /* do we have dialog?*/

	if (access (EXPANDED_SBINDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	if (ret != NULL)
		*ret = FALSE;

	dialog = g_find_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("gdialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("whiptail");
	if (dialog != NULL) {
		char *argv[7];
		int retint;

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = dialog;
		argv[2] = "--yesno";
		argv[3] = (char *)msg;
		argv[4] = "11";
		argv[5] = "70";
		argv[6] = NULL;

		/* will unset DISPLAY and XAUTHORITY if they exist
		 * so that gdialog (if used) doesn't get confused */
		retint = gdm_exec_wait (argv, TRUE /* no display */,
					TRUE /* de_setuid */);
		if (retint < 0) {
			g_free (dialog);
			return FALSE;
		}

		if (ret != NULL)
			*ret = (retint == 0) ? TRUE : FALSE;

		g_free (dialog);

		return TRUE;
	} else {
		char tempname[] = "/tmp/gdm-yesno-XXXXXX";
		int tempfd;
		FILE *fp;
		char buf[256];
		char *argv[5];

		tempfd = g_mkstemp (tempname);
		if (tempfd < 0)
			return FALSE;

		close (tempfd);

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = "/bin/sh";
		argv[2] = "-c";
		argv[3] = g_strdup_printf
			("clear ; "
			 "echo \"%s\" ; echo ; echo \"%s\" ; "
			 "read RETURN ; echo $RETURN > %s ; clear'",
			 msg,
			 /* Translators, don't translate the 'y' and 'n' */
			 _("y = Yes or n = No? >"),
			 tempname);
		argv[4] = NULL;

		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (argv[3]);
			return FALSE;
		}
		g_free (argv[3]);

		if (ret != NULL) {
			fp = fopen (tempname, "r");
			if (fp != NULL) {
				if (fgets (buf, sizeof (buf), fp) != NULL &&
				    (buf[0] == 'y' || buf[0] == 'Y'))
					*ret = TRUE;
				fclose (fp);
			} else {
				return FALSE;
			}
		}

		unlink (tempname);

		return TRUE;
	}
}

int
gdm_exec_wait (char * const *argv, gboolean no_display,
	       gboolean de_setuid)
{
	int status;
	pid_t pid;

	if (argv == NULL ||
	    argv[0] == NULL ||
	    access (argv[0], X_OK) != 0)
		return -1;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		closelog ();

		gdm_close_all_descriptors (0 /* from */, -1 /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		if (de_setuid) {
			gdm_desetuid ();
		}

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		if (no_display) {
			gnome_unsetenv ("DISPLAY");
			gnome_unsetenv ("XAUTHORITY");
		}
		
		execv (argv[0], argv);

		_exit (-1);
	}

	if (pid < 0)
		return -1;

	gdm_wait_for_extra (&status);

	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return -1;
}

static int sigchld_blocked = 0;
static sigset_t sigchldblock_mask, sigchldblock_oldmask;

static int sigterm_blocked = 0;
static sigset_t sigtermblock_mask, sigtermblock_oldmask;

static int sigusr2_blocked = 0;
static sigset_t sigusr2block_mask, sigusr2block_oldmask;

void
gdm_sigchld_block_push (void)
{
	sigchld_blocked ++;

	if (sigchld_blocked == 1) {
		/* Set signal mask */
		sigemptyset (&sigchldblock_mask);
		sigaddset (&sigchldblock_mask, SIGCHLD);
		sigprocmask (SIG_BLOCK, &sigchldblock_mask, &sigchldblock_oldmask);
	}
}

void
gdm_sigchld_block_pop (void)
{
	sigchld_blocked --;

	if (sigchld_blocked == 0) {
		/* reset signal mask back */
		sigprocmask (SIG_SETMASK, &sigchldblock_oldmask, NULL);
	}
}

void
gdm_sigterm_block_push (void)
{
	sigterm_blocked ++;

	if (sigterm_blocked == 1) {
		/* Set signal mask */
		sigemptyset (&sigtermblock_mask);
		sigaddset (&sigtermblock_mask, SIGTERM);
		sigaddset (&sigtermblock_mask, SIGINT);
		sigaddset (&sigtermblock_mask, SIGHUP);
		sigprocmask (SIG_BLOCK, &sigtermblock_mask, &sigtermblock_oldmask);
	}
}

void
gdm_sigterm_block_pop (void)
{
	sigterm_blocked --;

	if (sigterm_blocked == 0) {
		/* reset signal mask back */
		sigprocmask (SIG_SETMASK, &sigtermblock_oldmask, NULL);
	}
}

void
gdm_sigusr2_block_push (void)
{
	sigusr2_blocked ++;

	if (sigusr2_blocked == 1) {
		/* Set signal mask */
		sigemptyset (&sigusr2block_mask);
		sigaddset (&sigusr2block_mask, SIGUSR2);
		sigprocmask (SIG_BLOCK, &sigusr2block_mask, &sigusr2block_oldmask);
	}
}

void
gdm_sigusr2_block_pop (void)
{
	sigusr2_blocked --;

	if (sigusr2_blocked == 0) {
		/* reset signal mask back */
		sigprocmask (SIG_SETMASK, &sigusr2block_oldmask, NULL);
	}
}

pid_t
gdm_fork_extra (void)
{
	pid_t pid;
	gdm_sigchld_block_push ();

	gdm_sigterm_block_push ();
	pid = extra_process = fork ();
	if (pid < 0)
		extra_process = 0;
	gdm_sigterm_block_pop ();

	gdm_sigchld_block_pop ();

	if (pid == 0) {
		/* In the child setup empty mask and set all signals to
		 * default values */
		gdm_unset_signals ();

		/* Also make a new process group so that we may use
		 * kill -(extra_process) to kill extra process and all it's
		 * possible children */
		setsid ();
	}

	return pid;
}

void
gdm_wait_for_extra (int *status)
{
	gdm_sigchld_block_push ();

	if (extra_process > 0) {
		ve_waitpid_no_signal (extra_process, &extra_status, 0);
	}
	extra_process = 0;

	if (status != NULL)
		*status = extra_status;

	gdm_sigchld_block_pop ();
}

/* done before each login.  This can do so sanity ensuring,
 * one of the things it does now is make sure /tmp/.ICE-unix
 * exists and has the correct permissions */
void
gdm_ensure_sanity (void)
{
	mode_t old_umask;

	/* The /tmp/.ICE-unix check, note that we do
	 * ignore errors, since it's not deadly to run
	 * if we can't perform this task :) */
	old_umask = umask (0);

        if (mkdir ("/tmp/.ICE-unix", 0777) == 0) {
		/* Make sure it is root */
		if (chown ("/tmp/.ICE-unix", 0, 0) == 0)
			chmod ("/tmp/.ICE-unix", 01777);
        } else {
		struct stat s;
		if (lstat ("/tmp/.ICE-unix", &s) == 0 &&
		    S_ISDIR (s.st_mode)) {
			/* Make sure it is root and sticky */
			if (chown ("/tmp/.ICE-unix", 0, 0) == 0)
				chmod ("/tmp/.ICE-unix", 01777);
		}
	}

	umask (old_umask);
}

const GList *
gdm_peek_local_address_list (void)
{
	static GList *the_list = NULL;
	static time_t last_time = 0;
	struct in_addr *addr;
#ifdef SIOCGIFCONF
	struct sockaddr_in *sin;
	struct ifconf ifc;
	struct ifreq *ifr;
	char *buf;
	int num;
	int sockfd;
#else /* SIOCGIFCONF */
	char hostbuf[BUFSIZ];
	struct hostent *he;
	int i;
#endif

	/* don't check more then every 5 seconds */
	if (last_time + 5 > time (NULL))
		return the_list;

	g_list_foreach (the_list, (GFunc)g_free, NULL);
	g_list_free (the_list);
	the_list = NULL;

	last_time = time (NULL);

#ifdef SIOCGIFCONF
	if (gdm_xdmcpfd > 0)
		sockfd = gdm_xdmcpfd;
	else
		/* Open a bogus socket */
		sockfd = socket (AF_INET, SOCK_DGRAM, 0); /* UDP */

#ifdef SIOCGIFNUM
	if (ioctl (sockfd, SIOCGIFNUM, &num) < 0) {
		num = 64;
	}
#else
	num = 64;
#endif

	ifc.ifc_len = sizeof(struct ifreq) * num;
	ifc.ifc_buf = buf = g_malloc0 (ifc.ifc_len);
	if (ioctl (sockfd, SIOCGIFCONF, &ifc) < 0) {
		gdm_error (_("%s: Cannot get local addresses!"),
			   "gdm_peek_local_address_list");
		g_free (buf);
		if (sockfd != gdm_xdmcpfd)
			close (sockfd);
		addr = g_new0 (struct in_addr, 1);
		addr->s_addr = INADDR_LOOPBACK;
		return g_list_append (NULL, addr);
	}

	ifr = ifc.ifc_req;
	num = ifc.ifc_len / sizeof(struct ifreq);
	for (; num-- > 0; ifr++) {
		if (ioctl (sockfd, SIOCGIFFLAGS, ifr) < 0 ||
		    ! (ifr->ifr_flags & IFF_UP))
			continue;
#ifdef IFF_UNNUMBERED
		if (ifr->ifr_flags & IFF_UNNUMBERED)
			continue;
#endif
		if (ioctl (sockfd, SIOCGIFADDR, ifr) < 0)
			continue;

		sin = (struct sockaddr_in *)&ifr->ifr_addr;

		if (sin->sin_family != AF_INET ||
		    sin->sin_addr.s_addr == INADDR_ANY ||
		    sin->sin_addr.s_addr == INADDR_LOOPBACK)
			continue;
		addr = g_new0 (struct in_addr, 1);
		memcpy (addr, &(sin->sin_addr.s_addr),
			sizeof (struct in_addr));
		the_list = g_list_append (the_list, addr);
	}

	if (sockfd != gdm_xdmcpfd)
		close (sockfd);

	g_free (buf);
#else /* SIOCGIFCONF */
	/* host based fallback, will likely only get 127.0.0.1 i think */

	hostbuf[BUFSIZ-1] = '\0';
	if (gethostname (hostbuf, BUFSIZ-1) != 0) {
		gdm_debug ("%s: Could not get server hostname: %s!",
			   "gdm_peek_local_address_list",
			   g_strerror (errno));
		addr = g_new0 (struct in_addr, 1);
		addr->s_addr = INADDR_LOOPBACK;
		return g_list_append (NULL, addr);
	}
	he = gethostbyname (hostbuf);
	if (he == NULL) {
		gdm_debug ("%s: Could not get address from hostname!",
			   "gdm_peek_local_address_list");
		addr = g_new0 (struct in_addr, 1);
		addr->s_addr = INADDR_LOOPBACK;
		return g_list_append (NULL, addr);
	}
	for (i = 0; he->h_addr_list[i] != NULL; i++) {
		struct in_addr *laddr = (struct in_addr *)he->h_addr_list[i];

		addr = g_new0 (struct in_addr, 1);
		memcpy (addr, laddr, sizeof (struct in_addr));
		the_list = g_list_append (the_list, addr);
	}
#endif

	return the_list;
}

gboolean
gdm_is_local_addr (struct in_addr *ia)
{
	const char lo[] = {127,0,0,1};

	if (ia->s_addr == INADDR_LOOPBACK ||
	    memcmp (&ia->s_addr, lo, 4) == 0) {
		return TRUE;
	} else {
		const GList *list = gdm_peek_local_address_list ();

		while (list != NULL) {
			struct in_addr *addr = list->data;

			if (memcmp (&ia->s_addr, &addr->s_addr, 4) == 0) {
				return TRUE;
			}

			list = list->next;
		}

		return FALSE;
	}
}

gboolean
gdm_is_loopback_addr (struct in_addr *ia)
{
	const char lo[] = {127,0,0,1};

	if (ia->s_addr == INADDR_LOOPBACK ||
	    memcmp (&ia->s_addr, lo, 4) == 0) {
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
gdm_setup_gids (const char *login, gid_t gid)
{
	/* FIXME: perhaps for *BSD there should be setusercontext
	 * stuff here */
	if (setgid (gid) < 0)  {
		gdm_error (_("Could not setgid %d. Aborting."), (int)gid);
		return FALSE;
	}

	if (initgroups (login, gid) < 0) {
		gdm_error (_("initgroups() failed for %s. Aborting."), login);
		return FALSE;
	}

	return TRUE;
}

void
gdm_desetuid (void)
{
	uid_t uid = getuid (); 
	gid_t gid = getgid (); 

#ifdef HAVE_SETRESUID
	{
		int setresuid(uid_t ruid, uid_t euid, uid_t suid);
		int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
		setresuid (uid, uid, uid);
		setresgid (gid, gid, gid);
	}
#else
	seteuid (getuid ());
	setegid (getgid ());
#endif
}

gboolean
gdm_test_opt (const char *cmd, const char *help, const char *option)
{
	char *q;
	char *full;
	char buf[1024];
	FILE *fp;
	static GString *cache = NULL;
	static char *cached_cmd = NULL;
	gboolean got_it;

	if (cached_cmd != NULL &&
	    strcmp (cached_cmd, cmd) == 0) {
		char *p = strstr (ve_sure_string (cache->str), option);
		char end;
		if (p == NULL)
			return FALSE;
		/* must be a full word */
		end = *(p + strlen (option));
		if ((end >= 'a' && end <= 'z') ||
		    (end >= 'A' && end <= 'Z') ||
		    (end >= '0' && end <= '9') ||
		    end == '_')
			return FALSE;
		return TRUE;
	}

	g_free (cached_cmd);
	cached_cmd = g_strdup (cmd);
	if (cache != NULL)
		g_string_assign (cache, "");
	else
		cache = g_string_new (NULL);

	q = g_shell_quote (cmd);

	full = g_strdup_printf ("%s %s 2>&1", q, help);
	g_free (q);

	fp = popen (full, "r");
	g_free (full);

	if (fp == NULL)
		return FALSE;

	got_it = FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p;
		char end;

		g_string_append (cache, buf);

		if (got_it)
			continue;

		p = strstr (buf, option);
		if (p == NULL)
			continue;
		/* must be a full word */
		end = *(p + strlen (option));
		if ((end >= 'a' && end <= 'z') ||
		    (end >= 'A' && end <= 'Z') ||
		    (end >= '0' && end <= '9') ||
		    end == '_')
			continue;

		got_it = TRUE;
	}
	fclose (fp);
	return got_it;
}

int
gdm_fdgetc (int fd)
{
	char buf[1];
	int bytes;

	bytes = read (fd, buf, 1);
	if (bytes != 1)
		return EOF;
	else
		return (int)buf[0];
}

char *
gdm_fdgets (int fd)
{
	int c;
	int bytes = 0;
	GString *gs = g_string_new (NULL);
	for (;;) {
		c = gdm_fdgetc (fd);
		if (c == '\n')
			return g_string_free (gs, FALSE);
		/* on EOF */
		if (c < 0) {
			if (bytes == 0) {
				g_string_free (gs, TRUE);
				return NULL;
			} else {
				return g_string_free (gs, FALSE);
			}
		} else {
			bytes ++;
			g_string_append_c (gs, c);
		}
	}
}

void
gdm_close_all_descriptors (int from, int except)
{
	int i;
	int max = sysconf (_SC_OPEN_MAX);
	for (i = from; i < max; i++) {
		if (i != except)
			close(i);
	}
}

gboolean
gdm_is_a_no_password_user (const char *user)
{
	char **vector = NULL;
	int i;

	if (ve_string_empty (GdmLocalNoPasswordUsers) ||
	    ve_string_empty (user) ||
	    strcmp (user, "root") == 0)
		return FALSE;

	vector = g_strsplit (GdmLocalNoPasswordUsers, ",", -1);
	for (i = 0; vector[i] != NULL; i++) {
		if (strcmp (vector[i], user) == 0) {
			g_strfreev (vector);
			return TRUE;
		}
	}
	g_strfreev (vector);
	return FALSE;
}

int
gdm_open_dev_null (mode_t mode)
{
	int ret;
	ret = open ("/dev/null", mode);
	if (ret < 0) {
		gdm_fail ("Cannot open /dev/null, system on crack!");
	}

	return ret;
}

void
gdm_unset_signals (void)
{
	sigset_t mask; 

	sigemptyset (&mask);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	signal (SIGUSR1, SIG_DFL);
	signal (SIGUSR2, SIG_DFL);
	signal (SIGCHLD, SIG_DFL);
	signal (SIGTERM, SIG_DFL);
	signal (SIGPIPE, SIG_DFL);
	signal (SIGALRM, SIG_DFL);
	signal (SIGHUP, SIG_DFL);
}

static char *
ascify (const char *text)
{
	unsigned char *p;
	char *t = g_strdup (text);
	for (p = (unsigned char *)t; p != NULL && *p != '\0'; p++) {
		if (*p > 127)
			*p = '?';
	}
	return t;
}

char *
gdm_locale_to_utf8 (const char *text)
{
	GIConv cd;
	char *out;
	GError *error = NULL;

	if (gdm_charset == NULL) {
		return g_strdup (text);
	}

	cd = g_iconv_open ("UTF-8", gdm_charset);
	if (cd == (GIConv)(-1)) {
		return ascify (text);
	}

	out = g_convert_with_iconv (text, -1, cd, NULL, NULL, &error);
	g_iconv_close (cd);
	if (out == NULL) {
		return ascify (text);
	}
	return out;
}

char *
gdm_locale_from_utf8 (const char *text)
{
	GIConv cd;
	char *out;

	if (gdm_charset == NULL) {
		return g_strdup (text);
	}

	cd = g_iconv_open (gdm_charset, "UTF-8");
	if (cd == (GIConv)(-1)) {
		return ascify (text);
	}

	out = g_convert_with_iconv (text, -1, cd, NULL, NULL, NULL);
	g_iconv_close (cd);
	if (out == NULL)
		return ascify (text);
	return out;
}



/* EOF */
