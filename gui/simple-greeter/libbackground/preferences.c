/* -*- mode: c; style: linux -*- */

/* preferences.c
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gdk/gdk.h>

#include "preferences.h"

static GObjectClass *parent_class;

static void      bg_preferences_init       (BGPreferences      *prefs,
					    BGPreferencesClass *class);
static void      bg_preferences_class_init (BGPreferencesClass *class);

static void      bg_preferences_finalize   (GObject            *object);

static GdkColor *read_color_from_string    (const gchar        *string);
static orientation_t read_orientation_from_string  (const gchar *string);
static wallpaper_type_t read_wptype_from_string (const gchar *string);

static GEnumValue _bg_wptype_values[] = {
	{ WPTYPE_TILED, "WPTYPE_TILED", "wallpaper"},
	{ WPTYPE_CENTERED, "WPTYPE_CENTERED", "centered"},
	{ WPTYPE_SCALED, "WPTYPE_SCALED", "scaled"},
	{ WPTYPE_STRETCHED, "WPTYPE_STRETCHED", "stretched"},
	{ WPTYPE_ZOOM, "WPTYPE_ZOOM", "zoom"},
	{ WPTYPE_NONE, "WPTYPE_NONE", "none"},
	{ 0, NULL, NULL }
};

static GEnumValue _bg_orientation_values[] = {
	{ ORIENTATION_SOLID, "ORIENTATION_SOLID", "solid"},
	{ ORIENTATION_HORIZ, "ORIENTATION_HORIZ", "horizontal-gradient"},
	{ ORIENTATION_VERT, "ORIENTATION_VERT", "vertical-gradient"},
	{ 0, NULL, NULL }
};

GType
bg_preferences_wptype_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		type = g_enum_register_static ("BgPreferencesWptype",
					       _bg_wptype_values);
	}

	return type;
}

GType
bg_preferences_orientation_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		type = g_enum_register_static ("BgPreferencesOrientation",
					       _bg_orientation_values);
	}

	return type;
}

GType
bg_preferences_get_type (void)
{
	static GType bg_preferences_type = 0;

	if (!bg_preferences_type) {
		GTypeInfo bg_preferences_info = {
			sizeof (BGPreferencesClass),
			NULL,
			NULL,
			(GClassInitFunc) bg_preferences_class_init,
			NULL,
			NULL,
			sizeof (BGPreferences),
			0,
			(GInstanceInitFunc) bg_preferences_init,
		};

		bg_preferences_type = 
			g_type_register_static (G_TYPE_OBJECT, "BGPreferences", &bg_preferences_info, 0);
	}

	return bg_preferences_type;
}

static void
bg_preferences_init (BGPreferences      *prefs,
		     BGPreferencesClass *class)
{
	prefs->frozen             = FALSE;

	/* Load default values */
	prefs->color1             = read_color_from_string ("#39374b");
	prefs->color2             = read_color_from_string ("#42528f");
	prefs->enabled            = TRUE;
	prefs->wallpaper_enabled  = FALSE;
	prefs->gradient_enabled   = TRUE;
	prefs->orientation        = ORIENTATION_VERT;
	prefs->wallpaper_type     = WPTYPE_TILED;
	prefs->wallpaper_filename = NULL;
	prefs->wallpaper_sel_path = g_strdup (g_get_home_dir ());
	prefs->auto_apply         = TRUE;
	prefs->wallpapers         = NULL;
	prefs->adjust_opacity     = TRUE;
	prefs->opacity            = 255;
}

static void
bg_preferences_class_init (BGPreferencesClass *class) 
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) class;
	object_class->finalize = bg_preferences_finalize;

	parent_class = 
		G_OBJECT_CLASS (g_type_class_ref (G_TYPE_OBJECT));
}

GObject *
bg_preferences_new (void) 
{
	GObject *object;

	object = g_object_new (bg_preferences_get_type (), NULL);
	BG_PREFERENCES (object)->enabled = TRUE;

	return object;
}

