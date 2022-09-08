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


#ifndef __GDM_SETTINGS_UTILS_H
#define __GDM_SETTINGS_UTILS_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GdmSettingsEntry GdmSettingsEntry;

GdmSettingsEntry *        gdm_settings_entry_new               (void);
void                      gdm_settings_entry_free              (GdmSettingsEntry *entry);

const char *              gdm_settings_entry_get_key           (GdmSettingsEntry *entry);
const char *              gdm_settings_entry_get_signature     (GdmSettingsEntry *entry);
const char *              gdm_settings_entry_get_default_value (GdmSettingsEntry *entry);
const char *              gdm_settings_entry_get_value         (GdmSettingsEntry *entry);

void                      gdm_settings_entry_set_value         (GdmSettingsEntry *entry,
                                                                const char       *value);

gboolean                  gdm_settings_parse_schemas           (const char  *file,
                                                                const char  *root,
                                                                GSList     **list);

gboolean                  gdm_settings_parse_value_as_boolean  (const char *value,
                                                                gboolean   *bool);
gboolean                  gdm_settings_parse_value_as_integer  (const char *value,
                                                                int        *intval);
gboolean                  gdm_settings_parse_value_as_double   (const char *value,
                                                                gdouble    *doubleval);

char *                    gdm_settings_parse_boolean_as_value  (gboolean    boolval);
char *                    gdm_settings_parse_integer_as_value  (int         intval);
char *                    gdm_settings_parse_double_as_value   (gdouble     doubleval);


G_END_DECLS

#endif /* __GDM_SETTINGS_UTILS_H */
