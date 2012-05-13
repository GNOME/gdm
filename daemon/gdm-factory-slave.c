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

#include "gdm-factory-slave.h"

#include "gdm-server.h"
#include "gdm-greeter-session.h"
#include "gdm-greeter-server.h"

#include "gdm-session-glue.h"
#include "gdm-local-display-factory-glue.h"
#include "gdm-dbus-util.h"

extern char **environ;

#define GDM_FACTORY_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_FACTORY_SLAVE, GdmFactorySlavePrivate))

#define GDM_DBUS_NAME                            "org.gnome.DisplayManager"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.LocalDisplayFactory"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmFactorySlavePrivate
{
        char              *id;
        GPid               pid;
        guint              greeter_reset_id;

        GPid               server_pid;
        Display           *server_display;
        guint              connection_attempts;

        GHashTable        *pending_queries;

        GdmDBusSession    *session;
        GDBusServer       *session_server;

        GdmServer         *server;
        GdmGreeterServer  *greeter_server;
        GdmGreeterSession *greeter;
        GdmDBusLocalDisplayFactory *factory_proxy;
        GDBusConnection            *connection;
};

static void     gdm_factory_slave_class_init    (GdmFactorySlaveClass *klass);
static void     gdm_factory_slave_init          (GdmFactorySlave      *factory_slave);
static void     gdm_factory_slave_finalize      (GObject             *object);

G_DEFINE_TYPE (GdmFactorySlave, gdm_factory_slave, GDM_TYPE_SLAVE)

static gboolean
greeter_reset_timeout (GdmFactorySlave *slave)
{
        gdm_greeter_server_reset (slave->priv->greeter_server);
        slave->priv->greeter_reset_id = 0;
        return FALSE;
}

static void
queue_greeter_reset (GdmFactorySlave *slave)
{
        if (slave->priv->greeter_reset_id > 0) {
                return;
        }

        slave->priv->greeter_reset_id = g_timeout_add_seconds (2, (GSourceFunc)greeter_reset_timeout, slave);
}

static void
on_greeter_session_start (GdmGreeterSession *greeter,
                          GdmFactorySlave   *slave)
{
        g_debug ("GdmFactorySlave: Greeter started");
}

static void
on_greeter_session_stop (GdmGreeterSession *greeter,
                         GdmFactorySlave   *slave)
{
        g_debug ("GdmFactorySlave: Greeter stopped");

        g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->greeter);
        GDM_FACTORY_SLAVE (slave)->priv->greeter = NULL;
}

