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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

#include "gdm-client.h"
#include "gdm-client-glue.h"
#include "gdm-manager-glue.h"

#define GDM_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_CLIENT, GdmClientPrivate))

#define SESSION_DBUS_PATH      "/org/gnome/DisplayManager/Session"

struct GdmClientPrivate
{
        GdmManager         *manager;

        GdmUserVerifier    *user_verifier;
        GHashTable         *user_verifier_extensions;

        GdmGreeter         *greeter;
        GdmRemoteGreeter   *remote_greeter;
        GdmChooser         *chooser;
        GDBusConnection    *connection;
        char               *address;

        char              **enabled_extensions;
};

static void     gdm_client_class_init  (GdmClientClass *klass);
static void     gdm_client_init        (GdmClient      *client);
static void     gdm_client_finalize    (GObject        *object);

G_DEFINE_TYPE (GdmClient, gdm_client, G_TYPE_OBJECT);

static gpointer client_object = NULL;

GQuark
gdm_client_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0)
                error_quark = g_quark_from_static_string ("gdm-client");

        return error_quark;
}

static void
on_got_manager (GdmManager          *manager,
                GAsyncResult        *result,
                GTask               *task)
{
        GdmClient *client;
        GdmManager       *new_manager;
        GError           *error;

        client = GDM_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (task)));

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
                g_task_return_error (task, error);
        } else {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->manager),
                                       (GDestroyNotify) g_object_unref);
        }

        g_object_unref (task);
        g_object_unref (client);
}

static void
get_manager (GdmClient           *client,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
        GTask *task;

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->manager != NULL) {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->manager),
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        gdm_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       "org.gnome.DisplayManager",
                                       "/org/gnome/DisplayManager/Manager",
                                       cancellable,
                                       (GAsyncReadyCallback)
                                       on_got_manager,
                                       task);
}

typedef struct {
        GTask           *task;
        GdmUserVerifier *user_verifier;
} UserVerifierData;

static UserVerifierData *
user_verifier_data_new (GTask *task, GdmUserVerifier *user_verifier)
{
        UserVerifierData *data;

        data = g_slice_new (UserVerifierData);
        data->task = g_object_ref (task);
        data->user_verifier = g_object_ref (user_verifier);

        return data;
}

static void
user_verifier_data_free (UserVerifierData *data)
{
        g_object_unref (data->task);
        g_object_unref (data->user_verifier);
        g_slice_free (UserVerifierData, data);
}

static void
complete_user_verifier_proxy_operation (GdmClient          *client,
                                        UserVerifierData   *data)
{
        g_task_return_pointer (data->task,
                               g_object_ref (data->user_verifier),
                               (GDestroyNotify) g_object_unref);
        user_verifier_data_free (data);
}

static void
maybe_complete_user_verifier_proxy_operation (GdmClient          *client,
                                              UserVerifierData   *data)
{
        GHashTableIter iter;
        gpointer key, value;

        if (client->priv->user_verifier_extensions != NULL) {
                g_hash_table_iter_init (&iter, client->priv->user_verifier_extensions);
                while (g_hash_table_iter_next (&iter, &key, &value)) {
                        if (value == NULL)
                                return;
                }
        }

        complete_user_verifier_proxy_operation (client, data);
}

static void
on_user_verifier_choice_list_proxy_created (GObject            *source,
                                            GAsyncResult       *result,
                                            UserVerifierData   *data)
{
        GdmClient                 *client;
        GdmUserVerifierChoiceList *choice_list;
        GError                    *error = NULL;

        client = GDM_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (data->task)));

        choice_list = gdm_user_verifier_choice_list_proxy_new_finish (result, &error);

        if (choice_list == NULL) {
                g_debug ("Couldn't create UserVerifier ChoiceList proxy: %s", error->message);
                g_clear_error (&error);
                g_hash_table_remove (client->priv->user_verifier_extensions, gdm_user_verifier_choice_list_interface_info ()->name);
        } else {
                g_hash_table_replace (client->priv->user_verifier_extensions, gdm_user_verifier_choice_list_interface_info ()->name, choice_list);
        }

        maybe_complete_user_verifier_proxy_operation (client, data);
}

