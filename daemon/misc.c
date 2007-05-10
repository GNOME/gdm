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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_DEFOPEN
#include <deflt.h>
#endif

#include <X11/Xlib.h>

#include <glib/gi18n.h>

#include "gdm.h"
#include "misc.h"
#include "xdmcp.h"
#include "slave.h"

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-daemon-config.h"

extern char **environ;

#ifdef ENABLE_IPV6

#ifdef __sun
static gboolean 
have_ipv6_solaris (void)
{
          int            s, i;
          int            ret;
          struct lifnum  ln; 
          struct lifconf ifc;
          struct lifreq *ifr;   
          char          *ifreqs;
          
          /* First, try the <AB>classic<BB> way */
          s = socket (AF_INET6, SOCK_DGRAM, 0);
          if (s < 0) return FALSE;
          close (s);

          s = socket (AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

          /*
           * Ok, the system is able to create IPv6 sockets, so
           * lets check if IPv6 is configured in the machine
           */
          ln.lifn_family=AF_UNSPEC;
          ln.lifn_flags=ln.lifn_count=0; 

          ret = ioctl (s, SIOCGLIFNUM, &ln);
          if (ret == -1) {
                        perror ("ioctl SIOCGLIFNUM");
                        return FALSE;
          }

          /* Alloc the memory and get the configuration */
          ifc.lifc_flags  = 0; 
          ifc.lifc_family = AF_UNSPEC;
          ifc.lifc_len    = ln.lifn_count * sizeof (struct lifreq);

          ifreqs = (char *) malloc (ifc.lifc_len);
          ifc.lifc_buf = ifreqs;

          if (ioctl (s, SIOCGLIFCONF, &ifc) < 0) {
                        perror ("ioctl SIOCGLIFCONF");
                        return FALSE;
          }

          /* Check each interface */
          ifr  = ifc.lifc_req;
          ret  = FALSE;

          for (i = ifc.lifc_len/sizeof (struct lifreq); --i >= 0; ifr++) {
                struct sockaddr_in *sin;

                        /* Check the address */
                        if (ioctl (s, SIOCGLIFFLAGS, ifr) < 0) {
                                   /* perror ("ioctl SIOCGLIFADDR"); */
                                   continue;
                        }

                        sin = (struct sockaddr_in *)&ifr->lifr_addr;

                        if (sin->sin_family == AF_INET6) {
                                   ret = TRUE;
                                   break;
                        }

                        /* Check the interface flags */
                        if (ioctl (s, SIOCGLIFFLAGS, (char *) ifr) < 0) {
                                   /* perror ("ioctl SIOCGLIFFLAGS"); */
                                   continue;
                        }

                        if (ifr->lifr_flags & IFF_IPV6) {      
                                   ret = TRUE;
                                   break;
                        }
          }
          
          /* Clean up */
          free (ifreqs);
          close (s);

          return ret;
}
#endif

static gboolean
have_ipv6 (void)
{
	int s;
        static gboolean has_ipv6 = -1;

#ifdef __sun
        has_ipv6 = have_ipv6_solaris ();
#else
        if (has_ipv6 != -1) return has_ipv6;

        s = socket (AF_INET6, SOCK_STREAM, 0);  
        if (s < 0) {
                  has_ipv6 = FALSE;
                  return FALSE;
	}

       VE_IGNORE_EINTR (close (s));            
#endif
       return has_ipv6;
}
#endif

/*
 * Clear environment, but keep the i18n ones,
 * note that this leaks memory so only use before exec
 */
void
gdm_clearenv_no_lang (void)
{
	int i;
	GList *li, *envs = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		char *env = environ[i];
		if (strncmp (env, "LC_", 3) == 0 ||
		    strncmp (env, "LANG", 4) == 0 ||
		    strncmp (env, "LINGUAS", 7) == 0) {
			envs = g_list_prepend (envs, g_strdup (env));
		}
	}

	ve_clearenv ();

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

const char *
gdm_saved_getenv (const char *var)
{
	int len;
	GList *li;

	len = strlen (var);

	for (li = stored_env; li != NULL; li = li->next) {
		const char *e = li->data;
		if (strncmp (var, e, len) == 0 &&
		    e[len] == '=') {
			return &(e[len+1]);
		}
	}
	return NULL;
}

/* leaks */
void
gdm_restoreenv (void)
{
	GList *li;

	ve_clearenv ();

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
	struct sockaddr_in serv_addr = { 0 };

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	/*
         * Cap this at 3000, I'm not sure we can ever seriously
	 * go that far
         */
	for (i = start; i < 3000; i++) {
		GSList *li;
		struct stat s;
		char buf[256];
		FILE *fp;
		int r;
                GSList *displays;

                displays = gdm_daemon_config_get_display_list ();

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

#ifdef ENABLE_IPV6
		if (have_ipv6 ()) {
			struct sockaddr_in6 serv6_addr = { 0 };

			sock = socket (AF_INET6, SOCK_STREAM,0);

			serv6_addr.sin6_family = AF_INET6;
			serv6_addr.sin6_addr = in6addr_loopback;
			serv6_addr.sin6_port = htons (6000 + i);
			errno = 0;
			VE_IGNORE_EINTR (connect (sock,
                                      (struct sockaddr *)&serv6_addr,
                                      sizeof (serv6_addr)));
		}
		else
#endif
		{
			sock = socket (AF_INET, SOCK_STREAM, 0);

			serv_addr.sin_port = htons (6000 + i);

			errno = 0;
			VE_IGNORE_EINTR (connect (sock,
				       (struct sockaddr *)&serv_addr,
				       sizeof (serv_addr)));
		}
		if (errno != 0 && errno != ECONNREFUSED) {
			VE_IGNORE_EINTR (close (sock));
			continue;
		}
		VE_IGNORE_EINTR (close (sock));

		/* if lock file exists and the process exists */
		g_snprintf (buf, sizeof (buf), "/tmp/.X%d-lock", i);
		VE_IGNORE_EINTR (r = g_stat (buf, &s));
		if (r == 0 &&
		    ! S_ISREG (s.st_mode)) {
			/*
                         * Eeeek! not a regular file?  Perhaps someone
			 * is trying to play tricks on us
                         */
			continue;
		}
		VE_IGNORE_EINTR (fp = fopen (buf, "r"));
		if (fp != NULL) {
			char buf2[100];
			char *getsret;
			VE_IGNORE_EINTR (getsret = fgets (buf2, sizeof (buf2), fp));
			if (getsret != NULL) {
				gulong pid;
				if (sscanf (buf2, "%lu", &pid) == 1 &&
				    kill (pid, 0) == 0) {
					VE_IGNORE_EINTR (fclose (fp));
					continue;
				}

			}
			VE_IGNORE_EINTR (fclose (fp));

			/* whack the file, it's a stale lock file */
			VE_IGNORE_EINTR (g_unlink (buf));
		}

		/* If starting as root, we'll be able to overwrite any
		 * stale sockets or lock files, but a user may not be
		 * able to */
		if (server_uid > 0) {
			g_snprintf (buf, sizeof (buf),
				    "/tmp/.X11-unix/X%d", i);
			VE_IGNORE_EINTR (r = g_stat (buf, &s));
			if (r == 0 &&
			    s.st_uid != server_uid) {
				continue;
			}

			g_snprintf (buf, sizeof (buf),
				    "/tmp/.X%d-lock", i);
			VE_IGNORE_EINTR (r = g_stat (buf, &s));
			if (r == 0 &&
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
	char *dialog; /* do we have dialog? */
	char *msg_quoted;

    if ( ! gdm_daemon_config_get_value_bool (GDM_KEY_CONSOLE_NOTIFY))
		return FALSE;

	if (g_access (LIBEXECDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	if (msg[0] == '-') {
		char *tmp = g_strconcat (" ", msg, NULL);
		msg_quoted = g_shell_quote (tmp);
		g_free (tmp);
	} else {
		msg_quoted = g_shell_quote (msg);
	}
	
	dialog = g_find_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("whiptail");
	if (dialog != NULL) {
		char *argv[6];

		if ( ! gdm_ok_console_language ()) {
			g_unsetenv ("LANG");
			g_unsetenv ("LC_ALL");
			g_unsetenv ("LC_MESSAGES");
			g_setenv ("LANG", "C", TRUE);
			g_setenv ("UNSAFE_TO_TRANSLATE", "yes", TRUE);
		}
		
		argv[0] = LIBEXECDIR "/gdmopen";
		argv[1] = "-l";
		argv[2] = "/bin/sh";
		argv[3] = "-c";
		argv[4] = g_strdup_printf ("%s --msgbox %s 16 70",
					   dialog, msg_quoted);
		argv[5] = NULL;

		/* Make sure gdialog wouldn't get confused */
		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (dialog);
			g_free (msg_quoted);
			g_free (argv[4]);
			return FALSE;
		}

		g_free (dialog);
		g_free (argv[4]);
	} else {
		char *argv[6];

		argv[0] = LIBEXECDIR "/gdmopen";
		argv[1] = "-l";
		argv[2] = "/bin/sh";
		argv[3] = "-c";
		argv[4] = g_strdup_printf
			("clear ; "
			 "echo %s ; read ; clear",
			 msg_quoted);
		argv[5] = NULL;

		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (argv[4]);
			g_free (msg_quoted);
			return FALSE;
		}
		g_free (argv[4]);
	}
	g_free (msg_quoted);
	return TRUE;
}

gboolean
gdm_text_yesno_dialog (const char *msg, gboolean *ret)
{
	char *dialog; /* do we have dialog? */
	char *msg_quoted;

    if ( ! gdm_daemon_config_get_value_bool (GDM_KEY_CONSOLE_NOTIFY))
		return FALSE;
	
	if (g_access (LIBEXECDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	if (ret != NULL)
		*ret = FALSE;

	if (msg[0] == '-') {
		char *tmp = g_strconcat (" ", msg, NULL);
		msg_quoted = g_shell_quote (tmp);
		g_free (tmp);
	} else {
		msg_quoted = g_shell_quote (msg);
	}
	
	dialog = g_find_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = g_find_program_in_path ("whiptail");
	if (dialog != NULL) {
		char *argv[6];
		int retint;

		if ( ! gdm_ok_console_language ()) {
			g_unsetenv ("LANG");
			g_unsetenv ("LC_ALL");
			g_unsetenv ("LC_MESSAGES");
			g_setenv ("LANG", "C", TRUE);
			g_setenv ("UNSAFE_TO_TRANSLATE", "yes", TRUE);
		}

		argv[0] = LIBEXECDIR "/gdmopen";
		argv[1] = "-l";
		argv[2] = "/bin/sh";
		argv[3] = "-c";
		argv[4] = g_strdup_printf ("%s --yesno %s 16 70",
					   dialog, msg_quoted);
		argv[5] = NULL;

		/*
                 * Will unset DISPLAY and XAUTHORITY if they exist
		 * so that gdialog (if used) doesn't get confused
                 */
		retint = gdm_exec_wait (argv, TRUE /* no display */,
					TRUE /* de_setuid */);
		if (retint < 0) {
			g_free (argv[4]);
			g_free (dialog);
			g_free (msg_quoted);
			return FALSE;
		}

		if (ret != NULL)
			*ret = (retint == 0) ? TRUE : FALSE;

		g_free (dialog);
		g_free (msg_quoted);
		g_free (argv[4]);

		return TRUE;
	} else {
		char tempname[] = "/tmp/gdm-yesno-XXXXXX";
		int tempfd;
		FILE *fp;
		char buf[256];
		char *argv[6];

		tempfd = g_mkstemp (tempname);
		if (tempfd < 0) {
			g_free (msg_quoted);
			return FALSE;
		}

		VE_IGNORE_EINTR (close (tempfd));

		argv[0] = LIBEXECDIR "/gdmopen";
		argv[1] = "-l";
		argv[2] = "/bin/sh";
		argv[3] = "-c";
		argv[4] = g_strdup_printf
			("clear ; "
			 "echo %s ; echo ; echo \"%s\" ; "
			 "read RETURN ; echo $RETURN > %s ; clear'",
			 msg_quoted,
			 /* Translators, don't translate the 'y' and 'n' */
			 _("y = Yes or n = No? >"),
			 tempname);
		argv[5] = NULL;

		if (gdm_exec_wait (argv, TRUE /* no display */,
				   TRUE /* de_setuid */) < 0) {
			g_free (argv[4]);
			g_free (msg_quoted);
			return FALSE;
		}
		g_free (argv[4]);

		if (ret != NULL) {
			VE_IGNORE_EINTR (fp = fopen (tempname, "r"));
			if (fp != NULL) {
				if (fgets (buf, sizeof (buf), fp) != NULL &&
				    (buf[0] == 'y' || buf[0] == 'Y'))
					*ret = TRUE;
				VE_IGNORE_EINTR (fclose (fp));
			} else {
				g_free (msg_quoted);
				return FALSE;
			}
		}

		VE_IGNORE_EINTR (g_unlink (tempname));

		g_free (msg_quoted);
		return TRUE;
	}
}

int
gdm_exec_wait (char * const *argv,
	       gboolean no_display,
	       gboolean de_setuid)
{
	int status;
	pid_t pid;

	if (argv == NULL ||
	    argv[0] == NULL ||
	    g_access (argv[0], X_OK) != 0)
		return -1;

	g_debug ("Forking extra process: %s", argv[0]);

	pid = gdm_fork_extra ();
	if (pid == 0) {
		gdm_log_shutdown ();

		gdm_close_all_descriptors (0 /* from */, -1 /* except */, -1 /* except2 */);

		/*
                 * No error checking here - if it's messed the best response
		 * is to ignore & try to continue
                 */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR);   /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR);   /* open stderr - fd 2 */

		if (de_setuid) {
			gdm_desetuid ();
		}

		gdm_log_init ();

		if (no_display) {
			g_unsetenv ("DISPLAY");
			g_unsetenv ("XAUTHORITY");
		}

		VE_IGNORE_EINTR (execv (argv[0], argv));

		_exit (-1);
	}

	if (pid < 0)
		return -1;

	gdm_wait_for_extra (pid, &status);

	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return -1;
}

pid_t
gdm_fork_extra (void)
{
	pid_t pid;

	gdm_sigchld_block_push ();
	gdm_sigterm_block_push ();

	pid = fork ();
	if (pid < 0)
		return 0;
	else if (pid == 0)
		/*
                 * Unset signals here, and yet again
		 * later as the block_pop will whack
		 * our signal mask
                 */
		gdm_unset_signals ();

	gdm_sigterm_block_pop ();
	gdm_sigchld_block_pop ();

	if (pid == 0) {
		/*
                 * In the child setup empty mask and set all signals to
		 * default values
                 */
		gdm_unset_signals ();

		/*
                 * Also make a new process group so that we may use
		 * kill -(extra_process) to kill extra process and all it's
		 * possible children
                 */
		setsid ();

		/* Harmless in children, but in case we'd run
		   extra processes from main daemon would fix
		   problems ... */
		if (gdm_daemon_config_get_value_bool (GDM_KEY_XDMCP))
			gdm_xdmcp_close ();
	}

	return pid;
}

void
gdm_wait_for_extra (pid_t pid,
		    int  *statusp)
{
	int status;

	gdm_sigchld_block_push ();

	if (pid > 1) {
		ve_waitpid_no_signal (pid, &status, 0);
	}

	if (statusp != NULL)
		*statusp = status;

	gdm_sigchld_block_pop ();
}

static void
ensure_tmp_socket_dir (const char *dir)
{
	mode_t old_umask;

	/*
         * The /tmp/.ICE-unix / .X11-unix check, note that we do
	 * ignore errors, since it's not deadly to run
	 * if we can't perform this task :)
         */
	old_umask = umask (0);

        if G_UNLIKELY (g_mkdir (dir, 01777) != 0) {
		/*
                 * If we can't create it, perhaps it
		 * already exists, in which case ensure the
		 * correct permissions
                 */
		struct stat s;
		int r;
		VE_IGNORE_EINTR (r = g_lstat (dir, &s));
		if G_LIKELY (r == 0 && S_ISDIR (s.st_mode)) {
			/* Make sure it is root and sticky */
			VE_IGNORE_EINTR (chown (dir, 0, 0));
			VE_IGNORE_EINTR (g_chmod (dir, 01777));
		} else {
			/*
                         * There is a file/link/whatever of the same name?
			 * whack and try mkdir
                         */
			VE_IGNORE_EINTR (g_unlink (dir));
			g_mkdir (dir, 01777);
		}
	}

	umask (old_umask);
}

/*
 * Done on startup and when running display_manage
 * This can do some sanity ensuring, one of the things it does now is make
 * sure /tmp/.ICE-unix and /tmp/.X11-unix exist and have the correct
 * permissions
 */
void
gdm_ensure_sanity (void)
{
	uid_t old_euid;
	gid_t old_egid;

	old_euid = geteuid ();
	old_egid = getegid ();

	NEVER_FAILS_root_set_euid_egid (0, 0);

	ensure_tmp_socket_dir ("/tmp/.ICE-unix");
	ensure_tmp_socket_dir ("/tmp/.X11-unix");

	NEVER_FAILS_root_set_euid_egid (old_euid, old_egid);
}


gboolean
gdm_setup_gids (const char *login, gid_t gid)
{
	/*
         * FIXME: perhaps for *BSD there should be setusercontext
	 * stuff here
         */
	if G_UNLIKELY (setgid (gid) < 0)  {
		gdm_error (_("Could not setgid %d. Aborting."), (int)gid);
		return FALSE;
	}

	if G_UNLIKELY (initgroups (login, gid) < 0) {
		gdm_error (_("initgroups () failed for %s. Aborting."), login);
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
		int setresuid (uid_t ruid, uid_t euid, uid_t suid);
		int setresgid (gid_t rgid, gid_t egid, gid_t sgid);
		setresgid (gid, gid, gid);
		setresuid (uid, uid, uid);
	}
#else
	setegid (getgid ());
	seteuid (getuid ());
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
		/* Must be a full word */
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
		/* Must be a full word */
		end = *(p + strlen (option));
		if ((end >= 'a' && end <= 'z') ||
		    (end >= 'A' && end <= 'Z') ||
		    (end >= '0' && end <= '9') ||
		    end == '_')
			continue;

		got_it = TRUE;
	}
	VE_IGNORE_EINTR (fclose (fp));
	return got_it;
}

void
gdm_unset_signals (void)
{
	sigset_t mask;

	sigemptyset (&mask);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	gdm_signal_default (SIGUSR1);
	gdm_signal_default (SIGUSR2);
	gdm_signal_default (SIGCHLD);
	gdm_signal_default (SIGTERM);
	gdm_signal_default (SIGINT);
	gdm_signal_default (SIGPIPE);
	gdm_signal_default (SIGALRM);
	gdm_signal_default (SIGHUP);
	gdm_signal_default (SIGABRT);
#ifdef SIGXFSZ
	gdm_signal_default (SIGXFSZ);
#endif
#ifdef SIGXCPU
	gdm_signal_default (SIGXCPU);
#endif
}

#ifdef RLIM_NLIMITS
#define NUM_OF_LIMITS RLIM_NLIMITS
#else /* ! RLIM_NLIMITS */
#ifdef RLIMIT_NLIMITS
#define NUM_OF_LIMITS RLIMIT_NLIMITS
#endif /* RLIMIT_NLIMITS */
#endif /* RLIM_NLIMITS */

/* If we can count limits then the reset code is simple */ 
#ifdef NUM_OF_LIMITS

static struct rlimit limits[NUM_OF_LIMITS];

void
gdm_get_initial_limits (void)
{
	int i;

	for (i = 0; i < NUM_OF_LIMITS; i++) {
		/* Some sane defaults */
		limits[i].rlim_cur = RLIM_INFINITY;
		limits[i].rlim_max = RLIM_INFINITY;
		/* Get the limits */
		getrlimit (i, &(limits[i]));
	}
}

void
gdm_reset_limits (void)
{
	int i;

	for (i = 0; i < NUM_OF_LIMITS; i++) {
		/* Get the limits */
		setrlimit (i, &(limits[i]));
	}
}

#define CHECK_LC(value, category) \
    (g_str_has_prefix (line->str, value "=")) \
      { \
	character = g_utf8_get_char (line->str + strlen (value "=")); \
\
	if ((character == '\'') || (character == '\"')) \
	  { \
	    q = g_utf8_find_prev_char (line->str, line->str + line->len); \
\
	    if ((q == NULL) || (g_utf8_get_char (q) != character)) \
	      { \
		g_string_set_size (line, 0); \
		continue; \
	      } \
\
	    g_string_set_size (line, line->len - 1); \
	    g_setenv (value, line->str + strlen (value "=") + 1, TRUE); \
	    if (category) \
	      setlocale ((category), line->str + strlen (value "=") + 1); \
	  } \
	else \
	  { \
	    g_setenv (value, line->str + strlen (value "="), TRUE); \
	    if (category) \
	      setlocale ((category), line->str + strlen (value "=")); \
	  } \
\
        g_string_set_size (line, 0); \
	continue; \
      }

void
gdm_reset_locale (void)
{
    char *i18n_file_contents;
    gsize i18n_file_length, i;
    GString *line;
    const gchar *p, *q;
    const gchar *gdmlang = g_getenv ("GDM_LANG");

    if (gdmlang)
      {
	g_setenv ("LANG", gdmlang, TRUE);
	g_unsetenv ("LC_ALL");
	g_unsetenv ("LC_MESSAGES");
	setlocale (LC_ALL, "");
	setlocale (LC_MESSAGES, "");
	return;
      }

    i18n_file_contents = NULL;
    line = NULL; 
    p = NULL;
    if (!g_file_get_contents (LANG_CONFIG_FILE, &i18n_file_contents,
			      &i18n_file_length, NULL))
      goto out;

    if (!g_utf8_validate (i18n_file_contents, i18n_file_length, NULL))
      goto out;

    line = g_string_new ("");
    p = i18n_file_contents;
    for (i = 0; i < i18n_file_length; 
	 p = g_utf8_next_char (p), i = p - i18n_file_contents) 
      {
	gunichar character;
	character = g_utf8_get_char (p);

	if ((character != '\n') && (character != '\0')) 
	  {
	    g_string_append_unichar (line, character);
	    continue;
	  }

	if CHECK_LC("LC_ALL", LC_ALL)
	else if CHECK_LC("LC_COLLATE", LC_COLLATE)
	else if CHECK_LC("LC_MESSAGES", LC_MESSAGES)
	else if CHECK_LC("LC_MONETARY", LC_MONETARY)
	else if CHECK_LC("LC_NUMERIC", LC_NUMERIC)
	else if CHECK_LC("LC_TIME", LC_TIME)
	else if CHECK_LC("LANG", 0)

        g_string_set_size (line, 0);
      }

    g_string_free (line, TRUE);

    setlocale (LC_ALL, "");

  out:
    g_free (i18n_file_contents);
}

#undef CHECK_LC

#else /* ! NUM_OF_LIMITS */
/* We have to go one by one here */

#ifdef RLIMIT_CPU
static struct rlimit limit_cpu = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_DATA
static struct rlimit limit_data = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_FSIZE
static struct rlimit limit_fsize = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_LOCKS
static struct rlimit limit_locks = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_MEMLOCK
static struct rlimit limit_memlock = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_NOFILE
static struct rlimit limit_nofile = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_OFILE
static struct rlimit limit_ofile = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_NPROC
static struct rlimit limit_nproc = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_RSS
static struct rlimit limit_rss = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_STACK
static struct rlimit limit_stack = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_CORE
static struct rlimit limit_core = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_AS
static struct rlimit limit_as = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_VMEM
static struct rlimit limit_vmem = { RLIM_INFINITY, RLIM_INFINITY };
#endif
#ifdef RLIMIT_PTHREAD
static struct rlimit limit_pthread = { RLIM_INFINITY, RLIM_INFINITY };
#endif

void
gdm_get_initial_limits (void)
{
	/* Note: I don't really know which ones are really very standard
	   and which ones are not, so I just test for them all one by one */

#ifdef RLIMIT_CPU
	getrlimit (RLIMIT_CPU, &limit_cpu);
#endif
#ifdef RLIMIT_DATA
	getrlimit (RLIMIT_DATA, &limit_data);
#endif
#ifdef RLIMIT_FSIZE
	getrlimit (RLIMIT_FSIZE, &limit_fsize);
#endif
#ifdef RLIMIT_LOCKS
	getrlimit (RLIMIT_LOCKS, &limit_locks);
#endif
#ifdef RLIMIT_MEMLOCK
	getrlimit (RLIMIT_MEMLOCK, &limit_memlock);
#endif
#ifdef RLIMIT_NOFILE
	getrlimit (RLIMIT_NOFILE, &limit_nofile);
#endif
#ifdef RLIMIT_OFILE
	getrlimit (RLIMIT_OFILE, &limit_ofile);
#endif
#ifdef RLIMIT_NPROC
	getrlimit (RLIMIT_NPROC, &limit_nproc);
#endif
#ifdef RLIMIT_RSS
	getrlimit (RLIMIT_RSS, &limit_rss);
#endif
#ifdef RLIMIT_STACK
	getrlimit (RLIMIT_STACK, &limit_stack);
#endif
#ifdef RLIMIT_CORE
	getrlimit (RLIMIT_CORE, &limit_core);
#endif
#ifdef RLIMIT_AS
	getrlimit (RLIMIT_AS, &limit_as);
#endif
#ifdef RLIMIT_VMEM
	getrlimit (RLIMIT_VMEM, &limit_vmem);
#endif
#ifdef RLIMIT_PTHREAD
	getrlimit (RLIMIT_PTHREAD, &limit_pthread);
#endif
}

void
gdm_reset_limits (void)
{
	/* Note: I don't really know which ones are really very standard
	   and which ones are not, so I just test for them all one by one */

#ifdef RLIMIT_CPU
	setrlimit (RLIMIT_CPU, &limit_cpu);
#endif
#ifdef RLIMIT_DATA
	setrlimit (RLIMIT_DATA, &limit_data);
#endif
#ifdef RLIMIT_FSIZE
	setrlimit (RLIMIT_FSIZE, &limit_fsize);
#endif
#ifdef RLIMIT_LOCKS
	setrlimit (RLIMIT_LOCKS, &limit_locks);
#endif
#ifdef RLIMIT_MEMLOCK
	setrlimit (RLIMIT_MEMLOCK, &limit_memlock);
#endif
#ifdef RLIMIT_NOFILE
	setrlimit (RLIMIT_NOFILE, &limit_nofile);
#endif
#ifdef RLIMIT_OFILE
	setrlimit (RLIMIT_OFILE, &limit_ofile);
#endif
#ifdef RLIMIT_NPROC
	setrlimit (RLIMIT_NPROC, &limit_nproc);
#endif
#ifdef RLIMIT_RSS
	setrlimit (RLIMIT_RSS, &limit_rss);
#endif
#ifdef RLIMIT_STACK
	setrlimit (RLIMIT_STACK, &limit_stack);
#endif
#ifdef RLIMIT_CORE
	setrlimit (RLIMIT_CORE, &limit_core);
#endif
#ifdef RLIMIT_AS
	setrlimit (RLIMIT_AS, &limit_as);
#endif
#ifdef RLIMIT_VMEM
	setrlimit (RLIMIT_VMEM, &limit_vmem);
#endif
#ifdef RLIMIT_PTHREAD
	setrlimit (RLIMIT_PTHREAD, &limit_pthread);
#endif
}

#endif /* NUM_OF_LIMITS */

const char *
gdm_root_user (void)
{
	static char *root_user = NULL;
	struct passwd *pwent;

	if (root_user != NULL)
		return root_user;

	pwent = getpwuid (0);
	if (pwent == NULL) /* huh? */
		root_user = g_strdup ("root");
	else
		root_user = g_strdup (pwent->pw_name);
	return root_user;
}

void
gdm_sleep_no_signal (int secs)
{
	time_t endtime = time (NULL)+secs;

	while (secs > 0) {
		struct timeval tv;
		tv.tv_sec = secs;
		tv.tv_usec = 0;
		select (0, NULL, NULL, NULL, &tv);
		/* Don't want to use sleep since we're using alarm
		   for pinging */
		secs = endtime - time (NULL);
	}
}

char *
gdm_ensure_extension (const char *name, const char *extension)
{
	const char *p;

	if (ve_string_empty (name))
		return g_strdup (name);

	p = strrchr (name, '.');
	if (p != NULL &&
	    strcmp (p, extension) == 0) {
		return g_strdup (name);
	} else {
		return g_strconcat (name, extension, NULL);
	}
}

char *
gdm_strip_extension (const char *name, const char *extension)
{
	const char *p = strrchr (name, '.');
	if (p != NULL &&
	    strcmp (p, extension) == 0) {
		char *r = g_strdup (name);
		char *rp = strrchr (r, '.');
		*rp = '\0';
		return r;
	} else {
		return g_strdup (name);
	}
}

void
gdm_twiddle_pointer (GdmDisplay *disp)
{
	if (disp == NULL ||
	    disp->dsp == NULL)
		return;

	XWarpPointer (disp->dsp,
		      None /* src_w */,
		      None /* dest_w */,
		      0 /* src_x */,
		      0 /* src_y */,
		      0 /* src_width */,
		      0 /* src_height */,
		      1 /* dest_x */,
		      1 /* dest_y */);
	XSync (disp->dsp, False);
	XWarpPointer (disp->dsp,
		      None /* src_w */,
		      None /* dest_w */,
		      0 /* src_x */,
		      0 /* src_y */,
		      0 /* src_width */,
		      0 /* src_height */,
		      -1 /* dest_x */,
		      -1 /* dest_y */);
	XSync (disp->dsp, False);
}

static char *
compress_string (const char *s)
{
	GString *gs = g_string_new (NULL);
	const char *p;
	gboolean in_whitespace = TRUE;

	for (p = s; *p != '\0'; p++) {
		if (*p == ' ' || *p == '\t') {
			if ( ! in_whitespace)
				g_string_append_c (gs, *p);
			in_whitespace = TRUE;
		} else {
			g_string_append_c (gs, *p);
			in_whitespace = FALSE;
		}
	}

	return g_string_free (gs, FALSE);
}


char *
gdm_get_last_info (const char *username)
{
	char *info = NULL;
	const char *cmd = NULL;

	if G_LIKELY (g_access ("/usr/bin/last", X_OK) == 0)
		cmd = "/usr/bin/last";
	else if (g_access ("/bin/last", X_OK) == 0)
		cmd = "/bin/last";

	if G_LIKELY (cmd != NULL) {
		char *user_quoted = g_shell_quote (username);
		char *newcmd;
		FILE *fp;

		newcmd = g_strdup_printf ("%s %s", cmd, user_quoted);

		VE_IGNORE_EINTR (fp = popen (newcmd, "r"));

		g_free (user_quoted);
		g_free (newcmd);

		if G_LIKELY (fp != NULL) {
			char buf[256];
			char *r;
			VE_IGNORE_EINTR (r = fgets (buf, sizeof (buf), fp));
			if G_LIKELY (r != NULL) {
				char *s = compress_string (buf);
				if ( ! ve_string_empty (s))
					info = g_strdup_printf (_("Last login:\n%s"), s);
				g_free (s);
			}
			VE_IGNORE_EINTR (pclose (fp));
		}
	}

	return info;
}

gboolean
gdm_ok_console_language (void)
{
	int i;
	char **v;
	static gboolean cached = FALSE;
	static gboolean is_ok;
	const char *loc;
	const char *consolecannothandle = gdm_daemon_config_get_value_string (GDM_KEY_CONSOLE_CANNOT_HANDLE);

	if (cached)
		return is_ok;

	/* So far we should be paranoid, we're not set yet */
	if (consolecannothandle == NULL)
		return FALSE;

	cached = TRUE;

	loc = setlocale (LC_MESSAGES, NULL);
	if (loc == NULL) {
		is_ok = TRUE;
		return TRUE;
	}

	is_ok = TRUE;

	v = g_strsplit (consolecannothandle, ",", -1);
	for (i = 0; v != NULL && v[i] != NULL; i++) {
		if ( ! ve_string_empty (v[i]) &&
		    strncmp (v[i], loc, strlen (v[i])) == 0) {
			is_ok = FALSE;
			break;
		}
	}
	if (v != NULL)
		g_strfreev (v);

	return is_ok;
}

const char *
gdm_console_translate (const char *str)
{
	if (gdm_ok_console_language ())
		return _(str);
	else
		return str;
}

/*
 * gdm_read_default
 *
 * This function is used to support systems that have the /etc/default/login
 * interface to control programs that affect security.  This is a Solaris
 * thing, though some users on other systems may find it useful.
 */
gchar *
gdm_read_default (gchar *key)
{
#ifdef HAVE_DEFOPEN
    gchar *retval = NULL;

    if (defopen ("/etc/default/login") == 0) {
       int flags = defcntl (DC_GETFLAGS, 0);

       TURNOFF (flags, DC_CASE);
       (void) defcntl (DC_SETFLAGS, flags);  /* ignore case */
       retval = g_strdup (defread (key));
       (void) defopen ((char *)NULL);
    }
    return retval;
#else
    return NULL;
#endif
}

