/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include <sys/wait.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "gdm-common.h"
#include "gdm-md5.h"

void
gdm_set_fatal_warnings_if_unstable (void)
{
        char **versions;

        versions = g_strsplit (VERSION, ".", 3);
        if (versions && versions [0] && versions [1]) {
                int major;
                major = atoi (versions [1]);
                if ((major % 2) != 0) {
                        g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
                        g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
                }
        }
        g_strfreev (versions);
}

int
gdm_signal_pid (int pid,
                int signal)
{
        int status = -1;

        /* perhaps block sigchld */
        g_debug ("sending signal %d to process %d", signal, pid);
        status = kill (pid, signal);

        if (status < 0) {
                if (errno == ESRCH) {
                        g_warning ("Child process %d was already dead.",
                                   (int)pid);
                } else {
                        g_warning ("Couldn't kill child process %d: %s",
                                   pid,
                                   g_strerror (errno));
                }
        }

        /* perhaps unblock sigchld */

        return status;
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
