/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDMSetup
 * Copyright (C) 2002, George Lebl
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

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <popt.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "misc.h"
#include "gdmcomm.h"
#include "gdmuser.h"
#include "gdmsession.h"
#include "gdmconfig.h"

#include "gdm-common.h"
#include "gdm-daemon-config-keys.h"

static char     *GdmSoundProgram = NULL;
static gchar    *GdmExclude      = NULL;
static gchar    *GdmInclude      = NULL;
static gint      GdmIconMaxHeight;
static gint      GdmIconMaxWidth;
static gboolean  GdmIncludeAll;
static gboolean  GdmUserChangesUnsaved;
static gboolean  GdmRandomFromSelectedChangesWarn;
static gint      last_selected_command;

/* set the DOING_GDM_DEVELOPMENT env variable if you want to
 * search for the glade file in the current dir and not the system
 * install dir, better then something you have to change
 * in the source and recompile
 */

static gboolean  DOING_GDM_DEVELOPMENT = FALSE;
static gboolean  RUNNING_UNDER_GDM     = FALSE;
static gboolean  gdm_running           = FALSE;
static GladeXML  *xml;
static GladeXML  *xml_add_users;
static GladeXML  *xml_add_xservers;
static GladeXML  *xml_xdmcp;
static GladeXML  *xml_xservers;
static GladeXML  *xml_commands;
static GtkWidget *setup_notebook;
static GList     *timeout_widgets = NULL;
static gchar     *last_theme_installed = NULL;
static char      *selected_themes = NULL;
static char      *selected_theme  = NULL;
static gchar     *config_file;
static gchar     *custom_config_file;
static GSList    *xservers;

/* This is used to store changes made to all
   possible fields of custom/normal commands */
static GHashTable  *GdmCommandChangesUnsaved = NULL;

/* Used to store all available sessions */
static GList *sessions = NULL;

enum {
	XSERVER_COLUMN_VT,
	XSERVER_COLUMN_SERVER,
	XSERVER_COLUMN_OPTIONS,
	XSERVER_NUM_COLUMNS
};

enum {
	THEME_COLUMN_SELECTED,
	THEME_COLUMN_SELECTED_LIST,
	THEME_COLUMN_DIR,
	THEME_COLUMN_FILE,
	THEME_COLUMN_SCREENSHOT,
	THEME_COLUMN_MARKUP,
	THEME_COLUMN_NAME,
	THEME_COLUMN_DESCRIPTION,
	THEME_COLUMN_AUTHOR,
	THEME_COLUMN_COPYRIGHT,
	THEME_NUM_COLUMNS
};

enum {
	USERLIST_NAME,
	USERLIST_NUM_COLUMNS
};

enum {
	ONE_THEME,
	RANDOM_THEME
};

enum {
	GENERAL_TAB,
	LOCAL_TAB,
	REMOTE_TAB,
	ACCESSIBILITY_TAB,
	SECURITY_TAB,
	USERS_TAB
};

enum {
	CLOCK_AUTO,
	CLOCK_YES,
	CLOCK_NO
};

enum {
	HALT_CMD,
	REBOOT_CMD,
	SUSPEND_CMD,
	CUSTOM_CMD
};

enum {
	LOCAL_PLAIN,
	LOCAL_PLAIN_WITH_FACE,
	LOCAL_THEMED
};

enum {
	REMOTE_DISABLED,
	REMOTE_SAME_AS_LOCAL,
	REMOTE_PLAIN = 2,
	REMOTE_THEMED = 2,
	REMOTE_PLAIN_WITH_FACE = 3
};

enum {
	XSERVER_LAUNCH_GREETER,
	XSERVER_LAUNCH_CHOOSER
};

enum {
	BACKGROUND_NONE,
	BACKGROUND_IMAGE_AND_COLOR,
	BACKGROUND_COLOR,
	BACKGROUND_IMAGE
};


static GtkTargetEntry target_table[] = {
	{ "text/uri-list", 0, 0 }
};

static guint n_targets = sizeof (target_table) / sizeof (target_table[0]);

static void
simple_spawn_sync (char **argv)
{
	g_spawn_sync (NULL /* working_directory */,
	              argv,
	              NULL /* envp */,
	              G_SPAWN_SEARCH_PATH |
	              G_SPAWN_STDOUT_TO_DEV_NULL |
	              G_SPAWN_STDERR_TO_DEV_NULL,
	              NULL /* child_setup */,
	              NULL /* user_data */,
	              NULL /* stdout */,
	              NULL /* stderr */,
	              NULL /* exit status */,
	              NULL /* error */);
}

static void
setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor;

	cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

static void
setup_window_cursor (GdkCursorType type)
{
	GdkCursor *cursor;
	GtkWidget *setup_dialog;

	cursor = gdk_cursor_new (type);
	setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
	if (setup_dialog->window)
		gdk_window_set_cursor (setup_dialog->window, cursor);
	gdk_cursor_unref (cursor);
}

static void
unsetup_window_cursor (void)
{
	GtkWidget *setup_dialog;

	setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
	if (setup_dialog->window)
		gdk_window_set_cursor (setup_dialog->window, NULL);
}

static GtkWidget *
hig_dialog_new (GtkWindow      *parent,
		GtkDialogFlags flags,
		GtkMessageType type,
		GtkButtonsType buttons,
		const gchar    *primary_message,
		const gchar    *secondary_message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
		                         GTK_DIALOG_DESTROY_WITH_PARENT,
		                         type,
		                         buttons,
		                         "%s", primary_message);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                  "%s", secondary_message);

	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 14);

  	return dialog;
}

static void
update_greeters (void)
{
	char *p, *ret;
	long pid;
	static gboolean shown_error = FALSE;
	gboolean have_error = FALSE;

	/* recheck for gdm */
	gdm_running = gdmcomm_check (FALSE);

	if ( ! gdm_running)
		return;

	ret = gdmcomm_call_gdm (GDM_SUP_GREETERPIDS,
				NULL /* auth_cookie */,
				"2.3.90.2",
				5);
	if (ret == NULL)
		return;
	p = strchr (ret, ' ');
	if (p == NULL) {
		g_free (ret);
		return;
	}
	p++;

	for (;;) {
		if (sscanf (p, "%ld", &pid) != 1) {
			g_free (ret);
			goto check_update_error;
		}

		/* sanity */
		if (pid <= 0)
			continue;

		if (kill (pid, SIGHUP) != 0)
			have_error = TRUE;
		p = strchr (p, ';');
		if (p == NULL) {
			g_free (ret);
			goto check_update_error;
		}
		p++;
	}

check_update_error:
	if ( ! shown_error && have_error) {
		GtkWidget *setup_dialog;
		GtkWidget *dlg;
		setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
		dlg = hig_dialog_new (GTK_WINDOW (setup_dialog),
				      GTK_DIALOG_MODAL |
				      GTK_DIALOG_DESTROY_WITH_PARENT,
				      GTK_MESSAGE_ERROR,
				      GTK_BUTTONS_OK,
				      _("An error occurred while "
					"trying to contact the "
					"login screens.  Not all "
					"updates may have taken "
					"effect."),
				      "");
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		shown_error = TRUE;
	}
}

static gboolean
the_timeout (gpointer data)
{
	GtkWidget *widget = data;
	gboolean (*func) (GtkWidget *);

	func = g_object_get_data (G_OBJECT (widget), "timeout_func");

	if ( ! (*func) (widget)) {
		g_object_set_data (G_OBJECT (widget), "change_timeout", NULL);
		g_object_set_data (G_OBJECT (widget), "timeout_func", NULL);
		timeout_widgets = g_list_remove (timeout_widgets, widget);
		return FALSE;
	} else {
		return TRUE;
	}
}

static void
run_timeout (GtkWidget *widget, guint tm, gboolean (*func) (GtkWidget *))
{
	guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget),
							"change_timeout"));
	if (id != 0) {
		g_source_remove (id);
	} else {
		timeout_widgets = g_list_prepend (timeout_widgets, widget);
	}

	id = g_timeout_add (tm, the_timeout, widget);
	g_object_set_data (G_OBJECT (widget), "timeout_func", func);

	g_object_set_data (G_OBJECT (widget), "change_timeout",
			   GUINT_TO_POINTER (id));
}

static void
update_key (const char *key)
{
	if (key == NULL)
	       return;

	/* recheck for gdm */
	gdm_running = gdmcomm_check (FALSE);

	if (gdm_running) {
		char *ret;
		char *s = g_strdup_printf ("%s %s", GDM_SUP_UPDATE_CONFIG,
					   key);
		ret = gdmcomm_call_gdm (s,
					NULL /* auth_cookie */,
					"2.3.90.2",
					5);
		g_free (s);
		g_free (ret);
	}
}

static void
gdm_setup_config_set_bool (const char *key, gboolean val)
{
	GKeyFile *cfg;
	GKeyFile *custom_cfg;
        gboolean defaultval;

        cfg = gdm_common_config_load (config_file, NULL);
	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
	gdm_common_config_get_boolean (cfg, key, &defaultval, NULL);

        if (val == defaultval) {
		gdm_common_config_remove_key (custom_cfg, key, NULL);
	} else {
		gdm_common_config_set_boolean (custom_cfg, key, val);
	}

	gdm_common_config_save (custom_cfg, custom_config_file, NULL);

	g_key_file_free (cfg);
	g_key_file_free (custom_cfg);

	update_key (key);
}

static void
gdm_setup_config_set_int (const char *key, int val)
{
	GKeyFile *cfg;
	GKeyFile *custom_cfg;
        int       defaultval;

        cfg = gdm_common_config_load (config_file, NULL);
	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
	gdm_common_config_get_int (cfg, key, &defaultval, NULL);

	if (val == defaultval) {
		gdm_common_config_remove_key (custom_cfg, key, NULL);
	} else {
		gdm_common_config_set_int (custom_cfg, key, val);
	}

	gdm_common_config_save (custom_cfg, custom_config_file, NULL);

	g_key_file_free (cfg);
	g_key_file_free (custom_cfg);

	update_key (key);
}

static void
gdm_setup_config_set_string (const char *key, gchar *val)
{
	GKeyFile *cfg;
	GKeyFile *custom_cfg;
        char     *defaultval;

        cfg = gdm_common_config_load (config_file, NULL);
	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
	gdm_common_config_get_string (cfg, key, &defaultval, NULL);

	if (defaultval != NULL &&
	    strcmp (ve_sure_string (val), ve_sure_string (defaultval)) == 0) {
		gdm_common_config_remove_key (custom_cfg, key, NULL);
	} else {
		gdm_common_config_set_string (custom_cfg, key, val);
	}

	if (defaultval)
		g_free (defaultval);

	gdm_common_config_save (custom_cfg, custom_config_file, NULL);

	g_key_file_free (cfg);
	g_key_file_free (custom_cfg);

	update_key (key);
}

static gboolean
radiogroup_timeout (GtkWidget *toggle)
{	
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	GSList *radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
		
	if (strcmp (ve_sure_string (key), GDM_KEY_RELAX_PERM) == 0) {
		GSList *tmp;
		gint val;
		gint selected = 0;
		gint i = 0;
		gint list_size;
		
		val = gdm_config_get_int ((gchar *)key);
		list_size = g_slist_length (radio_group) - 1;
		
		for (tmp = radio_group; tmp != NULL; tmp = tmp->next, i++) {
			GtkWidget *radio_button;
			radio_button = (GtkWidget *) tmp->data;
			if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_button)))
				selected = list_size - i;
		}
		
		if (val != selected)
			gdm_setup_config_set_int (key, selected);
			
	}
	return FALSE;	
}

static gboolean
bool_equal (gboolean a, gboolean b)
{
	if ((a && b) || (!a && !b))
		return TRUE;
	else
		return FALSE;
}

static gboolean
toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	gboolean    val = gdm_config_get_bool ((gchar *)key);

	if (strcmp (ve_sure_string (key), GDM_KEY_ENTRY_INVISIBLE) == 0) {
		/* This is a lil bit back to front
		   true is false and false is true in this case */
		if ( bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
			gdm_setup_config_set_bool (key, !GTK_TOGGLE_BUTTON (toggle)->active);
		}
	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_GLOBAL_FACE_DIR) == 0) {
		/* Once enabled write the curently selected item
		   in the filechooser widget, otherwise disable
		   the config entry, i.e. write an empty string */
		if (GTK_TOGGLE_BUTTON (toggle)->active == TRUE) {
			gchar *filename;
			GtkWidget *file_chooser;

			file_chooser = glade_xml_get_widget (xml, "global_face_dir_filechooser");
			filename  = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
			if (strcmp (ve_sure_string (gdm_config_get_string ((char*)key)),
				    ve_sure_string (filename)) != 0)
				gdm_setup_config_set_string (key, ve_sure_string (filename));

			g_free (filename);
		}
		else
			gdm_setup_config_set_string (key, "");		    
	}	
	else if (strcmp (ve_sure_string (key), GDM_KEY_DEFAULT_FACE) == 0) {
		/* Once enabled write the curently selected item
		   in the filechooser widget, otherwise disable
		   the config entry, i.e. write an empty string */
		if (GTK_TOGGLE_BUTTON (toggle)->active == TRUE) {
			gchar *filename;
			GtkWidget *file_chooser;
			
			file_chooser = glade_xml_get_widget (xml, "default_face_filechooser");

			filename  = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
			
			if (strcmp (ve_sure_string (gdm_config_get_string ((char*)key)), 
				    ve_sure_string (filename)) != 0)				
				gdm_setup_config_set_string (key, ve_sure_string (filename));
			
			g_free (filename);
		}
		else
			gdm_setup_config_set_string (key, "");		    
	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_GTKRC) == 0) {
		/* Once enabled write the curently selected item
		   in the filechooser widget, otherwise disable
		   the config entry, i.e. write an empty string */
		if (GTK_TOGGLE_BUTTON (toggle)->active == TRUE) {
			gchar *filename;
			GtkWidget *file_chooser;
			
			file_chooser = glade_xml_get_widget (xml, "gtkrc_chooserbutton");

			filename  = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
			
			if (strcmp (ve_sure_string (gdm_config_get_string ((char*)key)), 
				    ve_sure_string (filename)) != 0)				
				gdm_setup_config_set_string (key, ve_sure_string (filename));
			
			g_free (filename);
		}
		else
			gdm_setup_config_set_string (key, "");		    
	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_DEFAULT_SESSION) == 0) {
		/* Once enabled write the curently selected item
		   in the combobox widget, otherwise disable
		   the config entry, i.e. write an empty string */		

		if (GTK_TOGGLE_BUTTON (toggle)->active == TRUE) {
			gint selected;
			gchar *value;
			gchar *new_val = NULL;			
			GtkWidget *default_session_combobox;
			
			default_session_combobox = glade_xml_get_widget (xml, "default_session_combobox");

			selected = gtk_combo_box_get_active (GTK_COMBO_BOX (default_session_combobox));
			
			value = gdm_config_get_string ((gchar *)key);
									
			new_val = g_strdup ((gchar*) g_list_nth_data (sessions, selected));					
			
			if (strcmp (ve_sure_string (value), ve_sure_string (new_val)) != 0)				
				gdm_setup_config_set_string (key, ve_sure_string (new_val));
			
			g_free (value);
			g_free (new_val);
		}
		else
			gdm_setup_config_set_string (key, "");		    
	}	
	else {
		/* All other cases */
		if ( ! bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
			gdm_setup_config_set_bool (key, GTK_TOGGLE_BUTTON (toggle)->active);
		}
	}	

	return FALSE;
}

static gboolean
command_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	
	if (strcmp (ve_sure_string (key), GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE) == 0 ||
	    strcmp (ve_sure_string (key), GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE) == 0) {
		/* This only applies to custom commands
		   First find which command has been ticked on/off then put the new value 
		   together with the corresponding key into the command changed hash. 
		   Enable apply command changes button. If command is equal to the existing
		   config value remove it from the hash and disable the apply command changes 
		   button (if applicable) */
		
		gchar *key_string;
		gboolean old_val = FALSE;
		gboolean val;
		GtkWidget *apply_cmd_changes;
		GtkWidget *command_combobox;
		gint selected, i;
		
		val = GTK_TOGGLE_BUTTON (toggle)->active;
		apply_cmd_changes = glade_xml_get_widget (xml_commands, "command_apply_button");
		command_combobox = glade_xml_get_widget (xml_commands, "cmd_type_combobox");
		
		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (command_combobox));
		
		i = selected - CUSTOM_CMD;
		key_string = g_strdup_printf(_("%s%d="), ve_sure_string (key), i); 
		old_val = gdm_config_get_bool (key_string);
		
		if (val != old_val) {	
			gboolean *p_val = g_new0 (gboolean, 1);
			*p_val = val;
			g_hash_table_insert (GdmCommandChangesUnsaved, g_strdup (key_string), p_val);
		}
		else if (g_hash_table_lookup (GdmCommandChangesUnsaved, key_string) != NULL) {
			g_hash_table_remove (GdmCommandChangesUnsaved, key_string);
		}
		
		g_free (key_string);
		
		if (g_hash_table_size (GdmCommandChangesUnsaved) == 0)
			gtk_widget_set_sensitive (apply_cmd_changes, FALSE);
		else 
			gtk_widget_set_sensitive (apply_cmd_changes, TRUE);
	}		
	
	return FALSE;
}

static gboolean
logo_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	GtkWidget  *chooserbutton;
	gchar      *filename;

	chooserbutton = glade_xml_get_widget (xml, "local_logo_image_chooserbutton");
						
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooserbutton));
	
 	if ((GTK_TOGGLE_BUTTON (toggle)->active) == FALSE) {
		gdm_setup_config_set_string (GDM_KEY_CHOOSER_BUTTON_LOGO, filename);	
		gdm_setup_config_set_string (key, "");
	}
	else if (filename != NULL) {
		gdm_setup_config_set_string (GDM_KEY_CHOOSER_BUTTON_LOGO, filename);
		gdm_setup_config_set_string (key, filename);
	}
	update_greeters ();
	g_free (filename);
	return FALSE;
}

static void
logo_toggle_toggled (GtkWidget *toggle, gpointer data)
{
	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {

		GtkWidget *checkbutton;
		
		checkbutton = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");

		g_signal_handlers_disconnect_by_func (checkbutton,
		                                      (gpointer) logo_toggle_toggled,
		                                      NULL);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton),
		                              GTK_TOGGLE_BUTTON (toggle)->active);

		g_signal_connect (G_OBJECT (checkbutton), "toggled", 
		                  G_CALLBACK (logo_toggle_toggled), NULL);
	}
	else {
		GtkWidget *checkbutton;

		checkbutton = glade_xml_get_widget (xml, "local_logo_image_checkbutton");

		g_signal_handlers_disconnect_by_func (checkbutton,
		                                      (gpointer) logo_toggle_toggled,
		                                      NULL);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton), 
		                              GTK_TOGGLE_BUTTON (toggle)->active);	

		g_signal_connect (G_OBJECT (checkbutton), "toggled", 
		                  G_CALLBACK (logo_toggle_toggled), NULL);
	}
	run_timeout (toggle, 200, logo_toggle_timeout);
}

/* Forward declarations */
static void
setup_user_combobox_list (const char *name, const char *key);
static char *
strings_list_add (char *strings_list, const char *string, const char *sep);
static char *
strings_list_remove (char *strings_list, const char *string, const char *sep);

static gboolean
intspin_timeout (GtkWidget *spin)
{
	const char *key = g_object_get_data (G_OBJECT (spin), "key");
	int new_val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));
	int val;
	gboolean greeters_need_update = FALSE;

	val = gdm_config_get_int ((gchar *)key);

	if (strcmp (ve_sure_string (key), GDM_KEY_MINIMAL_UID) == 0){
		/* We have changed MinimalUID, so we need to go through
		   the list of existing users in the Include list and remove
		   the entries that do not match the criteria anymore. If there 
		   are any user is informed about the changes. Auto login and 
		   timed login comboboxes are adjusted and greeters restarted */
		char **list;
		char *removed = NULL;	
		int i;
		gchar *autologon_user;
		gchar *timedlogon_user;
		
		
		list = g_strsplit (GdmInclude, ",", 0);
		for (i=0; list != NULL && list[i] != NULL; i++) {
			if (gdm_user_uid (list[i]) >= new_val) 
				continue;					
			
			GdmInclude = strings_list_remove (GdmInclude, list[i], ",");
			removed = strings_list_add(removed, list[i], ",");
			
		}
		g_strfreev (list);

		//Now if there were items to remove then
		if (removed != NULL) {
			gboolean  valid;
			gchar *text;
			GtkWidget *include_treeview;
			GtkTreeModel *include_model;			
			GtkWidget *dlg;	
			GtkTreeIter iter;			
			GtkWidget *setup_dialog;

			setup_dialog = glade_xml_get_widget(xml, "setup_dialog");

			//Inform user about the change and its implications
			dlg = hig_dialog_new (GTK_WINDOW (setup_dialog),
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_MESSAGE_WARNING,
					      GTK_BUTTONS_OK,
					      _("Users include list modification"),
					      _("Some of the users in the Include list "
						"(Users tab) now have uid lower than "
						"MinimalUID and will be removed."));
			gtk_dialog_run (GTK_DIALOG (dlg));
			gtk_widget_destroy (dlg);						

			include_treeview  = glade_xml_get_widget (xml, "fb_include_treeview");

			include_model = gtk_tree_view_get_model (GTK_TREE_VIEW (include_treeview));

			valid = gtk_tree_model_get_iter_first (include_model, &iter);
			while (valid) {
				gtk_tree_model_get (include_model, &iter, USERLIST_NAME,
						    &text, -1);				
				
				if (strstr (removed, text) != NULL) {					
					valid = gtk_list_store_remove (GTK_LIST_STORE (include_model), &iter);	
				}
				else {				
					valid = gtk_tree_model_iter_next (include_model, &iter);
				}
			}
			g_free (text);
			/* Now we need to save updated list, toggle the 
			   automatic and timed loggon comboboxes and update greeters */
			gdm_setup_config_set_string (GDM_KEY_INCLUDE, GdmInclude);				
			
			greeters_need_update = TRUE;

		}
		
		g_free (removed);

		/* We also need to check if user (if any) in the
		   autologon/timed logon still match the criteria */
		autologon_user = gdm_config_get_string (GDM_KEY_AUTOMATIC_LOGIN);
		timedlogon_user = gdm_config_get_string (GDM_KEY_TIMED_LOGIN);

		if(!ve_string_empty (autologon_user)) {
			if (gdm_is_user_valid (autologon_user) && gdm_user_uid (autologon_user) < new_val) {
				gdm_setup_config_set_string (GDM_KEY_AUTOMATIC_LOGIN, "");			
				greeters_need_update = TRUE;
			}
		}

		if(!ve_string_empty (timedlogon_user)) {
			if (gdm_is_user_valid (timedlogon_user) && gdm_user_uid (timedlogon_user) < new_val) {
				gdm_setup_config_set_string (GDM_KEY_TIMED_LOGIN, "");						
				greeters_need_update = TRUE;
			}
		}

		g_free (autologon_user);
		g_free (timedlogon_user);

	}

	if (val != new_val)
		gdm_setup_config_set_int (key, new_val);

	if (greeters_need_update) {
		setup_user_combobox_list ("autologin_combo",
					  GDM_KEY_AUTOMATIC_LOGIN);
		setup_user_combobox_list ("timedlogin_combo",
					  GDM_KEY_TIMED_LOGIN);
		update_greeters ();
	}

	return FALSE;
}

static gint
display_sort_func (gpointer d1, gpointer d2)
{
   return (strcmp (ve_sure_string ((gchar *)d1), ve_sure_string ((gchar *)d2)));
}

static GSList *displays          = NULL;
static GSList *displays_inactive = NULL;
static GHashTable *dispval_hash  = NULL;

static void
gdm_load_displays (GKeyFile *cfg,
		   char    **keys)
{
	GSList *li2;
	int i;

	for (i = 0; keys[i] != NULL; i++) {
		const gchar *key = keys[i];

		if (isdigit (*key)) {
			gchar *fullkey;
			gchar *dispval;
			int keynum = atoi (key);
			gboolean skip_entry = FALSE;

			fullkey = g_strdup_printf ("%s/%s", GDM_KEY_SECTION_SERVERS, key);
			gdm_common_config_get_string (cfg, fullkey, &dispval, NULL);
			g_free (fullkey);

			/* Do not add if already in the list */
			for (li2 = displays; li2 != NULL; li2 = li2->next) {
				gchar *disp = li2->data;
				if (atoi (disp) == keynum) {
					skip_entry = TRUE;
					break;
				}
			}

			/* Do not add if this display was marked as inactive already */
			for (li2 = displays_inactive; li2 != NULL; li2 = li2->next) {
				gchar *disp = li2->data;
				if (atoi (disp) == keynum) {
					skip_entry = TRUE;
					break;
				}
			}

			if (skip_entry == TRUE) {
				g_free (dispval);
				continue;
			}

			if (g_ascii_strcasecmp (ve_sure_string (dispval), "inactive") == 0) {
				displays_inactive = g_slist_append (displays_inactive, g_strdup (key));
			} else {
				if (dispval_hash == NULL)
					dispval_hash = g_hash_table_new (g_str_hash, g_str_equal);            

				displays = g_slist_insert_sorted (displays, g_strdup (key), (GCompareFunc) display_sort_func);
				g_hash_table_insert (dispval_hash, g_strdup (key), g_strdup (dispval));
			}

			g_free (dispval);
		}
	}
}

static char *
ve_rest (const char *s)
{
	const char *p;
	gboolean single_quot = FALSE;
	gboolean double_quot = FALSE;
	gboolean escape = FALSE;

	if (s == NULL)
		return NULL;

	for (p = s; *p != '\0'; p++) {
		if (single_quot) {
			if (*p == '\'') {
				single_quot = FALSE;
			}
		} else if (escape) {
			escape = FALSE;
		} else if (double_quot) {
			if (*p == '"') {
				double_quot = FALSE;
			} else if (*p == '\\') {
				escape = TRUE;
			}
		} else if (*p == '\'') {
			single_quot = TRUE;
		} else if (*p == '"') {
			double_quot = TRUE;
		} else if (*p == '\\') {
			escape = TRUE;
		} else if (*p == ' ' || *p == '\t') {
			while (*p == ' ' || *p == '\t')
				p++;
			return g_strdup (p);
		}
	}

	return NULL;
}

static void
xservers_get_displays (GtkListStore *store)
{
	/* Find server definitions */
	GKeyFile *custom_cfg;
	GKeyFile *cfg;
	char    **keys;
	GSList *li;
	gchar *server, *options;

	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
        cfg = gdm_common_config_load (config_file, NULL);

	/* Fill list with all the active displays */
	if (custom_cfg != NULL) {
		keys = g_key_file_get_keys (custom_cfg, GDM_KEY_SECTION_SERVERS, NULL, NULL);
		gdm_load_displays (custom_cfg, keys);
		g_strfreev (keys);
	}

	keys = g_key_file_get_keys (cfg, GDM_KEY_SECTION_SERVERS, NULL, NULL);
	gdm_load_displays (cfg, keys);
	g_strfreev (keys);

	for (li = displays; li != NULL; li = li->next) {
		GtkTreeIter iter;
		gchar *key = li->data;
		int vt = atoi (key);
		server = ve_first_word (g_hash_table_lookup (dispval_hash, key));
		options = ve_rest (g_hash_table_lookup (dispval_hash, key));

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    XSERVER_COLUMN_VT, vt,
				    XSERVER_COLUMN_SERVER, server,
				    XSERVER_COLUMN_OPTIONS, options,
				    -1);
		g_free (server);
	}
	for (li = displays; li != NULL; li = li->next) {
		gchar *disp = li->data;
		g_free (disp);
	}
	g_slist_free (displays);
        displays = NULL;
	for (li = displays_inactive; li != NULL; li = li->next) {
		gchar *disp = li->data;
		g_free (disp);
	}
	g_slist_free (displays_inactive);
        displays_inactive = NULL;
        if (dispval_hash) {
           g_hash_table_destroy (dispval_hash);
           dispval_hash = NULL;
        }
}

static void
xserver_update_delete_sensitivity ()
{
	GtkWidget *modify_combobox, *delete_button;
	GtkListStore *store;
	GtkTreeIter iter;
	GdmXserver *xserver;
	gchar *text;
	gchar *selected;
	gboolean valid;
	gint i;

	modify_combobox = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");
	delete_button   = glade_xml_get_widget (xml_xservers, "xserver_deletebutton");

	/* Get list of servers that are set to start */
	store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
	                            G_TYPE_INT    /* virtual terminal */,
	                            G_TYPE_STRING /* server type */,
	                            G_TYPE_STRING /* options */);

	/* Get list of servers and determine which one was selected */
	xservers_get_displays (store);

	i = gtk_combo_box_get_active (GTK_COMBO_BOX (modify_combobox));
	if (i < 0) {
		gtk_widget_set_sensitive(delete_button, FALSE);
	} else {
		/* Get the xserver selected */
		xserver = g_slist_nth_data (xservers, i);
	
		/* Sensitivity of delete_button */
		if (g_slist_length (xservers) <= 1) {
			/* Can't delete the last server */
			gtk_widget_set_sensitive (delete_button, FALSE);
		} else {
			gtk_widget_set_sensitive (delete_button, TRUE);
			valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store),
			                                       &iter);
			selected = gtk_combo_box_get_active_text (
			              GTK_COMBO_BOX (modify_combobox));

			/* Can't delete servers currently in use */
			while (valid) {
				gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
				                    XSERVER_COLUMN_SERVER, &text, -1);
				if (strcmp (ve_sure_string (text), ve_sure_string (selected)) == 0) {
					gtk_widget_set_sensitive(delete_button, FALSE);
					break;
				}
				valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store),
				                                  &iter);
			}
		}
	}
}

static
void init_servers_combobox (int index)
{
	GtkWidget *mod_combobox;
	GtkWidget *name_entry;
	GtkWidget *command_entry;
	GtkWidget *style_combobox;
	GtkWidget *handled_checkbutton;
	GtkWidget *flexible_checkbutton;
	GtkWidget *priority_spinbutton;
	GtkListStore *store;
	GdmXserver *xserver;

	mod_combobox = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");
	name_entry = glade_xml_get_widget (xml_xservers, "xserver_name_entry");
	command_entry = glade_xml_get_widget (xml_xservers, "xserver_command_entry");
	style_combobox = glade_xml_get_widget (xml_xservers, "xserver_style_combobox");
	handled_checkbutton = glade_xml_get_widget (xml_xservers, "xserver_handled_checkbutton");
	flexible_checkbutton = glade_xml_get_widget (xml_xservers, "xserver_flexible_checkbutton");
	priority_spinbutton = glade_xml_get_widget(xml_xservers, "xserv_priority_spinbutton");

	/* Get list of servers that are set to start */
	store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
	                            G_TYPE_INT    /* virtual terminal */,
	                            G_TYPE_STRING /* server type */,
	                            G_TYPE_STRING /* options */);
	xservers_get_displays (store);

	xserver = g_slist_nth_data (xservers, index);

	gtk_combo_box_set_active (GTK_COMBO_BOX (mod_combobox), index);
	gtk_entry_set_text (GTK_ENTRY (name_entry), xserver->name);
	gtk_entry_set_text (GTK_ENTRY (command_entry), xserver->command);

	if (!xserver->chooser) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (style_combobox), XSERVER_LAUNCH_GREETER);
	}
	else {
		gtk_combo_box_set_active (GTK_COMBO_BOX (style_combobox), XSERVER_LAUNCH_CHOOSER);
	}
	
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (priority_spinbutton), 
				   xserver->priority);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (handled_checkbutton),
	                              xserver->handled);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flexible_checkbutton),
	                              xserver->flexible);

	xserver_update_delete_sensitivity ();
}

static void
update_remote_sensitivity (gboolean value)
{
	GtkWidget *remote_background_color_hbox;
	GtkWidget *remote_background_image_hhox;
	GtkWidget *remote_background_image_checkbutton;
	GtkWidget *remote_background_image_chooserbutton;
	GtkWidget *remote_logo_image_checkbutton;
	GtkWidget *remote_plain_logo_hbox;
	GtkWidget *remote_theme_background_hbox;
	GtkWidget *remote_theme_mode_hbox;
	GtkWidget *remote_theme_select_hbox;
	GtkWidget *sg_scale_background_remote_hbox;

	remote_background_color_hbox = glade_xml_get_widget (xml, "remote_background_color_hbox");
	remote_background_image_hhox = glade_xml_get_widget (xml, "remote_background_image_hhox");
	remote_background_image_checkbutton = glade_xml_get_widget (xml, "remote_background_image_checkbutton");
	remote_background_image_chooserbutton = glade_xml_get_widget (xml, "remote_background_image_chooserbutton");
	remote_logo_image_checkbutton = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");
	remote_plain_logo_hbox = glade_xml_get_widget (xml, "remote_plain_logo_hbox");
	remote_theme_background_hbox = glade_xml_get_widget (xml, "remote_theme_background_hbox");
	remote_theme_mode_hbox = glade_xml_get_widget (xml, "remote_theme_mode_hbox");
	remote_theme_select_hbox = glade_xml_get_widget (xml, "remote_theme_select_hbox");
	sg_scale_background_remote_hbox = glade_xml_get_widget (xml, "sg_scale_background_remote_hbox");

	gtk_widget_set_sensitive (remote_background_color_hbox, value);
	gtk_widget_set_sensitive (remote_background_image_hhox, value);
	gtk_widget_set_sensitive (remote_background_image_checkbutton, value);
	gtk_widget_set_sensitive (remote_background_image_chooserbutton, value);
	gtk_widget_set_sensitive (remote_logo_image_checkbutton, value);
	gtk_widget_set_sensitive (remote_plain_logo_hbox, value);
	gtk_widget_set_sensitive (remote_theme_background_hbox, value);
	gtk_widget_set_sensitive (remote_theme_mode_hbox, value);
	gtk_widget_set_sensitive (remote_theme_select_hbox, value);
	gtk_widget_set_sensitive (sg_scale_background_remote_hbox, value);
}

