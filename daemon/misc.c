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
#include <gnome.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <grp.h>
#include <sys/types.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"

/* Configuration option variables */
extern gchar *GdmPidFile;
extern gboolean GdmDebug;
extern GSList *displays;
extern int gdm_xdmcpfd;

extern char **environ;

extern pid_t extra_process;
extern int extra_status;


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
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* Log to both syslog and stderr */
    syslog (LOG_CRIT, "%s", s);
    fprintf (stderr, "%s\n", s);
    fflush (stderr);

    g_free (s);

    if (GdmPidFile != NULL)
	    unlink (GdmPidFile);
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
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_INFO, "%s", s);
    
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
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    
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
    gchar *s;

    if (/*0 && */! GdmDebug) 
	return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* UGLY DEBUGGING HACK! */
    
    /*{ FILE *fp = fopen ("/tmp/foo.gdm", "a"); fprintf (fp, "%s\n", s); fflush (fp); fclose (fp); };*/
    
    syslog (LOG_ERR, "%s", s);	/* FIXME: LOG_DEBUG */
    
    g_free (s);
}

/* clear environment, but keep the i18n ones,
 * note that this leaks memory so only use before exec */
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
	}

	ve_clearenv ();

	for (li = envs; li != NULL; li = li->next) {
		putenv (li->data);
	}

	g_list_free (envs);
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

	dialog = gnome_is_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("gdialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("whiptail");
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

	dialog = gnome_is_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("gdialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("whiptail");
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

		tempfd = mkstemp (tempname);
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
		int i;

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close (i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		if (de_setuid) {
			seteuid (getuid ());
			setegid (getgid ());
		}

		if (no_display) {
			ve_unsetenv ("DISPLAY");
			ve_unsetenv ("XAUTHORITY");
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

pid_t
gdm_fork_extra (void)
{
	pid_t pid;
	gdm_sigchld_block_push ();

	gdm_sigterm_block_push ();
	pid = extra_process = fork ();
	gdm_sigterm_block_pop ();

	gdm_sigchld_block_pop ();

	return pid;
}

void
gdm_wait_for_extra (int *status)
{
	gdm_sigchld_block_push ();

	if (extra_process > 0) {
		waitpid (extra_process, &extra_status, 0);
	}
	extra_process = -1;

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
	ifc.ifc_buf = buf = g_malloc (ifc.ifc_len);
	if (ioctl (sockfd, SIOCGIFCONF, &ifc) < 0) {
		gdm_error (_("%s: Cannot get local addresses!"),
			   "gdm_peek_local_address_list");
		g_free (buf);
		if (sockfd != gdm_xdmcpfd)
			close (sockfd);
		return NULL;
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

	if (gethostname (hostbuf, BUFSIZ-1) != 0) {
		gdm_error (_("%s: Could not get server hostname: %s!"),
			   "gdm_peek_local_address_list",
			   g_strerror (errno));
		return NULL;
	}
	he = gethostbyname (hostbuf);
	if (he == NULL) {
		gdm_error (_("%s: Could not get address from hostname!"),
			   "gdm_peek_local_address_list");
		return NULL;
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



/* EOF */
