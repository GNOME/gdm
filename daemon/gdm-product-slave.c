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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-product-slave.h"

#include "gdm-server.h"
#include "gdm-session-direct.h"
#include "gdm-session-glue.h"
#include "gdm-product-display-glue.h"

extern char **environ;

#define GDM_PRODUCT_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_PRODUCT_SLAVE, GdmProductSlavePrivate))

#define GDM_DBUS_NAME                      "org.gnome.DisplayManager"
#define GDM_DBUS_PRODUCT_DISPLAY_INTERFACE "org.gnome.DisplayManager.ProductDisplay"

#define RELAY_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/SessionRelay"
#define RELAY_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.SessionRelay"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmProductSlavePrivate
{
        char             *id;
        GPid              pid;

        char             *relay_address;

        GPid              server_pid;
        Display          *server_display;
        guint             connection_attempts;

        GdmServer        *server;
        GdmSessionDirect *session;
        GdmDBusSession   *session_relay;
        GDBusConnection  *session_connection;

        GdmDBusProductDisplay *product_display;
        GDBusConnection  *connection;

        char             *start_session_service_name;
};

static void     gdm_product_slave_class_init    (GdmProductSlaveClass *klass);
static void     gdm_product_slave_init          (GdmProductSlave      *product_slave);
static void     gdm_product_slave_finalize      (GObject             *object);

G_DEFINE_TYPE (GdmProductSlave, gdm_product_slave, GDM_TYPE_SLAVE)

static void
relay_session_started (GdmProductSlave *slave,
                       const char      *service_name,
                       int              pid)
{
        gdm_dbus_session_call_session_started_sync (slave->priv->session_relay,
                                                    service_name,
                                                    pid,
                                                    NULL, NULL);
}

static void
relay_session_conversation_started (GdmProductSlave *slave,
                                    const char      *service_name)
{
        gdm_dbus_session_call_conversation_started_sync (slave->priv->session_relay,
                                                         service_name,
                                                         NULL, NULL);
}

static void
on_session_conversation_started (GdmSessionDirect *session,
                                 const char       *service_name,
                                 GdmProductSlave  *slave)
{
        g_debug ("GdmProductSlave: session conversation started");

        relay_session_conversation_started (slave, service_name);
}

static void
disconnect_relay (GdmProductSlave *slave)
{
        /* drop the connection */

        g_dbus_connection_close_sync (slave->priv->session_connection, NULL, NULL);
        g_clear_object (&slave->priv->session_connection);
}

static void
on_session_started (GdmSessionDirect *session,
                    const char       *service_name,
                    int               pid,
                    GdmProductSlave  *slave)
{
        g_debug ("GdmProductSlave: session started");

        relay_session_started (slave, service_name, pid);

        disconnect_relay (slave);
}

static void
on_session_exited (GdmSessionDirect *session,
                   int               exit_code,
                   GdmProductSlave  *slave)
{
        g_debug ("GdmProductSlave: session exited with code %d", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_session_died (GdmSessionDirect *session,
                 int               signal_number,
                 GdmProductSlave  *slave)
{
        g_debug ("GdmProductSlave: session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
setup_server (GdmProductSlave *slave)
{
        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));
}

static gboolean
add_user_authorization (GdmProductSlave *slave,
                        char           **filename)
{
        char    *username;
        gboolean ret;

        username = gdm_session_direct_get_username (slave->priv->session);

        ret = gdm_slave_add_user_authorization (GDM_SLAVE (slave),
                                                username,
                                                filename);
        g_debug ("GdmProductSlave: Adding user authorization for %s: %s", username, *filename);

        g_free (username);

        return ret;
}

static gboolean
setup_session (GdmProductSlave *slave)
{
        char *auth_file;
        char *display_device;

        auth_file = NULL;
        add_user_authorization (slave, &auth_file);

        g_assert (auth_file != NULL);

        display_device = NULL;
        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        g_object_set (slave->priv->session,
                      "display-device", display_device,
                      "user-x11-authority-file", auth_file,
                      NULL);

        g_free (display_device);
        g_free (auth_file);

        gdm_session_direct_start_session (slave->priv->session,
                                          slave->priv->start_session_service_name);

        return TRUE;
}

static gboolean
idle_connect_to_display (GdmProductSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                /* FIXME: handle wait-for-go */

                setup_server (slave);
                setup_session (slave);
        } else {
                if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
                        exit (1);
                }
                return TRUE;
        }

        return FALSE;
}

