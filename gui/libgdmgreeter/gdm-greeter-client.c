/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include "gdm-greeter-client.h"
#include "gdm-client-glue.h"
#include "gdm-manager-glue.h"

#define GDM_GREETER_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_CLIENT, GdmGreeterClientPrivate))

#define SESSION_DBUS_PATH      "/org/gnome/DisplayManager/Session"

struct GdmGreeterClientPrivate
{
        GdmManager         *manager;
        GdmUserVerifier    *user_verifier;
        GdmGreeter         *greeter;
        GdmRemoteGreeter   *remote_greeter;
        GdmChooser         *chooser;
        GDBusConnection    *connection;
        char               *address;

        GList              *pending_opens;
};

static void     gdm_greeter_client_class_init  (GdmGreeterClientClass *klass);
static void     gdm_greeter_client_init        (GdmGreeterClient *client);
static void     gdm_greeter_client_finalize    (GObject        *object);

G_DEFINE_TYPE (GdmGreeterClient, gdm_greeter_client, G_TYPE_OBJECT);

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
on_got_manager (GdmManager          *manager,
                GAsyncResult        *result,
                GSimpleAsyncResult  *operation_result)
{
        GdmGreeterClient *client;
        GdmManager       *new_manager;
        GError           *error;

        client = GDM_GREETER_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

        error = NULL;
        new_manager = gdm_manager_proxy_new_finish (result, &error);

        if (client->priv->manager == NULL) {
                client->priv->manager = new_manager;

        } else {
                g_object_ref (client->priv->manager);
                g_object_unref (new_manager);

                g_clear_error (&error);
        }

        if (error != NULL) {
                g_simple_async_result_take_error (operation_result, error);
        } else {
                g_simple_async_result_set_op_res_gpointer (operation_result,
                                                           g_object_ref (client->priv->manager),
                                                           (GDestroyNotify)
                                                           g_object_unref);
        }

        g_simple_async_result_complete_in_idle (operation_result);
}

static void
get_manager (GdmGreeterClient    *client,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
        GSimpleAsyncResult *result;

        result = g_simple_async_result_new (G_OBJECT (client),
                                            callback,
                                            user_data,
                                            get_manager);
        g_simple_async_result_set_check_cancellable (result, cancellable);

        if (client->priv->manager != NULL) {
                g_simple_async_result_set_op_res_gpointer (result,
                                                           g_object_ref (client->priv->manager),
                                                           (GDestroyNotify)
                                                           g_object_unref);
                g_simple_async_result_complete_in_idle (result);
                return;
        }

        gdm_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       "org.gnome.DisplayManager",
                                       "/org/gnome/DisplayManager/Manager",
                                       cancellable,
                                       (GAsyncReadyCallback)
                                       on_got_manager,
                                       result);
}

