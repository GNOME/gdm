/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include "config.h"

#include <locale.h>
#include <sysexits.h>

#include "gdm-common.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"
#include "gdm-log.h"

#include "gdm-manager-glue.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <glib.h>

#include <gio/gunixinputstream.h>

#define BUS_ADDRESS_FILENO (STDERR_FILENO + 1)

typedef struct
{
        GdmSettings  *settings;
        GCancellable *cancellable;

        GSubprocess     *bus_subprocess;
        GDBusConnection *bus_connection;
        GdmDBusManager  *display_manager_proxy;
        char            *bus_address;

        char           **environment;

        GSubprocess  *session_subprocess;
        char         *session_command;
        int           session_exit_status;

        guint         register_session_id;

        GMainLoop    *main_loop;

        guint32       debug_enabled : 1;
} State;

static void
on_bus_finished (GSubprocess  *subprocess,
                 GAsyncResult *result,
                 State        *state)
{
        gboolean cancelled;

        cancelled = !g_subprocess_wait_finish (subprocess, result, NULL);

        if (cancelled) {
                goto out;
        }

        if (g_subprocess_get_if_exited (subprocess)) {
                int exit_status;

                exit_status = g_subprocess_get_exit_status (subprocess);

                g_debug ("message bus exited with status %d", exit_status);
        } else {
                int signal_number;

                signal_number = g_subprocess_get_term_sig (subprocess);
                g_debug ("message bus was killed with status %d", signal_number);
        }

        g_clear_object (&state->bus_subprocess);
out:
        g_main_loop_quit (state->main_loop);
}

static gboolean
spawn_bus (State        *state,
           GCancellable *cancellable)
{
        GDBusConnection     *bus_connection = NULL;
        GPtrArray           *arguments = NULL;
        GSubprocessLauncher *launcher = NULL;
        GSubprocess         *subprocess = NULL;
        GInputStream        *input_stream = NULL;
        GDataInputStream    *data_stream = NULL;
        GError              *error = NULL;
        char                *bus_address_fd_string = NULL;
        char                *bus_address = NULL;
        gsize                bus_address_size;

        gboolean  is_running = FALSE;
        int       ret;
        int       pipe_fds[2];

        g_debug ("Running session message bus");

        bus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                         cancellable,
                                         NULL);

        if (bus_connection != NULL) {
                g_debug ("session message bus already running, not starting another one");
                state->bus_connection = bus_connection;
                return TRUE;
        }

        ret = g_unix_open_pipe (pipe_fds, FD_CLOEXEC, &error);

        if (!ret) {
                g_debug ("could not open pipe: %s", error->message);
                goto out;
        }

        arguments = g_ptr_array_new ();
        launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
        g_subprocess_launcher_take_fd (launcher, pipe_fds[1], BUS_ADDRESS_FILENO);

        bus_address_fd_string = g_strdup_printf ("%d", BUS_ADDRESS_FILENO);

        g_ptr_array_add (arguments, "dbus-daemon");

        g_ptr_array_add (arguments, "--print-address");
        g_ptr_array_add (arguments, bus_address_fd_string);
        g_ptr_array_add (arguments, "--session");
        g_ptr_array_add (arguments, NULL);

        subprocess = g_subprocess_launcher_spawnv (launcher,
                                                   (const char * const *) arguments->pdata,
                                                   &error);
        g_free (bus_address_fd_string);
        g_clear_object (&launcher);
        g_ptr_array_free (arguments, TRUE);

        if (subprocess == NULL) {
                g_debug ("could not start dbus-daemon: %s", error->message);
                goto out;
        }

        input_stream = g_unix_input_stream_new (pipe_fds[0], TRUE);
        data_stream = g_data_input_stream_new (input_stream);
        g_clear_object (&input_stream);

        bus_address = g_data_input_stream_read_line (data_stream,
                                                     &bus_address_size,
                                                     cancellable,
                                                     &error);

        if (error != NULL) {
                g_debug ("could not read address from session message bus: %s", error->message);
                goto out;
        }

        if (bus_address == NULL) {
                g_debug ("session message bus did not write address");
                goto out;
        }

        state->bus_address = bus_address;

        state->bus_subprocess = g_object_ref (subprocess);

        g_subprocess_wait_async (state->bus_subprocess,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_bus_finished,
                                 state);

        bus_connection = g_dbus_connection_new_for_address_sync (state->bus_address,
                                                                 G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                                 G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                                 NULL,
                                                                 cancellable,
                                                                 &error);

        if (bus_connection == NULL) {
                g_debug ("could not open connection to session bus: %s",
                         error->message);
                goto out;
        }

        state->bus_connection = bus_connection;
        is_running = TRUE;
out:
        g_clear_object (&data_stream);
        g_clear_object (&subprocess);
        g_clear_object (&launcher);
        g_clear_error (&error);

        return is_running;
}

