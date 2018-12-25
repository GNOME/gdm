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

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-launch-environment.h"
#include "gdm-xdmcp-display.h"

#include "gdm-common.h"
#include "gdm-address.h"

#include "gdm-settings.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

typedef struct _GdmXdmcpDisplayPrivate
{
        GdmAddress             *remote_address;
        gint32                  session_number;
        guint                   connection_attempts;
} GdmXdmcpDisplayPrivate;

enum {
        PROP_0,
        PROP_REMOTE_ADDRESS,
        PROP_SESSION_NUMBER,
};

#define MAX_CONNECT_ATTEMPTS  10

static void     gdm_xdmcp_display_class_init    (GdmXdmcpDisplayClass *klass);
static void     gdm_xdmcp_display_init          (GdmXdmcpDisplay      *xdmcp_display);

G_DEFINE_TYPE_WITH_PRIVATE (GdmXdmcpDisplay, gdm_xdmcp_display, GDM_TYPE_DISPLAY)

gint32
gdm_xdmcp_display_get_session_number (GdmXdmcpDisplay *display)
{
        GdmXdmcpDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_XDMCP_DISPLAY (display), 0);

        priv = gdm_xdmcp_display_get_instance_private (display);
        return priv->session_number;
}

GdmAddress *
gdm_xdmcp_display_get_remote_address (GdmXdmcpDisplay *display)
{
        GdmXdmcpDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_XDMCP_DISPLAY (display), NULL);

        priv = gdm_xdmcp_display_get_instance_private (display);
        return priv->remote_address;
}

static void
_gdm_xdmcp_display_set_remote_address (GdmXdmcpDisplay *display,
                                       GdmAddress      *address)
{
        GdmXdmcpDisplayPrivate *priv;

        priv = gdm_xdmcp_display_get_instance_private (display);
        if (priv->remote_address != NULL) {
                gdm_address_free (priv->remote_address);
        }

        g_assert (address != NULL);

        gdm_address_debug (address);
        priv->remote_address = gdm_address_copy (address);
}

static void
gdm_xdmcp_display_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GdmXdmcpDisplay *self;
        GdmXdmcpDisplayPrivate *priv;

        self = GDM_XDMCP_DISPLAY (object);
        priv = gdm_xdmcp_display_get_instance_private (self);

        switch (prop_id) {
        case PROP_REMOTE_ADDRESS:
                _gdm_xdmcp_display_set_remote_address (self, g_value_get_boxed (value));
                break;
        case PROP_SESSION_NUMBER:
                priv->session_number = g_value_get_int (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_xdmcp_display_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GdmXdmcpDisplay *self;
        GdmXdmcpDisplayPrivate *priv;

        self = GDM_XDMCP_DISPLAY (object);
        priv = gdm_xdmcp_display_get_instance_private (self);

        switch (prop_id) {
        case PROP_REMOTE_ADDRESS:
                g_value_set_boxed (value, priv->remote_address);
                break;
        case PROP_SESSION_NUMBER:
                g_value_set_int (value, priv->session_number);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
gdm_xdmcp_display_prepare (GdmDisplay *display)
{
        GdmXdmcpDisplay *self = GDM_XDMCP_DISPLAY (display);
        GdmLaunchEnvironment *launch_environment;
        char          *display_name;
        char          *seat_id;
        char          *hostname;

        launch_environment = NULL;
        display_name = NULL;
        seat_id = NULL;
        hostname = NULL;

        g_object_get (self,
                      "x11-display-name", &display_name,
                      "seat-id", &seat_id,
                      "remote-hostname", &hostname,
                      "launch-environment", &launch_environment,
                      NULL);

        if (launch_environment == NULL) {
                launch_environment = gdm_create_greeter_launch_environment (display_name,
                                                                            seat_id,
                                                                            NULL,
                                                                            hostname,
                                                                            FALSE);
                g_object_set (self, "launch-environment", launch_environment, NULL);
                g_object_unref (launch_environment);
        }

        if (!gdm_display_create_authority (display)) {
                g_warning ("Unable to set up access control for display %s",
                           display_name);
                return FALSE;
        }

        return GDM_DISPLAY_CLASS (gdm_xdmcp_display_parent_class)->prepare (display);
}

static gboolean
idle_connect_to_display (GdmXdmcpDisplay *self)
{
        GdmXdmcpDisplayPrivate *priv;
        gboolean res;

        priv = gdm_xdmcp_display_get_instance_private (self);
        priv->connection_attempts++;

        res = gdm_display_connect (GDM_DISPLAY (self));
        if (res) {
                g_object_set (G_OBJECT (self), "status", GDM_DISPLAY_MANAGED, NULL);
        } else {
                if (priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", priv->connection_attempts);
                        gdm_display_unmanage (GDM_DISPLAY (self));
                        return FALSE;
                }
                return TRUE;
        }

        return FALSE;
}

static void
gdm_xdmcp_display_manage (GdmDisplay *display)
{
        GdmXdmcpDisplay *self = GDM_XDMCP_DISPLAY (display);

        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, self);
}

static void
gdm_xdmcp_display_class_init (GdmXdmcpDisplayClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->get_property = gdm_xdmcp_display_get_property;
        object_class->set_property = gdm_xdmcp_display_set_property;

        display_class->prepare = gdm_xdmcp_display_prepare;
        display_class->manage = gdm_xdmcp_display_manage;

        g_object_class_install_property (object_class,
                                         PROP_REMOTE_ADDRESS,
                                         g_param_spec_boxed ("remote-address",
                                                             "Remote address",
                                                             "Remote address",
                                                             GDM_TYPE_ADDRESS,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_NUMBER,
                                         g_param_spec_int ("session-number",
                                                           "session-number",
                                                           "session-number",
                                                           G_MININT,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

}

static void
gdm_xdmcp_display_init (GdmXdmcpDisplay *xdmcp_display)
{

        gboolean allow_remote_autologin;

        allow_remote_autologin = FALSE;
        gdm_settings_direct_get_boolean (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN, &allow_remote_autologin);

        g_object_set (G_OBJECT (xdmcp_display), "allow-timed-login", allow_remote_autologin, NULL);
}

GdmDisplay *
gdm_xdmcp_display_new (const char *hostname,
                       int         number,
                       GdmAddress *address,
                       gint32      session_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf ("%s:%d", hostname, number);
        object = g_object_new (GDM_TYPE_XDMCP_DISPLAY,
                               "remote-hostname", hostname,
                               "x11-display-number", number,
                               "x11-display-name", x11_display,
                               "is-local", FALSE,
                               "remote-address", address,
                               "session-number", session_number,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
