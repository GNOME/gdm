/* 
 *    GDMconfig, a graphical configurator for the GNOME display manager
 *    Copyright (C) 1999, Lee Mallabone <lee0@callnetuk.com>
 * 
 *    Inspired in places by the original gdmconfig.c, by Martin K. Petersen.
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

#include "gdmconfig.h"

GladeXML *GUI = NULL;

int number_of_servers = 0;
int selected_server_row = -1;

GtkWidget *GDMconfigurator = NULL;


int
main (int argc, char *argv[])
{
    gchar *glade_filename;
#ifdef ENABLE_NLS
    bindtextdomain (PACKAGE, PACKAGE_LOCALE_DIR);
    textdomain (PACKAGE);
#endif

    gnome_init ("gdmconfig", "0.1", argc, argv);
    glade_gnome_init();

    /* Make sure the user is root. If not, they shouldn't be messing with 
     * GDM's configuration.
     */
    if (geteuid() != 0)
      {
	  GtkWidget *fatal_error = 
	  gnome_error_dialog(_("You must be the superuser (root) to configure GDM.\n"));
	  gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
	  exit(EXIT_FAILURE);
      }

    /* Look for the glade file in $(datadir)/gdmconfig or, failing that,
     * look in the current directory.
     */
    glade_filename = gnome_datadir_file("gdmconfig/gdmconfig.glade");
    if (!glade_filename)
      {	  
	  glade_filename = g_strdup("gdmconfig.glade");
      }

    /* Build the user interface */
    GUI = glade_xml_new(glade_filename, NULL);
    g_free(glade_filename);
    g_assert(GUI != NULL);

    /* Sanity checking */
    GDMconfigurator = get_widget("gdmconfigurator");
    g_assert(GDMconfigurator != NULL);
    
    /* We set most of the user interface NOT wanting signals to get triggered as
     * we do it. Then we hook up the signals, and THEN set a few remaining elements.
     * This ensures sensitivity of some widgets is correct, and that the font picker
     * gets set properly.
     */
    gdm_config_parse_most();
    glade_xml_signal_autoconnect(GUI);
    gdm_config_parse_remaining();

    gtk_window_set_title(GTK_WINDOW(GDMconfigurator),
			 _("GNOME Display Manager Configurator"));
    gnome_property_box_set_state(GNOME_PROPERTY_BOX(GDMconfigurator), FALSE);
    
    gtk_main ();
    return 0;
}


