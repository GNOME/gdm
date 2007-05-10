/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * (c) 2000 Eazel, Inc.
 * (c) 2001,2002 George Lebl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <setjmp.h>
#include <dirent.h>

#ifdef HAVE_CRT_EXTERNS_H
#include <crt_externs.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>

#include "gdm-common.h"

int
gdm_fdgetc (int fd)
{
	char buf[1];
	int bytes;

	VE_IGNORE_EINTR (bytes = read (fd, buf, 1));
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
			bytes++;
			g_string_append_c (gs, c);
		}
	}
}

void
gdm_fdprintf (int fd, const gchar *format, ...)
{
	va_list args;
	gchar *s;
	int written, len;

	va_start (args, format);
	s = g_strdup_vprintf (format, args);
	va_end (args);

	len = strlen (s);

	if (len == 0) {
		g_free (s);
		return;
	}

	written = 0;
	while (written < len) {
		int w;
		VE_IGNORE_EINTR (w = write (fd, &s[written], len - written));
		if (w < 0)
			/* evil! */
			break;
		written += w;
	}

	g_free (s);
}

void
gdm_close_all_descriptors (int from, int except, int except2)
{
	DIR *dir;
	struct dirent *ent;
	GSList *openfds = NULL;

	/*
         * Evil, but less evil then going to _SC_OPEN_MAX
	 * which can be very VERY large
         */
	dir = opendir ("/proc/self/fd/");   /* This is the Linux dir */
	if (dir == NULL)
		dir = opendir ("/dev/fd/"); /* This is the FreeBSD dir */
	if G_LIKELY (dir != NULL) {
		GSList *li;
		while ((ent = readdir (dir)) != NULL) {
			int fd;
			if (ent->d_name[0] == '.')
				continue;
			fd = atoi (ent->d_name);
			if (fd >= from && fd != except && fd != except2)
				openfds = g_slist_prepend (openfds, GINT_TO_POINTER (fd));
		}
		closedir (dir);
		for (li = openfds; li != NULL; li = li->next) {
			int fd = GPOINTER_TO_INT (li->data); 
			VE_IGNORE_EINTR (close (fd));
		}
		g_slist_free (openfds);
	} else {
		int i;
		int max = sysconf (_SC_OPEN_MAX);
		/*
                 * Don't go higher then this.  This is
		 * a safety measure to not hang on crazy
		 * systems
                 */
		if G_UNLIKELY (max > 4096) {
			/* FIXME: warn about this perhaps */
			/*
                         * Try an open, in case we're really
			 * leaking fds somewhere badly, this
			 * should be very high
                         */
			i = gdm_open_dev_null (O_RDONLY);
			max = MAX (i+1, 4096);
		}
		for (i = from; i < max; i++) {
			if G_LIKELY (i != except && i != except2)
				VE_IGNORE_EINTR (close (i));
		}
	}
}

void
gdm_signal_ignore (int signal)
{
	struct sigaction ign_signal;

	ign_signal.sa_handler = SIG_IGN;
	ign_signal.sa_flags = SA_RESTART;
	sigemptyset (&ign_signal.sa_mask);

	if G_UNLIKELY (sigaction (signal, &ign_signal, NULL) < 0)
		g_warning (_("%s: Error setting signal %d to %s"),
			   "gdm_signal_ignore", signal, "SIG_IGN");
}

void
gdm_signal_default (int signal)
{
	struct sigaction def_signal;

	def_signal.sa_handler = SIG_DFL;
	def_signal.sa_flags = SA_RESTART;
	sigemptyset (&def_signal.sa_mask);

	if G_UNLIKELY (sigaction (signal, &def_signal, NULL) < 0)
		g_warning (_("%s: Error setting signal %d to %s"),
			   "gdm_signal_ignore", signal, "SIG_DFL");
}

int
gdm_open_dev_null (mode_t mode)
{
	int ret;
	VE_IGNORE_EINTR (ret = open ("/dev/null", mode));
	if G_UNLIKELY (ret < 0) {
		/*
                 * Never output anything, we're likely in some
		 * strange state right now
                 */
		gdm_signal_ignore (SIGPIPE);
		VE_IGNORE_EINTR (close (2));
		g_error ("Cannot open /dev/null, system on crack!");
	}

	return ret;
}

char *
gdm_make_filename (const char *dir,
		   const char *name,
		   const char *extension)
{
	char *base = g_strconcat (name, extension, NULL);
	char *full = g_build_filename (dir, base, NULL);
	g_free (base);
	return full;
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
	sigchld_blocked++;

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
		/* Reset signal mask back */
		sigprocmask (SIG_SETMASK, &sigchldblock_oldmask, NULL);
	}
}

