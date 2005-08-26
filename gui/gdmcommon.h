/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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

#ifndef GDM_COMMON_H
#define GDM_COMMON_H

#include <vicious.h>

#include "misc.h"

void    gdm_common_show_info_msg        (const gchar *msg_file,
                                         const gchar *msg_font);
void	gdm_common_message		(const gchar *primary_message, 
					 const gchar *secondary_message);
void	gdm_common_abort		(const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void	gdm_common_setup_cursor		(GdkCursorType type);
gint	gdm_common_query		(const gchar *primary_message,
					 const gchar *secondary_message,
					 const char *posbutton,
					 const char *negbutton,
					 gboolean has_cancel);
gint	gdm_common_warn 		(const gchar *primary_message,
					 const gchar *secondary_message,
					 const char *posbutton,
					 const char *negbutton,
					 gboolean has_cancel);

gboolean gdm_common_string_same		(VeConfig *config,
					 const char *cur, const char *key);
gboolean gdm_common_bool_same		(VeConfig *config,
					 gboolean cur, const char *key);
gboolean gdm_common_int_same		(VeConfig *config,
					 int cur, const char *key);

void    gdm_common_login_sound          (const gchar *GdmSoundProgram,
                                         const gchar *GdmSoundOnLoginReadyFile,
                                         gboolean     GdmSoundOnLoginReady);

void	gdm_setup_blinking		(void);
void	gdm_setup_blinking_entry	(GtkWidget *entry);
gint	gdm_session_sort_func		(const char *a, const char *b);
GdkPixbuf *gdm_common_get_face          (const char *filename,
                                         const char *fallback_filename,
                                         guint       max_width,
                                         guint       max_height);
gchar*	gdm_common_get_config_file	(void);

gboolean gdm_common_select_time_format	(VeConfig *config);

#endif /* GDM_COMMON_H */
