/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-xdmcp-chooser-slave.h"

#include "gdm-server.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"
#include "gdm-session.h"

#define GDM_XDMCP_CHOOSER_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_XDMCP_CHOOSER_SLAVE, GdmXdmcpChooserSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS  10
#define DEFAULT_PING_INTERVAL 15

struct GdmXdmcpChooserSlavePrivate
{
        char              *id;
        GPid               pid;

        int                ping_interval;

        guint              connection_attempts;

        GdmLaunchEnvironment *chooser_environment;
};

enum {
        HOSTNAME_SELECTED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_xdmcp_chooser_slave_class_init     (GdmXdmcpChooserSlaveClass *klass);
static void     gdm_xdmcp_chooser_slave_init           (GdmXdmcpChooserSlave      *xdmcp_chooser_slave);
static void     gdm_xdmcp_chooser_slave_finalize       (GObject                   *object);

G_DEFINE_TYPE (GdmXdmcpChooserSlave, gdm_xdmcp_chooser_slave, GDM_TYPE_SLAVE)

static void
on_chooser_session_opened (GdmLaunchEnvironment    *chooser,
                           GdmXdmcpChooserSlave *slave)
{
        char       *session_id;

        g_debug ("GdmSimpleSlave: Chooser session opened");
        session_id = gdm_launch_environment_get_session_id (GDM_LAUNCH_ENVIRONMENT (chooser));

        g_object_set (GDM_SLAVE (slave), "session-id", session_id, NULL);
        g_free (session_id);
}

static void
on_chooser_session_start (GdmLaunchEnvironment    *chooser,
                          GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser started");
}

static void
on_chooser_session_stop (GdmLaunchEnvironment    *chooser,
                         GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser stopped");
        gdm_slave_stop (GDM_SLAVE (slave));

        g_object_unref (GDM_XDMCP_CHOOSER_SLAVE (slave)->priv->chooser_environment);
        GDM_XDMCP_CHOOSER_SLAVE (slave)->priv->chooser_environment = NULL;
}

static void
on_chooser_session_exited (GdmLaunchEnvironment    *chooser,
                           int                   code,
                           GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser exited: %d", code);

        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        gdm_slave_stop (GDM_SLAVE (slave));
}

static void
on_chooser_session_died (GdmLaunchEnvironment    *chooser,
                         int                   signal,
                         GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser died: %d", signal);

        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        gdm_slave_stop (GDM_SLAVE (slave));
}

static void
on_chooser_hostname_selected (GdmSession           *session,
                              const char           *name,
                              GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: connecting to host %s", name);
        g_signal_emit (slave, signals [HOSTNAME_SELECTED], 0, name);
}

static void
on_chooser_disconnected (GdmSession           *session,
                         GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser disconnected");

        /* stop pinging */
        alarm (0);

        gdm_slave_stop (GDM_SLAVE (slave));
}

static void
on_chooser_connected (GdmSession           *session,
                      GCredentials         *credentials,
                      GPid                  pid_of_client,
                      GdmXdmcpChooserSlave *slave)
{
        g_debug ("GdmXdmcpChooserSlave: Chooser connected");
}

static void
setup_server (GdmXdmcpChooserSlave *slave)
{
}

static GdmLaunchEnvironment *
create_chooser_session (const char *display_name,
                        const char *display_device,
                        const char *display_hostname)
{
        return g_object_new (GDM_TYPE_LAUNCH_ENVIRONMENT,
                             "command", LIBEXECDIR "/gdm-simple-chooser",
                             "verification-mode", GDM_SESSION_VERIFICATION_MODE_CHOOSER,
                             "x11-display-name", display_name,
                             "x11-display-device", display_device,
                             "x11-display-hostname", display_hostname,
                             NULL);
}

static void
run_chooser (GdmXdmcpChooserSlave *slave)
{
        char          *display_name;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        gboolean       res;
        GdmSession    *session;

        g_debug ("GdmXdmcpChooserSlave: Running chooser");

        display_name = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        g_debug ("GdmXdmcpChooserSlave: Creating chooser for %s %s", display_name, display_hostname);

        /* FIXME: send a signal back to the master */

        /* If XDMCP setup pinging */
        slave->priv->ping_interval = DEFAULT_PING_INTERVAL;
        res = gdm_settings_direct_get_int (GDM_KEY_PING_INTERVAL,
                                           &(slave->priv->ping_interval));

        if (res && slave->priv->ping_interval > 0) {
                alarm (slave->priv->ping_interval);
        }

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_run_script (GDMCONFDIR "/Init", GDM_USERNAME,
                        display_name,
                        display_hostname,
                        auth_file);

        g_debug ("GdmXdmcpChooserSlave: Creating chooser on %s %s %s", display_name, display_device, display_hostname);
        slave->priv->chooser_environment = create_chooser_session (display_name,
                                                       display_device,
                                                       display_hostname);
        g_signal_connect (slave->priv->chooser_environment,
                          "opened",
                          G_CALLBACK (on_chooser_session_opened),
                          slave);
        g_signal_connect (slave->priv->chooser_environment,
                          "started",
                          G_CALLBACK (on_chooser_session_start),
                          slave);
        g_signal_connect (slave->priv->chooser_environment,
                          "stopped",
                          G_CALLBACK (on_chooser_session_stop),
                          slave);
        g_signal_connect (slave->priv->chooser_environment,
                          "exited",
                          G_CALLBACK (on_chooser_session_exited),
                          slave);
        g_signal_connect (slave->priv->chooser_environment,
                          "died",
                          G_CALLBACK (on_chooser_session_died),
                          slave);
        g_object_set (slave->priv->chooser_environment,
                      "x11-authority-file", auth_file,
                      NULL);

        gdm_launch_environment_start (GDM_LAUNCH_ENVIRONMENT (slave->priv->chooser_environment));

        session = gdm_launch_environment_get_session (GDM_LAUNCH_ENVIRONMENT (slave->priv->chooser_environment));

        g_signal_connect (session,
                          "hostname-selected",
                          G_CALLBACK (on_chooser_hostname_selected),
                          slave);
        g_signal_connect (session,
                          "client-disconnected",
                          G_CALLBACK (on_chooser_disconnected),
                          slave);
        g_signal_connect (session,
                          "disconnected",
                          G_CALLBACK (on_chooser_disconnected),
                          slave);
        g_signal_connect (session,
                          "client-connected",
                          G_CALLBACK (on_chooser_connected),
                          slave);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmXdmcpChooserSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                /* FIXME: handle wait-for-go */

                setup_server (slave);
                run_chooser (slave);
        } else {
                if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
                        exit (1);
                }
                return TRUE;
        }

        return FALSE;
}