static void
refresh_remote_tab (void)
{
	GtkWidget *local_greeter;
	GtkWidget *remote_greeter;
	GtkWidget *remote_plain_vbox;
	GtkWidget *remote_themed_vbox;
	GtkWidget *configure_xdmcp_vbox;
	GtkWidget *welcome_message_vbox;
	GtkWidget *allowremoteroot;
	GtkWidget *allowremoteauto;
	gchar *remote_style;
	gint local_style;

	local_greeter = glade_xml_get_widget (xml, "local_greeter");
	remote_greeter = glade_xml_get_widget (xml, "remote_greeter");
	remote_plain_vbox = glade_xml_get_widget (xml, "remote_plain_properties_vbox");
	remote_themed_vbox = glade_xml_get_widget (xml, "remote_themed_properties_vbox");
	configure_xdmcp_vbox = glade_xml_get_widget (xml, "remote_configure_xdmcp_vbox");
	welcome_message_vbox = glade_xml_get_widget (xml, "remote_welcome_message_vbox");
	allowremoteroot = glade_xml_get_widget (xml, "allowremoteroot");
	allowremoteauto = glade_xml_get_widget (xml, "allowremoteauto");

	/* Remove previously added items from the combobox */
	gtk_combo_box_remove_text (GTK_COMBO_BOX (remote_greeter), REMOTE_PLAIN_WITH_FACE);
	gtk_combo_box_remove_text (GTK_COMBO_BOX (remote_greeter), REMOTE_PLAIN);
	
	local_style  = gtk_combo_box_get_active (GTK_COMBO_BOX (local_greeter));
	remote_style = gdm_config_get_string (GDM_KEY_REMOTE_GREETER);
					     			 
	if (gdm_config_get_bool (GDM_KEY_XDMCP) == FALSE) {
				
		if (local_style == LOCAL_PLAIN || local_style == LOCAL_PLAIN_WITH_FACE) {
			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Themed"));
		}
		else {
			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Plain"));
			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Plain with face browser"));
		}
				
		gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_DISABLED);
		gtk_widget_set_sensitive (allowremoteroot, FALSE);
		gtk_widget_set_sensitive (allowremoteauto, FALSE);
		gtk_widget_hide (remote_plain_vbox);
		gtk_widget_hide (remote_themed_vbox);
		gtk_widget_hide (welcome_message_vbox);
		gtk_widget_hide (configure_xdmcp_vbox);
	}
	else {
		if (local_style == LOCAL_PLAIN || local_style == LOCAL_PLAIN_WITH_FACE) {

			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Themed"));
			
			if (strstr (remote_style, "/gdmlogin") != NULL) {
				gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_SAME_AS_LOCAL);
				update_remote_sensitivity (FALSE);
				gtk_widget_show (remote_plain_vbox);
				gtk_widget_hide (remote_themed_vbox);
			}
			else if (strstr (remote_style, "/gdmgreeter") != NULL) {
				gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_THEMED);
				update_remote_sensitivity (TRUE);
				gtk_widget_hide (remote_plain_vbox);
				gtk_widget_show (remote_themed_vbox);
			}
		}
		else {
			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Plain"));
			gtk_combo_box_append_text (GTK_COMBO_BOX (remote_greeter), _("Plain with face browser"));

			if (strstr (remote_style, "/gdmlogin") != NULL) {
				gboolean use_browser;
				
				use_browser = gdm_config_get_bool (GDM_KEY_BROWSER);
				if (use_browser == FALSE) {
					gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_PLAIN);
				}
				else {
					gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_PLAIN_WITH_FACE);
				}
				update_remote_sensitivity (TRUE);
				gtk_widget_hide (remote_themed_vbox);
				gtk_widget_show (remote_plain_vbox);
			}
			else if (strstr (remote_style, "/gdmgreeter") != NULL) {
				gtk_combo_box_set_active (GTK_COMBO_BOX (remote_greeter), REMOTE_SAME_AS_LOCAL);
				update_remote_sensitivity (FALSE);
				gtk_widget_hide (remote_plain_vbox);
				gtk_widget_show (remote_themed_vbox);
			}
		}
		gtk_widget_set_sensitive (allowremoteauto, gdm_config_get_bool (GDM_KEY_XDMCP));
		gtk_widget_set_sensitive (allowremoteroot, TRUE);
		gtk_widget_show (welcome_message_vbox);
		gtk_widget_show (configure_xdmcp_vbox);
	}
}

/*
 * We probably should check the server definition in the defaults.conf file
 * and just erase the section if the values are the same, like we do for the
 * displays section and the normal configuration sections.
 */
static void
update_xserver (gchar *section, GdmXserver *svr)
{
	GKeyFile *custom_cfg;
	gchar *real_section;
	gchar *key;

	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
	real_section = g_strdup_printf ("%s%s", GDM_KEY_SERVER_PREFIX, section);

	key = g_strconcat (real_section, "/" GDM_KEY_SERVER_NAME, NULL);
	gdm_common_config_set_string (custom_cfg, key, svr->name);
	g_free (key);

	key = g_strconcat (real_section, "/" GDM_KEY_SERVER_COMMAND, NULL);
	gdm_common_config_set_string (custom_cfg, key, svr->command);
	g_free (key);

	key = g_strconcat (real_section, "/", GDM_KEY_SERVER_CHOOSER, NULL);
	gdm_common_config_set_boolean (custom_cfg, key, svr->chooser);
	g_free (key);

	key = g_strconcat (real_section, "/" GDM_KEY_SERVER_HANDLED, NULL);
	gdm_common_config_set_boolean (custom_cfg, key, svr->handled);
	g_free (key);

	key = g_strconcat (real_section, "/" GDM_KEY_SERVER_FLEXIBLE, NULL);
	gdm_common_config_set_boolean (custom_cfg, key, svr->flexible);
	g_free (key);

	key = g_strconcat (real_section, "/" GDM_KEY_SERVER_PRIORITY, NULL);
	gdm_common_config_set_int (custom_cfg, key, svr->priority);
	g_free (key);

        g_free (real_section);
	gdm_common_config_save (custom_cfg, custom_config_file, NULL);

	g_key_file_free (custom_cfg);

	update_key ("xservers/PARAMETERS");
}

static gboolean
combobox_timeout (GtkWidget *combo_box)
{
	const char *key = g_object_get_data (G_OBJECT (combo_box), "key");
	int selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));

	/* Local Greeter Comboboxes */
	if (strcmp (ve_sure_string (key), GDM_KEY_GREETER) == 0) {

		gchar *old_key_val;
		gchar *new_key_val;
		gboolean browser_val;

		old_key_val = gdm_config_get_string ((gchar *)key);
		new_key_val = NULL;

		if (selected == LOCAL_PLAIN_WITH_FACE) {		
			new_key_val = g_strdup (LIBEXECDIR "/gdmlogin");
			browser_val = TRUE;
		} 
		else if (selected == LOCAL_THEMED) {
			new_key_val = g_strdup (LIBEXECDIR "/gdmgreeter");
			browser_val = gdm_config_get_bool (GDM_KEY_BROWSER);
		}
		else {  /* Plain style */
			new_key_val = g_strdup (LIBEXECDIR "/gdmlogin");
			browser_val = FALSE;
		}		
		
		if (new_key_val && 
		    strcmp (ve_sure_string (old_key_val), ve_sure_string (new_key_val)) != 0) {	
		    
			gdm_setup_config_set_string (key, new_key_val);
			gdm_setup_config_set_bool (GDM_KEY_BROWSER, browser_val);
		}
		else {
			gdm_setup_config_set_bool (GDM_KEY_BROWSER, browser_val);
		}
		update_greeters ();
		
		refresh_remote_tab ();
		g_free (new_key_val);
	}
	/* Remote Greeter Comboboxes */
	else if (strcmp (ve_sure_string (key), GDM_KEY_REMOTE_GREETER) == 0) {
		
		if (selected == REMOTE_DISABLED) {
			gdm_setup_config_set_bool (GDM_KEY_XDMCP, FALSE);		
		} else {
			gchar    *new_key_val = NULL;
			gboolean free_new_val = TRUE;
						
			if (selected == REMOTE_SAME_AS_LOCAL) {
				new_key_val  = gdm_config_get_string (GDM_KEY_GREETER);
				free_new_val = FALSE;
			}
			else if (selected == REMOTE_PLAIN_WITH_FACE) {
				new_key_val = g_strdup (LIBEXECDIR "/gdmlogin");
				gdm_setup_config_set_bool (GDM_KEY_BROWSER, TRUE);
			}
			else {
				gchar *selected_text;
				
				selected_text = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo_box));
				
				if (strcmp (ve_sure_string (selected_text), _("Themed")) == 0) {
					new_key_val = g_strdup (LIBEXECDIR "/gdmgreeter");
				}
				else {
					new_key_val = g_strdup (LIBEXECDIR "/gdmlogin");
					gdm_setup_config_set_bool (GDM_KEY_BROWSER, FALSE);
				}
				g_free (selected_text);
			}			
			
			gdm_setup_config_set_string (key, new_key_val);
			gdm_setup_config_set_bool (GDM_KEY_XDMCP, TRUE);
			if (free_new_val)
				g_free (new_key_val);
		}
		update_greeters ();
		return FALSE;

	/* Automatic Login Combobox */
	} else if (strcmp (ve_sure_string (key), GDM_KEY_AUTOMATIC_LOGIN) == 0 ||
	           strcmp (ve_sure_string (key), GDM_KEY_TIMED_LOGIN) == 0) {

		GtkTreeIter iter;
		char *new_val = NULL;
		gchar *val;
		
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), 
		    &iter)) {
			gtk_tree_model_get (gtk_combo_box_get_model (
				GTK_COMBO_BOX (combo_box)), &iter,
				0, &new_val, -1);
		}
		else {
			/* The selection was typed so there are two possibilities
			   1. The typed value is a user
			   2. The typed value is a script, garbage, ete (anything but an
			   existing user)
			   If its case 1. then we check if the user matches the MinimalUID 
			   criteria or is not root. If its case 2. we do not do any checking */
			
			new_val = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo_box));
			if (gdm_is_user_valid (ve_sure_string (new_val))) {
				gint user_uid = gdm_user_uid (new_val);
				gint GdmMinimalUID = gdm_config_get_int (GDM_KEY_MINIMAL_UID);
				
				if (user_uid == 0 || user_uid < GdmMinimalUID) {
					/* we can't accept users that have uid lower
					   than minimal uid, or uid = 0 (root) */
					gchar *str;
					GtkWidget *dialog;
					GtkWidget *setup_dialog;
					
					if (gdm_user_uid (new_val) == 0)
						str = g_strdup (_("Autologin or timed login to the root account is forbidden."));
					else 
						str = g_strdup_printf (_("The \"%s\" user UID is lower than allowed MinimalUID."), new_val);				
					setup_dialog = glade_xml_get_widget(xml, "setup_dialog");
										
					dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
								 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
								 GTK_MESSAGE_ERROR,
								 GTK_BUTTONS_OK,
								 _("User not allowed"),
								 str);
					gtk_dialog_run (GTK_DIALOG (dialog));
					gtk_widget_destroy (dialog);
					g_free (str);
					
					gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
					return TRUE;
				}
			}
		}


		val = gdm_config_get_string ((gchar *)key);
		if (new_val &&
		    strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {

			gdm_setup_config_set_string (key, new_val);
		}
		g_free (new_val);
	} 
	else if (strcmp (ve_sure_string (key), GDM_KEY_GRAPHICAL_THEME_RAND) == 0 ) {	
	
		/* Theme Combobox */
		gboolean new_val;
		gboolean old_val ;
		
		old_val = gdm_config_get_bool ((gchar *)key);

		/* Choose to display radio or checkbox toggle column.
		   We will only set this option to true if there is at least one
		   item in the selected_themes list otherwise Random from selected
		   will be disabled */
		if (selected == RANDOM_THEME && !ve_string_empty (selected_themes))
			new_val = TRUE;
		else /* Default to one theme */
			new_val = FALSE;

		/* Update config */
		if (new_val != old_val)
			gdm_setup_config_set_bool (key, new_val);
	}
	/* Style combobox */
	else if (strcmp (ve_sure_string (key), GDM_KEY_SERVER_CHOOSER) == 0) {
		GtkWidget *mod_combobox;
		GtkWidget *style_combobox;
		GSList *li;
		gchar *section;
		gboolean val_old, val_new;

		mod_combobox    = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");
		style_combobox  = glade_xml_get_widget (xml_xservers, "xserver_style_combobox");

		/* Get xserver section to update */
		section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (mod_combobox));

		for (li = xservers; li != NULL; li = li->next) {
			GdmXserver *svr = li->data;
			if (strcmp (ve_sure_string (svr->id), ve_sure_string (section)) == 0) {

				val_old = svr->chooser;
				val_new = (gtk_combo_box_get_active (GTK_COMBO_BOX (style_combobox)) != 0);

				/* Update this servers configuration */
				if (! bool_equal (val_old, val_new)) {
					svr->chooser = val_new;
					update_xserver (section, svr);
				}
				break;
			}
		}
		g_free (section);
	}
	/* Use 24 clock combobox */
	else if (strcmp (ve_sure_string (key), GDM_KEY_USE_24_CLOCK) == 0) {		
		gchar *val;		
		gchar *new_val;
		
		new_val = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo_box));
		val = gdm_config_get_string ((gchar *)key);
		if (new_val &&
		    strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {			
			gdm_setup_config_set_string (key, new_val);
		}
		g_free (new_val);
		g_free (val);
	}
	/* Commands combobox */
	else if (strcmp (ve_sure_string (key), _("command_chooser_combobox")) == 0) {
		/* We need to set the data according to selected command */
		GtkWidget *hrs_cmd_entry = NULL;
		GtkWidget *cust_cmd_entry = NULL;
		GtkWidget *cust_cmd_label_entry = NULL;
		GtkWidget *cust_cmd_lrlabel_entry = NULL;
		GtkWidget *cust_cmd_text_entry = NULL;
		GtkWidget *cust_cmd_tooltip_entry = NULL;
		GtkWidget *cust_cmd_persistent_checkbox = NULL;
		GtkWidget *cust_cmd_norestart_checkbox = NULL;
		GtkWidget *status_label;
		gchar *val = NULL;		
		gboolean bool_val = FALSE;		
		gboolean enabled_command = FALSE;
		
		hrs_cmd_entry = glade_xml_get_widget (xml_commands, "hrs_cmd_path_entry");
		cust_cmd_entry  = glade_xml_get_widget (xml_commands, "custom_cmd_path_entry");
		status_label = glade_xml_get_widget (xml_commands, "command_status_label");

		if (selected == HALT_CMD) {
			val = gdm_config_get_string (GDM_KEY_HALT);		
			gtk_entry_set_text (GTK_ENTRY (hrs_cmd_entry), ve_sure_string (val));
			enabled_command = !ve_string_empty (val);
		}
		else if (selected == REBOOT_CMD) {
			val = gdm_config_get_string (GDM_KEY_REBOOT);			
			gtk_entry_set_text (GTK_ENTRY (hrs_cmd_entry), ve_sure_string (val));
			enabled_command = !ve_string_empty (val);
		}
		else if (selected == SUSPEND_CMD) {
			val = gdm_config_get_string (GDM_KEY_SUSPEND);			
			gtk_entry_set_text (GTK_ENTRY (hrs_cmd_entry), ve_sure_string (val));
			enabled_command = !ve_string_empty (val);
		}
		else {
			gchar *key_string = NULL;

			gint i = selected - CUSTOM_CMD;
			/* Here we are going to deal with custom commands */
			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_TEMPLATE, i);
			val = gdm_config_get_string (key_string);
			gtk_entry_set_text (GTK_ENTRY (cust_cmd_entry), ve_sure_string (val));
			enabled_command = !ve_string_empty (val);
			g_free (key_string);
			g_free (val);
			
			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_LABEL_TEMPLATE, i);
			cust_cmd_label_entry  = glade_xml_get_widget (xml_commands, "custom_cmd_label_entry");
			val = gdm_config_get_string (key_string);
			gtk_entry_set_text (GTK_ENTRY (cust_cmd_label_entry), ve_sure_string (val));
			g_free (key_string);
			g_free (val);

			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_LR_LABEL_TEMPLATE, i);
			cust_cmd_lrlabel_entry  = glade_xml_get_widget (xml_commands, "custom_cmd_lrlabel_entry");
			val = gdm_config_get_string (key_string);
			gtk_entry_set_text (GTK_ENTRY (cust_cmd_lrlabel_entry), ve_sure_string (val));
			g_free (key_string);
			g_free (val);

			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_TEXT_TEMPLATE, i);
			cust_cmd_text_entry  = glade_xml_get_widget (xml_commands, "custom_cmd_text_entry");
			val = gdm_config_get_string (key_string);
			gtk_entry_set_text (GTK_ENTRY (cust_cmd_text_entry), ve_sure_string (val));
			g_free (key_string);
			g_free (val);
			
			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_TOOLTIP_TEMPLATE, i);
			cust_cmd_tooltip_entry  = glade_xml_get_widget (xml_commands, "custom_cmd_tooltip_entry");
			val = gdm_config_get_string (key_string);
			gtk_entry_set_text (GTK_ENTRY (cust_cmd_tooltip_entry), ve_sure_string (val));
			g_free (key_string);
			
			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE, i);
			cust_cmd_norestart_checkbox  = glade_xml_get_widget (xml_commands, "custom_cmd_norestart_checkbutton");
			bool_val = gdm_config_get_bool (key_string);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cust_cmd_norestart_checkbox), bool_val);
			g_free (key_string);
			
			key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE, i);
			cust_cmd_persistent_checkbox  = glade_xml_get_widget (xml_commands, "custom_cmd_persistent_checkbutton");
			bool_val = gdm_config_get_bool (key_string);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cust_cmd_persistent_checkbox), bool_val);
			g_free (key_string);

		}
		g_free (val);

		if (enabled_command)
			gtk_label_set_text (GTK_LABEL (status_label), _("(Enabled)"));			
		else 
			gtk_label_set_text (GTK_LABEL (status_label), _("(Disabled)"));	
	}
	/* Default session combobox*/
	else if (strcmp (ve_sure_string (key), GDM_KEY_DEFAULT_SESSION) == 0) {
		/* First we get the selected index. Next we lookup the actual
		   filename in the List of sessions */
		gchar *val;
		gchar *new_val = NULL;
		
		val = gdm_config_get_string ((gchar *)key);
		new_val = g_strdup ((gchar*) g_list_nth_data (sessions, selected));
		
		if (strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0)
			gdm_setup_config_set_string (key,  ve_sure_string (new_val));

		g_free (new_val);
		g_free (val);		
	}
	return FALSE;
}

static void
toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 200, toggle_timeout);
}

static void
command_toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 200, command_toggle_timeout);
}

static void
radiogroup_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 200, radiogroup_timeout);
}

static void
list_selection_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *widget = data;
	GtkWidget *include_treeview;
	GtkTreeSelection *selection;
	GtkTreeModel *include_model;
	GtkTreeIter iter;
	gboolean val;

	val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));

	include_treeview  = glade_xml_get_widget (xml, "fb_include_treeview");
	include_model = gtk_tree_view_get_model (GTK_TREE_VIEW (include_treeview));
	
	selection = gtk_tree_view_get_selection (
	            GTK_TREE_VIEW (include_treeview));

	if ((val == FALSE) && (gtk_tree_selection_get_selected (selection, &(include_model), &iter))) {
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	run_timeout (toggle, 200, toggle_timeout);
}

static void
intspin_changed (GtkWidget *spin)
{
	run_timeout (spin, 500, intspin_timeout);
}

static void
combobox_changed (GtkWidget *combobox)
{
	const char *key = g_object_get_data (G_OBJECT (combobox), "key");
	
	if (strcmp (ve_sure_string (key), GDM_KEY_GREETER) == 0) {

		GtkWidget *local_plain_vbox;
		GtkWidget *local_themed_vbox;
		gint selected;
		
		local_plain_vbox = glade_xml_get_widget (xml, "local_plain_properties_vbox");
		local_themed_vbox = glade_xml_get_widget (xml, "local_themed_properties_vbox");

		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));

		if (selected == LOCAL_PLAIN_WITH_FACE) {					
			gtk_widget_show (local_plain_vbox);
			gtk_widget_hide (local_themed_vbox);									
		} 
		else if (selected == LOCAL_THEMED) {						
			gtk_widget_hide (local_plain_vbox);
			gtk_widget_show (local_themed_vbox);
		}
		else {  /* Plain style */
			gtk_widget_show (local_plain_vbox);
			gtk_widget_hide (local_themed_vbox);
		}
	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_REMOTE_GREETER) == 0) {

		GtkWidget *remote_plain_vbox;
		GtkWidget *remote_themed_vbox;
		GtkWidget *configure_xdmcp_vbox;
		GtkWidget *welcome_message_vbox;
		gint selected;

		remote_plain_vbox = glade_xml_get_widget (xml, "remote_plain_properties_vbox");
		remote_themed_vbox = glade_xml_get_widget (xml, "remote_themed_properties_vbox");
		configure_xdmcp_vbox = glade_xml_get_widget (xml, "remote_configure_xdmcp_vbox");
		welcome_message_vbox = glade_xml_get_widget (xml, "remote_welcome_message_vbox");

		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));

		if (selected == REMOTE_DISABLED) {
			GtkWidget *allowremoteauto;
			GtkWidget *allowremoteroot;

			allowremoteauto = glade_xml_get_widget (xml, "allowremoteauto");
			allowremoteroot = glade_xml_get_widget (xml, "allowremoteroot");

			gtk_widget_set_sensitive (allowremoteauto, FALSE);
			gtk_widget_set_sensitive (allowremoteroot, FALSE);

			gtk_widget_hide (remote_plain_vbox);
			gtk_widget_hide (remote_themed_vbox);
			gtk_widget_hide (welcome_message_vbox);
			gtk_widget_hide (configure_xdmcp_vbox);
		}
		else {
			GtkWidget *timedlogin;
			GtkWidget *allowremoteauto;
			GtkWidget *allowremoteroot;

			timedlogin = glade_xml_get_widget (xml, "timedlogin");
			allowremoteauto = glade_xml_get_widget (xml, "allowremoteauto");
			allowremoteroot = glade_xml_get_widget (xml, "allowremoteroot");

			gtk_widget_set_sensitive (allowremoteauto, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (timedlogin)));
			gtk_widget_set_sensitive (allowremoteroot, TRUE);
			
			gtk_widget_show (welcome_message_vbox);
			gtk_widget_show (configure_xdmcp_vbox);
			
			if (selected == REMOTE_SAME_AS_LOCAL) {
				gchar *greeter_style;
				
				greeter_style = gdm_config_get_string (GDM_KEY_GREETER);
				update_remote_sensitivity (FALSE);
				
				if (strstr (greeter_style, "/gdmgreeter") != NULL) {
					gtk_widget_hide (remote_plain_vbox);
					gtk_widget_show (remote_themed_vbox);
				}
				else {
					gtk_widget_hide (remote_themed_vbox);
					gtk_widget_show (remote_plain_vbox);
				}
			}
			else if (selected == REMOTE_PLAIN_WITH_FACE) {
				update_remote_sensitivity (TRUE);
				gtk_widget_hide (remote_themed_vbox);
				gtk_widget_show (remote_plain_vbox);
			}
			else {
				gchar *selected_text;
				
				selected_text = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combobox));
				update_remote_sensitivity (TRUE);
				
				if (strcmp (ve_sure_string (selected_text), _("Themed")) == 0) {
					gtk_widget_hide (remote_plain_vbox);
					gtk_widget_show (remote_themed_vbox);
				}
				else {
					gtk_widget_hide (remote_themed_vbox);
					gtk_widget_show (remote_plain_vbox);
				}
				g_free (selected_text);
			}
		}

	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_GRAPHICAL_THEME_RAND) == 0) {

		GtkWidget *theme_list;
		GtkWidget *theme_list_remote;
		GtkWidget *delete_button;
		GtkWidget *delete_button_remote;
		GtkTreeViewColumn *radioColumn = NULL;
		GtkTreeViewColumn *radioColumnRemote = NULL;
		GtkTreeViewColumn *checkboxColumn = NULL;
		GtkTreeViewColumn *checkboxColumnRemote = NULL;
		GtkTreeSelection *selection;
		GtkTreeIter iter;
		gint selected;
				
		theme_list = glade_xml_get_widget (xml, "gg_theme_list");
		theme_list_remote = glade_xml_get_widget (xml, "gg_theme_list_remote");
		delete_button = glade_xml_get_widget (xml, "gg_delete_theme");
		delete_button_remote = glade_xml_get_widget (xml, "gg_delete_theme_remote");

		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));

		if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {
			GtkWidget *mode_combobox;
			
			mode_combobox = glade_xml_get_widget (xml, "gg_mode_combobox_remote");
			gtk_combo_box_set_active (GTK_COMBO_BOX (mode_combobox), selected);

			if (mode_combobox != combobox) {
				/* Display a warning when no themes are selected and the
				   Random from selected option has been activated. 
				   This is a bit of a HACK as combo_box handler is 
				   called multiple times and we want this warning 
				   to be displayed only once */
				
				if (selected == RANDOM_THEME && ve_string_empty (selected_themes)) {
					GtkWidget *setup_dialog = glade_xml_get_widget(xml, "setup_dialog");
					
					GtkWidget *warn_dlg = hig_dialog_new (GTK_WINDOW (setup_dialog) /* parent */,
									      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT /* flags */,
									      GTK_MESSAGE_WARNING,
									      GTK_BUTTONS_OK,
									      _("No themes selected!"),
									      _("You need one or more themes selected for the "
										"\"Random from selected\" option to be valid. "
										"Failure to do so will force \"Selected only\" mode."));
					gtk_dialog_run (GTK_DIALOG (warn_dlg));
					gtk_widget_destroy (warn_dlg);
					
					GdmRandomFromSelectedChangesWarn = TRUE;
				}
				else if (selected == ONE_THEME)
					GdmRandomFromSelectedChangesWarn = FALSE;
			}
		}
		else {
			GtkWidget *mode_combobox;
			
			mode_combobox = glade_xml_get_widget (xml, "gg_mode_combobox");
			gtk_combo_box_set_active (GTK_COMBO_BOX (mode_combobox), selected);

			if (mode_combobox != combobox) {
				/* Display a warning when no themes are selected and the
				   Random from selected option has been activated. 
				   This is a bit of a HACK as combo_box handler is 
				   called multiple times and we want this warning 
				   to be displayed only once */
				if (selected == RANDOM_THEME && ve_string_empty (selected_themes)) {
					GtkWidget *setup_dialog = glade_xml_get_widget(xml, "setup_dialog");
					
					GtkWidget *warn_dlg = hig_dialog_new (GTK_WINDOW (setup_dialog) /* parent */,
									      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT /* flags */,
									      GTK_MESSAGE_WARNING,
									      GTK_BUTTONS_OK,
									      _("No themes selected!"),
									      _("You need one or more themes "
										"selected for the \"Random from selected\" "
										"option to be valid. Failure to do so will "
										"force \"Selected only\" mode."));
					gtk_dialog_run (GTK_DIALOG (warn_dlg));
					gtk_widget_destroy (warn_dlg);
					
					GdmRandomFromSelectedChangesWarn = TRUE;
				}
				else if (selected == ONE_THEME) {
					GdmRandomFromSelectedChangesWarn = FALSE;
				}
			}
		}

		radioColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
		                                        THEME_COLUMN_SELECTED);
		radioColumnRemote = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list_remote),
		                                              THEME_COLUMN_SELECTED);
		checkboxColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
		                                           THEME_COLUMN_SELECTED_LIST);
		checkboxColumnRemote = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list_remote),
		                                                 THEME_COLUMN_SELECTED_LIST);
				
		/* Choose to display radio or checkbox toggle column */
		if (selected == RANDOM_THEME) {
			if (GTK_IS_TREE_VIEW_COLUMN (radioColumn)) 
				gtk_tree_view_column_set_visible (radioColumn, FALSE);
			if (GTK_IS_TREE_VIEW_COLUMN (radioColumnRemote))
				gtk_tree_view_column_set_visible (radioColumnRemote, FALSE);
			if (GTK_IS_TREE_VIEW_COLUMN (checkboxColumn))
				gtk_tree_view_column_set_visible (checkboxColumn, TRUE);
			if (GTK_IS_TREE_VIEW_COLUMN (checkboxColumnRemote))
				gtk_tree_view_column_set_visible (checkboxColumnRemote, TRUE);
		} else { /* Default to one theme */
			if (GTK_IS_TREE_VIEW_COLUMN (radioColumn))
				gtk_tree_view_column_set_visible (radioColumn, TRUE);
			if (GTK_IS_TREE_VIEW_COLUMN (radioColumnRemote))
				gtk_tree_view_column_set_visible (radioColumnRemote, TRUE);
			if (GTK_IS_TREE_VIEW_COLUMN (checkboxColumn))
				gtk_tree_view_column_set_visible (checkboxColumn, FALSE);
			if (GTK_IS_TREE_VIEW_COLUMN (checkboxColumnRemote))
				gtk_tree_view_column_set_visible (checkboxColumnRemote, FALSE);
		}

		/* Update delete button's sensitivity */
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
		gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

		if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
			gtk_widget_set_sensitive (delete_button, FALSE);
			gtk_widget_set_sensitive (delete_button_remote, FALSE);
		} 
		else {
			GValue value = {0, };
			GtkTreeModel *model;
			gboolean GdmGraphicalThemeRand;
			
			/* Determine if the theme selected is currently active */
			model = gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list));
			
			GdmGraphicalThemeRand = gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND);
			if (GdmGraphicalThemeRand) {
				gtk_tree_model_get_value (model, &iter,
				                          THEME_COLUMN_SELECTED_LIST, &value);
				
				/* Make sure that the theme is not selected in the
				   single theme mode */
				if (!g_value_get_boolean (&value)) {
					g_value_unset (&value);
					gtk_tree_model_get_value (model, &iter,
								  THEME_COLUMN_SELECTED, &value);
				}
			} else {
				gtk_tree_model_get_value (model, &iter,
				                          THEME_COLUMN_SELECTED, &value);
				
				/* We might have "Random from selected" option selected, but no
				   active themes there hence defaulting to "Selected only" 
				   Lets make this theme delete-able from "Random from selected"
				   mode in this case */
				if (g_value_get_boolean (&value) && selected == RANDOM_THEME) {	
					g_value_unset (&value);
					g_value_set_boolean (&value, FALSE);				    
				}
			}
	
    			if (g_value_get_boolean (&value)) {
				/* Do not allow deleting of active themes */
        			gtk_widget_set_sensitive (delete_button, FALSE);
        			gtk_widget_set_sensitive (delete_button_remote, FALSE);
		    	}
			else {
				gtk_widget_set_sensitive (delete_button, TRUE);
				gtk_widget_set_sensitive (delete_button_remote, TRUE);
			}

			g_value_unset (&value);
		}      			      
	}
	else if (strcmp (ve_sure_string (key), GDM_KEY_SERVER_PREFIX) == 0 ) {
		init_servers_combobox (gtk_combo_box_get_active (GTK_COMBO_BOX (combobox)));
	}
	else if (strcmp (ve_sure_string (key), _("command_chooser_combobox")) == 0) {
		gint selected;
		GtkWidget *hrs_cmd_vbox;
		GtkWidget *custom_cmd_vbox;
		
		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));			

		/* First of all we need to check if we had made any changes
		   to any of the command fields. If so user gets reminded and
		   given an option to save, or discard */
		if (g_hash_table_size (GdmCommandChangesUnsaved) != 0) {
			GtkWidget *prompt;
			GtkWidget *setup_dialog;
			GtkWidget *apply_command;
			gint response;
			
			setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
			
			prompt = hig_dialog_new (GTK_WINDOW (setup_dialog),
						 GTK_DIALOG_MODAL |
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_NONE,
						 _("Apply changes to the modified command?"),
						 _("If you don't apply, the changes "
						   "will be discarded."));
			
			gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-cancel", GTK_RESPONSE_CANCEL); 
			gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-apply", GTK_RESPONSE_APPLY);
			
			response = gtk_dialog_run (GTK_DIALOG (prompt));
			gtk_widget_destroy (prompt);	       
			
			apply_command = glade_xml_get_widget (xml_commands, "command_apply_button");
			if (response == GTK_RESPONSE_APPLY)				
				g_signal_emit_by_name (G_OBJECT (apply_command), "clicked");
			
			else {
				g_hash_table_remove_all (GdmCommandChangesUnsaved);				
				gtk_widget_set_sensitive (apply_command, FALSE);	
			}
		}
		
		last_selected_command = selected;

		hrs_cmd_vbox = glade_xml_get_widget (xml_commands, "hrs_command_vbox");
		custom_cmd_vbox = glade_xml_get_widget (xml_commands, "custom_command_vbox");
		if (selected > SUSPEND_CMD) {
			/* We are dealing with custom commands */							
			gtk_widget_show (custom_cmd_vbox);
			gtk_widget_hide (hrs_cmd_vbox);					
		}
		else {
			/* We are dealing with hrs (Halt, Reboot, Shutdown) commands */
			gtk_widget_hide (custom_cmd_vbox);
			gtk_widget_show (hrs_cmd_vbox);	
		}

		/* We dont want default timeout for this one so we
		   are going to bail out now */
		run_timeout (combobox, 10, combobox_timeout);		
		return;
	}
	run_timeout (combobox, 500, combobox_timeout);
}

