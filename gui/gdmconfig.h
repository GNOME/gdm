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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
GtkWidget *get_widget(gchar *widget_name);

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

/* Some more macros for readable coding of the gnome_config_set_* functions */


#define gdm_entry_write(entry_name, key) \
        gnome_config_set_string(key, gtk_entry_get_text(GTK_ENTRY(get_widget(entry_name))));

#define gdm_spin_write(spin_button_name, key) \
        gnome_config_set_int(key, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(get_widget(spin_button_name))));

#define gdm_toggle_write(toggle_name, key) \
        gnome_config_set_bool(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(get_widget(toggle_name)))?TRUE:FALSE);
#define gdm_toggle_write_int(toggle_name, key) \
        gnome_config_set_int(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(get_widget(toggle_name)))?1:0);

#define gdm_icon_write(icon_name, key) \
        if (gnome_icon_entry_get_filename(GNOME_ICON_ENTRY(get_widget(icon_name)))) \
           gnome_config_set_string(key, gnome_icon_entry_get_filename(GNOME_ICON_ENTRY(get_widget(icon_name)))); \
        else \
           gnome_config_set_string(key, "");

#define gdm_font_write(picker_name, key) \
        gnome_config_set_string(key, gnome_font_picker_get_font_name(GNOME_FONT_PICKER(get_widget(picker_name))));


/* Function Prototypes */

void user_level_row_selected(GtkCList *clist, gint row,
							 gint column, GdkEvent *event, gpointer data);
void show_about_box(void);
void 
gdm_config_parse_most                  (void);
void 
gdm_config_parse_remaining             (void);

void
write_new_config_file                  (GtkButton *button,
                                        gpointer         user_data);
void revert_settings_to_file_state (GtkMenuItem *menu_item,
									gpointer user_data);
void
write_and_close                        (GtkButton *button,
										gpointer user_data);
void
open_help_page                         (GtkButton *button,
                                        gpointer         user_data);
gint
exit_configurator                      (void     *gnomedialog,
                                        gpointer         user_data);
void
can_apply_now                          (GtkEditable     *editable,
                                        gpointer         user_data);
void
change_xdmcp_sensitivity               (GtkButton       *button,
                                        gpointer         user_data);
void
set_face_sensitivity                   (GtkButton       *button,
                                        gpointer         user_data);
void handle_server_add_or_edit         (gchar           *string,
					gpointer         user_data);
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
