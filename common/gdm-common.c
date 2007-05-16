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
#include "gdm-md5.h"

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

/* hex conversion adapted from D-Bus */

/**
 * Appends a two-character hex digit to a string, where the hex digit
 * has the value of the given byte.
 *
 * @param str the string
 * @param byte the byte
 */
static void
_gdm_string_append_byte_as_hex (GString *str,
				int      byte)
{
	const char hexdigits[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f'
	};

	str = g_string_append_c (str, hexdigits[(byte >> 4)]);

	str = g_string_append_c (str, hexdigits[(byte & 0x0f)]);
}

/**
 * Encodes a string in hex, the way MD5 and SHA-1 are usually
 * encoded. (Each byte is two hex digits.)
 *
 * @param source the string to encode
 * @param start byte index to start encoding
 * @param dest string where encoded data should be placed
 * @param insert_at where to place encoded data
 * @returns #TRUE if encoding was successful, #FALSE if no memory etc.
 */
gboolean
gdm_string_hex_encode (const GString *source,
		       int            start,
		       GString       *dest,
		       int            insert_at)
{
	GString             *result;
	const unsigned char *p;
	const unsigned char *end;
	gboolean             retval;

	g_assert (start <= source->len);

	result = g_string_new (NULL);

	retval = FALSE;

	p = (const unsigned char*) source->str;
	end = p + source->len;
	p += start;

	while (p != end) {
		_gdm_string_append_byte_as_hex (result, *p);
		++p;
	}

	dest = g_string_insert (dest, insert_at, result->str);

	retval = TRUE;

	g_string_free (result, TRUE);

	return retval;
}

/**
 * Decodes a string from hex encoding.
 *
 * @param source the string to decode
 * @param start byte index to start decode
 * @param end_return return location of the end of the hex data, or #NULL
 * @param dest string where decoded data should be placed
 * @param insert_at where to place decoded data
 * @returns #TRUE if decoding was successful, #FALSE if no memory.
 */
gboolean
gdm_string_hex_decode (const GString *source,
		       int            start,
		       int           *end_return,
		       GString       *dest,
		       int            insert_at)
{
	GString             *result;
	const unsigned char *p;
	const unsigned char *end;
	gboolean             retval;
	gboolean             high_bits;

	g_assert (start <= source->len);

	result = g_string_new (NULL);

	retval = FALSE;

	high_bits = TRUE;
	p = (const unsigned char*) source->str;
	end = p + source->len;
	p += start;

	while (p != end) {
		unsigned int val;

		switch (*p) {
		case '0':
			val = 0;
			break;
		case '1':
			val = 1;
			break;
		case '2':
			val = 2;
			break;
		case '3':
			val = 3;
			break;
		case '4':
			val = 4;
			break;
		case '5':
			val = 5;
			break;
		case '6':
			val = 6;
			break;
		case '7':
			val = 7;
			break;
		case '8':
			val = 8;
			break;
		case '9':
			val = 9;
			break;
		case 'a':
		case 'A':
			val = 10;
			break;
		case 'b':
		case 'B':
			val = 11;
			break;
		case 'c':
		case 'C':
			val = 12;
			break;
		case 'd':
		case 'D':
			val = 13;
			break;
		case 'e':
		case 'E':
			val = 14;
			break;
		case 'f':
		case 'F':
			val = 15;
			break;
		default:
			goto done;
		}

		if (high_bits) {
			result = g_string_append_c (result, val << 4);
		} else {
			int           len;
			unsigned char b;

			len = result->len;

			b = result->str[len - 1];

			b |= val;

			result->str[len - 1] = b;
		}

		high_bits = !high_bits;

		++p;
	}

 done:
	dest = g_string_insert (dest, insert_at, result->str);

	if (end_return) {
		*end_return = p - (const unsigned char*) source->str;
	}

	retval = TRUE;

	g_string_free (result, TRUE);

	return retval;
}

