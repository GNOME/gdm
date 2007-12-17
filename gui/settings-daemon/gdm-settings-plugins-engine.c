/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 * Copyright (C) 2007      William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gmodule.h>
#include <gconf/gconf-client.h>

#include "gdm-settings-plugins-engine.h"
#include "gdm-settings-plugin.h"

#include "gdm-settings-module.h"

#define PLUGIN_EXT ".gdm-settings-plugin"
#define PLUGIN_GROUP "GDM Settings Plugin"

typedef enum
{
        GDM_SETTINGS_PLUGIN_LOADER_C,
        GDM_SETTINGS_PLUGIN_LOADER_PY,
} GdmSettingsPluginLoader;

struct _GdmSettingsPluginInfo
{
        char                    *file;

        char                    *location;
        GdmSettingsPluginLoader  loader;
        GTypeModule             *module;

        char                    *name;
        char                    *desc;
        char                   **authors;
        char                    *copyright;
        char                    *website;

        GdmSettingsPlugin       *plugin;

        gint                     active : 1;

        guint                    active_notification_id;

        /* A plugin is unavailable if it is not possible to activate it
           due to an error loading the plugin module (e.g. for Python plugins
           when the interpreter has not been correctly initializated) */
        gint                     available : 1;
};

static char        *gdm_settings_gconf_prefix = NULL;
static GHashTable  *gdm_settings_plugins = NULL;
static GConfClient *client = NULL;

static void
gdm_settings_plugin_info_free (GdmSettingsPluginInfo *info)
{
        if (info->plugin != NULL) {
                g_debug ("Unref plugin %s", info->name);

                g_object_unref (info->plugin);

                /* info->module must not be unref since it is not possible to finalize
                 * a type module */
        }

        g_free (info->file);
        g_free (info->location);
        g_free (info->name);
        g_free (info->desc);
        g_free (info->website);
        g_free (info->copyright);
        g_strfreev (info->authors);

        g_free (info);
}

static GdmSettingsPluginInfo *
gdm_settings_plugins_engine_load (const char *file)
{
        GdmSettingsPluginInfo *info;
        GKeyFile              *plugin_file = NULL;
        char                  *str;

        g_return_val_if_fail (file != NULL, NULL);

        g_debug ("Loading plugin: %s", file);

        info = g_new0 (GdmSettingsPluginInfo, 1);
        info->file = g_strdup (file);

        plugin_file = g_key_file_new ();
        if (! g_key_file_load_from_file (plugin_file, file, G_KEY_FILE_NONE, NULL)) {
                g_warning ("Bad plugin file: %s", file);
                goto error;
        }

        if (! g_key_file_has_key (plugin_file, PLUGIN_GROUP, "IAge", NULL)) {
                g_debug ("IAge key does not exist in file: %s", file);
                goto error;
        }

        /* Check IAge=2 */
        if (g_key_file_get_integer (plugin_file, PLUGIN_GROUP, "IAge", NULL) != 0) {
                g_debug ("Wrong IAge in file: %s", file);
                goto error;
        }

        /* Get Location */
        str = g_key_file_get_string (plugin_file, PLUGIN_GROUP, "Module", NULL);

        if ((str != NULL) && (*str != '\0')) {
                info->location = str;
        } else {
                g_warning ("Could not find 'Module' in %s", file);
                goto error;
        }

        /* Get the loader for this plugin */
        str = g_key_file_get_string (plugin_file, PLUGIN_GROUP, "Loader", NULL);
        if (str && strcmp(str, "python") == 0) {
                info->loader = GDM_SETTINGS_PLUGIN_LOADER_PY;
#ifndef ENABLE_PYTHON
                g_warning ("Cannot load Python plugin '%s' since gdm_settings was not "
                           "compiled with Python support.", file);
                goto error;
#endif
        } else {
                info->loader = GDM_SETTINGS_PLUGIN_LOADER_C;
        }
        g_free (str);

        /* Get Name */
        str = g_key_file_get_locale_string (plugin_file, PLUGIN_GROUP, "Name", NULL, NULL);
        if (str) {
                info->name = str;
        } else {
                g_warning ("Could not find 'Name' in %s", file);
                goto error;
        }

        /* Get Description */
        str = g_key_file_get_locale_string (plugin_file, PLUGIN_GROUP, "Description", NULL, NULL);
        if (str)
                info->desc = str;
        else
                g_debug ("Could not find 'Description' in %s", file);

        /* Get Authors */
        info->authors = g_key_file_get_string_list (plugin_file, PLUGIN_GROUP, "Authors", NULL, NULL);
        if (info->authors == NULL)
                g_debug ("Could not find 'Authors' in %s", file);

        /* Get Copyright */
        str = g_key_file_get_string (plugin_file, PLUGIN_GROUP, "Copyright", NULL);
        if (str)
                info->copyright = str;
        else
                g_debug ("Could not find 'Copyright' in %s", file);

        /* Get Website */
        str = g_key_file_get_string (plugin_file, PLUGIN_GROUP, "Website", NULL);
        if (str)
                info->website = str;
        else
                g_debug ("Could not find 'Website' in %s", file);

        g_key_file_free (plugin_file);

        /* If we know nothing about the availability of the plugin,
           set it as available */
        info->available = TRUE;

        return info;

error:
        g_free (info->file);
        g_free (info->location);
        g_free (info->name);
        g_free (info);
        g_key_file_free (plugin_file);

        return NULL;
}

