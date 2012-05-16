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
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
#include <sched.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-common.h"
#include "gdm-chooser-server.h"
#include "gdm-dbus-util.h"

#define GDM_CHOOSER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/ChooserServer"
#define GDM_CHOOSER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.ChooserServer"

#define GDM_CHOOSER_SERVER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_CHOOSER_SERVER, GdmChooserServerPrivate))

static const char* chooser_server_introspection =
        "<node>"
        "  <interface name=\"org.gnome.DisplayManager.ChooserServer\">"
        "    <method name=\"SelectHostname\">"
        "      <arg name=\"text\" direction=\"in\" type=\"s\"/>"
        "    </method>"
        "    <method name=\"Disconnect\">"
        "    </method>"
        "  </interface>"
        "</node>";

struct GdmChooserServerPrivate
{
        char           *user_name;
        char           *group_name;
        char           *display_id;

        GDBusServer    *server;
        GDBusConnection *chooser_connection;
};

enum {
        PROP_0,
        PROP_USER_NAME,
        PROP_GROUP_NAME,
        PROP_DISPLAY_ID,
};

enum {
        HOSTNAME_SELECTED,
        CONNECTED,
        DISCONNECTED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_chooser_server_class_init   (GdmChooserServerClass *klass);
static void     gdm_chooser_server_init         (GdmChooserServer      *chooser_server);

G_DEFINE_TYPE (GdmChooserServer, gdm_chooser_server, G_TYPE_OBJECT)

static void
handle_select_hostname (GdmChooserServer      *chooser_server,
                        GDBusMethodInvocation *invocation,
                        GVariant              *parameters)
{
        const char  *text;

        g_variant_get (parameters, "(&s)", &text);

        g_debug ("ChooserServer: SelectHostname: %s", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (chooser_server, signals [HOSTNAME_SELECTED], 0, text);
}

static void
handle_disconnect (GdmChooserServer      *chooser_server,
                   GDBusMethodInvocation *invocation,
                   GVariant              *parameters)
{
        g_debug ("ChooserServer: Disconnect");

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (chooser_server, signals [DISCONNECTED], 0);
}

static void
chooser_handle_child_method (GDBusConnection       *connection,
                             const char            *sender,
                             const char            *object_path,
                             const char            *interface_name,
                             const char            *method_name,
                             GVariant              *parameters,
                             GDBusMethodInvocation *invocation,
                             void                  *user_data)
{
        GdmChooserServer *chooser_server = GDM_CHOOSER_SERVER (user_data);

        if (strcmp (method_name, "SelectHostname") == 0)
                return handle_select_hostname (chooser_server, invocation, parameters);
        else if (strcmp (method_name, "Disconnect") == 0)
                return handle_disconnect (chooser_server, invocation, parameters);
}

static void
connection_closed (GDBusConnection  *connection,
                   GdmChooserServer *chooser_server)
{
        g_clear_object (&chooser_server->priv->chooser_connection);
}

static gboolean
allow_user_function (GDBusAuthObserver *observer,
                     GIOStream         *stream,
                     GCredentials      *credentials,
                     void              *data)
{
        GdmChooserServer *chooser_server = GDM_CHOOSER_SERVER (data);
        struct passwd    *pwent;

        if (chooser_server->priv->user_name == NULL) {
                return FALSE;
        }

        gdm_get_pwent_for_name (chooser_server->priv->user_name, &pwent);
        if (pwent == NULL) {
                return FALSE;
        }

        return g_credentials_get_unix_user (credentials, NULL) == pwent->pw_uid;
}

static void
handle_connection (GDBusServer      *server,
                   GDBusConnection  *new_connection,
                   void             *user_data)
{
        GdmChooserServer *chooser_server = GDM_CHOOSER_SERVER (user_data);

        g_debug ("ChooserServer: Handing new connection");

        if (chooser_server->priv->chooser_connection == NULL) {
                GDBusInterfaceVTable vtable = { chooser_handle_child_method };
                GDBusNodeInfo *info;

                chooser_server->priv->chooser_connection = g_object_ref (new_connection);

                info = g_dbus_node_info_new_for_xml (chooser_server_introspection, NULL);

                g_debug ("GdmChooserServer: new connection %p", new_connection);

                g_dbus_connection_register_object (new_connection,
                                                   "/org/gnome/DisplayManager/ChooserServer",
                                                   info->interfaces[0],
                                                   &vtable,
                                                   NULL, NULL, NULL);

                g_signal_connect (new_connection, "closed",
                                  G_CALLBACK (connection_closed), chooser_server);

                g_signal_emit (chooser_server, signals[CONNECTED], 0);
        }
}

gboolean
gdm_chooser_server_start (GdmChooserServer *chooser_server)
{
        GDBusAuthObserver *observer;
        GError *error = NULL;
        gboolean ret;

        ret = FALSE;

        g_debug ("ChooserServer: Creating D-Bus server for chooser");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect (observer, "authorize-authenticated-peer",
                          G_CALLBACK (allow_user_function), chooser_server);

        chooser_server->priv->server = gdm_dbus_setup_private_server (observer,
                                                                      &error);
        g_object_unref (observer);

        if (chooser_server->priv->server == NULL) {
                g_warning ("Cannot create D-BUS server for the chooser: %s", error->message);
                /* FIXME: should probably fail if we can't create the socket */
                goto out;
        }

        g_signal_connect (chooser_server->priv->server, "new-connection",
                          G_CALLBACK (handle_connection), chooser_server);

        ret = TRUE;

        g_debug ("ChooserServer: D-Bus server listening on %s",
                 g_dbus_server_get_client_address (chooser_server->priv->server));

        g_dbus_server_start (chooser_server->priv->server);

 out:
        return ret;
}

gboolean
gdm_chooser_server_stop (GdmChooserServer *chooser_server)
{
        gboolean ret;

        ret = FALSE;

        g_debug ("ChooserServer: Stopping chooser server...");

        g_clear_object (&chooser_server->priv->server);
        g_clear_object (&chooser_server->priv->chooser_connection);

        return ret;
}

char *
gdm_chooser_server_get_address (GdmChooserServer *chooser_server)
{
        return g_strdup (g_dbus_server_get_client_address (chooser_server->priv->server));
}

static void
_gdm_chooser_server_set_display_id (GdmChooserServer *chooser_server,
                                    const char       *display_id)
{
        g_free (chooser_server->priv->display_id);
        chooser_server->priv->display_id = g_strdup (display_id);
}

static void
_gdm_chooser_server_set_user_name (GdmChooserServer *chooser_server,
                                  const char *name)
{
        g_free (chooser_server->priv->user_name);
        chooser_server->priv->user_name = g_strdup (name);
}

static void
_gdm_chooser_server_set_group_name (GdmChooserServer *chooser_server,
                                    const char *name)
{
        g_free (chooser_server->priv->group_name);
        chooser_server->priv->group_name = g_strdup (name);
}

static void
gdm_chooser_server_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GdmChooserServer *self;

        self = GDM_CHOOSER_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                _gdm_chooser_server_set_display_id (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_chooser_server_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_GROUP_NAME:
                _gdm_chooser_server_set_group_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_chooser_server_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GdmChooserServer *self;

        self = GDM_CHOOSER_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                g_value_set_string (value, self->priv->display_id);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, self->priv->user_name);
                break;
        case PROP_GROUP_NAME:
                g_value_set_string (value, self->priv->group_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_chooser_server_class_init (GdmChooserServerClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_chooser_server_get_property;
        object_class->set_property = gdm_chooser_server_set_property;

        g_type_class_add_private (klass, sizeof (GdmChooserServerPrivate));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_ID,
                                         g_param_spec_string ("display-id",
                                                              "display id",
                                                              "display id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
         g_object_class_install_property (object_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "user name",
                                                              "user name",
                                                              GDM_USERNAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_GROUP_NAME,
                                         g_param_spec_string ("group-name",
                                                              "group name",
                                                              "group name",
                                                              GDM_GROUPNAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserServerClass, hostname_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [CONNECTED] =
                g_signal_new ("connected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserServerClass, connected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserServerClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
}

static void
gdm_chooser_server_init (GdmChooserServer *chooser_server)
{

        chooser_server->priv = GDM_CHOOSER_SERVER_GET_PRIVATE (chooser_server);
}

GdmChooserServer *
gdm_chooser_server_new (const char *display_id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_CHOOSER_SERVER,
                               "display-id", display_id,
                               NULL);

        return GDM_CHOOSER_SERVER (object);
}
