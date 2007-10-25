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

#ifndef __GDM_SETTINGS_MANAGER_H
#define __GDM_SETTINGS_MANAGER_H

#include <glib-object.h>

#include "gdm-settings-client.h"

G_BEGIN_DECLS

#define GDM_TYPE_SETTINGS_MANAGER         (gdm_settings_manager_get_type ())
#define GDM_SETTINGS_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SETTINGS_MANAGER, GdmSettingsManager))
#define GDM_SETTINGS_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SETTINGS_MANAGER, GdmSettingsManagerClass))
#define GDM_IS_SETTINGS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SETTINGS_MANAGER))
#define GDM_IS_SETTINGS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SETTINGS_MANAGER))
#define GDM_SETTINGS_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SETTINGS_MANAGER, GdmSettingsManagerClass))

typedef struct GdmSettingsManagerPrivate GdmSettingsManagerPrivate;

#define GDM_SETTINGS_ALL_LEVELS (GDM_SETTINGS_LEVEL_STARTUP | GDM_SETTINGS_LEVEL_CONFIGURATION | GDM_SETTINGS_LEVEL_LOGIN_WINDOW | GDM_SETTINGS_LEVEL_HOST_CHOOSER | GDM_SETTINGS_LEVEL_REMOTE_HOST | GDM_SETTINGS_LEVEL_SHUTDOWN)

typedef struct
{
        GObject                    parent;
        GdmSettingsManagerPrivate *priv;
} GdmSettingsManager;

typedef struct
{
        GObjectClass   parent_class;
} GdmSettingsManagerClass;

GType                gdm_settings_manager_get_type            (void);

GdmSettingsManager * gdm_settings_manager_new                 (void);
gboolean             gdm_settings_manager_start               (GdmSettingsManager *manager,
                                                               GError            **error);
void                 gdm_settings_manager_stop                (GdmSettingsManager *manager);

G_END_DECLS

#endif /* __GDM_SETTINGS_MANAGER_H */