static void
on_user_verifier_proxy_created (GdmUserVerifier    *user_verifier,
                                GAsyncResult       *result,
                                GSimpleAsyncResult *operation_result)
{

        GError       *error;

        if (!gdm_user_verifier_proxy_new_finish (result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        g_debug ("UserVerifier %p created", user_verifier);

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   g_object_ref (user_verifier),
                                                   (GDestroyNotify)
                                                   g_object_unref);
        g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_reauthentication_channel_connected (GDBusConnection    *connection,
                                       GAsyncResult       *result,
                                       GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!g_dbus_connection_new_for_address_finish (result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_user_verifier_proxy_new (connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     SESSION_DBUS_PATH,
                                     cancellable,
                                     (GAsyncReadyCallback)
                                     on_user_verifier_proxy_created,
                                     operation_result);
}

static void
on_reauthentication_channel_opened (GdmManager         *manager,
                                    GAsyncResult       *result,
                                    GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        char         *address;
        GError       *error;

        error = NULL;
        if (!gdm_manager_call_open_reauthentication_channel_finish (manager,
                                                                    &address,
                                                                    result,
                                                                    &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        g_dbus_connection_new_for_address (address,
                                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                           NULL,
                                           cancellable,
                                           (GAsyncReadyCallback)
                                           on_reauthentication_channel_connected,
                                           operation_result);
}

static void
on_got_manager_for_reauthentication (GdmGreeterClient    *client,
                                     GAsyncResult        *result,
                                     GSimpleAsyncResult  *operation_result)
{
        GCancellable *cancellable;
        char         *username;
        GError       *error;

        error = NULL;
        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                   &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        username = g_object_get_data (G_OBJECT (operation_result), "username");
        gdm_manager_call_open_reauthentication_channel (client->priv->manager,
                                                        username,
                                                        cancellable,
                                                        (GAsyncReadyCallback)
                                                        on_reauthentication_channel_opened,
                                                        operation_result);

}

static gboolean
gdm_greeter_client_open_connection_sync (GdmGreeterClient *client,
                                 GCancellable   *cancellable,
                                 GError        **error)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->manager == NULL) {
                client->priv->manager = gdm_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                            G_DBUS_PROXY_FLAGS_NONE,
                                                                            "org.gnome.DisplayManager",
                                                                            "/org/gnome/DisplayManager/Manager",
                                                                            cancellable,
                                                                            error);

                if (client->priv->manager == NULL) {
                        goto out;
                }
        } else {
                client->priv->manager = g_object_ref (client->priv->manager);
        }

        if (client->priv->connection == NULL) {
                ret = gdm_manager_call_open_session_sync (client->priv->manager,
                                                          &client->priv->address,
                                                          cancellable,
                                                          error);

                if (!ret) {
                        g_clear_object (&client->priv->manager);
                        goto out;
                }

                g_debug ("GdmGreeterClient: connecting to address: %s", client->priv->address);

                client->priv->connection = g_dbus_connection_new_for_address_sync (client->priv->address,
                                                                                   G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                                   NULL,
                                                                                   cancellable,
                                                                                   error);

                if (client->priv->connection == NULL) {
                        g_clear_object (&client->priv->manager);
                        g_clear_pointer (&client->priv->address, g_free);
                        goto out;
                }
        } else {
                client->priv->connection = g_object_ref (client->priv->connection);
        }

 out:
        return client->priv->connection != NULL;
}

static void
on_connected (GDBusConnection    *connection,
              GAsyncResult       *result,
              GSimpleAsyncResult *operation_result)
{
        GError *error;

        error = NULL;
        if (!g_dbus_connection_new_for_address_finish (result,
                                                       &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   g_object_ref (connection),
                                                   (GDestroyNotify)
                                                   g_object_unref);
        g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_session_opened (GdmManager         *manager,
                   GAsyncResult       *result,
                   GSimpleAsyncResult *operation_result)
{
        GdmGreeterClient *client;
        GCancellable     *cancellable;
        GError           *error;

        client = GDM_GREETER_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

        error = NULL;
        if (!gdm_manager_call_open_session_finish (manager,
                                                   &client->priv->address,
                                                   result,
                                                   &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        g_dbus_connection_new_for_address (client->priv->address,
                                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                           NULL,
                                           cancellable,
                                           (GAsyncReadyCallback)
                                           on_connected,
                                           operation_result);
}

static void
on_got_manager_for_opening_connection (GdmGreeterClient    *client,
                                       GAsyncResult        *result,
                                       GSimpleAsyncResult  *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                   &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_manager_call_open_session (client->priv->manager,
                                       cancellable,
                                       (GAsyncReadyCallback)
                                       on_session_opened,
                                       operation_result);
}

static void
finish_pending_opens (GdmGreeterClient *client,
                      GError    *error)
{
    GList *node;

    for (node = client->priv->pending_opens;
         node != NULL;
         node = node->next) {

        GSimpleAsyncResult *pending_result = node->data;

        g_simple_async_result_set_from_error (pending_result, error);
        g_simple_async_result_complete_in_idle (pending_result);
        g_object_unref (pending_result);
    }
    g_clear_pointer (&client->priv->pending_opens,
                     (GDestroyNotify) g_list_free);
}

static gboolean
gdm_greeter_client_open_connection_finish (GdmGreeterClient *client,
                                   GAsyncResult   *result,
                                   GError        **error)
{
        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                   error)) {
            finish_pending_opens (client, *error);
            return FALSE;
        }

        if (client->priv->connection == NULL) {
                client->priv->connection = g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result)));
        }

        finish_pending_opens (client, NULL);
        return TRUE;
}

static void
gdm_greeter_client_open_connection (GdmGreeterClient    *client,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_open_connection);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);

        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        if (client->priv->connection != NULL) {
            g_simple_async_result_set_op_res_gpointer (operation_result,
                                                       g_object_ref (client->priv->connection),
                                                       (GDestroyNotify)
                                                       g_object_unref);
            g_simple_async_result_complete_in_idle (operation_result);
            return;
        }

        if (client->priv->pending_opens == NULL) {
            get_manager (client,
                         cancellable,
                         (GAsyncReadyCallback)
                         on_got_manager_for_opening_connection,
                         operation_result);
        } else {
                client->priv->pending_opens = g_list_prepend (client->priv->pending_opens,
                                                              operation_result);
        }

}

