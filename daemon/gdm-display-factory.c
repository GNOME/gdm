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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <polkit/polkit.h>

#include "gdm-display-factory.h"
#include "gdm-common.h"
#include "gdm-display-store.h"

#define GDM_DISPLAY_FACTORY_MANAGE_DISPLAYS_POLKIT_ACTION "org.gnome.displaymanager.displayfactory.manage-user-displays"

typedef struct _GdmDisplayFactoryPrivate
{
        GdmDisplayStore *display_store;
        guint            purge_displays_id;

        GHashTable      *display_creation_users;

       PolkitAuthority  *authority;
} GdmDisplayFactoryPrivate;

enum {
        PROP_0,
        PROP_DISPLAY_STORE,
};

static void     gdm_display_factory_class_init  (GdmDisplayFactoryClass *klass);
static void     gdm_display_factory_init        (GdmDisplayFactory      *factory);
static void     gdm_display_factory_finalize    (GObject                *object);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GdmDisplayFactory, gdm_display_factory, G_TYPE_OBJECT)

GQuark
gdm_display_factory_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_display_factory_error");
        }

        return ret;
}

static gboolean
purge_display (char       *id,
               GdmDisplay *display,
               gpointer    user_data)
{
        int status;

        status = gdm_display_get_status (display);

        switch (status) {
        case GDM_DISPLAY_FINISHED:
        case GDM_DISPLAY_FAILED:
                return TRUE;
        default:
                return FALSE;
        }
}

static gboolean
purge_displays (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);
        priv->purge_displays_id = 0;
        gdm_display_store_foreach_remove (priv->display_store,
                                          (GdmDisplayStoreFunc)purge_display,
                                          NULL);

        return G_SOURCE_REMOVE;
}

void
gdm_display_factory_queue_purge_displays (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        g_return_if_fail (GDM_IS_DISPLAY_FACTORY (factory));

        priv = gdm_display_factory_get_instance_private (factory);
        if (priv->purge_displays_id == 0) {
                priv->purge_displays_id = g_idle_add ((GSourceFunc) purge_displays, factory);
        }
}

GdmDisplayStore *
gdm_display_factory_get_display_store (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), NULL);

        priv = gdm_display_factory_get_instance_private (factory);
        return priv->display_store;
}

gboolean
gdm_display_factory_start (GdmDisplayFactory *factory)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        g_object_ref (factory);
        ret = GDM_DISPLAY_FACTORY_GET_CLASS (factory)->start (factory);
        g_object_unref (factory);

        return ret;
}

gboolean
gdm_display_factory_stop (GdmDisplayFactory *factory)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        g_object_ref (factory);
        ret = GDM_DISPLAY_FACTORY_GET_CLASS (factory)->stop (factory);
        g_object_unref (factory);

        return ret;
}

static gboolean
ensure_polkit_authority (GdmDisplayFactory  *factory,
                         GError            **error)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);

        if (priv->authority)
                return TRUE;

        priv->authority = polkit_authority_get_sync (NULL, error);

        return priv->authority != NULL;
}

gboolean
gdm_display_factory_authorize_manage_user_displays (GdmDisplayFactory      *factory,
                                                    GDBusMethodInvocation  *invocation,
                                                    GError                **error)
{
        g_autoptr (PolkitAuthorizationResult) result = NULL;
        g_autoptr (PolkitSubject) subject = NULL;
        g_autoptr (GError) local_error = NULL;
        const char *action;
        const char *method;
        const char *sender;
        PolkitCheckAuthorizationFlags flags;
        GdmDisplayFactoryPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        method = g_dbus_method_invocation_get_method_name (invocation);
        if (g_strcmp0 (method, "CreateUserDisplay") != 0 &&
            g_strcmp0 (method, "DestroyUserDisplay") != 0)
                return TRUE;

        if (!ensure_polkit_authority (factory, &local_error)) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "Error getting polkit authority: %s",
                             local_error->message);
                return FALSE;
        }

        priv = gdm_display_factory_get_instance_private (factory);

        sender = g_dbus_method_invocation_get_sender (invocation);
        subject = polkit_system_bus_name_new (sender);
        action = GDM_DISPLAY_FACTORY_MANAGE_DISPLAYS_POLKIT_ACTION;
        flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
        result = polkit_authority_check_authorization_sync (priv->authority,
                                                            subject, action,
                                                            NULL, flags, NULL,
                                                            &local_error);
        if (!result) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "Failed to check authorization: %s",
                             local_error->message);
                return FALSE;
        }

        if (!polkit_authorization_result_get_is_authorized (result)) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "Not authorized for action %s", action);
                return FALSE;
        }

        return TRUE;
}

