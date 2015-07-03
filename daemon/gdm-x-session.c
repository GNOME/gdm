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
#include <glib-unix.h>
#include <X11/Xauth.h>

#define DISPLAY_FILENO (STDERR_FILENO + 1)
#define BUS_ADDRESS_FILENO (DISPLAY_FILENO + 1)

typedef struct
{
        GdmSettings  *settings;
        GCancellable *cancellable;

        GSubprocess  *x_subprocess;
        char         *auth_file;
        char         *display_name;

        GSubprocess     *bus_subprocess;
        GDBusConnection *bus_connection;
        char            *bus_address;

        char           **environment;

        GSubprocess  *session_subprocess;
        char         *session_command;
        int           session_exit_status;

        GMainLoop    *main_loop;

        guint32       debug_enabled : 1;
} State;

static FILE *
create_auth_file (char **filename)
{
        char *auth_dir = NULL;
        char *auth_file = NULL;
        int fd;
        FILE *fp = NULL;

        auth_dir = g_build_filename (g_get_user_runtime_dir (),
                                     "gdm",
                                     NULL);

        g_mkdir_with_parents (auth_dir, 0711);
        auth_file = g_build_filename (auth_dir, "Xauthority", NULL);
        g_clear_pointer (&auth_dir, g_free);

        fd = g_open (auth_file, O_RDWR | O_CREAT | O_TRUNC, 0700);

        if (fd < 0) {
                g_debug ("could not open %s to store auth cookie: %m",
                         auth_file);
                g_clear_pointer (&auth_file, g_free);
                goto out;
        }

        fp = fdopen (fd, "w+");

        if (fp == NULL) {
                g_debug ("could not set up stream for auth cookie file: %m");
                g_clear_pointer (&auth_file, g_free);
                close (fd);
                goto out;
        }

        *filename = auth_file;
out:
        return fp;
}

static char *
prepare_auth_file (void)
{
        FILE     *fp = NULL;
        char     *filename = NULL;
        GError   *error = NULL;
        gboolean  prepared = FALSE;
        Xauth     auth_entry = { 0 };
        char      localhost[HOST_NAME_MAX + 1] = "";

        g_debug ("Preparing auth file for X server");

        fp = create_auth_file (&filename);

        if (fp == NULL) {
                return NULL;
        }

        if (gethostname (localhost, HOST_NAME_MAX) < 0) {
                strncpy (localhost, "localhost", sizeof (localhost) - 1);
        }

        auth_entry.family = FamilyLocal;
        auth_entry.address = localhost;
        auth_entry.address_length = strlen (auth_entry.address);
        auth_entry.name = "MIT-MAGIC-COOKIE-1";
        auth_entry.name_length = strlen (auth_entry.name);

        auth_entry.data_length = 16;
        auth_entry.data = gdm_generate_random_bytes (auth_entry.data_length, &error);

        if (error != NULL) {
                goto out;
        }

        if (!XauWriteAuth (fp, &auth_entry) || fflush (fp) == EOF) {
                goto out;
        }

        auth_entry.family = FamilyWild;
        if (!XauWriteAuth (fp, &auth_entry) || fflush (fp) == EOF) {
                goto out;
        }

        prepared = TRUE;

out:
        g_clear_pointer (&auth_entry.data, g_free);
        g_clear_pointer (&fp, fclose);

        if (!prepared) {
                g_clear_pointer (&filename, g_free);
        }

        return filename;
}

static void
on_x_server_finished (GSubprocess  *subprocess,
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

                g_debug ("X server exited with status %d", exit_status);
        } else {
                int signal_number;

                signal_number = g_subprocess_get_term_sig (subprocess);
                g_debug ("X server was killed with status %d", signal_number);
        }

        g_clear_object (&state->x_subprocess);
out:
        g_main_loop_quit (state->main_loop);
}

static gboolean
spawn_x_server (State        *state,
                gboolean      allow_remote_connections,
                GCancellable *cancellable)
{
        GPtrArray           *arguments = NULL;
        GSubprocessLauncher *launcher = NULL;
        GSubprocess         *subprocess = NULL;
        GInputStream        *input_stream = NULL;
        GDataInputStream    *data_stream = NULL;
        GError              *error = NULL;

        char     *auth_file;
        gboolean  is_running = FALSE;
        int       ret;
        int       pipe_fds[2];
        char     *display_fd_string = NULL;
        char     *vt_string = NULL;
        char     *display_number;
        gsize     display_number_size;

        auth_file = prepare_auth_file ();

        g_debug ("Running X server");

        ret = g_unix_open_pipe (pipe_fds, FD_CLOEXEC, &error);

        if (!ret) {
                g_debug ("could not open pipe: %s", error->message);
                goto out;
        }

        arguments = g_ptr_array_new ();
        launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_INHERIT);
        g_subprocess_launcher_setenv (launcher, "XORG_RUN_AS_USER_OK", "1", TRUE);
        g_subprocess_launcher_take_fd (launcher, pipe_fds[1], DISPLAY_FILENO);

        if (g_getenv ("XDG_VTNR") != NULL) {
                int vt;

                vt = atoi (g_getenv ("XDG_VTNR"));

                if (vt > 0 && vt < 64) {
                        vt_string = g_strdup_printf ("vt%d", vt);
                }
        }

        display_fd_string = g_strdup_printf ("%d", DISPLAY_FILENO);

        g_ptr_array_add (arguments, X_SERVER);

        if (vt_string != NULL) {
                g_ptr_array_add (arguments, vt_string);
        }

        g_ptr_array_add (arguments, "-displayfd");
        g_ptr_array_add (arguments, display_fd_string);

        g_ptr_array_add (arguments, "-auth");
        g_ptr_array_add (arguments, auth_file);

        /* If we were compiled with Xserver >= 1.17 we need to specify
         * '-listen tcp' as the X server dosen't listen on tcp sockets
         * by default anymore. In older versions we need to pass
         * -nolisten tcp to disable listening on tcp sockets.
         */