static void
on_user_verifier_extensions_enabled (GdmUserVerifier    *user_verifier,
                                     GAsyncResult       *result,
                                     UserVerifierData   *data)
{
        GdmClient *client;
        GCancellable *cancellable;
        GDBusConnection *connection;
        GError    *error = NULL;
        size_t     i;

        client = GDM_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (data->task)));
        cancellable = g_task_get_cancellable (data->task);

        gdm_user_verifier_call_enable_extensions_finish (user_verifier, result, &error);

        if (error != NULL) {
                g_debug ("Couldn't enable user verifier extensions: %s",
                         error->message);
                g_clear_error (&error);
                complete_user_verifier_proxy_operation (client, data);
                return;
        }

        connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (user_verifier));

        for (i = 0; client->priv->enabled_extensions[i] != NULL; i++) {
                g_debug ("Enabled extensions[%lu] = %s", i, client->priv->enabled_extensions[i]);
                g_hash_table_insert (client->priv->user_verifier_extensions, client->priv->enabled_extensions[i], NULL);

                if (strcmp (client->priv->enabled_extensions[i],
                            gdm_user_verifier_choice_list_interface_info ()->name) == 0) {
                        g_hash_table_insert (client->priv->user_verifier_extensions, client->priv->enabled_extensions[i], NULL);
                        gdm_user_verifier_choice_list_proxy_new (connection,
                                                                 G_DBUS_PROXY_FLAGS_NONE,
                                                                 NULL,
                                                                 SESSION_DBUS_PATH,
                                                                 cancellable,
                                                                 (GAsyncReadyCallback)
                                                                 on_user_verifier_choice_list_proxy_created,
                                                                 data);
                } else {
                        g_debug ("User verifier extension %s is unsupported", client->priv->enabled_extensions[i]);
                        g_hash_table_remove (client->priv->user_verifier_extensions,
                                             client->priv->enabled_extensions[i]);
                }
        }

        if (g_hash_table_size (client->priv->user_verifier_extensions) == 0) {
                g_debug ("No supported user verifier extensions");
                complete_user_verifier_proxy_operation (client, data);
        }

}

static void
free_interface_skeleton (GDBusInterfaceSkeleton *interface)
{
        if (interface == NULL)
                return;

        g_object_unref (interface);
}

