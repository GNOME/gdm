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

#include "gdm-session-manager.h"
#include "gdm-marshal.h"

#define GDM_SESSION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_MANAGER, GdmSessionManagerPrivate))

static guint32 notify_serial = 1;

struct GdmSessionManagerPrivate
{
        GHashTable        *levels;
        GHashTable        *notifications;
        GHashTable        *level_notifications;
        GdmSessionLevel    level;
};

enum {
        PROP_0,
};

enum {
        LEVEL_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_session_manager_class_init  (GdmSessionManagerClass *klass);
static void     gdm_session_manager_init        (GdmSessionManager      *session_manager);
static void     gdm_session_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GdmSessionManager, gdm_session_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

typedef struct
{
        guint                     id;
        GdmSessionLevelNotifyFunc func;
        gpointer                  data;
        GdmSessionLevel           levels;
} NotifyData;

static void
notify_data_free (NotifyData *data)
{
        g_free (data);
}

static void
add_notify_for_level (GdmSessionManager *manager,
                      NotifyData        *data,
                      GdmSessionLevel    level)
{
        GList *list;

        list = g_hash_table_lookup (manager->priv->level_notifications, GUINT_TO_POINTER (level));
        list = g_list_prepend (list, data);
        g_hash_table_insert (manager->priv->level_notifications, GUINT_TO_POINTER (level), list);
}

static guint32
get_next_notify_id (void)
{
        guint32 serial;

        serial = notify_serial++;

        if ((gint32)notify_serial < 0) {
                notify_serial = 1;
        }

        return serial;
}

guint
gdm_session_manager_add_notify (GdmSessionManager        *manager,
                                GdmSessionLevel           levels,
                                GdmSessionLevelNotifyFunc func,
                                gpointer                  user_data)
{
        int         i;
        guint       id;
        NotifyData *ndata;

        g_return_val_if_fail (GDM_IS_SESSION_MANAGER (manager), 0);

        id = get_next_notify_id ();

        ndata = g_new0 (NotifyData, 1);
        ndata->id = id;
        ndata->func = func;
        ndata->data = user_data;
        ndata->levels = levels;

        g_hash_table_insert (manager->priv->notifications, GUINT_TO_POINTER (id), ndata);

        for (i = GDM_SESSION_LEVEL_NONE; i <= GDM_SESSION_LEVEL_SHUTDOWN; i = i << 1) {
                if (levels & i) {
                        add_notify_for_level (manager, ndata, i);
                }
        }

        return id;
}

static void
add_client_to_level (GdmSessionManager *manager,
                     GdmSessionClient  *client,
                     GdmSessionLevel    level)
{
        GList *list;

        list = g_hash_table_lookup (manager->priv->levels, GUINT_TO_POINTER (level));
        list = g_list_prepend (list, g_object_ref (client));
        g_hash_table_insert (manager->priv->levels, GUINT_TO_POINTER (level), list);
}

void
gdm_session_manager_add_client (GdmSessionManager *manager,
                                GdmSessionClient  *client,
                                GdmSessionLevel    levels)
{
        int i;

        g_return_if_fail (GDM_IS_SESSION_MANAGER (manager));

        for (i = GDM_SESSION_LEVEL_NONE; i <= GDM_SESSION_LEVEL_SHUTDOWN; i = i << 1) {
                if (levels & i) {
                        add_client_to_level (manager, client, i);
                }
        }
}

void
gdm_session_manager_load_autostart_dir  (GdmSessionManager *manager,
                                         const char        *path,
                                         GdmSessionLevel    levels)
{
        g_return_if_fail (GDM_IS_SESSION_MANAGER (manager));
}

GdmSessionLevel
gdm_session_manager_get_level (GdmSessionManager *manager)
{
        g_return_val_if_fail (GDM_IS_SESSION_MANAGER (manager), 0);

        return manager->priv->level;
}

static void
_change_level (GdmSessionManager *manager,
               GdmSessionLevel    new_level)
{
        GList *old_clients;
        GList *new_clients;
        GList *old_notify;
        GList *new_notify;
        GList *list;
        guint  old_level;

        /* unlike some other run level systems
         * we do not run intermediate levels
         * but jump directly to the new level */

        old_clients = g_hash_table_lookup (manager->priv->levels,
                                           GUINT_TO_POINTER (manager->priv->level));
        new_clients = g_hash_table_lookup (manager->priv->levels,
                                           GUINT_TO_POINTER (new_level));

        old_notify = g_hash_table_lookup (manager->priv->level_notifications,
                                          GUINT_TO_POINTER (manager->priv->level));
        new_notify = g_hash_table_lookup (manager->priv->level_notifications,
                                          GUINT_TO_POINTER (new_level));

        /* shutdown all running services that won't
         * be run in the new level */
        for (list = old_clients; list; list = list->next) {
                if (! g_list_find (new_clients, list->data)) {
                        GdmSessionClient *client;
                        client = list->data;
                        gdm_session_client_stop (client);
                }
        }

        /* run the off notifications for the new level */
        for (list = old_notify; list; list = list->next) {
                if (! g_list_find (new_notify, list->data)) {
                        NotifyData *ndata;
                        ndata = list->data;
                        ndata->func (manager, FALSE, ndata->data);
                }
        }

        /* change the level */
        old_level = (guint)manager->priv->level;
        manager->priv->level = new_level;
        g_signal_emit (manager, signals [LEVEL_CHANGED], 0, old_level, new_level);

        /* start up the new services for new level */
        for (list = new_clients; list; list = list->next) {
                if (! g_list_find (old_clients, list->data)) {
                        GdmSessionClient *client;
                        gboolean          res;
                        GError           *error;

                        client = list->data;
                        error = NULL;
                        res = gdm_session_client_start (client, &error);
                        if (! res) {
                                g_warning ("Unable to start client: %s", error->message);
                                g_error_free (error);
                        }
                }
        }

        /* run the on notifications for the new level */
        for (list = new_notify; list; list = list->next) {
                if (! g_list_find (old_notify, list->data)) {
                        NotifyData *ndata;
                        ndata = list->data;
                        ndata->func (manager, TRUE, ndata->data);
                }
        }
}

void
gdm_session_manager_set_level (GdmSessionManager *manager,
                               GdmSessionLevel    level)
{
        g_return_if_fail (GDM_IS_SESSION_MANAGER (manager));

        if (level == manager->priv->level) {
                return;
        }

        _change_level (manager, level);
}

static void
gdm_session_manager_set_property (GObject        *object,
                                  guint           prop_id,
                                  const GValue   *value,
                                  GParamSpec     *pspec)
{
        GdmSessionManager *self;

        self = GDM_SESSION_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_manager_get_property (GObject        *object,
                                  guint           prop_id,
                                  GValue         *value,
                                  GParamSpec     *pspec)
{
        GdmSessionManager *self;

        self = GDM_SESSION_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_session_manager_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        GdmSessionManager      *session_manager;
        GdmSessionManagerClass *klass;

        klass = GDM_SESSION_MANAGER_CLASS (g_type_class_peek (GDM_TYPE_SESSION_MANAGER));

        session_manager = GDM_SESSION_MANAGER (G_OBJECT_CLASS (gdm_session_manager_parent_class)->constructor (type,
                                                                                                                     n_construct_properties,
                                                                                                                     construct_properties));

        return G_OBJECT (session_manager);
}

static void
gdm_session_manager_dispose (GObject *object)
{
        GdmSessionManager *session_manager;

        session_manager = GDM_SESSION_MANAGER (object);

        G_OBJECT_CLASS (gdm_session_manager_parent_class)->dispose (object);
}

static void
gdm_session_manager_class_init (GdmSessionManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_manager_get_property;
        object_class->set_property = gdm_session_manager_set_property;
        object_class->constructor = gdm_session_manager_constructor;
        object_class->dispose = gdm_session_manager_dispose;
        object_class->finalize = gdm_session_manager_finalize;

        g_type_class_add_private (klass, sizeof (GdmSessionManagerPrivate));

        signals [LEVEL_CHANGED] =
                g_signal_new ("level-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmSessionManagerClass, level_changed),
                              NULL, NULL,
                              gdm_marshal_VOID__UINT_UINT,
                              G_TYPE_NONE,
                              2, G_TYPE_UINT, G_TYPE_UINT);

}

static void
gdm_session_manager_init (GdmSessionManager *manager)
{

        manager->priv = GDM_SESSION_MANAGER_GET_PRIVATE (manager);

        manager->priv->levels = g_hash_table_new (NULL, NULL);
        manager->priv->level_notifications = g_hash_table_new (NULL, NULL);
        manager->priv->notifications = g_hash_table_new_full (NULL,
                                                              NULL,
                                                              NULL,
                                                              (GDestroyNotify)notify_data_free);
}

static void
gdm_session_manager_finalize (GObject *object)
{
        GdmSessionManager *session_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_MANAGER (object));

        session_manager = GDM_SESSION_MANAGER (object);

        g_return_if_fail (session_manager->priv != NULL);

        G_OBJECT_CLASS (gdm_session_manager_parent_class)->finalize (object);
}

GdmSessionManager *
gdm_session_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GDM_TYPE_SESSION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GDM_SESSION_MANAGER (manager_object);
}