/**
 * gdm_greeter_client_open_reauthentication_channel_sync:
 * @client: a #GdmGreeterClient
 * @username: user to reauthenticate
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmUserVerifier object that can be used to
 * reauthenticate an already logged in user. Free with
 * g_object_unref to close reauthentication channel.
 *
 * Returns: (transfer full): #GdmUserVerifier or %NULL if @username is not
 * already logged in.
 */
GdmUserVerifier *
gdm_greeter_client_open_reauthentication_channel_sync (GdmGreeterClient     *client,
                                               const char    *username,
                                               GCancellable  *cancellable,
                                               GError       **error)
{
        GDBusConnection *connection;
        GdmUserVerifier *user_verifier = NULL;
        gboolean         ret;
        char            *address;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->manager == NULL) {
                client->priv->manager = gdm_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                            G_DBUS_PROXY_FLAGS_NONE,
                                                                            "org.gnome.DisplayManager",
                                                                            "/org/gnome/DisplayManager/Manager",
                                                                            cancellable,
                                                                            error);

                if (client->priv->manager == NULL) {
                        goto out;
                }
        } else {
                client->priv->manager = g_object_ref (client->priv->manager);
        }

        ret = gdm_manager_call_open_reauthentication_channel_sync (client->priv->manager,
                                                                   username,
                                                                   &address,
                                                                   cancellable,
                                                                   error);

        if (!ret) {
                goto out;
        }

        g_debug ("GdmGreeterClient: connecting to address: %s", client->priv->address);

        connection = g_dbus_connection_new_for_address_sync (address,
                                                             G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                             NULL,
                                                             cancellable,
                                                             error);

        if (connection == NULL) {
                g_free (address);
                goto out;
        }
        g_free (address);

        user_verifier = gdm_user_verifier_proxy_new_sync (connection,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          NULL,
                                                          SESSION_DBUS_PATH,
                                                          cancellable,
                                                          error);

        if (user_verifier != NULL) {
                g_object_weak_ref (G_OBJECT (user_verifier),
                                   (GWeakNotify)
                                   g_object_unref,
                                   connection);

                g_object_weak_ref (G_OBJECT (user_verifier),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->manager);
        }

 out:
        return user_verifier;
}

/**
 * gdm_greeter_client_open_reauthentication_channel:
 * @client: a #GdmGreeterClient
 * @username: user to reauthenticate
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmUserVerifier object that can be used to
 * reauthenticate an already logged in user.
 */
void
gdm_greeter_client_open_reauthentication_channel (GdmGreeterClient    *client,
                                                  const char          *username,
                                                  GCancellable        *cancellable,
                                                  GAsyncReadyCallback  callback,
                                                  gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_open_reauthentication_channel);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);
        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        g_object_set_data_full (G_OBJECT (operation_result),
                                "username",
                                g_strdup (username),
                                (GDestroyNotify)
                                g_free);

        get_manager (client,
                     cancellable,
                     (GAsyncReadyCallback)
                     on_got_manager_for_reauthentication,
                     operation_result);
}

/**
 * gdm_greeter_client_open_reauthentication_channel_finish:
 * @client: a #GdmGreeterClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_greeter_client_open_reauthentication_channel().
 *
 * Returns: (transfer full):  a #GdmUserVerifier
 */
GdmUserVerifier *
gdm_greeter_client_open_reauthentication_channel_finish (GdmGreeterClient  *client,
                                                 GAsyncResult    *result,
                                                 GError         **error)
{
        GdmUserVerifier *user_verifier;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                   error)) {
                return NULL;
        }

        user_verifier = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        return g_object_ref (user_verifier);
}