static gboolean
import_environment (State        *state,
                    GCancellable *cancellable)
{
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GVariant) environment_variant = NULL;
        g_autoptr(GError)   error = NULL;

        reply = g_dbus_connection_call_sync (state->bus_connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.DBus.Properties",
                                             "Get",
                                             g_variant_new ("(ss)",
                                                            "org.freedesktop.systemd1.Manager",
                                                            "Environment"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, cancellable, &error);

        if (reply == NULL) {
                g_debug ("could not fetch environment: %s", error->message);
                return FALSE;
        }

        g_variant_get (reply, "(v)", &environment_variant);

        state->environment = g_variant_dup_strv (environment_variant, NULL);

        return TRUE;
}

static void
on_session_finished (GSubprocess  *subprocess,
                     GAsyncResult *result,
                     State        *state)
{
        gboolean cancelled;

        cancelled = !g_subprocess_wait_finish (subprocess, result, NULL);

        if (cancelled) {
                goto out;
        }

        if (g_subprocess_get_if_exited (subprocess)) {
                int exit_status;

                exit_status = g_subprocess_get_exit_status (subprocess);

                g_debug ("session exited with status %d", exit_status);

                state->session_exit_status = exit_status;
        } else {
                int signal_number;

                signal_number = g_subprocess_get_term_sig (subprocess);
                g_debug ("session was killed with status %d", signal_number);
        }

        g_clear_object (&state->session_subprocess);
out:
        g_main_loop_quit (state->main_loop);
}

static gboolean
spawn_session (State        *state,
               GCancellable *cancellable)
{
        GSubprocessLauncher *launcher = NULL;
        GSubprocess         *subprocess = NULL;
        GError              *error = NULL;
        gboolean             is_running = FALSE;
        int                  ret;
        char               **argv = NULL;
        static const char  *session_variables[] = { "DISPLAY",
                                                    "XAUTHORITY",
                                                    "WAYLAND_DISPLAY",
                                                    "WAYLAND_SOCKET",
                                                    "GNOME_SHELL_SESSION_MODE",
                                                    NULL };
        /* The environment variables listed below are those we have set (or
         * received from our own execution environment) only as a fallback to
         * make things work, as opposed to a information directly pertaining to
         * the session about to be started. Variables listed here will not
         * overwrite the existing environment (possibly) imported from the
         * systemd --user instance.
         * As an example: We need a PATH for some of the launched subprocesses
         * to work, but if the user (or the distributor) has customized the PATH
         * via one of systemds user-environment-generators, that version should
         * be preferred. */
        static const char  *fallback_variables[] = { "PATH", NULL };

        g_debug ("Running wayland session");

        ret = g_shell_parse_argv (state->session_command,
                                  NULL,
                                  &argv,
                                  &error);

        if (!ret) {
                g_debug ("could not parse session arguments: %s", error->message);
                goto out;
        }

        launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

        if (state->environment != NULL) {
                size_t i;

                for (i = 0; state->environment[i] != NULL; i++) {
                        g_auto(GStrv) environment_entry = NULL;

                        if (state->environment[i][0] == '\0') {
                                continue;
                        }

                        environment_entry = g_strsplit (state->environment[i], "=", 2);

                        if (environment_entry[0] == NULL || environment_entry[1] == NULL) {
                                continue;
                        }

                        /* Merge the environment block imported from systemd --user with the
                         * environment we have set for ourselves (and thus pass on to the
                         * launcher process). Variables we have set have precedence, as to not
                         * import stale data from prior user sessions, with the exception of
                         * those listed in fallback_variables. See the comment there for more
                         * explanations. */
                        g_subprocess_launcher_setenv (launcher,
                                                      environment_entry[0],
                                                      environment_entry[1],
                                                      g_strv_contains (fallback_variables, environment_entry[0]));
                }

                /* Don't allow session specific environment variables from earlier sessions to
                 * leak through */
                for (i = 0; session_variables[i] != NULL; i++) {
                        if (g_getenv (session_variables[i]) == NULL) {
                                g_subprocess_launcher_unsetenv (launcher, session_variables[i]);
                        }
                }
        }

        if (state->bus_address != NULL) {
                g_subprocess_launcher_setenv (launcher, "DBUS_SESSION_BUS_ADDRESS", state->bus_address, TRUE);
        }

        subprocess = g_subprocess_launcher_spawnv (launcher,
                                                   (const char * const *) argv,
                                                   &error);
        g_strfreev (argv);

        if (subprocess == NULL) {
                g_debug ("could not start session: %s", error->message);
                goto out;
        }

        state->session_subprocess = g_object_ref (subprocess);

        g_subprocess_wait_async (state->session_subprocess,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_session_finished,
                                 state);

        is_running = TRUE;
out:
        g_clear_object (&subprocess);
        return is_running;
}

static void
signal_subprocesses (State *state)
{
        if (state->session_subprocess != NULL) {
                g_subprocess_send_signal (state->session_subprocess, SIGTERM);
        }

        if (state->bus_subprocess != NULL) {
                g_subprocess_send_signal (state->bus_subprocess, SIGTERM);
        }
}

static void
wait_on_subprocesses (State *state)
{
        if (state->bus_subprocess != NULL) {
                g_subprocess_wait (state->bus_subprocess, NULL, NULL);
        }

        if (state->session_subprocess != NULL) {
                g_subprocess_wait (state->session_subprocess, NULL, NULL);
        }
}

