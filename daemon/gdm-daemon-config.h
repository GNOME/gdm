/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _GDM_DAEMON_CONFIG_H
#define _GDM_DAEMON_CONFIG_H

#include "gdm-daemon-config-entries.h"

G_BEGIN_DECLS

gchar*         gdm_daemon_config_get_display_custom_config_file (const gchar *display);
gchar*         gdm_daemon_config_get_custom_config_file (void);

const char*    gdm_daemon_config_get_value_string     (const gchar *key);
gboolean       gdm_daemon_config_get_value_bool       (const gchar *key);
gint           gdm_daemon_config_get_value_int        (const gchar *key);
gchar*         gdm_daemon_config_get_value_string_per_display (const gchar *display,
                                                               const gchar *key);
gboolean       gdm_daemon_config_get_value_bool_per_display   (const gchar *display,
                                                               const gchar *key);
gint           gdm_daemon_config_get_value_int_per_display    (const gchar *display,
                                                               const gchar *key);


void           gdm_daemon_config_set_value_string     (const gchar *key,
                                                       const gchar *value);
void           gdm_daemon_config_set_value_bool       (const gchar *key,
                                                       gboolean value);
void           gdm_daemon_config_set_value_int        (const gchar *key,
                                                       gint value);


void           gdm_daemon_config_key_to_string_per_display (const gchar *display,
                                                                   const gchar *key,
                                                                   gchar **retval);
void           gdm_daemon_config_key_to_string        (const gchar *file,
                                                       const gchar *key,
                                                       gchar **retval);
void           gdm_daemon_config_to_string            (const gchar *key,
                                                       const gchar *display,
                                                       gchar **retval);
gboolean       gdm_daemon_config_update_key           (const gchar *key);


void           gdm_daemon_config_parse                (const char *config_file);
GdmXserver *   gdm_daemon_config_find_xserver         (const gchar *id);
gchar *        gdm_daemon_config_get_xservers         (void);
int            gdm_daemon_config_compare_displays     (gconstpointer a,
                                                       gconstpointer b);
uid_t          gdm_daemon_config_get_gdmuid           (void);
uid_t          gdm_daemon_config_get_gdmgid           (void);
gint           gdm_daemon_config_get_high_display_num (void);
void           gdm_daemon_config_set_high_display_num (gint val);
gboolean       gdm_daemon_config_is_valid_key         (const gchar *key);
gboolean       gdm_daemon_config_signal_terminthup_was_notified  (void);

gchar *        gdm_daemon_config_get_facefile_from_home (const char *homedir,
                                                         guint uid);
gchar *        gdm_daemon_config_get_facefile_from_global (const char *username,
                                                           guint uid);
void           gdm_daemon_config_get_user_session_lang (char **usrsess,
                                                        char **usrlang,
                                                        const char *homedir,
                                                        gboolean *savesess);
void		gdm_daemon_config_set_user_session_lang (gboolean savesess,
                                                         gboolean savelang,
                                                         const char *home_dir,
                                                         const char *save_session,
                                                         const char *save_language);
char *          gdm_daemon_config_get_session_exec     (const char *session_name,
                                                        gboolean check_try_exec);


G_END_DECLS

#endif /* _GDM_DAEMON_CONFIG_H */