/**
 * gdm_greeter_client_get_user_verifier_sync:
 * @client: a #GdmGreeterClient
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmUserVerifier object that can be used to
 * verify a user's local account.
 *
 * Returns: (transfer full): #GdmUserVerifier or %NULL if not connected
 */
GdmUserVerifier *
gdm_greeter_client_get_user_verifier_sync (GdmGreeterClient     *client,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
        if (client->priv->user_verifier != NULL) {
                return g_object_ref (client->priv->user_verifier);
        }

        if (!gdm_greeter_client_open_connection_sync (client, cancellable, error)) {
                return NULL;
        }

        client->priv->user_verifier = gdm_user_verifier_proxy_new_sync (client->priv->connection,
                                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                                        NULL,
                                                                        SESSION_DBUS_PATH,
                                                                        cancellable,
                                                                        error);

        if (client->priv->user_verifier != NULL) {
                g_object_add_weak_pointer (G_OBJECT (client->priv->user_verifier),
                                           (gpointer *)
                                           &client->priv->user_verifier);
                g_object_weak_ref (G_OBJECT (client->priv->user_verifier),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->manager);
                g_object_weak_ref (G_OBJECT (client->priv->user_verifier),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->connection);
        }

        return client->priv->user_verifier;
}

static void
on_connection_opened_for_user_verifier (GdmGreeterClient     *client,
                                        GAsyncResult       *result,
                                        GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_greeter_client_open_connection_finish (client, result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_user_verifier_proxy_new (client->priv->connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     SESSION_DBUS_PATH,
                                     cancellable,
                                     (GAsyncReadyCallback)
                                     on_user_verifier_proxy_created,
                                     operation_result);
}

/**
 * gdm_greeter_client_get_user_verifier:
 * @client: a #GdmGreeterClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmUserVerifier object that can be used to
 * verify a user's local account.
 */
void
gdm_greeter_client_get_user_verifier (GdmGreeterClient    *client,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_get_user_verifier);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);

        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        if (client->priv->user_verifier != NULL) {
                g_simple_async_result_set_op_res_gpointer (operation_result,
                                                           g_object_ref (client->priv->user_verifier),
                                                           (GDestroyNotify)
                                                           g_object_unref);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        gdm_greeter_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_user_verifier,
                                    operation_result);
}

/**
 * gdm_greeter_client_get_user_verifier_finish:
 * @client: a #GdmGreeterClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_greeter_client_get_user_verifier().
 *
 * Returns: (transfer full): a #GdmUserVerifier
 */
GdmUserVerifier *
gdm_greeter_client_get_user_verifier_finish (GdmGreeterClient  *client,
                                     GAsyncResult    *result,
                                     GError         **error)
{
        GdmUserVerifier *user_verifier;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->user_verifier != NULL) {
                return g_object_ref (client->priv->user_verifier);
        } else if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                          error)) {
                return NULL;
        }

        user_verifier = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        client->priv->user_verifier = user_verifier;

        g_object_add_weak_pointer (G_OBJECT (client->priv->user_verifier),
                                   (gpointer *)
                                   &client->priv->user_verifier);

        g_object_weak_ref (G_OBJECT (client->priv->user_verifier),
                           (GWeakNotify)
                           g_object_unref,
                           client->priv->connection);

        g_object_weak_ref (G_OBJECT (client->priv->user_verifier),
                           (GWeakNotify)
                           g_clear_object,
                           &client->priv->manager);

        return g_object_ref (user_verifier);
}

