/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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

#include "config.h"
#include <locale.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <string.h>
#include <syslog.h>

#include <vicious.h>
#include "viciousui.h"

#include "gdm.h"

#include "gdmwm.h"
#include "gdmcommon.h"

void
gdm_common_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	_exit (DISPLAY_GREETERFAILED);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    closelog ();

    g_free (s);

    _exit (DISPLAY_GREETERFAILED);
}

void
gdm_common_setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

gboolean
gdm_common_string_same (VeConfig *config, const char *cur, const char *key)
{
	char *val = ve_config_get_string (config, key);
	if (strcmp (ve_sure_string (cur), ve_sure_string (val)) == 0) {
		g_free (val);
		return TRUE;
	} else {
		g_free (val);
		return FALSE;
	}
}

gboolean
gdm_common_bool_same (VeConfig *config, gboolean cur, const char *key)
{
	gboolean val = ve_config_get_bool (config, key);
	if (ve_bool_equal (cur, val)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
gdm_common_int_same (VeConfig *config, int cur, const char *key)
{
	int val = ve_config_get_int (config, key);
	if (cur == val) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void
gdm_common_login_sound (const gchar *GdmSoundProgram,
			const gchar *GdmSoundOnLoginReadyFile,
			gboolean     GdmSoundOnLoginReady)
{
	if ( ! GdmSoundOnLoginReady)
		return;

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")) ||
	    ve_string_empty (GdmSoundProgram) ||
	    ve_string_empty (GdmSoundOnLoginReadyFile) ||
	    access (GdmSoundProgram, F_OK) != 0 ||
	    access (GdmSoundOnLoginReadyFile, F_OK) != 0) {
		gdk_beep ();
	} else {
		/* login sound interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_LOGIN_SOUND);
		fflush (stdout);
	}
}

typedef struct {
	GtkWidget *entry;
	gboolean   blink;
} EntryBlink;

static GSList *entries = NULL;
static guint noblink_timeout = 0;

#define NOBLINK_TIMEOUT (20*1000)

static void
setup_blink (gboolean blink)
{
	GSList *li;
	for (li = entries; li != NULL; li = li->next) {
		EntryBlink *eb = li->data;
		if (eb->blink) {
			GtkSettings *settings
				= gtk_widget_get_settings (eb->entry);
			g_object_set (settings,
				      "gtk-cursor-blink", blink, NULL);
			gtk_widget_queue_resize (eb->entry);
		}
	}
}

static gboolean
no_blink (gpointer data)
{
	noblink_timeout = 0;
	setup_blink (FALSE);
	return FALSE;
}

static gboolean
delay_noblink (GSignalInvocationHint *ihint,
	       guint	           n_param_values,
	       const GValue	  *param_values,
	       gpointer		   data)
{
	setup_blink (TRUE);
	if (noblink_timeout > 0)
		g_source_remove (noblink_timeout);
	noblink_timeout
		= g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
	return TRUE;
}      


void
gdm_setup_blinking (void)
{
	guint sid;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	sid = g_signal_lookup ("activate",
			       GTK_TYPE_MENU_ITEM);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	sid = g_signal_lookup ("key_press_event",
			       GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	sid = g_signal_lookup ("button_press_event",
			       GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	noblink_timeout = g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
}

void
gdm_setup_blinking_entry (GtkWidget *entry)
{
	EntryBlink *eb;
	GtkSettings *settings;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	eb = g_new0 (EntryBlink, 1);

	eb->entry = entry;
	settings = gtk_widget_get_settings (eb->entry);
	g_object_get (settings, "gtk-cursor-blink", &(eb->blink), NULL);

	entries = g_slist_prepend (entries, eb);
}

GdkPixbuf *
gdm_common_get_face (const char *filename,
		     const char *fallback_filename,
		     guint       max_width,
		     guint       max_height)
{
	GdkPixbuf *pixbuf    = NULL;

	/* If we don't have a filename then try the fallback */
	if (! filename) {
		GtkIconTheme *theme;
		int           icon_size = 48;

		/* If we don't have a fallback then return NULL */
		if (! fallback_filename)
			return NULL;

		/* Try to load an icon from the theme before the fallback */
		theme = gtk_icon_theme_get_default ();
		pixbuf = gtk_icon_theme_load_icon (theme, "stock_person", icon_size, 0, NULL);
		if (! pixbuf)
			pixbuf = gdk_pixbuf_new_from_file (fallback_filename, NULL);
	} else {
		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	}

	if (pixbuf) {
		guint w, h;
	
		w = gdk_pixbuf_get_width (pixbuf);
		h = gdk_pixbuf_get_height (pixbuf);

		if (w > h && w > max_width) {
			h = h * ((gfloat) max_width / w);
			w = max_width;
		} else if (h > max_height) {
			w = w * ((gfloat) max_height / h);
			h = max_height;
		}

		if (w != gdk_pixbuf_get_width (pixbuf) ||
		    h != gdk_pixbuf_get_height (pixbuf)) {
			GdkPixbuf *img;

			img = gdk_pixbuf_scale_simple (pixbuf, w, h, GDK_INTERP_BILINEAR);
			g_object_unref (pixbuf);
			pixbuf = img;
		}
        }

	return pixbuf;
}
