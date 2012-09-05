/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gdm-log.h"
#include "gdm-common.h"
#include "gdm-signal-handler.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"
#include "gdm-profile.h"

#include "gdm-greeter-session.h"

#include "gsm-client-glue.h"
#include "gsm-manager-glue.h"

#define SM_DBUS_NAME      "org.gnome.SessionManager"
#define SM_DBUS_PATH      "/org/gnome/SessionManager"
#define SM_DBUS_INTERFACE "org.gnome.SessionManager"

#define SM_CLIENT_DBUS_INTERFACE "org.gnome.SessionManager.ClientPrivate"

static GDBusConnection  *bus_connection = NULL;
static GsmManager       *sm_proxy = NULL;
static char             *client_id = NULL;
static GsmClientPrivate *client_proxy = NULL;

static gboolean
is_debug_set (void)
{
        gboolean debug = FALSE;

        /* enable debugging for unstable builds */
        if (gdm_is_version_unstable ()) {
                return TRUE;
        }

        gdm_settings_client_get_boolean (GDM_KEY_DEBUG, &debug);
        return debug;
}


static gboolean
signal_cb (int      signo,
           gpointer data)
{
        int ret;

        g_debug ("Got callback for signal %d", signo);

        ret = TRUE;

        switch (signo) {
        case SIGFPE:
        case SIGPIPE:
                /* let the fatal signals interrupt us */
                g_debug ("Caught signal %d, shutting down abnormally.", signo);
                ret = FALSE;

                break;

        case SIGINT:
        case SIGTERM:
                /* let the fatal signals interrupt us */
                g_debug ("Caught signal %d, shutting down normally.", signo);
                ret = FALSE;

                break;

        case SIGHUP:
                g_debug ("Got HUP signal");
                /* FIXME:
                 * Reread config stuff like system config files, VPN service files, etc
                 */
                ret = TRUE;

                break;

        case SIGUSR1:
                g_debug ("Got USR1 signal");
                /* FIXME:
                 * Play with log levels or something
                 */
                ret = TRUE;

                gdm_log_toggle_debug ();

                break;

        default:
                g_debug ("Caught unhandled signal %d", signo);
                ret = TRUE;

                break;
        }

        return ret;
}

static gboolean
session_manager_connect (void)
{
        GError *error;

        error = NULL;

        if (bus_connection == NULL) {
                bus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
                if (bus_connection == NULL) {
                        g_message ("Failed to connect to the session bus: %s",
                                   error->message);
                        g_error_free (error);
                        exit (1);
                }

                g_signal_connect (G_OBJECT (bus_connection),
                                  "closed",
                                  G_CALLBACK (gtk_main_quit),
                                  NULL);
        }

        sm_proxy = gsm_manager_proxy_new_sync (bus_connection,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               SM_DBUS_NAME,
                                               SM_DBUS_PATH,
                                               NULL,
                                               &error);

        if (sm_proxy == NULL) {
                g_message ("Failed to connect to the session manager: %s",
                           error->message);
                g_error_free (error);
        }

        return (sm_proxy != NULL);
}

static void
stop_cb (GsmClientPrivate *client_private,
         gpointer          data)
{
        gtk_main_quit ();
}

static gboolean
end_session_response (gboolean is_okay, const gchar *reason)
{
        gboolean ret;
        GError *error = NULL;

        if (reason == NULL) {
                reason = "";
        }

        ret = gsm_client_private_call_end_session_response_sync (client_proxy, is_okay, reason, NULL, &error);

        if (!ret) {
                g_warning ("Failed to send session response %s", error->message);
                g_error_free (error);
        }

        return ret;
}

static void
query_end_session_cb (GsmClientPrivate *client_private,
                      guint             flags,
                      gpointer          data)
{
        end_session_response (TRUE, NULL);
}

static void
end_session_cb (guint flags, gpointer data)
{
        end_session_response (TRUE, NULL);
        gtk_main_quit ();
}

static gboolean
register_client (void)
{
        GError     *error;
        gboolean    res;
        const char *startup_id;
        const char *app_id;

        startup_id = g_getenv ("DESKTOP_AUTOSTART_ID");
        app_id = "gdm-simple-greeter.desktop";

        error = NULL;
        res = gsm_manager_call_register_client_sync (sm_proxy, app_id, startup_id, &client_id, NULL, &error);
        if (! res) {
                g_warning ("Failed to register client: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_debug ("Client registered with session manager: %s", client_id);
        client_proxy = gsm_client_private_proxy_new_sync (bus_connection,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          SM_DBUS_NAME,
                                                          client_id,
                                                          NULL,
                                                          &error);

        if (client_proxy == NULL) {
                g_warning ("Failed to track client: %s", error->message);
                g_error_free (error);

                return FALSE;
        }

        g_signal_connect (client_proxy,
                          "stop",
                          G_CALLBACK (stop_cb),
                          NULL);

        g_signal_connect (client_proxy,
                          "query-end-session",
                          G_CALLBACK (query_end_session_cb),
                          NULL);

        g_signal_connect (client_proxy,
                          "end-session",
                          G_CALLBACK (end_session_cb),
                          NULL);

        g_unsetenv ("DESKTOP_AUTOSTART_ID");

        return TRUE;
}

int
main (int argc, char *argv[])
{
        GError            *error;
        GdmGreeterSession *session;
        gboolean           res;
        GdmSignalHandler  *signal_handler;

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        g_type_init ();

        gdm_profile_start ("Initializing settings client");
        if (! gdm_settings_client_init (DATADIR "/gdm/gdm.schemas", "/")) {
                g_critical ("Unable to initialize settings client");
                exit (1);
        }
        gdm_profile_end ("Initializing settings client");

        g_debug ("Greeter session pid=%d display=%s xauthority=%s",
                 (int)getpid (),
                 g_getenv ("DISPLAY"),
                 g_getenv ("XAUTHORITY"));

        /* FIXME: For testing to make it easier to attach gdb */
        /*sleep (15);*/

        gdm_log_init ();
        gdm_log_set_debug (is_debug_set ());

        gtk_init (&argc, &argv);

        signal_handler = gdm_signal_handler_new ();
        gdm_signal_handler_add_fatal (signal_handler);
        gdm_signal_handler_add (signal_handler, SIGTERM, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGINT, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGFPE, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGHUP, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGUSR1, signal_cb, NULL);

        gdm_profile_start ("Creating new greeter session");
        session = gdm_greeter_session_new ();
        if (session == NULL) {
                g_critical ("Unable to create greeter session");
                exit (1);
        }
        gdm_profile_end ("Creating new greeter session");

        error = NULL;
        res = gdm_greeter_session_start (session, &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Unable to start greeter session: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        res = session_manager_connect ();
        if (! res) {
                g_warning ("Unable to connect to session manager");
                exit (1);
        }

        res = register_client ();
        if (! res) {
                g_warning ("Unable to register client with session manager");
        }

        gtk_main ();

        if (session != NULL) {
                g_object_unref (session);
        }

        return 0;
}
