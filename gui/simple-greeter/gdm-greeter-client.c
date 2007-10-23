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
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-greeter-client.h"

#define GDM_GREETER_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_CLIENT, GdmGreeterClientPrivate))

#define SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

struct GdmGreeterClientPrivate
{
        DBusGConnection  *connection;
        DBusGProxy       *server_proxy;
        char             *address;
};

enum {
        PROP_0,
};

enum {
        INFO,
        PROBLEM,
        INFO_QUERY,
        SECRET_INFO_QUERY,
        READY,
        RESET,
        LAST_SIGNAL
};

static guint gdm_greeter_client_signals [LAST_SIGNAL];

static void     gdm_greeter_client_class_init  (GdmGreeterClientClass *klass);
static void     gdm_greeter_client_init        (GdmGreeterClient      *greeter_client);
static void     gdm_greeter_client_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmGreeterClient, gdm_greeter_client, G_TYPE_OBJECT)

static gpointer client_object = NULL;

GQuark
gdm_greeter_client_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0)
                error_quark = g_quark_from_static_string ("gdm-greeter-client");

        return error_quark;
}

static void
on_info (DBusGProxy       *proxy,
         const char       *text,
         GdmGreeterClient *client)
{
        g_debug ("GREETER INFO: %s", text);

        g_signal_emit (client,
                       gdm_greeter_client_signals[INFO],
                       0, text);
}

static void
on_problem (DBusGProxy       *proxy,
            const char       *text,
            GdmGreeterClient *client)
{
        g_debug ("GREETER PROBLEM: %s", text);

        g_signal_emit (client,
                       gdm_greeter_client_signals[PROBLEM],
                       0, text);
}

static void
on_ready (DBusGProxy       *proxy,
          GdmGreeterClient *client)
{
        g_debug ("GREETER SERVER READY");

        g_signal_emit (client,
                       gdm_greeter_client_signals[READY],
                       0);
}

static void
on_reset (DBusGProxy       *proxy,
          GdmGreeterClient *client)
{
        g_debug ("GREETER RESET");

        g_signal_emit (client,
                       gdm_greeter_client_signals[RESET],
                       0);
}

static void
on_info_query (DBusGProxy       *proxy,
               const char       *text,
               GdmGreeterClient *client)
{
        g_debug ("GREETER Info query: %s", text);

        g_signal_emit (client,
                       gdm_greeter_client_signals[INFO_QUERY],
                       0, text);
}

static void
on_secret_info_query (DBusGProxy       *proxy,
                      const char       *text,
                      GdmGreeterClient *client)
{
        g_debug ("GREETER Secret info query: %s", text);

        g_signal_emit (client,
                       gdm_greeter_client_signals[SECRET_INFO_QUERY],
                       0, text);
}

void
gdm_greeter_client_call_begin_verification (GdmGreeterClient *client,
                                            const char       *username)
{
        gboolean res;
        GError  *error;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        g_debug ("GREETER begin verification");

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "BeginVerification",
                                 &error,
                                 G_TYPE_STRING, username,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send BeginVerification: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_answer_query (GdmGreeterClient *client,
                                      const char       *text)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER answer");

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "AnswerQuery",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send AnswerQuery: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_select_session (GdmGreeterClient *client,
                                        const char       *text)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER client selected: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "SelectSession",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SelectSession: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_select_language (GdmGreeterClient *client,
                                         const char       *text)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER client selected: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "SelectLanguage",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SelectLanguage: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_select_user (GdmGreeterClient *client,
                                     const char       *text)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER user selected: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "SelectUser",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SelectUser: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_select_hostname (GdmGreeterClient *client,
                                         const char       *text)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER hostname selected: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "SelectHostname",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SelectHostname: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_cancel (GdmGreeterClient *client)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER cancelled");

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "Cancel",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send Cancelled: %s", error->message);
                g_error_free (error);
        }
}

void
gdm_greeter_client_call_disconnect (GdmGreeterClient *client)
{
        gboolean res;
        GError  *error;

        g_debug ("GREETER disconnected");

        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "Disconnect",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send Disconnected: %s", error->message);
                g_error_free (error);
        }
}

char *
gdm_greeter_client_call_get_display_id (GdmGreeterClient *client)
{
        gboolean res;
        GError  *error;
        char    *id;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), NULL);

        id = NULL;
        error = NULL;
        res = dbus_g_proxy_call (client->priv->server_proxy,
                                 "GetDisplayId",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &id,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to GetDisplayId: %s", error->message);
                g_error_free (error);
        }

        return id;
}

static void
proxy_destroyed (GObject *object,
                 gpointer data)
{
        g_debug ("GREETER Proxy disconnected");
}