void
gdm_sigterm_block_push (void)
{
	sigterm_blocked++;

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
		/* Reset signal mask back */
		sigprocmask (SIG_SETMASK, &sigtermblock_oldmask, NULL);
	}
}

void
gdm_sigusr2_block_push (void)
{
	sigset_t oldmask;

	if (sigusr2_blocked == 0) {
		/* Set signal mask */
		sigemptyset (&sigusr2block_mask);
		sigaddset (&sigusr2block_mask, SIGUSR2);
		sigprocmask (SIG_BLOCK, &sigusr2block_mask, &oldmask);
	}

	sigusr2_blocked++;

	sigusr2block_oldmask = oldmask;
}

void
gdm_sigusr2_block_pop (void)
{
	sigset_t oldmask;

	oldmask = sigusr2block_oldmask;

	sigusr2_blocked--;

	if (sigusr2_blocked == 0) {
	        /* Reset signal mask back */
	        sigprocmask (SIG_SETMASK, &sigusr2block_oldmask, NULL);
	}
}

static GdmHostent *
fillout_addrinfo (struct addrinfo *res,
		  struct sockaddr *ia,
		  const char *name)
{
	GdmHostent *he;
	gint i;
	gint addr_count = 0;
	struct addrinfo *tempaddrinfo;

	he = g_new0 (GdmHostent, 1);

	he->addrs = NULL;
	he->addr_count = 0;

	if (res != NULL && res->ai_canonname != NULL) {
		he->hostname = g_strdup (res->ai_canonname);
		he->not_found = FALSE;
	} else {
		he->not_found = TRUE;
		if (name != NULL)
			he->hostname = g_strdup (name);
		else {
			static char buffer6[INET6_ADDRSTRLEN];
			static char buffer[INET_ADDRSTRLEN];
			const char *new = NULL;

			if (ia->sa_family == AF_INET6) {
				if (IN6_IS_ADDR_V4MAPPED (&((struct sockaddr_in6 *)ia)->sin6_addr)) {
					new = inet_ntop (AF_INET, &(((struct sockaddr_in6 *)ia)->sin6_addr.s6_addr[12]), buffer, sizeof (buffer));
				} else {
					new = inet_ntop (AF_INET6, &((struct sockaddr_in6 *)ia)->sin6_addr, buffer6, sizeof (buffer6));
				}
			} else if (ia->sa_family == AF_INET) {
				new = inet_ntop (AF_INET, &((struct sockaddr_in *)ia)->sin_addr, buffer, sizeof (buffer));
			}

			if (new != NULL) {
				he->hostname = g_strdup (new);
			} else {
				he->hostname = NULL;
			}
		}
	}

	tempaddrinfo = res;

	while (res != NULL) {
		addr_count++;
		res = res->ai_next;
	}

	he->addrs = g_new0 (struct sockaddr_storage, addr_count);
	he->addr_count = addr_count;
	res = tempaddrinfo;
	for (i = 0; ; i++) {
		if (res == NULL)
			break;

		if ((res->ai_family == AF_INET) || (res->ai_family == AF_INET6)) {
			(he->addrs)[i] = *(struct sockaddr_storage *)(res->ai_addr);
		}

		res = res->ai_next;
	}

	/* We don't want the ::ffff: that could arise here */
	if (he->hostname != NULL &&
	    strncmp (he->hostname, "::ffff:", 7) == 0) {
		strcpy (he->hostname, he->hostname + 7);
	}

	return he;
}

/* stolen from xdm sources */
#if defined(X_NOT_POSIX) || defined(__EMX__) || defined(__NetBSD__) && defined(__sparc__)
#define Setjmp(e)	setjmp(e)
#define Longjmp(e,v)	longjmp(e,v)
#define Jmp_buf		jmp_buf
#else
#define Setjmp(e)   sigsetjmp(e,1)
#define Longjmp(e,v)	siglongjmp(e,v)
#define Jmp_buf		sigjmp_buf
#endif

static gboolean do_jumpback = FALSE;
static Jmp_buf signal_jumpback;
static struct sigaction oldterm, oldint, oldhup;

