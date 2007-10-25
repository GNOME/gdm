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
        char                   *file;

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

        /* A plugin is unavailable if it is not possible to activate it
           due to an error loading the plugin module (e.g. for Python plugins
           when the interpreter has not been correctly initializated) */
        gint                     available : 1;
};

static GList  *gdm_settings_plugins_list = NULL;
static GSList *active_plugins = NULL;

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
        GKeyFile *plugin_file = NULL;
        char *str;

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

static gint
compare_plugin_info (GdmSettingsPluginInfo *info1,
                     GdmSettingsPluginInfo *info2)
{
        return strcmp (info1->location, info2->location);
}

static void
gdm_settings_plugins_engine_load_dir (const char *dir)
{
        GError *error = NULL;
        GDir *d;
        const char *dirent;

        g_debug ("DIR: %s", dir);

        d = g_dir_open (dir, 0, &error);
        if (!d) {
                g_warning (error->message);
                g_error_free (error);
                return;
        }

        while ((dirent = g_dir_read_name (d))) {
                if (g_str_has_suffix (dirent, PLUGIN_EXT)) {
                        char *plugin_file;
                        GdmSettingsPluginInfo *info;

                        plugin_file = g_build_filename (dir, dirent, NULL);
                        info = gdm_settings_plugins_engine_load (plugin_file);
                        g_free (plugin_file);

                        if (info == NULL)
                                continue;

                        /* If a plugin with this name has already been loaded
                         * drop this one (user plugins override system plugins) */
                        if (g_list_find_custom (gdm_settings_plugins_list,
                                                info,
                                                (GCompareFunc)compare_plugin_info) != NULL) {
                                g_warning ("Two or more plugins named '%s'. "
                                           "Only the first will be considered.\n",
                                           info->location);

                                gdm_settings_plugin_info_free (info);

                                continue;
                        }

                        /* Actually, the plugin will be activated when reactivate_all
                         * will be called for the first time. */
                        if (g_slist_find_custom (active_plugins, info->location, (GCompareFunc)strcmp) != NULL) {
                                info->active = TRUE;
                        } else {
                                info->active = FALSE;
                        }

                        gdm_settings_plugins_list = g_list_prepend (gdm_settings_plugins_list, info);

                        g_debug ("Plugin %s loaded", info->name);
                }
        }

        gdm_settings_plugins_list = g_list_reverse (gdm_settings_plugins_list);

        g_dir_close (d);
}

static void
gdm_settings_plugins_engine_load_all (void)
{
        /* load system plugins */
        gdm_settings_plugins_engine_load_dir (GDM_SETTINGS_PLUGINDIR "/");
}

gboolean
gdm_settings_plugins_engine_init (void)
{
        g_return_val_if_fail (gdm_settings_plugins_list == NULL, FALSE);

        if (!g_module_supported ()) {
                g_warning ("gdm_settings is not able to initialize the plugins engine.");
                return FALSE;
        }

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
        GList *pl;

#ifdef ENABLE_PYTHON
        /* Note: that this may cause finalization of objects by
         * running the garbage collector. Since some of the plugin may
         * have installed callbacks upon object finalization it must
         * run before we get rid of the plugins.
         */
        gdm_settings_python_shutdown ();
#endif

        for (pl = gdm_settings_plugins_list; pl; pl = pl->next) {
                GdmSettingsPluginInfo *info = (GdmSettingsPluginInfo*)pl->data;

                gdm_settings_plugin_info_free (info);
        }

        g_slist_foreach (active_plugins, (GFunc)g_free, NULL);
        g_slist_free (active_plugins);

        active_plugins = NULL;

        g_list_free (gdm_settings_plugins_list);
        gdm_settings_plugins_list = NULL;
}

const GList *
gdm_settings_plugins_engine_get_plugins_list (void)
{
        return gdm_settings_plugins_list;
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

        if (!info->available)
                return FALSE;

        if (info->active)
                return TRUE;

        if (gdm_settings_plugins_engine_activate_plugin_real (info)) {
                GSList *list;

                /* Update plugin state */
                info->active = TRUE;

                /* I want to be really sure :) */
                list = active_plugins;
                while (list != NULL) {
                        if (strcmp (info->location, (char *)list->data) == 0) {
                                g_warning ("Plugin '%s' is already active.", info->name);
                                return TRUE;
                        }

                        list = g_slist_next (list);
                }

                active_plugins = g_slist_insert_sorted (active_plugins,
                                                        g_strdup (info->location),
                                                        (GCompareFunc)strcmp);

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
        gboolean res;
        GSList *list;

        g_return_val_if_fail (info != NULL, FALSE);

        if (!info->active || !info->available)
                return TRUE;

        gdm_settings_plugins_engine_deactivate_plugin_real (info);

        /* Update plugin state */
        info->active = FALSE;

        list = active_plugins;
        res = (list == NULL);

        while (list != NULL) {
                if (strcmp (info->location, (char *)list->data) == 0) {
                        g_free (list->data);
                        active_plugins = g_slist_delete_link (active_plugins, list);
                        list = NULL;
                        res = TRUE;
                } else {
                        list = g_slist_next (list);
                }
        }

        if (!res) {
                g_warning ("Plugin '%s' is already deactivated.", info->name);
                return TRUE;
        }

        if (!res)
                g_warning ("Error saving the list of active plugins.");

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

static void
reactivate_all (void)
{
        GList *pl;

        for (pl = gdm_settings_plugins_list; pl; pl = pl->next) {
                GdmSettingsPluginInfo *info;
                gboolean               res;

                info = (GdmSettingsPluginInfo*)pl->data;

                g_debug ("Trying to activate plugin: %s", info->name);

                if (! info->available) {
                        g_debug ("Plugin is not available");
                        continue;
                }

                if (! info->available) {
                        g_debug ("Plugin is not active");
                        continue;
                }

                res = TRUE;
                if (info->plugin == NULL) {
                        res = load_plugin_module (info);
                }

                if (!res) {
                        g_debug ("Unable to load plugin module");
                        continue;
                }

                gdm_settings_plugin_activate (info->plugin);
        }
}

void
gdm_settings_plugins_engine_activate_all (void)
{
        g_debug ("Activating all plugins");

        reactivate_all ();
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
