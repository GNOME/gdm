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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#include "gdm-xdmcp-chooser-display.h"
#include "gdm-xdmcp-display-glue.h"
#include "gdm-xdmcp-chooser-slave-glue.h"

#include "gdm-common.h"
#include "gdm-address.h"

#define DEFAULT_SLAVE_COMMAND LIBEXECDIR"/gdm-xdmcp-chooser-slave"

#define GDM_DBUS_NAME                          "/org/gnome/DisplayManager"
#define GDM_XDMCP_CHOOSER_SLAVE_DBUS_INTERFACE "org.gnome.DisplayManager.XdmcpChooserSlave"

#define GDM_XDMCP_CHOOSER_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_XDMCP_CHOOSER_DISPLAY, GdmXdmcpChooserDisplayPrivate))

struct GdmXdmcpChooserDisplayPrivate
{
        GdmDBusXdmcpDisplay      *skeleton;
        GdmDBusXdmcpChooserSlave *slave_proxy;
};

enum {
        HOSTNAME_SELECTED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_xdmcp_chooser_display_class_init    (GdmXdmcpChooserDisplayClass *klass);
static void     gdm_xdmcp_chooser_display_init          (GdmXdmcpChooserDisplay      *xdmcp_chooser_display);
static void     gdm_xdmcp_chooser_display_finalize      (GObject                     *object);
static gboolean gdm_xdmcp_chooser_display_finish (GdmDisplay *display);

G_DEFINE_TYPE (GdmXdmcpChooserDisplay, gdm_xdmcp_chooser_display, GDM_TYPE_XDMCP_DISPLAY)

static void
on_hostname_selected (GdmDBusXdmcpChooserSlave *proxy,
                      const char               *hostname,
                      GdmXdmcpChooserDisplay   *display)
{
        g_debug ("GdmXdmcpChooserDisplay: hostname selected: %s", hostname);
        g_signal_emit (display, signals [HOSTNAME_SELECTED], 0, hostname);
}

static gboolean
gdm_xdmcp_chooser_display_set_slave_bus_name (GdmDisplay *display,
                                              const char *name,
                                              GError    **error)
{
        char            *display_id;
        const char      *slave_num;
        char            *slave_id;
        GDBusConnection *connection;
        GError          *local_error;
        GdmXdmcpChooserDisplay *chooser_display;

        display_id = NULL;
        slave_id = NULL;
        slave_num = NULL;

        chooser_display = GDM_XDMCP_CHOOSER_DISPLAY (display);
        if (chooser_display->priv->slave_proxy != NULL) {
                g_object_unref (chooser_display->priv->slave_proxy);
        }

        g_object_get (display, "id", &display_id, NULL);

        if (g_str_has_prefix (display_id, "/org/gnome/DisplayManager/Display")) {
                slave_num = display_id + strlen ("/org/gnome/DisplayManager/Display");
        }

        slave_id = g_strdup_printf ("/org/gnome/DisplayManager/Slave%s", slave_num);

        local_error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &local_error);
        if (connection == NULL) {
                g_critical ("error getting system bus: %s", local_error->message);
                g_error_free (local_error);
        }

        g_debug ("GdmXdmcpChooserDisplay: creating proxy for %s on %s", slave_id, name);

        chooser_display->priv->slave_proxy = GDM_DBUS_XDMCP_CHOOSER_SLAVE (
                gdm_dbus_xdmcp_chooser_slave_proxy_new_sync (connection,
                                                             G_DBUS_PROXY_FLAGS_NONE,
                                                             name,
                                                             slave_id,
                                                             NULL,
                                                             &local_error));
        if (chooser_display->priv->slave_proxy == NULL) {
                g_warning ("Failed to connect to the slave object: %s", local_error->message);
                g_error_free (local_error);
                goto out;
        }

        g_signal_connect (chooser_display->priv->slave_proxy, "hostname-selected",
                          G_CALLBACK (on_hostname_selected), display);
 out:

        g_free (display_id);
        g_free (slave_id);

        return GDM_DISPLAY_CLASS (gdm_xdmcp_chooser_display_parent_class)->set_slave_bus_name (display, name, error);
}

static gboolean
gdm_xdmcp_chooser_display_manage (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        GDM_DISPLAY_CLASS (gdm_xdmcp_chooser_display_parent_class)->manage (display);

        return TRUE;
}

static GObject *
gdm_xdmcp_chooser_display_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GdmXdmcpChooserDisplay      *display;

        display = GDM_XDMCP_CHOOSER_DISPLAY (G_OBJECT_CLASS (gdm_xdmcp_chooser_display_parent_class)->constructor (type,
                                                                                                           n_construct_properties,
                                                                                                           construct_properties));

        display->priv->skeleton = GDM_DBUS_XDMCP_DISPLAY (gdm_dbus_xdmcp_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->priv->skeleton));

        return G_OBJECT (display);
}

static void
gdm_xdmcp_chooser_display_class_init (GdmXdmcpChooserDisplayClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->constructor = gdm_xdmcp_chooser_display_constructor;
        object_class->finalize = gdm_xdmcp_chooser_display_finalize;

        display_class->manage = gdm_xdmcp_chooser_display_manage;
        display_class->set_slave_bus_name = gdm_xdmcp_chooser_display_set_slave_bus_name;
        display_class->finish = gdm_xdmcp_chooser_display_finish;

        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmXdmcpChooserDisplayClass, hostname_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (GdmXdmcpChooserDisplayPrivate));
}

static void
gdm_xdmcp_chooser_display_init (GdmXdmcpChooserDisplay *xdmcp_chooser_display)
{

        xdmcp_chooser_display->priv = GDM_XDMCP_CHOOSER_DISPLAY_GET_PRIVATE (xdmcp_chooser_display);
}

static void
gdm_xdmcp_chooser_display_finalize (GObject *object)
{
        GdmXdmcpChooserDisplay *chooser_display;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_XDMCP_CHOOSER_DISPLAY (object));

        chooser_display = GDM_XDMCP_CHOOSER_DISPLAY (object);

        g_return_if_fail (chooser_display->priv != NULL);

        g_clear_object (&chooser_display->priv->slave_proxy);
        g_clear_object (&chooser_display->priv->skeleton);

        G_OBJECT_CLASS (gdm_xdmcp_chooser_display_parent_class)->finalize (object);
}

static gboolean
gdm_xdmcp_chooser_display_finish (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        GDM_DISPLAY_CLASS (gdm_xdmcp_chooser_display_parent_class)->finish (display);

        gdm_display_unmanage (display);

        return TRUE;
}

GdmDisplay *
gdm_xdmcp_chooser_display_new (const char              *hostname,
                               int                      number,
                               GdmAddress              *address,
                               gint32                   session_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf ("%s:%d", hostname, number);
        object = g_object_new (GDM_TYPE_XDMCP_CHOOSER_DISPLAY,
                               "slave-command", DEFAULT_SLAVE_COMMAND,
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