static void
timeout_remove (GtkWidget *widget)
{
	gboolean (*func) (GtkWidget *);
	guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget),
							"change_timeout"));
	if (id != 0) {
		g_source_remove (id);
		g_object_set_data (G_OBJECT (widget), "change_timeout", NULL);
	}

	func = g_object_get_data (G_OBJECT (widget), "timeout_func");
	if (func != NULL) {
		(*func) (widget);
		g_object_set_data (G_OBJECT (widget), "timeout_func", NULL);
	}
}

static void
timeout_remove_all (void)
{
	GList *li, *list;

	list = timeout_widgets;
	timeout_widgets = NULL;

	for (li = list; li != NULL; li = li->next) {
		timeout_remove (li->data);
		li->data = NULL;
	}
	g_list_free (list);
}

static void
toggle_toggled_sensitivity_positive (GtkWidget *toggle, GtkWidget *depend)
{
	gtk_widget_set_sensitive (depend, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)));
}

static void
toggle_toggled_sensitivity_negative (GtkWidget *toggle, GtkWidget *depend)
{
	gtk_widget_set_sensitive (depend, !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)));
}

static void
timedlogin_allow_remote_toggled (GtkWidget *toggle, GtkWidget *depend)
{
	if (gdm_config_get_bool (GDM_KEY_XDMCP) == TRUE) {
		gtk_widget_set_sensitive (depend, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)));
	}
	else {
		gtk_widget_set_sensitive (depend, FALSE);
	}		
}

static void
setup_notify_toggle (const char *name,
		     const char *key)
{
	GtkWidget *toggle;
	gboolean val;

	toggle = glade_xml_get_widget (xml, name);
	val    = gdm_config_get_bool ((gchar *)key);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	g_object_set_data_full (G_OBJECT (toggle),
	                        "key", g_strdup (key),
	                        (GDestroyNotify) g_free);

	if (strcmp (ve_sure_string (name), "sysmenu") == 0) {
	
		GtkWidget *config_available;
		GtkWidget *chooser_button;
		
		config_available = glade_xml_get_widget (xml, "config_available");
		chooser_button = glade_xml_get_widget (xml, "chooser_button");

		gtk_widget_set_sensitive (config_available, val);
		gtk_widget_set_sensitive (chooser_button, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), config_available);
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), chooser_button);
	}
	else if (strcmp ("autologin", ve_sure_string (name)) == 0) {

		GtkWidget *autologin_label;
		GtkWidget *autologin_combo;

		autologin_label = glade_xml_get_widget (xml, "autologin_label");
		autologin_combo = glade_xml_get_widget (xml, "autologin_combo");

		gtk_widget_set_sensitive (autologin_label, val);
		gtk_widget_set_sensitive (autologin_combo, val);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), 
		                  autologin_label);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), 
		                  autologin_combo);
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);
	}
	else if (strcmp ("timedlogin", ve_sure_string (name)) == 0) {

		GtkWidget *timedlogin_label;
		GtkWidget *timedlogin_combo;
		GtkWidget *timedlogin_seconds_label;
		GtkWidget *timedlogin_seconds_spin_button;
		GtkWidget *timedlogin_seconds_units;
		GtkWidget *timedlogin_allow_remote;
		
		timedlogin_label = glade_xml_get_widget (xml, "timed_login_label");
		timedlogin_combo = glade_xml_get_widget (xml, "timedlogin_combo");
		timedlogin_seconds_label = glade_xml_get_widget (xml, "timedlogin_seconds_label");
		timedlogin_seconds_spin_button = glade_xml_get_widget (xml,"timedlogin_seconds");
		timedlogin_seconds_units = glade_xml_get_widget (xml,"timedlogin_seconds_units");
		timedlogin_allow_remote = glade_xml_get_widget (xml, "allowremoteauto");

		gtk_widget_set_sensitive (timedlogin_label, val);
		gtk_widget_set_sensitive (timedlogin_combo, val);
		gtk_widget_set_sensitive (timedlogin_seconds_label, val);
		gtk_widget_set_sensitive (timedlogin_seconds_spin_button, val);
		gtk_widget_set_sensitive (timedlogin_seconds_units, val);

		if (gdm_config_get_bool (GDM_KEY_XDMCP) == FALSE) {
			gtk_widget_set_sensitive (timedlogin_allow_remote, FALSE);
		}
		else {
			gtk_widget_set_sensitive (timedlogin_allow_remote, val);		
		}

		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), timedlogin_label);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), timedlogin_combo);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), timedlogin_seconds_label);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), timedlogin_seconds_spin_button);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), timedlogin_seconds_units);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
		                  G_CALLBACK (timedlogin_allow_remote_toggled), timedlogin_allow_remote);
	}
	else if (strcmp ("vis_feedback_passwd_checkbox", ve_sure_string (name)) == 0) {
		/* This one is a lil bit back to front
		   true is false and false is true */
		GtkWidget *use_circles_in_passwd;
		use_circles_in_passwd = glade_xml_get_widget (xml, "use_circles_passwd_checkbox");

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), !val);
		gtk_widget_set_sensitive (use_circles_in_passwd, !val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), use_circles_in_passwd);
		
	}
	else if (strcmp ("local_set_pos_checkbox", ve_sure_string (name)) == 0) {
		
		GtkWidget *posx;
		GtkWidget *posy;

		posx =  glade_xml_get_widget (xml, "local_posx_spinbutton");

		posy =  glade_xml_get_widget (xml, "local_posy_spinbutton");

		gtk_widget_set_sensitive (posx, val);
		gtk_widget_set_sensitive (posy, val);

		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), posx);
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), posy);
	}
	else if (strcmp ("remote_set_pos_checkbox", ve_sure_string (name)) == 0) {
		
		GtkWidget *posx;
		GtkWidget *posy;

		posx =  glade_xml_get_widget (xml, "remote_posx_spinbutton");
		posy =  glade_xml_get_widget (xml, "remote_posy_spinbutton");

		gtk_widget_set_sensitive (posx, val);
		gtk_widget_set_sensitive (posy, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), posx);
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), posy);
	}
	else if (strcmp ("local_title_bar_checkbutton", ve_sure_string (name)) == 0) {
		GtkWidget *lockpos;
		
		lockpos = glade_xml_get_widget (xml, "local_lock_pos_checkbox");

		gtk_widget_set_sensitive (lockpos, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), lockpos);
	}
	else if (strcmp ("remote_title_bar_checkbutton", ve_sure_string (name)) == 0) {
		GtkWidget *lockpos;
		
		lockpos = glade_xml_get_widget (xml, "remote_lock_pos_checkbox");

		gtk_widget_set_sensitive (lockpos, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_positive), lockpos);
	}
	else if (strcmp ("disallow_tcp", ve_sure_string (name)) == 0) {
		GtkWidget *nfs_cookies;
		
		nfs_cookies = glade_xml_get_widget (xml, "never_cookies_NFS_checkbutton");

		gtk_widget_set_sensitive (nfs_cookies, !val);

		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), toggle);	
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled_sensitivity_negative), nfs_cookies);
		
	}
	else {
		g_signal_connect (G_OBJECT (toggle), "toggled",
		                  G_CALLBACK (toggle_toggled), NULL);
	}
}

static gboolean
commands_entry_timeout (GtkWidget *entry)
{
	GtkWidget *apply_cmd_changes;
	GtkWidget *command_combobox;
	gint selected;

	const char  *key  = g_object_get_data (G_OBJECT (entry), "key");
	const gchar *val  = gtk_entry_get_text (GTK_ENTRY (entry));
	
	apply_cmd_changes = glade_xml_get_widget (xml_commands, "command_apply_button");
	command_combobox = glade_xml_get_widget (xml_commands, "cmd_type_combobox");
	
	selected = gtk_combo_box_get_active (GTK_COMBO_BOX (command_combobox));	

	/* All hrs (Halt, Shutdown, Suspend) commands will fall into this category */
	if (strcmp ("hrs_custom_cmd", ve_sure_string (key)) == 0) {
		gchar *old_val = NULL;
		gchar *cmd_key = NULL;

		if (selected == HALT_CMD)
			cmd_key = g_strdup (GDM_KEY_HALT);			
		else if (selected == REBOOT_CMD)
			cmd_key = g_strdup (GDM_KEY_REBOOT);
		else if (selected == SUSPEND_CMD)
			cmd_key = g_strdup (GDM_KEY_SUSPEND);
		
		old_val = gdm_config_get_string (cmd_key);		
			
		if (strcmp (ve_sure_string (val), ve_sure_string (old_val)) != 0) 			
			g_hash_table_insert (GdmCommandChangesUnsaved, g_strdup (cmd_key), g_strdup (val));

		else if (g_hash_table_lookup (GdmCommandChangesUnsaved, cmd_key) != NULL)			
			g_hash_table_remove (GdmCommandChangesUnsaved, cmd_key);		
		
		g_free (old_val);
		g_free (cmd_key);
	}
	/* All the custom commands will fall into this category */
	else {
		gchar *key_string = NULL;
		gchar *old_val = NULL;
		gint i;			

		i = selected - CUSTOM_CMD;
		key_string = g_strdup_printf(_("%s%d="), ve_sure_string (key), i); 
		old_val = gdm_config_get_string (key_string);				
	
		
		if (strcmp (ve_sure_string (val), ve_sure_string (old_val)) != 0)
			g_hash_table_insert (GdmCommandChangesUnsaved, g_strdup (key_string), g_strdup (val));
		
		else if (g_hash_table_lookup (GdmCommandChangesUnsaved, key_string) != NULL)
			g_hash_table_remove (GdmCommandChangesUnsaved, key_string);
		
		g_free (old_val);
		g_free (key_string);
	}	
	
	if (g_hash_table_size (GdmCommandChangesUnsaved) == 0)
		gtk_widget_set_sensitive (apply_cmd_changes, FALSE);
	else 
		gtk_widget_set_sensitive (apply_cmd_changes, TRUE);
	
	return FALSE;
}

static void
commands_entry_changed (GtkWidget *entry)
{
	run_timeout (entry, 100, commands_entry_timeout);
}

static void
setup_commands_text_entry (const char *name,
                           const char *key)
{
	GtkWidget *entry;

	entry = glade_xml_get_widget (xml_commands, name);
	
	g_object_set_data_full (G_OBJECT (entry),
	                        "key", g_strdup (key),
	                        (GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (entry), "changed",
		          G_CALLBACK (commands_entry_changed), NULL);
}

static void
setup_commands_notify_toggle (const char *name,
			      const char *key)
{
	GtkWidget *toggle;
	
	toggle = glade_xml_get_widget (xml_commands, name);
	
	g_object_set_data_full (G_OBJECT (toggle),
	                        "key", g_strdup (key),
	                        (GDestroyNotify) g_free);
	
	g_signal_connect (G_OBJECT (toggle), "toggled",
		          G_CALLBACK (command_toggle_toggled), NULL);
}

#ifdef HAVE_LIBXDMCP
static void
setup_xdmcp_notify_toggle (const char *name,
                           const char *key)
{
	GtkWidget *toggle;
	gboolean val;

	toggle = glade_xml_get_widget (xml_xdmcp, name);

	val = gdm_config_get_bool ((gchar *)key);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	g_object_set_data_full (G_OBJECT (toggle),
	                        "key", g_strdup (key),
	                        (GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (toggle), "toggled",
		          G_CALLBACK (toggle_toggled), NULL);
}
#endif

static void
root_not_allowed (GtkWidget *combo_box)
{
	static gboolean warned = FALSE;
	const char *text = NULL;
	GtkTreeIter iter;

	if (warned)
		return;

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), 
	    &iter)) {
		gtk_tree_model_get (gtk_combo_box_get_model (
			GTK_COMBO_BOX (combo_box)), &iter,
				0, &text, -1);
		}

	if ( ! ve_string_empty (text) &&
	    strcmp (text, get_root_user ()) == 0) {
		GtkWidget *dlg = hig_dialog_new (NULL /* parent */,
						 GTK_DIALOG_MODAL /* flags */,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Autologin or timed login to the root account is not allowed."),
						 "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		warned = TRUE;
	}
}

static gint
users_string_compare_func (gconstpointer a, gconstpointer b)
{
	return strcmp(a, b);
}

/* Sets up Automatic Login Username and Timed Login User entry comboboxes
 * from the general configuration tab. */
static void
setup_user_combobox_list (const char *name, const char *key)
{
	GtkListStore *combobox_store = NULL;
	GtkWidget    *combobox_entry = glade_xml_get_widget (xml, name);
	GtkTreeIter iter;
	GList *users = NULL;
	GList *users_string = NULL;
	GList *li;
	static gboolean GDM_IS_LOCAL = FALSE;
	char *selected_user;
	gint size_of_users = 0;
	int selected = -1;
	int cnt;

	combobox_store = gtk_list_store_new (USERLIST_NUM_COLUMNS, G_TYPE_STRING);
	selected_user  = gdm_config_get_string ((gchar *)key);

	/* normally empty */
	users_string = g_list_append (users_string, g_strdup (""));

	if ( ! ve_string_empty (selected_user))
		users_string = g_list_append (users_string, g_strdup (selected_user));

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
		GDM_IS_LOCAL = FALSE;
	else
		GDM_IS_LOCAL = TRUE;

	gdm_users_init (&users, &users_string, selected_user, NULL,
	                &size_of_users, GDM_IS_LOCAL, FALSE);

	users_string = g_list_sort (users_string, users_string_compare_func);

	cnt=0;
	for (li = users_string; li != NULL; li = li->next) {
		if (strcmp (li->data, ve_sure_string (selected_user)) == 0)
			selected=cnt;
		gtk_list_store_append (combobox_store, &iter);
		gtk_list_store_set(combobox_store, &iter, USERLIST_NAME, li->data, -1);
		cnt++;
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (combobox_entry),
		GTK_TREE_MODEL (combobox_store));

	if (selected != -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combobox_entry), selected);

	g_list_foreach (users, (GFunc)g_free, NULL);
	g_list_free (users);
	g_list_foreach (users_string, (GFunc)g_free, NULL);
	g_list_free (users_string);
}

static void
setup_user_combobox (const char *name, const char *key)
{
	GtkWidget *combobox_entry = glade_xml_get_widget (xml, name);
	setup_user_combobox_list (name, key);
	g_object_set_data_full (G_OBJECT (combobox_entry), "key",
	                        g_strdup (key), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (combobox_entry), "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (G_OBJECT (combobox_entry), "changed",
	                  G_CALLBACK (root_not_allowed), NULL);
}

static void
setup_intspin (const char *name,
	       const char *key)
{
	GtkWidget *spin = glade_xml_get_widget (xml, name);
	int val = gdm_config_get_int ((gchar *)key);

	g_object_set_data_full (G_OBJECT (spin),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), val);

	g_signal_connect (G_OBJECT (spin), "value_changed",
			  G_CALLBACK (intspin_changed), NULL);
}

#ifdef HAVE_LIBXDMCP
static void
setup_xdmcp_intspin (const char *name,
	             const char *key)
{
	GtkWidget *spin;
	int val = gdm_config_get_int ((gchar *)key);

	spin = glade_xml_get_widget (xml_xdmcp, name);

	g_object_set_data_full (G_OBJECT (spin),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), val);

	g_signal_connect (G_OBJECT (spin), "value_changed",
			  G_CALLBACK (intspin_changed), NULL);
}
#endif

static GtkListStore *
setup_include_exclude (GtkWidget *treeview, const char *key)
{
	GtkListStore *face_store = gtk_list_store_new (USERLIST_NUM_COLUMNS,
		G_TYPE_STRING);
	GtkTreeIter iter;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	char **list;
	int i;

	column = gtk_tree_view_column_new ();

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_attributes (column, renderer,
		"text", USERLIST_NAME, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),
		(GTK_TREE_MODEL (face_store)));

	if ((! ve_string_empty (GdmInclude)) && 
            (strcmp (ve_sure_string (key), GDM_KEY_INCLUDE) == 0))
		list = g_strsplit (GdmInclude, ",", 0);
	else if ((! ve_string_empty (GdmExclude)) &&
                 (strcmp (ve_sure_string (key), GDM_KEY_EXCLUDE) == 0))
		list = g_strsplit (GdmExclude, ",", 0);
	else
		list = NULL;

	for (i=0; list != NULL && list[i] != NULL; i++) {
		gtk_list_store_append (face_store, &iter);
		gtk_list_store_set(face_store, &iter, USERLIST_NAME, list[i], -1);
	}
	g_strfreev (list);

	return (face_store);
}

typedef enum {
	INCLUDE,
	EXCLUDE
} FaceType;

typedef struct _FaceCommon {
	GtkWidget *apply;
	GtkWidget *include_treeview;
	GtkWidget *exclude_treeview;
	GtkListStore *include_store;
	GtkListStore *exclude_store;
	GtkTreeModel *include_model;
	GtkTreeModel *exclude_model;
	GtkWidget *include_add;
	GtkWidget *exclude_add;
	GtkWidget *include_del;
	GtkWidget *exclude_del;
	GtkWidget *to_include_button;
	GtkWidget *to_exclude_button;
	GtkWidget *allusers;
} FaceCommon;

typedef struct _FaceData {
	FaceCommon *fc;
	FaceType type;
} FaceData;

typedef struct _FaceApply {
	FaceData *exclude;
	FaceData *include;
} FaceApply;

static void
face_add (FaceData *fd)
{
	GtkWidget *user_entry;
	const char *text = NULL;
	const char *model_text;
	GtkTreeIter iter;
	gboolean valid;

	user_entry = glade_xml_get_widget (xml_add_users, "fb_addentry");
	text = gtk_entry_get_text (GTK_ENTRY (user_entry));

	if (gdm_is_user_valid (text)) {
		valid = gtk_tree_model_get_iter_first (fd->fc->include_model, &iter);
		while (valid) {
			gtk_tree_model_get (fd->fc->include_model, &iter, USERLIST_NAME,
				 &model_text, -1);
			if (strcmp (ve_sure_string (text), ve_sure_string (model_text)) == 0) {
				GtkWidget *setup_dialog;
				GtkWidget *dialog;
				gchar *str;
				
				str = g_strdup_printf (_("The \"%s\" user already exists in the include list."), text); 
				
				setup_dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");
				dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
							 GTK_DIALOG_MODAL | 
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_OK,
							 _("Cannot add user"),
							 str);
				gtk_dialog_run (GTK_DIALOG (dialog));
				gtk_widget_destroy (dialog);
				g_free (str);				
				return;
			}

			valid = gtk_tree_model_iter_next (fd->fc->include_model, &iter);
		}

		valid = gtk_tree_model_get_iter_first (fd->fc->exclude_model, &iter);
		while (valid) {
			gtk_tree_model_get (fd->fc->exclude_model, &iter, USERLIST_NAME,
				 &model_text, -1);
			if (strcmp (ve_sure_string (text), ve_sure_string (model_text)) == 0) {
				GtkWidget *setup_dialog;
				GtkWidget *dialog;
				gchar *str;
				
				str = g_strdup_printf (_("The \"%s\" user already exists in the exclude list."), text); 
				
				setup_dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");
				dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
							 GTK_DIALOG_MODAL | 
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_OK,
							 _("Cannot add user"),
							 str);
				gtk_dialog_run (GTK_DIALOG (dialog));
				gtk_widget_destroy (dialog);
				g_free (str);	
				return;
			}

			valid = gtk_tree_model_iter_next (fd->fc->exclude_model, &iter);
		}

		if (fd->type == INCLUDE) {
			/* Now the user is valid but his/hers UID might be smaller than the MinimalUID */
			gint user_uid = gdm_user_uid (text);
			if (user_uid < gdm_config_get_int (GDM_KEY_MINIMAL_UID)) {
				GtkWidget *setup_dialog;
				GtkWidget *dialog;
				gchar *str;
				
				str = g_strdup_printf (_("The \"%s\" user UID is lower than allowed MinimalUID."), text); 
				
				setup_dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");
				dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
							 GTK_DIALOG_MODAL | 
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_OK,
							 _("Cannot add user"),
							 str);
				gtk_dialog_run (GTK_DIALOG (dialog));
				gtk_widget_destroy (dialog);
				g_free (str);
				return;
			}
			gtk_list_store_append (fd->fc->include_store, &iter);
			gtk_list_store_set (fd->fc->include_store, &iter,
				USERLIST_NAME, text, -1);
		} else if (fd->type == EXCLUDE) {
			gtk_list_store_append (fd->fc->exclude_store, &iter);
			gtk_list_store_set (fd->fc->exclude_store, &iter,
				USERLIST_NAME, text, -1);
		}
		gtk_widget_set_sensitive (fd->fc->apply, TRUE);
		GdmUserChangesUnsaved = TRUE;
	} else {
		GtkWidget *setup_dialog;
		GtkWidget *dialog;
		gchar *str;
		
		str = g_strdup_printf (_("The \"%s\" user does not exist."), text); 
			
		setup_dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");
		dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Cannot add user"),
					 str);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		g_free (str);
	}
}

static void
face_del (GtkWidget *button, gpointer data)
{
	FaceData *fd = data;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	if (fd->type == INCLUDE) { 
		selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (fd->fc->include_treeview));

		if (gtk_tree_selection_get_selected (selection, &(fd->fc->include_model), &iter)) {
			gtk_list_store_remove (fd->fc->include_store, &iter);
			gtk_widget_set_sensitive (fd->fc->apply, TRUE);
			GdmUserChangesUnsaved = TRUE;
		}
	} else if (fd->type == EXCLUDE) {
		selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (fd->fc->exclude_treeview));

		if (gtk_tree_selection_get_selected (selection, &(fd->fc->exclude_model), &iter)) {
			gtk_list_store_remove (fd->fc->exclude_store, &iter);
			gtk_widget_set_sensitive (fd->fc->apply, TRUE);
			GdmUserChangesUnsaved = TRUE;
		}
	}
}

static void
browser_move (GtkWidget *button, gpointer data)
{
	FaceData *fd = data;
	GtkTreeSelection *selection = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model = NULL;
	char *text;

	/* The fd->type value passed in corresponds with the list moving to */
	if (fd->type == INCLUDE) {
		model = fd->fc->exclude_model;
		selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (fd->fc->exclude_treeview));
	} else if (fd->type == EXCLUDE) {
		model = fd->fc->include_model;
		selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (fd->fc->include_treeview));
	}

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
	        gtk_tree_model_get (model, &iter, USERLIST_NAME, &text, -1);
		if (fd->type == INCLUDE) {
			/* We cant move the users that have uid smaller that MinimalUID */
			gint user_uid = gdm_user_uid (text);
			if (user_uid < gdm_config_get_int (GDM_KEY_MINIMAL_UID)) {
				GtkWidget *setup_dialog;
				GtkWidget *dialog;
				gchar *str;
				
				str = g_strdup_printf (_("The \"%s\" user UID is lower than allowed MinimalUID."), text); 
				
				setup_dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");
				dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
							 GTK_DIALOG_MODAL | 
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_OK,
							 _("Cannot add user"),
							 str);
				gtk_dialog_run (GTK_DIALOG (dialog));
				gtk_widget_destroy (dialog);
				g_free (str);
				return;
			}
			gtk_list_store_remove (fd->fc->exclude_store, &iter);
			gtk_list_store_append (fd->fc->include_store, &iter);
			gtk_list_store_set (fd->fc->include_store, &iter,
				USERLIST_NAME, text, -1);
		} else if (fd->type == EXCLUDE) {
			gtk_list_store_remove (fd->fc->include_store, &iter);
			gtk_list_store_append (fd->fc->exclude_store, &iter);
			gtk_list_store_set (fd->fc->exclude_store, &iter,
				USERLIST_NAME, text, -1);
		}
		gtk_widget_set_sensitive (fd->fc->apply, TRUE);
		GdmUserChangesUnsaved = TRUE;
	}
}

/* Hash key happens to be equal to the config entry key,
   se we save the value under the key and clean up
*/
static gboolean
unsaved_data_from_hash_table_func (gpointer key, gpointer value, gpointer user_data)
{
	gchar *c_key = key;
        if (strncmp (c_key, GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE, 
		     strlen (GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE)) == 0 ||
	    strncmp (c_key, GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE,
		     strlen (GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE)) == 0) {
		gboolean *p_val = (gboolean*)value;
		gdm_setup_config_set_bool (c_key, *p_val);
	}
	else
		gdm_setup_config_set_string (c_key, (gchar*)value);	
	
	/* And final cleanup */
	g_free (value);
	g_free (key);

	return TRUE;
}

/* Go thru and remove each of the hash entries
   then clean the hash (if not already empty)
   just in case, then de-sensitise the apply 
   command changes button
*/
static void
command_apply (GtkWidget *button, gpointer data)
{
	const gchar *command = NULL;
	GtkWidget *cmd_path_entry = NULL;
	GtkWidget *command_combobox = (GtkWidget*)data;
	gboolean command_exists = FALSE;
	gint selected;
	
	selected = gtk_combo_box_get_active (GTK_COMBO_BOX (command_combobox));
	
	if (last_selected_command < CUSTOM_CMD)
		cmd_path_entry = glade_xml_get_widget (xml_commands, "hrs_cmd_path_entry");
	else
		cmd_path_entry = glade_xml_get_widget (xml_commands, "custom_cmd_path_entry");

	command = gtk_entry_get_text (GTK_ENTRY (cmd_path_entry));
	
	command_exists = ve_string_empty (command) || gdm_working_command_exists (command);
	
	if(command_exists)
		g_hash_table_foreach_remove (GdmCommandChangesUnsaved,
					     (GHRFunc) unsaved_data_from_hash_table_func, NULL);
	
	else {
		GtkWidget *parent = glade_xml_get_widget (xml_commands, "commands_dialog");

		GtkWidget *dialog = hig_dialog_new (GTK_WINDOW (parent),
						    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						    GTK_MESSAGE_ERROR,
						    GTK_BUTTONS_OK,
						    _("Invalid command path"),
						    _("The path you provided for this "
						      "command is not valid. The changes "
						      "will not be saved."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
	
	/* Just to make sure */
	if (g_hash_table_size (GdmCommandChangesUnsaved) != 0)
		g_hash_table_remove_all (GdmCommandChangesUnsaved);

	gtk_widget_set_sensitive (button, FALSE);
	
	if (selected == last_selected_command)
		g_signal_emit_by_name (G_OBJECT (command_combobox), "changed");
	
	if(command_exists)
		update_greeters ();
}

static void
browser_apply (GtkWidget *button, gpointer data)
{
	FaceCommon *fc = data;
	GString *userlist = g_string_new (NULL);
	const char *model_text;
	char *val;
	GtkTreeIter iter;
	gboolean valid;
	gboolean update_greet = FALSE;
	char *sep = "";
	gint minimalUID = -1;
	gboolean any_removed = FALSE;
	
	minimalUID = gdm_config_get_int (GDM_KEY_MINIMAL_UID);
	
	valid = gtk_tree_model_get_iter_first (fc->include_model, &iter);
	while (valid) {
		gtk_tree_model_get (fc->include_model, &iter, USERLIST_NAME,
			 &model_text, -1);

		/* We need to take check that during the time between adding
		   a user and clicking on the apply button UID has not changed 
		   If so then the offending users should be removed
		*/
		if (gdm_user_uid (model_text) < minimalUID) {
			valid = gtk_list_store_remove (fc->include_store, &iter);
			any_removed = TRUE;
		}
		else {
			g_string_append (userlist, sep);
			sep = ",";
			g_string_append (userlist, model_text);		
			valid = gtk_tree_model_iter_next (fc->include_model, &iter);
		}
	}

	if (any_removed) {
		GtkWidget *setup_dialog;
		GtkWidget *dlg;
		
		setup_dialog = glade_xml_get_widget(xml, "setup_dialog");
		
		//Inform user about the change
		dlg = hig_dialog_new (GTK_WINDOW (setup_dialog),
				      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				      GTK_MESSAGE_WARNING,
				      GTK_BUTTONS_OK,
				      _("Users include list modification"),
				      _("Some of the users had uid lower than "
					"MinimalUID (Security tab) and "
					"could not be added."));
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);						
		
	}

	val = gdm_config_get_string (GDM_KEY_INCLUDE);

	if (strcmp (ve_sure_string (val),
		    ve_sure_string (userlist->str)) != 0) {
		gdm_setup_config_set_string (GDM_KEY_INCLUDE, userlist->str);
		update_greet = TRUE;
	}

	g_string_free (userlist, TRUE);

	userlist = g_string_new (NULL);
	sep = "";
	valid = gtk_tree_model_get_iter_first (fc->exclude_model, &iter);
	while (valid) {
		gtk_tree_model_get (fc->exclude_model, &iter, USERLIST_NAME,
			 &model_text, -1);

		g_string_append (userlist, sep);
		sep = ",";
		g_string_append (userlist, model_text);

		valid = gtk_tree_model_iter_next (fc->exclude_model, &iter);
	}

	val = gdm_config_get_string (GDM_KEY_EXCLUDE);

	if (strcmp (ve_sure_string (val),
		    ve_sure_string (userlist->str)) != 0) {
		gdm_setup_config_set_string (GDM_KEY_EXCLUDE, userlist->str);
		update_greet = TRUE;
	}

	if (update_greet)
		update_greeters ();

	/* Re-initialize combox with updated userlist. */
	GdmInclude = gdm_config_get_string (GDM_KEY_INCLUDE);
	GdmExclude = gdm_config_get_string (GDM_KEY_EXCLUDE);
	setup_user_combobox_list ("autologin_combo",
			  GDM_KEY_AUTOMATIC_LOGIN);
	setup_user_combobox_list ("timedlogin_combo",
			  GDM_KEY_TIMED_LOGIN);
	gtk_widget_set_sensitive (button, FALSE);

	GdmUserChangesUnsaved = FALSE;
	g_string_free (userlist, TRUE);
}


static void
face_rowdel (GtkTreeModel *treemodel, GtkTreePath *arg1, gpointer data)
{
	FaceCommon *fc = data;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (
		GTK_TREE_VIEW (fc->include_treeview));
	if (gtk_tree_selection_get_selected (selection, &(fc->include_model), &iter)) {
		gtk_widget_set_sensitive (fc->to_exclude_button, TRUE);
		gtk_widget_set_sensitive (fc->include_del, TRUE);
	} else {
		gtk_widget_set_sensitive (fc->to_exclude_button, FALSE);
		gtk_widget_set_sensitive (fc->include_del, FALSE);
	}

	selection = gtk_tree_view_get_selection (
		GTK_TREE_VIEW (fc->exclude_treeview));
	if (gtk_tree_selection_get_selected (selection, &(fc->exclude_model), &iter)) {
		gtk_widget_set_sensitive (fc->to_include_button, TRUE);
		gtk_widget_set_sensitive (fc->exclude_del, TRUE);
	} else {
		gtk_widget_set_sensitive (fc->to_include_button, FALSE);
		gtk_widget_set_sensitive (fc->exclude_del, FALSE);
	}
}

static void
face_selection_changed (GtkTreeSelection *selection, gpointer data)
{
	FaceData *fd = data;
	GtkTreeIter iter;

	if (fd->type == INCLUDE) {
		if (gtk_tree_selection_get_selected (selection, &(fd->fc->include_model), &iter)) {
			gtk_widget_set_sensitive (fd->fc->to_exclude_button, TRUE);
			gtk_widget_set_sensitive (fd->fc->include_del, TRUE);
		} else {
			gtk_widget_set_sensitive (fd->fc->to_exclude_button, FALSE);
			gtk_widget_set_sensitive (fd->fc->include_del, FALSE);
		}
	} else if (fd->type == EXCLUDE) {
		if (gtk_tree_selection_get_selected (selection, &(fd->fc->exclude_model), &iter)) {
			gtk_widget_set_sensitive (fd->fc->to_include_button, TRUE);
			gtk_widget_set_sensitive (fd->fc->exclude_del, TRUE);
		} else {
			gtk_widget_set_sensitive (fd->fc->to_include_button, FALSE);
			gtk_widget_set_sensitive (fd->fc->exclude_del, FALSE);
		}
	}
}

static void
users_add_button_clicked (GtkWidget *button, gpointer data)
{
	static GtkWidget *dialog = NULL;
	FaceData *fd = data;
	GtkWidget *user_entry;
	GtkWidget *parent;
	
	if (dialog == NULL) {
		parent = glade_xml_get_widget (xml, "setup_dialog");
		dialog = glade_xml_get_widget (xml_add_users, "add_user_dialog");

		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	}

	user_entry = glade_xml_get_widget (xml_add_users, "fb_addentry");

	gtk_widget_grab_focus (user_entry);
	
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		face_add (fd);
	}
	gtk_widget_hide (dialog);
}

static void
setup_face (void)
{
	static FaceCommon fc;
	static FaceData fd_include;
	static FaceData fd_exclude;
	static FaceApply face_apply;

	GtkTreeSelection *selection;

	fc.include_add       = glade_xml_get_widget (xml, "fb_includeadd");
	fc.include_del       = glade_xml_get_widget (xml, "fb_includedelete");
	fc.exclude_add       = glade_xml_get_widget (xml, "fb_excludeadd");
	fc.exclude_del       = glade_xml_get_widget (xml, "fb_excludedelete");
	fc.to_include_button = glade_xml_get_widget (xml, "fb_toinclude");
	fc.to_exclude_button = glade_xml_get_widget (xml, "fb_toexclude");
	fc.apply             = glade_xml_get_widget (xml, "fb_faceapply");
	fc.include_treeview  = glade_xml_get_widget (xml, "fb_include_treeview");
	fc.exclude_treeview  = glade_xml_get_widget (xml, "fb_exclude_treeview");
	fc.allusers          = glade_xml_get_widget (xml, "fb_allusers");

	fc.include_store = setup_include_exclude (fc.include_treeview,
	                                          GDM_KEY_INCLUDE);
	fc.exclude_store = setup_include_exclude (fc.exclude_treeview,
	                                          GDM_KEY_EXCLUDE);

	fc.include_model = gtk_tree_view_get_model (
	                   GTK_TREE_VIEW (fc.include_treeview));
	fc.exclude_model = gtk_tree_view_get_model (
	                   GTK_TREE_VIEW (fc.exclude_treeview));

	fd_include.fc = &fc;
	fd_include.type = INCLUDE;

	fd_exclude.fc = &fc;
	fd_exclude.type = EXCLUDE;

	gtk_widget_set_sensitive (fc.include_del, FALSE);
	gtk_widget_set_sensitive (fc.exclude_del, FALSE);
	gtk_widget_set_sensitive (fc.to_include_button, FALSE);
	gtk_widget_set_sensitive (fc.to_exclude_button, FALSE);
	gtk_widget_set_sensitive (fc.apply, FALSE);

	face_apply.include = &fd_include;
	face_apply.exclude = &fd_exclude;

	xml_add_users = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "add_user_dialog", NULL);

	g_signal_connect (G_OBJECT (fc.include_add), "clicked",
	                  G_CALLBACK (users_add_button_clicked), &fd_include);
	g_signal_connect (fc.exclude_add, "clicked",
	                  G_CALLBACK (users_add_button_clicked), &fd_exclude);
	g_signal_connect (fc.include_del, "clicked",
	                  G_CALLBACK (face_del), &fd_include);
	g_signal_connect (fc.exclude_del, "clicked",
	                  G_CALLBACK (face_del), &fd_exclude);

	g_signal_connect (fc.include_model, "row-deleted",
	                  G_CALLBACK (face_rowdel), &fc);
	g_signal_connect (fc.exclude_model, "row-deleted",
	                  G_CALLBACK (face_rowdel), &fc);

	selection = gtk_tree_view_get_selection (
	            GTK_TREE_VIEW (fc.include_treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
	                  G_CALLBACK (face_selection_changed), &fd_include);
	selection = gtk_tree_view_get_selection (
	            GTK_TREE_VIEW (fc.exclude_treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
	                  G_CALLBACK (face_selection_changed), &fd_exclude);

	g_signal_connect (fc.to_include_button, "clicked",
	                  G_CALLBACK (browser_move), &fd_include);
	g_signal_connect (fc.to_exclude_button, "clicked",
	                  G_CALLBACK (browser_move), &fd_exclude);

	g_signal_connect (fc.apply, "clicked",
	                  G_CALLBACK (browser_apply), &fc);
}

static void
include_all_toggle (GtkWidget *toggle)
{
	if (GTK_TOGGLE_BUTTON (toggle)->active)
		GdmIncludeAll = TRUE;
	else
		GdmIncludeAll = FALSE;

	setup_user_combobox_list ("autologin_combo",
			  GDM_KEY_AUTOMATIC_LOGIN);
	setup_user_combobox_list ("timedlogin_combo",
			  GDM_KEY_TIMED_LOGIN);
}

static gboolean
greeter_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	gboolean val = gdm_config_get_bool ((gchar *)key);

	if ( ! bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
	
		if (strcmp (ve_sure_string (key), GDM_KEY_BACKGROUND_SCALE_TO_FIT) == 0) {
	
			if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {

				GtkWidget *checkbutton;
		
				checkbutton = glade_xml_get_widget (xml, "sg_scale_background_remote");

				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton),
				                              GTK_TOGGLE_BUTTON (toggle)->active);
			}
			else {
				GtkWidget *checkbutton;
			
				checkbutton = glade_xml_get_widget (xml, "sg_scale_background");

				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton),
				                              GTK_TOGGLE_BUTTON (toggle)->active);	
			}
		}
		gdm_setup_config_set_bool (key, GTK_TOGGLE_BUTTON (toggle)->active);
		update_greeters ();

		if (strcmp (ve_sure_string (key), GDM_KEY_INCLUDE_ALL) == 0) {
			include_all_toggle (toggle);
		}
	}

	return FALSE;
}

