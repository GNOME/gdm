/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include "gdm-settings-plugin.h"
#include "gdm-xsettings-plugin.h"
#include "gdm-xsettings-manager.h"

struct GdmXsettingsPluginPrivate {
        GdmXsettingsManager *manager;
};

#define GDM_XSETTINGS_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GDM_TYPE_XSETTINGS_PLUGIN, GdmXsettingsPluginPrivate))

GDM_SETTINGS_PLUGIN_REGISTER (GdmXsettingsPlugin, gdm_xsettings_plugin)

static void
gdm_xsettings_plugin_init (GdmXsettingsPlugin *plugin)
{
        plugin->priv = GDM_XSETTINGS_PLUGIN_GET_PRIVATE (plugin);

        g_debug ("GdmXsettingsPlugin initializing");

        plugin->priv->manager = gdm_xsettings_manager_new ();
}

static void
gdm_xsettings_plugin_finalize (GObject *object)
{
        GdmXsettingsPlugin *plugin;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_XSETTINGS_PLUGIN (object));

        g_debug ("GdmXsettingsPlugin finalizing");

        plugin = GDM_XSETTINGS_PLUGIN (object);

        g_return_if_fail (plugin->priv != NULL);

        if (plugin->priv->manager != NULL) {
                g_object_unref (plugin->priv->manager);
        }

        G_OBJECT_CLASS (gdm_xsettings_plugin_parent_class)->finalize (object);
}

static void
impl_activate (GdmSettingsPlugin *plugin)
{
        gboolean res;
        GError  *error;

        g_debug ("Activating xsettings plugin");

        error = NULL;
        res = gdm_xsettings_manager_start (GDM_XSETTINGS_PLUGIN (plugin)->priv->manager, &error);
        if (! res) {
                g_warning ("Unable to start xsettings manager: %s", error->message);
                g_error_free (error);
        }
}

static void
impl_deactivate (GdmSettingsPlugin *plugin)
{
        g_debug ("Deactivating xsettings plugin");
}

static void
gdm_xsettings_plugin_class_init (GdmXsettingsPluginClass *klass)
{
        GObjectClass           *object_class = G_OBJECT_CLASS (klass);
        GdmSettingsPluginClass *plugin_class = GDM_SETTINGS_PLUGIN_CLASS (klass);

        object_class->finalize = gdm_xsettings_plugin_finalize;

        plugin_class->activate = impl_activate;
        plugin_class->deactivate = impl_deactivate;

        g_type_class_add_private (klass, sizeof (GdmXsettingsPluginPrivate));
}