static void
gdm_settings_plugins_engine_plugin_active_cb (GConfClient           *client,
                                              guint                  cnxn_id,
                                              GConfEntry            *entry,
                                              GdmSettingsPluginInfo *info)
{
        if (gconf_value_get_bool (entry->value)) {
                gdm_settings_plugins_engine_activate_plugin (info);
        } else {
                gdm_settings_plugins_engine_deactivate_plugin (info);
        }
}

static void
gdm_settings_plugins_engine_load_file (const char *filename)
{
        GdmSettingsPluginInfo *info;
        char                  *key_name;
        gboolean               activate;

        if (g_str_has_suffix (filename, PLUGIN_EXT) == FALSE) {
                return;
        }

        info = gdm_settings_plugins_engine_load (filename);
        if (info == NULL) {
                return;
        }

        if (g_hash_table_lookup (gdm_settings_plugins, info->location)) {
                gdm_settings_plugin_info_free (info);
                return;
        }

        g_hash_table_insert (gdm_settings_plugins, info->location, info);

        key_name = g_strdup_printf ("%s/%s", gdm_settings_gconf_prefix, info->location);
        gconf_client_add_dir (client, key_name, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
        g_free (key_name);

        key_name = g_strdup_printf ("%s/%s/active", gdm_settings_gconf_prefix, info->location);

        info->active_notification_id = gconf_client_notify_add (client,
                                                                key_name,
                                                                (GConfClientNotifyFunc)gdm_settings_plugins_engine_plugin_active_cb,
                                                                info,
                                                                NULL,
                                                                NULL);

        g_debug ("Reading gconf key: %s", key_name);
        activate = gconf_client_get_bool (client, key_name, NULL);
        g_free (key_name);

        if (activate) {
                gboolean res;
                res = gdm_settings_plugins_engine_activate_plugin (info);
                if (res) {
                        g_debug ("Plugin %s: active", info->location);
                } else {
                        g_debug ("Plugin %s: activation failed", info->location);
                }
        } else {
                g_debug ("Plugin %s: inactive", info->location);
        }
}

static void
gdm_settings_plugins_engine_load_dir (const char *path)
{
        GError     *error;
        GDir       *d;
        const char *name;

        g_debug ("Loading settings plugins from dir: %s", path);

        error = NULL;
        d = g_dir_open (path, 0, &error);
        if (d == NULL) {
                g_warning (error->message);
                g_error_free (error);
                return;
        }

        while ((name = g_dir_read_name (d))) {
                char *filename;

                filename = g_build_filename (path, name, NULL);
                if (g_file_test (filename, G_FILE_TEST_IS_DIR) != FALSE) {
                        gdm_settings_plugins_engine_load_dir (filename);
                } else {
                        gdm_settings_plugins_engine_load_file (filename);
                }
                g_free (filename);

        }

        g_dir_close (d);
}

static void
gdm_settings_plugins_engine_load_all (void)
{
        /* load system plugins */
        gdm_settings_plugins_engine_load_dir (GDM_SETTINGS_PLUGINDIR "/");
}

gboolean
gdm_settings_plugins_engine_init (const char *gconf_prefix)
{
        g_return_val_if_fail (gdm_settings_plugins == NULL, FALSE);
        g_return_val_if_fail (gconf_prefix != NULL, FALSE);

        if (!g_module_supported ()) {
                g_warning ("gdm_settings is not able to initialize the plugins engine.");
                return FALSE;
        }

        gdm_settings_plugins = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      NULL,
                                                      (GDestroyNotify)gdm_settings_plugin_info_free);

        gdm_settings_gconf_prefix = g_strdup (gconf_prefix);

        client = gconf_client_get_default ();

        gdm_settings_plugins_engine_load_all ();

        return TRUE;
}

