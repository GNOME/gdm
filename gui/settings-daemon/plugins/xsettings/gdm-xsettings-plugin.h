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

#ifndef __GDM_XSETTINGS_PLUGIN_H__
#define __GDM_XSETTINGS_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

#include "gdm-settings-plugin.h"

G_BEGIN_DECLS

#define GDM_TYPE_XSETTINGS_PLUGIN                (gdm_xsettings_plugin_get_type ())
#define GDM_XSETTINGS_PLUGIN(o)                  (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_XSETTINGS_PLUGIN, GdmXsettingsPlugin))
#define GDM_XSETTINGS_PLUGIN_CLASS(k)            (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_XSETTINGS_PLUGIN, GdmXsettingsPluginClass))
#define GDM_IS_XSETTINGS_PLUGIN(o)               (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_XSETTINGS_PLUGIN))
#define GDM_IS_XSETTINGS_PLUGIN_CLASS(k)         (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_XSETTINGS_PLUGIN))
#define GDM_XSETTINGS_PLUGIN_GET_CLASS(o)        (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_XSETTINGS_PLUGIN, GdmXsettingsPluginClass))

typedef struct GdmXsettingsPluginPrivate GdmXsettingsPluginPrivate;

typedef struct
{
        GdmSettingsPlugin          parent;
        GdmXsettingsPluginPrivate *priv;
} GdmXsettingsPlugin;

typedef struct
{
        GdmSettingsPluginClass parent_class;
} GdmXsettingsPluginClass;

GType   gdm_xsettings_plugin_get_type            (void) G_GNUC_CONST;

/* All the plugins must implement this function */
G_MODULE_EXPORT GType register_gdm_settings_plugin (GTypeModule *module);

G_END_DECLS

#endif /* __GDM_XSETTINGS_PLUGIN_H__ */
