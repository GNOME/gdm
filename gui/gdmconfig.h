/* 
 *    GDMconfig, a graphical configurator for the GNOME display manager
 *    Copyright (C) 1999,2000,2001 Lee Mallabone <lee0@callnetuk.com>
 * 
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *   
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *   
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#include <gnome.h>
#include <glade/glade.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#include "../daemon/gdm.h"

/* Some macros to make setting the tens of GtkEntry and GtkSpinButtons much simpler. 
 * It also makes the code a lot more readable too.
 */
GtkWidget *get_widget(const gchar *widget_name);


typedef struct _GdmConfigSession GdmConfigSession;
struct _GdmConfigSession {
   char *name;
   char *script_contents;
   gboolean changable; /* if we had trouble reading it it's not */
   gboolean changed;
   gboolean renamed;
   char *old_name;
   gboolean is_default;
};

/* Set a GtkEntry to 'value'. entry_name is retrieved from the GladeXML 
 * pointed to by 'GUI'. 'value' should always have been allocated (usually 
 * with gnome_config_get_string), as it is freed here.
 */

#define gdm_entry_set(entry_name, value) \
        if (value) { \
           gtk_entry_set_text(GTK_ENTRY(get_widget(entry_name)), \
                              (char *)value); \
           g_free(value); \
        }
#define gdm_radio_set(radio_name, value, maximum) \
        if (value >= 0 && value <= maximum) { \
	   gchar *widget_name = g_strdup_printf ("%s_%d", radio_name, value); \
           gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(get_widget(widget_name)), TRUE); \
	   g_free (widget_name); \
	}

#define gdm_spin_set(spin_button_name, value) \
        if (value) \
           gtk_spin_button_set_value(GTK_SPIN_BUTTON(get_widget(spin_button_name)), (float)value);

#define gdm_toggle_set(toggle_name, value) \
        if (value) \
           gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(get_widget(toggle_name)), value);

#define gdm_icon_set(icon_name, value) \
        if (value) \
           gnome_icon_entry_set_icon(GNOME_ICON_ENTRY(get_widget(icon_name)), \
                              (char *)value);

#define gdm_font_set(font_name, value) \
        if (value) \
           gnome_font_picker_set_font_name(GNOME_FONT_PICKER(get_widget(font_name)), \
                                           (char *)value);
#define gdm_color_set(picker_name, value) \
        if (value) { \
	   GdkColor color; \
	   if (gdk_color_parse (value, &color)) \
		   gnome_color_picker_set_i16 (GNOME_COLOR_PICKER (get_widget (picker_name)), \
					       color.red, color.green, color.blue, 0); \
	}

/* Some more macros for readable coding of the gnome_config_set_* functions */


#define gdm_entry_write(entry_name, key) \
        gnome_config_set_string(key, gtk_entry_get_text(GTK_ENTRY(get_widget(entry_name))));

#define gdm_spin_write(spin_button_name, key) \
        gnome_config_set_int(key, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(get_widget(spin_button_name))));

#define gdm_toggle_write(toggle_name, key) \
        gnome_config_set_bool(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(get_widget(toggle_name)))?TRUE:FALSE);

/* This one is not used */
#define gdm_toggle_write_int(toggle_name, key) \
        gnome_config_set_int(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(get_widget(toggle_name)))?1:0);

#define gdm_icon_write(icon_name, key) \
	{ \
	char *icon = hack_icon_entry_get_icon(GNOME_ICON_ENTRY(get_widget(icon_name))); \
        if (icon != NULL) \
           gnome_config_set_string(key, icon); \
        else \
           gnome_config_set_string(key, ""); \
	g_free (icon); \
	}

#define gdm_font_write(picker_name, key) \
        gnome_config_set_string(key, gnome_font_picker_get_font_name(GNOME_FONT_PICKER(get_widget(picker_name))));