void
gdm_settings_plugins_engine_garbage_collect (void)
{
#ifdef ENABLE_PYTHON
        gdm_settings_python_garbage_collect ();
#endif
}

void
gdm_settings_plugins_engine_shutdown (void)
{

#ifdef ENABLE_PYTHON
        /* Note: that this may cause finalization of objects by
         * running the garbage collector. Since some of the plugin may
         * have installed callbacks upon object finalization it must
         * run before we get rid of the plugins.
         */
        gdm_settings_python_shutdown ();
#endif

        if (gdm_settings_plugins != NULL) {
                g_hash_table_destroy (gdm_settings_plugins);
                gdm_settings_plugins = NULL;
        }

        if (client != NULL) {
                g_object_unref (client);
                client = NULL;
        }

        g_free (gdm_settings_gconf_prefix);
        gdm_settings_gconf_prefix = NULL;
}

static void
collate_values_cb (gpointer key,
                   gpointer value,
                   GList  **list)
{
        *list = g_list_prepend (*list, value);
}

const GList *
gdm_settings_plugins_engine_get_plugins_list (void)
{
        GList *list = NULL;

        if (gdm_settings_plugins == NULL) {
                return NULL;
        }

        g_hash_table_foreach (gdm_settings_plugins, (GHFunc)collate_values_cb, &list);
        list = g_list_reverse (list);

        return list;
}

static gboolean
load_plugin_module (GdmSettingsPluginInfo *info)
{
        char *path;
        char *dirname;

        g_return_val_if_fail (info != NULL, FALSE);
        g_return_val_if_fail (info->file != NULL, FALSE);
        g_return_val_if_fail (info->location != NULL, FALSE);
        g_return_val_if_fail (info->plugin == NULL, FALSE);
        g_return_val_if_fail (info->available, FALSE);

        switch (info->loader) {
                case GDM_SETTINGS_PLUGIN_LOADER_C:
                        dirname = g_path_get_dirname (info->file);
                        g_return_val_if_fail (dirname != NULL, FALSE);

                        path = g_module_build_path (dirname, info->location);
                        g_free (dirname);
                        g_return_val_if_fail (path != NULL, FALSE);

                        info->module = G_TYPE_MODULE (gdm_settings_module_new (path));
                        g_free (path);

                        break;

#ifdef ENABLE_PYTHON
                case GDM_SETTINGS_PLUGIN_LOADER_PY:
                {
                        char *dir;

                        if (!gdm_settings_python_init ()) {
                                /* Mark plugin as unavailable and fails */
                                info->available = FALSE;

                                g_warning ("Cannot load Python plugin '%s' since gdm_settings "
                                           "was not able to initialize the Python interpreter.",
                                           info->name);

                                return FALSE;
                        }

                        dir = g_path_get_dirname (info->file);

                        g_return_val_if_fail ((info->location != NULL) &&
                                              (info->location[0] != '\0'),
                                              FALSE);

                        info->module = G_TYPE_MODULE (
                                        gdm_settings_python_module_new (dir, info->location));

                        g_free (dir);
                        break;
                }
#endif
                default:
                        g_return_val_if_reached (FALSE);
        }

        if (!g_type_module_use (info->module)) {
                switch (info->loader) {
                        case GDM_SETTINGS_PLUGIN_LOADER_C:
                                g_warning ("Cannot load plugin '%s' since file '%s' cannot be read.",
                                           info->name,
                                           gdm_settings_module_get_path (GDM_SETTINGS_MODULE (info->module)));
                                break;

                        case GDM_SETTINGS_PLUGIN_LOADER_PY:
                                g_warning ("Cannot load Python plugin '%s' since file '%s' cannot be read.",
                                           info->name,
                                           info->location);
                                break;

                        default:
                                g_return_val_if_reached (FALSE);
                }

                g_object_unref (G_OBJECT (info->module));
                info->module = NULL;

                /* Mark plugin as unavailable and fails */
                info->available = FALSE;

                return FALSE;
        }

        switch (info->loader) {
                case GDM_SETTINGS_PLUGIN_LOADER_C:
                        info->plugin =
                                GDM_SETTINGS_PLUGIN (gdm_settings_module_new_object (GDM_SETTINGS_MODULE (info->module)));
                        break;

#ifdef ENABLE_PYTHON
                case GDM_SETTINGS_PLUGIN_LOADER_PY:
                        info->plugin =
                                GDM_SETTINGS_PLUGIN (gdm_settings_python_module_new_object (GDM_SETTINGS_PYTHON_MODULE (info->module)));
                        break;
#endif

                default:
                        g_return_val_if_reached (FALSE);
        }

        g_type_module_unuse (info->module);

        return TRUE;
}