static void
greeter_toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 500, greeter_toggle_timeout);
}

static void
sensitive_entry_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *widget = data;
	gboolean val;

	val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));

	if (val == FALSE) {
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	run_timeout (toggle, 500, greeter_toggle_timeout);
}

static gboolean
local_background_type_toggle_timeout (GtkWidget *toggle)
{
	GtkWidget *color_radiobutton;
	GtkWidget *image_radiobutton;
	GtkWidget *color_remote_radiobutton;
	GtkWidget *image_remote_radiobutton;
	gboolean image_value;
	gboolean color_value;

	image_radiobutton = glade_xml_get_widget (xml, "local_background_image_checkbutton");
	color_radiobutton = glade_xml_get_widget (xml, "local_background_color_checkbutton");
	image_remote_radiobutton = glade_xml_get_widget (xml, "remote_background_image_checkbutton");
	color_remote_radiobutton = glade_xml_get_widget (xml, "remote_background_color_checkbutton");

	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {
		image_value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (image_radiobutton));
		color_value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (color_radiobutton));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_remote_radiobutton), image_value);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_remote_radiobutton), color_value);
	}
	else { 
		image_value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (image_remote_radiobutton));
		color_value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (color_remote_radiobutton));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), image_value);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), color_value);
	}
		
	if (image_value == TRUE && color_value == TRUE) {		
		/* Image & color */
                gdm_setup_config_set_int (GDM_KEY_BACKGROUND_TYPE, 1);
	}
	else if (image_value == FALSE && color_value == TRUE) {
		/* Color only */
		gdm_setup_config_set_int (GDM_KEY_BACKGROUND_TYPE, 2);
	}
	else if (image_value == TRUE && color_value == FALSE) {
		/* Image only*/
		gdm_setup_config_set_int (GDM_KEY_BACKGROUND_TYPE, 3);
	}
	else {
		/* No Background */
		gdm_setup_config_set_int (GDM_KEY_BACKGROUND_TYPE, 0);
	}
		
	update_greeters ();
	return FALSE;
}

static void
local_background_type_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 200, local_background_type_toggle_timeout);
}

static void
setup_greeter_toggle (const char *name,
		      const char *key)
{
	GtkWidget *toggle = glade_xml_get_widget (xml, name);
	gboolean val = gdm_config_get_bool ((gchar *)key);

	g_object_set_data_full (G_OBJECT (toggle), "key", g_strdup (key),
		(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	if (strcmp ("sg_defaultwelcome", ve_sure_string (name)) == 0) {
		GtkWidget *welcome = glade_xml_get_widget (xml, "welcome");
		GtkWidget *custom = glade_xml_get_widget (xml, "sg_customwelcome");

		gtk_widget_set_sensitive (welcome, !val);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom), !val);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), welcome);

	} else if (strcmp ("sg_defaultwelcomeremote", ve_sure_string (name)) == 0) {
		GtkWidget *welcomeremote = glade_xml_get_widget (xml, "welcomeremote");
		GtkWidget *customremote = glade_xml_get_widget (xml, "sg_customwelcomeremote");

		gtk_widget_set_sensitive (welcomeremote, !val);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (customremote), !val);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), welcomeremote);
	
	} else if (strcmp ("fb_allusers", ve_sure_string (name)) == 0) {

		GtkWidget *fb_includetree = glade_xml_get_widget (xml, "fb_include_treeview");
		GtkWidget *fb_buttonbox = glade_xml_get_widget (xml, "UsersButtonBox");
		GtkWidget *fb_includeadd = glade_xml_get_widget (xml, "fb_includeadd");
		GtkWidget *fb_includeremove = glade_xml_get_widget (xml, "fb_includedelete");
		GtkWidget *fb_includelabel = glade_xml_get_widget (xml, "fb_includelabel");

		gtk_widget_set_sensitive (fb_buttonbox, !val);
		gtk_widget_set_sensitive (fb_includetree, !val);
		gtk_widget_set_sensitive (fb_includelabel, !val);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_buttonbox);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_includetree);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_includeadd);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (list_selection_toggled), fb_includeremove);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_includelabel);
	}
	else if (strcmp ("acc_sound_ready", ve_sure_string (name)) == 0) {
	
		GtkWidget *file_chooser;
		GtkWidget *play_button;
		
		file_chooser = glade_xml_get_widget (xml, "acc_sound_ready_button");
		play_button = glade_xml_get_widget (xml, "acc_soundtest_ready_button");

		gtk_widget_set_sensitive (file_chooser, val);
		gtk_widget_set_sensitive (play_button, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), file_chooser);		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), play_button);
	}
	else if (strcmp ("acc_sound_success", ve_sure_string (name)) == 0) {
	
		GtkWidget *file_chooser;
		GtkWidget *play_button;
		
		file_chooser = glade_xml_get_widget (xml, "acc_sound_success_button");
		play_button = glade_xml_get_widget (xml, "acc_soundtest_success_button");

		gtk_widget_set_sensitive (file_chooser, val);
		gtk_widget_set_sensitive (play_button, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), file_chooser);		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), play_button);
	}
	else if (strcmp ("acc_sound_failure", ve_sure_string (name)) == 0) {
	
		GtkWidget *file_chooser;
		GtkWidget *play_button;
		
		file_chooser = glade_xml_get_widget (xml, "acc_sound_failure_button");
		play_button = glade_xml_get_widget (xml, "acc_soundtest_failure_button");

		gtk_widget_set_sensitive (file_chooser, val);
		gtk_widget_set_sensitive (play_button, val);
		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), file_chooser);		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), play_button);
	}
	else if (strcmp ("local_logo_image_checkbutton", ve_sure_string (name)) == 0) {
	
		GtkWidget *file_chooser;
		GtkWidget *file_chooser_remote;
		GtkWidget *checkbutton;
		
		checkbutton = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");

		file_chooser = glade_xml_get_widget (xml, "local_logo_image_chooserbutton");

		file_chooser_remote = glade_xml_get_widget (xml, "remote_logo_image_chooserbutton");

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton), val);
		gtk_widget_set_sensitive (file_chooser, val);

		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), file_chooser);		
		g_signal_connect (G_OBJECT (toggle), "toggled",	G_CALLBACK (toggle_toggled_sensitivity_positive), file_chooser_remote);		
		g_signal_connect (G_OBJECT (toggle), "toggled", G_CALLBACK (logo_toggle_toggled), NULL);
	}	
	g_signal_connect (G_OBJECT (toggle), "toggled",
		G_CALLBACK (greeter_toggle_toggled), NULL);
}

static gboolean
greeter_color_timeout (GtkWidget *picker)
{
	const char *key = g_object_get_data (G_OBJECT (picker), "key");
	GdkColor color_val;
	char *val, *color;

	gtk_color_button_get_color (GTK_COLOR_BUTTON (picker), &color_val);
	
	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {

		GtkWidget *colorbutton;
		
		if (strcmp (GDM_KEY_GRAPHICAL_THEMED_COLOR, ve_sure_string (key)) == 0) {
			colorbutton = glade_xml_get_widget (xml, "remote_background_theme_colorbutton");
		} else {
			colorbutton = glade_xml_get_widget (xml, "remote_background_colorbutton");
		}
		gtk_color_button_set_color (GTK_COLOR_BUTTON (colorbutton), &color_val);
	}
	else {
		GtkWidget *colorbutton;

		if (strcmp (GDM_KEY_GRAPHICAL_THEMED_COLOR, ve_sure_string (key)) == 0) {
			colorbutton = glade_xml_get_widget (xml, "local_background_theme_colorbutton");
		} else {
			colorbutton = glade_xml_get_widget (xml, "local_background_colorbutton");
		}
		gtk_color_button_set_color (GTK_COLOR_BUTTON (colorbutton), &color_val);
	}
	
	color = g_strdup_printf ("#%02x%02x%02x",
	                         (guint16)color_val.red / 256, 
	                         (guint16)color_val.green / 256, 
	                         (guint16)color_val.blue / 256);

	val = gdm_config_get_string ((gchar *)key);

	if (strcmp (ve_sure_string (val), ve_sure_string (color)) != 0) {
		gdm_setup_config_set_string (key, ve_sure_string (color));
		update_greeters ();
	}

	g_free (color);

	return FALSE;
}

static void
greeter_color_changed (GtkWidget *picker,
		       guint r, guint g, guint b, guint a)
{
	run_timeout (picker, 500, greeter_color_timeout);
}

static void
setup_greeter_color (const char *name,
		     const char *key)
{
	GtkWidget *picker = glade_xml_get_widget (xml, name);
	char *val = gdm_config_get_string ((gchar *)key);

	g_object_set_data_full (G_OBJECT (picker),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

        if (val != NULL) {
		GdkColor color;

		if (gdk_color_parse (val, &color)) {
			gtk_color_button_set_color (GTK_COLOR_BUTTON (picker), &color);
		}
	}

	g_signal_connect (G_OBJECT (picker), "color_set",
			  G_CALLBACK (greeter_color_changed), NULL);
}

typedef enum {
	BACKIMAGE,
	LOGO
} ImageType;

typedef struct _ImageData {
	GtkWidget *image;
	gchar *filename;
	gchar *key;
} ImageData;

/*
 * Do we really want to throw away the user's translations just because they
 * changed the non-translated value?
 */
static gboolean
greeter_entry_untranslate_timeout (GtkWidget *entry)
{
	GKeyFile   *custom_cfg;
	const char *key;
	const char *text;
	char       *config_group;
	char       *config_key;
	int         i;
	char       *prefix;
	char      **keys;

	custom_cfg = gdm_common_config_load (custom_config_file, NULL);
        key = g_object_get_data (G_OBJECT (entry), "key");

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	config_group = config_key = NULL;
	if (! gdm_common_config_parse_key_string (key, &config_group, &config_key, NULL)) {
		goto out;
	}

	prefix = g_strdup_printf ("%s[", config_key);
	keys = g_key_file_get_keys (custom_cfg, config_group, NULL, NULL);
	for (i = 0; keys[i] != NULL; i++) {
		if (g_str_has_prefix (keys[i], prefix)) {
			g_key_file_remove_key (custom_cfg, config_group, keys[i], NULL);
		}
	}
	g_strfreev (keys);

	gdm_setup_config_set_string (key, (char *)ve_sure_string (text));
	update_greeters ();

	gdm_common_config_save (custom_cfg, custom_config_file, NULL);

 out:
	g_key_file_free (custom_cfg);

	return FALSE;
}


static void
greeter_entry_untranslate_changed (GtkWidget *entry)
{
	run_timeout (entry, 500, greeter_entry_untranslate_timeout);
}

static void
command_response (GtkWidget *button, gpointer data)
{

	GtkWidget *chooser = NULL;
	GtkWidget *setup_dialog;
	gint response;
	gchar *filename;
	
	const gchar *key;
	gchar *value;
	GtkWidget *command_combobox;
	GtkWidget *command_entry = NULL;
	gint selected;
	
	setup_dialog = glade_xml_get_widget (xml_commands, "commands_dialog");
	
	/* first get the file */
	chooser = gtk_file_chooser_dialog_new (_("Select Command"),
					       GTK_WINDOW (setup_dialog),
					       GTK_FILE_CHOOSER_ACTION_OPEN,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_OK, GTK_RESPONSE_OK,
					       NULL);	

	response = gtk_dialog_run (GTK_DIALOG (chooser));
	
	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (chooser);
		return;
	}

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
	gtk_widget_destroy (chooser);

	if (filename == NULL) {
		
		GtkWidget *dialog;
		
		dialog = hig_dialog_new (GTK_WINDOW (setup_dialog),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("No file selected"),
					 "");
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		return;
	}

	key = g_object_get_data (G_OBJECT (button), "key");	
	
	/* Then according to the selected command
	   append the chosen filepath onto the existing
	   string */
	
	command_combobox = glade_xml_get_widget (xml_commands, "cmd_type_combobox");

	selected = gtk_combo_box_get_active (GTK_COMBO_BOX (command_combobox));
	
	if (selected == HALT_CMD)
		value = gdm_config_get_string (GDM_KEY_HALT);			
	else if (selected == REBOOT_CMD)
		value = gdm_config_get_string (GDM_KEY_REBOOT);		
	else if (selected == SUSPEND_CMD) 
		value = gdm_config_get_string (GDM_KEY_SUSPEND);
	else {
		gchar *key_string;
		gint i;

		i = selected - CUSTOM_CMD;
		
		key_string = g_strdup_printf(_("%s%d="), GDM_KEY_CUSTOM_CMD_TEMPLATE, i); 
		value = gdm_config_get_string (key_string);		
		g_free (key_string);
	}
	
	value = strings_list_add (value, filename, ";");
	
	if (strcmp (ve_sure_string (key), "add_hrs_cmd_button") == 0)
		command_entry = glade_xml_get_widget (xml_commands, "hrs_cmd_path_entry");
	else if (strcmp (ve_sure_string (key), "add_custom_cmd_button") == 0)
		command_entry = glade_xml_get_widget (xml_commands, "custom_cmd_path_entry");

	gtk_entry_set_text (GTK_ENTRY (command_entry), ve_sure_string (value));

	g_free (value);
	g_free (filename);
}

static void
default_filechooser_response (GtkWidget *file_chooser, gpointer data)
{
	gchar *filename;
	gchar *key;
	gchar *value;
		
	filename  = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
	key = g_object_get_data (G_OBJECT (file_chooser), "key");	
	value     = gdm_config_get_string (key);
	
	if (strcmp (ve_sure_string (key), GDM_KEY_GLOBAL_FACE_DIR) == 0) {
		/* we need to append trailing / so it matches the default
		   config  values. This is not really necessary but makes
		   things neater */
		gchar *corr_filename;
		
		corr_filename = g_strdup_printf("%s/", ve_sure_string (filename));
		
		if (strcmp (ve_sure_string (value), corr_filename) != 0)
			gdm_setup_config_set_string (key, corr_filename);	
		
		g_free (corr_filename);
	}
	else {
		/* All other cases */
		if (strcmp (ve_sure_string (value), ve_sure_string (filename)) != 0)
			gdm_setup_config_set_string (key, ve_sure_string (filename));		
	}
	
	g_free (filename);
	g_free (value);
}

static void
setup_general_command_buttons (const char *name,
			       const char *key)
{
	GtkWidget *button = glade_xml_get_widget (xml_commands, name);
	
	g_object_set_data_full (G_OBJECT (button),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (button), "clicked",
			  G_CALLBACK (command_response),
			  NULL);
}

static void
setup_greeter_untranslate_entry (const char *name,
				 const char *key)
{
	GtkWidget *entry = glade_xml_get_widget (xml, name);
	char *val;

	val = gdm_config_get_translated_string ((gchar *)key);

	g_object_set_data_full (G_OBJECT (entry),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_entry_set_text (GTK_ENTRY (entry), ve_sure_string (val));

	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (greeter_entry_untranslate_changed),
			  NULL);

	g_free (val);
}

#ifdef HAVE_LIBXDMCP
static void
xdmcp_button_clicked (void)
{
	static GtkWidget *dialog = NULL;

	if (dialog == NULL) {

		GtkWidget *parent;

		xml_xdmcp = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "xdmcp_dialog", NULL);

		parent = glade_xml_get_widget (xml, "setup_dialog");
		dialog = glade_xml_get_widget (xml_xdmcp, "xdmcp_dialog");

		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

		setup_xdmcp_notify_toggle ("honour_indirect", GDM_KEY_INDIRECT);
		setup_xdmcp_intspin ("udpport", GDM_KEY_UDP_PORT);
		setup_xdmcp_intspin ("maxpending", GDM_KEY_MAX_PENDING);
		setup_xdmcp_intspin ("maxpendingindirect", GDM_KEY_MAX_INDIRECT);
		setup_xdmcp_intspin ("maxremotesessions", GDM_KEY_MAX_SESSIONS);
		setup_xdmcp_intspin ("maxwait", GDM_KEY_MAX_WAIT);
		setup_xdmcp_intspin ("maxwaitindirect", GDM_KEY_MAX_WAIT_INDIRECT);
		setup_xdmcp_intspin ("displaysperhost", GDM_KEY_DISPLAYS_PER_HOST);
		setup_xdmcp_intspin ("pinginterval", GDM_KEY_PING_INTERVAL);
	}
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_hide (dialog);
}
#endif

static void
apply_command_changes (GObject *object, gint response, gpointer command_data)
{
	GtkWidget *dialog = command_data;
	
	if (g_hash_table_size (GdmCommandChangesUnsaved) != 0 && 
	    response != GTK_RESPONSE_HELP) {

		GtkWidget *prompt;
		gint response;

		prompt = hig_dialog_new (GTK_WINDOW (dialog),
					 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Apply the changes to commands before closing?"),
					 _("If you don't apply, the changes made "
					   "will be disregarded."));
		
		gtk_dialog_add_button (GTK_DIALOG (prompt), _("Close _without Applying"), GTK_RESPONSE_CLOSE);
		gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-apply", GTK_RESPONSE_APPLY);
		
		response = gtk_dialog_run (GTK_DIALOG (prompt));
		gtk_widget_destroy (prompt);
		
		if (response == GTK_RESPONSE_APPLY) {
			GtkWidget *apply_button;

			apply_button = glade_xml_get_widget (xml_commands, "command_apply_button");
			g_signal_emit_by_name (G_OBJECT (apply_button), "clicked");
		}
		else {
			/* Just to make sure */
			if (g_hash_table_size (GdmCommandChangesUnsaved) != 0)
				g_hash_table_remove_all (GdmCommandChangesUnsaved);
		}
	}
}

/* Make label surrounded by tag (if tag = "tag" add <tag>text</tag>) */
static void
glade_helper_tagify_label (GladeXML *xml,
			   const char *name,
			   const char *tag)
{
	const char *lbl;
	char *s;
	GtkWidget *label;

	label = glade_xml_get_widget (xml, name);
	if (GTK_IS_BUTTON (label)) {
		label = GTK_BIN(label)->child;
	}

	lbl = gtk_label_get_label (GTK_LABEL (label));
	s = g_strdup_printf ("<%s>%s</%s>", tag, lbl, tag);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_label (GTK_LABEL (label), s);
	g_free (s);
}

static void
command_button_clicked (void)
{
	static GtkWidget *dialog = NULL;
	GtkWidget *command_chooser = NULL;
	gint selected = -1;
	gint response;

	if (dialog == NULL) {

		GtkWidget *parent;
		GtkWidget *apply_command_changes_button;
		gint i;

		xml_commands = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "commands_dialog", NULL);

		command_chooser = glade_xml_get_widget (xml_commands, "cmd_type_combobox");

		glade_helper_tagify_label (xml_commands, "custom_cmd_note_label", "i");
		glade_helper_tagify_label (xml_commands, "custom_cmd_note_label", "small");

		glade_helper_tagify_label (xml_commands, "hrs_path_label", "i");
		glade_helper_tagify_label (xml_commands, "hrs_path_label", "small");
		glade_helper_tagify_label (xml_commands, "custom_path_label", "i");
		glade_helper_tagify_label (xml_commands, "custom_path_label", "small");
		glade_helper_tagify_label (xml_commands, "label_label", "i");
		glade_helper_tagify_label (xml_commands, "label_label", "small");
		glade_helper_tagify_label (xml_commands, "lrlabel_label", "i");
		glade_helper_tagify_label (xml_commands, "lrlabel_label", "small");		
		glade_helper_tagify_label (xml_commands, "text_label", "i");
		glade_helper_tagify_label (xml_commands, "text_label", "small");
		glade_helper_tagify_label (xml_commands, "tooltip_label", "i");
		glade_helper_tagify_label (xml_commands, "tooltip_label", "small");
		glade_helper_tagify_label (xml_commands, "persistent_label", "i");
		glade_helper_tagify_label (xml_commands, "persistent_label", "small");
		glade_helper_tagify_label (xml_commands, "norestart_label", "i");
		glade_helper_tagify_label (xml_commands, "norestart_label", "small");
	
		parent = glade_xml_get_widget (xml, "setup_dialog");
		dialog = glade_xml_get_widget (xml_commands, "commands_dialog");
		
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

		/* Set up unsaved changes storage container */
		GdmCommandChangesUnsaved = g_hash_table_new (g_str_hash, g_str_equal);
				
		
		/* Add halt, reboot and suspend commands */
		gtk_combo_box_append_text (GTK_COMBO_BOX (command_chooser), _("Halt command"));
		gtk_combo_box_append_text (GTK_COMBO_BOX (command_chooser), _("Reboot command"));
		gtk_combo_box_append_text (GTK_COMBO_BOX (command_chooser), _("Suspend command"));
		
		/* Add all the custom commands */
		for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
			gchar *label = g_strdup_printf("Custom command %d", i);
			gtk_combo_box_append_text (GTK_COMBO_BOX (command_chooser), label);
			g_free (label);
		}
		
		g_object_set_data_full (G_OBJECT (command_chooser), "key",
					_("command_chooser_combobox"), (GDestroyNotify) g_free);
		g_signal_connect (G_OBJECT (command_chooser), "changed",
				  G_CALLBACK (combobox_changed), NULL);			
		
		/* Lets setup handlers for all the entries 
		   They will be assigned exactly the same key and handler
		   as their only functionality would be to notify about changes */
		
		setup_commands_text_entry ("hrs_cmd_path_entry", "hrs_custom_cmd");
		setup_commands_text_entry ("custom_cmd_path_entry", GDM_KEY_CUSTOM_CMD_TEMPLATE);
		setup_commands_text_entry ("custom_cmd_label_entry", GDM_KEY_CUSTOM_CMD_LABEL_TEMPLATE);
		setup_commands_text_entry ("custom_cmd_lrlabel_entry", GDM_KEY_CUSTOM_CMD_LR_LABEL_TEMPLATE);
		setup_commands_text_entry ("custom_cmd_text_entry", GDM_KEY_CUSTOM_CMD_TEXT_TEMPLATE);
		setup_commands_text_entry ("custom_cmd_tooltip_entry", GDM_KEY_CUSTOM_CMD_TOOLTIP_TEMPLATE);
		
		setup_commands_notify_toggle ("custom_cmd_persistent_checkbutton", GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE);
		setup_commands_notify_toggle ("custom_cmd_norestart_checkbutton", GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE);	
		
		/* Set up append command buttons */
		setup_general_command_buttons("hrs_command_add", "add_hrs_cmd_button");
		setup_general_command_buttons("custom_command_add", "add_custom_cmd_button");
		
		/* set up apply command changes button */
		apply_command_changes_button = glade_xml_get_widget (xml_commands, "command_apply_button");
		g_object_set_data_full (G_OBJECT (apply_command_changes_button), "key",
					g_strdup ("apply_command_changes"), (GDestroyNotify) g_free);
		g_signal_connect (G_OBJECT (apply_command_changes_button), "clicked",
				  G_CALLBACK (command_apply), command_chooser);
	
		gtk_widget_set_sensitive (apply_command_changes_button, FALSE);
		
						
		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (apply_command_changes), dialog);
	}
	else {
		command_chooser = glade_xml_get_widget (xml_commands, "cmd_type_combobox");

		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (command_chooser));
		
	}

	/* Finally lets set our default choice */
	gtk_combo_box_set_active (GTK_COMBO_BOX (command_chooser), HALT_CMD); 
	if (selected == last_selected_command)
		g_signal_emit_by_name (G_OBJECT (command_chooser), "changed");

	do {
		response = gtk_dialog_run (GTK_DIALOG (dialog));
		if (response == GTK_RESPONSE_HELP) {
			g_spawn_command_line_sync ("gnome-open ghelp:gdm", NULL, NULL,
						   NULL, NULL);
		}
	} while (response != GTK_RESPONSE_CLOSE &&
                 response != GTK_RESPONSE_DELETE_EVENT);
	
	gtk_widget_hide (dialog);
}

static void
vt_spinbutton_activate (GtkWidget * widget,
                        gpointer data)
{
	GtkDialog * dialog = data;
	gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static void
setup_greeter_combobox (const char *name,
                        const char *key)
{
	GtkWidget *combobox = glade_xml_get_widget (xml, name);
	char *greetval      = g_strdup (gdm_config_get_string ((gchar *)key));

	if (greetval != NULL &&
	    strcmp (ve_sure_string (greetval),
	    LIBEXECDIR "/gdmlogin --disable-sound --disable-crash-dialog") == 0) {
		g_free (greetval);
		greetval = g_strdup (LIBEXECDIR "/gdmlogin");
	}

	/* Set initial state of local style combo box. */
	if (strcmp (ve_sure_string (key), GDM_KEY_GREETER) == 0) {

		if (strstr (greetval, "/gdmlogin") != NULL) {
	
			GtkWidget *local_plain_vbox;
			GtkWidget *local_themed_vbox;
			gboolean val;
			
			val = gdm_config_get_bool (GDM_KEY_BROWSER);

			local_plain_vbox = glade_xml_get_widget (xml, "local_plain_properties_vbox");
			local_themed_vbox = glade_xml_get_widget (xml, "local_themed_properties_vbox");
			if (val == FALSE) {
				gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), LOCAL_PLAIN);
			}
			else {
				gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), LOCAL_PLAIN_WITH_FACE);	
			}

			gtk_widget_show (local_plain_vbox);
			gtk_widget_hide (local_themed_vbox);
		}
		else if (strstr (greetval, "/gdmgreeter") != NULL) {
			GtkWidget *local_plain_vbox;
			GtkWidget *local_themed_vbox;

			local_plain_vbox = glade_xml_get_widget (xml, "local_plain_properties_vbox");
			local_themed_vbox = glade_xml_get_widget (xml, "local_themed_properties_vbox");

			gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), LOCAL_THEMED);
			gtk_widget_hide (local_plain_vbox);
			gtk_widget_show (local_themed_vbox);
		}
	}
	/* Set initial state of remote style combo box. */
	else if (strcmp (ve_sure_string (key), GDM_KEY_REMOTE_GREETER) == 0) {
		refresh_remote_tab ();
	}

	g_object_set_data_full (G_OBJECT (combobox), "key",
	                        g_strdup (key), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (combobox), "changed",
	                  G_CALLBACK (combobox_changed), NULL);

	g_free (greetval);
}

static void
setup_xdmcp_support (void)
{
	GtkWidget *xdmcp_button;

	xdmcp_button = glade_xml_get_widget (xml, "xdmcp_configbutton");
#ifndef HAVE_LIBXDMCP
	/* HAVE_LIBXDMCP */
	gtk_widget_set_sensitive (xdmcp_button, FALSE);
#else
	/* HAVE_LIBXDMCP */
	gtk_widget_set_sensitive (xdmcp_button, TRUE);
#endif

}

static gboolean
module_compare (const char *mod1, const char *mod2)
{
	char *base1;
	char *base2;
	char *p;
	gboolean ret;

	/* first cannonify the names */
	base1 = g_path_get_basename (mod1);
	base2 = g_path_get_basename (mod2);
	if (strncmp (ve_sure_string (base1), "lib", 3) == 0)
		strcpy (base1, &base1[3]);
	if (strncmp (ve_sure_string (base2), "lib", 3) == 0)
		strcpy (base2, &base2[3]);
	p = strstr (base1, ".so");
	if (p != NULL)
		*p = '\0';
	p = strstr (base2, ".so");
	if (p != NULL)
		*p = '\0';

	ret = (strcmp (ve_sure_string (base1), ve_sure_string (base2)) == 0);

	g_free (base1);
	g_free (base2);

	return ret;
}

static gboolean
modules_list_contains (const char *modules_list, const char *module)
{
	char **vec;
	int i;

	if (ve_string_empty (modules_list))
		return FALSE;

	vec = g_strsplit (modules_list, ":", -1);
	if (vec == NULL)
		return FALSE;

	for (i = 0; vec[i] != NULL; i++) {
		if (module_compare (vec[i], module)) {
			g_strfreev (vec);
			return TRUE;
		}
	}

	g_strfreev (vec);
	return FALSE;
}