#define gdm_color_write(picker_name, key) \
	{ \
		char *value; \
		guint8 r, g, b; \
		gnome_color_picker_get_i8 (GNOME_COLOR_PICKER (get_widget (picker_name)), \
					   &r, &g, &b, NULL); \
		value = g_strdup_printf ("#%02x%02x%02x", (int)r, (int)g, (int)b); \
		gnome_config_set_string (key, value); \
		g_free (value); \
	}



/* Function Prototypes */

/* This function examines a set of commonly named radio buttons and writes out an integer
 * with gnome_config under 'key'. It looks for max_value number of radio buttons, and writes
 * the integer with the value of the earliest named radio button it finds.
 */
void gdm_radio_write (gchar *radio_base_name, 
		      gchar *key, 
		      int max_value);

void user_level_row_selected(GtkCList *clist, gint row,
			     gint column, GdkEvent *event, gpointer data);
void show_about_box(void);
void gdm_config_parse_most                  (gboolean factory);
void gdm_config_parse_remaining             (gboolean factory);

void
write_new_config_file                  (GtkButton *button,
                                        gpointer         user_data);
void revert_settings_to_file_state (GtkMenuItem *menu_item,
				    gpointer user_data);
void revert_to_factory_settings (GtkMenuItem *menu_item,
				 gpointer user_data);
void
write_and_close                        (GtkButton *button,
					gpointer user_data);
void
open_help_page                         (GtkButton *button,
                                        gpointer         user_data);

gint exit_configurator (GtkWidget *gnomedialog, gpointer user_data);

void
can_apply_now                          (GtkEditable     *editable,
                                        gpointer         user_data);
void
change_xdmcp_sensitivity               (GtkButton       *button,
                                        gpointer         user_data);
void
change_background_sensitivity_image    (GtkButton       *button,
                                        gpointer         user_data);
void
change_background_sensitivity_none     (GtkButton       *button,
                                        gpointer         user_data);
void
set_face_sensitivity                   (GtkButton       *button,
                                        gpointer         user_data);
void add_new_server_def (GtkButton *button, gpointer user_data);
void edit_selected_server_def (GtkButton *button, gpointer user_data);
void delete_selected_server_def (GtkButton *button, gpointer user_data);
void make_server_def_default (GtkButton *button, gpointer user_data);
void record_selected_server_def (GtkCList *clist,
				 gint row,
				 gint column,
				 GdkEventButton *event,
				 gpointer user_data);

void
add_new_server                         (GtkButton       *button,
                                        gpointer         user_data);
void
edit_selected_server                   (GtkButton       *button,
                                        gpointer         user_data);
void
delete_selected_server                 (GtkButton       *button,
                                        gpointer         user_data);
void
record_selected_server                  (GtkCList *clist,
			        	 gint row,
					 gint column,
					 GdkEventButton *event,
					 gpointer user_data);
void
move_server_up                         (GtkButton       *button,
                                        gpointer         user_data);
void
move_server_down                       (GtkButton       *button,
                                        gpointer         user_data);
void
session_text_edited (GtkEditable *text, gpointer data);
void
modify_session_name (GtkEntry *entry, gpointer data);


void
sessions_clist_row_selected                  (GtkCList *clist,
					      gint row,
					      gint column,
					      GdkEventButton *event,
					      gpointer user_data);
void
set_new_default_session (GtkButton *button,
			 gpointer user_data);
void
add_session_real (gchar *new_session_name, gpointer data);
void
add_session (GtkButton *button,
	     gpointer user_data);
void
remove_session (GtkButton *button,
		gpointer user_data);
void
session_text_edited (GtkEditable *text, gpointer data);
void
modify_session_name (GtkEntry *entry, gpointer data);
void 
session_directory_modified (GtkEntry *entry, gpointer data);

void change_automatic_sensitivity (GtkButton *button, gpointer user_data);
void change_timed_sensitivity (GtkButton *button, gpointer user_data);