gboolean
gdm_greeter_client_start (GdmGreeterClient *client,
                          GError           **error)
{
        gboolean ret;
        GError  *local_error;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        ret = FALSE;

        if (client->priv->address == NULL) {
                g_warning ("GDM_GREETER_DBUS_ADDRESS not set");
                g_set_error (error,
                             GDM_GREETER_CLIENT_ERROR,
                             GDM_GREETER_CLIENT_ERROR_GENERIC,
                             "GDM_GREETER_DBUS_ADDRESS not set");
                goto out;
        }

        g_debug ("GREETER connecting to address: %s", client->priv->address);

        local_error = NULL;
        client->priv->connection = dbus_g_connection_open (client->priv->address, &local_error);
        if (client->priv->connection == NULL) {
                if (local_error != NULL) {
                        g_warning ("error opening connection: %s", local_error->message);
                        g_propagate_error (error, local_error);
                } else {
                        g_warning ("Unable to open connection");
                }
                goto out;
        }

        g_debug ("GREETER creating proxy for peer: %s", SERVER_DBUS_PATH);
        client->priv->server_proxy = dbus_g_proxy_new_for_peer (client->priv->connection,
                                                                SERVER_DBUS_PATH,
                                                                SERVER_DBUS_INTERFACE);
        if (client->priv->server_proxy == NULL) {
                g_warning ("Unable to create proxy for peer");
                g_set_error (error,
                             GDM_GREETER_CLIENT_ERROR,
                             GDM_GREETER_CLIENT_ERROR_GENERIC,
                             "Unable to create proxy for peer");

                /* FIXME: drop connection? */
                goto out;
        }

        g_signal_connect (client->priv->server_proxy, "destroy", G_CALLBACK (proxy_destroyed), NULL);

        /* FIXME: not sure why introspection isn't working */
        dbus_g_proxy_add_signal (client->priv->server_proxy, "InfoQuery", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (client->priv->server_proxy, "SecretInfoQuery", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (client->priv->server_proxy, "Info", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (client->priv->server_proxy, "Problem", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (client->priv->server_proxy, "Ready", G_TYPE_INVALID);
        dbus_g_proxy_add_signal (client->priv->server_proxy, "Reset", G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "InfoQuery",
                                     G_CALLBACK (on_info_query),
                                     NULL,
                                     NULL);
        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "SecretInfoQuery",
                                     G_CALLBACK (on_secret_info_query),
                                     NULL,
                                     NULL);
        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "Info",
                                     G_CALLBACK (on_info),
                                     NULL,
                                     NULL);
        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "Problem",
                                     G_CALLBACK (on_problem),
                                     NULL,
                                     NULL);
        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "Ready",
                                     G_CALLBACK (on_ready),
                                     NULL,
                                     NULL);
        dbus_g_proxy_connect_signal (client->priv->server_proxy,
                                     "Reset",
                                     G_CALLBACK (on_reset),
                                     NULL,
                                     NULL);
        ret = TRUE;

 out:
        return ret;
}

void
gdm_greeter_client_stop (GdmGreeterClient *client)
{
        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

}

static void
gdm_greeter_client_set_property (GObject        *object,
                                 guint           prop_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
        GdmGreeterClient *self;

        self = GDM_GREETER_CLIENT (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_client_get_property (GObject        *object,
                                 guint           prop_id,
                                 GValue         *value,
                                 GParamSpec     *pspec)
{
        GdmGreeterClient *self;

        self = GDM_GREETER_CLIENT (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_greeter_client_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        GdmGreeterClient      *greeter_client;
        GdmGreeterClientClass *klass;

        klass = GDM_GREETER_CLIENT_CLASS (g_type_class_peek (GDM_TYPE_GREETER_CLIENT));

        greeter_client = GDM_GREETER_CLIENT (G_OBJECT_CLASS (gdm_greeter_client_parent_class)->constructor (type,
                                                                                                            n_construct_properties,
                                                                                                            construct_properties));

        return G_OBJECT (greeter_client);
}

static void
gdm_greeter_client_dispose (GObject *object)
{
        GdmGreeterClient *greeter_client;

        greeter_client = GDM_GREETER_CLIENT (object);

        g_debug ("Disposing greeter_client");

        G_OBJECT_CLASS (gdm_greeter_client_parent_class)->dispose (object);
}

static void
gdm_greeter_client_class_init (GdmGreeterClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_greeter_client_get_property;
        object_class->set_property = gdm_greeter_client_set_property;
        object_class->constructor = gdm_greeter_client_constructor;
        object_class->dispose = gdm_greeter_client_dispose;
        object_class->finalize = gdm_greeter_client_finalize;

        g_type_class_add_private (klass, sizeof (GdmGreeterClientPrivate));

        gdm_greeter_client_signals[INFO_QUERY] =
                g_signal_new ("info-query",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterClientClass, info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        gdm_greeter_client_signals[SECRET_INFO_QUERY] =
                g_signal_new ("secret-info-query",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterClientClass, secret_info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        gdm_greeter_client_signals[INFO] =
                g_signal_new ("info",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterClientClass, info),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        gdm_greeter_client_signals[PROBLEM] =
                g_signal_new ("problem",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterClientClass, problem),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        gdm_greeter_client_signals[READY] =
                g_signal_new ("ready",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterClientClass, ready),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
}

static void
gdm_greeter_client_init (GdmGreeterClient *client)
{

        client->priv = GDM_GREETER_CLIENT_GET_PRIVATE (client);

        client->priv->address = g_strdup (g_getenv ("GDM_GREETER_DBUS_ADDRESS"));
}

static void
gdm_greeter_client_finalize (GObject *object)
{
        GdmGreeterClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_CLIENT (object));

        client = GDM_GREETER_CLIENT (object);

        g_return_if_fail (client->priv != NULL);

        g_free (client->priv->address);

        G_OBJECT_CLASS (gdm_greeter_client_parent_class)->finalize (object);
}

GdmGreeterClient *
gdm_greeter_client_new (void)
{
        if (client_object != NULL) {
                g_object_ref (client_object);
        } else {
                client_object = g_object_new (GDM_TYPE_GREETER_CLIENT, NULL);
                g_object_add_weak_pointer (client_object,
                                           (gpointer *) &client_object);
        }

        return GDM_GREETER_CLIENT (client_object);
}
