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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-simple-slave.h"
#include "gdm-simple-slave-glue.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-session-direct.h"
#include "gdm-greeter-server.h"
#include "gdm-greeter-session.h"

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmSimpleSlavePrivate
{
        char              *id;
        GPid               pid;
        guint              output_watch_id;
        guint              error_watch_id;

        guint              greeter_reset_id;

        int                ping_interval;

        GPid               server_pid;
        guint              connection_attempts;

        GdmServer         *server;
        GdmGreeterServer  *greeter_server;
        GdmGreeterSession *greeter;
        GdmSessionDirect  *session;
        DBusGConnection   *connection;
};

enum {
        PROP_0,
};

static void     gdm_simple_slave_class_init     (GdmSimpleSlaveClass *klass);
static void     gdm_simple_slave_init           (GdmSimpleSlave      *simple_slave);
static void     gdm_simple_slave_finalize       (GObject             *object);

G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

static void
on_session_started (GdmSession       *session,
                    GdmSimpleSlave   *slave)
{
        g_debug ("session started");

        /* FIXME: should we do something here? */
}

static void
on_session_exited (GdmSession     *session,
                   int             exit_code,
                   GdmSimpleSlave *slave)
{
        g_debug ("session exited with code %d\n", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_session_died (GdmSession     *session,
                 int             signal_number,
                 GdmSimpleSlave *slave)
{
        g_debug ("session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static gboolean
add_user_authorization (GdmSimpleSlave *slave,
                        char          **filename)
{
        char    *username;
        gboolean ret;

        username = gdm_session_direct_get_username (slave->priv->session);
        ret = gdm_slave_add_user_authorization (GDM_SLAVE (slave),
                                                username,
                                                filename);
        g_free (username);

        return ret;
}

static void
on_session_user_verified (GdmSession     *session,
                          GdmSimpleSlave *slave)
{
        char *auth_file;

        gdm_greeter_session_stop (slave->priv->greeter);
        gdm_greeter_server_stop (slave->priv->greeter_server);

        auth_file = NULL;
        add_user_authorization (slave, &auth_file);

        g_object_set (session,
                      "user-x11-authority-file", auth_file,
                      NULL);

        g_free (auth_file);

        gdm_session_start_session (session);
}

static gboolean
greeter_reset_timeout (GdmSimpleSlave *slave)
{
        gdm_greeter_server_reset (slave->priv->greeter_server);
        slave->priv->greeter_reset_id = 0;
        return FALSE;
}

static void
queue_greeter_reset (GdmSimpleSlave *slave)
{
        if (slave->priv->greeter_reset_id > 0) {
                return;
        }

        slave->priv->greeter_reset_id = g_timeout_add_seconds (2, (GSourceFunc)greeter_reset_timeout, slave);
}

static void
on_session_user_verification_error (GdmSession     *session,
                                    const char     *message,
                                    GdmSimpleSlave *slave)
{
        gdm_greeter_server_problem (slave->priv->greeter_server, _("Unable to authenticate user"));

        queue_greeter_reset (slave);
}

static void
on_session_info (GdmSession     *session,
                 const char     *text,
                 GdmSimpleSlave *slave)
{
        g_debug ("Info: %s", text);
        gdm_greeter_server_info (slave->priv->greeter_server, text);
}

static void
on_session_problem (GdmSession     *session,
                    const char     *text,
                    GdmSimpleSlave *slave)
{
        g_debug ("Problem: %s", text);
        gdm_greeter_server_problem (slave->priv->greeter_server, text);
}

static void
on_session_info_query (GdmSession     *session,
                       const char     *text,
                       GdmSimpleSlave *slave)
{

        g_debug ("Info query: %s", text);
        gdm_greeter_server_info_query (slave->priv->greeter_server, text);
}

static void
on_session_secret_info_query (GdmSession     *session,
                              const char     *text,
                              GdmSimpleSlave *slave)
{
        g_debug ("Secret info query: %s", text);
        gdm_greeter_server_secret_info_query (slave->priv->greeter_server, text);
}

static void
on_session_opened (GdmSession     *session,
                   GdmSimpleSlave *slave)
{
        gboolean res;

        g_debug ("session opened");
        res = gdm_greeter_server_ready (slave->priv->greeter_server);
        if (! res) {
                g_warning ("Unable to send ready");
        }
}

static void
on_session_selected_user_changed (GdmSession     *session,
                                  const char     *text,
                                  GdmSimpleSlave *slave)
{
        g_debug ("Selected user changed: %s", text);

        gdm_greeter_server_selected_user_changed (slave->priv->greeter_server, text);
}


static void
create_new_session (GdmSimpleSlave *slave)
{
        gboolean       display_is_local;
        char          *display_name;
        char          *display_hostname;
        char          *display_device;

        g_debug ("Creating new session");

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-is-local", &display_is_local,
                      NULL);

        display_device = NULL;
        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        slave->priv->session = gdm_session_direct_new (display_name,
                                                       display_hostname,
                                                       display_device,
                                                       display_is_local);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);

        g_signal_connect (slave->priv->session,
                          "opened",
                          G_CALLBACK (on_session_opened),
                          slave);
#if 0
        g_signal_connect (slave->priv->session,
                          "closed",
                          G_CALLBACK (on_session_closed),
                          slave);
#endif
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
                          "user-verified",
                          G_CALLBACK (on_session_user_verified),
                          slave);

        g_signal_connect (slave->priv->session,
                          "user-verification-error",
                          G_CALLBACK (on_session_user_verification_error),
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

        g_signal_connect (slave->priv->session,
                          "selected-user-changed",
                          G_CALLBACK (on_session_selected_user_changed),
                          slave);
}

static void
on_greeter_start (GdmGreeterSession *greeter,
                  GdmSimpleSlave    *slave)
{
        g_debug ("Greeter started");
}

static void
on_greeter_stop (GdmGreeterSession *greeter,
                 GdmSimpleSlave    *slave)
{
        g_debug ("Greeter stopped");
}

static void
on_greeter_begin_verification (GdmGreeterServer *greeter_server,
                               GdmSimpleSlave   *slave)
{
        g_debug ("begin verification");
        gdm_session_begin_verification (GDM_SESSION (slave->priv->session));
}

static void
on_greeter_begin_verification_for_user (GdmGreeterServer *greeter_server,
                                        const char       *username,
                                        GdmSimpleSlave   *slave)
{
        g_debug ("begin verification");
        gdm_session_begin_verification_for_user (GDM_SESSION (slave->priv->session),
                                                 username);
}

static void
on_greeter_answer (GdmGreeterServer *greeter_server,
                   const char       *text,
                   GdmSimpleSlave   *slave)
{
        gdm_session_answer_query (GDM_SESSION (slave->priv->session), text);
}

static void
on_greeter_session_selected (GdmGreeterServer *greeter_server,
                             const char       *text,
                             GdmSimpleSlave   *slave)
{
        gdm_session_select_session (GDM_SESSION (slave->priv->session), text);
}

static void
on_greeter_language_selected (GdmGreeterServer *greeter_server,
                              const char       *text,
                              GdmSimpleSlave   *slave)
{
        gdm_session_select_language (GDM_SESSION (slave->priv->session), text);
}

static void
on_greeter_user_selected (GdmGreeterServer *greeter_server,
                          const char       *text,
                          GdmSimpleSlave   *slave)
{
        g_debug ("Greeter user selected");
}

static void
on_greeter_cancel (GdmGreeterServer *greeter_server,
                   GdmSimpleSlave   *slave)
{
        g_debug ("Greeter cancelled");

        if (slave->priv->session != NULL) {
                gdm_session_close (GDM_SESSION (slave->priv->session));
                g_object_unref (slave->priv->session);
        }

        create_new_session (slave);

        gdm_session_open (GDM_SESSION (slave->priv->session));
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
                      GdmSimpleSlave   *slave)
{
        gboolean display_is_local;

        g_debug ("Greeter started");

        gdm_session_open (GDM_SESSION (slave->priv->session));

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      NULL);

        /* If XDMCP stop pinging */
        if ( ! display_is_local) {
                alarm (0);
        }
}

static void
setup_server (GdmSimpleSlave *slave)
{
        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));
}