static gboolean
gdm_xdmcp_chooser_slave_run (GdmXdmcpChooserSlave *slave)
{
        char    *display_name;
        char    *auth_file;

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);

        g_free (display_name);
        g_free (auth_file);

        return TRUE;
}

static gboolean
gdm_xdmcp_chooser_slave_start (GdmSlave *slave)
{
        GDM_SLAVE_CLASS (gdm_xdmcp_chooser_slave_parent_class)->start (slave);

        gdm_xdmcp_chooser_slave_run (GDM_XDMCP_CHOOSER_SLAVE (slave));

        return TRUE;
}

static gboolean
gdm_xdmcp_chooser_slave_stop (GdmSlave *slave)
{
        GdmXdmcpChooserSlave *self = GDM_XDMCP_CHOOSER_SLAVE (slave);

        g_debug ("GdmXdmcpChooserSlave: Stopping xdmcp_chooser_slave");

        GDM_SLAVE_CLASS (gdm_xdmcp_chooser_slave_parent_class)->stop (slave);

        if (self->priv->chooser_environment != NULL) {
                gdm_launch_environment_stop (GDM_LAUNCH_ENVIRONMENT (self->priv->chooser_environment));
                g_object_unref (self->priv->chooser_environment);
                self->priv->chooser_environment = NULL;
        }

        return TRUE;
}

static void
gdm_xdmcp_chooser_slave_class_init (GdmXdmcpChooserSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->finalize = gdm_xdmcp_chooser_slave_finalize;

        slave_class->start = gdm_xdmcp_chooser_slave_start;
        slave_class->stop = gdm_xdmcp_chooser_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmXdmcpChooserSlavePrivate));

        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
}

static void
gdm_xdmcp_chooser_slave_init (GdmXdmcpChooserSlave *slave)
{
        slave->priv = GDM_XDMCP_CHOOSER_SLAVE_GET_PRIVATE (slave);
}

static void
gdm_xdmcp_chooser_slave_finalize (GObject *object)
{
        GdmXdmcpChooserSlave *xdmcp_chooser_slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_XDMCP_CHOOSER_SLAVE (object));

        xdmcp_chooser_slave = GDM_XDMCP_CHOOSER_SLAVE (object);

        g_return_if_fail (xdmcp_chooser_slave->priv != NULL);

        G_OBJECT_CLASS (gdm_xdmcp_chooser_slave_parent_class)->finalize (object);
}
