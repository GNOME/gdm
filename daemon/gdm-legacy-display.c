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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-launch-environment.h"
#include "gdm-legacy-display.h"
#include "gdm-local-display-glue.h"
#include "gdm-server.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_LEGACY_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LEGACY_DISPLAY, GdmLegacyDisplayPrivate))

struct GdmLegacyDisplayPrivate
{
        GdmDBusLocalDisplay *skeleton;

        GdmServer           *server;
};

static void     gdm_legacy_display_class_init   (GdmLegacyDisplayClass *klass);
static void     gdm_legacy_display_init         (GdmLegacyDisplay      *legacy_display);

G_DEFINE_TYPE (GdmLegacyDisplay, gdm_legacy_display, GDM_TYPE_DISPLAY)

static GObject *
gdm_legacy_display_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmLegacyDisplay      *display;

        display = GDM_LEGACY_DISPLAY (G_OBJECT_CLASS (gdm_legacy_display_parent_class)->constructor (type,
                                                                                                     n_construct_properties,
                                                                                                     construct_properties));

        display->priv->skeleton = GDM_DBUS_LOCAL_DISPLAY (gdm_dbus_local_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->priv->skeleton));

        return G_OBJECT (display);
}

static void
gdm_legacy_display_finalize (GObject *object)
{
        GdmLegacyDisplay *display = GDM_LEGACY_DISPLAY (object);

        g_clear_object (&display->priv->skeleton);
        g_clear_object (&display->priv->server);

        G_OBJECT_CLASS (gdm_legacy_display_parent_class)->finalize (object);
}

static gboolean
gdm_legacy_display_prepare (GdmDisplay *display)
{
        GdmLegacyDisplay *self = GDM_LEGACY_DISPLAY (display);
        GdmLaunchEnvironment *launch_environment;
        char          *display_name;
        char          *seat_id;
        gboolean       doing_initial_setup = FALSE;

        display_name = NULL;
        seat_id = NULL;

        g_object_get (self,
                      "x11-display-name", &display_name,
                      "seat-id", &seat_id,
                      "doing-initial-setup", &doing_initial_setup,
                      NULL);

        if (!doing_initial_setup) {
                launch_environment = gdm_create_greeter_launch_environment (display_name,
                                                                            seat_id,
                                                                            NULL,
                                                                            NULL,
                                                                            TRUE);
        } else {
                launch_environment = gdm_create_initial_setup_launch_environment (display_name,
                                                                                seat_id,
                                                                                NULL,
                                                                                TRUE);
        }

        g_object_set (self, "launch-environment", launch_environment, NULL);
        g_object_unref (launch_environment);

        if (!gdm_display_create_authority (display)) {
                g_warning ("Unable to set up access control for display %s",
                           display_name);
                return FALSE;
        }

        return GDM_DISPLAY_CLASS (gdm_legacy_display_parent_class)->prepare (display);
}

static void
on_server_ready (GdmServer       *server,
                 GdmLegacyDisplay *self)
{
        gboolean ret;

        ret = gdm_display_connect (GDM_DISPLAY (self));

        if (!ret) {
                g_debug ("GdmDisplay: could not connect to display");
                gdm_display_unmanage (GDM_DISPLAY (self));
        } else {
                GdmLaunchEnvironment *launch_environment;
                char *display_device;

                display_device = gdm_server_get_display_device (server);

                g_object_get (G_OBJECT (self),
                              "launch-environment", &launch_environment,
                              NULL);
                g_object_set (G_OBJECT (launch_environment),
                              "x11-display-device",
                              display_device,
                              NULL);
                g_clear_pointer(&display_device, g_free);
                g_clear_object (&launch_environment);

                g_debug ("GdmDisplay: connected to display");
                g_object_set (G_OBJECT (self), "status", GDM_DISPLAY_MANAGED, NULL);
        }
}

static void
on_server_exited (GdmServer  *server,
                  int         exit_code,
                  GdmDisplay *self)
{
        g_debug ("GdmDisplay: server exited with code %d\n", exit_code);

        gdm_display_unmanage (GDM_DISPLAY (self));
}

static void
on_server_died (GdmServer  *server,
                int         signal_number,
                GdmDisplay *self)
{
        g_debug ("GdmDisplay: server died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_display_unmanage (GDM_DISPLAY (self));
}

static void
gdm_legacy_display_manage (GdmDisplay *display)
{
        GdmLegacyDisplay *self = GDM_LEGACY_DISPLAY (display);
        char            *display_name;
        char            *auth_file;
        char            *seat_id;
        gboolean         is_initial;
        gboolean         res;
        gboolean         disable_tcp;

        g_object_get (G_OBJECT (self),
                      "x11-display-name", &display_name,
                      "x11-authority-file", &auth_file,
                      "seat-id", &seat_id,
                      "is-initial", &is_initial,
                      NULL);

        self->priv->server = gdm_server_new (display_name, seat_id, auth_file, is_initial);

        g_free (display_name);
        g_free (auth_file);
        g_free (seat_id);

        disable_tcp = TRUE;
        if (gdm_settings_direct_get_boolean (GDM_KEY_DISALLOW_TCP, &disable_tcp)) {
                g_object_set (self->priv->server,
                              "disable-tcp", disable_tcp,
                              NULL);
        }

        g_signal_connect (self->priv->server,
                          "exited",
                          G_CALLBACK (on_server_exited),
                          self);
        g_signal_connect (self->priv->server,
                          "died",
                          G_CALLBACK (on_server_died),
                          self);
        g_signal_connect (self->priv->server,
                          "ready",
                          G_CALLBACK (on_server_ready),
                          self);

        res = gdm_server_start (self->priv->server);
        if (! res) {
                g_warning (_("Could not start the X "
                             "server (your graphical environment) "
                             "due to an internal error. "
                             "Please contact your system administrator "
                             "or check your syslog to diagnose. "
                             "In the meantime this display will be "
                             "disabled.  Please restart GDM when "
                             "the problem is corrected."));
                gdm_display_unmanage (GDM_DISPLAY (self));
        }

        g_debug ("GdmDisplay: Started X server");

}

static void
gdm_legacy_display_class_init (GdmLegacyDisplayClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->constructor = gdm_legacy_display_constructor;
        object_class->finalize = gdm_legacy_display_finalize;

        display_class->prepare = gdm_legacy_display_prepare;
        display_class->manage = gdm_legacy_display_manage;

        g_type_class_add_private (klass, sizeof (GdmLegacyDisplayPrivate));
}

static void
on_display_status_changed (GdmLegacyDisplay *self)
{
        int status;

        status = gdm_display_get_status (self);

        switch (status) {
            case GDM_DISPLAY_UNMANAGED:
                if (self->priv->server != NULL)
                        gdm_server_stop (self->priv->server);
                break;
            default:
                break;
        }
}

static void
gdm_legacy_display_init (GdmLegacyDisplay *legacy_display)
{

        legacy_display->priv = GDM_LEGACY_DISPLAY_GET_PRIVATE (legacy_display);

        g_signal_connect (legacy_display, "notify::status",
                          G_CALLBACK (on_display_status_changed),
                          NULL);
}

GdmDisplay *
gdm_legacy_display_new (int display_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf (":%d", display_number);
        object = g_object_new (GDM_TYPE_LEGACY_DISPLAY,
                               "x11-display-number", display_number,
                               "x11-display-name", x11_display,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
