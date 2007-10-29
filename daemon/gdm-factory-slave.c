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

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-factory-slave.h"
#include "gdm-factory-slave-glue.h"

#include "gdm-server.h"
#include "gdm-greeter-session.h"
#include "gdm-greeter-server.h"
#include "gdm-session-relay.h"

extern char **environ;

#define GDM_FACTORY_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_FACTORY_SLAVE, GdmFactorySlavePrivate))

#define GDM_DBUS_NAME                      "org.gnome.DisplayManager"
#define GDM_DBUS_FACTORY_DISPLAY_INTERFACE "org.gnome.DisplayManager.StaticFactoryDisplay"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmFactorySlavePrivate
{
        char              *id;
        GPid               pid;
        guint              output_watch_id;
        guint              error_watch_id;
        guint              greeter_reset_id;

        GPid               server_pid;
        Display           *server_display;
        guint              connection_attempts;

        GdmServer         *server;
        GdmSessionRelay   *session_relay;
        GdmGreeterServer  *greeter_server;
        GdmGreeterSession *greeter;
        DBusGProxy        *factory_display_proxy;
        DBusGConnection   *connection;
};

enum {
        PROP_0,
};

static void     gdm_factory_slave_class_init    (GdmFactorySlaveClass *klass);
static void     gdm_factory_slave_init          (GdmFactorySlave      *factory_slave);
static void     gdm_factory_slave_finalize      (GObject             *object);

G_DEFINE_TYPE (GdmFactorySlave, gdm_factory_slave, GDM_TYPE_SLAVE)

static void
on_greeter_start (GdmGreeterSession *greeter,
                  GdmFactorySlave   *slave)
{
        g_debug ("Greeter started");
}

static void
on_greeter_stop (GdmGreeterSession *greeter,
                 GdmFactorySlave   *slave)
{
        g_debug ("Greeter stopped");
}

static void
on_session_relay_info (GdmSessionRelay *relay,
                       const char      *text,
                       GdmFactorySlave  *slave)
{
        g_debug ("Info: %s", text);
        gdm_greeter_server_info (slave->priv->greeter_server, text);
}

static void
on_session_relay_problem (GdmSessionRelay *relay,
                          const char      *text,
                          GdmFactorySlave *slave)
{
        g_debug ("Problem: %s", text);
        gdm_greeter_server_problem (slave->priv->greeter_server, text);
}

static void
on_session_relay_info_query (GdmSessionRelay *relay,
                             const char      *text,
                             GdmFactorySlave *slave)
{

        g_debug ("Info query: %s", text);
        gdm_greeter_server_info_query (slave->priv->greeter_server, text);
}

static void
on_session_relay_secret_info_query (GdmSessionRelay *relay,
                                    const char      *text,
                                    GdmFactorySlave *slave)
{
        g_debug ("Secret info query: %s", text);
        gdm_greeter_server_secret_info_query (slave->priv->greeter_server, text);
}

static void
on_session_relay_opened (GdmSessionRelay *relay,
                         GdmFactorySlave *slave)
{
        g_debug ("Relay session opened");

        gdm_greeter_server_ready (slave->priv->greeter_server);
}

static void
on_session_relay_user_verified (GdmSessionRelay *relay,
                                GdmFactorySlave *slave)
{
        g_debug ("Relay session user verified");

        gdm_greeter_server_reset (slave->priv->greeter_server);
}

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
on_session_relay_user_verification_error (GdmSessionRelay *relay,
                                          const char      *message,
                                          GdmFactorySlave *slave)
{
        g_debug ("could not successfully authenticate user: %s",
                 message);

        gdm_greeter_server_problem (slave->priv->greeter_server, _("Unable to authenticate user"));

        queue_greeter_reset (slave);
}

static gboolean
create_product_display (GdmFactorySlave *slave)
{
        char    *display_id;
        char    *server_address;
        char    *product_id;
        GError  *error;
        gboolean res;
        gboolean ret;

        ret = FALSE;

        g_debug ("Create product display");

        g_object_get (slave,
                      "display-id", &display_id,
                      NULL);

        g_debug ("Connecting to display %s", display_id);
        slave->priv->factory_display_proxy = dbus_g_proxy_new_for_name (slave->priv->connection,
                                                                        GDM_DBUS_NAME,
                                                                        display_id,
                                                                        GDM_DBUS_FACTORY_DISPLAY_INTERFACE);
        g_free (display_id);

        if (slave->priv->factory_display_proxy == NULL) {
                g_warning ("Failed to create display proxy %s", display_id);
                goto out;
        }

        server_address = gdm_session_relay_get_address (slave->priv->session_relay);

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->factory_display_proxy,
                                 "CreateProductDisplay",
                                 &error,
                                 G_TYPE_STRING, server_address,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &product_id,
                                 G_TYPE_INVALID);
        g_free (server_address);

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
on_session_relay_disconnected (GdmSessionRelay *relay,
                               GdmFactorySlave *slave)
{
        g_debug ("Relay disconnected");

        /* FIXME: do some kind of loop detection */
        gdm_greeter_server_reset (slave->priv->greeter_server);
        create_product_display (slave);
}