#ifdef HAVE_XSERVER_THAT_DEFAULTS_TO_LOCAL_ONLY
        if (allow_remote_connections) {
                g_ptr_array_add (arguments, "-listen");
                g_ptr_array_add (arguments, "tcp");
        }
#else
        if (!allow_remote_connections) {
                g_ptr_array_add (arguments, "-nolisten");
                g_ptr_array_add (arguments, "tcp");
        }
#endif

        g_ptr_array_add (arguments, "-background");
        g_ptr_array_add (arguments, "none");

        g_ptr_array_add (arguments, "-noreset");
        g_ptr_array_add (arguments, "-keeptty");
        g_ptr_array_add (arguments, "-audit");
        g_ptr_array_add (arguments, "4");

        g_ptr_array_add (arguments, "-verbose");
        if (state->debug_enabled) {
                g_ptr_array_add (arguments, "7");
        } else {
                g_ptr_array_add (arguments, "3");
        }

        if (state->debug_enabled) {
                g_ptr_array_add (arguments, "-core");
        }
        g_ptr_array_add (arguments, NULL);

        subprocess = g_subprocess_launcher_spawnv (launcher,
                                                   (const char * const *) arguments->pdata,
                                                   &error);
        g_free (display_fd_string);
        g_clear_object (&launcher);
        g_ptr_array_free (arguments, TRUE);

        if (subprocess == NULL) {
                g_debug ("could not start X server: %s", error->message);
                goto out;
        }

        input_stream = g_unix_input_stream_new (pipe_fds[0], TRUE);
        data_stream = g_data_input_stream_new (input_stream);
        g_clear_object (&input_stream);

        display_number = g_data_input_stream_read_line (data_stream,
                                                        &display_number_size,
                                                        cancellable,
                                                        &error);

        if (error != NULL) {
                g_debug ("could not read display string from X server: %s", error->message);
                goto out;
        }

        if (display_number == NULL) {
                g_debug ("X server did not write display string");
                goto out;
        }

        state->display_name = g_strdup_printf (":%s", display_number);
        g_clear_pointer (&display_number, g_free);

        state->auth_file = g_strdup (auth_file);
        state->x_subprocess = g_object_ref (subprocess);

        g_subprocess_wait_async (state->x_subprocess,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_x_server_finished,
                                 state);

        is_running = TRUE;