static void
on_greeter_session_exited (GdmGreeterSession    *greeter,
                           int                   code,
                           GdmFactorySlave      *slave)
{
        g_debug ("GdmSimpleSlave: Greeter exited: %d", code);
        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_greeter_session_died (GdmGreeterSession    *greeter,
                         int                   signal,
                         GdmFactorySlave      *slave)
{
        g_debug ("GdmSimpleSlave: Greeter died: %d", signal);
        gdm_slave_stopped (GDM_SLAVE (slave));
}


static gboolean
on_session_handle_info (GdmDBusSession        *session,
                        GDBusMethodInvocation *invocation,
                        const char            *service_name,
                        const char            *text,
                        GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: Info: %s", text);
        gdm_greeter_server_info (slave->priv->greeter_server, service_name, text);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_problem (GdmDBusSession        *session,
                           GDBusMethodInvocation *invocation,
                           const char            *service_name,
                           const char            *text,
                           GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: Problem: %s", text);
        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, text);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_info_query (GdmDBusSession        *session,
                              GDBusMethodInvocation *invocation,
                              const char            *service_name,
                              const char            *text,
                              GdmFactorySlave       *slave)
{
        GDBusMethodInvocation *previous;

        g_debug ("GdmFactorySlave: Info query: %s", text);

        if ((previous = g_hash_table_lookup (slave->priv->pending_queries, service_name))) {
                g_dbus_method_invocation_return_dbus_error (invocation,
                                                            "org.gnome.DisplayManager.SessionRelayCancelled",
                                                            "Cancelled by another request for the same service");
        }

        g_hash_table_replace (slave->priv->pending_queries, g_strdup (service_name), g_object_ref (invocation));
        gdm_greeter_server_info_query (slave->priv->greeter_server, service_name, text);

        return TRUE;
}

static gboolean
on_session_handle_secret_info_query (GdmDBusSession        *session,
                                     GDBusMethodInvocation *invocation,
                                     const char            *service_name,
                                     const char            *text,
                                     GdmFactorySlave       *slave)
{
        GDBusMethodInvocation *previous;

        g_debug ("GdmFactorySlave: Secret info query: %s", text);

        if ((previous = g_hash_table_lookup (slave->priv->pending_queries, service_name))) {
                g_dbus_method_invocation_return_dbus_error (invocation,
                                                            "org.gnome.DisplayManager.SessionRelayCancelled",
                                                            "Cancelled by another request for the same service");
        }

        g_hash_table_replace (slave->priv->pending_queries, g_strdup (service_name), g_object_ref (invocation));
        gdm_greeter_server_secret_info_query (slave->priv->greeter_server, service_name, text);

        return TRUE;
}

static gboolean
on_session_handle_conversation_started (GdmDBusSession        *session,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: session conversation started");

        gdm_greeter_server_ready (slave->priv->greeter_server,
                                  service_name);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_setup_complete (GdmDBusSession        *session,
                                  GDBusMethodInvocation *invocation,
                                  const char            *service_name,
                                  GdmFactorySlave       *slave)
{
        gdm_dbus_session_emit_authenticate (session, service_name);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_setup_failed (GdmDBusSession        *session,
                                GDBusMethodInvocation *invocation,
                                const char            *service_name,
                                const char            *message,
                                GdmFactorySlave       *slave)
{
        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, _("Unable to initialize login system"));

        queue_greeter_reset (slave);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_authenticated (GdmDBusSession        *session,
                                 GDBusMethodInvocation *invocation,
                                 const char            *service_name,
                                 GdmFactorySlave       *slave)
{
        gdm_dbus_session_emit_authorize (session, service_name);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_authentication_failed (GdmDBusSession        *session,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         const char            *message,
                                         GdmFactorySlave       *slave)
{
        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, _("Unable to authenticate user"));

        queue_greeter_reset (slave);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_authorized (GdmDBusSession        *session,
                              GDBusMethodInvocation *invocation,
                              const char            *service_name,
                              GdmFactorySlave       *slave)
{
        /* FIXME: check for migration? */
        gdm_dbus_session_emit_establish_credentials (session, service_name);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_authorization_failed (GdmDBusSession        *session,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        const char            *message,
                                        GdmFactorySlave       *slave)
{
        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, _("Unable to authorize user"));

        queue_greeter_reset (slave);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_accredited (GdmDBusSession        *session,
                              GDBusMethodInvocation *invocation,
                              const char            *service_name,
                              GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave:  session user verified");

        gdm_dbus_session_emit_open_session (session, service_name);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_accreditation_failed (GdmDBusSession        *session,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        const char            *message,
                                        GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: could not successfully authenticate user: %s",
                 message);

        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, _("Unable to establish credentials"));

        queue_greeter_reset (slave);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_opened (GdmDBusSession        *session,
                          GDBusMethodInvocation *invocation,
                          const char            *service_name,
                          GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: session opened");

        gdm_dbus_session_emit_start_session (session, service_name);

        gdm_greeter_server_reset (slave->priv->greeter_server);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_open_failed (GdmDBusSession        *session,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               const char            *message,
                               GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: could not open session: %s", message);

        gdm_greeter_server_problem (slave->priv->greeter_server, service_name, _("Unable to open session"));

        queue_greeter_reset (slave);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
on_session_handle_session_started (GdmDBusSession        *session,
                                   GDBusMethodInvocation *invocation,
                                   const char            *service_name,
                                   int                    pid,
                                   GdmFactorySlave       *slave)
{
        g_debug ("GdmFactorySlave: Relay session started");

        gdm_greeter_server_reset (slave->priv->greeter_server);

        g_dbus_method_invocation_return_value (invocation, NULL);
        return TRUE;
}

static gboolean
create_product_display (GdmFactorySlave *slave)
{
        char    *parent_display_id;
        const char *server_address;
        char    *product_id;
        GError  *error = NULL;
        gboolean res;
        gboolean ret;

        ret = FALSE;

        g_debug ("GdmFactorySlave: Create product display");

        g_debug ("GdmFactorySlave: Connecting to local display factory");
        slave->priv->factory_proxy = GDM_DBUS_LOCAL_DISPLAY_FACTORY (
                gdm_dbus_local_display_factory_proxy_new_sync (slave->priv->connection,
                                                            G_DBUS_PROXY_FLAGS_NONE,
                                                            GDM_DBUS_NAME,
                                                            GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH,
                                                            NULL, &error));
        if (slave->priv->factory_proxy == NULL) {
                g_warning ("Failed to create local display factory proxy: %s", error->message);
                g_error_free (error);
                goto out;
        }

        server_address = g_dbus_server_get_client_address (slave->priv->session_server);

        g_object_get (slave,
                      "display-id", &parent_display_id,
                      NULL);

        res = gdm_dbus_local_display_factory_call_create_product_display_sync (slave->priv->factory_proxy,
                                                                               parent_display_id,
                                                                               server_address,
                                                                               &product_id,
                                                                               NULL, &error);
        g_free (parent_display_id);

        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to create product display: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to create product display");
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
on_session_connection_closed (GDBusConnection *connection,
                              gboolean         remote_peer_vanished,
                              GdmFactorySlave *slave)
{
        g_debug ("GdmFactorySlave: Relay disconnected");

        /* FIXME: do some kind of loop detection */
        gdm_greeter_server_reset (slave->priv->greeter_server);
        create_product_display (slave);

        /* pair the reference added in handle_new_connection */
        g_object_unref (connection);
}

static void
on_greeter_start_conversation (GdmGreeterServer *greeter_server,
                               const char       *service_name,
                               GdmFactorySlave  *slave)
{
        g_debug ("GdmFactorySlave: start conversation");

        gdm_dbus_session_emit_start_conversation (slave->priv->session, service_name);
}

static void
on_greeter_begin_verification (GdmGreeterServer *greeter_server,
                               const char       *service_name,
                               GdmFactorySlave  *slave)
{
        g_debug ("GdmFactorySlave: begin verification");
        gdm_dbus_session_emit_setup (slave->priv->session,
                                     service_name, "", "", "", "", "");
}

static void
on_greeter_begin_verification_for_user (GdmGreeterServer *greeter_server,
                                        const char       *service_name,
                                        const char       *username,
                                        GdmFactorySlave  *slave)
{
        g_debug ("GdmFactorySlave: begin verification for user");
        gdm_dbus_session_emit_setup_for_user (slave->priv->session,
                                              service_name, username,
                                              "", "", "", "", "");
}

static void
on_greeter_answer (GdmGreeterServer *greeter_server,
                   const char       *service_name,
                   const char       *text,
                   GdmFactorySlave  *slave)
{
        GDBusMethodInvocation *invocation;

        g_debug ("GdmFactorySlave: Greeter answer");

        invocation = g_hash_table_lookup (slave->priv->pending_queries, service_name);
        g_dbus_method_invocation_return_value (invocation,
                                               g_variant_new ("(s)", text));

        g_hash_table_remove (slave->priv->pending_queries, service_name);
}

static void
on_greeter_session_selected (GdmGreeterServer *greeter_server,
                             const char       *text,
                             GdmFactorySlave  *slave)
{
        gdm_dbus_session_emit_set_session_name (slave->priv->session, text);
}

static void
on_greeter_language_selected (GdmGreeterServer *greeter_server,
                              const char       *text,
                              GdmFactorySlave  *slave)
{
        gdm_dbus_session_emit_set_language_name (slave->priv->session, text);
}

static void
on_greeter_user_selected (GdmGreeterServer *greeter_server,
                          const char       *text,
                          GdmFactorySlave  *slave)
{
        gdm_dbus_session_emit_set_user_name (slave->priv->session, text);
}

static void
on_greeter_cancel (GdmGreeterServer *greeter_server,
                   GdmFactorySlave  *slave)
{
        gdm_dbus_session_emit_cancelled (slave->priv->session);
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
                      GdmFactorySlave  *slave)
{
        g_debug ("GdmFactorySlave: Greeter started");

        create_product_display (slave);
}

static void
setup_server (GdmFactorySlave *slave)
{
        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));
}

static void
run_greeter (GdmFactorySlave *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *seat_id;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        char          *address;

        g_debug ("GdmFactorySlave: Running greeter");

        display_is_local = FALSE;
        display_id = NULL;
        display_name = NULL;
        seat_id = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-id", &display_id,
                      "display-name", &display_name,
                      "display-seat-id", &seat_id,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        /* FIXME: send a signal back to the master */

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", GDM_USERNAME);

        slave->priv->greeter_server = gdm_greeter_server_new (display_id);
        g_signal_connect (slave->priv->greeter_server,
                          "start-conversation",
                          G_CALLBACK (on_greeter_start_conversation),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "begin-verification",
                          G_CALLBACK (on_greeter_begin_verification),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "begin-verification-for-user",
                          G_CALLBACK (on_greeter_begin_verification_for_user),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "query-answer",
                          G_CALLBACK (on_greeter_answer),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "session-selected",
                          G_CALLBACK (on_greeter_session_selected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "language-selected",
                          G_CALLBACK (on_greeter_language_selected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "user-selected",
                          G_CALLBACK (on_greeter_user_selected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "connected",
                          G_CALLBACK (on_greeter_connected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "cancelled",
                          G_CALLBACK (on_greeter_cancel),
                          slave);
        gdm_greeter_server_start (slave->priv->greeter_server);

        address = gdm_greeter_server_get_address (slave->priv->greeter_server);

        g_debug ("GdmFactorySlave: Creating greeter on %s %s", display_name, display_device);
        slave->priv->greeter = gdm_greeter_session_new (display_name,
                                                        seat_id,
                                                        display_device,
                                                        display_hostname,
                                                        display_is_local);
        g_signal_connect (slave->priv->greeter,
                          "started",
                          G_CALLBACK (on_greeter_session_start),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "stopped",
                          G_CALLBACK (on_greeter_session_stop),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "exited",
                          G_CALLBACK (on_greeter_session_exited),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "died",
                          G_CALLBACK (on_greeter_session_died),
                          slave);
        g_object_set (slave->priv->greeter,
                      "x11-authority-file", auth_file,
                      NULL);
        gdm_welcome_session_set_server_address (GDM_WELCOME_SESSION (slave->priv->greeter), address);
        gdm_welcome_session_start (GDM_WELCOME_SESSION (slave->priv->greeter));

        g_free (address);

        g_free (display_id);
        g_free (display_name);
        g_free (seat_id);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmFactorySlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        g_debug ("GdmFactorySlave: Connect to display");

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                /* FIXME: handle wait-for-go */

                setup_server (slave);
                run_greeter (slave);
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
                 GdmFactorySlave *slave)
{
        g_debug ("GdmFactorySlave: Server ready");

        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static void
on_server_exited (GdmServer       *server,
                  int              exit_code,
                  GdmFactorySlave *slave)
{
        g_debug ("GdmFactorySlave: server exited with code %d\n", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_server_died (GdmServer       *server,
                int              signal_number,
                GdmFactorySlave *slave)
{
        g_debug ("GdmFactorySlave: server died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static gboolean
gdm_factory_slave_run (GdmFactorySlave *slave)
{
        char    *display_name;
        char    *seat_id;
        char    *auth_file;
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

                g_debug ("GdmFactorySlave: Started X server");
        } else {
                g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
        }

        g_free (display_name);
        g_free (auth_file);

        return TRUE;
}

static gboolean
handle_new_connection (GDBusServer     *server,
                       GDBusConnection *connection,
                       GdmFactorySlave *slave)
{
        g_object_ref (connection);

        slave->priv->session = GDM_DBUS_SESSION (gdm_dbus_session_skeleton_new ());

        g_signal_connect_object (slave->priv->session,
                                 "handle-conversation-started",
                                 G_CALLBACK (on_session_handle_conversation_started),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-setup-complete",
                                 G_CALLBACK (on_session_handle_setup_complete),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-setup-failed",
                                 G_CALLBACK (on_session_handle_setup_failed),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-authenticated",
                                 G_CALLBACK (on_session_handle_authenticated),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-authentication-failed",
                                 G_CALLBACK (on_session_handle_authentication_failed),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-authorized",
                                 G_CALLBACK (on_session_handle_authorized),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-authorization-failed",
                                 G_CALLBACK (on_session_handle_authorization_failed),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-accredited",
                                 G_CALLBACK (on_session_handle_accredited),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-accreditation-failed",
                                 G_CALLBACK (on_session_handle_accreditation_failed),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-session-opened",
                                 G_CALLBACK (on_session_handle_opened),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-session-open-failed",
                                 G_CALLBACK (on_session_handle_open_failed),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-info",
                                 G_CALLBACK (on_session_handle_info),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-problem",
                                 G_CALLBACK (on_session_handle_problem),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-info-query",
                                 G_CALLBACK (on_session_handle_info_query),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-secret-info-query",
                                 G_CALLBACK (on_session_handle_secret_info_query),
                                 slave, 0);
        g_signal_connect_object (slave->priv->session,
                                 "handle-session-started",
                                 G_CALLBACK (on_session_handle_session_started),
                                 slave, 0);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (slave->priv->session),
                                          connection,
                                          "/org/gnome/DisplayManager/Session",
                                          NULL);

        g_signal_connect_object (connection, "closed",
                                 G_CALLBACK (on_session_connection_closed), slave, 0);

        return TRUE;
}

static gboolean
gdm_factory_slave_start (GdmSlave *slave)
{
        GdmFactorySlave *self;
        GError *error = NULL;
        gboolean ret;

        ret = FALSE;

        self = GDM_FACTORY_SLAVE (slave);

        g_debug ("GdmFactorySlave: Starting factory slave");

        GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->start (slave);

        self->priv->pending_queries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                              g_free, g_object_unref);

        self->priv->session_server = gdm_dbus_setup_private_server (NULL,
                                                                      &error);

        if (self->priv->session_server == NULL) {
                g_critical ("Failed to setup private DBus server: %s", error->message);
                return FALSE;
        }
        g_signal_connect_object (self->priv->session_server, "new-connection",
                                 G_CALLBACK (handle_new_connection), slave, 0);

        g_dbus_server_start (self->priv->session_server);

        gdm_factory_slave_run (self);

        ret = TRUE;

        return ret;
}

static gboolean
gdm_factory_slave_stop (GdmSlave *slave)
{
        GdmFactorySlave *self = GDM_FACTORY_SLAVE (slave);

        g_debug ("GdmFactorySlave: Stopping factory_slave");

        GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->stop (slave);

        if (self->priv->session != NULL) {
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->priv->session));
                g_clear_object (&self->priv->session);
        }

        if (self->priv->session_server != NULL) {
                g_dbus_server_stop (self->priv->session_server);
                g_clear_object (&self->priv->session_server);
        }

        if (self->priv->greeter_server != NULL) {
                gdm_greeter_server_stop (self->priv->greeter_server);
                g_clear_object (&self->priv->greeter_server);
        }

        if (self->priv->greeter != NULL) {
                gdm_welcome_session_stop (GDM_WELCOME_SESSION (self->priv->greeter));
        }

        if (self->priv->server != NULL) {
                gdm_server_stop (self->priv->server);
                g_clear_object (&self->priv->server);
        }

        g_clear_object (&self->priv->factory_proxy);

        if (self->priv->pending_queries) {
                g_hash_table_unref (self->priv->pending_queries);
                self->priv->pending_queries = NULL;
        }

        return TRUE;
}

static void
gdm_factory_slave_class_init (GdmFactorySlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->finalize = gdm_factory_slave_finalize;

        slave_class->start = gdm_factory_slave_start;
        slave_class->stop = gdm_factory_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmFactorySlavePrivate));
}

static void
gdm_factory_slave_init (GdmFactorySlave *slave)
{
        GError *error;

        slave->priv = GDM_FACTORY_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;

        error = NULL;
        slave->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (slave->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }
}

static void
gdm_factory_slave_finalize (GObject *object)
{
        GdmFactorySlave *factory_slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_FACTORY_SLAVE (object));

        factory_slave = GDM_FACTORY_SLAVE (object);

        g_debug ("GdmFactorySlave: Finalizing slave");

        g_return_if_fail (factory_slave->priv != NULL);

        gdm_factory_slave_stop (GDM_SLAVE (factory_slave));

        if (factory_slave->priv->greeter_reset_id > 0) {
                g_source_remove (factory_slave->priv->greeter_reset_id);
                factory_slave->priv->greeter_reset_id = 0;
        }

        G_OBJECT_CLASS (gdm_factory_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_factory_slave_new (const char *id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_FACTORY_SLAVE,
                               "display-id", id,
                               NULL);

        return GDM_SLAVE (object);
}
