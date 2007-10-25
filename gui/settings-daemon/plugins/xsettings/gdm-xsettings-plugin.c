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

#include "gdm-settings-plugin.h"
#include "gdm-xsettings-plugin.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#define GDM_XSETTINGS_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GDM_TYPE_XSETTINGS_PLUGIN, GdmXsettingsPluginPrivate))

GDM_SETTINGS_PLUGIN_REGISTER (GdmXsettingsPlugin, gdm_xsettings_plugin)

static void
gdm_xsettings_plugin_init (GdmXsettingsPlugin *plugin)
{
        g_debug ("GdmXsettingsPlugin initializing");
}

static void
gdm_xsettings_plugin_finalize (GObject *object)
{
        g_debug ("GdmXsettingsPlugin finalizing");

        G_OBJECT_CLASS (gdm_xsettings_plugin_parent_class)->finalize (object);
}

static void
impl_activate (GdmSettingsPlugin *plugin)
{
        g_debug ("Activating plugin");
}

static void
impl_deactivate (GdmSettingsPlugin *plugin)
{
        g_debug ("Deactivating plugin");
}

static void
gdm_xsettings_plugin_class_init (GdmXsettingsPluginClass *klass)
{
        GObjectClass           *object_class = G_OBJECT_CLASS (klass);
        GdmSettingsPluginClass *plugin_class = GDM_SETTINGS_PLUGIN_CLASS (klass);

        object_class->finalize = gdm_xsettings_plugin_finalize;

        plugin_class->activate = impl_activate;
        plugin_class->deactivate = impl_deactivate;
}
