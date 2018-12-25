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


#ifndef __GDM_SETTINGS_DESKTOP_BACKEND_H
#define __GDM_SETTINGS_DESKTOP_BACKEND_H

#include <glib-object.h>
#include "gdm-settings-backend.h"

G_BEGIN_DECLS

#define GDM_TYPE_SETTINGS_DESKTOP_BACKEND (gdm_settings_desktop_backend_get_type ())
G_DECLARE_FINAL_TYPE (GdmSettingsDesktopBackend, gdm_settings_desktop_backend, GDM, SETTINGS_DESKTOP_BACKEND, GdmSettingsBackend)

GdmSettingsBackend        *gdm_settings_desktop_backend_new             (const char* filename);

G_END_DECLS

#endif /* __GDM_SETTINGS_DESKTOP_BACKEND_H */
