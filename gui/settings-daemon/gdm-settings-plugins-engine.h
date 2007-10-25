/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2002-2005 - Paolo Maggi
 * Copyright (C) 2007        William Jon McCann <mccann@jhu.edu>
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

#ifndef __GDM_SETTINGS_PLUGINS_ENGINE_H__
#define __GDM_SETTINGS_PLUGINS_ENGINE_H__

#include <glib.h>

typedef struct _GdmSettingsPluginInfo GdmSettingsPluginInfo;

gboolean         gdm_settings_plugins_engine_init                   (void);
void             gdm_settings_plugins_engine_shutdown               (void);

void             gdm_settings_plugins_engine_garbage_collect        (void);

const GList     *gdm_settings_plugins_engine_get_plugins_list       (void);

void             gdm_settings_plugins_engine_activate_all           (void);

gboolean         gdm_settings_plugins_engine_activate_plugin        (GdmSettingsPluginInfo *info);
gboolean         gdm_settings_plugins_engine_deactivate_plugin      (GdmSettingsPluginInfo *info);
gboolean         gdm_settings_plugins_engine_plugin_is_active       (GdmSettingsPluginInfo *info);
gboolean         gdm_settings_plugins_engine_plugin_is_available    (GdmSettingsPluginInfo *info);

const char      *gdm_settings_plugins_engine_get_plugin_name        (GdmSettingsPluginInfo *info);
const char      *gdm_settings_plugins_engine_get_plugin_description (GdmSettingsPluginInfo *info);
const char     **gdm_settings_plugins_engine_get_plugin_authors     (GdmSettingsPluginInfo *info);
const char      *gdm_settings_plugins_engine_get_plugin_website     (GdmSettingsPluginInfo *info);
const char      *gdm_settings_plugins_engine_get_plugin_copyright   (GdmSettingsPluginInfo *info);

#endif  /* __GDM_SETTINGS_PLUGINS_ENGINE_H__ */