static void
_gdm_generate_pseudorandom_bytes_buffer (char *buffer,
					 int   n_bytes)
{
	int i;

	/* fall back to pseudorandom */
	g_debug ("Falling back to pseudorandom for %d bytes\n",
                 n_bytes);

	i = 0;
	while (i < n_bytes) {
		int b;

		b = g_random_int_range (0, 255);

		buffer[i] = b;

		++i;
	}
}

static gboolean
_gdm_generate_pseudorandom_bytes (GString *str,
				  int      n_bytes)
{
	int old_len;
	char *p;

	old_len = str->len;

	str = g_string_set_size (str, old_len + n_bytes);

	p = str->str + old_len;

	_gdm_generate_pseudorandom_bytes_buffer (p, n_bytes);

	return TRUE;
}


static int
_gdm_fdread (int            fd,
	     GString       *buffer,
	     int            count)
{
	int   bytes_read;
	int   start;
	char *data;

	g_assert (count >= 0);

	start = buffer->len;

	buffer = g_string_set_size (buffer, start + count);

	data = buffer->str + start;

 again:
	bytes_read = read (fd, data, count);

	if (bytes_read < 0) {
		if (errno == EINTR) {
			goto again;
		} else {
			/* put length back (note that this doesn't actually realloc anything) */
			buffer = g_string_set_size (buffer, start);
			return -1;
		}
	} else {
		/* put length back (doesn't actually realloc) */
		buffer = g_string_set_size (buffer, start + bytes_read);

		return bytes_read;
	}
}

/**
 * Closes a file descriptor.
 *
 * @param fd the file descriptor
 * @param error error object
 * @returns #FALSE if error set
 */
static gboolean
_gdm_fdclose (int fd)
{
 again:
	if (close (fd) < 0) {
		if (errno == EINTR)
			goto again;

		g_warning ("Could not close fd %d: %s",
			   fd,
			   g_strerror (errno));
		return FALSE;
	}

	return TRUE;
}

/**
 * Generates the given number of random bytes,
 * using the best mechanism we can come up with.
 *
 * @param str the string
 * @param n_bytes the number of random bytes to append to string
 */
gboolean
gdm_generate_random_bytes (GString *str,
			   int      n_bytes)
{
	int old_len;
	int fd;

	/* FALSE return means "no memory", if it could
	 * mean something else then we'd need to return
	 * a DBusError. So we always fall back to pseudorandom
	 * if the I/O fails.
	 */

	old_len = str->len;
	fd = -1;

	/* note, urandom on linux will fall back to pseudorandom */
	fd = g_open ("/dev/urandom", O_RDONLY, 0);
	if (fd < 0) {
		return _gdm_generate_pseudorandom_bytes (str, n_bytes);
	}

	if (_gdm_fdread (fd, str, n_bytes) != n_bytes) {
		_gdm_fdclose (fd);
		str = g_string_set_size (str, old_len);
		return _gdm_generate_pseudorandom_bytes (str, n_bytes);
	}

	g_debug ("Read %d bytes from /dev/urandom\n", n_bytes);

	_gdm_fdclose (fd);

	return TRUE;
}

/**
 * Computes the ASCII hex-encoded md5sum of the given data and
 * appends it to the output string.
 *
 * @param data input data to be hashed
 * @param ascii_output string to append ASCII md5sum to
 * @returns #FALSE if not enough memory
 */
static gboolean
gdm_md5_compute (const GString *data,
		 GString       *ascii_output)
{
	GdmMD5Context context;
	GString      *digest;

	gdm_md5_init (&context);

	gdm_md5_update (&context, data);

	digest = g_string_new (NULL);
	if (digest == NULL)
		return FALSE;

	if (! gdm_md5_final (&context, digest))
		goto error;

	if (! gdm_string_hex_encode (digest,
				     0,
				     ascii_output,
				     ascii_output->len))
		goto error;

	g_string_free (digest, TRUE);

	return TRUE;

 error:
	g_string_free (digest, TRUE);

	return FALSE;
}

gboolean
gdm_generate_cookie (GString *result)
{
	gboolean ret;
	GString *data;

	data = g_string_new (NULL);
	gdm_generate_random_bytes (data, 16);

	ret = gdm_md5_compute (data, result);
	g_string_free (data, TRUE);

	return ret;
}