static gboolean
register_display (State        *state,
                  GCancellable *cancellable)
{
        GError          *error = NULL;
        gboolean         registered = FALSE;
        GVariantBuilder  details;

        g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
        g_variant_builder_add (&details, "{ss}", "session-type", "wayland");

        registered = gdm_dbus_manager_call_register_display_sync (state->display_manager_proxy,
                                                                  g_variant_builder_end (&details),
                                                                  cancellable,
                                                                  &error);
        if (error != NULL) {
                g_debug ("Could not register display: %s", error->message);
                g_error_free (error);
        }

        return registered;
}

static void
init_state (State **state)
{
        static State state_allocation;

        *state = &state_allocation;
}

static void
clear_state (State **out_state)
{
        State *state = *out_state;

        g_clear_object (&state->cancellable);
        g_clear_object (&state->bus_connection);
        g_clear_object (&state->session_subprocess);
        g_clear_pointer (&state->environment, g_strfreev);
        g_clear_pointer (&state->main_loop, g_main_loop_unref);
        g_clear_handle_id (&state->register_session_id, g_source_remove);
        *out_state = NULL;
}

static gboolean
on_sigterm (State *state)
{
        g_cancellable_cancel (state->cancellable);

        if (g_main_loop_is_running (state->main_loop)) {
                g_main_loop_quit (state->main_loop);
        }

        return G_SOURCE_CONTINUE;
}

static gboolean
register_session_timeout_cb (gpointer user_data)
{
        State *state;
        GError *error = NULL;

        state = (State *) user_data;

        gdm_dbus_manager_call_register_session_sync (state->display_manager_proxy,
                                                     g_variant_new ("a{sv}", NULL),
                                                     state->cancellable,
                                                     &error);

        if (error != NULL) {
                g_warning ("Could not register session: %s", error->message);
                g_error_free (error);
        }

        return G_SOURCE_REMOVE;
}

static gboolean
connect_to_display_manager (State *state)
{
        g_autoptr (GError) error = NULL;

        state->display_manager_proxy = gdm_dbus_manager_proxy_new_for_bus_sync (
                G_BUS_TYPE_SYSTEM,
                G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                "org.gnome.DisplayManager",
                "/org/gnome/DisplayManager/Manager",
                state->cancellable,
                &error);

        if (state->display_manager_proxy == NULL) {
                g_printerr ("gdm-wayland-session: could not contact display manager: %s\n",
                            error->message);
                return FALSE;
        }

        return TRUE;
}

int
main (int    argc,
      char **argv)
{
        State           *state = NULL;
        GOptionContext  *context = NULL;
        static char    **args = NULL;
        gboolean         debug = FALSE;
        gboolean         ret;
        int              exit_status = EX_OK;
        static gboolean  register_session = FALSE;

        static GOptionEntry entries []   = {
                { "register-session", 0, 0, G_OPTION_ARG_NONE, &register_session, "Register session after a delay", NULL },
                { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, "", "" },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        gdm_log_init ();

        context = g_option_context_new (_("GNOME Display Manager Wayland Session Launcher"));
        g_option_context_add_main_entries (context, entries, NULL);

        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        if (args == NULL || args[0] == NULL || args[1] != NULL) {
                g_warning ("gdm-wayland-session takes one argument (the session)");
                exit_status = EX_USAGE;
                goto out;
        }

        init_state (&state);

        state->session_command = args[0];

        state->settings = gdm_settings_new ();
        ret = gdm_settings_direct_init (state->settings, DATADIR "/gdm/gdm.schemas", "/");

        if (!ret) {
                g_printerr ("Unable to initialize settings\n");
                exit_status = EX_DATAERR;
                goto out;
        }

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        state->debug_enabled = debug;

        gdm_log_set_debug (debug);

        state->main_loop = g_main_loop_new (NULL, FALSE);
        state->cancellable = g_cancellable_new ();

        g_unix_signal_add (SIGTERM, (GSourceFunc) on_sigterm, state);

        ret = spawn_bus (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to run session message bus\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        import_environment (state, state->cancellable);

        ret = spawn_session (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to run session\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        if (!connect_to_display_manager (state))
                goto out;

        ret = register_display (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to register display with display manager\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        if (register_session) {
                g_debug ("gdm-wayland-session: Will register session in %d seconds", REGISTER_SESSION_TIMEOUT);
                state->register_session_id = g_timeout_add_seconds (REGISTER_SESSION_TIMEOUT,
                                                                    register_session_timeout_cb,
                                                                    state);
        } else {
                g_debug ("gdm-wayland-session: Session will register itself");
        }

        g_main_loop_run (state->main_loop);

        /* Only use exit status of session if we're here because it exit */

        if (state->session_subprocess == NULL) {
                exit_status = state->session_exit_status;
        }

out:
        if (state != NULL) {
                signal_subprocesses (state);
                wait_on_subprocesses (state);
                clear_state (&state);
        }

        return exit_status;
}
