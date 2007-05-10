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

const char *   gdm_daemon_config_get_string_for_id    (int id);
gboolean       gdm_daemon_config_get_bool_for_id      (int id);
int            gdm_daemon_config_get_int_for_id       (int id);

void           gdm_daemon_config_parse                (const char *config_file,
                                                       gboolean    no_console);
GdmXserver *   gdm_daemon_config_find_xserver         (const char *id);
char *         gdm_daemon_config_get_xservers         (void);
GSList *       gdm_daemon_config_get_display_list     (void);
GSList *       gdm_daemon_config_display_list_append  (GdmDisplay *display);
GSList *       gdm_daemon_config_display_list_insert  (GdmDisplay *display);
GSList *       gdm_daemon_config_display_list_remove  (GdmDisplay *display);
uid_t          gdm_daemon_config_get_gdmuid           (void);
uid_t          gdm_daemon_config_get_gdmgid           (void);
gint           gdm_daemon_config_get_high_display_num (void);
void           gdm_daemon_config_set_high_display_num (gint val);

/* deprecated */
char *         gdm_daemon_config_get_display_custom_config_file (const char *display);
char *         gdm_daemon_config_get_custom_config_file (void);


const char *   gdm_daemon_config_get_value_string     (const char *key);
const char **  gdm_daemon_config_get_value_string_array (const char *key);
gboolean       gdm_daemon_config_get_value_bool       (const char *key);
gint           gdm_daemon_config_get_value_int        (const char *key);
char *         gdm_daemon_config_get_value_string_per_display (const char *key,
                                                               const char *display);
gboolean       gdm_daemon_config_get_value_bool_per_display   (const char *key,
                                                               const char *display);
gint           gdm_daemon_config_get_value_int_per_display    (const char *key,
                                                               const char *display);

void           gdm_daemon_config_set_value_string     (const char *key,
                                                       const char *value);
void           gdm_daemon_config_set_value_bool       (const char *key,
                                                       gboolean value);
void           gdm_daemon_config_set_value_int        (const char *key,
                                                       gint value);


gboolean       gdm_daemon_config_key_to_string_per_display (const char *display,
                                                            const char *key,
                                                            char **retval);
gboolean       gdm_daemon_config_key_to_string        (const char *file,
                                                       const char *key,
                                                       char **retval);
gboolean       gdm_daemon_config_to_string            (const char *key,
                                                       const char *display,
                                                       char **retval);
gboolean       gdm_daemon_config_update_key           (const char *key);


int            gdm_daemon_config_compare_displays     (gconstpointer a,
                                                       gconstpointer b);
gboolean       gdm_daemon_config_is_valid_key         (const char *key);
gboolean       gdm_daemon_config_signal_terminthup_was_notified  (void);

char *         gdm_daemon_config_get_facefile_from_home (const char *homedir,
                                                         guint uid);
char *         gdm_daemon_config_get_facefile_from_global (const char *username,
                                                           guint uid);
void           gdm_daemon_config_get_user_session_lang (char **usrsess,
                                                        char **usrlang,
                                                        const char *homedir,
                                                        gboolean *savesess);
void	       gdm_daemon_config_set_user_session_lang (gboolean savesess,
                                                        gboolean savelang,
                                                        const char *home_dir,
                                                        const char *save_session,
                                                        const char *save_language);
char *         gdm_daemon_config_get_session_exec     (const char *session_name,
                                                       gboolean check_try_exec);
char *         gdm_daemon_config_get_session_xserver_args (const char *session_name);


G_END_DECLS

#endif /* _GDM_DAEMON_CONFIG_H */
