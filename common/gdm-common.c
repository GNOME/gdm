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