void
gdm_config_parse_most (void)
{
    void *iter; gchar *key, *value;

    /* If the GDM config file does not exist, we have sensible defaults,
     * but make sure the user is warned.
     */
    if (!g_file_exists(GDM_CONFIG_FILE))
      {
	  GtkWidget *error_dialog =
	  gnome_error_dialog(_("The configuration file: " GDM_CONFIG_FILE "\ndoes not exist! Using default values."));
	  gnome_dialog_set_parent(GNOME_DIALOG(error_dialog), 
				  (GtkWindow *) GDMconfigurator);
	  gnome_dialog_run_and_close(GNOME_DIALOG(error_dialog));
      }
    
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
    
    /* Fill the widgets in GDM tab */
    gdm_entry_set("chooser_binary", gnome_config_get_string (GDM_KEY_CHOOSER));
    gdm_entry_set("greeter_binary", gnome_config_get_string (GDM_KEY_GREETER));
    gdm_entry_set("halt_command", gnome_config_get_string (GDM_KEY_HALT));
    gdm_entry_set("reboot_command", gnome_config_get_string (GDM_KEY_REBOOT));
    
    gdm_entry_set("init_dir", gnome_config_get_string (GDM_KEY_INITDIR));
    gdm_entry_set("log_dir", gnome_config_get_string (GDM_KEY_LOGDIR));
    gdm_entry_set("session_dir", gnome_config_get_string (GDM_KEY_SESSDIR));
    gdm_entry_set("pre_session_dir", gnome_config_get_string (GDM_KEY_PRESESS));
    gdm_entry_set("post_session_dir", gnome_config_get_string (GDM_KEY_POSTSESS));

    gdm_entry_set("pid_file", gnome_config_get_string (GDM_KEY_PIDFILE));
    gdm_entry_set("default_path", gnome_config_get_string (GDM_KEY_PATH));
    gdm_entry_set("root_path", gnome_config_get_string (GDM_KEY_ROOTPATH));
 

    /* Fill the widgets in Security tab */
    gdm_toggle_set("allow_root", gnome_config_get_bool(GDM_KEY_ALLOWROOT));
    gdm_toggle_set("kill_init_clients", gnome_config_get_bool(GDM_KEY_KILLIC));
    /* FIXME: see comment on _set */
    gdm_toggle_set("relax_perms", gnome_config_get_int(GDM_KEY_RELAXPERM));
    gdm_toggle_set("verbose_auth", gnome_config_get_bool(GDM_KEY_VERBAUTH));

    gdm_entry_set("gdm_runs_as_user", gnome_config_get_string (GDM_KEY_USER));
    gdm_entry_set("gdm_runs_as_group", gnome_config_get_string (GDM_KEY_GROUP));
    gdm_entry_set("user_auth_dir", gnome_config_get_string (GDM_KEY_UAUTHDIR));
    gdm_entry_set("user_auth_fb_dir", gnome_config_get_string (GDM_KEY_UAUTHFB));
    gdm_entry_set("user_auth_file", gnome_config_get_string (GDM_KEY_UAUTHFILE));

    gdm_spin_set("retry_delay", gnome_config_get_int(GDM_KEY_RETRYDELAY));
    gdm_spin_set("max_user_length", gnome_config_get_int(GDM_KEY_MAXFILE));
    

    /* Fill the widgets in the XDMCP tab */
    /* enable toggle is in parse_remaining() */
    gdm_toggle_set("honour_indirect", gnome_config_get_bool(GDM_KEY_INDIRECT));
    gdm_spin_set("net_port", gnome_config_get_int(GDM_KEY_UDPPORT));
    gdm_spin_set("net_requests", gnome_config_get_int(GDM_KEY_MAXPEND));
    gdm_spin_set("net_indirect_requests", gnome_config_get_int(GDM_KEY_MAXINDIR));
    gdm_spin_set("max_sessions", gnome_config_get_int(GDM_KEY_MAXSESS));
    gdm_spin_set("max_wait_time", gnome_config_get_int(GDM_KEY_MAXWAIT));
    gdm_spin_set("max_indirect_wait_time", gnome_config_get_int(GDM_KEY_MAXINDWAIT));
    
    
    /* Fill the widgets in User Interface tab */
    gdm_entry_set("gtkrc_file", gnome_config_get_string (GDM_KEY_GTKRC));
    gdm_entry_set("logo_file", gnome_config_get_string (GDM_KEY_LOGO));
    gdm_icon_set("gdm_icon", gnome_config_get_string (GDM_KEY_ICON));
    
    gdm_toggle_set("show_system", gnome_config_get_bool (GDM_KEY_SYSMENU));
    gdm_toggle_set("quiver", gnome_config_get_bool (GDM_KEY_QUIVER));
    
    gdm_entry_set("exclude_users", gnome_config_get_string (GDM_KEY_EXCLUDE));
    /* font picker is in parse_remaining() */
    gdm_entry_set("welcome_message", gnome_config_get_string (GDM_KEY_WELCOME));
    

    /* Fill the widgets in Greeter tab */
    /* enable_face_browser is in parse_remaining() */
    gdm_entry_set("default_face_file", gnome_config_get_string (GDM_KEY_FACE));
    gdm_entry_set("global_faces_dir", gnome_config_get_string (GDM_KEY_FACEDIR));
    gdm_spin_set("max_face_width", gnome_config_get_int (GDM_KEY_ICONWIDTH));
    gdm_spin_set("max_face_height", gnome_config_get_int (GDM_KEY_ICONHEIGHT));
  
    gdm_entry_set("locale_file", gnome_config_get_string(GDM_KEY_LOCFILE));
    gdm_entry_set("default_locale", gnome_config_get_string(GDM_KEY_LOCALE));


    /* Fill the widgets in Chooser tab */
    gdm_entry_set("host_images_dir", gnome_config_get_string (GDM_KEY_HOSTDIR));
    gdm_entry_set("default_host_image_file", gnome_config_get_string (GDM_KEY_HOST));
    gdm_spin_set("refresh_interval", gnome_config_get_int(GDM_KEY_SCAN));

    gdm_toggle_set("enable_debug", gnome_config_get_bool(GDM_KEY_DEBUG));

    gnome_config_pop_prefix();
    
    /* Fill the widgets in Servers tab */
    iter=gnome_config_init_iterator("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter=gnome_config_iterator_next (iter, &key, &value);
    
    while (iter) {
	char *a_server[2];
	
	if(isdigit(*key)) 
	  {
	      a_server[0] = key;
	      a_server[1] = value;
	      gtk_clist_prepend(GTK_CLIST(get_widget("server_clist")), a_server);
	      number_of_servers++;
	  }
	else
	  {
	      gnome_warning_dialog(_("gdm_config_parse_most: Invalid server line in config file. Ignoring!"));
	  }
	iter=gnome_config_iterator_next (iter, &key, &value);
    }
    /* FIXME: Could do something nicer with the 'exclude users' GUI sometime */
}


void
gdm_config_parse_remaining (void)
{
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    /* Ensure the XDMCP frame is the correct sensitivity */
    gdm_toggle_set("enable_xdmcp", gnome_config_get_bool(GDM_KEY_XDMCP));
    
    /* This should make the font picker to update itself. But for some
     * strange reason, it doesn't.
     */
    gdm_font_set("font_picker", gnome_config_get_string (GDM_KEY_FONT));

    /* Face browser stuff */
    gdm_toggle_set("enable_face_browser", gnome_config_get_bool (GDM_KEY_BROWSER));
    
    gnome_config_pop_prefix();
}

static void
run_warn_dialog (void)
{
	GtkWidget *w;

	w = gnome_ok_dialog_parented
		(_("The greeter settings will take effect the next time\n"
		   "it is displayed.  The rest of the settings will not\n"
		   "take effect until gdm is restarted or the computer is\n"
		   "rebooted"),
		 GTK_WINDOW (GDMconfigurator));
	gnome_dialog_run_and_close (GNOME_DIALOG (w));
}


void
write_new_config_file                  (GnomePropertyBox *gnomepropertybox,
                                        gint             arg1,
                                        gpointer         user_data)
{
    int i = -1;
    
    /* Apply everything only once per click of apply */
    
    if (arg1 != -1)
      return;

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
    
    /* Write out the widget contents of the GDM tab */
    gdm_entry_write("automatic_login", GDM_KEY_AUTOMATICLOGIN);
    gdm_entry_write("chooser_binary", GDM_KEY_CHOOSER);
    gdm_entry_write("greeter_binary", GDM_KEY_GREETER);
    gdm_entry_write("halt_command", GDM_KEY_HALT);
    gdm_entry_write("reboot_command", GDM_KEY_REBOOT);
    
    gdm_entry_write("init_dir", GDM_KEY_INITDIR);
    gdm_entry_write("log_dir", GDM_KEY_LOGDIR);
    gdm_entry_write("session_dir", GDM_KEY_SESSDIR);
    gdm_entry_write("pre_session_dir", GDM_KEY_PRESESS);
    gdm_entry_write("post_session_dir", GDM_KEY_POSTSESS);

    gdm_entry_write("pid_file", GDM_KEY_PIDFILE);
    gdm_entry_write("default_path", GDM_KEY_PATH);
    gdm_entry_write("root_path", GDM_KEY_ROOTPATH);
 

    /* Write out the widget contents of the Security tab */
    gdm_toggle_write("allow_root", GDM_KEY_ALLOWROOT);
    gdm_toggle_write("kill_init_clients", GDM_KEY_KILLIC);
    /* FIXME: This should allow 0, 1, 2 levels, it should really be an enum! */
    gdm_toggle_write_int("relax_perms", GDM_KEY_RELAXPERM);
    gdm_toggle_write("verbose_auth", GDM_KEY_VERBAUTH);

    gdm_entry_write("gdm_runs_as_user", GDM_KEY_USER);
    gdm_entry_write("gdm_runs_as_group", GDM_KEY_GROUP);
    gdm_entry_write("user_auth_dir", GDM_KEY_UAUTHDIR);
    gdm_entry_write("user_auth_fb_dir", GDM_KEY_UAUTHFB);
    gdm_entry_write("user_auth_file", GDM_KEY_UAUTHFILE);

    gdm_spin_write("retry_delay", GDM_KEY_RETRYDELAY);
    gdm_spin_write("max_user_length", GDM_KEY_MAXFILE);
    

    /* Write out the widget contents of the XDMCP tab */
    gdm_toggle_write("enable_xdmcp", GDM_KEY_XDMCP);
    gdm_toggle_write("honour_indirect", GDM_KEY_INDIRECT);
    gdm_spin_write("net_port", GDM_KEY_UDPPORT);
    gdm_spin_write("net_requests", GDM_KEY_MAXPEND);
    gdm_spin_write("net_indirect_requests", GDM_KEY_MAXINDIR);
    gdm_spin_write("max_sessions", GDM_KEY_MAXSESS);
    gdm_spin_write("max_wait_time", GDM_KEY_MAXWAIT);
    gdm_spin_write("max_indirect_wait_time", GDM_KEY_MAXINDWAIT);
    
    
    /* Write out the widget contents of the User Interface tab */
    gdm_entry_write("gtkrc_file", GDM_KEY_GTKRC);
    gdm_entry_write("logo_file", GDM_KEY_LOGO);
    gdm_icon_write("gdm_icon", GDM_KEY_ICON);
    
    gdm_toggle_write("show_system", GDM_KEY_SYSMENU);
    gdm_toggle_write("quiver", GDM_KEY_QUIVER);
    gdm_entry_write("background_program", GDM_KEY_BACKGROUNDPROG);
    gdm_toggle_write("lock_position", GDM_KEY_LOCK_POSITION);
    gdm_toggle_write("set_position", GDM_KEY_SET_POSITION);
    gdm_spin_write("position_x", GDM_KEY_POSITIONX);
    gdm_spin_write("position_y", GDM_KEY_POSITIONY);
    
    gdm_entry_write("exclude_users", GDM_KEY_EXCLUDE);
    gdm_font_write("font_picker", GDM_KEY_FONT);
    gdm_entry_write("welcome_message", GDM_KEY_WELCOME);
    

    /* Write out the widget contents of the Greeter tab */
    gdm_toggle_write("enable_face_browser", GDM_KEY_BROWSER);
    gdm_entry_write("default_face_file", GDM_KEY_FACE);
    gdm_entry_write("global_faces_dir", GDM_KEY_FACEDIR);
    gdm_spin_write("max_face_width", GDM_KEY_ICONWIDTH);
    gdm_spin_write("max_face_height", GDM_KEY_ICONHEIGHT);
  
    gdm_entry_write("locale_file", GDM_KEY_LOCFILE);
    gdm_entry_write("default_locale", GDM_KEY_LOCALE);


    /* Write out the widget contents of the Chooser tab */
    gdm_entry_write("host_images_dir", GDM_KEY_HOSTDIR);
    gdm_entry_write("default_host_image_file", GDM_KEY_HOST);
    gdm_spin_write("refresh_interval", GDM_KEY_SCAN);

    gdm_toggle_write("enable_debug", GDM_KEY_DEBUG);

    gnome_config_pop_prefix();
    
    gnome_config_clean_section("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    gnome_config_push_prefix("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    
    /* Write out the widget contents of the Servers tab */

    for (i=0; i<number_of_servers; i++)
      {
	  char *current_server;
	  gtk_clist_get_text(GTK_CLIST(get_widget("server_clist")),
			     i, 1, &current_server);
	  gnome_config_set_string(g_strdup_printf("%d", i), current_server);
      }

    gnome_config_pop_prefix();
    gnome_config_sync();

    run_warn_dialog ();
}


void
open_help_page                         (GnomePropertyBox *gnomepropertybox,
                                        gint             arg1,
                                        gpointer         user_data)
{

}


gint
exit_configurator                      (GnomeDialog     *gnomedialog,
                                        gpointer         user_data)
{
    gtk_main_quit();
    return 0;
}


void
can_apply_now                          (GtkEditable     *editable,
                                        gpointer         user_data)
{
    gnome_property_box_changed(GNOME_PROPERTY_BOX(glade_xml_get_widget(GUI, "gdmconfigurator")));
}


void
change_xdmcp_sensitivity               (GtkButton       *button,
                                        gpointer         user_data)
{
    g_assert(button != NULL);
    g_assert(GTK_IS_TOGGLE_BUTTON(button));
    
    gtk_widget_set_sensitive(glade_xml_get_widget(GUI, "xdmcp_frame"), 
			     GTK_TOGGLE_BUTTON(button)->active);
}


void
set_face_sensitivity                   (GtkButton       *button,
                                        gpointer         user_data)
{
    gtk_widget_set_sensitive(get_widget("faces_table"),
			     GTK_TOGGLE_BUTTON(get_widget("enable_face_browser"))->active);
}


void handle_server_add_or_edit         (gchar           *string,
					gpointer         user_data)
{
    if(!string)
      return;

    /* Add a new server to the end of the server CList */
    if (strcmp("add", (char *)user_data) == 0)
      {
	  gchar *new_server[2];

	  new_server[0] = g_strdup_printf("%d", number_of_servers);;
	  new_server[1] = string;
	  /* We now have an extra server */
	  number_of_servers++;
	  gtk_clist_append(GTK_CLIST(get_widget("server_clist")), new_server);
      }
    else
      {
	  /* Update the current server */
	  gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
			     selected_server_row, 1,
			     string);
      }
}


void
add_new_server                         (GtkButton       *button,
                                        gpointer         user_data)
{
    /* Request the command line for this new server */
    gnome_request_dialog(FALSE, "Enter the path to the X server,and\nany parameters that should be passed to it.",
			 "/usr/bin/X11/X",
			 -1, handle_server_add_or_edit,
			 (gpointer)"add", (GtkWindow *)GDMconfigurator);
}


void
edit_selected_server                   (GtkButton       *button,
                                        gpointer         user_data)
{
    char *current_server;
    
    if (selected_server_row < 0)
      return;

    /* Ask for the new name of this server */
    gtk_clist_get_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 1, &current_server);
    gnome_request_dialog(FALSE, _("Enter the path to the X server,and\nany parameters that should be passed to it."),
			 current_server,
			 -1, handle_server_add_or_edit,
			 (gpointer)"edit", (GtkWindow *)GDMconfigurator);

}


