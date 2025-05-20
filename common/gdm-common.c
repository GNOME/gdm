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
#include <grp.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "gdm-common.h"

#include <systemd/sd-login.h>

#define GDM_DBUS_NAME                            "org.gnome.DisplayManager"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.LocalDisplayFactory"

G_DEFINE_QUARK (gdm-common-error, gdm_common_error);

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

gboolean
gdm_get_pwent_for_uid (uid_t           uid,
                       struct passwd **pwentp)
{
        struct passwd *pwent;

        do {
                errno = 0;
                pwent = getpwuid (uid);
        } while (pwent == NULL && errno == EINTR);

        if (pwentp != NULL) {
                *pwentp = pwent;
        }

        return (pwent != NULL);
}

gboolean
gdm_get_grent_for_name (const char    *name,
                        struct group **grentp)
{
        struct group *grent;

        do {
                errno = 0;
                grent = getgrnam (name);
        } while (grent == NULL && errno == EINTR);

        if (grentp != NULL) {
                *grentp = grent;
        }

        return (grent != NULL);
}

static gboolean
gdm_get_grent_for_gid (gint           gid,
                       struct group **grentp)
{
        struct group *grent;

        do {
                errno = 0;
                grent = getgrgid (gid);
        } while (grent == NULL && errno == EINTR);

        if (grentp != NULL) {
                *grentp = grent;
        }

        return (grent != NULL);
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
                g_set_error_literal (error,
                                     G_FILE_ERROR,
                                     g_file_error_from_errno (errno),
                                     g_strerror (errno));
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
                g_set_error_literal (error,
                                     G_FILE_ERROR,
                                     g_file_error_from_errno (errno),
                                     g_strerror (errno));
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
                          GCancellable    *cancellable,
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
                                             cancellable, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to create transient display: %s", local_error->message);
                g_propagate_prefixed_error (error, local_error, _("Unable to create transient display: "));
                return FALSE;
        }

        g_variant_get (reply, "(&o)", &value);
        g_debug ("Started %s", value);

        g_variant_unref (reply);
        return TRUE;
}