static void
on_greeter_proxy_created (GdmGreeter         *greeter,
                          GAsyncResult       *result,
                          GSimpleAsyncResult *operation_result)
{

        GError       *error;

        if (!gdm_greeter_proxy_new_finish (result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   g_object_ref (greeter),
                                                   (GDestroyNotify)
                                                   g_object_unref);
        g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_connection_opened_for_greeter (GdmGreeterClient     *client,
                                  GAsyncResult       *result,
                                  GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_greeter_client_open_connection_finish (client, result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_greeter_proxy_new (client->priv->connection,
                               G_DBUS_PROXY_FLAGS_NONE,
                               NULL,
                               SESSION_DBUS_PATH,
                               cancellable,
                               (GAsyncReadyCallback)
                               on_greeter_proxy_created,
                               operation_result);
}

/**
 * gdm_greeter_client_get_greeter:
 * @client: a #GdmGreeterClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmGreeter object that can be used to
 * verify a user's local account.
 */
void
gdm_greeter_client_get_greeter (GdmGreeterClient    *client,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_get_greeter);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);

        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        if (client->priv->greeter != NULL) {
                g_simple_async_result_set_op_res_gpointer (operation_result,
                                                           g_object_ref (client->priv->greeter),
                                                           (GDestroyNotify)
                                                           g_object_unref);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        gdm_greeter_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_greeter,
                                    operation_result);
}

/**
 * gdm_greeter_client_get_greeter_finish:
 * @client: a #GdmGreeterClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_greeter_client_get_greeter().
 *
 * Returns: (transfer full): a #GdmGreeter
 */
GdmGreeter *
gdm_greeter_client_get_greeter_finish (GdmGreeterClient  *client,
                               GAsyncResult    *result,
                               GError         **error)
{
        GdmGreeter *greeter;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->greeter != NULL) {
                return g_object_ref (client->priv->greeter);
        } else if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                          error)) {
                return NULL;
        }

        greeter = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        client->priv->greeter = greeter;

        g_object_add_weak_pointer (G_OBJECT (client->priv->greeter),
                                   (gpointer *)
                                   &client->priv->greeter);

        g_object_weak_ref (G_OBJECT (client->priv->greeter),
                           (GWeakNotify)
                           g_object_unref,
                           client->priv->connection);

        g_object_weak_ref (G_OBJECT (client->priv->greeter),
                           (GWeakNotify)
                           g_clear_object,
                           &client->priv->manager);

        return g_object_ref (greeter);
}

/**
 * gdm_greeter_client_get_greeter_sync:
 * @client: a #GdmGreeterClient
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmGreeter object that can be used
 * to do do various login screen related tasks, such
 * as selecting a users session, and starting that
 * session.
 *
 * Returns: (transfer full): #GdmGreeter or %NULL if caller is not a greeter
 */
GdmGreeter *
gdm_greeter_client_get_greeter_sync (GdmGreeterClient     *client,
                             GCancellable  *cancellable,
                             GError       **error)
{
        if (client->priv->greeter != NULL) {
                return g_object_ref (client->priv->greeter);
        }

        if (!gdm_greeter_client_open_connection_sync (client, cancellable, error)) {
                return NULL;
        }

        client->priv->greeter = gdm_greeter_proxy_new_sync (client->priv->connection,
                                                            G_DBUS_PROXY_FLAGS_NONE,
                                                            NULL,
                                                            SESSION_DBUS_PATH,
                                                            cancellable,
                                                            error);

        if (client->priv->greeter != NULL) {
                g_object_add_weak_pointer (G_OBJECT (client->priv->greeter),
                                           (gpointer *)
                                           &client->priv->greeter);
                g_object_weak_ref (G_OBJECT (client->priv->greeter),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->manager);
                g_object_weak_ref (G_OBJECT (client->priv->greeter),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->connection);
        }

        return client->priv->greeter;
}

static void
on_remote_greeter_proxy_created (GdmRemoteGreeter   *remote_greeter,
                                 GAsyncResult       *result,
                                 GSimpleAsyncResult *operation_result)
{

        GError       *error;

        if (!gdm_remote_greeter_proxy_new_finish (result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   g_object_ref (remote_greeter),
                                                   (GDestroyNotify)
                                                   g_object_unref);
        g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_connection_opened_for_remote_greeter (GdmGreeterClient     *client,
                                         GAsyncResult       *result,
                                         GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_greeter_client_open_connection_finish (client, result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_remote_greeter_proxy_new (client->priv->connection,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      NULL,
                                      SESSION_DBUS_PATH,
                                      cancellable,
                                      (GAsyncReadyCallback)
                                      on_remote_greeter_proxy_created,
                                      operation_result);
}

/**
 * gdm_greeter_client_get_remote_greeter:
 * @client: a #GdmGreeterClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmRemoteGreeter object that can be used to
 * verify a user's local account.
 */
void
gdm_greeter_client_get_remote_greeter (GdmGreeterClient    *client,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_get_remote_greeter);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);

        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        if (client->priv->remote_greeter != NULL) {
                g_simple_async_result_set_op_res_gpointer (operation_result,
                                                           g_object_ref (client->priv->remote_greeter),
                                                           (GDestroyNotify)
                                                           g_object_unref);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        gdm_greeter_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_remote_greeter,
                                    operation_result);
}

/**
 * gdm_greeter_client_get_remote_greeter_finish:
 * @client: a #GdmGreeterClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_greeter_client_get_remote_greeter().
 *
 * Returns: (transfer full): a #GdmRemoteGreeter
 */
GdmRemoteGreeter *
gdm_greeter_client_get_remote_greeter_finish (GdmGreeterClient     *client,
                                      GAsyncResult  *result,
                                      GError       **error)
{
        GdmRemoteGreeter *remote_greeter;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->remote_greeter != NULL) {
                return g_object_ref (client->priv->remote_greeter);
        } else if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                          error)) {
                return NULL;
        }

        remote_greeter = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        client->priv->remote_greeter = remote_greeter;

        g_object_add_weak_pointer (G_OBJECT (client->priv->remote_greeter),
                                   (gpointer *)
                                   &client->priv->remote_greeter);

        g_object_weak_ref (G_OBJECT (client->priv->remote_greeter),
                           (GWeakNotify)
                           g_object_unref,
                           client->priv->connection);

        g_object_weak_ref (G_OBJECT (client->priv->remote_greeter),
                           (GWeakNotify)
                           g_clear_object,
                           &client->priv->manager);

        return g_object_ref (remote_greeter);
}

