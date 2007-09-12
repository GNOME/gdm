/* -*- mode: c; style: linux -*- */

/* preferences.h
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen <hovinen@helixcode.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __BGPREFERENCES_H
#define __BGPREFERENCES_H

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gconf/gconf-client.h>

#define BG_PREFERENCES(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, bg_preferences_get_type (), BGPreferences)
#define BG_PREFERENCES_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, bg_preferences_get_type (), BGPreferencesClass)
#define IS_BG_PREFERENCES(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, bg_preferences_get_type ())

#define BG_PREFERENCES_DRAW_BACKGROUND    "/desktop/gnome/background/draw_background"
#define BG_PREFERENCES_PRIMARY_COLOR      "/desktop/gnome/background/primary_color"
#define BG_PREFERENCES_SECONDARY_COLOR    "/desktop/gnome/background/secondary_color"
#define BG_PREFERENCES_COLOR_SHADING_TYPE "/desktop/gnome/background/color_shading_type"
#define BG_PREFERENCES_PICTURE_OPTIONS    "/desktop/gnome/background/picture_options"
#define BG_PREFERENCES_PICTURE_OPACITY    "/desktop/gnome/background/picture_opacity"
#define BG_PREFERENCES_PICTURE_FILENAME   "/desktop/gnome/background/picture_filename"


typedef struct _BGPreferences BGPreferences;
typedef struct _BGPreferencesClass BGPreferencesClass;

typedef enum _orientation_t {
	ORIENTATION_SOLID = 0, 
	ORIENTATION_HORIZ, 
	ORIENTATION_VERT
} orientation_t;

typedef enum _wallpaper_type_t {
	WPTYPE_TILED = 0, WPTYPE_CENTERED, WPTYPE_SCALED,
	WPTYPE_STRETCHED, WPTYPE_ZOOM, WPTYPE_NONE,
	WPTYPE_UNSET
} wallpaper_type_t;

struct _BGPreferences
{
	GObject           object;

	gint              frozen;
	gboolean          auto_apply;
	guint             timeout_id;

	gboolean          enabled;
	gboolean          gradient_enabled;
	gboolean          wallpaper_enabled;
	orientation_t     orientation;
	wallpaper_type_t  wallpaper_type;

	GdkColor         *color1;
	GdkColor         *color2;

	gchar            *wallpaper_filename;
	gchar            *wallpaper_sel_path;

	GSList           *wallpapers;

	gboolean          adjust_opacity;
	gint              opacity;
};

struct _BGPreferencesClass
{
	GObjectClass klass;
};

GType    bg_preferences_get_type    (void);

GObject *bg_preferences_new         (void);
GObject *bg_preferences_clone       (const BGPreferences   *prefs);

void     bg_preferences_load        (BGPreferences         *prefs);

void     bg_preferences_merge_entry (BGPreferences         *prefs,
				     const GConfEntry      *entry);

void     bg_preferences_save        (BGPreferences *prefs);

const gchar *bg_preferences_get_wptype_as_string      (wallpaper_type_t wp);
const gchar *bg_preferences_get_orientation_as_string (orientation_t    o);
GType        bg_preferences_wptype_get_type	      (void);
GType	     bg_preferences_orientation_get_type      (void);

	
#endif /* __PREFERENCES_H */