void
delete_selected_server                 (GtkButton       *button,
                                        gpointer         user_data)
{
    int tmp = selected_server_row;
    
    if (selected_server_row < 0)
      return;

    /* Remove the server from the list */
    gtk_clist_remove(GTK_CLIST(get_widget("server_clist")), 
		     selected_server_row);
    number_of_servers--;
    
    /* The server numbers will be out of sync now, so correct that */ 
    while (tmp < number_of_servers)
      {
	  gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")), tmp, 0,
			     g_strdup_printf("%d", tmp));
	  tmp++;
      }
    selected_server_row = -1;
}


void
record_selected_server                  (GtkCList *clist,
			        	 gint row,
					 gint column,
					 GdkEventButton *event,
					 gpointer user_data)
{
    selected_server_row = row;
}


void
move_server_up                         (GtkButton       *button,
                                        gpointer         user_data)
{
    if (selected_server_row < 1)
      return;
    
    /* Move a server up the CList, decreasing its number by one. 
     * However, make sure the other server numbers are all still correct too.
     */
    gtk_clist_swap_rows(GTK_CLIST(get_widget("server_clist")),
			selected_server_row,
			selected_server_row - 1);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));
    
    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 --selected_server_row, 0);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));

}


void
move_server_down                       (GtkButton       *button,
                                        gpointer         user_data)
{
    if (selected_server_row < 0)
      return;
    
    if (selected_server_row == (number_of_servers - 1))
      return;

    /* Move a server down the CList, increasing its number by one. 
     * However, make sure the other server numbers are all still correct too.
     */
    gtk_clist_swap_rows(GTK_CLIST(get_widget("server_clist")),
			selected_server_row,
			selected_server_row + 1);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));

    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 ++selected_server_row, 0);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));
}