static void
on_server_ready (GdmServer       *server,
                 GdmProductSlave *slave)
{
        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static void
on_server_exited (GdmServer       *server,
                  int              exit_code,
                  GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: server exited with code %d\n", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_server_died (GdmServer       *server,
                int              signal_number,
                GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: server died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static gboolean
gdm_product_slave_create_server (GdmProductSlave *slave)
{
        char    *display_name;
        char    *auth_file;
        char    *seat_id;
        gboolean display_is_local;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-seat-id", &seat_id,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        /* if this is local display start a server if one doesn't
         * exist */
        if (display_is_local) {
                gboolean res;

                slave->priv->server = gdm_server_new (display_name, seat_id, auth_file);
                g_signal_connect (slave->priv->server,
                                  "exited",
                                  G_CALLBACK (on_server_exited),
                                  slave);
                g_signal_connect (slave->priv->server,
                                  "died",
                                  G_CALLBACK (on_server_died),
                                  slave);
                g_signal_connect (slave->priv->server,
                                  "ready",
                                  G_CALLBACK (on_server_ready),
                                  slave);

                res = gdm_server_start (slave->priv->server);
                if (! res) {
                        g_warning (_("Could not start the X "
                                     "server (your graphical environment) "
                                     "due to an internal error. "
                                     "Please contact your system administrator "
                                     "or check your syslog to diagnose. "
                                     "In the meantime this display will be "
                                     "disabled.  Please restart GDM when "
                                     "the problem is corrected."));
                        exit (1);
                }

                g_debug ("GdmProductSlave: Started X server");
        } else {
                g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
        }

        g_free (display_name);
        g_free (auth_file);

        return TRUE;
}

static void
on_session_setup_complete (GdmSessionDirect *session,
                           const char       *service_name,
                           GdmProductSlave  *slave)
{
        gdm_dbus_session_call_setup_complete_sync (slave->priv->session_relay,
                                                   service_name,
                                                   NULL, NULL);
}

static void
on_session_setup_failed (GdmSessionDirect *session,
                         const char       *service_name,
                         const char       *message,
                         GdmProductSlave  *slave)
{
        gdm_dbus_session_call_setup_failed_sync (slave->priv->session_relay,
                                                 service_name,
                                                 message,
                                                 NULL, NULL);
}

static void
on_session_authenticated (GdmSessionDirect *session,
                          const char       *service_name,
                          GdmProductSlave  *slave)
{
        gdm_dbus_session_call_authenticated_sync (slave->priv->session_relay,
                                                  service_name,
                                                  NULL, NULL);
}

static void
on_session_authentication_failed (GdmSessionDirect *session,
                                  const char       *service_name,
                                  const char       *message,
                                  GdmProductSlave  *slave)
{
        gdm_dbus_session_call_authentication_failed_sync (slave->priv->session_relay,
                                                          service_name,
                                                          message,
                                                          NULL, NULL);
}

static void
on_session_authorized (GdmSessionDirect *session,
                       const char       *service_name,
                       GdmProductSlave  *slave)
{
        gdm_dbus_session_call_authorized_sync (slave->priv->session_relay,
                                               service_name,
                                               NULL, NULL);
}

static void
on_session_authorization_failed (GdmSessionDirect *session,
                                 const char       *service_name,
                                 const char       *message,
                                 GdmProductSlave  *slave)
{
        gdm_dbus_session_call_authorization_failed_sync (slave->priv->session_relay,
                                                         service_name,
                                                         message,
                                                         NULL, NULL);
}

static void
on_session_accredited (GdmSessionDirect *session,
                       const char       *service_name,
                       GdmProductSlave  *slave)
{
        gdm_dbus_session_call_accredited_sync (slave->priv->session_relay,
                                               service_name,
                                               NULL, NULL);
}

static void
on_session_accreditation_failed (GdmSessionDirect *session,
                                 const char       *service_name,
                                 const char       *message,
                                 GdmProductSlave  *slave)
{
        gdm_dbus_session_call_accreditation_failed_sync (slave->priv->session_relay,
                                                         service_name,
                                                         message,
                                                         NULL, NULL);
}

static void
on_session_opened (GdmSessionDirect *session,
                   const char       *service_name,
                   GdmProductSlave  *slave)
{
        gdm_dbus_session_call_opened_sync (slave->priv->session_relay,
                                           service_name,
                                           NULL, NULL);
}

static void
on_session_open_failed (GdmSessionDirect *session,
                        const char       *service_name,
                        const char       *message,
                        GdmProductSlave  *slave)
{
        gdm_dbus_session_call_open_failed_sync (slave->priv->session_relay,
                                                service_name,
                                                message,
                                                NULL, NULL);
}

static void
on_session_info (GdmSessionDirect *session,
                 const char       *service_name,
                 const char       *text,
                 GdmProductSlave  *slave)
{
        gdm_dbus_session_call_info_sync (slave->priv->session_relay,
                                         service_name,
                                         text,
                                         NULL, NULL);
}

static void
on_session_problem (GdmSessionDirect *session,
                    const char       *service_name,
                    const char       *text,
                    GdmProductSlave  *slave)
{
        gdm_dbus_session_call_problem_sync (slave->priv->session_relay,
                                            service_name,
                                            text,
                                            NULL, NULL);
}

typedef struct {
        GdmProductSlave *slave;
        char *service_name;
} QueryClosure;

static void
on_session_query_finish (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
        GVariant *res;
        const char *text;
        QueryClosure *closure;

        closure = user_data;

        res = g_dbus_proxy_call_finish (G_DBUS_PROXY (object),
                                        result, NULL);
        if (!res)
                return;

        g_variant_get (res, "(&s)", &text);

        gdm_session_direct_answer_query (closure->slave->priv->session,
                                         closure->service_name, text);

        g_object_unref (closure->slave);
        g_free (closure->service_name);
        g_slice_free (QueryClosure, closure);
}

static void
on_session_info_query (GdmSessionDirect *session,
                       const char       *service_name,
                       const char       *text,
                       GdmProductSlave  *slave)
{
        QueryClosure *closure;

        closure = g_slice_new (QueryClosure);
        closure->slave = g_object_ref (slave);
        closure->service_name = g_strdup (service_name);

        gdm_dbus_session_call_info_query (slave->priv->session_relay,
                                          service_name,
                                          text,
                                          NULL,
                                          on_session_query_finish, closure);
}

static void
on_session_secret_info_query (GdmSessionDirect *session,
                              const char       *service_name,
                              const char       *text,
                              GdmProductSlave  *slave)
{
        QueryClosure *closure;

        closure = g_slice_new (QueryClosure);
        closure->slave = g_object_ref (slave);
        closure->service_name = g_strdup (service_name);

        gdm_dbus_session_call_secret_info_query (slave->priv->session_relay,
                                                 service_name,
                                                 text,
                                                 NULL,
                                                 on_session_query_finish, closure);
}

static void
on_relay_setup (GdmDBusSession  *session_relay,
                const gchar     *service_name,
                const gchar     *x11_display_name,
                const gchar     *x11_authority_file,
                const gchar     *display_device,
                const gchar     *display_seat,
                const gchar     *hostname,
                GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Setup");
        gdm_session_direct_setup (slave->priv->session, service_name);
}

static void
on_relay_setup_for_user (GdmDBusSession  *session_relay,
                         const gchar     *service_name,
                         const gchar     *user_name,
                         const gchar     *x11_display_name,
                         const gchar     *x11_authority_file,
                         const gchar     *display_device,
                         const gchar     *display_seat,
                         const gchar     *hostname,
                         GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay SetupForUser");
        gdm_session_direct_setup_for_user (slave->priv->session,
                                           service_name, user_name);
}

static void
on_relay_authenticate (GdmDBusSession  *session_relay,
                       const gchar     *service_name,
                       GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Authenticate");
        gdm_session_direct_authenticate (slave->priv->session, service_name);
}

static void
on_relay_authorize (GdmDBusSession  *session_relay,
                    const gchar     *service_name,
                    GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Authorize");
        gdm_session_direct_authorize (slave->priv->session, service_name);
}

static void
on_relay_establish_credentials (GdmDBusSession  *session_relay,
                                const gchar     *service_name,
                                GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Establish Credentials");
        gdm_session_direct_accredit (slave->priv->session, service_name, FALSE);
}

static void
on_relay_refresh_credentials (GdmDBusSession  *session_relay,
                              const gchar     *service_name,
                              GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Refresh Credentials");
        gdm_session_direct_accredit (slave->priv->session, service_name, TRUE);
}

static void
on_relay_set_session_name (GdmDBusSession  *session_relay,
                           const gchar     *session_name,
                           GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Set Session Name");
        gdm_session_direct_select_session (slave->priv->session, session_name);
}

static void
on_relay_set_language_name (GdmDBusSession  *session_relay,
                            const gchar     *language_name,
                            GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Set Language Name");
        gdm_session_direct_select_language (slave->priv->session, language_name);
}

static void
on_relay_set_user_name (GdmDBusSession  *session_relay,
                        const gchar     *user_name,
                        GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay Set User Name");
        gdm_session_direct_select_user (slave->priv->session, user_name);
}

static void
on_relay_start_conversation (GdmDBusSession  *session_relay,
                             const gchar     *service_name,
                             GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Started Conversation with %s", service_name);
        gdm_session_direct_start_conversation (slave->priv->session, service_name);
}

static void
on_relay_open_session (GdmDBusSession  *session_relay,
                       const gchar     *service_name,
                       GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: open session for %s", service_name);
        gdm_session_direct_open_session (slave->priv->session, service_name);
}

static void
on_relay_start_session (GdmDBusSession  *session_relay,
                        const gchar     *service_name,
                        GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay StartSession");

        g_free (slave->priv->start_session_service_name);
        slave->priv->start_session_service_name = g_strdup (service_name);
        gdm_product_slave_create_server (slave);
}

static void
create_new_session (GdmProductSlave *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *display_hostname;
        char          *display_device;
        char          *display_seat_id;
        char          *display_x11_authority_file;

        g_debug ("GdmProductSlave: Creating new session");

        g_object_get (slave,
                      "display-id", &display_id,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-is-local", &display_is_local,
                      "display-x11-authority-file", &display_x11_authority_file,
                      "display-seat-id", &display_seat_id,
                      NULL);

        /* FIXME: we don't yet have a display device! */
        display_device = g_strdup ("");

        slave->priv->session = gdm_session_direct_new (display_id,
                                                       display_name,
                                                       display_hostname,
                                                       display_device,
                                                       display_seat_id,
                                                       display_x11_authority_file,
                                                       display_is_local);
        g_free (display_id);
        g_free (display_name);
        g_free (display_hostname);
        g_free (display_x11_authority_file);
        g_free (display_device);

        g_signal_connect (slave->priv->session,
                          "conversation-started",
                          G_CALLBACK (on_session_conversation_started),
                          slave);
        g_signal_connect (slave->priv->session,
                          "setup-complete",
                          G_CALLBACK (on_session_setup_complete),
                          slave);
        g_signal_connect (slave->priv->session,
                          "setup-failed",
                          G_CALLBACK (on_session_setup_failed),
                          slave);
        g_signal_connect (slave->priv->session,
                          "authenticated",
                          G_CALLBACK (on_session_authenticated),
                          slave);
        g_signal_connect (slave->priv->session,
                          "authentication-failed",
                          G_CALLBACK (on_session_authentication_failed),
                          slave);
        g_signal_connect (slave->priv->session,
                          "authorized",
                          G_CALLBACK (on_session_authorized),
                          slave);
        g_signal_connect (slave->priv->session,
                          "authorization-failed",
                          G_CALLBACK (on_session_authorization_failed),
                          slave);
        g_signal_connect (slave->priv->session,
                          "accredited",
                          G_CALLBACK (on_session_accredited),
                          slave);
        g_signal_connect (slave->priv->session,
                          "accreditation-failed",
                          G_CALLBACK (on_session_accreditation_failed),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-opened",
                          G_CALLBACK (on_session_opened),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-open-failed",
                          G_CALLBACK (on_session_open_failed),
                          slave);
        g_signal_connect (slave->priv->session,
                          "info",
                          G_CALLBACK (on_session_info),
                          slave);
        g_signal_connect (slave->priv->session,
                          "problem",
                          G_CALLBACK (on_session_problem),
                          slave);
        g_signal_connect (slave->priv->session,
                          "info-query",
                          G_CALLBACK (on_session_info_query),
                          slave);
        g_signal_connect (slave->priv->session,
                          "secret-info-query",
                          G_CALLBACK (on_session_secret_info_query),
                          slave);

        g_signal_connect (slave->priv->session,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-died",
                          G_CALLBACK (on_session_died),
                          slave);
}

static void
on_relay_cancelled (GdmDBusSession  *session_relay,
                    GdmProductSlave *slave)
{
        g_debug ("GdmProductSlave: Relay cancelled");

        if (slave->priv->session != NULL) {
                gdm_session_direct_close (slave->priv->session);
                g_object_unref (slave->priv->session);
        }

        create_new_session (slave);
}

static void
get_relay_address (GdmProductSlave *slave)
{
        GError  *error;
        char    *text;
        gboolean res;

        text = NULL;
        error = NULL;
        res = gdm_dbus_product_display_call_get_relay_address_sync (slave->priv->product_display,
                                                                    &text,
                                                                    NULL, &error);

        if (! res) {
                g_warning ("Unable to get relay address: %s", error->message);
                g_error_free (error);
        } else {
                g_free (slave->priv->relay_address);
                slave->priv->relay_address = text;
                g_debug ("GdmProductSlave: Got relay address: %s", slave->priv->relay_address);
        }
}

static gboolean
connect_to_session_relay (GdmProductSlave *slave)
{
        GError *error = NULL;

        get_relay_address (slave);

        g_debug ("GdmProductSlave: connecting to session relay address: %s", slave->priv->relay_address);

        slave->priv->session_connection = g_dbus_connection_new_for_address_sync (slave->priv->relay_address,
                                                                                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                                  NULL, NULL, &error);
        if (!slave->priv->session_connection) {
                g_warning ("Unable to connect to session relay: %s", error->message);

                g_clear_error (&error);
                return FALSE;
        }

        slave->priv->session_relay = GDM_DBUS_SESSION (gdm_dbus_session_proxy_new_sync (slave->priv->session_connection,
                                                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                                                        NULL, /* dbus name */
                                                                                        "/org/gnome/DisplayManager/Session",
                                                                                        NULL, &error));
        if (!slave->priv->session_relay) {
                g_warning ("Unable to construct session relay: %s", error->message);

                g_clear_object (&slave->priv->session_connection);
                g_clear_error (&error);
                return FALSE;
        }

        g_signal_connect_object (slave->priv->session_relay, "setup",
                                 G_CALLBACK (on_relay_setup), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "setup-for-user",
                                 G_CALLBACK (on_relay_setup_for_user), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "authenticate",
                                 G_CALLBACK (on_relay_authenticate), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "authorize",
                                 G_CALLBACK (on_relay_authorize), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "establish-credentials",
                                 G_CALLBACK (on_relay_establish_credentials), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "refresh-credentials",
                                 G_CALLBACK (on_relay_refresh_credentials), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "set-language-name",
                                 G_CALLBACK (on_relay_set_language_name), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "set-session-name",
                                 G_CALLBACK (on_relay_set_session_name), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "set-user-name",
                                 G_CALLBACK (on_relay_set_user_name), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "start-conversation",
                                 G_CALLBACK (on_relay_start_conversation), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "open-session",
                                 G_CALLBACK (on_relay_open_session), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "start-session",
                                 G_CALLBACK (on_relay_start_session), slave, 0);
        g_signal_connect_object (slave->priv->session_relay, "cancelled",
                                 G_CALLBACK (on_relay_cancelled), slave, 0);

        return TRUE;
}

static gboolean
gdm_product_slave_start (GdmSlave *slave)
{
        GdmProductSlave *self;
        gboolean ret;
        GError  *error;
        char    *display_id;

        ret = FALSE;
        self = GDM_PRODUCT_SLAVE (slave);

        GDM_SLAVE_CLASS (gdm_product_slave_parent_class)->start (slave);

        g_object_get (slave,
                      "display-id", &display_id,
                      NULL);

        error = NULL;
        self->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (self->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        error = NULL;
        self->priv->product_display = GDM_DBUS_PRODUCT_DISPLAY (
                gdm_dbus_product_display_proxy_new_sync (self->priv->connection,
                                                         G_DBUS_PROXY_FLAGS_NONE,
                                                         GDM_DBUS_NAME,
                                                         display_id,
                                                         NULL, &error));
        if (self->priv->product_display == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to create display proxy %s: %s", display_id, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to create display proxy");
                }
                goto out;
        }

        create_new_session (self);
        connect_to_session_relay (self);

        ret = TRUE;

 out:
        g_free (display_id);

        return ret;
}

static gboolean
gdm_product_slave_stop (GdmSlave *slave)
{
        GdmProductSlave *self = GDM_PRODUCT_SLAVE (slave);

        g_debug ("GdmProductSlave: Stopping product_slave");

        GDM_SLAVE_CLASS (gdm_product_slave_parent_class)->stop (slave);

        if (self->priv->session != NULL) {
                gdm_session_direct_close (self->priv->session);
                g_clear_object (&self->priv->session);
        }

        if (self->priv->server != NULL) {
                gdm_server_stop (self->priv->server);
                g_clear_object (&self->priv->server);
        }

        g_clear_object (&self->priv->product_display);
        g_clear_object (&self->priv->session_connection);
        g_clear_object (&self->priv->session_relay);

        return TRUE;
}

static void
gdm_product_slave_class_init (GdmProductSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->finalize = gdm_product_slave_finalize;

        slave_class->start = gdm_product_slave_start;
        slave_class->stop = gdm_product_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmProductSlavePrivate));
}

static void
gdm_product_slave_init (GdmProductSlave *slave)
{
        slave->priv = GDM_PRODUCT_SLAVE_GET_PRIVATE (slave);
}

static void
gdm_product_slave_finalize (GObject *object)
{
        GdmProductSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_PRODUCT_SLAVE (object));

        slave = GDM_PRODUCT_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        gdm_product_slave_stop (GDM_SLAVE (slave));

        g_free (slave->priv->relay_address);

        G_OBJECT_CLASS (gdm_product_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_product_slave_new (const char *id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_PRODUCT_SLAVE,
                               "display-id", id,
                               NULL);

        return GDM_SLAVE (object);
}