static gboolean
themes_list_contains (const char *themes_list, const char *theme)
{
	char **vec;
	int i;

	if (ve_string_empty (themes_list))
		return FALSE;

	vec = g_strsplit (themes_list, GDM_DELIMITER_THEMES, -1);
	if (vec == NULL)
		return FALSE;

	for (i = 0; vec[i] != NULL; i++) {
		if (strcmp (ve_sure_string (vec[i]), ve_sure_string (theme)) == 0) {
			g_strfreev (vec);
			return TRUE;
		}
	}

	g_strfreev (vec);
	return FALSE;
}

static char *
modules_list_remove (char *modules_list, const char *module)
{
	char **vec;
	GString *str;
	char *sep = "";
	int i;

	if (ve_string_empty (modules_list))
		return g_strdup ("");

	vec = g_strsplit (modules_list, ":", -1);
	if (vec == NULL)
		return g_strdup ("");

	str = g_string_new (NULL);

	for (i = 0; vec[i] != NULL; i++) {
		if ( ! module_compare (vec[i], module)) {
			g_string_append (str, sep);
			sep = ":";
			g_string_append (str, vec[i]);
		}
	}

	g_strfreev (vec);

	return g_string_free (str, FALSE);
}

/* This function concatenates *string onto *strings_list with the addition
   of *sep as a deliminator inbetween the strings_list and string, then
   returns a copy of the new strings_list. */
static char *
strings_list_add (char *strings_list, const char *string, const char *sep)
{
	char *n;
	if (ve_string_empty (strings_list))
		n = g_strdup (string);
	else
		n = g_strconcat (strings_list, sep, string, NULL);
	g_free (strings_list);
	return n;
}

/* This function removes *string with the addition of *sep
   as a postfix deliminator the string from *strings_list, then
   returns a copy of the new strings_list. */
static char *
strings_list_remove (char *strings_list, const char *string, const char *sep)
{
    char **actions;   
    gint i;
    GString *msg;
    const char *separator = "";
    char *n;

    if (ve_string_empty (strings_list))
        return strings_list;
    
    msg = g_string_new ("");

    actions = g_strsplit (strings_list, sep, -1);
        for (i = 0; actions[i]; i++) {
        if (strncmp (actions[i], string, strlen (string)) == 0)
            continue;
        g_string_append_printf (msg, "%s%s", separator, actions[i]);
        separator = sep;
    }
    g_strfreev (actions);
    n = g_strdup (msg->str);
    g_string_free (msg, TRUE);
    g_free (strings_list);
    return n;
}

static void
acc_modules_toggled (GtkWidget *toggle, gpointer data)
{
	gboolean add_gtk_modules = gdm_config_get_bool (GDM_KEY_ADD_GTK_MODULES);
	char *modules_list       = g_strdup (gdm_config_get_string (GDM_KEY_GTK_MODULES_LIST));

	/* first whack the modules from the list */
	modules_list = modules_list_remove (modules_list, "gail");
	modules_list = modules_list_remove (modules_list, "atk-bridge");
	modules_list = modules_list_remove (modules_list, "dwellmouselistener");
	modules_list = modules_list_remove (modules_list, "keymouselistener");

	if (GTK_TOGGLE_BUTTON (toggle)->active) {
		if ( ! add_gtk_modules) {
			g_free (modules_list);
			modules_list = NULL;
		}

		modules_list = strings_list_add (modules_list, "gail",
			GDM_DELIMITER_MODULES);
		modules_list = strings_list_add (modules_list, "atk-bridge",
			GDM_DELIMITER_MODULES);
		modules_list = strings_list_add (modules_list,
			LIBDIR "/gtk-2.0/modules/libkeymouselistener",
			GDM_DELIMITER_MODULES);
		modules_list = strings_list_add (modules_list,
			LIBDIR "/gtk-2.0/modules/libdwellmouselistener",
			GDM_DELIMITER_MODULES);
		add_gtk_modules = TRUE;
	}

	if (ve_string_empty (modules_list))
		add_gtk_modules = FALSE;

	gdm_setup_config_set_string (GDM_KEY_GTK_MODULES_LIST,
	                      ve_sure_string (modules_list));
	gdm_setup_config_set_bool (GDM_KEY_ADD_GTK_MODULES,
	                    add_gtk_modules);

	g_free (modules_list);
}

static void
test_sound (GtkWidget *button, gpointer data)
{
	GtkWidget *acc_sound_file_chooser = data;
	gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (acc_sound_file_chooser));
	const char *argv[3];

	if ((filename == NULL) || g_access (filename, R_OK) != 0 ||
	    ve_string_empty (GdmSoundProgram))
	       return;

	argv[0] = GdmSoundProgram;
	argv[1] = filename;
	argv[2] = NULL;

	g_spawn_async ("/" /* working directory */,
		       (char **)argv,
		       NULL /* envp */,
		       0    /* flags */,
		       NULL /* child setup */,
		       NULL /* user data */,
		       NULL /* child pid */,
		       NULL /* error */);

	g_free (filename);
}

static void
sound_response (GtkWidget *file_chooser, gpointer data)
{
	gchar *filename;
	gchar *sound_key;
	gchar *value;
		
	filename  = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
	sound_key = g_object_get_data (G_OBJECT (file_chooser), "key");	
	value     = gdm_config_get_string (sound_key);
	
	if (strcmp (ve_sure_string (value), ve_sure_string (filename)) != 0) {
		gdm_setup_config_set_string (sound_key,
			(char *)ve_sure_string (filename));
		update_greeters ();
	}
	g_free (filename);
}

static void
setup_users_tab (void)
{
	GtkFileFilter *filter;
	GtkWidget *default_face_filechooser;
	GtkWidget *default_face_checkbox;
	GtkWidget *global_face_dir_filechooser;
	GtkWidget *global_face_dir_checkbox;
	gchar *filename;

	setup_greeter_toggle ("fb_allusers",
			      GDM_KEY_INCLUDE_ALL);
	setup_face ();

	/* Setup default face */
	default_face_filechooser = glade_xml_get_widget (xml, "default_face_filechooser");

	filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("Images"));
        gtk_file_filter_add_pixbuf_formats (filter);
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (default_face_filechooser), filter);

	filename = gdm_config_get_string (GDM_KEY_DEFAULT_FACE);

	default_face_checkbox = glade_xml_get_widget (xml, "default_face_checkbutton");

	if (!ve_string_empty (filename) && access (filename, R_OK|X_OK)) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (default_face_filechooser),
					       filename);
		
		gtk_widget_set_sensitive (default_face_filechooser, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (default_face_checkbox), TRUE);
		
	}	
	else {
		gtk_widget_set_sensitive (default_face_filechooser, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (default_face_checkbox), FALSE);
	}
	
	g_object_set_data_full (G_OBJECT (default_face_filechooser),
				"key", g_strdup (GDM_KEY_DEFAULT_FACE),
				(GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (default_face_filechooser), "selection-changed",
			  G_CALLBACK (default_filechooser_response),
			  NULL);	
	
	g_object_set_data_full (G_OBJECT (default_face_checkbox),
				"key", g_strdup (GDM_KEY_DEFAULT_FACE),
				(GDestroyNotify) g_free);       	
	g_signal_connect (G_OBJECT (default_face_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled), default_face_checkbox);	
	g_signal_connect (G_OBJECT (default_face_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled_sensitivity_positive), default_face_filechooser);			
	
	/* Setup global face dir */
	g_free (filename);		
	
	global_face_dir_filechooser = glade_xml_get_widget (xml, "global_face_dir_filechooser");

	filename = gdm_config_get_string (GDM_KEY_GLOBAL_FACE_DIR);

	global_face_dir_checkbox = glade_xml_get_widget (xml, "global_face_dir_checkbutton");

	if (!ve_string_empty (filename) && access (filename, R_OK|X_OK) == 0) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (global_face_dir_filechooser),
					       filename);
		gtk_widget_set_sensitive (global_face_dir_filechooser, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (global_face_dir_checkbox), TRUE);
	}
	else {
		gtk_widget_set_sensitive (global_face_dir_filechooser, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (global_face_dir_checkbox), FALSE);
	}
       

	g_object_set_data_full (G_OBJECT (global_face_dir_filechooser),
				"key", g_strdup (GDM_KEY_GLOBAL_FACE_DIR),
				(GDestroyNotify) g_free);		
	g_signal_connect (G_OBJECT (global_face_dir_filechooser), "selection-changed",
			  G_CALLBACK (default_filechooser_response), NULL);
			
	g_object_set_data_full (G_OBJECT (global_face_dir_checkbox),
				"key", g_strdup (GDM_KEY_GLOBAL_FACE_DIR),
				(GDestroyNotify) g_free);       	
	g_signal_connect (G_OBJECT (global_face_dir_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled), global_face_dir_checkbox);	
	g_signal_connect (G_OBJECT (global_face_dir_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled_sensitivity_positive), global_face_dir_filechooser);				
	g_free (filename);
}

static void
setup_accessibility_tab (void)
{
	GtkWidget *enable_accessible_login;				     
	GtkWidget *access_sound_ready_file_chooser;
	GtkWidget *access_sound_ready_play_button;
	GtkWidget *access_sound_success_file_chooser;
	GtkWidget *access_sound_success_play_button;
	GtkWidget *access_sound_failure_file_chooser;
	GtkWidget *access_sound_failure_play_button;				      
	GtkFileFilter *all_sounds_filter;
	GtkFileFilter *all_files_filter;
	gboolean add_gtk_modules;
	gchar *gdm_key_sound_ready;
	gchar *gdm_key_sound_success;
	gchar *gdm_key_sound_failure;
	gchar *modules_list;
	gchar *value;
	
	enable_accessible_login = glade_xml_get_widget (xml, "acc_modules");

	access_sound_ready_file_chooser = glade_xml_get_widget (xml, "acc_sound_ready_button");

	access_sound_ready_play_button = glade_xml_get_widget (xml, "acc_soundtest_ready_button");

	access_sound_success_file_chooser = glade_xml_get_widget (xml, "acc_sound_success_button");

	access_sound_success_play_button = glade_xml_get_widget (xml, "acc_soundtest_success_button");

	access_sound_failure_file_chooser = glade_xml_get_widget (xml, "acc_sound_failure_button");

	access_sound_failure_play_button = glade_xml_get_widget (xml, "acc_soundtest_failure_button");

	setup_greeter_toggle ("acc_theme",
	                      GDM_KEY_ALLOW_GTK_THEME_CHANGE);
	setup_greeter_toggle ("acc_sound_ready", 
	                      GDM_KEY_SOUND_ON_LOGIN);
	setup_greeter_toggle ("acc_sound_success",
	                      GDM_KEY_SOUND_ON_LOGIN_SUCCESS);
	setup_greeter_toggle ("acc_sound_failure",
	                      GDM_KEY_SOUND_ON_LOGIN_FAILURE);

	add_gtk_modules = gdm_config_get_bool (GDM_KEY_ADD_GTK_MODULES);
	modules_list    = gdm_config_get_string (GDM_KEY_GTK_MODULES_LIST);

	if (!(add_gtk_modules &&
	    modules_list_contains (modules_list, "gail") &&
	    modules_list_contains (modules_list, "atk-bridge") &&
	    modules_list_contains (modules_list, "dwellmouselistener") &&
	    modules_list_contains (modules_list, "keymouselistener"))) {
	    
  		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_accessible_login),
					      FALSE);
	}
	else {
	 	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_accessible_login),
					      TRUE);
	}
	
	all_sounds_filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (all_sounds_filter, _("Sounds"));
	gtk_file_filter_add_mime_type (all_sounds_filter, "audio/x-wav");
	gtk_file_filter_add_mime_type (all_sounds_filter, "application/ogg");

	all_files_filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (all_files_filter, _("All Files"));
	gtk_file_filter_add_pattern(all_files_filter, "*");

	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_ready_file_chooser), all_sounds_filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_ready_file_chooser), all_files_filter);

	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_success_file_chooser), all_sounds_filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_success_file_chooser), all_files_filter);
	
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_failure_file_chooser), all_sounds_filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (access_sound_failure_file_chooser), all_files_filter);
		
	value = gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE);

	if (value != NULL && *value != '\0') {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (access_sound_ready_file_chooser), 
		                               value);
	}
	else {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (access_sound_ready_file_chooser),
		                                     DATADIR"/sounds");
	}

	value = gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);

	if (value != NULL && *value != '\0') {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (access_sound_success_file_chooser),
		                               value);
	}
	else {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (access_sound_success_file_chooser),
		                                     DATADIR"/sounds");
	}
	
	value = gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

	if (value != NULL && *value != '\0') {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (access_sound_failure_file_chooser),
		                               value);
	} 
	else {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (access_sound_failure_file_chooser),
		                                     DATADIR"/sounds");
	}
	
	gdm_key_sound_ready = g_strdup (GDM_KEY_SOUND_ON_LOGIN_FILE);
	
	g_object_set_data (G_OBJECT (access_sound_ready_file_chooser), "key",
	                   gdm_key_sound_ready);
	
	gdm_key_sound_success = g_strdup (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);

	g_object_set_data (G_OBJECT (access_sound_success_file_chooser), "key",
	                   gdm_key_sound_success);
	
	gdm_key_sound_failure = g_strdup (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

	g_object_set_data (G_OBJECT (access_sound_failure_file_chooser), "key",
	                   gdm_key_sound_failure);

	g_signal_connect (G_OBJECT (enable_accessible_login), "toggled",
			  G_CALLBACK (acc_modules_toggled), NULL);			  			  
	g_signal_connect (G_OBJECT (access_sound_ready_play_button), "clicked",
			  G_CALLBACK (test_sound), access_sound_ready_file_chooser);
	g_signal_connect (G_OBJECT (access_sound_success_play_button), "clicked",
			  G_CALLBACK (test_sound), access_sound_success_file_chooser);
	g_signal_connect (G_OBJECT (access_sound_failure_play_button), "clicked",
			  G_CALLBACK (test_sound), access_sound_failure_file_chooser);
	g_signal_connect (G_OBJECT (access_sound_ready_file_chooser), "selection-changed", 
	                  G_CALLBACK (sound_response), access_sound_ready_file_chooser);
	g_signal_connect (G_OBJECT (access_sound_success_file_chooser), "selection-changed", 
	                  G_CALLBACK (sound_response), access_sound_success_file_chooser);
	g_signal_connect (G_OBJECT (access_sound_failure_file_chooser), "selection-changed", 
	                  G_CALLBACK (sound_response), access_sound_failure_file_chooser);
}

static char *
get_theme_dir (void)
{
	/*
	 * We always want to free the value returned from this function, since
	 * we use strdup to build a reasonable value if the configuration value
	 * is not good.  So use g_strdup here.
	 */
	char *theme_dir = g_strdup (gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME_DIR));

	if (theme_dir == NULL ||
	    theme_dir[0] == '\0' ||
	    g_access (theme_dir, R_OK) != 0) {
		theme_dir = g_strdup (DATADIR "/gdm/themes/");
	}

	return theme_dir;
}

static void
textview_set_buffer (GtkTextView *view, const char *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
	gtk_text_buffer_set_text (buffer, text, -1);
}

/* Sets up the preview section of Themed Greeter page
   after a theme has been selected */
static void
gg_selection_changed (GtkTreeSelection *selection, gpointer data)
{
	static gboolean FirstPass = TRUE;
	GtkWidget *label;
	GtkWidget *label_remote;
	GtkWidget *delete_button;
	GtkWidget *delete_button_remote;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTextBuffer *buffer_local, *buffer_remote;
	GtkTextIter iter_local, iter_remote;
	GValue value  = {0, };
	gboolean GdmGraphicalThemeRand;
	gchar *str;
	gint selected = -1;

	delete_button = glade_xml_get_widget (xml, "gg_delete_theme");
	delete_button_remote = glade_xml_get_widget (xml, "gg_delete_theme_remote");

	if ( !gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_widget_set_sensitive (delete_button, FALSE);
		gtk_widget_set_sensitive (delete_button_remote, FALSE);
		return;
	}

	/* Default to allow deleting of themes */
	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {
	
		GtkWidget *theme_list;
		GtkWidget *local_combobox;
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreePath *path;
		
		theme_list = glade_xml_get_widget (xml, "gg_theme_list_remote");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list));

		local_combobox = glade_xml_get_widget (xml, "gg_mode_combobox");
		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (local_combobox));

		if (model != NULL) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);					  
			if (path != NULL) {
				gtk_tree_selection_select_path (selection, path);
				if (GTK_WIDGET_REALIZED(theme_list)) {
					gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (theme_list),
        	                                     		      path, NULL, FALSE, 0.0, 0.0);
				}
			}
		}
		gtk_widget_set_sensitive (delete_button, TRUE);
		gtk_widget_set_sensitive (delete_button_remote, TRUE);
	}
	else {
		GtkWidget *theme_list;
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreePath *path;
		
		theme_list = glade_xml_get_widget (xml, "gg_theme_list");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list));
		
		if (model != NULL) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);					  
			if (path != NULL) {
				gtk_tree_selection_select_path (selection, path);
				if (GTK_WIDGET_REALIZED(theme_list)) {
					gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (theme_list),
        	                                     		      path, NULL, FALSE, 0.0, 0.0);
				}
			}
		}
		gtk_widget_set_sensitive (delete_button, TRUE);
		gtk_widget_set_sensitive (delete_button_remote, TRUE);
	}
	/* Determine if the theme selected is currently active */
	GdmGraphicalThemeRand = gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND);
	if (GdmGraphicalThemeRand) {
		gtk_tree_model_get_value (model, &iter, THEME_COLUMN_SELECTED_LIST, &value);
	} else {
		gtk_tree_model_get_value (model, &iter, THEME_COLUMN_SELECTED, &value);

		/* We might have "Random form selected" option selected, but no
		   active themes there hence defaulting to "Selected only" 
		   Lets make this theme delete-able from "Random from selected"
		   mode in this case */
		if (g_value_get_boolean (&value) && selected == RANDOM_THEME) {	
			g_value_unset (&value);
			g_value_set_boolean (&value, FALSE);		    	
		}
	}

	/* Do not allow deleting of active themes */
	if (g_value_get_boolean (&value)) {
		gtk_widget_set_sensitive (delete_button, FALSE);
		gtk_widget_set_sensitive (delete_button_remote, FALSE);
	}
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_AUTHOR,
				  &value);
				  		  
	if (!ve_string_empty (ve_sure_string (g_value_get_string (&value)))) { 
		str = g_strconcat (ve_sure_string (g_value_get_string (&value)), NULL);
	}
	else {
		str = g_strconcat (_("None"), NULL);
	}

	label = glade_xml_get_widget (xml, "gg_author_text_view");
	label_remote = glade_xml_get_widget (xml, "gg_author_text_view_remote");

	textview_set_buffer (GTK_TEXT_VIEW (label), "");
	textview_set_buffer (GTK_TEXT_VIEW (label_remote), "");
	
	buffer_local = gtk_text_view_get_buffer (GTK_TEXT_VIEW (label));
	buffer_remote = gtk_text_view_get_buffer (GTK_TEXT_VIEW (label_remote));
	gtk_text_buffer_get_iter_at_offset (buffer_local, &iter_local, 0);
	gtk_text_buffer_get_iter_at_offset (buffer_remote, &iter_remote, 0);

	if (FirstPass == TRUE) {
		gtk_text_buffer_create_tag (buffer_local, "small",
		                            "scale", PANGO_SCALE_SMALL, NULL);
		gtk_text_buffer_create_tag (buffer_remote, "small",
		                            "scale", PANGO_SCALE_SMALL, NULL);
	}
	
	gtk_text_buffer_insert_with_tags_by_name (buffer_local, &iter_local,
	                                          str, -1, "small", NULL);	     
	gtk_text_buffer_insert_with_tags_by_name (buffer_remote, &iter_remote,
	                                          str, -1, "small", NULL);
	g_value_unset (&value);
	g_free (str);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_COPYRIGHT,
				  &value);
				  
	label = glade_xml_get_widget (xml, "gg_copyright_text_view");
	label_remote = glade_xml_get_widget (xml, "gg_copyright_text_view_remote");

	textview_set_buffer (GTK_TEXT_VIEW (label), "");
	textview_set_buffer (GTK_TEXT_VIEW (label_remote), "");
	
	buffer_local = gtk_text_view_get_buffer (GTK_TEXT_VIEW (label));
	buffer_remote = gtk_text_view_get_buffer (GTK_TEXT_VIEW (label_remote));
		
	gtk_text_buffer_get_iter_at_offset (buffer_local, &iter_local, 0);
	gtk_text_buffer_get_iter_at_offset (buffer_remote, &iter_remote, 0);	
	if (FirstPass == TRUE) {	     
		gtk_text_buffer_create_tag (buffer_local, "small",
		                            "scale", PANGO_SCALE_SMALL, NULL);
		gtk_text_buffer_create_tag (buffer_remote, "small",
		                            "scale", PANGO_SCALE_SMALL, NULL);
	}
				  
	if (!ve_string_empty (ve_sure_string (g_value_get_string (&value)))) { 
		str = g_strconcat (ve_sure_string (g_value_get_string (&value)), NULL);
	}
	else {
		str = g_strconcat (_("None"), NULL);
	}

	gtk_text_buffer_insert_with_tags_by_name (buffer_local, &iter_local,
	                                          str, -1, "small", NULL);	     
	gtk_text_buffer_insert_with_tags_by_name (buffer_remote, &iter_remote,
	                                          str, -1, "small", NULL);
	
	FirstPass = FALSE;
	g_value_unset (&value);
}

static GtkTreeIter *
read_themes (GtkListStore *store, const char *theme_dir, DIR *dir,
	     const char *select_item)
{
	struct dirent *dent;
	GtkTreeIter *select_iter = NULL;
	GdkPixbuf *pb = NULL;
	gchar *markup = NULL;
	gchar * real_selected_themes = NULL;
	
	while ((dent = readdir (dir)) != NULL) {
		char *n, *file, *name, *desc, *author, *copyright, *ss;
		char *full;
		GtkTreeIter iter;
		gboolean sel_theme;
		gboolean sel_themes;
		GKeyFile *theme_file;

		if (dent->d_name[0] == '.')
			continue;
		n = g_strconcat (theme_dir, "/", dent->d_name,
				 "/GdmGreeterTheme.desktop", NULL);
		if (g_access (n, R_OK) != 0) {
			g_free (n);
			n = g_strconcat (theme_dir, "/", dent->d_name,
					 "/GdmGreeterTheme.info", NULL);
		}
		if (g_access (n, R_OK) != 0) {
			g_free (n);
			continue;
		}

		file = gdm_get_theme_greeter (n, dent->d_name);
		full = g_strconcat (theme_dir, "/", dent->d_name,
				    "/", file, NULL);
		if (g_access (full, R_OK) != 0) {
			g_free (file);
			g_free (full);
			g_free (n);
			continue;
		}
		g_free (full);

		if (selected_theme != NULL &&
		    strcmp (ve_sure_string (dent->d_name), ve_sure_string (selected_theme)) == 0)
			sel_theme = TRUE;
		else
			sel_theme = FALSE;

		if (selected_themes != NULL &&
		    themes_list_contains (selected_themes, dent->d_name)) {
			sel_themes = TRUE;
			/* It might be the case that the config option RandomThemes that 
			   do not longer exist in the theme dir. Here we rectifying that */
			real_selected_themes = strings_list_add (real_selected_themes,
								 dent->d_name, GDM_DELIMITER_THEMES);
		}
		else
			sel_themes = FALSE;

		theme_file = gdm_common_config_load (n, NULL);
		name = NULL;
		gdm_common_config_get_translated_string (theme_file, "GdmGreeterTheme/Name", &name, NULL);
		if (ve_string_empty (name)) {
			g_free (name);
			name = g_strdup (dent->d_name);
		}

		desc = author = copyright = ss = NULL;
		gdm_common_config_get_translated_string (theme_file, "GdmGreeterTheme/Description", &desc, NULL);
		gdm_common_config_get_translated_string (theme_file, "GdmGreeterTheme/Author", &author, NULL);
		gdm_common_config_get_translated_string (theme_file, "GdmGreeterTheme/Copyright", &copyright, NULL);
		gdm_common_config_get_translated_string (theme_file, "GdmGreeterTheme/Screenshot", &ss, NULL);

		g_key_file_free (theme_file);

		if (ss != NULL)
			full = g_strconcat (theme_dir, "/", dent->d_name,
					    "/", ss, NULL);
		else
			full = NULL;

		if ( ! ve_string_empty (full) &&
		    g_access (full, R_OK) == 0) {

			pb = gdk_pixbuf_new_from_file (full, NULL);
			if (pb != NULL) {
				if (gdk_pixbuf_get_width (pb) > 64 ||
				    gdk_pixbuf_get_height (pb) > 50) {
					GdkPixbuf *pb2;
					pb2 = gdk_pixbuf_scale_simple
						(pb, 64, 50,
						 GDK_INTERP_BILINEAR);
					g_object_unref (G_OBJECT (pb));
					pb = pb2;
				}
			}
		}			   
					   
		markup = g_markup_printf_escaped ("<b>%s</b>\n<small>%s</small>", name, desc);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    THEME_COLUMN_SELECTED, sel_theme,
				    THEME_COLUMN_SELECTED_LIST, sel_themes,
				    THEME_COLUMN_DIR, dent->d_name,
				    THEME_COLUMN_FILE, file,
				    THEME_COLUMN_SCREENSHOT, pb,
				    THEME_COLUMN_MARKUP, markup,
				    THEME_COLUMN_NAME, name,
				    THEME_COLUMN_DESCRIPTION, desc,
				    THEME_COLUMN_AUTHOR, author,
				    THEME_COLUMN_COPYRIGHT, copyright,
				    -1);

		if (select_item != NULL &&
		    strcmp (ve_sure_string (dent->d_name), ve_sure_string (select_item)) == 0) {
			/* anality */ g_free (select_iter);
			select_iter = g_new0 (GtkTreeIter, 1);
			*select_iter = iter;
		}

		g_free (file);
		g_free (name);
		g_free (markup);
		g_free (desc);
		g_free (author);
		g_free (copyright);
		g_free (ss);
		g_free (full);
		g_free (n);
	}

	g_free (selected_themes);
	selected_themes = g_strdup (real_selected_themes);
	g_free (real_selected_themes);

	return select_iter;
}

static gboolean
greeter_theme_timeout (GtkWidget *toggle)
{
	char *theme;
	char *themes;

	theme  = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME);
	themes = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEMES);

	/* If no checkbox themes selected */
	if (selected_themes == NULL)
		selected_themes = "";

	/* If themes have changed from the custom_config file, update it. */
	if (strcmp (ve_sure_string (theme),
		ve_sure_string (selected_theme)) != 0) {

		gdm_setup_config_set_string (GDM_KEY_GRAPHICAL_THEME,
			selected_theme);
		update_greeters ();
	}

	if (strcmp (ve_sure_string (themes),
		ve_sure_string (selected_themes)) != 0) {

		gdm_setup_config_set_string (GDM_KEY_GRAPHICAL_THEMES,
			selected_themes);

		/* This should only be executed if we dealing with
		   random theme setting. If no random themes are present and
		   the random theme option was set to true we force it to be false.
		   If there is at least one random theme selected and the random 
		   theme option was false we force it to be true */		
		if (ve_string_empty (selected_themes) && 
		    gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND)) {
			gdm_setup_config_set_bool (GDM_KEY_GRAPHICAL_THEME_RAND, FALSE);
			GdmRandomFromSelectedChangesWarn = TRUE;
			
		} else if (!ve_string_empty (selected_themes) && 
			   !gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND)) {
			gdm_setup_config_set_bool (GDM_KEY_GRAPHICAL_THEME_RAND, TRUE);
			GdmRandomFromSelectedChangesWarn = FALSE;
		}
	       
		update_greeters ();
	}

	return FALSE;
}

static void
selected_toggled (GtkCellRendererToggle *cell,
		  char                  *path_str,
		  gpointer               data)
{
	gchar *theme_name   = NULL;
	GtkTreeModel *model = GTK_TREE_MODEL (data);
	GtkTreeIter selected_iter;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreePath *sel_path = gtk_tree_path_new_from_string (path_str);
	GtkWidget *theme_list = glade_xml_get_widget (xml, "gg_theme_list");
	GtkWidget *del_button = glade_xml_get_widget (xml, "gg_delete_theme");
	GtkWidget *del_button_remote = glade_xml_get_widget (xml, "gg_delete_theme_remote");
	gboolean is_radio;

	gtk_tree_model_get_iter (model, &selected_iter, sel_path);
	path     = gtk_tree_path_new_first ();
	is_radio = gtk_cell_renderer_toggle_get_radio (cell);
	
	if (is_radio) { /* Radiobuttons */
		/* Clear list of all selected themes */
		g_free (selected_theme);

		/* Get the new selected theme */
		gtk_tree_model_get (model, &selected_iter,
				    THEME_COLUMN_DIR, &selected_theme, -1);

		/* Loop through all themes in list */
		while (gtk_tree_model_get_iter (model, &iter, path)) {
			/* If this toggle was just toggled */
			if (gtk_tree_path_compare (path, sel_path) == 0) {
				gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					THEME_COLUMN_SELECTED, TRUE,
					-1); /* Toggle ON */
				gtk_widget_set_sensitive (del_button, FALSE);
				gtk_widget_set_sensitive (del_button_remote, FALSE);
			} else {
				gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					THEME_COLUMN_SELECTED, FALSE,
					-1); /* Toggle OFF */
			}

			gtk_tree_path_next (path);
		}
	} else { /* Checkboxes */

		/* Clear list of all selected themes */
		g_free (selected_themes);
		selected_themes = NULL;

		/* Loop through all checkboxes */
		while (gtk_tree_model_get_iter (model, &iter, path)) {
			gboolean selected = FALSE;

			/* If this checkbox was just toggled */
			if (gtk_tree_path_compare (path, sel_path) == 0) {

			gtk_tree_model_get (model, &selected_iter,
					    THEME_COLUMN_DIR, &theme_name, -1);
				if (gtk_cell_renderer_toggle_get_active (cell)) {
					gtk_list_store_set (GTK_LIST_STORE (model), &iter,
						THEME_COLUMN_SELECTED_LIST,
						FALSE, -1); /* Toggle OFF */
					/* We will only make a theme delete-able if its not the
					   selected theme from single the theme mode */
					gtk_widget_set_sensitive (del_button, TRUE);
					gtk_widget_set_sensitive (del_button_remote, TRUE);
				} else {
					gtk_list_store_set (GTK_LIST_STORE (model), &iter,
						THEME_COLUMN_SELECTED_LIST,
						TRUE, -1); /* Toggle ON */
					gtk_widget_set_sensitive (del_button, FALSE);
					gtk_widget_set_sensitive (del_button_remote, FALSE);
				}
			}
	
			gtk_tree_model_get (model, &iter, THEME_COLUMN_SELECTED_LIST,
				&selected, THEME_COLUMN_DIR, &theme_name, -1);
	
			if (selected)
				selected_themes = strings_list_add (selected_themes,
					theme_name, GDM_DELIMITER_THEMES);
	
			g_free (theme_name);
			gtk_tree_path_next (path);
		}

		if (selected_themes == NULL)
			selected_themes = g_strdup("");
		
		/* There are no themes selected atm in the Random form selected mode.
		   We need to inform users that is the case */
		if (ve_string_empty (selected_themes)) {
			GtkWidget *setup_dialog = glade_xml_get_widget(xml, "setup_dialog");
		
			GtkWidget *dlg = hig_dialog_new (GTK_WINDOW (setup_dialog),
							 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_WARNING,
							 GTK_BUTTONS_OK,
							 _("No themes selected!"),
							 _("You need one or more themes selected for "
							   "the \"Random from selected\" option to be "
							   "valid. Failure to do so will force "
							   "\"Selected only\" mode."));
			gtk_dialog_run (GTK_DIALOG (dlg));
			gtk_widget_destroy (dlg);
		}
	}

	gtk_tree_path_free (path);
	gtk_tree_path_free (sel_path);

	run_timeout (theme_list, 500, greeter_theme_timeout);
}

static gboolean
is_ext (gchar *filename, const char *ext)
{
	const char *dot;

	dot = strrchr (filename, '.');
	if (dot == NULL)
		return FALSE;

	if (strcmp (ve_sure_string (dot), ve_sure_string (ext)) == 0)
		return TRUE;
	else
		return FALSE;
}

/* sense the right unzip program */
static char *
find_unzip (gchar *filename)
{
	char *prog;
	char *tryg[] = {
		"/bin/gunzip",
		"/usr/bin/gunzip",
		NULL };
	char *tryb[] = {
		"/bin/bunzip2",
		"/usr/bin/bunzip2",
		NULL };
	int i;

	if (is_ext (filename, ".bz2")) {
		prog = g_find_program_in_path ("bunzip2");
		if (prog != NULL)
			return prog;

		for (i = 0; tryb[i] != NULL; i++) {
			if (g_access (tryb[i], X_OK) == 0)
				return g_strdup (tryb[i]);
		}
	}

	prog = g_find_program_in_path ("gunzip");
	if (prog != NULL)
		return prog;

	for (i = 0; tryg[i] != NULL; i++) {
		if (g_access (tryg[i], X_OK) == 0)
			return g_strdup (tryg[i]);
	}
	/* Hmmm, fallback */
	return g_strdup ("/bin/gunzip");
}

