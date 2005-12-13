/* GDM - The GNOME Display Manager
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
#include "gdm.h"

/* To access ve_string_empty(), ve_sure_string(), and
   VE_IGNORE_EINTR macros ve-misc.h must be included */

#include "ve-misc.h"

gchar*         gdm_get_value_string     (gchar *key);
gboolean       gdm_get_value_bool       (gchar *key);
gint           gdm_get_value_int        (gchar *key);
void           gdm_set_value_string     (gchar *key,
                                         gchar *value);
void           gdm_set_value_bool       (gchar *key,
                                         gboolean value);
void           gdm_set_value_int        (gchar *key,
                                         gint value);
gboolean       gdm_config_to_string     (gchar *key,
                                         gchar **retval);
gboolean       gdm_update_config        (gchar *key);
void           gdm_config_init          (void);
void           gdm_config_parse         (void);
GdmXserver*    gdm_find_x_server        (const gchar *id);
gchar*         gdm_get_x_servers        (void);
int            gdm_compare_displays     (gconstpointer a,
                                         gconstpointer b);
uid_t          gdm_get_gdmuid           (void);
uid_t          gdm_get_gdmgid           (void);
gint           gdm_get_high_display_num (void);
void           gdm_set_high_display_num (gint val);
void           gdm_print_all_config     (void);
gboolean       gdm_is_valid_key         (gchar *key);
gboolean       gdm_signal_terminthup_was_notified  (void);

gchar*         gdm_get_facefile_from_home
                                        (const char *homedir,
                                         guint uid);
gchar*         gdm_get_facefile_from_global
                                        (const char *username,
                                         guint uid);
void           gdm_get_user_session_lang
                                        (char **usrsess,
                                         char **usrlang,
                                         const char *homedir,
                                         gboolean *savesess);
void		gdm_set_user_session_lang (gboolean savesess,
					 gboolean savelang,
					 const char *home_dir,
					 const char *save_session,
					 const char *save_language);
char*          gdm_get_session_exec     (const char *session_name,
                                         gboolean check_try_exec);