GObject *
bg_preferences_clone (const BGPreferences *prefs)
{
	GObject *object;
	BGPreferences *new_prefs;

	g_return_val_if_fail (prefs != NULL, NULL);
	g_return_val_if_fail (IS_BG_PREFERENCES (prefs), NULL);

	object = bg_preferences_new ();

	new_prefs = BG_PREFERENCES (object);

	new_prefs->enabled            = prefs->enabled;
	new_prefs->gradient_enabled   = prefs->gradient_enabled;
	new_prefs->wallpaper_enabled  = prefs->wallpaper_enabled;
	new_prefs->orientation        = prefs->orientation;
	new_prefs->wallpaper_type     = prefs->wallpaper_type;

	if (new_prefs->color1 != NULL)
		gdk_color_free (new_prefs->color1);
	new_prefs->color1 = (prefs->color1 != NULL)
		? gdk_color_copy (prefs->color1) : NULL;

	if (new_prefs->color2 != NULL)
		gdk_color_free (new_prefs->color2);
	new_prefs->color2 = (prefs->color2 != NULL)
		? gdk_color_copy (prefs->color2) : NULL;

	if (new_prefs->wallpaper_filename != NULL)
		g_free (new_prefs->wallpaper_filename);
	new_prefs->wallpaper_filename = g_strdup (prefs->wallpaper_filename);

	if (new_prefs->wallpaper_sel_path != NULL)
		g_free (new_prefs->wallpaper_sel_path);
	new_prefs->wallpaper_sel_path = g_strdup (prefs->wallpaper_sel_path);;

	new_prefs->auto_apply         = prefs->auto_apply;
	new_prefs->adjust_opacity     = prefs->adjust_opacity;
	new_prefs->opacity            = prefs->opacity;

	return object;
}

static void
bg_preferences_finalize (GObject *object) 
{
	BGPreferences *prefs;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (object));

	prefs = BG_PREFERENCES (object);

	g_free (prefs->wallpaper_filename);
	prefs->wallpaper_filename = NULL;

	g_free (prefs->wallpaper_sel_path);
	prefs->wallpaper_sel_path = NULL;

	if (prefs->color1 != NULL) {
		gdk_color_free (prefs->color1);
		prefs->color1 = NULL;
	}
	if (prefs->color2 != NULL) {
		gdk_color_free (prefs->color2);
		prefs->color2 = NULL;
	}
	
	parent_class->finalize (object);
}

void
bg_preferences_load (BGPreferences *prefs)
{
	GConfClient *client;
	GError      *error = NULL;
	char *tmp;

	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	client = gconf_client_get_default ();

	prefs->enabled = gconf_client_get_bool (client, BG_PREFERENCES_DRAW_BACKGROUND, &error);
	tmp = gconf_client_get_string (client, BG_PREFERENCES_PICTURE_FILENAME, &error);
	if (tmp) {
		if (g_utf8_validate (tmp, -1, NULL) &&
		    g_file_test (tmp, G_FILE_TEST_EXISTS))
			prefs->wallpaper_filename = g_strdup (tmp);
		else
			prefs->wallpaper_filename = g_filename_from_utf8 (tmp, -1, NULL,
									  NULL, NULL);
	}
	g_free (tmp);

	if (prefs->color1 != NULL)
		gdk_color_free (prefs->color1);
	tmp = gconf_client_get_string (client, BG_PREFERENCES_PRIMARY_COLOR, &error);
	prefs->color1 = read_color_from_string (tmp);
	g_free (tmp);

	if (prefs->color2 != NULL)
		gdk_color_free (prefs->color2);
	tmp = gconf_client_get_string (client, BG_PREFERENCES_SECONDARY_COLOR, &error);
	prefs->color2 = read_color_from_string (tmp);
	g_free (tmp);
	
	prefs->opacity = gconf_client_get_int (client, BG_PREFERENCES_PICTURE_OPACITY, &error);
	if (prefs->opacity >= 100 || prefs->opacity < 0)
		prefs->adjust_opacity = FALSE;

	tmp = gconf_client_get_string (client, BG_PREFERENCES_COLOR_SHADING_TYPE, &error);
	prefs->orientation = read_orientation_from_string (tmp);
	g_free (tmp);

	if (prefs->orientation == ORIENTATION_SOLID)
		prefs->gradient_enabled = FALSE;
	else
		prefs->gradient_enabled = TRUE;

	tmp = gconf_client_get_string (client, BG_PREFERENCES_PICTURE_OPTIONS, &error);
	prefs->wallpaper_type = read_wptype_from_string (tmp);
	g_free (tmp);

	if (prefs->wallpaper_type == WPTYPE_UNSET) {
	  prefs->wallpaper_enabled = FALSE;
	  prefs->wallpaper_type = WPTYPE_CENTERED;
	} else {
	  prefs->wallpaper_enabled = TRUE;
	}

	g_object_unref (client);
}