static gboolean
gdm_settings_plugins_engine_activate_plugin_real (GdmSettingsPluginInfo *info)
{
        gboolean res = TRUE;

        if (!info->available) {
                /* Plugin is not available, don't try to activate/load it */
                return FALSE;
        }

        if (info->plugin == NULL)
                res = load_plugin_module (info);

        if (res) {
                gdm_settings_plugin_activate (info->plugin);
        } else {
                g_warning ("Error activating plugin '%s'", info->name);
        }

        return res;
}

gboolean
gdm_settings_plugins_engine_activate_plugin (GdmSettingsPluginInfo *info)
{

        g_return_val_if_fail (info != NULL, FALSE);

        if (! info->available) {
                return FALSE;
        }

        if (info->active) {
                return TRUE;
        }

        if (gdm_settings_plugins_engine_activate_plugin_real (info)) {
                char *key_name;

                key_name = g_strdup_printf ("%s/%s/active",
                                            gdm_settings_gconf_prefix,
                                            info->location);
                gconf_client_set_bool (client, key_name, TRUE, NULL);
                g_free (key_name);

                info->active = TRUE;

                return TRUE;
        }

        return FALSE;
}

static void
gdm_settings_plugins_engine_deactivate_plugin_real (GdmSettingsPluginInfo *info)
{
        gdm_settings_plugin_deactivate (info->plugin);
}

gboolean
gdm_settings_plugins_engine_deactivate_plugin (GdmSettingsPluginInfo *info)
{
        char *key_name;

        g_return_val_if_fail (info != NULL, FALSE);

        if (!info->active || !info->available) {
                return TRUE;
        }

        gdm_settings_plugins_engine_deactivate_plugin_real (info);

        /* Update plugin state */
        info->active = FALSE;

        key_name = g_strdup_printf ("%s/%s/active",
                                    gdm_settings_gconf_prefix,
                                    info->location);
        gconf_client_set_bool (client, key_name, FALSE, NULL);
        g_free (key_name);

        return TRUE;
}

gboolean
gdm_settings_plugins_engine_plugin_is_active (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, FALSE);

        return (info->available && info->active);
}

gboolean
gdm_settings_plugins_engine_plugin_is_available (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, FALSE);

        return (info->available != FALSE);
}

const char *
gdm_settings_plugins_engine_get_plugin_name (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->name;
}

const char *
gdm_settings_plugins_engine_get_plugin_description (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->desc;
}

const char **
gdm_settings_plugins_engine_get_plugin_authors (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, (const char **)NULL);

        return (const char **)info->authors;
}

const char *
gdm_settings_plugins_engine_get_plugin_website (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->website;
}

const char *
gdm_settings_plugins_engine_get_plugin_copyright (GdmSettingsPluginInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->copyright;
}