gboolean
gdm_display_factory_on_user_display_creation (GdmDisplayFactory  *factory,
                                              const char         *user,
                                              GError            **error)
{
        g_auto (GStrv) sessions = NULL;
        GdmDisplayFactoryPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        priv = gdm_display_factory_get_instance_private (factory);

        if (g_hash_table_lookup (priv->display_creation_users, user) != NULL) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "A display for user %s has already been created", user);
                return FALSE;
        }

        if (!gdm_find_graphical_sessions_for_username (user, &sessions, error))
                return FALSE;

        if (g_strv_length (sessions) > 0) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "There's already an opened session for user %s", user);
                return FALSE;
        }

        g_hash_table_insert (priv->display_creation_users, g_strdup (user), NULL);

        return TRUE;
}

gboolean
gdm_display_factory_on_user_display_destruction (GdmDisplayFactory  *factory,
                                                 const char         *user,
                                                 GError            **error)
{
        g_auto (GStrv) session_ids = NULL;
        g_autoptr (GError) local_error = NULL;
        g_autoptr (GDBusConnection) connection = NULL;
        GdmDisplayFactoryPrivate *priv;
        int i;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        priv = gdm_display_factory_get_instance_private (factory);

        if (!g_hash_table_remove (priv->display_creation_users, user)) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "A display for user %s wasn't created", user);
                return FALSE;
        }

        if (!gdm_find_graphical_sessions_for_username (user, &session_ids, error))
                return FALSE;

        if (g_strv_length (session_ids) == 0) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "There's no display for user %s", user);
                return FALSE;
        }

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &local_error);
        if (connection == NULL) {
                g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                             "Failed getting system bus %s", local_error->message);
                return FALSE;
        }

        for (i = 0; session_ids[i] != NULL; i++) {
                if (!gdm_terminate_session_by_id (connection, NULL, session_ids[i])) {
                        g_set_error (error, GDM_DISPLAY_ERROR, GDM_DISPLAY_ERROR_GENERAL,
                                     "Failed to terminate session %s",
                                     session_ids[i]);
                        return FALSE;
                }
        }

        return TRUE;
}

static void
gdm_display_factory_set_display_store (GdmDisplayFactory *factory,
                                       GdmDisplayStore   *display_store)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);
        g_clear_object (&priv->display_store);

        if (display_store != NULL) {
                priv->display_store = g_object_ref (display_store);
        }
}

static void
gdm_display_factory_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmDisplayFactory *self;

        self = GDM_DISPLAY_FACTORY (object);

        switch (prop_id) {
        case PROP_DISPLAY_STORE:
                gdm_display_factory_set_display_store (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_display_factory_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmDisplayFactory *self;
        GdmDisplayFactoryPrivate *priv;

        self = GDM_DISPLAY_FACTORY (object);
        priv = gdm_display_factory_get_instance_private (self);

        switch (prop_id) {
        case PROP_DISPLAY_STORE:
                g_value_set_object (value, priv->display_store);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_display_factory_class_init (GdmDisplayFactoryClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_display_factory_get_property;
        object_class->set_property = gdm_display_factory_set_property;
        object_class->finalize = gdm_display_factory_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_STORE,
                                         g_param_spec_object ("display-store",
                                                              "display store",
                                                              "display store",
                                                              GDM_TYPE_DISPLAY_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gdm_display_factory_init (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);

        priv->display_creation_users = g_hash_table_new_full (g_str_hash,
                                                              g_str_equal,
                                                              (GDestroyNotify) g_free,
                                                              NULL);
}

static void
gdm_display_factory_finalize (GObject *object)
{
        GdmDisplayFactory *factory;
        GdmDisplayFactoryPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DISPLAY_FACTORY (object));

        factory = GDM_DISPLAY_FACTORY (object);
        priv = gdm_display_factory_get_instance_private (factory);

        g_return_if_fail (priv != NULL);

        g_clear_pointer (&priv->display_creation_users, g_hash_table_destroy);

        g_clear_handle_id (&priv->purge_displays_id, g_source_remove);

        g_clear_object (&priv->display_store);

        g_clear_object (&priv->authority);

        G_OBJECT_CLASS (gdm_display_factory_parent_class)->finalize (object);
}