static void
run_greeter (GdmSimpleSlave *slave)
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
                      "display-id", &display_id,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        g_debug ("Creating greeter for %s %s", display_name, display_hostname);

        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        /* FIXME: send a signal back to the master */

        /* If XDMCP setup pinging */
        if ( ! display_is_local && slave->priv->ping_interval > 0) {
                alarm (slave->priv->ping_interval);
        }

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", "gdm");

        create_new_session (slave);

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

        g_debug ("Creating greeter on %s %s %s", display_name, display_device, display_hostname);
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

        g_free (display_id);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmSimpleSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

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
        }

        return FALSE;
}

static void
server_ready_cb (GdmServer      *server,
                 GdmSimpleSlave *slave)
{
        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static gboolean
gdm_simple_slave_run (GdmSimpleSlave *slave)
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
gdm_simple_slave_start (GdmSlave *slave)
{
        gboolean res;

        res = GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->start (slave);

        gdm_simple_slave_run (GDM_SIMPLE_SLAVE (slave));

        return TRUE;
}

static gboolean
gdm_simple_slave_stop (GdmSlave *slave)
{
        gboolean res;

        g_debug ("Stopping simple_slave");

        res = GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->stop (slave);

        if (GDM_SIMPLE_SLAVE (slave)->priv->greeter != NULL) {
                gdm_greeter_session_stop (GDM_SIMPLE_SLAVE (slave)->priv->greeter);
                g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->greeter);
                GDM_SIMPLE_SLAVE (slave)->priv->greeter = NULL;
        }

        if (GDM_SIMPLE_SLAVE (slave)->priv->session != NULL) {
                gdm_session_close (GDM_SESSION (GDM_SIMPLE_SLAVE (slave)->priv->session));
                g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->session);
                GDM_SIMPLE_SLAVE (slave)->priv->session = NULL;
        }

        if (GDM_SIMPLE_SLAVE (slave)->priv->server != NULL) {
                gdm_server_stop (GDM_SIMPLE_SLAVE (slave)->priv->server);
                g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->server);
                GDM_SIMPLE_SLAVE (slave)->priv->server = NULL;
        }

        return TRUE;
}