static void
jumpback_sighandler (int signal)
{
	/*
         * This avoids a race see Note below.
	 * We want to jump back only on the first
	 * signal invocation, even if the signal
	 * handler didn't return.
         */
	gboolean old_do_jumpback = do_jumpback;
	do_jumpback = FALSE;

	if (signal == SIGINT)
		oldint.sa_handler (signal);
	else if (signal == SIGTERM)
		oldint.sa_handler (signal);
	else if (signal == SIGHUP)
		oldint.sa_handler (signal);
	/* No others should be set up */

	/* Note that we may not get here since
	   the SIGTERM handler in slave.c
	   might have in fact done the big Longjmp
	   to the slave's death */

	if (old_do_jumpback) {
		Longjmp (signal_jumpback, 1);
	}
}

/*
 * This sets up interruptes to be proxied and the
 * gethostbyname/addr to be whacked using longjmp,
 * in case INT/TERM/HUP was gotten in which case
 * we no longer care for the result of the
 * resolution.
 */
#define SETUP_INTERRUPTS_FOR_TERM_DECLS \
    struct sigaction term;

#define SETUP_INTERRUPTS_FOR_TERM_SETUP \
    do_jumpback = FALSE;						\
    									\
    term.sa_handler = jumpback_sighandler;				\
    term.sa_flags = SA_RESTART;						\
    sigemptyset (&term.sa_mask);					\
									\
    if G_UNLIKELY (sigaction (SIGTERM, &term, &oldterm) < 0) 		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "TERM", g_strerror (errno)); \
									\
    if G_UNLIKELY (sigaction (SIGINT, &term, &oldint) < 0)		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "INT", g_strerror (errno)); \
									\
    if G_UNLIKELY (sigaction (SIGHUP, &term, &oldhup) < 0) 		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "HUP", g_strerror (errno)); \

#define SETUP_INTERRUPTS_FOR_TERM_TEARDOWN \
    do_jumpback = FALSE;						\
									\
    if G_UNLIKELY (sigaction (SIGTERM, &oldterm, NULL) < 0) 		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "TERM", g_strerror (errno)); \
									\
    if G_UNLIKELY (sigaction (SIGINT, &oldint, NULL) < 0) 		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "INT", g_strerror (errno)); \
									\
    if G_UNLIKELY (sigaction (SIGHUP, &oldhup, NULL) < 0) 		\
	g_critical (_("%s: Error setting up %s signal handler: %s"),	\
		  "SETUP_INTERRUPTS_FOR_TERM", "HUP", g_strerror (errno));

GdmHostent *
gdm_gethostbyname (const char *name)
{
	struct addrinfo hints;
	/* static because of Setjmp */
	static struct addrinfo *result;

	SETUP_INTERRUPTS_FOR_TERM_DECLS

	/* The cached address */
	static GdmHostent *he = NULL;
	static time_t last_time = 0;
	static char *cached_hostname = NULL;

	if (cached_hostname != NULL &&
	    strcmp (cached_hostname, name) == 0) {
		/* Don't check more then every 60 seconds */
		if (last_time + 60 > time (NULL))
			return gdm_hostent_copy (he);
	}

	SETUP_INTERRUPTS_FOR_TERM_SETUP

	if (Setjmp (signal_jumpback) == 0) {
		do_jumpback = TRUE;

		/* Find client hostname */
		memset (&hints, 0, sizeof (hints));
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = AI_CANONNAME;

		if (result) {
			freeaddrinfo (result);
			result = NULL;
		}

		getaddrinfo (name, NULL, &hints, &result);
		do_jumpback = FALSE;
	} else {
               /* Here we got interrupted */
		result = NULL;
	}

	SETUP_INTERRUPTS_FOR_TERM_TEARDOWN

	g_free (cached_hostname);
	cached_hostname = g_strdup (name);

	gdm_hostent_free (he);

	he = fillout_addrinfo (result, NULL, name);

	last_time = time (NULL);
	return gdm_hostent_copy (he);
}