out:
        g_clear_pointer (&auth_file, g_free);
        g_clear_object (&data_stream);
        g_clear_object (&subprocess);
        g_clear_object (&launcher);
        g_clear_error (&error);

        return is_running;
}

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
update_bus_environment (State        *state,
                        GCancellable *cancellable)
{
        GVariantBuilder      builder;
        GVariant            *reply = NULL;
        GError              *error = NULL;
        gboolean             environment_updated = FALSE;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
        g_variant_builder_add (&builder, "{ss}", "DISPLAY", state->display_name);
        g_variant_builder_add (&builder, "{ss}", "XAUTHORITY", state->auth_file);

        reply = g_dbus_connection_call_sync (state->bus_connection,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "UpdateActivationEnvironment",
                                             g_variant_new ("(@a{ss})",
                                                            g_variant_builder_end (&builder)),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, cancellable, &error);

        if (reply == NULL) {
                g_debug ("could not update activation environment: %s", error->message);
                goto out;
        }

        g_variant_unref (reply);

        environment_updated = TRUE;

out:
        g_clear_error (&error);

        return environment_updated;
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
        char                *bus_address_fd_string;
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
               gboolean      run_script,
               GCancellable *cancellable)
{
        GSubprocessLauncher *launcher = NULL;
        GSubprocess         *subprocess = NULL;
        GError              *error = NULL;
        gboolean             is_running = FALSE;
        const char          *vt;
        static const char   *session_variables[] = { "DISPLAY",
                                                     "XAUTHORITY",
                                                     "WAYLAND_DISPLAY",
                                                     "WAYLAND_SOCKET",
                                                     "GNOME_SHELL_SESSION_MODE",
                                                     NULL };

        g_debug ("Running X session");

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

                        g_subprocess_launcher_setenv (launcher, environment_entry[0], environment_entry[1], FALSE);
                }

                /* Don't allow session specific environment variables from earlier sessions to
                 * leak through */
                for (i = 0; session_variables[i] != NULL; i++) {
                        if (g_getenv (session_variables[i]) == NULL) {
                                g_subprocess_launcher_unsetenv (launcher, session_variables[i]);
                        }
                }
        }

        g_subprocess_launcher_setenv (launcher, "DISPLAY", state->display_name, TRUE);
        g_subprocess_launcher_setenv (launcher, "XAUTHORITY", state->auth_file, TRUE);

        if (state->bus_address != NULL) {
                g_subprocess_launcher_setenv (launcher, "DBUS_SESSION_BUS_ADDRESS", state->bus_address, TRUE);
        }

        vt = g_getenv ("XDG_VTNR");

        if (vt != NULL) {
                g_subprocess_launcher_setenv (launcher, "WINDOWPATH", vt, TRUE);
        }

        if (run_script) {
                subprocess = g_subprocess_launcher_spawn (launcher,
                                                          &error,
                                                          GDMCONFDIR "/Xsession",
                                                          state->session_command,
                                                          NULL);
        } else {
                int ret;
                char **argv;

                ret = g_shell_parse_argv (state->session_command,
                                          NULL,
                                          &argv,
                                          &error);

                if (!ret) {
                        g_debug ("could not parse session arguments: %s", error->message);
                        goto out;
                }
                subprocess = g_subprocess_launcher_spawnv (launcher,
                                                           (const char * const *) argv,
                                                           &error);
                g_strfreev (argv);
        }

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

        if (state->x_subprocess != NULL) {
                g_subprocess_send_signal (state->x_subprocess, SIGTERM);
        }
}

static void
wait_on_subprocesses (State *state)
{
        if (state->x_subprocess != NULL) {
                g_subprocess_wait (state->x_subprocess, NULL, NULL);
        }

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
        GdmDBusManager  *manager = NULL;
        GError          *error = NULL;
        gboolean         registered = FALSE;
        GVariantBuilder  details;

        manager = gdm_dbus_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                           G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                           "org.gnome.DisplayManager",
                                                           "/org/gnome/DisplayManager/Manager",
                                                           cancellable,
                                                           &error);

        if (!manager) {
                g_debug ("could not contact display manager: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
        g_variant_builder_add (&details, "{ss}", "session-type", "x11");
        g_variant_builder_add (&details, "{ss}", "x11-display-name", state->display_name);

        registered = gdm_dbus_manager_call_register_display_sync (manager,
                                                                  g_variant_builder_end (&details),
                                                                  cancellable,
                                                                  &error);
        if (error != NULL) {
                g_debug ("Could not register display: %s", error->message);
                g_error_free (error);
        }

out:
        g_clear_object (&manager);
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
        g_clear_object (&state->x_subprocess);
        g_clear_pointer (&state->environment, g_strfreev);
        g_clear_pointer (&state->auth_file, g_free);
        g_clear_pointer (&state->display_name, g_free);
        g_clear_pointer (&state->main_loop, g_main_loop_unref);
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

int
main (int    argc,
      char **argv)
{
        State           *state = NULL;
        GOptionContext  *context = NULL;
        static char    **args = NULL;
        static gboolean  run_script = FALSE;
        static gboolean  allow_remote_connections = FALSE;
        gboolean         debug = FALSE;
        gboolean         ret;
        int              exit_status = EX_OK;
        static GOptionEntry entries []   = {
                { "run-script", 'r', 0, G_OPTION_ARG_NONE, &run_script, N_("Run program through /etc/gdm/Xsession wrapper script"), NULL },
                { "allow-remote-connections", 'a', 0, G_OPTION_ARG_NONE, &allow_remote_connections, N_("Listen on TCP socket"), NULL },
                { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, "", "" },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        gdm_log_init ();

        context = g_option_context_new (_("GNOME Display Manager X Session Launcher"));
        g_option_context_add_main_entries (context, entries, NULL);

        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        if (args == NULL || args[0] == NULL || args[1] != NULL) {
                g_warning ("gdm-x-session takes one argument (the session)");
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

        ret = spawn_x_server (state, allow_remote_connections, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to run X server\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        ret = spawn_bus (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to run session message bus\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        import_environment (state, state->cancellable);

        ret = update_bus_environment (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to update bus environment\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        ret = register_display (state, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to register display with display manager\n");
                exit_status = EX_SOFTWARE;
                goto out;
        }

        ret = spawn_session (state, run_script, state->cancellable);

        if (!ret) {
                g_printerr ("Unable to run session\n");
                exit_status = EX_SOFTWARE;
                goto out;
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