static char *
find_tar (void)
{
	char *tar_prog;
	char *try[] = {
		"/bin/gtar",
		"/bin/tar",
		"/usr/bin/gtar",
		"/usr/bin/tar",
		NULL };
	int i;

	tar_prog = g_find_program_in_path ("gtar");
	if (tar_prog != NULL)
		return tar_prog;

	tar_prog = g_find_program_in_path ("tar");
	if (tar_prog != NULL)
		return tar_prog;

	for (i = 0; try[i] != NULL; i++) {
		if (g_access (try[i], X_OK) == 0)
			return g_strdup (try[i]);
	}
	/* Hmmm, fallback */
	return g_strdup ("/bin/tar");
}

static char *
find_chmod (void)
{
	char *chmod_prog;
	char *try[] = {
		"/bin/chmod",
		"/sbin/chmod",
		"/usr/bin/chmod",
		"/usr/sbin/chmod",
		NULL };
	int i;

	chmod_prog = g_find_program_in_path ("chmod");
	if (chmod_prog != NULL)
		return chmod_prog;

	for (i = 0; try[i] != NULL; i++) {
		if (g_access (try[i], X_OK) == 0)
			return g_strdup (try[i]);
	}
	/* Hmmm, fallback */
	return g_strdup ("/bin/chmod");
}

static char *
find_chown (void)
{
	char *chown_prog;
	char *try[] = {
		"/bin/chown",
		"/sbin/chown",
		"/usr/bin/chown",
		"/usr/sbin/chown",
		NULL };
	int i;

	chown_prog = g_find_program_in_path ("chown");
	if (chown_prog != NULL)
		return chown_prog;

	for (i = 0; try[i] != NULL; i++) {
		if (g_access (try[i], X_OK) == 0)
			return g_strdup (try[i]);
	}
	/* Hmmm, fallback */
	return g_strdup ("/bin/chown");
}


static char *
get_the_dir (FILE *fp, char **error)
{
	char buf[2048];
	char *dir = NULL;
	int dirlen = 0;
	gboolean got_info = FALSE;
	gboolean read_a_line = FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p, *s;

		read_a_line = TRUE;

		p = strchr (buf, '\n');
		if (p != NULL)
			*p = '\0';
		if (dir == NULL) {
			p = strchr (buf, '/');
			if (p != NULL)
				*p = '\0';
			dir = g_strdup (buf);
			if (p != NULL)
				*p = '/';
			dirlen = strlen (dir);

			if (dirlen < 1) {
				*error =
					_("Archive is not of a subdirectory");

				g_free (dir);
				return NULL;
			}
		}

		if (strncmp (ve_sure_string (buf), ve_sure_string (dir), dirlen) != 0) {
			*error = _("Archive is not of a single subdirectory");
			g_free (dir);
			return NULL;
		}

		if ( ! got_info) {
			s = g_strconcat (dir, "/GdmGreeterTheme.info", NULL);
			if (strcmp (ve_sure_string (buf), ve_sure_string (s)) == 0)
				got_info = TRUE;
			g_free (s);
		}

		if ( ! got_info) {
			s = g_strconcat (dir, "/GdmGreeterTheme.desktop", NULL);
			if (strcmp (ve_sure_string (buf), ve_sure_string (s)) == 0)
				got_info = TRUE;
			g_free (s);
		}
	}

	if (got_info)
		return dir;

	if ( ! read_a_line)
		*error = _("File not a tar.gz or tar archive");
	else
		*error = _("Archive does not include a "
			   "GdmGreeterTheme.info file");

	g_free (dir);
	return NULL;
}

static char *
get_archive_dir (gchar *filename, char **untar_cmd, char **error)
{
	char *quoted;
	char *tar;
	char *unzip;
	char *cmd;
	char *dir;
	FILE *fp;

	*untar_cmd = NULL;

	*error = NULL;

	if (g_access (filename, F_OK) != 0) {
		*error = _("File does not exist");
		return NULL;
	}

	quoted = g_shell_quote (filename);
	tar = find_tar ();
	unzip = find_unzip (filename);

	cmd = g_strdup_printf ("%s -c %s | %s -tf -", unzip, quoted, tar);
	fp = popen (cmd, "r");
	g_free (cmd);
	if (fp != NULL) {
		int ret;
		dir = get_the_dir (fp, error);
		ret = pclose (fp);
		if (ret == 0 && dir != NULL) {
			*untar_cmd = g_strdup_printf ("%s -c %s | %s -xf -",
						      unzip, quoted, tar);
			g_free (tar);
			g_free (unzip);
			g_free (quoted);
			return dir;
		} else {
			*error = NULL;
		}
		g_free (dir);
	}

	/* error due to command failing */
	if (*error == NULL) {
		/* Try uncompressed? */
		cmd = g_strdup_printf ("%s -tf %s", tar, quoted);
		fp = popen (cmd, "r");
		g_free (cmd);
		if (fp != NULL) {
			int ret;
			dir = get_the_dir (fp, error);
			ret = pclose (fp);
			if (ret == 0 && dir != NULL) {
				*untar_cmd = g_strdup_printf ("%s -xf %s",
							      tar, quoted);
				g_free (tar);
				g_free (unzip);
				g_free (quoted);
				return dir;
			} else {
				*error = NULL;
			}
			g_free (dir);
		}
	}

	if (*error == NULL)
		*error = _("File not a tar.gz or tar archive");

	g_free (tar);
	g_free (unzip);
	g_free (quoted);

	return NULL;
}

static gboolean
dir_exists (const char *parent, const char *dir)
{
	DIR *dp = opendir (parent);
	struct dirent *dent;
	
	if (dp == NULL)
		return FALSE;

	while ((dent = readdir (dp)) != NULL) {
		if (strcmp (ve_sure_string (dent->d_name), ve_sure_string (dir)) == 0) {
			closedir (dp);
			return TRUE;
		}
	}
	closedir (dp);
	return FALSE;
}

static void
install_theme_file (gchar *filename, GtkListStore *store, GtkWindow *parent)
{
	GtkTreeSelection *selection;
	GtkTreeIter *select_iter = NULL;
	GtkWidget *theme_list;
	DIR *dp;
	gchar *cwd;
	gchar *dir;
	gchar *error;
	gchar *theme_dir;
	gchar *untar_cmd;
	gboolean success = FALSE;

	theme_list = glade_xml_get_widget (xml, "gg_theme_list");

	cwd = g_get_current_dir ();
	theme_dir = get_theme_dir ();

	if ( !g_path_is_absolute (filename)) {

		gchar *temp;
		
		temp = g_build_filename (cwd, filename, NULL);
		g_free (filename);
		filename = temp;
	}
	
	dir = get_archive_dir (filename, &untar_cmd, &error);

	/* FIXME: perhaps do a little bit more sanity checking of
	 * the archive */

	if (dir == NULL) {

		GtkWidget *dialog;
		gchar *msg;

		msg = g_strdup_printf (_("%s"), error);

		dialog = hig_dialog_new (GTK_WINDOW (parent),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Not a theme archive"),
					 msg);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		g_free (theme_dir);
		g_free (untar_cmd);
		g_free (cwd);
		g_free (msg);
		return;
	}

	if (dir_exists (theme_dir, dir)) {

		GtkWidget *button;
		GtkWidget *dialog;
		gchar *fname;
		gchar *s;

		fname = ve_filename_to_utf8 (dir);

		/* FIXME: if exists already perhaps we could also have an
		 * option to change the dir name */
		s = g_strdup_printf (_("Theme directory '%s' seems to be already "
				       "installed. Install again anyway?"),
				     fname);
		
		dialog = hig_dialog_new (GTK_WINDOW (parent),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 s,
					 "");
		g_free (fname);
		g_free (s);

		button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_NO);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);

		button = gtk_button_new_from_stock ("_Install Anyway");
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_YES);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_YES);

		gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

		if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_YES) {
			gtk_widget_destroy (dialog);
			g_free (theme_dir);
			g_free (untar_cmd);
			g_free (cwd);
			g_free (dir);
			return;
		}
		gtk_widget_destroy (dialog);
	}

	g_assert (untar_cmd != NULL);

	if (g_chdir (theme_dir) == 0 &&
	    /* this is a security sanity check */
	    strchr (dir, '/') == NULL &&
	    system (untar_cmd) == 0) {

		gchar *argv[5];
		gchar *quoted;
		gchar *chown;
		gchar *chmod;

		quoted = g_strconcat ("./", dir, NULL);
		chown = find_chown ();
		chmod = find_chmod ();
		success = TRUE;

		/* HACK! */
		argv[0] = chown;
		argv[1] = "-R";
		argv[2] = "root:root";
		argv[3] = quoted;
		argv[4] = NULL;
		simple_spawn_sync (argv);

		argv[0] = chmod;
		argv[1] = "-R";
		argv[2] = "a+r";
		argv[3] = quoted;
		argv[4] = NULL;
		simple_spawn_sync (argv);

		argv[0] = chmod;
		argv[1] = "a+x";
		argv[2] = quoted;
		argv[3] = NULL;
		simple_spawn_sync (argv);

		g_free (quoted);
		g_free (chown);
		g_free (chmod);
	}

	if (!success) {
	
		GtkWidget *dialog;
		
		dialog = hig_dialog_new (GTK_WINDOW (parent),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Some error occurred when "
					   "installing the theme"),
					 "");
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

	gtk_list_store_clear (store);

	dp = opendir (theme_dir);

	if (dp != NULL) {
		select_iter = read_themes (store, theme_dir, dp, dir);
		closedir (dp);
	}

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));

	if (select_iter != NULL) {
		gtk_tree_selection_select_iter (selection, select_iter);
		g_free (select_iter);
	}
	
	g_free (untar_cmd);
	g_free (theme_dir);
	g_free (dir);
	g_free (cwd);
}

static void
theme_install_response (GtkWidget *chooser, gint response, gpointer data)
{
	GtkListStore *store = data;
	gchar *filename;

	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (chooser);
		return;
	}

	if (last_theme_installed != NULL) {
		g_free (last_theme_installed);
	}

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));	
	last_theme_installed = g_strdup (filename);
	
	if (filename == NULL) {
	
		GtkWidget *dialog;

		dialog = hig_dialog_new (GTK_WINDOW (chooser),
					 GTK_DIALOG_MODAL | 
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("No file selected"),
					 "");
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		return;
	}

	install_theme_file (filename, store, GTK_WINDOW (chooser));
	gtk_widget_destroy (chooser);
	g_free (filename);
}

static void
install_new_theme (GtkWidget *button, gpointer data)
{
	GtkListStore *store = data;
	static GtkWidget *chooser = NULL;
	GtkWidget *setup_dialog;
	GtkFileFilter *filter;
	
	setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
	
	chooser = gtk_file_chooser_dialog_new (_("Select Theme Archive"),
					       GTK_WINDOW (setup_dialog),
					       GTK_FILE_CHOOSER_ACTION_OPEN,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       _("_Install"), GTK_RESPONSE_OK,
					       NULL);
	
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Theme archives"));
	gtk_file_filter_add_mime_type (filter, "application/x-tar");
	gtk_file_filter_add_mime_type (filter, "application/x-compressed-tar");
	
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);
	
	gtk_file_chooser_set_show_hidden (GTK_FILE_CHOOSER (chooser), FALSE);

	g_signal_connect (G_OBJECT (chooser), "destroy",
			  G_CALLBACK (gtk_widget_destroyed), &chooser);
	g_signal_connect (G_OBJECT (chooser), "response",
			  G_CALLBACK (theme_install_response), store);

	if (last_theme_installed != NULL) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser),
		                               last_theme_installed);
	}
	gtk_widget_show (chooser);
}

static void
delete_theme (GtkWidget *button, gpointer data)
{
	GtkListStore *store = data;
	GtkWidget *theme_list;
	GtkWidget *theme_list_remote;
	GtkWidget *setup_dialog;
	GtkWidget *del_button;
	GtkWidget *del_button_remote;
	GtkWidget *local_combobox;
	GtkTreeSelection *selection;
        char *dir, *name;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = {0, };
	GtkWidget *dlg;
	char *s;
	gboolean GdmGraphicalThemeRand;
	gboolean selected_warning = FALSE;
	gint selected = -1;

	setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
	theme_list = glade_xml_get_widget (xml, "gg_theme_list");
	theme_list_remote = glade_xml_get_widget (xml, "gg_theme_list_remote");
	del_button = glade_xml_get_widget (xml, "gg_delete_theme");
	del_button_remote = glade_xml_get_widget (xml, "gg_delete_theme_remote");
	local_combobox = glade_xml_get_widget (xml, "gg_mode_combobox");

	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
	}
	else {
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list_remote));
	}

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter)) {
		/* should never get here since the button shuld not be
		 * enabled */
		return;
	}

	GdmGraphicalThemeRand = gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND);

	if (GdmGraphicalThemeRand) {
		gtk_tree_model_get_value (model, &iter,
		                          THEME_COLUMN_SELECTED_LIST, &value);
		
		/* Make sure that the theme is not selected in the
		   single theme mode */
		if (!g_value_get_boolean (&value)) {
			g_value_unset (&value);
			gtk_tree_model_get_value (model, &iter,
						  THEME_COLUMN_SELECTED, &value);
			
			if (g_value_get_boolean (&value))
				selected_warning = TRUE;
		}
	}
	else {
		gtk_tree_model_get_value (model, &iter,
		                          THEME_COLUMN_SELECTED,
		                          &value);

		/* We might have "Random form selected" option selected, but no
		   active themes there hence defaulting to "Selected only" 
		   Lets make this theme delete-able from "Random from selected"
		   mode in this case */
		selected = gtk_combo_box_get_active (GTK_COMBO_BOX (local_combobox));
		
		if (g_value_get_boolean (&value) && selected == RANDOM_THEME)
			selected_warning = TRUE;
	}
	
	/* The theme we trying to delete in "Random from selected"
	   mode is currently in use in the "Selected only" mode 
	   so lets warn the user about it */
	if (selected_warning == TRUE) {
		dlg = hig_dialog_new (GTK_WINDOW (setup_dialog) /* parent */,
				      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT /* flags */,
				      GTK_MESSAGE_WARNING,
				      GTK_BUTTONS_OK,
				      _("Theme active in \"Selected only\" mode"),
				      _("This theme cannot be deleted at this point. "
					"If you wish to delete this theme switch to "
					"\"Selected only\" mode, and deselect it by "
					"choosing a different theme."));
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);			    
	}

	/* Do not allow deleting of selected theme */
	if (g_value_get_boolean (&value)) {
		/* should never get here since the button shuld not be
		 * enabled */
		g_value_unset (&value);
		return;
	}
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_NAME,
				  &value);
	name = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_DIR,
				  &value);
	dir = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	s = g_strdup_printf (_("Remove the \"%s\" theme?"),
			     name);
	dlg = hig_dialog_new (GTK_WINDOW (setup_dialog),
			      GTK_DIALOG_MODAL | 
			      GTK_DIALOG_DESTROY_WITH_PARENT,
			      GTK_MESSAGE_WARNING,
			      GTK_BUTTONS_NONE,
			      s,
		 _("If you choose to remove the theme, it will be permanently lost."));
	g_free (s);

	button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
	gtk_dialog_add_action_widget (GTK_DIALOG (dlg), button, GTK_RESPONSE_NO);
	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
	gtk_widget_show (button);

	button = gtk_button_new_from_stock (_("_Remove Theme"));
	gtk_dialog_add_action_widget (GTK_DIALOG (dlg), button, GTK_RESPONSE_YES);
	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
	gtk_widget_show (button);
	
	gtk_dialog_set_default_response (GTK_DIALOG (dlg),
					 GTK_RESPONSE_YES);

	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_YES) {
		char *theme_dir = get_theme_dir ();
		char *cwd = g_get_current_dir ();
		if (g_chdir (theme_dir) == 0 &&
		    /* this is a security sanity check, since we're doing rm -fR */
		    strchr (dir, '/') == NULL) {
			/* HACK! */
			DIR *dp;
			char *argv[4];
			GtkTreeIter *select_iter = NULL;
			argv[0] = "/bin/rm";
			argv[1] = "-fR";
			argv[2] = g_strconcat ("./", dir, NULL);
			argv[3] = NULL;
			simple_spawn_sync (argv);
			g_free (argv[2]);

			/* Update the list */
			gtk_list_store_clear (store);

			dp = opendir (theme_dir);

			if (dp != NULL) {
				select_iter = read_themes (store, theme_dir, dp, 
							   selected_theme);
				closedir (dp);
			}

			if (select_iter != NULL) {
				gtk_tree_selection_select_iter (selection, select_iter);
				g_free (select_iter);
			}

		}
		g_chdir (cwd);
		g_free (cwd);
		g_free (theme_dir);
	}
	gtk_widget_destroy (dlg);

	g_free (name);
	g_free (dir);
}

static gboolean
xserver_entry_timeout (GtkWidget *entry)
{
	GtkWidget *mod_combobox;
	GSList *li;
	const char *key  = g_object_get_data (G_OBJECT (entry), "key");
	const char *text = gtk_entry_get_text (GTK_ENTRY (entry));
	gchar *string_old = NULL;
	gchar *section;

	mod_combobox    = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");

	/* Get xserver section to update */
	section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (mod_combobox));

	for (li = xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (strcmp (ve_sure_string (svr->id), ve_sure_string (section)) == 0) {

			if (strcmp (ve_sure_string (key),
                            ve_sure_string (GDM_KEY_SERVER_NAME)) == 0)
				string_old = svr->name;
			else if (strcmp (ve_sure_string (key),
                                 ve_sure_string (GDM_KEY_SERVER_COMMAND)) == 0)
				string_old = svr->command;

			/* Update this servers configuration */
			if (strcmp (ve_sure_string (string_old),
                            ve_sure_string (text)) != 0) {
				if (strcmp (ve_sure_string (key),
                                    ve_sure_string (GDM_KEY_SERVER_NAME)) == 0) {
					if (svr->name)
						g_free (svr->name);
					svr->name = g_strdup (text);
				} else if (strcmp (ve_sure_string (key),
                                           ve_sure_string (GDM_KEY_SERVER_COMMAND)) == 0) {
					if (svr->command)
						g_free (svr->command);
					svr->command = g_strdup (text);;
				}
				update_xserver (section, svr);
			}
			break;
		}
	}
	g_free (section);

	return FALSE;
}

static gboolean
xserver_priority_timeout (GtkWidget *entry)
{
	GtkWidget *mod_combobox;
	GSList *li;
	const char *key  = g_object_get_data (G_OBJECT (entry), "key");
	gint value = 0;
	gchar *section;

	mod_combobox    = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");

	/* Get xserver section to update */
	section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (mod_combobox));
	
	for (li = xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (strcmp (ve_sure_string (svr->id), ve_sure_string (section)) == 0) {
			gint new_value;
			
			if (strcmp (ve_sure_string (key),
				    ve_sure_string (GDM_KEY_SERVER_PRIORITY)) == 0)
				value = svr->priority;
			
			/* Update this servers configuration */
			new_value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry));
			if (new_value != value) {
				svr->priority = new_value;				
				update_xserver (section, svr);			
			}
			break;
		}
	}
	g_free (section);

	return FALSE;
}

static gboolean
xserver_toggle_timeout (GtkWidget *toggle)
{
	GtkWidget *mod_combobox;
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	GSList     *li;
	gboolean   val = FALSE;
	gchar      *section;

	mod_combobox    = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");

	/* Get xserver section to update */
	section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (mod_combobox));

	/* Locate this server's section */
	for (li = xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (strcmp (ve_sure_string (svr->id), ve_sure_string (section)) == 0) {

			if (strcmp (ve_sure_string (key),
                            ve_sure_string (GDM_KEY_SERVER_HANDLED)) == 0) {
				val = svr->handled;
			} else if (strcmp (ve_sure_string (key),
                                   ve_sure_string (GDM_KEY_SERVER_FLEXIBLE)) == 0) {
				val = svr->flexible;
			}

			/* Update this servers configuration */
			if ( ! bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
				gboolean new_val = GTK_TOGGLE_BUTTON (toggle)->active;

				if (strcmp (ve_sure_string (key),
                                    ve_sure_string (GDM_KEY_SERVER_HANDLED)) == 0)
					svr->handled = new_val;
				else if (strcmp (ve_sure_string (key),
                                         ve_sure_string (GDM_KEY_SERVER_FLEXIBLE)) == 0)
					svr->flexible = new_val;

				update_xserver (section, svr);
			}
			break;
		}
	}
	g_free (section);

	return FALSE;
}

static void
xserver_toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 500, xserver_toggle_timeout);
}

static void
xserver_entry_changed (GtkWidget *entry)
{
	run_timeout (entry, 500, xserver_entry_timeout);
}

static void
xserver_priority_changed (GtkWidget *entry)
{
	run_timeout (entry, 500, xserver_priority_timeout);
}

static void
xserver_append_combobox (GdmXserver *xserver, GtkComboBox *combobox)
{
	gtk_combo_box_append_text (combobox, (xserver->id));
}

static void
xserver_populate_combobox (GtkComboBox* combobox)
{
	gint i,j;

	/* Get number of items in combobox */
	i = gtk_tree_model_iter_n_children(
	        gtk_combo_box_get_model (GTK_COMBO_BOX (combobox)), NULL);

	/* Delete all items from combobox */
	for (j = 0; j < i; j++) {
		gtk_combo_box_remove_text(combobox,0);
	}

	/* Populate combobox with list of current servers */
	g_slist_foreach (xservers, (GFunc) xserver_append_combobox, combobox);
}

static void
xserver_init_server_list ()
{
	/* Get Widgets from glade */
	GtkWidget *treeview = glade_xml_get_widget (xml_xservers, "xserver_tree_view");
	GtkWidget *remove_button = glade_xml_get_widget (xml_xservers, "xserver_remove_button");

	/* create list store */
	GtkListStore *store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
	                            G_TYPE_INT    /* virtual terminal */,
	                            G_TYPE_STRING /* server type */,
	                            G_TYPE_STRING /* options */);

	/* Read all xservers to start from configuration */
	xservers_get_displays (store);
	gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
	                         GTK_TREE_MODEL (store));
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (treeview), TRUE);
	gtk_widget_set_sensitive (remove_button, FALSE);
}

static void
xserver_init_servers ()
{
    GtkWidget *remove_button;

    /* Init widget states */
    xserver_init_server_list();

    remove_button = glade_xml_get_widget (xml_xservers, "xserver_remove_button");
    gtk_widget_set_sensitive (remove_button, FALSE);
}

static void
xserver_row_selected(GtkTreeSelection *selection, gpointer data)
{
    GtkWidget *remove_button;
    
    remove_button = glade_xml_get_widget (xml_xservers, "xserver_remove_button");
    gtk_widget_set_sensitive (remove_button, TRUE);
}

/*
 * Remove a server from the list of servers to start (not the same as
 * deleting a server definition)
 */
static void
xserver_remove_display (gpointer data)
{
	GtkWidget *treeview, *combo;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gint vt;
        char vt_value[3];

        treeview = glade_xml_get_widget (xml_xservers, "xserver_tree_view");

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
	        GKeyFile *cfg;
		GKeyFile *custom_cfg;
		gchar *defaultval;
		gchar *key;

		cfg = gdm_common_config_load (config_file, NULL);
		custom_cfg = gdm_common_config_load (custom_config_file, NULL);

		combo = glade_xml_get_widget (xml_add_xservers, "xserver_server_combobox");

		/* Update config */
		gtk_tree_model_get (model, &iter, XSERVER_COLUMN_VT, &vt, -1);

		g_snprintf (vt_value,  sizeof (vt_value), "%d", vt);
		key = g_strconcat (GDM_KEY_SECTION_SERVERS, "/", vt_value, "=", NULL);

		defaultval = NULL;
		gdm_common_config_get_string (cfg, key, &defaultval, NULL);

		/*
		 * If the value is in the default config file, set it to inactive in
		 * the custom config file, else delete it
		 */
		if (! ve_string_empty (defaultval)) {
			gdm_common_config_set_string (custom_cfg, key, "inactive");
		} else {
			gdm_common_config_remove_key (custom_cfg, key, NULL);
		}
		g_free (defaultval);

		gdm_common_config_save (custom_cfg, custom_config_file, NULL);
		g_key_file_free (custom_cfg);
		g_key_file_free (cfg);

		/* Update gdmsetup */
		xserver_init_server_list ();
		xserver_update_delete_sensitivity ();
	}
}

/* Add a display to the list of displays to start */
static void
xserver_add_display (gpointer data)
{
        GKeyFile *cfg;
        GKeyFile *custom_cfg;
	GtkWidget *spinner, *combo, *entry, *button;
	gchar *string;
	gchar *defaultval;
	char spinner_value[3], *key;

        cfg = gdm_common_config_load (config_file, NULL);
	custom_cfg = gdm_common_config_load (custom_config_file, NULL);

	/* Get Widgets from glade */
	spinner  = glade_xml_get_widget (xml_add_xservers, "xserver_spin_button");
	entry    = glade_xml_get_widget (xml_add_xservers, "xserver_options_entry");
	combo    = glade_xml_get_widget (xml_add_xservers, "xserver_server_combobox");
	button   = glade_xml_get_widget (xml_xservers, "xserver_add_button");

	/* String to add to config */
	g_snprintf (spinner_value,  sizeof (spinner_value), "%d",
	            gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spinner)));

	key = g_strconcat (GDM_KEY_SECTION_SERVERS, "/", spinner_value, "=", NULL);
	if (! ve_string_empty (gtk_entry_get_text (GTK_ENTRY (entry)))) {
		string = g_strconcat (gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo)),
		                      " ", gtk_entry_get_text (GTK_ENTRY (entry)),
		                      NULL);
	} else {
		string = g_strdup (gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo)));
	}

	defaultval = NULL;
	gdm_common_config_get_string (cfg, key, &defaultval, NULL);

	/* Add to config */
	if (strcmp (ve_sure_string (defaultval), ve_sure_string (string)) == 0) {
		gdm_common_config_remove_key (custom_cfg, key, NULL);
	} else {
		gdm_common_config_set_string (custom_cfg, key, ve_sure_string(string));
	}

	gdm_common_config_save (custom_cfg, custom_config_file, NULL);
	g_key_file_free (custom_cfg);
	g_key_file_free (cfg);

	/* Reinitialize gdmsetup */
	xserver_init_servers ();
	xserver_update_delete_sensitivity ();

	/* Free memory */
	g_free (defaultval);
	g_free (string);
	g_free (key);
}

static void
xserver_add_button_clicked (void)
{
	static GtkWidget *dialog = NULL;
	GtkWidget *options_entry;
	GtkWidget *server_combobox;
	GtkWidget *vt_spinbutton;
	GtkWidget *parent;
	GtkWidget *treeview;
	GtkTreeSelection *selection;
	GtkTreeModel *treeview_model;
	GtkTreeIter treeview_iter;
	guint activate_signal_id;
	gboolean res;
		
	if (dialog == NULL) {
		parent = glade_xml_get_widget (xml_xservers, "xserver_dialog");
		dialog = glade_xml_get_widget (xml_add_xservers, "add_xserver_dialog");

		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	}

	vt_spinbutton = glade_xml_get_widget (xml_add_xservers, "xserver_spin_button");
	server_combobox = glade_xml_get_widget (xml_add_xservers, "xserver_server_combobox");
	options_entry = glade_xml_get_widget (xml_add_xservers, "xserver_options_entry");

	activate_signal_id = g_signal_connect (G_OBJECT (vt_spinbutton), "activate",
	                                       G_CALLBACK (vt_spinbutton_activate),
	                                       (gpointer) dialog);
	
	xserver_populate_combobox (GTK_COMBO_BOX (server_combobox));
	
	gtk_widget_grab_focus (vt_spinbutton);
		
	treeview = glade_xml_get_widget (xml_xservers, "xserver_tree_view");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

	/* set default values */
	if (gtk_tree_selection_get_selected (selection, &treeview_model, &treeview_iter)) {

		GtkTreeModel *combobox_model;
		GtkTreeIter combobox_iter;
		gchar *label;
		gchar *server;
		gint vt;

		gtk_tree_model_get (treeview_model, &treeview_iter, XSERVER_COLUMN_VT, &vt, -1);	
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (vt_spinbutton), vt);

		gtk_tree_model_get (GTK_TREE_MODEL (treeview_model), &treeview_iter,
				    XSERVER_COLUMN_SERVER, &server, -1);
		combobox_model = gtk_combo_box_get_model (GTK_COMBO_BOX (server_combobox));

		for (res = gtk_tree_model_get_iter_first (combobox_model, &combobox_iter); res; res = gtk_tree_model_iter_next (combobox_model, &combobox_iter)) {
	      		gtk_tree_model_get (combobox_model, &combobox_iter, 0, &label, -1);
	      		if (strcmp (ve_sure_string (label), ve_sure_string (server)) == 0) {
				gtk_combo_box_set_active_iter (GTK_COMBO_BOX (server_combobox), &combobox_iter);
      			}
      			g_free (label);
    		}

		gtk_tree_model_get (GTK_TREE_MODEL (treeview_model), &treeview_iter,
				    XSERVER_COLUMN_OPTIONS, &server, -1);
		if (server != NULL)
			gtk_entry_set_text (GTK_ENTRY (options_entry), server);
	} else {
		gint high_value = 0;
		gint vt;

		for (res = gtk_tree_model_get_iter_first (treeview_model, &treeview_iter); res; res = gtk_tree_model_iter_next (treeview_model, &treeview_iter)) {
	      		gtk_tree_model_get (treeview_model, &treeview_iter, XSERVER_COLUMN_VT, &vt, -1);
	      		if (high_value < vt) {
				high_value = vt;
      			}
    		}
		
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (vt_spinbutton), ++high_value);
		gtk_combo_box_set_active (GTK_COMBO_BOX (server_combobox), 0);
		gtk_entry_set_text (GTK_ENTRY (options_entry), "");
	}
	
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		xserver_add_display (NULL);
	}
	g_signal_handler_disconnect (vt_spinbutton, activate_signal_id);
	gtk_widget_hide (dialog);
}

/*
 * TODO: This section needs a little work until it is ready (mainly config
 * section modifications) 
 * Create a server definition (not the same as removing a server
 * from the list of servers to start)
 */
#ifdef GDM_TODO_CODE
static void
xserver_create (gpointer data)
{
	/* VeConfig *cfg; */
	gboolean success;

	/* Init Widgets */
	GtkWidget *frame, *modify_combobox;
	GtkWidget *name_entry, *command_entry;
	GtkWidget *handled_check, *flexible_check;
	GtkWidget *greeter_radio, *chooser_radio;
	GtkWidget *create_button, *delete_button;
	GtkWidget *priority_spinbutton;

	/* Get Widgets from glade */
	frame           = glade_xml_get_widget (xml, "xserver_modify_frame");
	name_entry      = glade_xml_get_widget (xml, "xserver_name_entry");
	command_entry   = glade_xml_get_widget (xml, "xserver_command_entry");
	priority_spinbutton = glade_xml_get_widget(xml, "xserv_priority_spinbutton");
	handled_check   = glade_xml_get_widget (xml, "xserver_handled_checkbutton");
	flexible_check  = glade_xml_get_widget (xml, "xserver_flexible_checkbutton");
	greeter_radio   = glade_xml_get_widget (xml, "xserver_greeter_radiobutton");
	chooser_radio   = glade_xml_get_widget (xml, "xserver_chooser_radiobutton");
	modify_combobox = glade_xml_get_widget (xml, "xserver_mod_combobox");
	create_button   = glade_xml_get_widget (xml, "xserver_create_button");
	delete_button   = glade_xml_get_widget (xml, "xserver_delete_button");

	gtk_combo_box_append_text (GTK_COMBO_BOX (modify_combobox),
	                           "New Server");

	/* TODO: Create a new section for this server */
	/* TODO: Write this value to the config and update xservers list */
	/* cfg = gdm_common_config_load (custom_config_file, NULL); */
	success = FALSE;
	/* success = ve_config_add_section (cfg, SECTION_NAME); */

	if (success)
	{
		gint i;

		/* Update settings for new server */
		gtk_widget_set_sensitive (frame, TRUE);
		gtk_widget_set_sensitive (delete_button, TRUE);
		gtk_widget_grab_focus (name_entry);
		gtk_entry_set_text (GTK_ENTRY (name_entry), "New Server");
		gtk_editable_select_region (GTK_EDITABLE (name_entry), 0, -1);
		gtk_entry_set_text (GTK_ENTRY (command_entry), X_SERVER);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (greeter_radio),
		                              TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chooser_radio),
		                              FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (handled_check),
		                              TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flexible_check),
		                              FALSE);

		/* Select the new server in the combobox */
		i = gtk_tree_model_iter_n_children (
		   gtk_combo_box_get_model (GTK_COMBO_BOX (modify_combobox)), NULL) - 1;
		gtk_combo_box_set_active (GTK_COMBO_BOX (modify_combobox), i);
	}
}
#endif

