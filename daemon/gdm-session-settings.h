/* gdm-session-settings.h - Object for auditing session login/logout
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */
#ifndef GDM_SESSION_SETTINGS_H
#define GDM_SESSION_SETTINGS_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_SETTINGS (gdm_session_settings_get_type ())
G_DECLARE_FINAL_TYPE (GdmSessionSettings, gdm_session_settings, GDM, SESSION_SETTINGS, GObject)

GdmSessionSettings *gdm_session_settings_new                (void);

gboolean            gdm_session_settings_load               (GdmSessionSettings  *settings,
                                                             const char          *username);
gboolean            gdm_session_settings_save               (GdmSessionSettings  *settings,
                                                             const char          *username);
gboolean            gdm_session_settings_is_loaded          (GdmSessionSettings  *settings);
char               *gdm_session_settings_get_language_name  (GdmSessionSettings *settings);
char               *gdm_session_settings_get_session_name   (GdmSessionSettings *settings);
char               *gdm_session_settings_get_session_type   (GdmSessionSettings *settings);
void                gdm_session_settings_set_language_name  (GdmSessionSettings *settings,
                                                             const char         *language_name);
void                gdm_session_settings_set_session_name   (GdmSessionSettings *settings,
                                                             const char         *session_name);
void                gdm_session_settings_set_session_type   (GdmSessionSettings *settings,
                                                             const char         *session_type);

G_END_DECLS
#endif /* GDM_SESSION_SETTINGS_H */