GdmHostent *
gdm_gethostbyaddr (struct sockaddr_storage *ia)
{
	struct addrinfo hints;
	/* static because of Setjmp */
	static struct addrinfo *result = NULL;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	static struct in6_addr cached_addr6;

	SETUP_INTERRUPTS_FOR_TERM_DECLS

	/* The cached address */
	static GdmHostent *he = NULL;
	static time_t last_time = 0;
	static struct in_addr cached_addr;

	if (last_time != 0) {
		if ((ia->ss_family == AF_INET6) && (memcmp (cached_addr6.s6_addr, ((struct sockaddr_in6 *) ia)->sin6_addr.s6_addr, sizeof (struct in6_addr)) == 0)) {
			/* Don't check more then every 60 seconds */
			if (last_time + 60 > time (NULL))
				return gdm_hostent_copy (he);
		} else if (ia->ss_family == AF_INET) {
			if (memcmp (&cached_addr, &(((struct sockaddr_in *)ia)->sin_addr), sizeof (struct in_addr)) == 0) {
				/* Don't check more then every 60 seconds */
				if (last_time + 60 > time (NULL))
					return gdm_hostent_copy (he);
			}
		}
	}

	SETUP_INTERRUPTS_FOR_TERM_SETUP

	if (Setjmp (signal_jumpback) == 0) {
		do_jumpback = TRUE;

		/* Find client hostname */
		memset (&hints, 0, sizeof (hints));
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = AI_CANONNAME;

		if (result) {
			freeaddrinfo (result);
			result = NULL;
		}

		if (ia->ss_family == AF_INET6) {
			char buffer6[INET6_ADDRSTRLEN];

			inet_ntop (AF_INET6, &((struct sockaddr_in6 *)ia)->sin6_addr, buffer6, sizeof (buffer6));

			/*
                         * In the case of IPv6 mapped address strip the
                         * ::ffff: and lookup as an IPv4 address
                         */
			if (strncmp (buffer6, "::ffff:", 7) == 0) {
				char *temp= (buffer6 + 7);
				strcpy (buffer6, temp);
			}
			getaddrinfo (buffer6, NULL, &hints, &result);

		} else if (ia->ss_family == AF_INET) {
			char buffer[INET_ADDRSTRLEN];

			inet_ntop (AF_INET, &((struct sockaddr_in *)ia)->sin_addr, buffer, sizeof (buffer));

			getaddrinfo (buffer, NULL, &hints, &result);
		}

		do_jumpback = FALSE;
	} else {
		/* Here we got interrupted */
		result = NULL;
	}

	SETUP_INTERRUPTS_FOR_TERM_TEARDOWN

	if (ia->ss_family == AF_INET6) {
		memcpy (cached_addr6.s6_addr, ((struct sockaddr_in6 *)ia)->sin6_addr.s6_addr, sizeof (struct in6_addr));
		memset (&sin6, 0, sizeof (sin6));
		memcpy (sin6.sin6_addr.s6_addr, cached_addr6.s6_addr, sizeof (struct in6_addr));
		sin6.sin6_family = AF_INET6;
		he = fillout_addrinfo (result, (struct sockaddr *)&sin6, NULL);
	}
	else if (ia->ss_family == AF_INET) {
		memcpy (&(cached_addr.s_addr), &(((struct sockaddr_in *)ia)->sin_addr.s_addr), sizeof (struct in_addr));
		memset (&sin, 0, sizeof (sin));
		memcpy (&sin.sin_addr, &cached_addr, sizeof (struct in_addr));
		sin.sin_family = AF_INET;
		he = fillout_addrinfo (result, (struct sockaddr *)&sin, NULL);
	}

	last_time = time (NULL);
	return gdm_hostent_copy (he);
}

GdmHostent *
gdm_hostent_copy (GdmHostent *he)
{
	GdmHostent *cpy;

	if (he == NULL)
		return NULL;

	cpy = g_new0 (GdmHostent, 1);
	cpy->not_found = he->not_found;
	cpy->hostname = g_strdup (he->hostname);
	if (he->addr_count == 0) {
		cpy->addr_count = 0;
		cpy->addrs = NULL;
	} else {
		cpy->addr_count = he->addr_count;
		cpy->addrs = g_new0 (struct sockaddr_storage, he->addr_count);
		memcpy (cpy->addrs, he->addrs, sizeof (struct sockaddr_storage) * he->addr_count);
	}
	return cpy;
}

void
gdm_hostent_free (GdmHostent *he)
{
	if (he == NULL)
		return;
	g_free (he->hostname);
	he->hostname = NULL;

	g_free (he->addrs);
	he->addrs = NULL;
	he->addr_count = 0;

	g_free (he);
}



/* Like fopen with "w" */
FILE *
gdm_safe_fopen_w (const char *file, mode_t perm)
{
	int fd;
	FILE *ret;
	VE_IGNORE_EINTR (g_unlink (file));
	do {
		errno = 0;
		fd = open (file, O_EXCL|O_CREAT|O_TRUNC|O_WRONLY
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
#ifdef O_NOFOLLOW
			   |O_NOFOLLOW
#endif
			   , perm);
	} while G_UNLIKELY (errno == EINTR);
	if (fd < 0)
		return NULL;
	VE_IGNORE_EINTR (ret = fdopen (fd, "w"));
	return ret;
}

