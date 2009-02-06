/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Written By: Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-plugin-manager.h"
#include "gdm-greeter-extension.h"

#define GDM_PLUGIN_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_PLUGIN_MANAGER, GdmPluginManagerPrivate))

typedef struct
{
        GModule             *module;
        char                *filename;
        GdmGreeterExtension *extension;
} GdmPluginManagerPlugin;

typedef struct
{
        GdmPluginManager *manager;
        GCancellable     *cancellable;
} GdmPluginManagerOperation;

struct GdmPluginManagerPrivate
{
        GHashTable      *plugins;

        GFileMonitor    *plugin_dir_monitor;
        GList           *pending_operations;
};

enum {
        PLUGINS_LOADED,
        PLUGIN_ADDED,
        PLUGIN_REMOVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_plugin_manager_class_init (GdmPluginManagerClass *klass);
static void     gdm_plugin_manager_init       (GdmPluginManager      *plugin_manager);
static void     gdm_plugin_manager_finalize   (GObject             *object);

static GObject *plugin_manager_object = NULL;

G_DEFINE_TYPE (GdmPluginManager, gdm_plugin_manager, G_TYPE_OBJECT)

static GdmPluginManagerOperation *
start_operation (GdmPluginManager *manager)
{
        GdmPluginManagerOperation *operation;

        operation = g_new0 (GdmPluginManagerOperation, 1);
        operation->cancellable = g_cancellable_new ();
        operation->manager = manager;

        return operation;
}

static void
free_operation (GdmPluginManagerOperation *operation)
{
        if (operation->cancellable != NULL) {
                g_object_unref (operation->cancellable);
                operation->cancellable = NULL;
        }
        g_free (operation);
}

static void
cancel_operation (GdmPluginManagerOperation *operation)
{
        if (operation->cancellable != NULL &&
            !g_cancellable_is_cancelled (operation->cancellable)) {
                g_cancellable_cancel (operation->cancellable);
        }

        free_operation (operation);
}

static void
gdm_plugin_manager_track_operation (GdmPluginManager          *manager,
                                    GdmPluginManagerOperation *operation)
{
        manager->priv->pending_operations =
            g_list_prepend (manager->priv->pending_operations, operation);
}

static void
gdm_plugin_manager_untrack_operation (GdmPluginManager          *manager,
                                     GdmPluginManagerOperation *operation)
{
        manager->priv->pending_operations =
            g_list_remove (manager->priv->pending_operations, operation);
}

static void
gdm_plugin_manager_cancel_pending_operations (GdmPluginManager *manager)
{
        GList *node;

        node = manager->priv->pending_operations;
        while (node != NULL) {
                GList *next_node;
                GdmPluginManagerOperation *operation;

                operation = node->data;
                next_node = node->next;

                cancel_operation (operation);
                manager->priv->pending_operations =
                    g_list_delete_link (manager->priv->pending_operations,
                                        node);

                node = next_node;
        }
}

static void
gdm_plugin_manager_class_init (GdmPluginManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_plugin_manager_finalize;

        signals [PLUGINS_LOADED] =
                g_signal_new ("plugins-loaded",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmPluginManagerClass, plugins_loaded),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [PLUGIN_ADDED] =
                g_signal_new ("plugin-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmPluginManagerClass, plugin_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_GREETER_PLUGIN);
        signals [PLUGIN_REMOVED] =
                g_signal_new ("plugin-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmPluginManagerClass, plugin_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_GREETER_PLUGIN);

        g_type_class_add_private (klass, sizeof (GdmPluginManagerPrivate));
}

static void
on_plugin_loaded (GdmPluginManager *manager,
                  GdmGreeterPlugin *plugin)
{
        g_debug ("GdmPluginManager: plugin '%s' loaded.",
                 gdm_greeter_plugin_get_filename (plugin));
        g_signal_emit (manager, signals [PLUGIN_ADDED], 0, plugin);
}

static void
on_plugin_load_failed (GdmPluginManager *manager,
                       GdmGreeterPlugin *plugin)
{
        const char *filename;

        g_debug ("GdmPluginManager: plugin '%s' could not be loaded.",
                 gdm_greeter_plugin_get_filename (plugin));
        filename = gdm_greeter_plugin_get_filename (plugin);
        g_hash_table_remove (manager->priv->plugins, filename);
}

static void
on_plugin_unloaded (GdmPluginManager       *manager,
                    GdmGreeterPlugin *plugin)
{
        const char *filename;

        filename = gdm_greeter_plugin_get_filename (plugin);
        g_hash_table_remove (manager->priv->plugins, filename);
}

static void
load_plugin (GdmPluginManager *manager,
             const char       *filename)
{
        GdmGreeterPlugin *plugin;

        g_debug ("GdmPluginManager: loading plugin '%s'", filename);

        plugin = gdm_greeter_plugin_new (filename);

        g_signal_connect_swapped (plugin, "loaded",
                                  G_CALLBACK (on_plugin_loaded),
                                  manager);
        g_signal_connect_swapped (plugin, "load-failed",
                                  G_CALLBACK (on_plugin_load_failed),
                                  manager);
        g_signal_connect_swapped (plugin, "unloaded",
                                  G_CALLBACK (on_plugin_unloaded),
                                  manager);
        g_hash_table_insert (manager->priv->plugins,
                             g_strdup (filename), plugin);

        gdm_greeter_plugin_load (plugin);
}

static void
on_plugin_info_read (GFileEnumerator           *enumerator,
                     GAsyncResult              *result,
                     GdmPluginManagerOperation *operation)
{
        GdmPluginManager *manager;
        GFile *plugin_dir_file;
        GList *file_list, *node;
        GError *error;

        manager = operation->manager;
        error = NULL;
        file_list = g_file_enumerator_next_files_finish (enumerator,
                                                         result, &error);
        plugin_dir_file = g_file_enumerator_get_container (enumerator);
        if (error != NULL) {
                char  *plugin_dir;

                plugin_dir = g_file_get_parse_name (plugin_dir_file);
                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        g_debug ("GdmPluginManager: Cancelled reading plugin directory %s",
                                 plugin_dir);
                } else {
                        g_warning ("GdmPluginManager: Unable to read plugin directory %s: %s",
                                   plugin_dir, error->message);
                }
                g_free (plugin_dir);
                g_error_free (error);
                g_object_unref (plugin_dir_file);
                gdm_plugin_manager_untrack_operation (manager, operation);
                return;
        }

#ifndef PLUGIN_ORDERING_FIGURED_OUT
        node = file_list;
        while (node != NULL) {
                GFileInfo *info;
                GFile *file;
                char *path;
                GList *next_node;

                next_node = node->next;

                info = (GFileInfo *) node->data;

                file = g_file_get_child (plugin_dir_file,
                                         g_file_info_get_name (info));
                path = g_file_get_path (file);

                if (g_str_has_suffix (path, "password.so")) {
                        file_list = g_list_delete_link (file_list, node);
                        file_list = g_list_prepend (file_list, info);
                        next_node = NULL;
                }
                g_free (path);
                g_object_unref (file);

                node = next_node;
        }
#endif

        node = file_list;
        while (node != NULL) {
                GFileInfo *info;
                GFile *file;
                char *path;

                info = (GFileInfo *) node->data;

                file = g_file_get_child (plugin_dir_file,
                                         g_file_info_get_name (info));
                path = g_file_get_path (file);

                if (g_str_has_suffix (path, G_MODULE_SUFFIX)) {
                        load_plugin (manager, path);
                }
                g_free (path);
                g_object_unref (file);

                node = node->next;
        }
        g_object_unref (plugin_dir_file);

        gdm_plugin_manager_untrack_operation (manager, operation);
        g_signal_emit (manager, signals [PLUGINS_LOADED], 0);

        g_list_free (file_list);
}

static void
on_plugin_dir_opened (GFile                     *plugin_dir_file,
                      GAsyncResult              *result,
                      GdmPluginManagerOperation *open_operation)
{
        GdmPluginManager *manager;
        GFileEnumerator *enumerator;
        GError *error;
        GdmPluginManagerOperation *operation;

        manager = open_operation->manager;
        gdm_plugin_manager_untrack_operation (manager, open_operation);

        error = NULL;
        enumerator = g_file_enumerate_children_finish (plugin_dir_file,
                                                       result, &error);

        if (enumerator == NULL) {
                char *plugin_dir;

                plugin_dir = g_file_get_parse_name (plugin_dir_file);

                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        g_debug ("GdmPluginManager: Cancelled opening plugin directory %s",
                                 plugin_dir);
                } else {
                        g_warning ("GdmPluginManager: Unable to open plugin directory %s: %s",
                                   plugin_dir, error->message);
                }
                g_free (plugin_dir);
                g_error_free (error);
                return;
        }

        operation = start_operation (manager);

        g_file_enumerator_next_files_async (enumerator, G_MAXINT,
                                            G_PRIORITY_DEFAULT,
                                            operation->cancellable,
                                            (GAsyncReadyCallback)
                                            on_plugin_info_read,
                                            operation);

        gdm_plugin_manager_track_operation (manager, operation);
}

static void
load_plugins_in_dir (GdmPluginManager *manager,
                     const char       *plugin_dir)
{
        GFile *plugin_dir_file;
        GdmPluginManagerOperation *operation;

        g_debug ("GdmPluginManager: loading plugins in dir '%s'", plugin_dir);

        operation = start_operation (manager);
        plugin_dir_file = g_file_new_for_path (plugin_dir);
        g_file_enumerate_children_async (plugin_dir_file, "standard::*",
                                         G_FILE_QUERY_INFO_NONE,
                                         G_PRIORITY_DEFAULT,
                                         operation->cancellable,
                                         (GAsyncReadyCallback)
                                         on_plugin_dir_opened,
                                         operation);
        g_object_unref (plugin_dir_file);
        gdm_plugin_manager_track_operation (manager, operation);
}

static void
on_plugin_dir_changed (GFileMonitor              *monitor,
                       GFile                     *file,
                       GFile                     *other_file,
                       GFileMonitorEvent          event_type,
                       GdmPluginManagerOperation *operation)
{
}

static void
watch_plugin_dir (GdmPluginManager *manager,
                  const char       *plugin_dir)
{

        GdmPluginManagerOperation *operation;
        GFile  *file;
        GError *error;

        operation = start_operation (manager);

        file = g_file_new_for_path (plugin_dir);
        manager->priv->plugin_dir_monitor = g_file_monitor_directory (file,
                                                                      G_FILE_MONITOR_NONE,
                                                                      operation->cancellable,
                                                                      &error);
        if (manager->priv->plugin_dir_monitor != NULL) {
                g_signal_connect (manager->priv->plugin_dir_monitor,
                                  "changed",
                                  G_CALLBACK (on_plugin_dir_changed),
                                  operation);
                gdm_plugin_manager_track_operation (manager, operation);
        } else {
                g_warning ("Unable to monitor %s: %s",
                           plugin_dir, error->message);
                g_error_free (error);
                free_operation (operation);
        }
        g_object_unref (file);
}

static void
gdm_plugin_manager_init (GdmPluginManager *manager)
{
        manager->priv = GDM_PLUGIN_MANAGER_GET_PRIVATE (manager);

        manager->priv->plugins = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        g_object_unref);
        watch_plugin_dir (manager, GDM_SIMPLE_GREETER_PLUGINS_DIR);
        load_plugins_in_dir (manager, GDM_SIMPLE_GREETER_PLUGINS_DIR);
}

static void
gdm_plugin_manager_finalize (GObject *object)
{
        GdmPluginManager *manager;

        manager = GDM_PLUGIN_MANAGER (object);

        g_hash_table_destroy (manager->priv->plugins);
        g_file_monitor_cancel (manager->priv->plugin_dir_monitor);

        gdm_plugin_manager_cancel_pending_operations (manager);

        G_OBJECT_CLASS (gdm_plugin_manager_parent_class)->finalize (object);
}

GdmPluginManager *
gdm_plugin_manager_ref_default (void)
{
        if (plugin_manager_object != NULL) {
                g_object_ref (plugin_manager_object);
        } else {
                plugin_manager_object = g_object_new (GDM_TYPE_PLUGIN_MANAGER, NULL);
                g_object_add_weak_pointer (plugin_manager_object,
                                           (gpointer *) &plugin_manager_object);
        }

        return GDM_PLUGIN_MANAGER (plugin_manager_object);
}

GdmGreeterPlugin *
gdm_plugin_manager_get_plugin (GdmPluginManager *manager,
                               const char       *name)
{
        return g_hash_table_lookup (manager->priv->plugins, name);
}
