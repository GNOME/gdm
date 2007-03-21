/* GDM - The GNOME Display Manager - misc functions
 * Copyright (C) 1998, 1999, 2000 Martin K, Petersen <mkp@mkp.net>
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

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <string.h>
#include <stdio.h>

#include "misc.h"
#include "gdmconfig.h"

#include "gdm-common.h"

#define INDEX_FILE1 "index.theme"
#define INDEX_FILE2 "index.theme.disabled"

static char *
gdm_get_font (const char *theme_name)
{
	char *font_name;
	char *theme_dir;
	char *file_name;
	char buf[512];
	GtkStyle *style;
	FILE *fp;

	style = gtk_widget_get_default_style ();
	font_name = pango_font_description_to_string (style->font_desc);

	theme_dir = gtk_rc_get_theme_dir ();
	file_name = g_build_filename (theme_dir, theme_name, INDEX_FILE1, NULL);
	if ( ! g_file_test (file_name, G_FILE_TEST_EXISTS)) {
		g_free (file_name);
		file_name = g_build_filename (theme_dir, theme_name, INDEX_FILE2, NULL);
		if ( ! g_file_test (file_name, G_FILE_TEST_EXISTS)) {
			g_free (theme_dir);
			g_free (file_name);
			return font_name;
		}
	} 
	g_free (theme_dir);

	/*
	 * FIXME: this is evil!
	 */
	fp = fopen (file_name, "r");
	if (fp != NULL) {
		while (fgets (buf, 512, fp) != NULL) {
			if (strncmp ("ApplicationFont", buf, 15) == 0) {
				char *tmp_name;
				tmp_name = strchr (buf, '=');

				if (tmp_name != NULL) {
					g_free (font_name);
					font_name = strdup (tmp_name + 1);
				}

				fclose (fp);
				g_free (file_name);
				return font_name;
			}
		}
		fclose (fp);
	}
	g_free (file_name);
	return font_name;
}

/* perhaps needs to do something like:
    login_window_resize (FALSE);
    gdm_wm_center_window (GTK_WINDOW (login));
   after calling if doing during runtime
  */
void
gdm_set_theme (const char *theme_name)
{
	char *font_name;
	GtkSettings *settings = gtk_settings_get_default ();

	font_name = gdm_get_font (theme_name);

	gtk_settings_set_string_property (settings,
					  "gtk-theme-name", theme_name, "gdm");
	gtk_settings_set_string_property (settings,
					  "gtk-font-name", font_name, "gdm");
	g_free (font_name);
}

gboolean
gdm_working_command_exists (const char *commands)
{
	char *command = ve_get_first_working_command
		(commands, TRUE /* only_existance */);
	if (command == NULL)
		return FALSE;
	g_free (command);
	return TRUE;
}


/* EOF */