static void
on_user_verifier_proxy_created (GObject            *source,
                                GAsyncResult       *result,
                                GTask              *task)
{
        GdmClient       *self;
        GdmUserVerifier *user_verifier;
        GCancellable    *cancellable = NULL;
        GError          *error = NULL;

        user_verifier = gdm_user_verifier_proxy_new_finish (result, &error);
        if (user_verifier == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        g_debug ("UserVerifier %p created", user_verifier);

        self = GDM_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (task)));
        if (self->priv->enabled_extensions == NULL) {
                g_debug ("no enabled extensions");
                g_task_return_pointer (task,
                                       user_verifier,
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        self->priv->user_verifier_extensions = g_hash_table_new_full (g_str_hash,
                                                                      g_str_equal,
                                                                      NULL,
                                                                      (GDestroyNotify)
                                                                      free_interface_skeleton);
        cancellable = g_task_get_cancellable (task);
        gdm_user_verifier_call_enable_extensions (user_verifier,
                                                  (const char * const *)
                                                  self->priv->enabled_extensions,
                                                  cancellable,
                                                  (GAsyncReadyCallback)
                                                  on_user_verifier_extensions_enabled,
                                                  user_verifier_data_new (task, user_verifier));
        g_object_unref (user_verifier);
}

static void
on_reauthentication_channel_connected (GObject            *source_object,
                                       GAsyncResult       *result,
                                       GTask              *task)
{
        GDBusConnection *connection;
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        connection = g_dbus_connection_new_for_address_finish (result, &error);
        if (!connection) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_user_verifier_proxy_new (connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     SESSION_DBUS_PATH,
                                     cancellable,
                                     (GAsyncReadyCallback)
                                     on_user_verifier_proxy_created,
                                     task);
        g_object_unref (connection);
}

static void
on_reauthentication_channel_opened (GdmManager         *manager,
                                    GAsyncResult       *result,
                                    GTask              *task)
{
        GCancellable *cancellable;
        char         *address;
        GError       *error;

        error = NULL;
        if (!gdm_manager_call_open_reauthentication_channel_finish (manager,
                                                                    &address,
                                                                    result,
                                                                    &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        g_dbus_connection_new_for_address (address,
                                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                           NULL,
                                           cancellable,
                                           (GAsyncReadyCallback)
                                           on_reauthentication_channel_connected,
                                           task);
}

static void
on_got_manager_for_reauthentication (GdmClient           *client,
                                     GAsyncResult        *result,
                                     GTask               *task)
{
        GCancellable *cancellable;
        GdmManager   *manager;
        char         *username;
        GError       *error;

        error = NULL;
        manager = g_task_propagate_pointer (G_TASK (result), &error);
        if (manager == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        username = g_object_get_data (G_OBJECT (task), "username");
        gdm_manager_call_open_reauthentication_channel (client->priv->manager,
                                                        username,
                                                        cancellable,
                                                        (GAsyncReadyCallback)
                                                        on_reauthentication_channel_opened,
                                                        task);

}

static gboolean
gdm_client_open_connection_sync (GdmClient      *client,
                                 GCancellable   *cancellable,
                                 GError        **error)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        if (client->priv->connection != NULL) {
                g_object_ref (client->priv->connection);
                return TRUE;
        }

        client->priv->manager = gdm_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "org.gnome.DisplayManager",
                                                                    "/org/gnome/DisplayManager/Manager",
                                                                    cancellable,
                                                                    error);

        if (client->priv->manager == NULL) {
                goto out;
        }

        ret = gdm_manager_call_open_session_sync (client->priv->manager,
                                                  &client->priv->address,
                                                  cancellable,
                                                  error);

        if (!ret) {
                g_clear_object (&client->priv->manager);
                goto out;
        }

        g_debug ("GdmClient: connecting to address: %s", client->priv->address);

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

        g_object_add_weak_pointer (G_OBJECT (client->priv->connection),
                                   (gpointer *)
                                   &client->priv->connection);

 out:
        return client->priv->connection != NULL;
}

static void
on_connected (GObject            *source_object,
              GAsyncResult       *result,
              GTask              *task)
{
        GDBusConnection *connection;
        GError *error;

        error = NULL;
        connection = g_dbus_connection_new_for_address_finish (result, &error);
        if (!connection) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        g_task_return_pointer (task,
                               g_object_ref (connection),
                               (GDestroyNotify) g_object_unref);
        g_object_unref (task);
        g_object_unref (connection);
}

static void
on_session_opened (GdmManager         *manager,
                   GAsyncResult       *result,
                   GTask              *task)
{
        GdmClient *client;
        GCancellable     *cancellable;
        GError           *error;

        client = GDM_CLIENT (g_async_result_get_source_object (G_ASYNC_RESULT (task)));

        error = NULL;
        if (!gdm_manager_call_open_session_finish (manager,
                                                   &client->priv->address,
                                                   result,
                                                   &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                g_object_unref (client);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        g_dbus_connection_new_for_address (client->priv->address,
                                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                           NULL,
                                           cancellable,
                                           (GAsyncReadyCallback)
                                           on_connected,
                                           task);
        g_object_unref (client);
}

static void
on_got_manager_for_opening_connection (GdmClient           *client,
                                       GAsyncResult        *result,
                                       GTask               *task)
{
        GCancellable *cancellable;
        GdmManager   *manager;
        GError       *error;

        error = NULL;
        manager = g_task_propagate_pointer (G_TASK (result), &error);
        if (manager == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_manager_call_open_session (client->priv->manager,
                                       cancellable,
                                       (GAsyncReadyCallback)
                                       on_session_opened,
                                       task);
}

static gboolean
gdm_client_open_connection_finish (GdmClient      *client,
                                   GAsyncResult   *result,
                                   GError        **error)
{
        g_autoptr(GDBusConnection) connection = NULL;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        connection = g_task_propagate_pointer (G_TASK (result), error);
        if (connection == NULL) {
                return FALSE;
        }

        if (client->priv->connection == NULL) {
                client->priv->connection = g_steal_pointer (&connection);
                g_object_add_weak_pointer (G_OBJECT (client->priv->connection),
                                           (gpointer *) &client->priv->connection);
        } else if (client->priv->connection == connection) {
                connection = NULL;
        }

        return TRUE;
}

static void
gdm_client_open_connection (GdmClient           *client,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->connection != NULL) {
            g_task_return_pointer (task,
                                   g_object_ref (client->priv->connection),
                                   (GDestroyNotify) g_object_unref);
            g_object_unref (task);
            return;
        }

        get_manager (client,
                     cancellable,
                     (GAsyncReadyCallback)
                     on_got_manager_for_opening_connection,
                     task);
}

/**
 * gdm_client_open_reauthentication_channel_sync:
 * @client: a #GdmClient
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
gdm_client_open_reauthentication_channel_sync (GdmClient     *client,
                                               const char    *username,
                                               GCancellable  *cancellable,
                                               GError       **error)
{
        GDBusConnection *connection;
        GdmUserVerifier *user_verifier = NULL;
        gboolean         ret;
        char            *address;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

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

        g_debug ("GdmClient: connecting to address: %s", client->priv->address);

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
 * gdm_client_open_reauthentication_channel:
 * @client: a #GdmClient
 * @username: user to reauthenticate
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmUserVerifier object that can be used to
 * reauthenticate an already logged in user.
 */
void
gdm_client_open_reauthentication_channel (GdmClient           *client,
                                          const char          *username,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        g_object_set_data_full (G_OBJECT (task),
                                "username",
                                g_strdup (username),
                                (GDestroyNotify)
                                g_free);

        get_manager (client,
                     cancellable,
                     (GAsyncReadyCallback)
                     on_got_manager_for_reauthentication,
                     task);
}

/**
 * gdm_client_open_reauthentication_channel_finish:
 * @client: a #GdmClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_client_open_reauthentication_channel().
 *
 * Returns: (transfer full):  a #GdmUserVerifier
 */
GdmUserVerifier *
gdm_client_open_reauthentication_channel_finish (GdmClient       *client,
                                                 GAsyncResult    *result,
                                                 GError         **error)
{
        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * gdm_client_get_user_verifier_sync:
 * @client: a #GdmClient
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Gets a #GdmUserVerifier object that can be used to
 * verify a user's local account.
 *
 * Returns: (transfer full): #GdmUserVerifier or %NULL if not connected
 */
GdmUserVerifier *
gdm_client_get_user_verifier_sync (GdmClient     *client,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
        if (client->priv->user_verifier != NULL) {
                return g_object_ref (client->priv->user_verifier);
        }

        if (!gdm_client_open_connection_sync (client, cancellable, error)) {
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
                                   g_object_unref,
                                   client->priv->connection);

                if (client->priv->enabled_extensions != NULL) {
                        gboolean res;

                        client->priv->user_verifier_extensions = g_hash_table_new_full (g_str_hash,
                                                                                        g_str_equal,
                                                                                        NULL,
                                                                                        (GDestroyNotify)
                                                                                        free_interface_skeleton);
                        res = gdm_user_verifier_call_enable_extensions_sync (client->priv->user_verifier,
                                                                            (const char * const *)
                                                                             client->priv->enabled_extensions,
                                                                             cancellable,
                                                                             NULL);

                        if (res) {
                                size_t i;
                                for (i = 0; client->priv->enabled_extensions[i] != NULL; i++) {
                                            if (strcmp (client->priv->enabled_extensions[i],
                                                        gdm_user_verifier_choice_list_interface_info ()->name) == 0) {
                                                        GdmUserVerifierChoiceList *choice_list_interface;
                                                        choice_list_interface = gdm_user_verifier_choice_list_proxy_new_sync (client->priv->connection,
                                                                                                                              G_DBUS_PROXY_FLAGS_NONE,
                                                                                                                              NULL,
                                                                                                                              SESSION_DBUS_PATH,
                                                                                                                              cancellable,
                                                                                                                              NULL);
                                                        if (choice_list_interface != NULL)
                                                                    g_hash_table_insert (client->priv->user_verifier_extensions, client->priv->enabled_extensions[i], choice_list_interface);
                                            }
                                }
                        }
                }
        }

        return client->priv->user_verifier;
}

static void
on_connection_opened_for_user_verifier (GdmClient          *client,
                                        GAsyncResult       *result,
                                        GTask              *task)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_client_open_connection_finish (client, result, &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_user_verifier_proxy_new (client->priv->connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     SESSION_DBUS_PATH,
                                     cancellable,
                                     (GAsyncReadyCallback)
                                     on_user_verifier_proxy_created,
                                     task);
}

/**
 * gdm_client_get_user_verifier:
 * @client: a #GdmClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmUserVerifier object that can be used to
 * verify a user's local account.
 */
void
gdm_client_get_user_verifier (GdmClient           *client,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->user_verifier != NULL) {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->user_verifier),
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        gdm_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_user_verifier,
                                    task);
}

/**
 * gdm_client_get_user_verifier_finish:
 * @client: a #GdmClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_client_get_user_verifier().
 *
 * Returns: (transfer full): a #GdmUserVerifier
 */
GdmUserVerifier *
gdm_client_get_user_verifier_finish (GdmClient       *client,
                                     GAsyncResult    *result,
                                     GError         **error)
{
        GdmUserVerifier *user_verifier;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        if (client->priv->user_verifier != NULL)
                return g_object_ref (client->priv->user_verifier);

        user_verifier = g_task_propagate_pointer (G_TASK (result), error);
        if (user_verifier == NULL)
                return NULL;

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

        return user_verifier;
}

/**
 * gdm_client_get_user_verifier_choice_list:
 * @client: a #GdmClient
 *
 * Gets a #GdmUserVerifierChoiceList object that can be used to
 * verify a user's local account.
 *
 * Returns: (transfer none): #GdmUserVerifierChoiceList or %NULL if user
 * verifier isn't yet fetched, or daemon doesn't support choice lists
 */
GdmUserVerifierChoiceList *
gdm_client_get_user_verifier_choice_list (GdmClient *client)
{
        if (client->priv->user_verifier_extensions == NULL)
                return NULL;

        return g_hash_table_lookup (client->priv->user_verifier_extensions,
                                    gdm_user_verifier_choice_list_interface_info ()->name);
}

static void
on_timed_login_details_got (GdmGreeter   *greeter,
                            GAsyncResult *result)
{
    gdm_greeter_call_get_timed_login_details_finish (greeter, NULL, NULL, NULL, result, NULL);
}

static void
query_for_timed_login_requested_signal (GdmGreeter *greeter)
{
        /* This just makes sure a timed-login-requested signal gets fired
         * off if appropriate.
         */
        gdm_greeter_call_get_timed_login_details (greeter,
                                                  NULL,
                                                  (GAsyncReadyCallback)
                                                  on_timed_login_details_got,
                                                  NULL);
}

static void
on_greeter_proxy_created (GObject            *source,
                          GAsyncResult       *result,
                          GTask              *task)
{
        GdmGreeter   *greeter;
        GError       *error = NULL;

        greeter = gdm_greeter_proxy_new_finish (result, &error);
        if (greeter == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        g_task_return_pointer (task,
                               greeter,
                               (GDestroyNotify) g_object_unref);
        g_object_unref (task);

        query_for_timed_login_requested_signal (greeter);
}

static void
on_connection_opened_for_greeter (GdmClient          *client,
                                  GAsyncResult       *result,
                                  GTask              *task)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_client_open_connection_finish (client, result, &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_greeter_proxy_new (client->priv->connection,
                               G_DBUS_PROXY_FLAGS_NONE,
                               NULL,
                               SESSION_DBUS_PATH,
                               cancellable,
                               (GAsyncReadyCallback)
                               on_greeter_proxy_created,
                               task);
}

/**
 * gdm_client_get_greeter:
 * @client: a #GdmClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmGreeter object that can be used to
 * verify a user's local account.
 */
void
gdm_client_get_greeter (GdmClient           *client,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->greeter != NULL) {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->greeter),
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        gdm_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_greeter,
                                    task);
}

/**
 * gdm_client_get_greeter_finish:
 * @client: a #GdmClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_client_get_greeter().
 *
 * Returns: (transfer full): a #GdmGreeter
 */
GdmGreeter *
gdm_client_get_greeter_finish (GdmClient       *client,
                               GAsyncResult    *result,
                               GError         **error)
{
        GdmGreeter *greeter;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        if (client->priv->greeter != NULL)
                return g_object_ref (client->priv->greeter);

        greeter = g_task_propagate_pointer (G_TASK (result), error);
        if (greeter == NULL)
                return NULL;

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

        return greeter;
}

/**
 * gdm_client_get_greeter_sync:
 * @client: a #GdmClient
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
gdm_client_get_greeter_sync (GdmClient     *client,
                             GCancellable  *cancellable,
                             GError       **error)
{
        if (client->priv->greeter != NULL) {
                return g_object_ref (client->priv->greeter);
        }

        if (!gdm_client_open_connection_sync (client, cancellable, error)) {
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
                                   g_object_unref,
                                   client->priv->connection);

                query_for_timed_login_requested_signal (client->priv->greeter);
        }

        return client->priv->greeter;
}

static void
on_remote_greeter_proxy_created (GObject            *object,
                                 GAsyncResult       *result,
                                 GTask              *task)
{
        GdmRemoteGreeter *remote_greeter;
        GError           *error = NULL;

        remote_greeter = gdm_remote_greeter_proxy_new_finish (result, &error);
        if (remote_greeter == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        g_task_return_pointer (task,
                               remote_greeter,
                               (GDestroyNotify) g_object_unref);
        g_object_unref (task);
}

static void
on_connection_opened_for_remote_greeter (GdmClient          *client,
                                         GAsyncResult       *result,
                                         GTask              *task)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_client_open_connection_finish (client, result, &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_remote_greeter_proxy_new (client->priv->connection,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      NULL,
                                      SESSION_DBUS_PATH,
                                      cancellable,
                                      (GAsyncReadyCallback)
                                      on_remote_greeter_proxy_created,
                                      task);
}

/**
 * gdm_client_get_remote_greeter:
 * @client: a #GdmClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmRemoteGreeter object that can be used to
 * verify a user's local account.
 */
void
gdm_client_get_remote_greeter (GdmClient           *client,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->remote_greeter != NULL) {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->remote_greeter),
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        gdm_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_remote_greeter,
                                    task);
}

