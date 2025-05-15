/*
 * SPDX-FileCopyrightText: 2025 Joan Torres Lopez <joantolo@redhat.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <sysexits.h>
#include <systemd/sd-login.h>

#define GDM_DBUS_NAME                        "org.gnome.DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_PATH       "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_REMOTE_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/RemoteDisplayFactory"
#define GDM_DISPLAYS_PATH                    "/org/gnome/DisplayManager/Displays"
#define GDM_LOCAL_DISPLAY_FACTORY_INTERFACE  "org.gnome.DisplayManager.LocalDisplayFactory"
#define GDM_REMOTE_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.RemoteDisplayFactory"
#define GDM_DISPLAY_INTERFACE                "org.gnome.DisplayManager.Display"

typedef struct _State {
        GMainLoop *event_loop;

        GDBusConnection *connection;
        GDBusObjectManager *displays;

        char *username;
        gboolean headless;

        char *display_path;

        gboolean terminate_requested;
} State;

static gboolean terminate_session (State   *state,
                                   GError **error);

static void
state_free (State *state)
{
        g_clear_pointer (&state->event_loop, g_main_loop_unref);
        g_clear_object (&state->connection);
        g_clear_object (&state->displays);
        g_clear_pointer (&state->username, g_free);
        g_clear_pointer (&state->display_path, g_free);
        g_free (state);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (State, state_free);

static void
on_display_added (GDBusObjectManager *displays,
                  GDBusObject        *display_object,
                  State              *state)
{
        g_autoptr (GDBusInterface) display_proxy = NULL;
        g_autoptr (GVariant) reply = NULL;
        g_autoptr (GError) error = NULL;
        g_autofree char *session_id = NULL;
        g_autofree char *username = NULL;

        display_proxy = g_dbus_object_get_interface (display_object, GDM_DISPLAY_INTERFACE);
        if (display_proxy == NULL) {
                g_warning ("GDM exported a non Display object on dbus");
                return;
        }

        reply = g_dbus_proxy_call_sync (G_DBUS_PROXY (display_proxy),
                                        "GetSessionId",
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
        if (reply == NULL) {
                g_warning ("Failed getting SessionId: %s", error->message);
                return;
        }

        g_variant_get (reply, "(s)", &session_id);

        if (sd_session_get_username (session_id, &username) < 0)
                return;

        if (g_strcmp0 (state->username, username) == 0) {
                g_print ("Session started\n");

                g_set_str (&state->display_path, g_dbus_object_get_object_path (display_object));

                g_signal_handlers_disconnect_by_func (displays,
                                                      G_CALLBACK (on_display_added),
                                                      state);

                if (state->terminate_requested) {
                        if (!terminate_session (state, &error))
                                g_warning ("Terminating session failed: %s", error->message);
                }
        }
}
static void
on_display_removed (GDBusObjectManager *displays,
                    GDBusObject        *display_object,
                    State              *state)
{
        const char *display_path = g_dbus_object_get_object_path (display_object);

        if (g_strcmp0 (state->display_path, display_path) == 0) {
                g_print ("Session finished\n");

                g_set_str (&state->display_path, NULL);
                g_main_loop_quit (state->event_loop);
        }
}

static gboolean
watch_displays (State *state,
                GError **error)
{
        g_autoptr (GDBusObjectManager) displays = NULL;

        displays = g_dbus_object_manager_client_new_sync (state->connection,
                                                          G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                          GDM_DBUS_NAME,
                                                          GDM_DISPLAYS_PATH,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          error);
        if (displays == NULL) {
                g_dbus_error_strip_remote_error (*error);
                return FALSE;
        }

        g_signal_connect (displays,
                          "object-added",
                          G_CALLBACK (on_display_added),
                          state);

        g_signal_connect (displays,
                          "object-removed",
                          G_CALLBACK (on_display_removed),
                          state);

        state->displays = g_steal_pointer (&displays);

        return TRUE;
}

static gboolean
setup (State   *state,
       GError **error)
{
        g_autoptr (GDBusConnection) connection = NULL;

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
        if (connection == NULL) {
                g_dbus_error_strip_remote_error (*error);
                return FALSE;
        }

        state->connection = g_steal_pointer (&connection);

        if (!watch_displays (state, error))
                return FALSE;

        return TRUE;
}

static gboolean
call_gdm_display_factory_method (State       *state,
                                 const char  *method,
                                 GError     **error)
{
        GDBusConnection *connection = state->connection;
        gboolean headless = state->headless;
        g_autoptr (GVariant) reply = NULL;
        const char *dbus_path;
        const char *dbus_iface;

        if (headless) {
                dbus_path = GDM_REMOTE_DISPLAY_FACTORY_PATH;
                dbus_iface = GDM_REMOTE_DISPLAY_FACTORY_INTERFACE;
        } else {
                dbus_path = GDM_LOCAL_DISPLAY_FACTORY_PATH;
                dbus_iface = GDM_LOCAL_DISPLAY_FACTORY_INTERFACE;
        }

        reply = g_dbus_connection_call_sync (connection,
                                             GDM_DBUS_NAME,
                                             dbus_path,
                                             dbus_iface,
                                             method,
                                             g_variant_new ("(s)", state->username),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, NULL, error);
        if (reply == NULL) {
                g_dbus_error_strip_remote_error (*error);
                return FALSE;
        }

        return TRUE;
}

static gboolean
create_session (State   *state,
                GError **error)
{
        return call_gdm_display_factory_method (state,
                                                "CreateUserDisplay",
                                                error);
}

static gboolean
terminate_session (State   *state,
                   GError **error)
{
        return call_gdm_display_factory_method (state,
                                                "DestroyUserDisplay",
                                                error);
}

static gboolean
on_sigterm (State *state)
{
        g_autoptr (GError) error = NULL;

        // Got SIGTERM before the session is created,
        // wait for it to be created and then terminate it
        if (state->display_path == NULL) {
                state->terminate_requested = TRUE;
                return G_SOURCE_REMOVE;
        }

        if (!terminate_session (state, &error))
                g_warning ("Terminating session failed: %s", error->message);

        return G_SOURCE_REMOVE;
}

int
main (int   argc,
      char *argv[])
{
        g_autoptr (GOptionContext) context = NULL;
        g_autoptr (State) state = NULL;
        g_autoptr (GError) error = NULL;
        g_autofree char *username = NULL;
        gboolean headless = FALSE;

        GOptionEntry entries[] =
        {
                { "headless", 'h', 0, G_OPTION_ARG_NONE, &headless, "Run headless session", NULL },
                { NULL }
        };

        context = g_option_context_new ("USER - Run a graphical session as a specified user");
        g_option_context_add_main_entries (context, entries, NULL);

        if (!g_option_context_parse (context, &argc, &argv, &error)) {
                g_printerr ("Option parsing failed: %s\n", error->message);
                return EX_USAGE;
        }

        if (argc < 2) {
                g_printerr ("Username is required\n");
                return EX_USAGE;
        }

        username = g_strdup (argv[1]);

        state = g_new0 (State, 1);
        state->username = g_steal_pointer (&username);
        state->event_loop = g_main_loop_new (NULL, FALSE);
        state->headless = headless;

        if (!setup (state, &error)) {
                g_printerr ("Failed: %s\n", error->message);
                return EX_SOFTWARE;
        }

        if (!create_session (state, &error)) {
                g_printerr ("Starting session failed: %s\n", error->message);
                return EX_SOFTWARE;
        }

        g_unix_signal_add (SIGTERM, (GSourceFunc) on_sigterm, state);

        g_main_loop_run (state->event_loop);

        return EX_OK;
}
