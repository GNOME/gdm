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


#ifndef __GDM_SETTINGS_DIRECT_H
#define __GDM_SETTINGS_DIRECT_H

#include <glib-object.h>
#include "gdm-settings.h"

G_BEGIN_DECLS

gboolean              gdm_settings_direct_init                       (GdmSettings       *settings,
                                                                      const char        *schemas_file,
                                                                      const char        *root);

void                  gdm_settings_direct_reload                     (void);
void                  gdm_settings_direct_shutdown                   (void);

gboolean              gdm_settings_direct_get                        (const char        *key,
                                                                      GValue            *value);
gboolean              gdm_settings_direct_set                        (const char        *key,
                                                                      GValue            *value);
gboolean              gdm_settings_direct_get_int                    (const char        *key,
                                                                      int               *value);
gboolean              gdm_settings_direct_get_uint                   (const char        *key,
                                                                      uint              *value);
gboolean              gdm_settings_direct_get_boolean                (const char        *key,
                                                                      gboolean          *value);
gboolean              gdm_settings_direct_get_string                 (const char        *key,
                                                                      char             **value);

G_END_DECLS

#endif /* __GDM_SETTINGS_DIRECT_H */
