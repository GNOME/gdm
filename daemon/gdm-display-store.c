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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display-store.h"
#include "gdm-display.h"

struct _GdmDisplayStore
{
        GObject     parent;
        GHashTable *displays;
};

typedef struct
{
        GdmDisplayStore *store;
        GdmDisplay      *display;
} StoredDisplay;

enum {
        DISPLAY_ADDED,
        DISPLAY_REMOVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_display_store_class_init    (GdmDisplayStoreClass *klass);
static void     gdm_display_store_init          (GdmDisplayStore      *display_store);
static void     gdm_display_store_finalize      (GObject              *object);

G_DEFINE_TYPE (GdmDisplayStore, gdm_display_store, G_TYPE_OBJECT)

static StoredDisplay *
stored_display_new (GdmDisplayStore *store,
                    GdmDisplay      *display)
{
        StoredDisplay *stored_display;

        stored_display = g_slice_new (StoredDisplay);
        stored_display->store = store;
        stored_display->display = g_object_ref (display);

        return stored_display;
}

static void
stored_display_free (StoredDisplay *stored_display)
{
        g_signal_emit (G_OBJECT (stored_display->store),
                       signals[DISPLAY_REMOVED],
                       0,
                       stored_display->display);

        g_debug ("GdmDisplayStore: Unreffing display: %p",
                 stored_display->display);
        g_object_unref (stored_display->display);

        g_slice_free (StoredDisplay, stored_display);
}

GQuark
gdm_display_store_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_display_store_error");
        }

        return ret;
}

void
gdm_display_store_clear (GdmDisplayStore    *store)
{
        g_return_if_fail (GDM_IS_DISPLAY_STORE (store));
        g_debug ("GdmDisplayStore: Clearing display store");
        g_hash_table_remove_all (store->displays);
}

static gboolean
remove_display (char              *id,
                GdmDisplay        *display,
                GdmDisplay        *display_to_remove)
{
        if (display == display_to_remove) {
                return TRUE;
        }
        return FALSE;
}

gboolean
gdm_display_store_remove (GdmDisplayStore    *store,
                          GdmDisplay         *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY_STORE (store), FALSE);
        g_return_val_if_fail (display != NULL, FALSE);

        gdm_display_store_foreach_remove (store,
                                          (GdmDisplayStoreFunc)remove_display,
                                          display);
        return FALSE;
}

typedef struct
{
        GdmDisplayStoreFunc predicate;
        gpointer            user_data;
} FindClosure;

static gboolean
find_func (const char    *id,
           StoredDisplay *stored_display,
           FindClosure   *closure)
{
        return closure->predicate (id,
                                   stored_display->display,
                                   closure->user_data);
}

static void
foreach_func (const char    *id,
              StoredDisplay *stored_display,
              FindClosure   *closure)
{
        (void) closure->predicate (id,
                                   stored_display->display,
                                   closure->user_data);
}

void
gdm_display_store_foreach (GdmDisplayStore    *store,
                           GdmDisplayStoreFunc func,
                           gpointer            user_data)
{
        FindClosure  closure;

        g_return_if_fail (GDM_IS_DISPLAY_STORE (store));
        g_return_if_fail (func != NULL);

        closure.predicate = func;
        closure.user_data = user_data;

        g_hash_table_foreach (store->displays,
                              (GHFunc) foreach_func,
                              &closure);
}

GdmDisplay *
gdm_display_store_lookup (GdmDisplayStore *store,
                          const char      *id)
{
        StoredDisplay *stored_display;

        g_return_val_if_fail (GDM_IS_DISPLAY_STORE (store), NULL);
        g_return_val_if_fail (id != NULL, NULL);

        stored_display = g_hash_table_lookup (store->displays,
                                              id);
        if (stored_display == NULL) {
                return NULL;
        }

        return stored_display->display;
}

GdmDisplay *
gdm_display_store_find (GdmDisplayStore    *store,
                        GdmDisplayStoreFunc predicate,
                        gpointer            user_data)
{
        StoredDisplay *stored_display;
        FindClosure    closure;

        g_return_val_if_fail (GDM_IS_DISPLAY_STORE (store), NULL);
        g_return_val_if_fail (predicate != NULL, NULL);

        closure.predicate = predicate;
        closure.user_data = user_data;

        stored_display = g_hash_table_find (store->displays,
                                            (GHRFunc) find_func,
                                            &closure);

        if (stored_display == NULL) {
                return NULL;
        }

        return stored_display->display;
}

guint
gdm_display_store_foreach_remove (GdmDisplayStore    *store,
                                  GdmDisplayStoreFunc func,
                                  gpointer            user_data)
{
        FindClosure closure;
        guint       ret;

        g_return_val_if_fail (GDM_IS_DISPLAY_STORE (store), 0);
        g_return_val_if_fail (func != NULL, 0);

        closure.predicate = func;
        closure.user_data = user_data;

        ret = g_hash_table_foreach_remove (store->displays,
                                           (GHRFunc) find_func,
                                           &closure);
        return ret;
}

void
gdm_display_store_add (GdmDisplayStore *store,
                       GdmDisplay      *display)
{
        char          *id;
        StoredDisplay *stored_display;

        g_return_if_fail (GDM_IS_DISPLAY_STORE (store));
        g_return_if_fail (display != NULL);

        gdm_display_get_id (display, &id, NULL);

        g_debug ("GdmDisplayStore: Adding display %s to store", id);

        stored_display = stored_display_new (store, display);
        g_hash_table_insert (store->displays,
                             id,
                             stored_display);

        g_signal_emit (G_OBJECT (store),
                       signals[DISPLAY_ADDED],
                       0,
                       id);
}

static void
gdm_display_store_class_init (GdmDisplayStoreClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_display_store_finalize;

        signals [DISPLAY_ADDED] =
                g_signal_new ("display-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [DISPLAY_REMOVED] =
                g_signal_new ("display-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE,
                              1, G_TYPE_OBJECT);
}

static void
gdm_display_store_init (GdmDisplayStore *store)
{
        store->displays = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 (GDestroyNotify)
                                                 stored_display_free);
}

static void
gdm_display_store_finalize (GObject *object)
{
        GdmDisplayStore *store;

        g_return_if_fail (GDM_IS_DISPLAY_STORE (object));

        store = GDM_DISPLAY_STORE (object);

        g_hash_table_destroy (store->displays);

        G_OBJECT_CLASS (gdm_display_store_parent_class)->finalize (object);
}

GdmDisplayStore *
gdm_display_store_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_DISPLAY_STORE,
                               NULL);

        return GDM_DISPLAY_STORE (object);
}