gboolean
gdm_activate_session_by_id (GDBusConnection *connection,
                            GCancellable    *cancellable,
                            const char      *seat_id,
                            const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
        g_return_val_if_fail (seat_id != NULL, FALSE);
        g_return_val_if_fail (session_id != NULL, FALSE);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "ActivateSessionOnSeat",
                                             g_variant_new ("(ss)", session_id, seat_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             cancellable, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to activate session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

gboolean
gdm_terminate_session_by_id (GDBusConnection *connection,
                             GCancellable    *cancellable,
                             const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
        g_return_val_if_fail (session_id != NULL, FALSE);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "TerminateSession",
                                             g_variant_new ("(s)", session_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             cancellable, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to terminate session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

gboolean
gdm_get_login_window_session_id (const char  *seat_id,
                                 char       **session_id)
{
        gboolean   ret;
        int        res, i;
        char     **sessions;

        g_return_val_if_fail (session_id != NULL, FALSE);

        res = sd_seat_get_sessions (seat_id, &sessions, NULL, NULL);
        if (res < 0) {
                g_debug ("Failed to determine sessions: %s", strerror (-res));
                return FALSE;
        }

        if (sessions == NULL || sessions[0] == NULL) {
                *session_id = NULL;
                ret = FALSE;
                goto out;
        }

        for (i = 0; sessions[i]; i ++) {
                char *service_id = NULL;
                char *service_class = NULL;
                char *state = NULL;

                res = sd_session_get_class (sessions[i], &service_class);
                if (res < 0) {
                        if (res == -ENXIO)
                                continue;

                        g_debug ("failed to determine class of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_class, "greeter") != 0) {
                        free (service_class);
                        continue;
                }

                free (service_class);

                res = sd_session_get_state (sessions[i], &state);
                if (res < 0) {
                        if (res == -ENXIO)
                                continue;

                        g_debug ("failed to determine state of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (g_strcmp0 (state, "closing") == 0) {
                        free (state);
                        continue;
                }
                free (state);

                res = sd_session_get_service (sessions[i], &service_id);
                if (res < 0) {
                        if (res == -ENXIO)
                                continue;

                        g_debug ("failed to determine service of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_id, "gdm-launch-environment") == 0) {
                        *session_id = g_strdup (sessions[i]);
                        ret = TRUE;

                        free (service_id);
                        goto out;
                }

                free (service_id);
        }

        *session_id = NULL;
        ret = FALSE;

out:
        if (sessions) {
                for (i = 0; sessions[i]; i ++) {
                        free (sessions[i]);
                }

                free (sessions);
        }

        return ret;
}

static gboolean
goto_login_session (GDBusConnection  *connection,
                    GCancellable     *cancellable,
                    GError          **error)
{
        gboolean        ret;
        int             res;
        char           *our_session;
        char           *session_id;
        char           *seat_id;
        GError         *local_error = NULL;

        ret = FALSE;
        session_id = NULL;
        seat_id = NULL;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        /* Note that we mostly use free () here, instead of g_free ()
         * since the data allocated is from libsystemd-logind, which
         * does not use GLib's g_malloc (). */

        if (!gdm_find_display_session (0, getuid (), &our_session, &local_error)) {
                g_propagate_prefixed_error (error, local_error, _("Could not identify the current session: "));

                return FALSE;
        }

        res = sd_session_get_seat (our_session, &seat_id);
        free (our_session);
        if (res < 0) {
                g_debug ("failed to determine own seat: %s", strerror (-res));
                g_set_error (error, GDM_COMMON_ERROR, 0, _("Could not identify the current seat."));

                return FALSE;
        }

        res = gdm_get_login_window_session_id (seat_id, &session_id);
        if (res && session_id != NULL) {
                res = gdm_activate_session_by_id (connection, cancellable, seat_id, session_id);

                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "seat0") == 0) {
                res = create_transient_display (connection, cancellable, error);
                if (res) {
                        ret = TRUE;
                }
        }

        free (seat_id);
        g_free (session_id);

        return ret;
}

gboolean
gdm_goto_login_session (GCancellable *cancellable,
                        GError      **error)
{
        g_autoptr(GDBusConnection) connection = NULL;
        g_autoptr(GError) local_error = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, &local_error);
        if (connection == NULL) {
                g_debug ("Failed to connect to the D-Bus daemon: %s", local_error->message);
                g_propagate_error (error, g_steal_pointer (&local_error));
                return FALSE;
        }

        return goto_login_session (connection, cancellable, error);
}

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("Gdm: script environment: %s", str);
        g_ptr_array_add (env, str);
}

GPtrArray *
gdm_get_script_environment (const char *username,
                            const char *display_name,
                            const char *display_hostname,
                            const char *display_x11_authority_file)
{
        GPtrArray     *env;
        GHashTable    *hash;
        struct passwd *pwent;

        env = g_ptr_array_new_with_free_func (g_free);

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        /* modify environment here */
        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        if (username != NULL) {
                g_hash_table_insert (hash, g_strdup ("LOGNAME"),
                                     g_strdup (username));
                g_hash_table_insert (hash, g_strdup ("USER"),
                                     g_strdup (username));
                g_hash_table_insert (hash, g_strdup ("USERNAME"),
                                     g_strdup (username));

                gdm_get_pwent_for_name (username, &pwent);
                if (pwent != NULL) {
                        if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                                g_hash_table_insert (hash, g_strdup ("HOME"),
                                                     g_strdup (pwent->pw_dir));
                                g_hash_table_insert (hash, g_strdup ("PWD"),
                                                     g_strdup (pwent->pw_dir));
                        }

                        g_hash_table_insert (hash, g_strdup ("SHELL"),
                                             g_strdup (pwent->pw_shell));

                        /* Also get group name and propagate down */
                        struct group *grent;

                        if (gdm_get_grent_for_gid (pwent->pw_gid, &grent)) {
                                g_hash_table_insert (hash, g_strdup ("GROUP"), g_strdup (grent->gr_name));
                        }
                }
        }

        if (display_hostname) {
                g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (display_hostname));
        }

        /* Runs as root */
        if (display_x11_authority_file) {
                g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (display_x11_authority_file));
        }

        if (display_name) {
                g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (display_name));
        }
        g_hash_table_insert (hash, g_strdup ("PATH"), g_strdup (GDM_SESSION_DEFAULT_PATH));
        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

        g_hash_table_remove (hash, "MAIL");

        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

gboolean
gdm_run_script (const char *dir,
                const char *username,
                const char *display_name,
                const char *display_hostname,
                const char *display_x11_authority_file)
{
        char      *script;
        char     **argv;
        gint       status;
        GError    *error;
        GPtrArray *env;
        gboolean   res;
        gboolean   ret;

        ret = FALSE;

        g_assert (dir != NULL);
        g_assert (username != NULL);

        script = g_build_filename (dir, display_name, NULL);
        g_debug ("Trying script %s", script);
        if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
               && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                g_debug ("script %s not found; skipping", script);
                g_free (script);
                script = NULL;
        }

        if (script == NULL
            && display_hostname != NULL
            && display_hostname[0] != '\0') {
                script = g_build_filename (dir, display_hostname, NULL);
                g_debug ("Trying script %s", script);
                if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
                       && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                        g_debug ("script %s not found; skipping", script);
                        g_free (script);
                        script = NULL;
                }
        }

        if (script == NULL) {
                script = g_build_filename (dir, "Default", NULL);
                g_debug ("Trying script %s", script);
                if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
                       && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                        g_debug ("script %s not found; skipping", script);
                        g_free (script);
                        script = NULL;
                }
        }

        if (script == NULL) {
                g_debug ("no script found");
                return TRUE;
        }

        g_debug ("Running process: %s", script);
        error = NULL;
        if (! g_shell_parse_argv (script, NULL, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                goto out;
        }

        env = gdm_get_script_environment (username,
                                          display_name,
                                          display_hostname,
                                          display_x11_authority_file);

        res = g_spawn_sync (NULL,
                            argv,
                            (char **)env->pdata,
                            G_SPAWN_SEARCH_PATH,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            &status,
                            &error);

        g_ptr_array_free (env, TRUE);
        g_strfreev (argv);

        if (! res) {
                g_warning ("Unable to run script: %s", error->message);
                g_error_free (error);
        }

        if (WIFEXITED (status)) {
                g_debug ("Process exit status: %d", WEXITSTATUS (status));
                ret = WEXITSTATUS (status) == 0;
        }

 out:
        g_free (script);

        return ret;
}