/* Like fopen with "a+" */
FILE *
gdm_safe_fopen_ap (const char *file, mode_t perm)
{
	int fd;
	FILE *ret;

	if (g_access (file, F_OK) == 0) {
		do {
			errno = 0;
			fd = open (file, O_APPEND|O_RDWR
#ifdef O_NOCTTY
				   |O_NOCTTY
#endif
#ifdef O_NOFOLLOW
				   |O_NOFOLLOW
#endif
				  );
		} while G_UNLIKELY (errno == EINTR);
	} else {
		/* Doesn't exist, open with O_EXCL */
		do {
			errno = 0;
			fd = open (file, O_EXCL|O_CREAT|O_RDWR
#ifdef O_NOCTTY
				   |O_NOCTTY
#endif
#ifdef O_NOFOLLOW
				   |O_NOFOLLOW
#endif
				   , perm);
		} while G_UNLIKELY (errno == EINTR);
	}
	if (fd < 0)
		return NULL;
	VE_IGNORE_EINTR (ret = fdopen (fd, "a+"));
	return ret;
}

void
gdm_fd_set_close_on_exec (int fd)
{
	int flags;

	flags = fcntl (fd, F_GETFD, 0);
	if (flags < 0) {
		return;
	}

	flags |= FD_CLOEXEC;

	fcntl (fd, F_SETFD, flags);
}

/**
 * ve_clearenv:
 *
 * Description: Clears out the environment completely.
 * In case there is no native implementation of clearenv,
 * this could cause leaks depending on the implementation
 * of environment.
 *
 **/
void
ve_clearenv (void)
{
#ifdef HAVE_CLEARENV
	clearenv ();
#else

#ifdef HAVE__NSGETENVIRON
#define environ (*_NSGetEnviron())
#else
        extern char **environ;
#endif

	if (environ != NULL)
		environ[0] = NULL;
#endif
}

char *
ve_first_word (const char *s)
{
	int argc;
	char **argv;
	char *ret;

	if (s == NULL)
		return NULL;

	if ( ! g_shell_parse_argv (s, &argc, &argv, NULL)) {
		char *p;
		ret = g_strdup (s);
		p = strchr (ret, ' ');
		if (p != NULL)
			*p = '\0';
		return ret;
	}

	ret = g_strdup (argv[0]);

	g_strfreev (argv);

	return ret;
}

static gboolean
ve_first_word_executable (const char *s,
			  gboolean only_existance)
{
	char *bin = ve_first_word (s);
	if (bin == NULL)
		return FALSE;
	if (g_access (bin, only_existance ? F_OK : X_OK) == 0) {
		g_free (bin);
		return TRUE;
	} else {
		g_free (bin);
		return FALSE;
	}
}

char *
ve_get_first_working_command (const char *list,
			      gboolean only_existance)
{
	int i;
	char **vector;
	char *ret = NULL;

	if (list == NULL)
		return NULL;

	vector = g_strsplit (list, ";", -1);
	for (i = 0; vector[i] != NULL; i++) {
		if (ve_first_word_executable (vector[i],
					      only_existance)) {
			ret = g_strdup (vector[i]);
			break;
		}
	}
	g_strfreev (vector);
	return ret;
}

char *
ve_locale_to_utf8 (const char *str)
{
	char *ret = g_locale_to_utf8 (str, -1, NULL, NULL, NULL);

	if (ret == NULL) {
		g_warning ("string not in proper locale encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_locale_from_utf8 (const char *str)
{
	char *ret = g_locale_from_utf8 (str, -1, NULL, NULL, NULL);

	if (ret == NULL) {
		g_warning ("string not in proper utf8 encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_filename_to_utf8 (const char *str)
{
	char *ret = g_filename_to_utf8 (str, -1, NULL, NULL, NULL);
	if (ret == NULL) {
		g_warning ("string not in proper locale encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_filename_from_utf8 (const char *str)
{
	char *ret = g_filename_from_utf8 (str, -1, NULL, NULL, NULL);
	if (ret == NULL) {
		g_warning ("string not in proper utf8 encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

pid_t
ve_waitpid_no_signal (pid_t pid, int *status, int options)
{
	pid_t ret;

	for (;;) {
		ret = waitpid (pid, status, options);
		if (ret == 0)
			return 0;
		if (errno != EINTR)
			return ret;
	}
}

gboolean
ve_locale_exists (const char *loc)
{
	gboolean ret;
	char *old = g_strdup (setlocale (LC_MESSAGES, NULL));
	if (setlocale (LC_MESSAGES, loc) != NULL)
		ret = TRUE;
	else
		ret = FALSE;
	setlocale (LC_MESSAGES, old);
	g_free (old);
	return ret;
}