static void
xserver_init_definitions ()
{
	GtkWidget *style_combobox;
	GtkWidget *modify_combobox;

	style_combobox  = glade_xml_get_widget (xml_xservers, "xserver_style_combobox");
	modify_combobox = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");

	xserver_populate_combobox (GTK_COMBO_BOX (modify_combobox));

	gtk_combo_box_set_active (GTK_COMBO_BOX (style_combobox), 0);	
	init_servers_combobox (gtk_combo_box_get_active (GTK_COMBO_BOX (style_combobox)));
}

/*
 * Deletes a server definition (not the same as removing a server
 * from the list of servers to start)
 *
 * NOTE, now that we have the %{datadir}/gdm/defaults.conf and
 * %{etc}/gdm/custom.conf files, this will need to work like the displays.
 * So if you want to delete something that is defaults.conf you will need
 * to write a new value to custom.conf section for this xserver like
 * "inactive=true".  For this to work, daemon/gdmconfig.c will also need
 * to be modified so that it doesn't bother loading xservers that are
 * marked as inactive in the custom.conf file.  As I said, this
 * is the same way the displays already work so the code should be
 * similar.  Or perhaps it makes more sense to just not allow
 * deleting of server-foo sections as defined in the defaults.conf
 * file.  If the user doesn't want to use them, they can always
 * create new server-foo sections in custom.conf and define their
 * displays to only use the ones they define. 
 */
#ifdef GDM_UNUSED_CODE
static void
xserver_delete (gpointer data)
{
	gchar temp_string;
	/* Get xserver section to delete */
	GtkWidget *combobox;
	gchar *section;
	/* Delete xserver section */
	VeConfig *custom_cfg;

	combobox = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");
	section = gtk_combo_box_get_active_text ( GTK_COMBO_BOX (combobox));
	custom_cfg = ve_config_get (custom_config_file);

	temp_string = g_strconcat (GDM_KEY_SERVER_PREFIX, section, NULL);
	ve_config_delete_section (custom_cfg, temp_string);
	g_free (temp_string);

	/* Reinitialize definitions */
	xserver_init_definitions();
}
#endif

static void
setup_xserver_support (GladeXML *xml_xservers)
{
	GtkWidget *command_entry;
	GtkWidget *priority_spinbutton;
	GtkWidget *name_entry;
	GtkWidget *handled_check;
	GtkWidget *flexible_check;
	GtkWidget *create_button;
	GtkWidget *delete_button;
	GtkWidget *remove_button;
	GtkWidget *servers_combobox;
	GtkWidget *style_combobox;
	GtkWidget *treeview;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	/* Initialize the xserver settings */
	xserver_init_definitions();
	xserver_init_servers();

	/* TODO: In the future, resolution/refresh rate configuration */
	/* setup_xrandr_support (); */

	/* Get Widgets from glade */
	treeview        = glade_xml_get_widget (xml_xservers, "xserver_tree_view");
	name_entry      = glade_xml_get_widget (xml_xservers, "xserver_name_entry");
	command_entry   = glade_xml_get_widget (xml_xservers, "xserver_command_entry");
	priority_spinbutton = glade_xml_get_widget(xml_xservers, "xserv_priority_spinbutton");
	handled_check   = glade_xml_get_widget (xml_xservers, "xserver_handled_checkbutton");
	flexible_check  = glade_xml_get_widget (xml_xservers, "xserver_flexible_checkbutton");
	style_combobox  = glade_xml_get_widget (xml_xservers, "xserver_style_combobox");
	servers_combobox = glade_xml_get_widget (xml_xservers, "xserver_mod_combobox");
	create_button   = glade_xml_get_widget (xml_xservers, "xserver_createbutton");
	delete_button   = glade_xml_get_widget (xml_xservers, "xserver_deletebutton");
	remove_button   = glade_xml_get_widget (xml_xservers, "xserver_remove_button");

	glade_helper_tagify_label (xml_xservers, "xserver_informationlabel", "i");
	glade_helper_tagify_label (xml_xservers, "xserver_informationlabel", "small");
	glade_helper_tagify_label (xml_xservers, "server_to_start_label", "b");
	glade_helper_tagify_label (xml_xservers, "server_settings_label", "b");
	
	/* Setup Virtual terminal column in servers to start frame */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_title (column, "VT");
	gtk_tree_view_column_set_attributes (column, renderer,
	                                    "text", XSERVER_COLUMN_VT,
	                                     NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Setup Server column in servers to start frame */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_title (column, "Server");
	gtk_tree_view_column_set_attributes (column, renderer,
	                                     "text", XSERVER_COLUMN_SERVER,
	                                     NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Setup Options column in servers to start frame*/
	column   = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_title (column, "Options");
	gtk_tree_view_column_set_attributes (column, renderer,
	                                     "text", XSERVER_COLUMN_OPTIONS,
	                                     NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Setup tree selections */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	/* Register these items with keys */
	g_object_set_data_full (G_OBJECT (servers_combobox), "key",
	                        g_strdup (GDM_KEY_SERVER_PREFIX),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (name_entry), "key",
	                        g_strdup (GDM_KEY_SERVER_NAME),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (command_entry), "key",
	                        g_strdup (GDM_KEY_SERVER_COMMAND),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (handled_check), "key",
	                        g_strdup (GDM_KEY_SERVER_HANDLED),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (flexible_check), "key",
	                        g_strdup (GDM_KEY_SERVER_FLEXIBLE),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (style_combobox), "key",
	                        g_strdup (GDM_KEY_SERVER_CHOOSER),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (priority_spinbutton), "key",
				g_strdup (GDM_KEY_SERVER_PRIORITY),
	                        (GDestroyNotify) g_free);
	/* Signals Handlers */
    	g_signal_connect (G_OBJECT (name_entry), "changed",
	                  G_CALLBACK (xserver_entry_changed),NULL);
    	g_signal_connect (G_OBJECT (command_entry), "changed",
	                  G_CALLBACK (xserver_entry_changed), NULL);
	g_signal_connect (G_OBJECT (handled_check), "toggled",
	                  G_CALLBACK (xserver_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (flexible_check), "toggled",
	                  G_CALLBACK (xserver_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (servers_combobox), "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (G_OBJECT (style_combobox), "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (G_OBJECT (remove_button), "clicked",
	                  G_CALLBACK (xserver_remove_display), NULL);
	g_signal_connect (G_OBJECT (selection), "changed",
	                  G_CALLBACK (xserver_row_selected), NULL);
	g_signal_connect (G_OBJECT (priority_spinbutton), "value_changed",
	                  G_CALLBACK (xserver_priority_changed), NULL);
	
	/* TODO: In the future, allow creation & delection of servers
	g_signal_connect (create_button, "clicked",
			  G_CALLBACK (xserver_create), NULL);
  	g_signal_connect (delete_button, "clicked",
	                  G_CALLBACK (xserver_delete), NULL);
	*/
}

static void
xserver_button_clicked (void)
{
	static GtkWidget *dialog = NULL;
	int response;

	if (dialog == NULL) {

		GtkWidget *parent;
		GtkWidget *button;
	
		xml_xservers = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "xserver_dialog", NULL);

		xml_add_xservers = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "add_xserver_dialog", NULL);

		parent = glade_xml_get_widget (xml, "setup_dialog");
		dialog = glade_xml_get_widget (xml_xservers, "xserver_dialog");
		button = glade_xml_get_widget (xml_xservers, "xserver_add_button");

		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

		g_signal_connect (G_OBJECT (button), "clicked",
		                  G_CALLBACK (xserver_add_button_clicked), NULL);

		setup_xserver_support (xml_xservers);
	}

	do {
		response = gtk_dialog_run (GTK_DIALOG (dialog));
		if (response == GTK_RESPONSE_HELP) {
			g_spawn_command_line_sync ("gnome-open ghelp:gdm", NULL, NULL,
							NULL, NULL);
		}
	} while (response != GTK_RESPONSE_CLOSE &&
                 response != GTK_RESPONSE_DELETE_EVENT);

	gtk_widget_hide (dialog);
}

static void
setup_radio_group (const gchar *name,
		   const gchar *key, gint position)
{
	GtkWidget *radio;
	gint val;
	
	radio = glade_xml_get_widget (xml, name);
	val   = gdm_config_get_int ((gchar *)key);
	
	if (val == position)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), FALSE);
	
	g_object_set_data_full (G_OBJECT (radio), "key", g_strdup (key),
	                        (GDestroyNotify) g_free);
			
    	g_signal_connect (G_OBJECT (radio), "toggled",
			  G_CALLBACK (radiogroup_toggled), NULL);	
}

static void
setup_security_tab (void)
{
	GtkWidget *checkbox;
	GtkWidget *label;
	GtkWidget *XDMCPbutton;

	/* Setup Local administrator login setttings */
	setup_notify_toggle ("allowroot", GDM_KEY_ALLOW_ROOT);

	/* Setup Remote administrator login setttings */
	setup_notify_toggle ("allowremoteroot", GDM_KEY_ALLOW_REMOTE_ROOT);

	/* Setup Enable debug message to system log */
	setup_notify_toggle ("enable_debug", GDM_KEY_DEBUG);

	/* Setup Deny TCP connections to Xserver */
	setup_notify_toggle ("disallow_tcp", GDM_KEY_DISALLOW_TCP);

	/* Setup never place cookies on NFS */
	setup_notify_toggle ("never_cookies_NFS_checkbutton", GDM_KEY_NEVER_PLACE_COOKIES_ON_NFS);

	/* Setup Retry delay */
	setup_intspin ("retry_delay", GDM_KEY_RETRY_DELAY);

	/* Bold the Enable automatic login label */
	checkbox = glade_xml_get_widget (xml, "autologin");
	label = gtk_bin_get_child (GTK_BIN (checkbox));
	g_object_set (G_OBJECT (label), "use_markup", TRUE, NULL);

	/* Bold the Enable timed login label */
	checkbox = glade_xml_get_widget (xml, "timedlogin");
	label = gtk_bin_get_child (GTK_BIN (checkbox));
	g_object_set (G_OBJECT (label), "use_markup", TRUE, NULL);

	/* Setup Enable automatic login */
 	setup_user_combobox ("autologin_combo",
			     GDM_KEY_AUTOMATIC_LOGIN);
	setup_notify_toggle ("autologin", GDM_KEY_AUTOMATIC_LOGIN_ENABLE);

	/* Setup Enable timed login */
 	setup_user_combobox ("timedlogin_combo",
			     GDM_KEY_TIMED_LOGIN);
	setup_intspin ("timedlogin_seconds", GDM_KEY_TIMED_LOGIN_DELAY);
	setup_notify_toggle ("timedlogin", GDM_KEY_TIMED_LOGIN_ENABLE);

	/* Setup Allow remote timed logins */
	setup_notify_toggle ("allowremoteauto", GDM_KEY_ALLOW_REMOTE_AUTOLOGIN);

	/* Setup check dir owner */
	setup_notify_toggle ("check_dir_owner_checkbutton", GDM_KEY_CHECK_DIR_OWNER);	
	
	/* Setup Relax permissions */
	setup_radio_group ("relax_permissions0_radiobutton", GDM_KEY_RELAX_PERM, 0);
	setup_radio_group ("relax_permissions1_radiobutton", GDM_KEY_RELAX_PERM, 1);
	setup_radio_group ("relax_permissions2_radiobutton", GDM_KEY_RELAX_PERM, 2);
	
	/* Setup MinimalUID */
	setup_intspin ("minimal_uid_spinbutton", GDM_KEY_MINIMAL_UID);

	/* Setup Configure XDMCP button */
	XDMCPbutton = glade_xml_get_widget (xml, "config_xserverbutton");
	setup_xdmcp_support ();
	g_signal_connect (G_OBJECT (XDMCPbutton), "clicked",
	                  G_CALLBACK (xserver_button_clicked), NULL);
}

static GList *
get_file_list_from_uri_list (gchar *uri_list)
{
	GList *list = NULL;
	gchar **uris = NULL;
	gint index;

	if (uri_list == NULL) {
		return NULL;
	}
	
	uris = g_uri_list_extract_uris (uri_list);
	
	for (index = 0; uris[index] != NULL; index++) {
		
		gchar *filename;

		if (g_path_is_absolute (uris[index]) == TRUE) {
			filename = g_strdup (uris[index]);
		}
		else {
			gchar *host = NULL;
			
			filename = g_filename_from_uri (uris[index], &host, NULL);
			
			/* Sorry, we can only accept local files. */
			if (host != NULL) {
				g_free (filename);
				g_free (host);
				filename = NULL;
			}
		}

		if (filename != NULL) {
			list = g_list_prepend (list, filename);
		}
	}
	g_strfreev (uris);
	return g_list_reverse (list);
}

static void  
theme_list_drag_data_received  (GtkWidget        *widget,
                                GdkDragContext   *context,
                                gint              x,
                                gint              y,
                                GtkSelectionData *data,
                                guint             info,
                                guint             time,
                                gpointer          extra_data)
{
	GtkWidget *parent;
	GtkWidget *theme_list;
	GtkListStore *store;
	GList *list;
	
	parent = glade_xml_get_widget (xml, "setup_dialog");
	theme_list = glade_xml_get_widget (xml, "gg_theme_list");
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list)));

	gtk_drag_finish (context, TRUE, FALSE, time);

	for (list = get_file_list_from_uri_list ((gchar *)data->data); list != NULL; list = list-> next) {

		GtkWidget *prompt;
		gchar *base;
		gchar *mesg;
		gchar *detail;
		gint response;

		base = g_path_get_basename ((gchar *)list->data);
		mesg = g_strdup_printf (_("Install the theme from '%s'?"), base);
		detail = g_strdup_printf (_("Select install to add the theme from the file '%s'."), (gchar *)list->data); 
		
		prompt = hig_dialog_new (GTK_WINDOW (parent),
					 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 mesg,
					 detail);

		gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-cancel", GTK_RESPONSE_CANCEL); 
		gtk_dialog_add_button (GTK_DIALOG (prompt), _("_Install"), GTK_RESPONSE_OK);

		response = gtk_dialog_run (GTK_DIALOG (prompt));
		gtk_widget_destroy (prompt);
		g_free (mesg);

		if (response == GTK_RESPONSE_OK) {
			install_theme_file (list->data, store, GTK_WINDOW (parent));
		}
	}
}

static gboolean
theme_list_equal_func (GtkTreeModel * model,
                       gint column,
                       const gchar * key,
                       GtkTreeIter * iter,
                       gpointer search_data)
{
	gboolean results = TRUE;
	gchar *name;

	gtk_tree_model_get (model, iter, THEME_COLUMN_MARKUP, &name, -1);

	if (name != NULL) {
		gchar * casefold_key;
		gchar * casefold_name;
	
		casefold_key = g_utf8_casefold (key, -1);
		casefold_name = g_utf8_casefold (name, -1);

		if ((casefold_key != NULL) &&
		    (casefold_name != NULL) && 
		    (strstr (casefold_name, casefold_key) != NULL)) {
			results = FALSE;
		}
		g_free (casefold_key);
		g_free (casefold_name);
		g_free (name);
	}
	return results;
}


static void
setup_local_themed_settings (void)
{
	gboolean GdmGraphicalThemeRand;
	DIR *dir;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTreeIter *select_iter = NULL;
	GtkWidget *color_colorbutton;
	GtkWidget *style_label;
	GtkWidget *theme_label;
	GtkSizeGroup *size_group;
	char *theme_dir;
	
	GtkWidget *theme_list = glade_xml_get_widget (xml, "gg_theme_list");
	GtkWidget *button = glade_xml_get_widget (xml, "gg_install_new_theme");
	GtkWidget *del_button = glade_xml_get_widget (xml, "gg_delete_theme");
	GtkWidget *mode_combobox = glade_xml_get_widget (xml, "gg_mode_combobox");

	style_label = glade_xml_get_widget (xml, "local_stylelabel");
	theme_label = glade_xml_get_widget (xml, "local_theme_label");
	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, style_label);
	gtk_size_group_add_widget (size_group, theme_label);
	
	color_colorbutton = glade_xml_get_widget (xml, "local_background_theme_colorbutton");

	g_object_set_data (G_OBJECT (color_colorbutton), "key",
	                   GDM_KEY_GRAPHICAL_THEMED_COLOR);

	setup_greeter_color ("local_background_theme_colorbutton", 
	                     GDM_KEY_GRAPHICAL_THEMED_COLOR);

	theme_dir = get_theme_dir ();

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (theme_list), TRUE);

	selected_theme  = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME);
	selected_themes = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEMES);

	/* FIXME: If a theme directory contains the string GDM_DELIMITER_THEMES
		  in the name, then this theme won't work when trying to load as it
		  will be perceived as two different themes seperated by
		  GDM_DELIMITER_THEMES.  This can be fixed by setting up an escape
		  character for it, but I'm not sure if directories can have the
		  slash (/) character in them, so I just made GDM_DELIMITER_THEMES
		  equal to "/:" instead. */

	GdmGraphicalThemeRand = gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND);

	/* create list store */
	store = gtk_list_store_new (THEME_NUM_COLUMNS,
				    G_TYPE_BOOLEAN /* selected theme */,
				    G_TYPE_BOOLEAN /* selected themes */,
				    G_TYPE_STRING /* dir */,
				    G_TYPE_STRING /* file */,
				    GDK_TYPE_PIXBUF /* preview */,
				    G_TYPE_STRING /* markup */,
				    G_TYPE_STRING /* name */,
				    G_TYPE_STRING /* desc */,
				    G_TYPE_STRING /* author */,
				    G_TYPE_STRING /* copyright */);

	/* Register theme mode combobox */
	g_object_set_data_full (G_OBJECT (mode_combobox), "key",
		g_strdup (GDM_KEY_GRAPHICAL_THEME_RAND),
		(GDestroyNotify) g_free);

	/* Signals */
	g_signal_connect (mode_combobox, "changed",
		G_CALLBACK (combobox_changed), NULL);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (install_new_theme), store);
	g_signal_connect (del_button, "clicked",
			  G_CALLBACK (delete_theme), store);

	/* Init controls */
	gtk_widget_set_sensitive (del_button, FALSE);
	gtk_combo_box_set_active (GTK_COMBO_BOX (mode_combobox),
		GdmGraphicalThemeRand);

	/* Read all Themes from directory and store in tree */
	dir = opendir (theme_dir);
	if (dir != NULL) {
		select_iter = read_themes (store, theme_dir, dir,
					   selected_theme);
		closedir (dir);
	}
	g_free (theme_dir);
	gtk_tree_view_set_model (GTK_TREE_VIEW (theme_list), 
				 GTK_TREE_MODEL (store));

	/* The radio toggle column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_toggle_new ();
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer),
					    TRUE);
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (selected_toggled), store);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
		"active", THEME_COLUMN_SELECTED, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);
	gtk_tree_view_column_set_visible(column, !GdmGraphicalThemeRand);

	/* The checkbox toggle column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_toggle_new ();
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer),
		FALSE);
	g_signal_connect (G_OBJECT (renderer), "toggled",
		G_CALLBACK (selected_toggled), store);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer, "active",
		THEME_COLUMN_SELECTED_LIST, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);
	gtk_tree_view_column_set_visible(column, GdmGraphicalThemeRand);

	/* The preview column */
	column   = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", THEME_COLUMN_SCREENSHOT,
                                             NULL);
	/* The markup column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
		"markup", THEME_COLUMN_MARKUP, NULL);
	gtk_tree_view_column_set_spacing (column, 6);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
	                                      THEME_COLUMN_MARKUP, GTK_SORT_ASCENDING);

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (theme_list),
	                                     theme_list_equal_func, NULL, NULL);

	/* Selection setup */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
		G_CALLBACK (gg_selection_changed), NULL);

	gtk_drag_dest_set (theme_list,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, n_targets,
			   GDK_ACTION_COPY);
			   
	g_signal_connect (theme_list, "drag_data_received",
		G_CALLBACK (theme_list_drag_data_received), NULL);

	if (select_iter != NULL) {
		gtk_tree_selection_select_iter (selection, select_iter);
		g_free (select_iter);
	}
}

static gboolean
delete_event (GtkWidget *w)
{
	timeout_remove_all ();
	gtk_main_quit ();
	return FALSE;
}

static void
dialog_response (GtkWidget *dlg, int response, gpointer data)
{
	if (response == GTK_RESPONSE_CLOSE) {
		timeout_remove_all ();
		gtk_main_quit ();
	} else if (response == GTK_RESPONSE_HELP) {
		GtkWidget *setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
		static GtkWidget *dlg = NULL;

		if (dlg != NULL) {
			gtk_window_present (GTK_WINDOW (dlg));
			return;
		}

		if ( ! RUNNING_UNDER_GDM) {
			gint exit_status;
			if (g_spawn_command_line_sync ("gnome-open ghelp:gdm", NULL, NULL,
							&exit_status, NULL) && exit_status == 0)
				return;
		}

		/* fallback help dialogue */
	
		/* HIG compliance? */
		dlg = gtk_message_dialog_new
			(GTK_WINDOW (setup_dialog),
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_INFO,
			 GTK_BUTTONS_OK,
			 /* This is the temporary help dialog */
			 _("This configuration window changes settings "
			   "for the GDM daemon, which is the graphical "
			   "login screen for GNOME.  Changes that you make "
			   "will take effect immediately.\n\n"
			   "Note that not all configuration options "
			   "are listed here.  You may want to edit %s "
			   "if you cannot find what you are looking for.\n\n"
			   "For complete documentation see the GNOME help browser "
			   "under the \"Desktop\" category."),
			 custom_config_file);
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
		g_signal_connect (G_OBJECT (dlg), "destroy",
				  G_CALLBACK (gtk_widget_destroyed),
				  &dlg);
		g_signal_connect_swapped (G_OBJECT (dlg), "response",
					  G_CALLBACK (gtk_widget_destroy),
					  dlg);
		gtk_widget_show (dlg);
	}
}

static void
background_filechooser_response (GtkWidget *file_chooser, gpointer data)
{
	gchar *filename = NULL;
	gchar *key;
	gchar *value;
				     		
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
	key      = g_object_get_data (G_OBJECT (file_chooser), "key");	
	value    = gdm_config_get_string (key);

	/*
	 * File_name should never be NULL, but something about this GUI causes
	 * this function to get called on startup and filename=NULL even
	 * though we set the filename in hookup_*_background.  Resetting the
	 * value to the default in this case seems to work around this.
	 */
	if (filename == NULL && !ve_string_empty (value))
		filename = g_strdup (value);

	if (filename != NULL &&
	   (strcmp (ve_sure_string (value), ve_sure_string (filename)) != 0)) {
		gchar *old_filename;
		old_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));

		if (strcmp (old_filename, filename) == 0)
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_chooser),
						       filename);
		g_free (old_filename);

		if (strcmp (ve_sure_string (value), ve_sure_string (filename)) != 0) {
			gdm_setup_config_set_string (key, (char *)ve_sure_string (filename));
			update_greeters ();
		}
	}
	g_free (filename);
}

static void
logo_filechooser_response (GtkWidget *file_chooser, gpointer data)
{
	GtkWidget *image_toggle;
	gchar *filename;
	gchar *key;
	gchar *value;
	
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
	key      = g_object_get_data (G_OBJECT (file_chooser), "key");	
	value    = gdm_config_get_string (key);
	
	/*
	 * File_name should never be NULL, but something about this GUI causes
	 * this function to get called on startup and filename=NULL even
	 * though we set the filename in hookup_*_background.  Resetting the
	 * value to the default in this case seems to work around this.
	 */
	if (filename == NULL && !ve_string_empty (value))
		filename = g_strdup (value);

	if (filename == NULL) {
		value    = gdm_config_get_string (GDM_KEY_CHOOSER_BUTTON_LOGO);
		if (!ve_string_empty (value))
			filename = g_strdup (value);
	}

	if (gtk_notebook_get_current_page (GTK_NOTEBOOK (setup_notebook)) == LOCAL_TAB) {
		image_toggle = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");
	} else {
		image_toggle = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");
	}

	if (filename != NULL &&
	   (strcmp (ve_sure_string (value), ve_sure_string (filename)) != 0)) {
		gchar *old_filename;
						      
		old_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));

		if (strcmp (old_filename, filename) != 0)
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_chooser),
						       filename);
		g_free (old_filename);

		if (GTK_TOGGLE_BUTTON (image_toggle)->active == TRUE) {
			gdm_setup_config_set_string (key,
    	                                      (char *)ve_sure_string (filename));
			update_greeters ();
		}
	}
	g_free (filename);
}

static GdkPixbuf *
create_preview_pixbuf (gchar *uri) 
{
	GdkPixbuf *pixbuf = NULL;
	
	if ((uri != NULL) && (uri[0] != '\0')) {
    
		gchar *file = NULL;
		
		if (g_path_is_absolute (uri) == TRUE) {
			file = g_strdup (uri);
		}
		else {
			/* URIs are local, because gtk_file_chooser_get_local_only() is true. */
			file = g_filename_from_uri (uri, NULL, NULL);	
		}
		
		if (file != NULL) {

			GdkPixbufFormat *info = NULL;
			gint width;
			gint height;

			info = gdk_pixbuf_get_file_info (file, &width, &height);
			
			if (width > 128 || height > 128) {
				pixbuf = gdk_pixbuf_new_from_file_at_size (file, 128, 128, NULL);
			}
			else {
				pixbuf = gdk_pixbuf_new_from_file (file, NULL);
			}
			g_free (file);
		}
	}				
	return pixbuf;
}

static void 
update_image_preview (GtkFileChooser *chooser) 
{
	GtkWidget *image;
	gchar *uri;

	image = gtk_file_chooser_get_preview_widget (GTK_FILE_CHOOSER (chooser));
	uri = gtk_file_chooser_get_preview_uri (chooser);
  
	if (uri != NULL) {
  
		GdkPixbuf *pixbuf = NULL;
    
		pixbuf = create_preview_pixbuf (uri);

		if (pixbuf != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
			g_object_unref (pixbuf);
		}
		else {
			gtk_image_set_from_stock (GTK_IMAGE (image),
			                          "gtk-dialog-question",
			                          GTK_ICON_SIZE_DIALOG);
		}
	}		
	gtk_file_chooser_set_preview_widget_active (chooser, TRUE);
}

static void
hookup_plain_background (void)
{	
	/* Initialize and hookup callbacks for plain background settings */
	GtkFileFilter *filter;
	GtkWidget *color_radiobutton;
	GtkWidget *color_colorbutton;
	GtkWidget *image_radiobutton;
	GtkWidget *image_filechooser;
	GtkWidget *image_scale_to_fit;
	GtkWidget *image_preview;
	gchar *background_filename;

	color_radiobutton = glade_xml_get_widget (xml, "local_background_color_checkbutton");
	color_colorbutton = glade_xml_get_widget (xml, "local_background_colorbutton");
	image_radiobutton = glade_xml_get_widget (xml, "local_background_image_checkbutton");
	image_filechooser = glade_xml_get_widget (xml, "local_background_image_chooserbutton");
	image_scale_to_fit = glade_xml_get_widget (xml, "sg_scale_background");

	setup_greeter_color ("local_background_colorbutton", 
	                     GDM_KEY_BACKGROUND_COLOR);

	setup_greeter_toggle ("sg_scale_background",
			      GDM_KEY_BACKGROUND_SCALE_TO_FIT);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_scale_to_fit), 
	                              gdm_config_get_bool (GDM_KEY_BACKGROUND_SCALE_TO_FIT));
	
        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("Images"));
        gtk_file_filter_add_pixbuf_formats (filter);
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (image_filechooser), filter);

        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("All Files"));
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (image_filechooser), filter);

	background_filename = gdm_config_get_string (GDM_KEY_BACKGROUND_IMAGE);

        if (ve_string_empty (background_filename)) {
                gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (image_filechooser),
                        PIXMAPDIR);
	} else {
                gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (image_filechooser),
			background_filename);
	}

	switch (gdm_config_get_int (GDM_KEY_BACKGROUND_TYPE)) {
	
		case BACKGROUND_IMAGE_AND_COLOR: {
			/* Image & Color background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), TRUE);
			gtk_widget_set_sensitive (image_scale_to_fit, TRUE);
			gtk_widget_set_sensitive (image_filechooser, TRUE);
			gtk_widget_set_sensitive (color_colorbutton, TRUE);
			
			break;
		}
		case BACKGROUND_COLOR: {
			/* Color background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), TRUE);
			gtk_widget_set_sensitive (image_scale_to_fit, FALSE);
			gtk_widget_set_sensitive (image_filechooser, FALSE);
			gtk_widget_set_sensitive (color_colorbutton, TRUE);

			break;
		}
		case BACKGROUND_IMAGE: {
			/* Image background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), FALSE);
			gtk_widget_set_sensitive (image_scale_to_fit, TRUE);
			gtk_widget_set_sensitive (image_filechooser, TRUE);
			gtk_widget_set_sensitive (color_colorbutton, FALSE);
			
			break;
		}
		default: {
			/* No background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), FALSE);
			gtk_widget_set_sensitive (color_colorbutton, FALSE);
			gtk_widget_set_sensitive (image_scale_to_fit, FALSE);
			gtk_widget_set_sensitive (image_filechooser, FALSE);
		}
	}

	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (image_filechooser),
					        FALSE);
	image_preview = gtk_image_new ();
	if (!ve_string_empty (background_filename)) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (image_preview),
			create_preview_pixbuf (background_filename));
	}
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (image_filechooser),
	                                     image_preview);
	gtk_widget_set_size_request (image_preview, 128, -1);  
	gtk_widget_show (image_preview); 

	g_object_set_data (G_OBJECT (color_radiobutton), "key",
	                   GDM_KEY_BACKGROUND_TYPE);
	g_object_set_data (G_OBJECT (color_colorbutton), "key",
	                   GDM_KEY_BACKGROUND_COLOR);
	g_object_set_data (G_OBJECT (image_radiobutton), "key",
	                   GDM_KEY_BACKGROUND_TYPE);
	g_object_set_data (G_OBJECT (image_filechooser), "key",
	                   GDM_KEY_BACKGROUND_IMAGE);			   
	g_object_set_data (G_OBJECT (image_scale_to_fit), "key",
	                   GDM_KEY_BACKGROUND_SCALE_TO_FIT);

	g_signal_connect (G_OBJECT (color_radiobutton), "toggled",
	                  G_CALLBACK (local_background_type_toggled), NULL);
	g_signal_connect (G_OBJECT (color_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), color_colorbutton);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (local_background_type_toggled), NULL);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), image_filechooser);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), image_scale_to_fit);
        g_signal_connect (G_OBJECT (image_filechooser), "selection-changed",
                          G_CALLBACK (background_filechooser_response), image_filechooser);
        g_signal_connect (G_OBJECT (image_filechooser), "update-preview",
			  G_CALLBACK (update_image_preview), NULL);
}

static void
hookup_plain_behaviour (void)
{
	/* Setup qiver */
	setup_notify_toggle ("local_quiver_checkbox", GDM_KEY_QUIVER);
	
	/* Setup show title bar */
	setup_notify_toggle ("local_title_bar_checkbutton", GDM_KEY_TITLE_BAR);
	
	/* Setup lock position */
	setup_notify_toggle ("local_lock_pos_checkbox", GDM_KEY_LOCK_POSITION);

	/* Setup set position */
	setup_notify_toggle ("local_set_pos_checkbox", GDM_KEY_SET_POSITION);

	/* Setup positionX */
	setup_intspin ("local_posx_spinbutton", GDM_KEY_POSITION_X);

	/* Setup positionX */
	setup_intspin ("local_posy_spinbutton", GDM_KEY_POSITION_Y);
	
}

static void
hookup_plain_logo (void)	
{
	/* Initialize and hookup callbacks for plain logo settings */
	GtkFileFilter *filter;
	GtkWidget *logo_checkbutton;
	GtkWidget *logo_button;
	GtkWidget *image_preview;
	gchar *logo_filename;

	logo_checkbutton = glade_xml_get_widget (xml, "local_logo_image_checkbutton");
	logo_button = glade_xml_get_widget (xml, "local_logo_image_chooserbutton");

	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (logo_button),
					        FALSE);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (logo_button), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All Files"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (logo_button), filter);

	logo_filename = gdm_config_get_string (GDM_KEY_LOGO);

	if (ve_string_empty (logo_filename)) {
		logo_filename = gdm_config_get_string (GDM_KEY_CHOOSER_BUTTON_LOGO);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logo_checkbutton), 
		                              FALSE);
		gtk_widget_set_sensitive (logo_button, FALSE);
	}
	else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logo_checkbutton), 
		                              TRUE);
		gtk_widget_set_sensitive (logo_button, TRUE);
	}

	if (ve_string_empty (logo_filename)) {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (logo_button),
			PIXMAPDIR);
	} else {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (logo_button),
			logo_filename);
	}

	image_preview = gtk_image_new ();
	if (!ve_string_empty (logo_filename)) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (image_preview),
			create_preview_pixbuf (logo_filename));
	}
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (logo_button), 
	                                     image_preview);
	gtk_widget_set_size_request (image_preview, 128, -1);  
	gtk_widget_show (image_preview); 

	g_object_set_data (G_OBJECT (logo_button), "key", GDM_KEY_LOGO);
	g_object_set_data (G_OBJECT (logo_checkbutton), "key", GDM_KEY_LOGO);

	g_signal_connect (G_OBJECT (logo_checkbutton), "toggled", 
	                  G_CALLBACK (logo_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (logo_checkbutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), logo_button);
        g_signal_connect (G_OBJECT (logo_button), "selection-changed",
                          G_CALLBACK (logo_filechooser_response), logo_button);
        g_signal_connect (G_OBJECT (logo_button), "update-preview",
                          G_CALLBACK (update_image_preview), NULL);
}