gboolean
gdm_shell_var_is_valid_char (gchar c, gboolean first)
{
        return (!first && g_ascii_isdigit (c)) ||
                c == '_' ||
                g_ascii_isalpha (c);
}

/* This expands a string somewhat similar to how a shell would do it
   if it was enclosed inside double quotes.  It handles variable
   expansion like $FOO and ${FOO}, single-char escapes using \, and
   non-escaped # at the begining of a word is taken as a comment and ignored */
char *
gdm_shell_expand (const char *str,
                  GdmExpandVarFunc expand_var_func,
                  gpointer user_data)
{
        GString *s = g_string_new("");
        const gchar *p, *start;
        gchar c;
        gboolean at_new_word;

        p = str;
        at_new_word = TRUE;
        while (*p) {
                c = *p;
                if (c == '\\') {
                        p++;
                        c = *p;
                        if (c != '\0') {
                                p++;
                                switch (c) {
                                case '\\':
                                        g_string_append_c (s, '\\');
                                        break;
                                case '$':
                                        g_string_append_c (s, '$');
                                        break;
                                case '#':
                                        g_string_append_c (s, '#');
                                        break;
                                default:
                                        g_string_append_c (s, '\\');
                                        g_string_append_c (s, c);
                                        break;
                                }
                        }
                } else if (c == '#' && at_new_word) {
                        break;
                } else if (c == '$') {
                        gboolean brackets = FALSE;
                        p++;
                        if (*p == '{') {
                                brackets = TRUE;
                                p++;
                        }
                        start = p;
                        while (*p != '\0' &&
                               gdm_shell_var_is_valid_char (*p, p == start))
                                p++;
                        if (p == start || (brackets && *p != '}')) {
                                /* Invalid variable, use as-is */
                                g_string_append_c (s, '$');
                                if (brackets)
                                        g_string_append_c (s, '{');
                                g_string_append_len (s, start, p - start);
                        } else {
                                gchar *expanded;
                                gchar *var = g_strndup (start, p - start);
                                if (brackets && *p == '}')
                                        p++;

                                expanded = expand_var_func (var, user_data);
                                if (expanded)
                                        g_string_append (s, expanded);
                                g_free (var);
                                g_free (expanded);
                        }
                } else {
                        p++;
                        g_string_append_c (s, c);
                        at_new_word = g_ascii_isspace (c);
                }
        }
        return g_string_free (s, FALSE);
}