/**
 * gdm_greeter_client_get_remote_greeter_sync:
 * @client: a #GdmGreeterClient
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmRemoteGreeter object that can be used
 * to do do various remote login screen related tasks,
 * such as disconnecting.
 *
 * Returns: (transfer full): #GdmRemoteGreeter or %NULL if caller is not remote
 */
GdmRemoteGreeter *
gdm_greeter_client_get_remote_greeter_sync (GdmGreeterClient     *client,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
        if (client->priv->remote_greeter != NULL) {
                return g_object_ref (client->priv->remote_greeter);
        }

        if (!gdm_greeter_client_open_connection_sync (client, cancellable, error)) {
                return NULL;
        }

        client->priv->remote_greeter = gdm_remote_greeter_proxy_new_sync (client->priv->connection,
                                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                                          NULL,
                                                                          SESSION_DBUS_PATH,
                                                                          cancellable,
                                                                          error);

        if (client->priv->remote_greeter != NULL) {
                g_object_add_weak_pointer (G_OBJECT (client->priv->remote_greeter),
                                           (gpointer *)
                                           &client->priv->remote_greeter);
                g_object_weak_ref (G_OBJECT (client->priv->remote_greeter),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->manager);
                g_object_weak_ref (G_OBJECT (client->priv->remote_greeter),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->connection);
        }

        return client->priv->remote_greeter;
}

static void
on_chooser_proxy_created (GdmChooser         *chooser,
                          GAsyncResult       *result,
                          GSimpleAsyncResult *operation_result)
{

        GError       *error;

        if (!gdm_chooser_proxy_new_finish (result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   g_object_ref (chooser),
                                                   (GDestroyNotify)
                                                   g_object_unref);
        g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_connection_opened_for_chooser (GdmGreeterClient     *client,
                                  GAsyncResult       *result,
                                  GSimpleAsyncResult *operation_result)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_greeter_client_open_connection_finish (client, result, &error)) {
                g_simple_async_result_take_error (operation_result, error);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
        gdm_chooser_proxy_new (client->priv->connection,
                               G_DBUS_PROXY_FLAGS_NONE,
                               NULL,
                               SESSION_DBUS_PATH,
                               cancellable,
                               (GAsyncReadyCallback)
                               on_chooser_proxy_created,
                               operation_result);
}

/**
 * gdm_greeter_client_get_chooser:
 * @client: a #GdmGreeterClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmChooser object that can be used to
 * verify a user's local account.
 */
void
gdm_greeter_client_get_chooser (GdmGreeterClient    *client,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
        GSimpleAsyncResult *operation_result;

        g_return_if_fail (GDM_IS_GREETER_CLIENT (client));

        operation_result = g_simple_async_result_new (G_OBJECT (client),
                                                      callback,
                                                      user_data,
                                                      gdm_greeter_client_get_chooser);
        g_simple_async_result_set_check_cancellable (operation_result, cancellable);

        g_object_set_data (G_OBJECT (operation_result),
                           "cancellable",
                           cancellable);

        if (client->priv->chooser != NULL) {
                g_simple_async_result_set_op_res_gpointer (operation_result,
                                                           g_object_ref (client->priv->chooser),
                                                           (GDestroyNotify)
                                                           g_object_unref);
                g_simple_async_result_complete_in_idle (operation_result);
                return;
        }

        gdm_greeter_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_chooser,
                                    operation_result);
}