/* Parse the event name given (the event being notification of a property having
 * changed and apply that change to the bg_preferences structure. Eliminates the
 * need to reload the structure entirely on every event notification
 */

void
bg_preferences_merge_entry (BGPreferences    *prefs,
			    const GConfEntry *entry)
{
	const GConfValue *value = gconf_entry_get_value (entry);

	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	if (!strcmp (entry->key, BG_PREFERENCES_PICTURE_OPTIONS)) {
  	        wallpaper_type_t wallpaper_type = read_wptype_from_string (gconf_value_get_string (value));
		if (wallpaper_type == WPTYPE_UNSET) {
		  prefs->wallpaper_enabled = FALSE;
		} else {
		  prefs->wallpaper_type = wallpaper_type;
		  prefs->wallpaper_enabled = TRUE;
		}
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_PICTURE_FILENAME)) {
	        const char *tmp;

		tmp = gconf_value_get_string (value);
		if (g_utf8_validate (tmp, -1, NULL) &&
		    g_file_test (tmp, G_FILE_TEST_EXISTS))
		  prefs->wallpaper_filename = g_strdup (tmp);
		else
		  prefs->wallpaper_filename = g_filename_from_utf8 (tmp, -1,
								    NULL,
								    NULL,
								    NULL);

		if (prefs->wallpaper_filename != NULL &&
		    strcmp (prefs->wallpaper_filename, "") != 0 &&
		    strcmp (prefs->wallpaper_filename, "(none)") != 0)
			prefs->wallpaper_enabled = TRUE;
		else
			prefs->wallpaper_enabled = FALSE;
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_PRIMARY_COLOR)) {
		if (prefs->color1 != NULL)
			gdk_color_free (prefs->color1);
		prefs->color1 = read_color_from_string (gconf_value_get_string (value));
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_SECONDARY_COLOR)) {
		if (prefs->color2 != NULL)
			gdk_color_free (prefs->color2);
		prefs->color2 = read_color_from_string (gconf_value_get_string (value));
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_PICTURE_OPACITY)) {
		prefs->opacity = gconf_value_get_int (value);

		if (prefs->opacity >= 100)
			prefs->adjust_opacity = FALSE;
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_COLOR_SHADING_TYPE)) {
		prefs->orientation = read_orientation_from_string (gconf_value_get_string (value));

		if (prefs->orientation == ORIENTATION_SOLID)
			prefs->gradient_enabled = FALSE;
		else
			prefs->gradient_enabled = TRUE;
	}
	else if (!strcmp (entry->key, BG_PREFERENCES_DRAW_BACKGROUND)) {
		if (gconf_value_get_bool (value) &&
				(prefs->wallpaper_filename != NULL) &&
		    strcmp (prefs->wallpaper_filename, "") != 0 &&
		    strcmp (prefs->wallpaper_filename, "(none)") != 0)
			prefs->enabled = TRUE;
		else
			prefs->enabled = FALSE;
	} else {
		g_warning ("%s: Unknown property: %s", G_GNUC_FUNCTION, entry->key);
	}
}