static gboolean
_systemd_session_is_graphical (const char *session_id)
{
        const gchar * const graphical_session_types[] = { "wayland", "x11", "mir", NULL };
        int saved_errno;
        g_autofree gchar *type = NULL;

        saved_errno = sd_session_get_type (session_id, &type);
        if (saved_errno < 0) {
                g_warning ("Couldn't get type for session '%s': %s",
                           session_id,
                           g_strerror (-saved_errno));
                return FALSE;
        }

        if (!g_strv_contains (graphical_session_types, type)) {
                g_debug ("Session '%s' is not a graphical session (type: '%s')",
                         session_id,
                         type);
                return FALSE;
        }

        return TRUE;
}

static gboolean
_systemd_session_is_active (const char *session_id)
{
        const gchar * const active_states[] = { "active", "online", NULL };
        int saved_errno;
        g_autofree gchar *state = NULL;

        /*
         * display sessions can be 'closing' if they are logged out but some
         * processes are lingering; we shouldn't consider these (this is
         * checking for a race condition since we specified
         * GDM_SYSTEMD_SESSION_REQUIRE_ONLINE)
         */
        saved_errno = sd_session_get_state (session_id, &state);
        if (saved_errno < 0) {
                g_warning ("Couldn't get state for session '%s': %s",
                           session_id,
                           g_strerror (-saved_errno));
                return FALSE;
        }

        if (!g_strv_contains (active_states, state)) {
                g_debug ("Session '%s' is not active or online", session_id);
                return FALSE;
        }

        return TRUE;
}

gboolean
gdm_find_display_session (GPid        pid,
                          const uid_t uid,
                          char      **out_session_id,
                          GError    **error)
{
        char *local_session_id = NULL;
        g_auto(GStrv) sessions = NULL;
        int n_sessions;
        int res;

        g_return_val_if_fail (out_session_id != NULL, FALSE);

        /* First try to look up the session using the pid. We need this
         * at least for the greeter, because it currently runs multiple
         * sessions under the same user.
         * See also commit 2b52d8933c8ab38e7ee83318da2363d00d8c5581
         * which added an explicit dbus-run-session for all but seat0.
         */
        res = sd_pid_get_session (pid, &local_session_id);
        if (res >= 0) {
                g_debug ("GdmCommon: Found session %s for PID %d, using", local_session_id, pid);

                *out_session_id = g_strdup (local_session_id);
                free (local_session_id);

                return TRUE;
        } else {
                if (res != -ENODATA)
                        g_warning ("GdmCommon: Failed to retrieve session information for pid %d: %s",
                                   pid, strerror (-res));
        }

        g_debug ("Finding a graphical session for user %d", uid);

        n_sessions = sd_uid_get_sessions (uid,
                                          GDM_SYSTEMD_SESSION_REQUIRE_ONLINE,
                                          &sessions);

        if (n_sessions < 0) {
                g_set_error (error,
                             GDM_COMMON_ERROR,
                             0,
                             "Failed to get sessions for user %d",
                             uid);
                return FALSE;
        }

        for (int i = 0; i < n_sessions; ++i) {
                g_debug ("Considering session '%s'", sessions[i]);

                if (!_systemd_session_is_graphical (sessions[i]))
                        continue;

                if (!_systemd_session_is_active (sessions[i]))
                        continue;

                /*
                 * We get the sessions from newest to oldest, so take the last
                 * one we find that's good
                 */
                local_session_id = sessions[i];
        }

        if (local_session_id == NULL) {
                g_set_error (error,
                             GDM_COMMON_ERROR,
                             0,
                             "Could not find a graphical session for user %d",
                             uid);
                return FALSE;
        }

        *out_session_id = g_strdup (local_session_id);

        return TRUE;
}