static void
gdm_simple_slave_set_property (GObject      *object,
                               guint          prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GdmSimpleSlave *self;

        self = GDM_SIMPLE_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_simple_slave_get_property (GObject    *object,
                               guint       prop_id,
                               GValue      *value,
                               GParamSpec *pspec)
{
        GdmSimpleSlave *self;

        self = GDM_SIMPLE_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_simple_slave_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GdmSimpleSlave      *simple_slave;
        GdmSimpleSlaveClass *klass;

        klass = GDM_SIMPLE_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_SIMPLE_SLAVE));

        simple_slave = GDM_SIMPLE_SLAVE (G_OBJECT_CLASS (gdm_simple_slave_parent_class)->constructor (type,
                                                                                 n_construct_properties,
                                                                                 construct_properties));

        return G_OBJECT (simple_slave);
}

static void
gdm_simple_slave_class_init (GdmSimpleSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->get_property = gdm_simple_slave_get_property;
        object_class->set_property = gdm_simple_slave_set_property;
        object_class->constructor = gdm_simple_slave_constructor;
        object_class->finalize = gdm_simple_slave_finalize;

        slave_class->start = gdm_simple_slave_start;
        slave_class->stop = gdm_simple_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmSimpleSlavePrivate));

        dbus_g_object_type_install_info (GDM_TYPE_SIMPLE_SLAVE, &dbus_glib_gdm_simple_slave_object_info);
}

static void
gdm_simple_slave_init (GdmSimpleSlave *slave)
{
        slave->priv = GDM_SIMPLE_SLAVE_GET_PRIVATE (slave);
}

static void
gdm_simple_slave_finalize (GObject *object)
{
        GdmSimpleSlave *simple_slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SIMPLE_SLAVE (object));

        simple_slave = GDM_SIMPLE_SLAVE (object);

        g_return_if_fail (simple_slave->priv != NULL);

        gdm_simple_slave_stop (GDM_SLAVE (simple_slave));

        if (simple_slave->priv->greeter_reset_id > 0) {
                g_source_remove (simple_slave->priv->greeter_reset_id);
                simple_slave->priv->greeter_reset_id = 0;
        }

        G_OBJECT_CLASS (gdm_simple_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_simple_slave_new (const char *id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SIMPLE_SLAVE,
                               "display-id", id,
                               NULL);

        return GDM_SLAVE (object);
}