static void
on_session_relay_session_started (GdmSessionRelay *relay,
                                  GdmFactorySlave *slave)
{
        g_debug ("Relay session started");
        gdm_greeter_server_reset (slave->priv->greeter_server);
}

static void
on_greeter_begin_verification (GdmGreeterServer *greeter_server,
                               GdmFactorySlave  *slave)
{
        g_debug ("begin verification");

        gdm_session_relay_begin_verification (slave->priv->session_relay);
}

static void
on_greeter_begin_verification_for_user (GdmGreeterServer *greeter_server,
                                        const char       *username,
                                        GdmFactorySlave  *slave)
{
        g_debug ("begin verification for user");

        gdm_session_relay_begin_verification_for_user (slave->priv->session_relay,
                                                       username);
}

static void
on_greeter_answer (GdmGreeterServer *greeter_server,
                   const char       *text,
                   GdmFactorySlave  *slave)
{
        g_debug ("Greeter answer");
        gdm_session_relay_answer_query (slave->priv->session_relay, text);
}

static void
on_greeter_session_selected (GdmGreeterServer *greeter_server,
                             const char       *text,
                             GdmFactorySlave  *slave)
{
        gdm_session_relay_select_session (slave->priv->session_relay, text);
}

static void
on_greeter_language_selected (GdmGreeterServer *greeter_server,
                              const char       *text,
                              GdmFactorySlave  *slave)
{
        gdm_session_relay_select_language (slave->priv->session_relay, text);
}

static void
on_greeter_user_selected (GdmGreeterServer *greeter_server,
                          const char       *text,
                          GdmFactorySlave  *slave)
{
        gdm_session_relay_select_user (slave->priv->session_relay, text);
}

static void
on_greeter_cancel (GdmGreeterServer *greeter_server,
                   GdmFactorySlave  *slave)
{
        gdm_session_relay_cancel (slave->priv->session_relay);
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
                      GdmFactorySlave  *slave)
{
        g_debug ("Greeter started");

        create_product_display (slave);
}

static void
run_greeter (GdmFactorySlave *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        char          *address;

        g_debug ("Running greeter");

        display_is_local = FALSE;
        display_id = NULL;
        display_name = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-id", &display_id,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));

        /* FIXME: send a signal back to the master */

#if 0

        /* OK from now on it's really the user whacking us most likely,
         * we have already started up well */
        do_xfailed_on_xio_error = FALSE;
#endif

#if 0
        /* checkout xinerama */
        gdm_screen_init (slave);
#endif

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", "gdm");

        slave->priv->greeter_server = gdm_greeter_server_new (display_id);
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

        g_debug ("Creating greeter on %s %s", display_name, display_device);
        slave->priv->greeter = gdm_greeter_session_new (display_name,
                                                        display_device,
                                                        display_hostname,
                                                        display_is_local);
        g_signal_connect (slave->priv->greeter,
                          "started",
                          G_CALLBACK (on_greeter_start),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "stopped",
                          G_CALLBACK (on_greeter_stop),
                          slave);
        g_object_set (slave->priv->greeter,
                      "x11-authority-file", auth_file,
                      NULL);
        gdm_greeter_session_set_server_address (slave->priv->greeter, address);
        gdm_greeter_session_start (slave->priv->greeter);

        g_free (address);

        g_free (display_id);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmFactorySlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        g_debug ("Connect to display");

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                /* FIXME: handle wait-for-go */

                run_greeter (slave);
        } else {
                if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
                        exit (1);
                }
        }

        return FALSE;
}

static void
server_ready_cb (GdmServer *server,
                 GdmFactorySlave  *slave)
{
        g_debug ("Server ready");

        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static gboolean
gdm_factory_slave_run (GdmFactorySlave *slave)
{
        char    *display_name;
        gboolean display_is_local;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      NULL);

        /* if this is local display start a server if one doesn't
         * exist */
        if (display_is_local) {
                gboolean res;

                slave->priv->server = gdm_server_new (display_name);

                g_signal_connect (slave->priv->server,
                                  "ready",
                                  G_CALLBACK (server_ready_cb),
                                  slave);

                res = gdm_server_start (slave->priv->server);
                if (! res) {
                        g_warning (_("Could not start the X "
                                     "server (your graphical environment) "
                                     "due to some internal error. "
                                     "Please contact your system administrator "
                                     "or check your syslog to diagnose. "
                                     "In the meantime this display will be "
                                     "disabled.  Please restart GDM when "
                                     "the problem is corrected."));
                        exit (1);
                }

                g_debug ("Started X server");
        } else {
                g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
        }

        g_free (display_name);

        return TRUE;
}

