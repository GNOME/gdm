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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "gdm-common.h"

#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#endif

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#define GDM_DBUS_NAME                            "org.gnome.DisplayManager"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.LocalDisplayFactory"

#ifdef WITH_CONSOLE_KIT
#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"
#endif

G_DEFINE_QUARK (gdm-common-error, gdm_common_error);

const char *
gdm_make_temp_dir (char *template)
{
        return mkdtemp (template);
}

gboolean
gdm_clear_close_on_exec_flag (int fd)
{
        int flags;

        if (fd < 0) {
                return FALSE;
        }

        flags = fcntl (fd, F_GETFD, 0);

        if (flags < 0) {
                return FALSE;
        }

        if ((flags & FD_CLOEXEC) != 0) {
                int status;

                status = fcntl (fd, F_SETFD, flags & ~FD_CLOEXEC);

                return status != -1;
        }

        return TRUE;
}

gboolean
gdm_get_pwent_for_name (const char     *name,
                        struct passwd **pwentp)
{
        struct passwd *pwent;

        do {
                errno = 0;
                pwent = getpwnam (name);
        } while (pwent == NULL && errno == EINTR);

        if (pwentp != NULL) {
                *pwentp = pwent;
        }

        return (pwent != NULL);
}

int
gdm_wait_on_and_disown_pid (int pid,
                            int timeout)
{
        int status;
        int ret;
        int num_tries;
        int flags;
        gboolean already_reaped;

        if (timeout > 0) {
                flags = WNOHANG;
                num_tries = 10 * timeout;
        } else {
                flags = 0;
                num_tries = 0;
        }
 wait_again:
        errno = 0;
        already_reaped = FALSE;
        ret = waitpid (pid, &status, flags);
        if (ret < 0) {
                if (errno == EINTR) {
                        goto wait_again;
                } else if (errno == ECHILD) {
                        already_reaped = TRUE;
                } else {
                        g_debug ("GdmCommon: waitpid () should not fail");
                }
        } else if (ret == 0) {
                num_tries--;

                if (num_tries > 0) {
                        g_usleep (G_USEC_PER_SEC / 10);
                } else {
                        char *path;
                        char *command;

                        path = g_strdup_printf ("/proc/%ld/cmdline", (long) pid);
                        if (g_file_get_contents (path, &command, NULL, NULL)) {;
                                g_warning ("GdmCommon: process (pid:%d, command '%s') isn't dying after %d seconds, now ignoring it.",
                                         (int) pid, command, timeout);
                                g_free (command);
                        } else {
                                g_warning ("GdmCommon: process (pid:%d) isn't dying after %d seconds, now ignoring it.",
                                         (int) pid, timeout);
                        }
                        g_free (path);

                        return 0;
                }
                goto wait_again;
        }

        g_debug ("GdmCommon: process (pid:%d) done (%s:%d)",
                 (int) pid,
                 already_reaped? "reaped earlier" :
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 already_reaped? 1 :
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        return status;
}

int
gdm_wait_on_pid (int pid)
{
    return gdm_wait_on_and_disown_pid (pid, 0);
}

int
gdm_signal_pid (int pid,
                int signal)
{
        int status = -1;

        /* perhaps block sigchld */
        g_debug ("GdmCommon: sending signal %d to process %d", signal, pid);
        errno = 0;
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

        g_return_val_if_fail (source != NULL, FALSE);
        g_return_val_if_fail (dest != NULL, FALSE);
        g_return_val_if_fail (source != dest, FALSE);
        g_return_val_if_fail (start >= 0, FALSE);
        g_return_val_if_fail (dest >= 0, FALSE);
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

        g_return_val_if_fail (source != NULL, FALSE);
        g_return_val_if_fail (dest != NULL, FALSE);
        g_return_val_if_fail (source != dest, FALSE);
        g_return_val_if_fail (start >= 0, FALSE);
        g_return_val_if_fail (dest >= 0, FALSE);

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

static gboolean
_fd_is_character_device (int fd)
{
        struct stat file_info;

        if (fstat (fd, &file_info) < 0) {
                return FALSE;
        }

        return S_ISCHR (file_info.st_mode);
}

static gboolean
_read_bytes (int      fd,
             char    *bytes,
             gsize    number_of_bytes,
             GError **error)
{
        size_t bytes_left_to_read;
        size_t total_bytes_read = 0;
        gboolean premature_eof;

        bytes_left_to_read = number_of_bytes;
        premature_eof = FALSE;
        do {
                size_t bytes_read = 0;

                errno = 0;
                bytes_read = read (fd, ((guchar *) bytes) + total_bytes_read,
                                   bytes_left_to_read);

                if (bytes_read > 0) {
                        total_bytes_read += bytes_read;
                        bytes_left_to_read -= bytes_read;
                } else if (bytes_read == 0) {
                        premature_eof = TRUE;
                        break;
                } else if ((errno != EINTR)) {
                        break;
                }
        } while (bytes_left_to_read > 0);

        if (premature_eof) {
                g_set_error (error,
                             G_FILE_ERROR,
                             G_FILE_ERROR_FAILED,
                             "No data available");

                return FALSE;
        } else if (bytes_left_to_read > 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                return FALSE;
        }

        return TRUE;
}

/**
 * Pulls a requested number of bytes from /dev/urandom
 *
 * @param size number of bytes to pull
 * @param error error if read fails
 * @returns The requested number of random bytes or #NULL if fail
 */

char *
gdm_generate_random_bytes (gsize    size,
                           GError **error)
{
        int fd;
        char *bytes;
        GError *read_error;

        /* We don't use the g_rand_* glib apis because they don't document
         * how much entropy they are seeded with, and it might be less
         * than the passed in size.
         */

        errno = 0;
        fd = open ("/dev/urandom", O_RDONLY);

        if (fd < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                close (fd);
                return NULL;
        }

        if (!_fd_is_character_device (fd)) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (ENODEV),
                             _("/dev/urandom is not a character device"));
                close (fd);
                return NULL;
        }

        bytes = g_malloc (size);
        read_error = NULL;
        if (!_read_bytes (fd, bytes, size, &read_error)) {
                g_propagate_error (error, read_error);
                g_free (bytes);
                close (fd);
                return NULL;
        }

        close (fd);
        return bytes;
}
static gboolean
create_transient_display (GDBusConnection *connection,
                          GError         **error)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char     *value;

        reply = g_dbus_connection_call_sync (connection,
                                             GDM_DBUS_NAME,
                                             GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH,
                                             GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE,
                                             "CreateTransientDisplay",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to create transient display: %s", local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

        g_variant_get (reply, "(&o)", &value);
        g_debug ("Started %s", value);

        g_variant_unref (reply);
        return TRUE;
}