/**
 * gdm_client_get_remote_greeter_finish:
 * @client: a #GdmClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_client_get_remote_greeter().
 *
 * Returns: (transfer full): a #GdmRemoteGreeter
 */
GdmRemoteGreeter *
gdm_client_get_remote_greeter_finish (GdmClient     *client,
                                      GAsyncResult  *result,
                                      GError       **error)
{
        GdmRemoteGreeter *remote_greeter;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        if (client->priv->remote_greeter != NULL)
                return g_object_ref (client->priv->remote_greeter);

        remote_greeter = g_task_propagate_pointer (G_TASK (result), error);
        if (remote_greeter == NULL)
                return NULL;

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

        return remote_greeter;
}

/**
 * gdm_client_get_remote_greeter_sync:
 * @client: a #GdmClient
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
gdm_client_get_remote_greeter_sync (GdmClient     *client,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
        if (client->priv->remote_greeter != NULL) {
                return g_object_ref (client->priv->remote_greeter);
        }

        if (!gdm_client_open_connection_sync (client, cancellable, error)) {
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
                                   g_object_unref,
                                   client->priv->connection);
        }

        return client->priv->remote_greeter;
}

static void
on_chooser_proxy_created (GObject            *source,
                          GAsyncResult       *result,
                          GTask              *task)
{
        GdmChooser   *chooser;
        GError       *error = NULL;

        chooser = gdm_chooser_proxy_new_finish (result, &error);
        if (chooser == NULL) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        g_task_return_pointer (task,
                               chooser,
                               (GDestroyNotify) g_object_unref);
        g_object_unref (task);
}

static void
on_connection_opened_for_chooser (GdmClient          *client,
                                  GAsyncResult       *result,
                                  GTask              *task)
{
        GCancellable *cancellable;
        GError       *error;

        error = NULL;
        if (!gdm_client_open_connection_finish (client, result, &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        cancellable = g_task_get_cancellable (task);
        gdm_chooser_proxy_new (client->priv->connection,
                               G_DBUS_PROXY_FLAGS_NONE,
                               NULL,
                               SESSION_DBUS_PATH,
                               cancellable,
                               (GAsyncReadyCallback)
                               on_chooser_proxy_created,
                               task);
}

/**
 * gdm_client_get_chooser:
 * @client: a #GdmClient
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: The data to pass to @callback
 * @cancellable: a #GCancellable
 *
 * Gets a #GdmChooser object that can be used to
 * verify a user's local account.
 */
