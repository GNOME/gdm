/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The Gnome Display Manager
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

#include <gtk/gtk.h>

#include "misc.h"

#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_SUSPEND 17	/* Suspend (don't use, use the interrupt) */
#define DISPLAY_CHOSEN 20	/* successful chooser session,
				   restart display */
#define DISPLAY_RUN_CHOOSER 30	/* Run chooser */
#define DISPLAY_XFAILED 64	/* X failed */
#define DISPLAY_GREETERFAILED 65 /* greeter failed (crashed) */
#define DISPLAY_RESTARTGREETER 127 /* Restart greeter */
#define DISPLAY_RESTARTGDM 128	/* Restart GDM */

char     *gdm_common_get_a_cookie           (gboolean binary);

/* Handle error messages */
void      gdm_common_log_init               (void);
void      gdm_common_log_set_debug          (gboolean enable);
void	  gdm_common_fail_exit		    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);
void	  gdm_common_fail_greeter	    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);
void	  gdm_common_info		    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);
void	  gdm_common_error		    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);
void	  gdm_common_warning		    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);
void	  gdm_common_debug		    (const gchar *format, ...)
					     G_GNUC_PRINTF (1, 2);

/* Misc. Common Functions */
void	  gdm_common_setup_cursor	    (GdkCursorType type);

void      gdm_common_login_sound            (const gchar *GdmSoundProgram,
                                             const gchar *GdmSoundOnLoginReadyFile,
                                             gboolean     GdmSoundOnLoginReady);

void	  gdm_common_setup_blinking	    (void);
void	  gdm_common_setup_blinking_entry   (GtkWidget *entry);

GdkPixbuf *gdm_common_get_face              (const char *filename,
                                             const char *fallback_filename,
                                             guint       max_width,
                                             guint       max_height);

gchar*	  gdm_common_text_to_escaped_utf8   (const char *text);
gchar*	  gdm_common_get_config_file	    (void);
gchar*	  gdm_common_get_custom_config_file (void);
gboolean  gdm_common_select_time_format	    (void);
void	  gdm_common_setup_background_color (gchar *bg_color);
void      gdm_common_set_root_background    (GdkPixbuf *pb);
gchar*	  gdm_common_get_welcomemsg	    (void);
void	  gdm_common_pre_fetch_launch       (void);
void      gdm_common_atspi_launch           (void);
gchar*    gdm_common_expand_text            (const gchar *text);
gchar*    gdm_common_get_clock              (struct tm **the_tm);

#endif /* GDM_COMMON_H */
