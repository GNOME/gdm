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


#ifndef __GDM_SETTINGS_H
#define __GDM_SETTINGS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SETTINGS (gdm_settings_get_type ())
G_DECLARE_FINAL_TYPE (GdmSettings, gdm_settings, GDM, SETTINGS, GObject)

typedef enum
{
        GDM_SETTINGS_ERROR_GENERAL,
        GDM_SETTINGS_ERROR_KEY_NOT_FOUND
} GdmSettingsError;

#define GDM_SETTINGS_ERROR gdm_settings_error_quark ()

GQuark              gdm_settings_error_quark                    (void);

GdmSettings *       gdm_settings_new                            (void);
void                gdm_settings_reload                         (GdmSettings *settings);

/* exported */

gboolean            gdm_settings_get_value                      (GdmSettings *settings,
                                                                 const char  *key,
                                                                 char       **value,
                                                                 GError     **error);
gboolean            gdm_settings_set_value                      (GdmSettings *settings,
                                                                 const char  *key,
                                                                 const char  *value,
                                                                 GError     **error);

G_END_DECLS

#endif /* __GDM_SETTINGS_H */