static void
load_env_file (GFile *file,
               GdmLoadEnvVarFunc load_env_func,
               GdmExpandVarFunc  expand_func,
               gpointer user_data)
{
        gchar *contents;
        gchar **lines;
        gchar *line, *p;
        gchar *var, *var_end;
        gchar *expanded;
        char *filename;
        int i;

        filename = g_file_get_path (file);
        g_debug ("Loading env vars from %s\n", filename);
        g_free (filename);

        if (g_file_load_contents (file, NULL, &contents, NULL, NULL, NULL)) {
                lines = g_strsplit (contents, "\n", -1);
                g_free (contents);
                for (i = 0; lines[i] != NULL; i++) {
                        line = lines[i];
                        p = line;
                        while (g_ascii_isspace (*p))
                                p++;
                        if (*p == '#' || *p == '\0')
                                continue;
                        var = p;
                        while (gdm_shell_var_is_valid_char (*p, p == var))
                                p++;
                        var_end = p;
                        while (g_ascii_isspace (*p))
                                p++;
                        if (var == var_end || *p != '=') {
                                g_warning ("Invalid env.d line '%s'\n", line);
                                continue;
                        }
                        *var_end = 0;
                        p++; /* Skip = */
                        while (g_ascii_isspace (*p))
                                p++;

                        expanded = gdm_shell_expand (p, expand_func, user_data);
                        expanded = g_strchomp (expanded);
                        load_env_func (var, expanded, user_data);
                        g_free (expanded);
                }
                g_strfreev (lines);
        }
}

static gint
compare_str (gconstpointer  a,
             gconstpointer  b)
{
  return strcmp (*(const char **)a, *(const char **)b);
}

static void
gdm_load_env_dir (GFile *dir,
                  GdmLoadEnvVarFunc load_env_func,
                  GdmExpandVarFunc  expand_func,
                  gpointer user_data)
{
        GFileInfo *info = NULL;
        GFileEnumerator *enumerator = NULL;
        GPtrArray *names = NULL;
        GFile *file;
        const gchar *name;
        int i;

        enumerator = g_file_enumerate_children (dir,
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE","
                                                G_FILE_ATTRIBUTE_STANDARD_NAME","
                                                G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN","
                                                G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP,
                                                G_FILE_QUERY_INFO_NONE,
                                                NULL, NULL);
        if (!enumerator) {
                goto out;
        }

        names = g_ptr_array_new_with_free_func (g_free);
        while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
                if (g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR &&
                    !g_file_info_get_is_hidden (info) &&
                    g_str_has_suffix (g_file_info_get_name (info), ".env"))
                  g_ptr_array_add (names, g_strdup (g_file_info_get_name (info)));

                g_clear_object (&info);
        }

        g_ptr_array_sort (names, compare_str);

        for (i = 0; i < names->len; i++) {
                name = g_ptr_array_index (names, i);
                file = g_file_get_child (dir, name);
                load_env_file (file, load_env_func, expand_func, user_data);
                g_object_unref (file);
        }

 out:
        g_clear_pointer (&names, g_ptr_array_unref);
        g_clear_object (&enumerator);
}

void
gdm_load_env_d (GdmLoadEnvVarFunc load_env_func,
                GdmExpandVarFunc  expand_func,
                gpointer user_data)
{
        GFile *dir;

        dir = g_file_new_for_path (DATADIR "/gdm/env.d");
        gdm_load_env_dir (dir, load_env_func, expand_func, user_data);
        g_object_unref (dir);

        dir = g_file_new_for_path (GDMCONFDIR "/env.d");
        gdm_load_env_dir (dir, load_env_func, expand_func, user_data);
        g_object_unref (dir);
}
