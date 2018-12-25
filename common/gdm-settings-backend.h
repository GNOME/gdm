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


#ifndef __GDM_SETTINGS_BACKEND_H
#define __GDM_SETTINGS_BACKEND_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SETTINGS_BACKEND (gdm_settings_backend_get_type ())
G_DECLARE_DERIVABLE_TYPE (GdmSettingsBackend, gdm_settings_backend, GDM, SETTINGS_BACKEND, GObject)

struct _GdmSettingsBackendClass
{
        GObjectClass   parent_class;

        /* methods */
        gboolean (*get_value)              (GdmSettingsBackend *settings_backend,
                                            const char         *key,
                                            char              **value,
                                            GError            **error);
        gboolean (*set_value)              (GdmSettingsBackend *settings_backend,
                                            const char         *key,
                                            const char         *value,
                                            GError            **error);

        /* signals */
        void          (* value_changed)    (GdmSettingsBackend *settings_backend,
                                            const char  *key,
                                            const char  *old_value,
                                            const char **new_value);
};

typedef enum
{
        GDM_SETTINGS_BACKEND_ERROR_GENERAL,
        GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND
} GdmSettingsBackendError;

#define GDM_SETTINGS_BACKEND_ERROR gdm_settings_backend_error_quark ()

GQuark              gdm_settings_backend_error_quark            (void);

gboolean            gdm_settings_backend_get_value              (GdmSettingsBackend *settings_backend,
                                                                 const char  *key,
                                                                 char       **value,
                                                                 GError     **error);
gboolean            gdm_settings_backend_set_value              (GdmSettingsBackend *settings_backend,
                                                                 const char  *key,
                                                                 const char  *value,
                                                                 GError     **error);

void                gdm_settings_backend_value_changed          (GdmSettingsBackend *settings_backend,
                                                                 const char  *key,
                                                                 const char  *old_value,
                                                                 const char  *new_value);

G_END_DECLS

#endif /* __GDM_SETTINGS_BACKEND_H */