static wallpaper_type_t
read_wptype_from_string (const gchar *string)
{
        wallpaper_type_t type = WPTYPE_UNSET;
      
	if (string) {
		if (!strncmp (string, "wallpaper", sizeof ("wallpaper"))) {
			type =  WPTYPE_TILED;
		} else if (!strncmp (string, "centered", sizeof ("centered"))) {
			type =  WPTYPE_CENTERED;
		} else if (!strncmp (string, "scaled", sizeof ("scaled"))) {
			type =  WPTYPE_SCALED;
		} else if (!strncmp (string, "stretched", sizeof ("stretched"))) {
			type =  WPTYPE_STRETCHED;
		} else if (!strncmp (string, "zoom", sizeof ("zoom"))) {
			type =  WPTYPE_ZOOM;
		}
	}

	return type;
}

static orientation_t
read_orientation_from_string (const gchar *string)
{
        orientation_t type = ORIENTATION_SOLID;

	if (string) {
		if (!strncmp (string, "vertical-gradient", sizeof ("vertical-gradient"))) {
			type = ORIENTATION_VERT;
		} else if (!strncmp (string, "horizontal-gradient", sizeof ("horizontal-gradient"))) {
			type = ORIENTATION_HORIZ;
		}
	}
	   
	return type;
}

static GdkColor *
read_color_from_string (const gchar *string) 
{
	GdkColor color;

	/* If all else fails use black */
	if (string == NULL || !gdk_color_parse (string, &color))
		gdk_color_parse ("black", &color);
	gdk_rgb_find_color (gdk_rgb_get_colormap (), &color);
	return gdk_color_copy (&color);
}

const gchar*
bg_preferences_get_wptype_as_string (wallpaper_type_t wp)
{
	switch (wp)
	{
		case WPTYPE_TILED:
			return "wallpaper";
		case WPTYPE_CENTERED:
			return "centered";
		case WPTYPE_SCALED:
			return "scaled";
		case WPTYPE_STRETCHED:
			return "stretched";
		case WPTYPE_ZOOM:
			return "zoom";
		case WPTYPE_NONE:
			return "none";
	        case WPTYPE_UNSET:
		        return NULL;
	}

	return NULL;
}

const gchar*
bg_preferences_get_orientation_as_string (orientation_t o)
{
	switch (o)
	{
		case ORIENTATION_SOLID:
			return "solid";
		case ORIENTATION_HORIZ:
			return "horizontal-gradient";
		case ORIENTATION_VERT:
			return "vertical-gradient";
	}

	return NULL;
}

void
bg_preferences_save (BGPreferences *prefs)
{
	GConfChangeSet *cs;
	gchar *tmp;
	GConfClient *client;
	
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	client = gconf_client_get_default();
	
	cs = gconf_change_set_new ();
	gconf_change_set_set_bool (cs, BG_PREFERENCES_DRAW_BACKGROUND, prefs->enabled);
	if (prefs->wallpaper_enabled)
		gconf_change_set_set_string (cs, BG_PREFERENCES_PICTURE_OPTIONS, bg_preferences_get_wptype_as_string (prefs->wallpaper_type));
	else
		gconf_change_set_set_string (cs, BG_PREFERENCES_PICTURE_OPTIONS, "none");

	gconf_change_set_set_string (cs, BG_PREFERENCES_PICTURE_FILENAME, prefs->wallpaper_filename);
	
	tmp = g_strdup_printf ("#%02x%02x%02x",
		prefs->color1->red >> 8,
		prefs->color1->green >> 8,
		prefs->color1->blue >> 8);
	gconf_change_set_set_string (cs, BG_PREFERENCES_PRIMARY_COLOR, tmp);
	g_free (tmp);
	
	tmp = g_strdup_printf ("#%02x%02x%02x",
		prefs->color2->red >> 8,
		prefs->color2->green >> 8,
		prefs->color2->blue >> 8);
	gconf_change_set_set_string (cs, BG_PREFERENCES_SECONDARY_COLOR, tmp);
	g_free (tmp);

	gconf_change_set_set_string (cs, BG_PREFERENCES_COLOR_SHADING_TYPE, bg_preferences_get_orientation_as_string (prefs->orientation));

	gconf_client_commit_change_set (client, cs, TRUE, NULL);
	gconf_change_set_unref (cs);
	g_object_unref (client);
}