static gboolean
gdm_factory_slave_start (GdmSlave *slave)
{
        gboolean res;
        gboolean ret;

        ret = FALSE;

        g_debug ("Starting factory slave");

        res = GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->start (slave);


        GDM_FACTORY_SLAVE (slave)->priv->session_relay = gdm_session_relay_new ();
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "info",
                          G_CALLBACK (on_session_relay_info),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "problem",
                          G_CALLBACK (on_session_relay_problem),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "info-query",
                          G_CALLBACK (on_session_relay_info_query),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "secret-info-query",
                          G_CALLBACK (on_session_relay_secret_info_query),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "user-verified",
                          G_CALLBACK (on_session_relay_user_verified),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "user-verification-error",
                          G_CALLBACK (on_session_relay_user_verification_error),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "opened",
                          G_CALLBACK (on_session_relay_opened),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "disconnected",
                          G_CALLBACK (on_session_relay_disconnected),
                          slave);
        g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
                          "session-started",
                          G_CALLBACK (on_session_relay_session_started),
                          slave);

        gdm_session_relay_start (GDM_FACTORY_SLAVE (slave)->priv->session_relay);

        gdm_factory_slave_run (GDM_FACTORY_SLAVE (slave));

        ret = TRUE;

        return ret;
}

static gboolean
gdm_factory_slave_stop (GdmSlave *slave)
{
        gboolean res;

        g_debug ("Stopping factory_slave");

        res = GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->stop (slave);

        if (GDM_FACTORY_SLAVE (slave)->priv->session_relay != NULL) {
                gdm_session_relay_stop (GDM_FACTORY_SLAVE (slave)->priv->session_relay);
                g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->session_relay);
                GDM_FACTORY_SLAVE (slave)->priv->session_relay = NULL;
        }

        if (GDM_FACTORY_SLAVE (slave)->priv->greeter_server != NULL) {
                gdm_greeter_server_stop (GDM_FACTORY_SLAVE (slave)->priv->greeter_server);
                g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->greeter_server);
                GDM_FACTORY_SLAVE (slave)->priv->greeter_server = NULL;
        }

        if (GDM_FACTORY_SLAVE (slave)->priv->greeter != NULL) {
                gdm_greeter_session_stop (GDM_FACTORY_SLAVE (slave)->priv->greeter);
                g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->greeter);
                GDM_FACTORY_SLAVE (slave)->priv->greeter = NULL;
        }

        if (GDM_FACTORY_SLAVE (slave)->priv->server != NULL) {
                gdm_server_stop (GDM_FACTORY_SLAVE (slave)->priv->server);
                g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->server);
                GDM_FACTORY_SLAVE (slave)->priv->server = NULL;
        }

        if (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy != NULL) {
                g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy);
        }

        return TRUE;
}

static void
gdm_factory_slave_set_property (GObject      *object,
                               guint          prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GdmFactorySlave *self;

        self = GDM_FACTORY_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_factory_slave_get_property (GObject    *object,
                               guint       prop_id,
                               GValue      *value,
                               GParamSpec *pspec)
{
        GdmFactorySlave *self;

        self = GDM_FACTORY_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_factory_slave_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GdmFactorySlave      *factory_slave;
        GdmFactorySlaveClass *klass;

        klass = GDM_FACTORY_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_FACTORY_SLAVE));

        factory_slave = GDM_FACTORY_SLAVE (G_OBJECT_CLASS (gdm_factory_slave_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (factory_slave);
}

static void
gdm_factory_slave_class_init (GdmFactorySlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->get_property = gdm_factory_slave_get_property;
        object_class->set_property = gdm_factory_slave_set_property;
        object_class->constructor = gdm_factory_slave_constructor;
        object_class->finalize = gdm_factory_slave_finalize;

        slave_class->start = gdm_factory_slave_start;
        slave_class->stop = gdm_factory_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmFactorySlavePrivate));

        dbus_g_object_type_install_info (GDM_TYPE_FACTORY_SLAVE, &dbus_glib_gdm_factory_slave_object_info);
}

static void
gdm_factory_slave_init (GdmFactorySlave *slave)
{
        GError *error;

        slave->priv = GDM_FACTORY_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;

        error = NULL;
        slave->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
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

        g_debug ("Finalizing slave");

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