void
gdm_client_get_chooser (GdmClient           *client,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
        GTask *task;

        g_return_if_fail (GDM_IS_CLIENT (client));

        task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

        if (client->priv->chooser != NULL) {
                g_task_return_pointer (task,
                                       g_object_ref (client->priv->chooser),
                                       (GDestroyNotify) g_object_unref);
                g_object_unref (task);
                return;
        }

        gdm_client_open_connection (client,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_connection_opened_for_chooser,
                                    task);
}

/**
 * gdm_client_get_chooser_finish:
 * @client: a #GdmClient
 * @result: The #GAsyncResult from the callback
 * @error: a #GError
 *
 * Finishes an operation started with
 * gdm_client_get_chooser().
 *
 * Returns: (transfer full): a #GdmChooser
 */
GdmChooser *
gdm_client_get_chooser_finish (GdmClient       *client,
                               GAsyncResult    *result,
                               GError         **error)
{
        GdmChooser *chooser;

        g_return_val_if_fail (GDM_IS_CLIENT (client), FALSE);

        if (client->priv->chooser != NULL)
                return g_object_ref (client->priv->chooser);

        chooser = g_task_propagate_pointer (G_TASK (result), error);
        if (chooser == NULL)
                return NULL;

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

        return chooser;
}

/**
 * gdm_client_get_chooser_sync:
 * @client: a #GdmClient
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
gdm_client_get_chooser_sync (GdmClient     *client,
                             GCancellable  *cancellable,
                             GError       **error)
{

        if (client->priv->chooser != NULL) {
                return g_object_ref (client->priv->chooser);
        }

        if (!gdm_client_open_connection_sync (client, cancellable, error)) {
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
                                   g_object_unref,
                                   client->priv->connection);
        }

        return client->priv->chooser;
}

static void
gdm_client_class_init (GdmClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_client_finalize;

        g_type_class_add_private (klass, sizeof (GdmClientPrivate));

}

static void
gdm_client_init (GdmClient *client)
{

        client->priv = GDM_CLIENT_GET_PRIVATE (client);
}

static void
gdm_client_finalize (GObject *object)
{
        GdmClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_CLIENT (object));

        client = GDM_CLIENT (object);

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

        if (client->priv->connection != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (client->priv->connection),
                                              (gpointer *)
                                              &client->priv->connection);
        }

        g_clear_object (&client->priv->manager);
        g_clear_object (&client->priv->connection);

        g_strfreev (client->priv->enabled_extensions);
        g_free (client->priv->address);

        G_OBJECT_CLASS (gdm_client_parent_class)->finalize (object);
}

GdmClient *
gdm_client_new (void)
{
        if (client_object != NULL) {
                g_object_ref (client_object);
        } else {
                client_object = g_object_new (GDM_TYPE_CLIENT, NULL);
                g_object_add_weak_pointer (client_object,
                                           (gpointer *) &client_object);
        }

        return GDM_CLIENT (client_object);
}


/**
 * gdm_client_set_enabled_extensions:
 * @client: a #GdmClient
 * @extensions: (array zero-terminated=1) (element-type utf8): a list of extensions
 *
 * Enables GDM's pam extensions.  Currently, only
 * org.gnome.DisplayManager.UserVerifier.ChoiceList is supported.
 */
void
gdm_client_set_enabled_extensions (GdmClient          *client,
                                   const char * const *extensions)
{
        client->priv->enabled_extensions = g_strdupv ((char **) extensions);
}