static void
setup_plain_menubar (void)
{
	/* Initialize and hookup callbacks for plain menu bar settings */
	setup_notify_toggle ("sysmenu", GDM_KEY_SYSTEM_MENU);
	setup_notify_toggle ("config_available", GDM_KEY_CONFIG_AVAILABLE);
	setup_notify_toggle ("chooser_button", GDM_KEY_CHOOSER_BUTTON);
}


static void
setup_local_welcome_message (void)	
{
	/* Initialize and hookup callbacks for local welcome message settings */ 
	setup_greeter_toggle ("sg_defaultwelcome", GDM_KEY_DEFAULT_WELCOME);
 	setup_greeter_untranslate_entry ("welcome", GDM_KEY_WELCOME);
}

static void
setup_remote_welcome_message (void)	
{
	/* Initialize and hookup callbacks for local welcome message settings */ 
	setup_greeter_toggle ("sg_defaultwelcomeremote", GDM_KEY_DEFAULT_REMOTE_WELCOME);
 	setup_greeter_untranslate_entry ("welcomeremote", GDM_KEY_REMOTE_WELCOME);
}

static void
setup_local_plain_settings (void)
{
	/* Style setting */
	setup_greeter_combobox ("local_greeter",
	                        GDM_KEY_GREETER);
	
	/* Plain background settings */
	hookup_plain_background ();

	/* Plain behaviour settings */
	hookup_plain_behaviour ();

	/* Plain logo settings */
	hookup_plain_logo ();	
	
	/* Plain menu bar settings */
	setup_plain_menubar ();
	
	/* Local welcome message settings */
	setup_local_welcome_message  ();
}

static void
setup_default_session (void)
{
	GtkWidget  *default_session_combobox;
	GtkWidget  *default_session_checkbox;
	GHashTable *sessnames        = NULL;
	GList      *org_sessions     = NULL;
	GList      *tmp;
	gint       i = 0;
	gint       active = 0;
	gchar      *org_val;

	_gdm_session_list_init (&sessnames, &org_sessions, NULL, NULL);
	
	default_session_combobox = glade_xml_get_widget (xml, "default_session_combobox");

	org_val = gdm_config_get_string (GDM_KEY_DEFAULT_SESSION);
	
	for (tmp = org_sessions; tmp != NULL; tmp = tmp->next, i++) {
		GdmSession *session;
		gchar *file;

		file = (gchar *) tmp->data;
		if (strcmp (ve_sure_string (org_val), ve_sure_string (file)) == 0)		
			active = i;
		
		session = g_hash_table_lookup (sessnames, file);
		
		if (!ve_string_empty (session->clearname)) {
			gtk_combo_box_append_text (GTK_COMBO_BOX (default_session_combobox), 
						   session->clearname);
			sessions = g_list_prepend (sessions, file);
		}
		/* This is a sort of safety fallback
		   if session does not have the clearname defined
		   we will use name instead*/		
		else if (!ve_string_empty (session->name)) {
			gtk_combo_box_append_text (GTK_COMBO_BOX (default_session_combobox), 
						   session->name);
			sessions = g_list_prepend (sessions, file);
		}
		
	}

	sessions = g_list_reverse (sessions);	

	/* some cleanup */	
	g_list_free (org_sessions);
	g_hash_table_remove_all (sessnames);
	
	if (!ve_string_empty (org_val)) {
		gtk_widget_set_sensitive (default_session_combobox, TRUE);		
		gtk_combo_box_set_active (GTK_COMBO_BOX (default_session_combobox), active);
	}
	else
		gtk_widget_set_sensitive (default_session_combobox, FALSE);
	
	g_object_set_data_full (G_OBJECT (default_session_combobox), "key",
	                        g_strdup (GDM_KEY_DEFAULT_SESSION),
				(GDestroyNotify) g_free);
	
	g_signal_connect (default_session_combobox, "changed",
		          G_CALLBACK (combobox_changed), NULL);
	
	default_session_checkbox = glade_xml_get_widget (xml, "default_session_checkbutton");

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (default_session_checkbox), !ve_string_empty (org_val));
	
	g_object_set_data_full (G_OBJECT (default_session_checkbox),
				"key", g_strdup (GDM_KEY_DEFAULT_SESSION),
				(GDestroyNotify) g_free);       
	
	g_signal_connect (G_OBJECT (default_session_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled), default_session_checkbox);
	g_signal_connect (G_OBJECT (default_session_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled_sensitivity_positive), default_session_combobox);
	
	g_free (org_val);
	
}

static void
setup_general_tab (void)
{
	GtkWidget *gtkrc_filechooser;
	GtkWidget *gtkrc_checkbox;
	GtkWidget *clock_type_chooser;	
	GtkWidget *commands_button;
	gchar *gtkrc_filename;
	gchar *user_24hr_clock;

	
	/* Setup use visual feedback in the passwotrd entry */
	setup_notify_toggle ("vis_feedback_passwd_checkbox", GDM_KEY_ENTRY_INVISIBLE);

	/* Setup use circles in the password entry */
	setup_notify_toggle ("use_circles_passwd_checkbox", GDM_KEY_ENTRY_CIRCLES);

	/* Setup default session */
	setup_default_session ();
	
	/* Setup GtkRC file path */
	gtkrc_filechooser = glade_xml_get_widget (xml, "gtkrc_chooserbutton");

	gtkrc_filename = gdm_config_get_string (GDM_KEY_GTKRC);

	gtkrc_checkbox = glade_xml_get_widget (xml, "gtkrc_checkbutton");

	if (!ve_string_empty (gtkrc_filename) && access (gtkrc_filename, R_OK) == 0) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (gtkrc_filechooser),
					       gtkrc_filename);
		
		gtk_widget_set_sensitive (gtkrc_filechooser, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gtkrc_checkbox), TRUE);
		
	}
	else {
		gtk_widget_set_sensitive (gtkrc_filechooser, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gtkrc_checkbox), FALSE);	
	}
		
	g_object_set_data_full (G_OBJECT (gtkrc_filechooser),
				"key", g_strdup (GDM_KEY_GTKRC),
				(GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (gtkrc_filechooser), "selection-changed",
			  G_CALLBACK (default_filechooser_response),
			  NULL);
	
	
	g_object_set_data_full (G_OBJECT (gtkrc_checkbox),
				"key", g_strdup (GDM_KEY_GTKRC),
				(GDestroyNotify) g_free);       
	
	g_signal_connect (G_OBJECT (gtkrc_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled), gtkrc_checkbox);	
	g_signal_connect (G_OBJECT (gtkrc_checkbox), "toggled",
			  G_CALLBACK (toggle_toggled_sensitivity_positive), gtkrc_filechooser);
	
	g_free (gtkrc_filename);

	/* Setup user 24Hr Clock */
	clock_type_chooser = glade_xml_get_widget (xml, "use_24hr_clock_combobox");

	user_24hr_clock = gdm_config_get_string (GDM_KEY_USE_24_CLOCK);
	if (!ve_string_empty (user_24hr_clock)) {
		if (strcasecmp (ve_sure_string (user_24hr_clock), _("auto")) == 0) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (clock_type_chooser), CLOCK_AUTO);
		}
		else if (strcasecmp (ve_sure_string (user_24hr_clock), _("yes")) == 0) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (clock_type_chooser), CLOCK_AUTO);
		}
		else if (strcasecmp (ve_sure_string (user_24hr_clock), _("no")) == 0) {	
			gtk_combo_box_set_active (GTK_COMBO_BOX (clock_type_chooser), CLOCK_NO);
		}
	}
	
	g_object_set_data_full (G_OBJECT (clock_type_chooser), "key",
	                        g_strdup (GDM_KEY_USE_24_CLOCK), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (clock_type_chooser), "changed",
	                  G_CALLBACK (combobox_changed), NULL);			
	
	commands_button = glade_xml_get_widget (xml, "configure_commands_button");
	g_signal_connect (G_OBJECT (commands_button), "clicked",
	                  G_CALLBACK (command_button_clicked), NULL);
      	
}

static void
setup_local_tab (void)
{
	setup_local_plain_settings ();
	setup_local_themed_settings ();
}

static void
hookup_remote_plain_background (void)
{	
	/* Initialize and hookup callbacks for plain background settings */
	GtkFileFilter *filter;
	GtkWidget *color_radiobutton;
	GtkWidget *color_colorbutton;
	GtkWidget *image_radiobutton;
	GtkWidget *image_filechooser;
	GtkWidget *image_scale_to_fit;
	GtkWidget *image_preview;
	gchar *background_filename;

	color_radiobutton = glade_xml_get_widget (xml, "remote_background_color_checkbutton");
	color_colorbutton = glade_xml_get_widget (xml, "remote_background_colorbutton");
	image_radiobutton = glade_xml_get_widget (xml, "remote_background_image_checkbutton");
	image_filechooser = glade_xml_get_widget (xml, "remote_background_image_chooserbutton");
	image_scale_to_fit = glade_xml_get_widget (xml, "sg_scale_background_remote");

	setup_greeter_color ("remote_background_colorbutton",
	                     GDM_KEY_BACKGROUND_COLOR);

	setup_greeter_toggle ("sg_scale_background_remote",
			      GDM_KEY_BACKGROUND_SCALE_TO_FIT);

	setup_greeter_toggle ("sg_remote_color_only",
			      GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_scale_to_fit), 
	                              gdm_config_get_bool (GDM_KEY_BACKGROUND_SCALE_TO_FIT));
	
        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("Images"));
        gtk_file_filter_add_pixbuf_formats (filter);
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (image_filechooser), filter);

        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("All Files"));
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (image_filechooser), filter);

	background_filename = gdm_config_get_string (GDM_KEY_BACKGROUND_IMAGE);

        if (ve_string_empty (background_filename)) {
                gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (image_filechooser),
                        PIXMAPDIR);
        } else {
                gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (image_filechooser),
			background_filename);
	}

	switch (gdm_config_get_int (GDM_KEY_BACKGROUND_TYPE)) {
	
		case BACKGROUND_IMAGE_AND_COLOR:	{
			/* Image & Color background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), TRUE);
			gtk_widget_set_sensitive (image_scale_to_fit, TRUE);
			gtk_widget_set_sensitive (image_filechooser, TRUE);
			gtk_widget_set_sensitive (color_colorbutton, TRUE);
			
			break;
		}
		case BACKGROUND_COLOR: {
			/* Color background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), TRUE);
			gtk_widget_set_sensitive (image_scale_to_fit, FALSE);
			gtk_widget_set_sensitive (image_filechooser, FALSE);
			gtk_widget_set_sensitive (color_colorbutton, TRUE);

			break;
		}
		case BACKGROUND_IMAGE: {
			/* Image background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), FALSE);
			gtk_widget_set_sensitive (image_scale_to_fit, TRUE);
			gtk_widget_set_sensitive (image_filechooser, TRUE);
			gtk_widget_set_sensitive (color_colorbutton, FALSE);
			
			break;
		}
		default: {
			/* No background type */
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_radiobutton), FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_radiobutton), FALSE);
			gtk_widget_set_sensitive (color_colorbutton, FALSE);
			gtk_widget_set_sensitive (image_scale_to_fit, FALSE);
			gtk_widget_set_sensitive (image_filechooser, FALSE);
		}
	}

	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (image_filechooser),
					        FALSE);
	image_preview = gtk_image_new ();
	if (!ve_string_empty (background_filename)) {
	gtk_image_set_from_pixbuf (GTK_IMAGE (image_preview),
		create_preview_pixbuf (background_filename));
	}
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (image_filechooser), 
	                                     image_preview);
	gtk_widget_set_size_request (image_preview, 128, -1);  
	gtk_widget_show (image_preview); 

	g_object_set_data (G_OBJECT (color_radiobutton), "key",
	                   GDM_KEY_BACKGROUND_TYPE);
	g_object_set_data (G_OBJECT (color_colorbutton), "key",
	                   GDM_KEY_BACKGROUND_COLOR);
	g_object_set_data (G_OBJECT (image_radiobutton), "key",
	                   GDM_KEY_BACKGROUND_TYPE);
	g_object_set_data (G_OBJECT (image_filechooser), "key",
	                   GDM_KEY_BACKGROUND_IMAGE);			   
	g_object_set_data (G_OBJECT (image_scale_to_fit), "key",
	                   GDM_KEY_BACKGROUND_SCALE_TO_FIT);

	g_signal_connect (G_OBJECT (color_radiobutton), "toggled",
	                  G_CALLBACK (local_background_type_toggled), NULL);
	g_signal_connect (G_OBJECT (color_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), color_colorbutton);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (local_background_type_toggled), NULL);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), image_filechooser);
	g_signal_connect (G_OBJECT (image_radiobutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), image_scale_to_fit);
        g_signal_connect (G_OBJECT (image_filechooser), "selection-changed",
                          G_CALLBACK (background_filechooser_response), image_filechooser);
        g_signal_connect (G_OBJECT (image_filechooser), "update-preview",
                          G_CALLBACK (update_image_preview), NULL);
}

static void
hookup_remote_plain_behaviour (void)
{
	/* Setup qiver */
	setup_notify_toggle ("remote_quiver_checkbox", GDM_KEY_QUIVER);
	
	/* Setup show title bar */
	setup_notify_toggle ("remote_title_bar_checkbutton", GDM_KEY_TITLE_BAR);
	
	/* Setup lock position */
	setup_notify_toggle ("remote_lock_pos_checkbox", GDM_KEY_LOCK_POSITION);

	/* Setup set position */
	setup_notify_toggle ("remote_set_pos_checkbox", GDM_KEY_SET_POSITION);

	/* Setup positionX */
	setup_intspin ("remote_posx_spinbutton", GDM_KEY_POSITION_X);

	/* Setup positionX */
	setup_intspin ("remote_posy_spinbutton", GDM_KEY_POSITION_Y);	
}

static void
hookup_remote_plain_logo (void)	
{
	/* Initialize and hookup callbacks for plain logo settings */
	GtkFileFilter *filter;
	GtkWidget *logo_checkbutton;
	GtkWidget *logo_button;
	GtkWidget *image_preview;
	gchar *logo_filename;

	logo_checkbutton = glade_xml_get_widget (xml, "remote_logo_image_checkbutton");
	logo_button = glade_xml_get_widget (xml, "remote_logo_image_chooserbutton");

	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (logo_button),
					        FALSE);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (logo_button), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All Files"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (logo_button), filter);

	logo_filename = gdm_config_get_string (GDM_KEY_LOGO);

	if (ve_string_empty (logo_filename)) {
		logo_filename = gdm_config_get_string (GDM_KEY_CHOOSER_BUTTON_LOGO);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logo_checkbutton), 
		                              FALSE);
		gtk_widget_set_sensitive (logo_button, FALSE);
	}
	else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (logo_checkbutton), 
		                              TRUE);
		gtk_widget_set_sensitive (logo_button, TRUE);
	}
		
        if (ve_string_empty (logo_filename))
                gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (logo_button),
                        PIXMAPDIR);
        else
                gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (logo_button), logo_filename);

	image_preview = gtk_image_new ();
	if (!ve_string_empty (logo_filename)) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (image_preview),
			create_preview_pixbuf (logo_filename));
	}
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (logo_button), 
	                                     image_preview);
	gtk_widget_set_size_request (image_preview, 128, -1);  
	gtk_widget_show (image_preview); 

	g_object_set_data (G_OBJECT (logo_button), "key",
	                   GDM_KEY_LOGO);
	g_object_set_data (G_OBJECT (logo_checkbutton), "key",
	                   GDM_KEY_LOGO);

	g_signal_connect (G_OBJECT (logo_checkbutton), "toggled", 
	                  G_CALLBACK (logo_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (logo_checkbutton), "toggled",
	                  G_CALLBACK (toggle_toggled_sensitivity_positive), logo_button);
        g_signal_connect (G_OBJECT (logo_button), "selection-changed",
                          G_CALLBACK (logo_filechooser_response), logo_button);
        g_signal_connect (G_OBJECT (logo_button), "update-preview",
                          G_CALLBACK (update_image_preview), NULL);
}

static void
setup_remote_plain_settings (void)
{
	GtkSizeGroup *size_group;
	GtkWidget *image_checkbutton;
	GtkWidget *color_checkbutton;

	image_checkbutton = glade_xml_get_widget (xml, "remote_background_image_checkbutton");
	color_checkbutton = glade_xml_get_widget (xml, "remote_background_color_checkbutton");

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, image_checkbutton);
	gtk_size_group_add_widget (size_group, color_checkbutton);

	/* Style setting */
	setup_greeter_combobox ("remote_greeter",
	                        GDM_KEY_REMOTE_GREETER);
	
	/* Plain background settings */
	hookup_remote_plain_background ();

	/* Plain behaviour settings */
	hookup_remote_plain_behaviour ();
	
	/* Plain logo settings */
	hookup_remote_plain_logo ();

	/* Remote welcome message settings */				
	setup_remote_welcome_message ();			
}

static void
setup_remote_themed_settings (void)
{
	gboolean GdmGraphicalThemeRand;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTreeIter *select_iter = NULL;
	GtkWidget *color_colorbutton;
	GtkWidget *style_label;
	GtkWidget *theme_label;
	GtkSizeGroup *size_group;

	GtkWidget *theme_list = glade_xml_get_widget (xml, "gg_theme_list_remote");
	GtkWidget *theme_list_local = glade_xml_get_widget (xml, "gg_theme_list");
	GtkWidget *button = glade_xml_get_widget (xml, "gg_install_new_theme_remote");
	GtkWidget *del_button = glade_xml_get_widget (xml, "gg_delete_theme_remote");
	GtkWidget *mode_combobox = glade_xml_get_widget (xml, "gg_mode_combobox_remote");

	style_label = glade_xml_get_widget (xml, "remote_stylelabel");
	theme_label = glade_xml_get_widget (xml, "remote_theme_label");
	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, style_label);
	gtk_size_group_add_widget (size_group, theme_label);

	color_colorbutton = glade_xml_get_widget (xml, "remote_background_theme_colorbutton");

	g_object_set_data (G_OBJECT (color_colorbutton), "key",
	                   GDM_KEY_GRAPHICAL_THEMED_COLOR);

	setup_greeter_color ("remote_background_theme_colorbutton", 
	                     GDM_KEY_GRAPHICAL_THEMED_COLOR);

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (theme_list), TRUE);

	GdmGraphicalThemeRand = gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND);

	/* Register theme mode combobox */
	g_object_set_data_full (G_OBJECT (mode_combobox), "key",
	                        g_strdup (GDM_KEY_GRAPHICAL_THEME_RAND),
	                       (GDestroyNotify) g_free);

	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list_local)));
	gtk_tree_view_set_model (GTK_TREE_VIEW (theme_list), 
				 GTK_TREE_MODEL (store));
	/* Signals */
	g_signal_connect (mode_combobox, "changed",
		          G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (install_new_theme), store);
	g_signal_connect (del_button, "clicked",
			  G_CALLBACK (delete_theme), store);

	/* Init controls */
	gtk_widget_set_sensitive (del_button, FALSE);
	gtk_combo_box_set_active (GTK_COMBO_BOX (mode_combobox),
	                          GdmGraphicalThemeRand);

	/* The radio toggle column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_toggle_new ();
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer),
					    TRUE);
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (selected_toggled), store);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
	                                     "active", THEME_COLUMN_SELECTED, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);
	gtk_tree_view_column_set_visible(column, !GdmGraphicalThemeRand);

	/* The checkbox toggle column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_toggle_new ();
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer),
	                                    FALSE);
	g_signal_connect (G_OBJECT (renderer), "toggled",
	                  G_CALLBACK (selected_toggled), store);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer, "active",
	                                     THEME_COLUMN_SELECTED_LIST, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);
	gtk_tree_view_column_set_visible (column, GdmGraphicalThemeRand);

	/* The preview column */
	column   = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", THEME_COLUMN_SCREENSHOT,
                                             NULL);

	/* The markup column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
	                                     "markup", THEME_COLUMN_MARKUP, NULL);
     	gtk_tree_view_column_set_spacing (column, 6);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
	                                      THEME_COLUMN_MARKUP, GTK_SORT_ASCENDING);

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (theme_list),
	                                     theme_list_equal_func, NULL, NULL);

	/* Selection setup */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
	                  G_CALLBACK (gg_selection_changed), NULL);

	gtk_drag_dest_set (theme_list,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, n_targets,
			   GDK_ACTION_COPY);
			   
	g_signal_connect (theme_list, "drag_data_received",
		G_CALLBACK (theme_list_drag_data_received), NULL);

	if (select_iter != NULL) {
		gtk_tree_selection_select_iter (selection, select_iter);
		g_free (select_iter);
	}
}

static void
setup_remote_tab (void)
{
	GtkWidget *xdmcp_button;

	xdmcp_button = glade_xml_get_widget (xml, "xdmcp_configbutton");

#ifndef HAVE_LIBXDMCP
	gtk_widget_set_sensitive (xdmcp_button, FALSE);
#else
	g_signal_connect (G_OBJECT (xdmcp_button), "clicked",
	                  G_CALLBACK (xdmcp_button_clicked), NULL);
#endif

	setup_remote_plain_settings ();
	setup_remote_themed_settings ();
}


static GtkWidget *
setup_gui (void)
{
	GtkWidget *dialog;

	xml = glade_xml_new (GDM_GLADE_DIR "/gdmsetup.glade", "setup_dialog", NULL);

	dialog = glade_xml_get_widget (xml, "setup_dialog");

	g_signal_connect (G_OBJECT (dialog), "delete_event",
			  G_CALLBACK (delete_event), NULL);
	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response), NULL);

	setup_notebook = glade_xml_get_widget (xml, "setup_notebook");

	/* Markup glade labels */
	glade_helper_tagify_label (xml, "themes_label", "b");
	glade_helper_tagify_label (xml, "sounds_label", "b");
	glade_helper_tagify_label (xml, "local_background_label", "b");
	glade_helper_tagify_label (xml, "local_behaviour_label", "b");	
	glade_helper_tagify_label (xml, "local_logo_label", "b");
	glade_helper_tagify_label (xml, "local_menubar_label", "b");
	glade_helper_tagify_label (xml, "local_welcome_message_label", "b");
	glade_helper_tagify_label (xml, "label_welcome_note", "i");
	glade_helper_tagify_label (xml, "label_welcome_note", "small");
	glade_helper_tagify_label (xml, "gg_author_label", "b");
	glade_helper_tagify_label (xml, "gg_author_label", "small");
	glade_helper_tagify_label (xml, "gg_copyright_label", "b");
	glade_helper_tagify_label (xml, "gg_copyright_label", "small");
	glade_helper_tagify_label (xml, "remote_plain_background_label", "b");
	glade_helper_tagify_label (xml, "remote_behaviour_label", "b");
	glade_helper_tagify_label (xml, "remote_logo_label", "b");
	glade_helper_tagify_label (xml, "remote_welcome_message_label", "b");
	glade_helper_tagify_label (xml, "label_welcomeremote_note", "i");
	glade_helper_tagify_label (xml, "label_welcomeremote_note", "small");
	glade_helper_tagify_label (xml, "autologin", "b");
	glade_helper_tagify_label (xml, "timedlogin", "b");
	glade_helper_tagify_label (xml, "security_label", "b");
	glade_helper_tagify_label (xml, "fb_informationlabel", "i");
	glade_helper_tagify_label (xml, "fb_informationlabel", "small");
	
	/* Setup preference tabs */
	setup_general_tab ();
	setup_local_tab (); 
	setup_remote_tab ();
 	setup_accessibility_tab (); 
	setup_security_tab ();
	setup_users_tab ();

	return (dialog);
}

static gboolean
get_sensitivity (void)
{
	static Atom atom = 0;
	Display *disp = gdk_x11_get_default_xdisplay ();
	Window root = gdk_x11_get_default_root_xwindow ();
	unsigned char *datac;
	gulong *data;
	gulong nitems_return;
	gulong bytes_after_return;
	Atom type_returned;
	int format_returned;

	if (atom == 0)
		atom = XInternAtom (disp, "_GDM_SETUP_INSENSITIVE", False);

	if (XGetWindowProperty (disp,
				root,
				atom,
				0, 1,
				False,
				XA_CARDINAL,
				&type_returned, &format_returned,
				&nitems_return,
				&bytes_after_return,
				&datac) != Success)
		return TRUE;

	data = (gulong *)datac;

	if (format_returned != 32 ||
	    data[0] == 0) {
		XFree (data);
		return TRUE;
	} else {
		XFree (data);
		return FALSE;
	}
}

static void
update_sensitivity (void)
{
	gboolean sensitive = get_sensitivity ();
	GtkWidget *setup_dialog = glade_xml_get_widget (xml, "setup_dialog");
	gtk_widget_set_sensitive (setup_dialog, sensitive);
	if (sensitive)
		unsetup_window_cursor ();
	else
		setup_window_cursor (GDK_WATCH);
}

static GdkFilterReturn
root_window_filter (GdkXEvent *gdk_xevent,
		    GdkEvent *event,
		    gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;

	if (xevent->type == PropertyNotify)
		update_sensitivity ();

	return GDK_FILTER_CONTINUE;
}

static void
setup_disable_handler (void)
{
	XWindowAttributes attribs = { 0, };
	Display *disp = gdk_x11_get_default_xdisplay ();
	Window root = gdk_x11_get_default_root_xwindow ();

	update_sensitivity ();

	/* set event mask for events on root window */
	XGetWindowAttributes (disp, root, &attribs);
	XSelectInput (disp, root,
		      attribs.your_event_mask |
		      PropertyChangeMask);

	gdk_window_add_filter (gdk_get_default_root_window (),
			       root_window_filter, NULL);
}

static gboolean
gdm_event (GSignalInvocationHint *ihint,
	   guint		n_param_values,
	   const GValue	       *param_values,
	   gpointer		data)
{
	GdkEvent *event;

	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	if (n_param_values != 2 ||
	    !G_VALUE_HOLDS (&param_values[1], GDK_TYPE_EVENT))
	  return FALSE;
	
	event = g_value_get_boxed (&param_values[1]);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}      

static void
apply_user_changes (GObject *object, gint arg1, gpointer user_data)
{
	GtkWidget *dialog = user_data;


	/* This is pretty ugly but it is probable
	   that both events can occur simultaneously.
	   Then we display two dialogs - one after the other. 
	   Lets hope that although probable this scenario 
	   does not araise very often */	
	if (GdmRandomFromSelectedChangesWarn == TRUE) {
		
		GtkWidget *prompt;
		
		prompt = hig_dialog_new (GTK_WINDOW (dialog),
					 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_OK,
					 _("Random theme mode change"),
					 _("Since no themes were selected in random theme mode"
					   " switching back to single theme mode."));
		
		gtk_dialog_run (GTK_DIALOG (prompt));
		gtk_widget_destroy (prompt);	
		
	}
	
	if (GdmUserChangesUnsaved == TRUE) {

		GtkWidget *prompt;
		gint response;

		prompt = hig_dialog_new (GTK_WINDOW (dialog),
					 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Apply the changes to users before closing?"),
					 _("If you don't apply, the changes made on "
					   "the Users tab will be disregarded."));

		gtk_dialog_add_button (GTK_DIALOG (prompt), _("Close _without Applying"), GTK_RESPONSE_CLOSE);
		gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-cancel", GTK_RESPONSE_CANCEL); 
		gtk_dialog_add_button (GTK_DIALOG (prompt), "gtk-apply", GTK_RESPONSE_APPLY);

		response = gtk_dialog_run (GTK_DIALOG (prompt));
		gtk_widget_destroy (prompt);

		if (response == GTK_RESPONSE_APPLY) {
			GtkWidget *apply_button;

			apply_button = glade_xml_get_widget (xml, "fb_faceapply");
			g_signal_emit_by_name (G_OBJECT (apply_button), "clicked");
		}
		
		gtk_main_quit ();

		if (response == GTK_RESPONSE_CANCEL) {
			gtk_main ();
		}
	}
}

int 
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	char **list;
	gint GdmMinimalUID;
	int i;

	gdm_config_never_cache (TRUE);

	if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
		DOING_GDM_DEVELOPMENT = TRUE;
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL)
		RUNNING_UNDER_GDM = TRUE;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* Lets check if gdm daemon is running
	   if no there is no point in continuing
	*/
	gdm_running = gdmcomm_check (TRUE);
	if (gdm_running == FALSE)
		exit (EXIT_FAILURE);

	gtk_window_set_default_icon_from_file (DATADIR"/pixmaps/gdm-setup.png", NULL);	
	glade_init();
	
	/* Start using socket */
	gdmcomm_comm_bulk_start ();
	
	config_file = gdm_common_get_config_file ();
	if (config_file == NULL) {
		GtkWidget *dialog;

		/* Done using socket */
		gdmcomm_comm_bulk_stop ();
		dialog = hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Could not access configuration file (defaults.conf)"),
					 _("Make sure that the file exists before launching login manager config utility."));
		
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		exit (EXIT_FAILURE);
	}
	custom_config_file = gdm_common_get_custom_config_file ();
	if (custom_config_file == NULL) {
		GtkWidget *dialog;

		/* Done using socket */
		gdmcomm_comm_bulk_stop ();
		dialog = hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Could not access configuration file (custom.conf)"),
					 _("Make sure that the file exists before launching login manager config utility."));

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		exit (EXIT_FAILURE);
	}	

	if (RUNNING_UNDER_GDM) {
		char *gtkrc;
		char *theme_name;

		/* Set busy cursor */
		setup_cursor (GDK_WATCH);

		/* parse the given gtk rc first */
		gtkrc = gdm_config_get_string (GDM_KEY_GTKRC);
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);

		theme_name = g_strdup (g_getenv ("GDM_GTK_THEME"));
		if (ve_string_empty (theme_name)) {
			g_free (theme_name);
			theme_name = gdm_config_get_string (GDM_KEY_GTK_THEME);
			gdm_set_theme (theme_name);
		} else {
			gdm_set_theme (theme_name);
		}

		/* evil, but oh well */
		g_type_class_ref (GTK_TYPE_WIDGET);	
	}

	/* Make sure the user is root. If not, they shouldn't be messing with 
	 * GDM's configuration.
	 */

	if ( ! DOING_GDM_DEVELOPMENT &&
	     geteuid() != 0) {
		GtkWidget *fatal_error;

		/* Done using socket */
		gdmcomm_comm_bulk_stop ();

		fatal_error = hig_dialog_new (NULL /* parent */,
					      GTK_DIALOG_MODAL /* flags */,
					      GTK_MESSAGE_ERROR,
					      GTK_BUTTONS_OK,
					      _("You must be the root user to configure GDM."),
					      "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	/*
         * XXX: the setup proggie using a greeter config var for it's
	 * ui?  Say it ain't so.  Our config sections are SUCH A MESS
         */
	GdmIconMaxHeight   = gdm_config_get_int (GDM_KEY_MAX_ICON_HEIGHT);
	GdmIconMaxWidth    = gdm_config_get_int (GDM_KEY_MAX_ICON_WIDTH);
	GdmIncludeAll      = gdm_config_get_bool (GDM_KEY_INCLUDE_ALL);
	GdmInclude         = gdm_config_get_string (GDM_KEY_INCLUDE);
	
	/* We need to make sure that the users in the include list exist
	   and have uid that are higher than MinimalUID. This protects us
	   from invalid data obtained from the config file */
	GdmMinimalUID = gdm_config_get_int (GDM_KEY_MINIMAL_UID);
	list = g_strsplit (GdmInclude, ",", 0);
	for (i=0; list != NULL && list[i] != NULL; i++) {
		if (gdm_is_user_valid (list[i]) && gdm_user_uid (list[i]) >= GdmMinimalUID)
			continue;
		
		GdmInclude = strings_list_remove (GdmInclude, list[i], ",");
	}
	g_strfreev (list);

	GdmExclude         = gdm_config_get_string (GDM_KEY_EXCLUDE);
	GdmSoundProgram    = gdm_config_get_string (GDM_KEY_SOUND_PROGRAM);

	if (ve_string_empty (GdmSoundProgram) ||
            g_access (GdmSoundProgram, X_OK) != 0) {
		GdmSoundProgram = NULL;
	}

        xservers = gdm_config_get_xservers (FALSE);

	/* Done using socket */
	gdmcomm_comm_bulk_stop ();

	/* Once we corrected the include list we need to save it if
	   it was modified */
	if ( strcmp (ve_sure_string (gdm_config_get_string (GDM_KEY_INCLUDE)), ve_sure_string (GdmInclude)) != 0)
		gdm_setup_config_set_string (GDM_KEY_INCLUDE, GdmInclude);
	
	dialog = setup_gui ();

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (apply_user_changes), dialog);
	gtk_widget_show (dialog);

	if (RUNNING_UNDER_GDM) {
		guint sid;

		/* also setup third button to work as first to work
		   in reverse situations transparently */
		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		setup_disable_handler ();

		setup_cursor (GDK_LEFT_PTR);
	}

	gtk_main ();

	return 0;
}

/* EOF */