#ifdef WITH_CONSOLE_KIT

static gboolean
get_current_session_id (GDBusConnection  *connection,
                        char            **session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             CK_MANAGER_PATH,
                                             CK_MANAGER_INTERFACE,
                                             "GetCurrentSession",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(o)", session_id);
        g_variant_unref (reply);

        return TRUE;
}

static gboolean
get_seat_id_for_session (GDBusConnection  *connection,
                         const char       *session_id,
                         char            **seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetSeatId",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine seat: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(o)", seat_id);
        g_variant_unref (reply);

        return TRUE;
}

static char *
get_current_seat_id (GDBusConnection *connection)
{
        gboolean res;
        char    *session_id;
        char    *seat_id;

        session_id = NULL;
        seat_id = NULL;

        res = get_current_session_id (connection, &session_id);
        if (res) {
                res = get_seat_id_for_session (connection, session_id, &seat_id);
        }
        g_free (session_id);

        return seat_id;
}

static gboolean
activate_session_id_for_ck (GDBusConnection *connection,
                            const char      *seat_id,
                            const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "ActivateSession",
                                             g_variant_new ("(o)", session_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to activate session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

static gboolean
session_is_login_window (GDBusConnection *connection,
                         const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char *value;
        gboolean ret;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetSessionType",
                                             NULL,
                                             G_VARIANT_TYPE ("(s)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine session type: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(&s)", &value);

        if (value == NULL || value[0] == '\0' || strcmp (value, "LoginWindow") != 0) {
                ret = FALSE;
        } else {
                ret = TRUE;
        }

        g_variant_unref (reply);

        return ret;
}

static gboolean
seat_can_activate_sessions (GDBusConnection *connection,
                            const char      *seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        gboolean ret;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "CanActivateSessions",
                                             NULL,
                                             G_VARIANT_TYPE ("(b)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine if can activate sessions: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(b)", &ret);
        g_variant_unref (reply);

        return ret;
}

static const char **
seat_get_sessions (GDBusConnection *connection,
                   const char      *seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char **value;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "GetSessions",
                                             NULL,
                                             G_VARIANT_TYPE ("(ao)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to list sessions: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(^ao)", &value);
        g_variant_unref (reply);

        return value;
}

static gboolean
get_login_window_session_id_for_ck (GDBusConnection  *connection,
                                    const char       *seat_id,
                                    char            **session_id)
{
        gboolean     can_activate_sessions;
        const char **sessions;
        int          i;

        *session_id = NULL;
        sessions = NULL;

        g_debug ("checking if seat can activate sessions");

        can_activate_sessions = seat_can_activate_sessions (connection, seat_id);
        if (! can_activate_sessions) {
                g_debug ("seat is unable to activate sessions");
                return FALSE;
        }

        sessions = seat_get_sessions (connection, seat_id);
        for (i = 0; sessions [i] != NULL; i++) {
                const char *ssid;

                ssid = sessions [i];

                if (session_is_login_window (connection, ssid)) {
                        *session_id = g_strdup (ssid);
                        break;
                }
        }
        g_free (sessions);

        return TRUE;
}

static gboolean
goto_login_session_for_ck (GDBusConnection  *connection,
                           GError          **error)
{
        gboolean        ret;
        gboolean        res;
        char           *session_id;
        char           *seat_id;

        ret = FALSE;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        seat_id = get_current_seat_id (connection);
        if (seat_id == NULL || seat_id[0] == '\0') {
                g_debug ("seat id is not set; can't switch sessions");
                g_set_error (error, GDM_COMMON_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = get_login_window_session_id_for_ck (connection, seat_id, &session_id);
        if (! res) {
                g_set_error (error, GDM_COMMON_ERROR, 1, _("User unable to switch sessions."));
                return FALSE;
        }

        if (session_id != NULL) {
                res = activate_session_id_for_ck (connection, seat_id, session_id);
                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "/org/freedesktop/ConsoleKit/Seat1") == 0) {
                res = create_transient_display (connection, error);
                if (res) {
                        ret = TRUE;
                }
        }

        return ret;
}
#endif

#ifdef WITH_SYSTEMD

static gboolean
activate_session_id_for_systemd (GDBusConnection *connection,
                                 const char      *seat_id,
                                 const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "ActivateSessionOnSeat",
                                             g_variant_new ("(ss)", session_id, seat_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to activate session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

static gboolean
get_login_window_session_id_for_systemd (const char  *seat_id,
                                         char       **session_id)
{
        gboolean   ret;
        int        res, i;
        char     **sessions;
        char      *service_class;
        char      *state;

        res = sd_seat_get_sessions (seat_id, &sessions, NULL, NULL);
        if (res < 0) {
                g_debug ("Failed to determine sessions: %s", strerror (-res));
                return FALSE;
        }

        if (sessions == NULL || sessions[0] == NULL) {
                *session_id = NULL;
                ret = TRUE;
                goto out;
        }

        for (i = 0; sessions[i]; i ++) {
                res = sd_session_get_class (sessions[i], &service_class);
                if (res < 0) {
                        g_debug ("failed to determine class of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_class, "greeter") != 0) {
                        free (service_class);
                        continue;
                }

                free (service_class);

                ret = sd_session_get_state (sessions[i], &state);
                if (ret < 0) {
                        g_debug ("failed to determine state of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (g_strcmp0 (state, "closing") == 0) {
                        free (state);
                        continue;
                }
                free (state);

                *session_id = g_strdup (sessions[i]);
                ret = TRUE;
                break;

        }

        *session_id = NULL;
        ret = TRUE;

out:
        for (i = 0; sessions[i]; i ++) {
                free (sessions[i]);
        }

        free (sessions);

        return ret;
}

static gboolean
goto_login_session_for_systemd (GDBusConnection  *connection,
                                GError          **error)
{
        gboolean        ret;
        int             res;
        char           *our_session;
        char           *session_id;
        char           *seat_id;

        ret = FALSE;
        session_id = NULL;
        seat_id = NULL;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        /* Note that we mostly use free () here, instead of g_free ()
         * since the data allocated is from libsystemd-logind, which
         * does not use GLib's g_malloc (). */

        res = sd_pid_get_session (0, &our_session);
        if (res < 0) {
                g_debug ("failed to determine own session: %s", strerror (-res));
                g_set_error (error, GDM_COMMON_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = sd_session_get_seat (our_session, &seat_id);
        free (our_session);
        if (res < 0) {
                g_debug ("failed to determine own seat: %s", strerror (-res));
                g_set_error (error, GDM_COMMON_ERROR, 0, _("Could not identify the current seat."));

                return FALSE;
        }

        res = sd_seat_can_multi_session (seat_id);
        if (res < 0) {
                free (seat_id);

                g_debug ("failed to determine whether seat can do multi session: %s", strerror (-res));
                g_set_error (error, GDM_COMMON_ERROR, 0, _("The system is unable to determine whether to switch to an existing login screen or start up a new login screen."));

                return FALSE;
        }

        if (res == 0) {
                free (seat_id);

                g_set_error (error, GDM_COMMON_ERROR, 0, _("The system is unable to start up a new login screen."));

                return FALSE;
        }

        res = get_login_window_session_id_for_systemd (seat_id, &session_id);
        if (res && session_id != NULL) {
                res = activate_session_id_for_systemd (connection, seat_id, session_id);

                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "seat0") == 0) {
                res = create_transient_display (connection, error);
                if (res) {
                        ret = TRUE;
                }
        }

        free (seat_id);
        g_free (session_id);

        return ret;
}
#endif

gboolean
gdm_goto_login_session (GError **error)
{
        GError *local_error;
        GDBusConnection *connection;

        local_error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &local_error);
        if (connection == NULL) {
                g_debug ("Failed to connect to the D-Bus daemon: %s", local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                return goto_login_session_for_systemd (connection, error);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return goto_login_session_for_ck (connection, error);
#endif
}