/**
 * gdm_greeter_client_get_chooser_finish:
 * @client: a #GdmGreeterClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_greeter_client_get_chooser().
 *
 * Returns: (transfer full): a #GdmChooser
 */
GdmChooser *
gdm_greeter_client_get_chooser_finish (GdmGreeterClient  *client,
                                       GAsyncResult      *result,
                                       GError           **error)
{
        GdmChooser *chooser;

        g_return_val_if_fail (GDM_IS_GREETER_CLIENT (client), FALSE);

        if (client->priv->chooser != NULL) {
                return g_object_ref (client->priv->chooser);
        } else if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                          error)) {
                return NULL;
        }

        chooser = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        client->priv->chooser = chooser;

        g_object_add_weak_pointer (G_OBJECT (client->priv->chooser),
                                   (gpointer *)
                                   &client->priv->chooser);

        g_object_weak_ref (G_OBJECT (client->priv->chooser),
                           (GWeakNotify)
                           g_object_unref,
                           client->priv->connection);

        g_object_weak_ref (G_OBJECT (client->priv->chooser),
                           (GWeakNotify)
                           g_clear_object,
                           &client->priv->manager);

        return g_object_ref (chooser);
}

/**
 * gdm_greeter_client_get_chooser_sync:
 * @client: a #GdmGreeterClient
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmChooser object that can be used
 * to do do various XDMCP chooser related tasks, such
 * as selecting a host or disconnecting.
 *
 * Returns: (transfer full): #GdmChooser or %NULL if caller is not a chooser
 */
GdmChooser *
gdm_greeter_client_get_chooser_sync (GdmGreeterClient     *client,
                             GCancellable  *cancellable,
                             GError       **error)
{

        if (client->priv->chooser != NULL) {
                return g_object_ref (client->priv->chooser);
        }

        if (!gdm_greeter_client_open_connection_sync (client, cancellable, error)) {
                return NULL;
        }

        client->priv->chooser = gdm_chooser_proxy_new_sync (client->priv->connection,
                                                            G_DBUS_PROXY_FLAGS_NONE,
                                                            NULL,
                                                            SESSION_DBUS_PATH,
                                                            cancellable,
                                                            error);

        if (client->priv->chooser != NULL) {
                g_object_add_weak_pointer (G_OBJECT (client->priv->chooser),
                                           (gpointer *)
                                           &client->priv->chooser);
                g_object_weak_ref (G_OBJECT (client->priv->chooser),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->manager);
                g_object_weak_ref (G_OBJECT (client->priv->chooser),
                                   (GWeakNotify)
                                   g_clear_object,
                                   &client->priv->connection);
        }

        return client->priv->chooser;
}

static void
gdm_greeter_client_class_init (GdmGreeterClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_greeter_client_finalize;

        g_type_class_add_private (klass, sizeof (GdmGreeterClientPrivate));

}

static void
gdm_greeter_client_init (GdmGreeterClient *client)
{

        client->priv = GDM_GREETER_CLIENT_GET_PRIVATE (client);
}

static void
gdm_greeter_client_finalize (GObject *object)
{
        GdmGreeterClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_CLIENT (object));

        client = GDM_GREETER_CLIENT (object);

        g_return_if_fail (client->priv != NULL);

        if (client->priv->user_verifier != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (client->priv->user_verifier),
                                              (gpointer *)
                                              &client->priv->user_verifier);
        }

        if (client->priv->greeter != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (client->priv->greeter),
                                              (gpointer *)
                                              &client->priv->greeter);
        }

        if (client->priv->remote_greeter != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (client->priv->remote_greeter),
                                              (gpointer *)
                                              &client->priv->remote_greeter);
        }

        if (client->priv->chooser != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (client->priv->chooser),
                                              (gpointer *)
                                              &client->priv->chooser);
        }

        g_clear_object (&client->priv->manager);
        g_clear_object (&client->priv->connection);

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
