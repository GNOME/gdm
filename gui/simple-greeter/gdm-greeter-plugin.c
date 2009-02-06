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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gdm-greeter-extension.h"
#include "gdm-greeter-plugin.h"

#define GDM_GREETER_PLUGIN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_PLUGIN, GdmGreeterPluginPrivate))

enum {
        PROP_0,
        PROP_FILENAME,
};

enum {
        LOADED,
        LOAD_FAILED,
        UNLOADED,
        LAST_SIGNAL
};

struct _GdmGreeterPluginPrivate {
        GObject              parent;

        GModule             *module;
        char                *filename;

        GdmGreeterExtension *extension;
};

static void gdm_greeter_plugin_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdmGreeterPlugin, gdm_greeter_plugin, G_TYPE_OBJECT)

static void
gdm_greeter_plugin_set_property (GObject      *object,
                                 guint         param_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        GdmGreeterPlugin *plugin;

        plugin = GDM_GREETER_PLUGIN (object);
        switch (param_id) {
        case PROP_FILENAME:
                plugin->priv->filename = g_strdup (g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_greeter_plugin_get_property (GObject    *object,
                                 guint       param_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
        GdmGreeterPlugin *plugin;

        plugin = GDM_GREETER_PLUGIN (object);

        switch (param_id) {
        case PROP_FILENAME:
                g_value_set_string (value, plugin->priv->filename);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_greeter_plugin_class_init (GdmGreeterPluginClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->set_property = gdm_greeter_plugin_set_property;
        gobject_class->get_property = gdm_greeter_plugin_get_property;
        gobject_class->finalize = gdm_greeter_plugin_finalize;

        g_object_class_install_property (gobject_class,
                                         PROP_FILENAME,
                                         g_param_spec_string ("filename",
                                                              "Filename",
                                                              "The full path to the plugin.",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        signals [LOADED] =
                g_signal_new ("loaded",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPluginClass, loaded),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [LOAD_FAILED] =
                g_signal_new ("load-failed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPluginClass, load_failed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [UNLOADED] =
                g_signal_new ("unloaded",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPluginClass, unloaded),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        g_type_class_add_private (class, sizeof (GdmGreeterPluginPrivate));
}

GdmGreeterPlugin *
gdm_greeter_plugin_new (const char *filename)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_PLUGIN,
                               "filename", filename, NULL);

        return GDM_GREETER_PLUGIN (object);
}

void
gdm_greeter_plugin_load (GdmGreeterPlugin *plugin)
{
        GModule *module;
        GdmGreeterExtension *extension;
        union {
                gpointer symbol;
                GdmGreeterPluginGetExtensionFunc invoke;
        } get_extension;


        module = g_module_open (plugin->priv->filename, G_MODULE_BIND_LOCAL);

        if (module == NULL) {
                g_warning ("plugin %s couldn't be opened: %s",
                           plugin->priv->filename,
                           g_module_error ());
                g_signal_emit (plugin, signals [LOAD_FAILED], 0);
                return;
        }

        if (!g_module_symbol (module,
                              "gdm_greeter_plugin_get_extension",
                              &get_extension.symbol) ||
            !get_extension.symbol) {
                g_warning ("plugin %s lacks gdm_greeter_plugin_get_extension()",
                           plugin->priv->filename);
                g_module_close (module);
                g_signal_emit (plugin, signals [LOAD_FAILED], 0);
                return;
        }

        extension = get_extension.invoke ();

        if (!extension) {
                g_warning ("plugin %s didn't return extension when asked",
                           plugin->priv->filename);
                g_module_close (module);
                g_signal_emit (plugin, signals [LOAD_FAILED], 0);
        }

        if (!GDM_IS_GREETER_EXTENSION (extension)) {
                g_warning ("plugin %s returned bogus extension when asked",
                           plugin->priv->filename);
                g_module_close (module);
                g_signal_emit (plugin, signals [LOAD_FAILED], 0);
        }

        plugin->priv->module = module;
        plugin->priv->extension = extension;

        g_signal_emit (plugin, signals [LOADED], 0);
}

void
gdm_greeter_plugin_unload (GdmGreeterPlugin *plugin)
{
        if (plugin->priv->extension != NULL) {
                g_object_unref (plugin->priv->extension);
                plugin->priv->extension = NULL;
        }

        if (plugin->priv->module != NULL) {
                g_module_close (plugin->priv->module);
                plugin->priv->module = NULL;
        }
}

const char *
gdm_greeter_plugin_get_filename (GdmGreeterPlugin *plugin)
{
        return plugin->priv->filename;
}

GdmGreeterExtension *
gdm_greeter_plugin_get_extension (GdmGreeterPlugin *plugin)
{
        return g_object_ref (plugin->priv->extension);
}

static void
gdm_greeter_plugin_init (GdmGreeterPlugin *plugin)
{
        plugin->priv = GDM_GREETER_PLUGIN_GET_PRIVATE (plugin);
}

static void
gdm_greeter_plugin_finalize (GObject *object)
{
        GdmGreeterPlugin *plugin;

        plugin = GDM_GREETER_PLUGIN (object);

        gdm_greeter_plugin_unload (plugin);
}

