/* GDMSetup
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

#include <config.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <gdk/gdkx.h>
#include <glade/glade.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <viciousui.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "misc.h"
#include "gdmcomm.h"
#include "gdmuser.h"

/* set the DOING_GDM_DEVELOPMENT env variable if you want to
 * search for the glade file in the current dir and not the system
 * install dir, better then something you have to change
 * in the source and recompile */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;

static gboolean RUNNING_UNDER_GDM = FALSE;

gint  GdmIconMaxHeight;
gint  GdmIconMaxWidth;
gint GdmMinimalUID = 100;
gchar *GdmExclude = NULL;
gchar *GdmInclude = NULL;
gboolean GdmIncludeAll;
gboolean GdmAllowRoot;
gboolean GdmAllowRemoteRoot;
static char *GdmSoundProgram = NULL;

static gboolean gdm_running = FALSE;
static GladeXML *xml;
static GList *timeout_widgets = NULL;
static gchar *last_theme_installed = NULL;
static int last_remote_login_setting = -1;
static gboolean have_sound_ready_file = FALSE;
static gboolean have_sound_success_file = FALSE;
static gboolean have_sound_failure_file = FALSE;
static char *selected_themes = NULL;
static char *selected_theme  = NULL;
static gchar *config_file;

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
	THEME_COLUMN_NAME,
	THEME_COLUMN_DESCRIPTION,
	THEME_COLUMN_AUTHOR,
	THEME_COLUMN_COPYRIGHT,
	THEME_COLUMN_SCREENSHOT,
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
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

static void
setup_window_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	GtkWidget *setup_dialog = glade_helper_get
		(xml, "setup_dialog", GTK_TYPE_WINDOW);
	if (setup_dialog->window)
		gdk_window_set_cursor (setup_dialog->window, cursor);
	gdk_cursor_unref (cursor);
}

static void
unsetup_window_cursor (void)
{
	GtkWidget *setup_dialog = glade_helper_get
		(xml, "setup_dialog", GTK_TYPE_WINDOW);
	if (setup_dialog->window)
		gdk_window_set_cursor (setup_dialog->window, NULL);
}

static void
update_greeters (void)
{
	char *p, *ret;
	long pid;
	static gboolean shown_error = FALSE;
	gboolean have_error = FALSE;

	/* recheck for gdm */
	gdm_running = gdmcomm_check (config_file, FALSE /* gui_bitching */);

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
		GtkWidget *setup_dialog = glade_helper_get
			(xml, "setup_dialog", GTK_TYPE_WINDOW);
		GtkWidget *dlg =
			ve_hig_dialog_new (GTK_WINDOW (setup_dialog),
						GTK_DIALOG_MODAL | 
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						FALSE /* markup */,
						_("An error occurred while "
						  "trying to contact the "
						  "login screens.  Not all "
						  "updates may have taken "
						  "effect."),
						/* avoid warning */ "%s", "");
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
update_key (const char *notify_key)
{
	if (notify_key == NULL)
	       return;

	/* recheck for gdm */
	gdm_running = gdmcomm_check (config_file, FALSE /* gui_bitching */);

	if (gdm_running) {
		char *ret;
		char *s = g_strdup_printf ("%s %s", GDM_SUP_UPDATE_CONFIG,
					   notify_key);
		ret = gdmcomm_call_gdm (s,
					NULL /* auth_cookie */,
					"2.3.90.2",
					5);
		g_free (s);
		g_free (ret);
	}
}

static gboolean
toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	const char *notify_key = g_object_get_data (G_OBJECT (toggle),
	                                            "notify_key");
	VeConfig *config = ve_config_get (config_file);
	gboolean val = ve_config_get_bool (config, key);

	if ( ! ve_bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
		ve_config_set_bool (config, key,
		                    GTK_TOGGLE_BUTTON (toggle)->active);
		ve_config_save (config, FALSE /* force */);

		update_key (notify_key);
	}

	return FALSE;
}

static gboolean
intspin_timeout (GtkWidget *spin)
{
	const char *key = g_object_get_data (G_OBJECT (spin), "key");
	const char *notify_key = g_object_get_data (G_OBJECT (spin),
						    "notify_key");
	int val, new_val;
	VeConfig *config = ve_config_get (config_file);

	new_val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));

	val = ve_config_get_int (config, key);

	if (val != new_val) {
		ve_config_set_int (config, key, new_val);

		ve_config_save (config, FALSE /* force */);

		update_key (notify_key);
	}

	return FALSE;
}

static void 
xservers_get_servers (GtkListStore *store)
{
    /* Find server definitions */
	VeConfig *cfg = ve_config_get (config_file);
	GList *list, *li;
	gchar *server, *options, *cpy;
	
	/* Fill list with all the active servers */
	list = ve_config_get_keys (cfg, GDM_KEY_SECTION_SERVERS);

	for (li = list; li != NULL; li = li->next) {
		GtkTreeIter iter;
		char *key = li->data;
		int vt = atoi(key);
		key = g_strconcat(GDM_KEY_SECTION_SERVERS, "/", key, NULL);
		cpy = ve_config_get_string (cfg, key);
		server = ve_first_word (cpy);
		options = ve_rest (cpy);
		
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
		                    XSERVER_COLUMN_VT, vt,
		                    XSERVER_COLUMN_SERVER, server,
		                    XSERVER_COLUMN_OPTIONS, options,
		                    -1);
		g_free(server);
    }
}

static GSList *
xservers_get_server_definitions()
{
	/* Find server definitions */
	GSList *xservers = NULL;
	GList *list, *li;
	VeConfig *cfg = ve_config_get (config_file);
	gchar *StandardXServer = ve_config_get_string (cfg,
	                                               GDM_KEY_STANDARD_XSERVER);

	/* Fill list with all server definitions */
	list = ve_config_get_sections (cfg);
	for (li = list; li != NULL; li = li->next) {
		const char *sec = li->data;
		if (strncmp (sec, GDM_KEY_SERVER_PREFIX,
		             strlen (GDM_KEY_SERVER_PREFIX)) == 0) {
			GdmXServer *svr = g_new0 (GdmXServer, 1);
			char buf[256];

			svr->id = g_strdup (sec + strlen (GDM_KEY_SERVER_PREFIX));
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_NAME, sec);
			svr->name = ve_config_get_string (cfg, buf);
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_COMMAND, sec);
			svr->command = ve_config_get_string (cfg, buf);
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_FLEXIBLE, sec);
			svr->flexible = ve_config_get_bool (cfg, buf);
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_CHOOSABLE, sec);
			svr->choosable = ve_config_get_bool (cfg, buf);
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_HANDLED, sec);
			svr->handled = ve_config_get_bool (cfg, buf);
			g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_CHOOSER, sec);
			svr->chooser = ve_config_get_bool (cfg, buf);

			if (ve_string_empty (svr->command)) {
				g_free (svr->command);
				svr->command = g_strdup (StandardXServer);
			}

			xservers = g_slist_append (xservers, svr);
		}
	}
	return xservers;
}

static void
xserver_update_delete_sensitivity()
{

	GtkWidget *modify_combobox, *delete_button;
	GtkListStore *store;
	GtkTreeIter iter;
	GSList *xservers;
	GdmXServer *xserver;
	gchar *text;
	gchar *selected;
	gboolean valid;
	gint i;

	modify_combobox = glade_helper_get (xml, "xserver_mod_combobox",
                                            GTK_TYPE_COMBO_BOX);
	delete_button   = glade_helper_get (xml, "xserver_delete_button",
                                            GTK_TYPE_BUTTON);

	/* Get list of servers that are set to start */
	store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
	                            G_TYPE_INT    /* virtual terminal */,
	                            G_TYPE_STRING /* server type */,
	                            G_TYPE_STRING /* options */);
	xservers_get_servers(store);

	/* Get list of servers and determine which one was selected */
	xservers = xservers_get_server_definitions();
	i = gtk_combo_box_get_active (GTK_COMBO_BOX (modify_combobox));
	if (i < 0) {
		gtk_widget_set_sensitive(delete_button, FALSE);
	} else {
		/* Get the xserver selected */
		xserver = g_slist_nth_data(xservers, i);
	
		/* Sensitivity of delete_button */
		if (g_slist_length(xservers) <= 1) {
			/* Can't delete the laster server */
			gtk_widget_set_sensitive(delete_button, FALSE);
		} else {
			gtk_widget_set_sensitive(delete_button, TRUE);
			valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store),
			                                       &iter);
			selected = gtk_combo_box_get_active_text (
			              GTK_COMBO_BOX (modify_combobox));

			/* Can't delete servers currently in use */
			while (valid) {
				gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
				                    XSERVER_COLUMN_SERVER, &text, -1);
				if (strcmp (text, selected) == 0) {
					gtk_widget_set_sensitive(delete_button, FALSE);
					break;
				}
				valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store),
				                                  &iter);
			}
		}
	}
}

static gboolean
combobox_timeout (GtkWidget *combo_box)
{
	const char *key = g_object_get_data (G_OBJECT (combo_box), "key");
	int selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
	VeConfig *config = ve_config_get (config_file);

	/* Local Greeter and Remote Greeter Comboboxes */
	if (strcmp (key, GDM_KEY_REMOTEGREETER) == 0 ||
	    strcmp (key, GDM_KEY_GREETER) == 0) {

		char *new_val = NULL;
		gchar *val;

		if (strcmp (key, GDM_KEY_REMOTEGREETER) == 0 &&
		    selected == 2) {

			/* Disable remote login if selected */
			GtkWidget *toggle = glade_helper_get (xml, "enable_xdmcp",
			      GTK_TYPE_TOGGLE_BUTTON);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), FALSE);
			return FALSE;
		}

		else if (selected == 0) {
			/* GTK+ Greeter */
			new_val = g_strdup (EXPANDED_LIBEXECDIR "/gdmlogin");
			last_remote_login_setting = 0;
		} else if (selected == 1) {
			/* Themed Greeter */
			new_val = g_strdup (EXPANDED_LIBEXECDIR "/gdmgreeter");
			last_remote_login_setting = 1;
		}

		val = ve_config_get_string (config, key);
		if (new_val &&
		    strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {

			ve_config_set_string (config, key, new_val);
			ve_config_save (config, FALSE /* force */);
			update_key (key);
		}
		g_free (new_val);

	/* Automatic Login Combobox */
	} else if (strcmp (key, GDM_KEY_AUTOMATICLOGIN) == 0 ||
	           strcmp (key, GDM_KEY_TIMED_LOGIN) == 0) {

		GtkTreeIter iter;
		char *new_val = NULL;
		gchar *val;
		
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), 
		    &iter)) {
			gtk_tree_model_get (gtk_combo_box_get_model (
				GTK_COMBO_BOX (combo_box)), &iter,
				0, &new_val, -1);
		}

		val = ve_config_get_string (config, key);
		if (new_val &&
		    strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {

			ve_config_set_string (config, key, new_val);
			ve_config_save (config, FALSE /* force */);
			update_key (key);
		}
		g_free (new_val);

	/* Theme Combobox */
	} else if (strcmp (key, GDM_KEY_GRAPHICAL_THEME_RAND) == 0 ) {
		gboolean GdmGraphicalThemeRand;
		gboolean new_val;
		GtkTreeViewColumn *radioColumn = NULL;
		GtkTreeViewColumn *checkboxColumn = NULL;
		GtkTreeSelection *selection;
		GtkTreeIter iter;
		GValue value  = {0, };
		gboolean val = ve_config_get_bool (config, key);
		GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
			GTK_TYPE_TREE_VIEW);
		GtkWidget *del_button = glade_helper_get (xml, "gg_delete_theme",
			GTK_TYPE_BUTTON);

		/* Choose to display radio or checkbox toggle column */
		if (selected == RANDOM_THEME) {
			new_val = TRUE;
			radioColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
			                                        THEME_COLUMN_SELECTED);
			checkboxColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
				THEME_COLUMN_SELECTED_LIST);
			gtk_tree_view_column_set_visible (radioColumn, FALSE);
			gtk_tree_view_column_set_visible (checkboxColumn, TRUE);
		} else { /* Default to one theme */
			new_val = FALSE;
			radioColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
			                                        THEME_COLUMN_SELECTED);
			checkboxColumn = gtk_tree_view_get_column (GTK_TREE_VIEW (theme_list),
				THEME_COLUMN_SELECTED_LIST);
			gtk_tree_view_column_set_visible (radioColumn, TRUE);
			gtk_tree_view_column_set_visible (checkboxColumn, FALSE);
		}

		/* Update config */
		if (new_val != val) {
			ve_config_set_bool (config, key, new_val);
			ve_config_save (config, FALSE /* force */);
			update_key (key);
		}

		/* Update Delete Button's sensitivity */
		GdmGraphicalThemeRand = ve_config_get_bool (
			ve_config_get (config_file),
			GDM_KEY_GRAPHICAL_THEME_RAND);
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
		gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
		gtk_widget_set_sensitive (del_button, FALSE);
		if ( ! gtk_tree_selection_get_selected (selection, NULL, &iter))
		{
			gtk_widget_set_sensitive (del_button, FALSE);
		} else {
			GtkTreeModel *model;

			/* Default to allow deleting of themes */
			gtk_widget_set_sensitive (del_button, TRUE);

			/* Determine if the theme selected is currently active */
			model = gtk_tree_view_get_model (GTK_TREE_VIEW (theme_list));
			if (GdmGraphicalThemeRand) {
				gtk_tree_model_get_value (model, &iter,
				                          THEME_COLUMN_SELECTED_LIST, &value);
			} else {
				gtk_tree_model_get_value (model, &iter,
				                          THEME_COLUMN_SELECTED, &value);
			}
	
		    /* Do not allow deleting of active themes */
    		if (g_value_get_boolean (&value)) {
        		gtk_widget_set_sensitive (del_button, FALSE);
		    }
		}


	/* Add/Modify Server to Start combobox */
	} else if (strcmp (key, GDM_KEY_SECTION_SERVERS) == 0 ) { 
		GtkWidget *add_button = glade_helper_get (xml, "xserver_add_button",
		                                          GTK_TYPE_BUTTON);
		gtk_widget_set_sensitive(add_button, TRUE);

	/* Modify Server Definition combobox */
	} else if (strcmp (key, GDM_KEY_SERVER_PREFIX) == 0 ) {
		/* Init Widgets */
		GtkWidget *frame, *modify_combobox;
		GtkWidget *name_entry, *command_entry;
		GtkWidget *handled_check, *flexible_check;
		GtkWidget *greeter_radio, *chooser_radio;
		GtkWidget *create_button, *delete_button;
		GtkListStore *store;
		GSList *xservers;
		GdmXServer *xserver;
		gint i;

		/* Get Widgets from glade */
		frame           = glade_helper_get (xml, "xserver_modify_frame",
		                                    GTK_TYPE_FRAME);
		name_entry      = glade_helper_get (xml, "xserver_name_entry",
	                                        GTK_TYPE_ENTRY);
		command_entry   = glade_helper_get (xml, "xserver_command_entry",
		                                    GTK_TYPE_ENTRY);
		handled_check   = glade_helper_get (xml, "xserver_handled_checkbutton",
		                                    GTK_TYPE_CHECK_BUTTON);
		flexible_check  = glade_helper_get (xml, "xserver_flexible_checkbutton",
		                                    GTK_TYPE_CHECK_BUTTON);
		greeter_radio   = glade_helper_get (xml, "xserver_greeter_radiobutton",
		                                    GTK_TYPE_RADIO_BUTTON);
		chooser_radio   = glade_helper_get (xml, "xserver_chooser_radiobutton",
		                                    GTK_TYPE_RADIO_BUTTON);
		modify_combobox = glade_helper_get (xml, "xserver_mod_combobox",
		                                    GTK_TYPE_COMBO_BOX);
		create_button   = glade_helper_get (xml, "xserver_create_button",
		                                    GTK_TYPE_BUTTON);
		delete_button   = glade_helper_get (xml, "xserver_delete_button",
		                                    GTK_TYPE_BUTTON);

	    /* Get list of servers that are set to start */
		store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
		                            G_TYPE_INT    /* virtual terminal */,
		                            G_TYPE_STRING /* server type */,
		                            G_TYPE_STRING /* options */);
		xservers_get_servers(store);

		/* Get list of servers and determine which one was selected */
		xservers = xservers_get_server_definitions();
		i = gtk_combo_box_get_active (GTK_COMBO_BOX (modify_combobox));
		xserver = g_slist_nth_data(xservers, i);

		/* Set all the corresponding values in the frame */
		gtk_entry_set_text (GTK_ENTRY (name_entry), xserver->name);
		gtk_entry_set_text (GTK_ENTRY (command_entry), xserver->command);

		/* Update sensitivity of delete button */
		xserver_update_delete_sensitivity();

		if (xserver->chooser)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chooser_radio),
			                              TRUE);
		else
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (greeter_radio),
			                              TRUE);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (handled_check),
		                              xserver->handled);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flexible_check),
		                              xserver->flexible);
		gtk_widget_set_sensitive(frame, TRUE);
	}

	return FALSE;
}

static void
toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 200, toggle_timeout);
}

static void
remote_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *widget = data;
	gboolean val;

	val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));

	if (val == TRUE) {
		if (last_remote_login_setting != -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget),
			last_remote_login_setting);
		gtk_widget_set_sensitive (widget, TRUE);

	} else {
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget),
			2);
	}

	run_timeout (toggle, 200, toggle_timeout);
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
setup_notify_toggle (const char *name,
		     const char *key,
		     const char *notify_key)
{
	GtkWidget *toggle = glade_helper_get (xml, name,
					      GTK_TYPE_TOGGLE_BUTTON);
	gboolean val;

	val = ve_config_get_bool (ve_config_get (config_file), key);

	g_object_set_data_full (G_OBJECT (toggle),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (toggle),
				"notify_key", g_strdup (notify_key),
				(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	if (strcmp ("enable_xdmcp", name) == 0) {
		GtkWidget *remote_greeter = glade_helper_get (xml,
			"remote_greeter", GTK_TYPE_COMBO_BOX);

		if (val == FALSE) {
			gtk_widget_set_sensitive (remote_greeter, FALSE);
		}
		
		g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (remote_toggled), remote_greeter);
	} else {
		g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (toggle_toggled), NULL);
	}
}

static void
toggle_toggled_sensitivity_positive (GtkWidget *toggle, GtkWidget *depend)
{
	gtk_widget_set_sensitive (depend, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)));
}

static void
setup_sensitivity_positive_toggle (const char *name,
				   const char *depend_name)
{
	GtkWidget *toggle = glade_helper_get (xml, name,
					      GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *depend = glade_helper_get (xml, depend_name,
					      GTK_TYPE_WIDGET);

	toggle_toggled_sensitivity_positive (toggle, depend);

	g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (toggle_toggled_sensitivity_positive), 
			  depend);
}

static const char *
get_root_user (void)
{
	static char *root_user = NULL;
	struct passwd *pwent;

	if (root_user != NULL)
		return root_user;

	pwent = getpwuid (0);
	if (pwent == NULL) /* huh? */
		root_user = g_strdup ("root");
	else
		root_user = g_strdup (pwent->pw_name);
	return root_user;
}

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
		GtkWidget *dlg = 
			ve_hig_dialog_new (NULL /* parent */,
					   GTK_DIALOG_MODAL /* flags */,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   FALSE /* markup */,
					   _("Autologin or timed login to the root account is not allowed."),
					   /* avoid warning */ "%s", "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		warned = TRUE;
	}
}

/* Sets up Automatic Login Username and Timed Login User entry comboboxes
 * from the general configuration tab. */
static void
setup_user_combobox (const char *name, const char *key)
{
	GtkWidget *combobox_entry = glade_helper_get (xml, name, GTK_TYPE_COMBO_BOX_ENTRY);
	GtkListStore *combobox_store = gtk_list_store_new (USERLIST_NUM_COLUMNS,
		G_TYPE_STRING);
	GtkTreeIter iter;
	GList *users = NULL;
	GList *users_string = NULL;
	GList *li;
	static gboolean GDM_IS_LOCAL = FALSE;
	char *selected_user;
	gint size_of_users = 0;
	int selected = -1;
	int cnt;

	selected_user = ve_config_get_string (ve_config_get (config_file), key);

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

	users_string = g_list_reverse (users_string);

	cnt=0;
	for (li = users_string; li != NULL; li = li->next) {
		gtk_list_store_append (combobox_store, &iter);
		if (strcmp (li->data, selected_user) == 0)
			selected=cnt;
		gtk_list_store_set(combobox_store, &iter, USERLIST_NAME, li->data, -1);
		cnt++;
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (combobox_entry),
		GTK_TREE_MODEL (combobox_store));

	if (selected != -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combobox_entry), selected);

	g_object_set_data_full (G_OBJECT (combobox_entry), "key",
	                        g_strdup (key), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (combobox_entry), "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (G_OBJECT (combobox_entry), "changed",
	                  G_CALLBACK (root_not_allowed), NULL);

	g_list_foreach (users, (GFunc)g_free, NULL);
	g_list_free (users);
	g_list_foreach (users_string, (GFunc)g_free, NULL);
	g_list_free (users_string);
	g_free (selected_user);
}

static void
setup_intspin (const char *name,
	       const char *key,
	       const char *notify_key)
{
	GtkWidget *spin = glade_helper_get (xml, name,
					    GTK_TYPE_SPIN_BUTTON);
	int val;

	val = ve_config_get_int (ve_config_get (config_file), key);

	g_object_set_data_full (G_OBJECT (spin),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (spin),
				"notify_key", g_strdup (notify_key),
				(GDestroyNotify) g_free);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), val);

	g_signal_connect (G_OBJECT (spin), "value_changed",
			  G_CALLBACK (intspin_changed), NULL);
}

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

	if (strcmp (key, GDM_KEY_INCLUDE) == 0)
		list = g_strsplit (GdmInclude, ",", 0);
	else if (strcmp (key, GDM_KEY_EXCLUDE) == 0)
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
	GtkWidget *label;
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
	GtkWidget *include_entry;
	GtkWidget *exclude_entry;
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
face_add (GtkWidget *button, gpointer data)
{
	FaceData *fd = data;
	const char *text = NULL;
	const char *model_text;
	GtkTreeIter iter;
	gboolean valid;

	if (fd->type == INCLUDE)
		text = gtk_entry_get_text (GTK_ENTRY (fd->fc->include_entry));
	else if (fd->type == EXCLUDE)
		text = gtk_entry_get_text (GTK_ENTRY (fd->fc->exclude_entry));

	if (gdm_is_user_valid (text)) {
		valid = gtk_tree_model_get_iter_first (fd->fc->include_model, &iter);
		while (valid) {
			gtk_tree_model_get (fd->fc->include_model, &iter, USERLIST_NAME,
				 &model_text, -1);
			if (strcmp (text, model_text) == 0) {
				gtk_label_set_text (GTK_LABEL (fd->fc->label),
					 "User already in Include list");
				return;
			}

			valid = gtk_tree_model_iter_next (fd->fc->include_model, &iter);
		}

		valid = gtk_tree_model_get_iter_first (fd->fc->exclude_model, &iter);
		while (valid) {
			gtk_tree_model_get (fd->fc->exclude_model, &iter, USERLIST_NAME,
				 &model_text, -1);
			if (strcmp (text, model_text) == 0) {
				gtk_label_set_text (GTK_LABEL (fd->fc->label),
					 "User already in Exclude list");
				return;
			}

			valid = gtk_tree_model_iter_next (fd->fc->exclude_model, &iter);
		}

		if (fd->type == INCLUDE) {
			gtk_list_store_append (fd->fc->include_store, &iter);
			gtk_list_store_set (fd->fc->include_store, &iter,
				USERLIST_NAME, text, -1);
			gtk_entry_set_text (GTK_ENTRY (fd->fc->include_entry),
				"");
		} else if (fd->type == EXCLUDE) {
			gtk_list_store_append (fd->fc->exclude_store, &iter);
			gtk_list_store_set (fd->fc->exclude_store, &iter,
				USERLIST_NAME, text, -1);
			gtk_entry_set_text (GTK_ENTRY (fd->fc->exclude_entry),
				"");
		}
		gtk_widget_set_sensitive (fd->fc->apply, TRUE);
		gtk_label_set_text (GTK_LABEL (fd->fc->label), "");
	} else {
		gtk_label_set_text (GTK_LABEL (fd->fc->label),
			"Not a valid user");
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
		}
	} else if (fd->type == EXCLUDE) {
		selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (fd->fc->exclude_treeview));

		if (gtk_tree_selection_get_selected (selection, &(fd->fc->exclude_model), &iter)) {
			gtk_list_store_remove (fd->fc->exclude_store, &iter);
			gtk_widget_set_sensitive (fd->fc->apply, TRUE);
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
	}
}

static void
browser_apply (GtkWidget *button, gpointer data)
{
	FaceCommon *fc = data;
	VeConfig *config = ve_config_get (config_file);
	GString *userlist = g_string_new (NULL);
	const char *model_text;
	char *val;
	GtkTreeIter iter;
	gboolean valid;
	char *sep = "";

	valid = gtk_tree_model_get_iter_first (fc->include_model, &iter);
	while (valid) {
		gtk_tree_model_get (fc->include_model, &iter, USERLIST_NAME,
			 &model_text, -1);

		g_string_append (userlist, sep);
		sep = ",";
		g_string_append (userlist, model_text);

		valid = gtk_tree_model_iter_next (fc->include_model, &iter);
	}

	val = ve_config_get_string (config, GDM_KEY_INCLUDE);

	if (strcmp (ve_sure_string (val),
		    ve_sure_string (userlist->str)) != 0) {
		ve_config_set_string (config, GDM_KEY_INCLUDE, userlist->str);
		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	g_string_free (userlist, TRUE);
	g_free (val);

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

	val = ve_config_get_string (config, GDM_KEY_EXCLUDE);

	if (strcmp (ve_sure_string (val),
		    ve_sure_string (userlist->str)) != 0) {
		ve_config_set_string (config, GDM_KEY_EXCLUDE, userlist->str);
		ve_config_save (config, FALSE /* force */);

		update_key (GDM_KEY_EXCLUDE);
	}

	g_string_free (userlist, TRUE);
	g_free (val);
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
face_entry_changed (GtkEditable *editable, gpointer data)
{
	FaceData *fd = data;
	const char *text;

	if (fd->type == INCLUDE) {
		text = gtk_entry_get_text (GTK_ENTRY (fd->fc->include_entry));
		if (strlen (text) < 1)
			gtk_widget_set_sensitive (fd->fc->include_add, FALSE);
		else
			gtk_widget_set_sensitive (fd->fc->include_add, TRUE);
	}
	else if (fd->type == EXCLUDE) {
		text = gtk_entry_get_text (GTK_ENTRY (fd->fc->exclude_entry));
		if (strlen (text) < 1)
			gtk_widget_set_sensitive (fd->fc->exclude_add, FALSE);
		else
			gtk_widget_set_sensitive (fd->fc->exclude_add, TRUE);
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
sensitivity_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *widget = data;

	gtk_widget_set_sensitive (widget,
				  GTK_TOGGLE_BUTTON (toggle)->active);
}

static void
setup_face (void)
{
	GtkWidget *fb_browser = glade_helper_get (xml, "fb_browser", GTK_TYPE_WIDGET);
	GtkWidget *face_frame = glade_helper_get (xml, "face_frame", GTK_TYPE_WIDGET);
	static FaceCommon fc;
	static FaceData fd_include;
	static FaceData fd_exclude;
	static FaceApply face_apply;

	GtkTreeSelection *selection;

	fc.include_add       = glade_helper_get (xml, "fb_includeadd",
	                                         GTK_TYPE_WIDGET);
	fc.include_del       = glade_helper_get (xml, "fb_includedelete",
	                                         GTK_TYPE_WIDGET);
	fc.exclude_add       = glade_helper_get (xml, "fb_excludeadd",
	                                         GTK_TYPE_WIDGET);
	fc.exclude_del       = glade_helper_get (xml, "fb_excludedelete",
	                                         GTK_TYPE_WIDGET);
	fc.to_include_button = glade_helper_get (xml, "fb_toinclude",
	                                         GTK_TYPE_WIDGET);
	fc.to_exclude_button = glade_helper_get (xml, "fb_toexclude",
	                                         GTK_TYPE_WIDGET);
	fc.apply             = glade_helper_get (xml, "fb_faceapply",
	                                         GTK_TYPE_WIDGET);
	fc.label             = glade_helper_get (xml, "fb_message",
	                                         GTK_TYPE_WIDGET);
	fc.include_entry     = glade_helper_get (xml, "fb_includeentry",
	                                         GTK_TYPE_WIDGET);
	fc.exclude_entry     = glade_helper_get (xml, "fb_excludeentry",
	                                         GTK_TYPE_WIDGET);
	fc.include_treeview  = glade_helper_get (xml, "fb_include_treeview",
	                                         GTK_TYPE_TREE_VIEW);
	fc.exclude_treeview  = glade_helper_get (xml, "fb_exclude_treeview",
	                                         GTK_TYPE_TREE_VIEW);
	fc.allusers          = glade_helper_get (xml, "fb_allusers",
	                                         GTK_TYPE_TOGGLE_BUTTON);

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

	gtk_widget_set_sensitive (fc.include_add, FALSE);
	gtk_widget_set_sensitive (fc.exclude_add, FALSE);
	gtk_widget_set_sensitive (fc.include_del, FALSE);
	gtk_widget_set_sensitive (fc.exclude_del, FALSE);
	gtk_widget_set_sensitive (fc.to_include_button, FALSE);
	gtk_widget_set_sensitive (fc.to_exclude_button, FALSE);
	gtk_widget_set_sensitive (fc.apply, FALSE);

	face_apply.include = &fd_include;
	face_apply.exclude = &fd_exclude;

	g_signal_connect (fc.include_add, "clicked",
	                  G_CALLBACK (face_add), &fd_include);
	g_signal_connect (fc.exclude_add, "clicked",
	                  G_CALLBACK (face_add), &fd_exclude);
	g_signal_connect (fc.include_del, "clicked",
	                  G_CALLBACK (face_del), &fd_include);
	g_signal_connect (fc.exclude_del, "clicked",
	                  G_CALLBACK (face_del), &fd_exclude);

	g_signal_connect (fc.include_entry, "changed",
	                  G_CALLBACK (face_entry_changed), &fd_include);
	g_signal_connect (fc.exclude_entry, "changed",
	                  G_CALLBACK (face_entry_changed), &fd_exclude);

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

static gboolean
greeter_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	VeConfig *config = ve_config_get (config_file);
	gboolean val = ve_config_get_bool (config, key);

	if ( ! ve_bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
		ve_config_set_bool (config, key,
				    GTK_TOGGLE_BUTTON (toggle)->active);

		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	return FALSE;
}

static void
greeter_toggle_toggled (GtkWidget *toggle)
{
	run_timeout (toggle, 500, greeter_toggle_timeout);
}

static void
setup_greeter_toggle (const char *name,
		      const char *key)
{
	GtkWidget *toggle = glade_helper_get (xml, name,
	      GTK_TYPE_TOGGLE_BUTTON);
	gboolean val = ve_config_get_bool (ve_config_get (config_file), key);

	g_object_set_data_full (G_OBJECT (toggle), "key", g_strdup (key),
		(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	if (strcmp ("sg_defaultwelcome", name) == 0) {
		GtkWidget *welcome = glade_helper_get (xml,
			"welcome", GTK_TYPE_ENTRY);

		if (val == TRUE)
			gtk_widget_set_sensitive (welcome, FALSE);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), welcome);

	} else if (strcmp ("sg_defaultremotewelcome", name) == 0) {
		GtkWidget *remotewelcome = glade_helper_get (xml,
			"remote_welcome", GTK_TYPE_ENTRY);

		if (val == TRUE)
			gtk_widget_set_sensitive (remotewelcome, FALSE);

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), remotewelcome);
	} else if (strcmp ("fb_allusers", name) == 0) {
		GtkWidget *fb_includebox = glade_helper_get (xml,
			"fb_includebox", GTK_TYPE_VBOX);
		GtkWidget *fb_buttonbox = glade_helper_get (xml,
			"fb_buttonbox", GTK_TYPE_VBOX);

		if (val == TRUE) {
			gtk_widget_set_sensitive (fb_includebox, FALSE);
			gtk_widget_set_sensitive (fb_buttonbox, FALSE);
		}

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_includebox);
		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), fb_buttonbox);
	}

	g_signal_connect (G_OBJECT (toggle), "toggled",
		G_CALLBACK (greeter_toggle_toggled), NULL);
}

static gboolean
greeter_color_timeout (GtkWidget *picker)
{
	const char *key = g_object_get_data (G_OBJECT (picker), "key");
	char *val, *color;
	guint8 r, g, b;
	VeConfig *config = ve_config_get (config_file);

	gnome_color_picker_get_i8 (GNOME_COLOR_PICKER (picker),
				   &r, &g, &b, NULL);
	color = g_strdup_printf ("#%02x%02x%02x", (int)r, (int)g, (int)b);

	val = ve_config_get_string (config, key);

	if (strcmp (ve_sure_string (val), ve_sure_string (color)) != 0) {
		ve_config_set_string (config, key, ve_sure_string (color));

		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	g_free (val);
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
	GtkWidget *picker = glade_helper_get (xml, name,
					      GNOME_TYPE_COLOR_PICKER);
	char *val;

	val = ve_config_get_string (ve_config_get (config_file), key);

	g_object_set_data_full (G_OBJECT (picker),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

        if (val != NULL) {
		GdkColor color;
		if (gdk_color_parse (val, &color))
			gnome_color_picker_set_i16
				(GNOME_COLOR_PICKER (picker),
				 color.red, color.green, color.blue, 0);
	}

	g_signal_connect (G_OBJECT (picker), "color_set",
			  G_CALLBACK (greeter_color_changed), NULL);

	g_free (val);
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

static void
update_preview_cb (GtkFileChooser *file_chooser, gpointer data)
{
	ImageData *image_data = data;

	char *filename = gtk_file_chooser_get_preview_filename (file_chooser);
	GdkPixbuf *pixbuf = NULL;

	if (filename != NULL)
		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

	if (pixbuf)
		gtk_image_set_from_file (GTK_IMAGE (image_data->image), filename);
}

static void
image_install_response (GtkWidget *file_dialog, gint response, gpointer data)
{
	ImageData *image_data = data;

        if (response == GTK_RESPONSE_ACCEPT) {
		char *val;
		VeConfig *config = ve_config_get (config_file);

                image_data->filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));

		val = ve_config_get_string (config, image_data->key);

		if (strcmp (ve_sure_string (val), ve_sure_string (image_data->filename)) != 0) {
			ve_config_set_string (config, image_data->key,
				ve_sure_string (image_data->filename));

			ve_config_save (config, FALSE /* force */);

			update_greeters ();
		}

		g_free (val);
        } else {
		gtk_image_set_from_file (GTK_IMAGE (image_data->image), image_data->filename);
        }

        gtk_widget_destroy (file_dialog);
}

static void
browse_button_cb (GtkWidget *widget, gpointer data)
{
	ImageData *image_data = data;
        GtkFileFilter *filter;
	GtkWidget *setup_dialog = glade_helper_get (xml, "setup_dialog",
		GTK_TYPE_WINDOW);

        GtkWidget *file_dialog = gtk_file_chooser_dialog_new (_("Open File"),
                                              GTK_WINDOW (setup_dialog),
                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                              GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                              NULL);

        if (image_data->filename != NULL && *(image_data->filename) != '\0')
                gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_dialog),
                        image_data->filename);
        else
                gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
                        EXPANDED_DATADIR "/pixmaps");

        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("PNG and JPEG"));
        gtk_file_filter_add_mime_type (filter, "image/jpeg");
        gtk_file_filter_add_mime_type (filter, "image/png");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	g_signal_connect (file_dialog, "update-preview",
			  G_CALLBACK (update_preview_cb), image_data);
        g_signal_connect (G_OBJECT (file_dialog), "destroy",
                          G_CALLBACK (gtk_widget_destroyed), &file_dialog);
        g_signal_connect (G_OBJECT (file_dialog), "response",
                          G_CALLBACK (image_install_response), image_data);

        gtk_widget_show (file_dialog);
}

static void
noimage_button_cb (GtkWidget *widget, gpointer data)
{
	ImageData *image_data = data;
	char *val;
	VeConfig *config = ve_config_get (config_file);

	gtk_image_set_from_file (GTK_IMAGE (image_data->image), NULL);
	image_data->filename = NULL;
	val = ve_config_get_string (config, image_data->key);

	if (strcmp (ve_sure_string (val), ve_sure_string (image_data->filename)) != 0) {
		ve_config_set_string (config, image_data->key,
			ve_sure_string (image_data->filename));

		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	g_free (val);
}

static void
setup_greeter_image (GtkWidget *dialog)
{
	static ImageData logo_data;
	static ImageData backimage_data;
	GdkPixbuf *pixbuf;
	GtkWidget *logo_button = glade_helper_get (xml, "sg_browselogo",
		GTK_TYPE_WIDGET);
	GtkWidget *backimage_button = glade_helper_get (xml, "sg_browsebackimage",
		GTK_TYPE_WIDGET);
	GtkWidget *nologo_button = glade_helper_get (xml, "sg_nologo",
		GTK_TYPE_WIDGET);

	logo_data.image = glade_helper_get (xml, "sg_logo", GTK_TYPE_WIDGET);
	logo_data.key = GDM_KEY_LOGO;
	logo_data.filename = ve_config_get_string (ve_config_get (config_file),
		GDM_KEY_LOGO);

	backimage_data.image    = glade_helper_get (xml, "sg_backimage",
		GTK_TYPE_WIDGET);
	backimage_data.filename = ve_config_get_string (
		ve_config_get (config_file), GDM_KEY_BACKGROUNDIMAGE);
	backimage_data.key = GDM_KEY_BACKGROUNDIMAGE;


	if (logo_data.filename != NULL) {
		pixbuf = gdk_pixbuf_new_from_file (logo_data.filename, NULL);
		if (pixbuf != NULL)
			gtk_image_set_from_file (GTK_IMAGE(logo_data.image),
				logo_data.filename);
		else
			logo_data.filename = NULL;
	}
	if (backimage_data.filename != NULL) {
		pixbuf = gdk_pixbuf_new_from_file (backimage_data.filename, NULL);
		if (pixbuf != NULL)
			gtk_image_set_from_file (GTK_IMAGE(backimage_data.image),
				backimage_data.filename);
		else
			backimage_data.filename = NULL;
	}

	g_signal_connect (G_OBJECT (logo_button), "clicked",
		G_CALLBACK (browse_button_cb), &logo_data);
	g_signal_connect (G_OBJECT (nologo_button), "clicked",
		G_CALLBACK (noimage_button_cb), &logo_data);

	g_signal_connect (G_OBJECT (backimage_button), "clicked",
		G_CALLBACK (browse_button_cb), &backimage_data);
}

static gboolean
greeter_entry_untranslate_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;
	VeConfig *config = ve_config_get (config_file);

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	ve_config_delete_translations (config, key);

	ve_config_set_string (config, key, ve_sure_string (text));

	ve_config_save (config, FALSE /* force */);

	update_greeters ();

	return FALSE;
}

static void
greeter_entry_untranslate_changed (GtkWidget *entry)
{
	run_timeout (entry, 500, greeter_entry_untranslate_timeout);
}

static void
setup_greeter_untranslate_entry (const char *name,
				 const char *key)
{
	GtkWidget *entry = glade_helper_get (xml, name, GTK_TYPE_ENTRY);
	char *val;

	val = ve_config_get_translated_string (ve_config_get (config_file),
					       key);

	g_object_set_data_full (G_OBJECT (entry),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_entry_set_text (GTK_ENTRY (entry), ve_sure_string (val));

	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (greeter_entry_untranslate_changed),
			  NULL);

	g_free (val);
}


static gboolean
greeter_backselect_timeout (GtkWidget *toggle)
{
	int val, new_val;
	GtkWidget *no_bg = glade_helper_get (xml, "sg_no_bg_rb",
					     GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *image_bg = glade_helper_get (xml, "sg_image_bg_rb",
						GTK_TYPE_TOGGLE_BUTTON);
	VeConfig *config = ve_config_get (config_file);

	val = ve_config_get_int (config, GDM_KEY_BACKGROUNDTYPE);

	if (GTK_TOGGLE_BUTTON (no_bg)->active)
		new_val = 0 /* No background */;
	else if (GTK_TOGGLE_BUTTON (image_bg)->active)
		new_val = 1 /* Image */;
	else 
		new_val = 2 /* Color */;

	if (val != new_val) {
		ve_config_set_int (config, GDM_KEY_BACKGROUNDTYPE, new_val);
		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	return FALSE;
}

static void
greeter_backselect_toggled (GtkWidget *toggle)
{
	/* We set the timeout on the no_bg radiobutton */
	GtkWidget *no_bg = glade_helper_get (xml, "sg_no_bg_rb",
					     GTK_TYPE_WIDGET);
	run_timeout (no_bg, 500, greeter_backselect_timeout);
}

static void
setup_greeter_backselect (void)
{
	int val;
	GtkWidget *no_bg = glade_helper_get (xml, "sg_no_bg_rb",
					     GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *image_bg = glade_helper_get (xml, "sg_image_bg_rb",
						GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *color_bg = glade_helper_get (xml, "sg_color_bg_rb",
						GTK_TYPE_TOGGLE_BUTTON);

	val = ve_config_get_int (ve_config_get (config_file),
				 GDM_KEY_BACKGROUNDTYPE);

	if (val == 0)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (no_bg), TRUE);
	else if (val == 1)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (image_bg), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (color_bg), TRUE);

	g_signal_connect (G_OBJECT (no_bg), "toggled",
			  G_CALLBACK (greeter_backselect_toggled), NULL);
	g_signal_connect (G_OBJECT (image_bg), "toggled",
			  G_CALLBACK (greeter_backselect_toggled), NULL);
	g_signal_connect (G_OBJECT (color_bg), "toggled",
			  G_CALLBACK (greeter_backselect_toggled), NULL);
}


/* Local and Remote greeter comboboxes from General configuration tab */
static void
setup_greeter_combobox (const char *name,
		      const char *key)
{
	GtkWidget *combobox = glade_helper_get (xml, name, GTK_TYPE_COMBO_BOX);
	GtkWidget *toggle   = glade_helper_get (xml, "enable_xdmcp",
	      GTK_TYPE_TOGGLE_BUTTON);
	char *val;

	val = ve_config_get_string (ve_config_get (config_file), key);

	if (val != NULL &&
	    strcmp (val,
	    EXPANDED_LIBEXECDIR "/gdmlogin --disable-sound --disable-crash-dialog") == 0) {
		g_free (val);
		val = g_strdup (EXPANDED_LIBEXECDIR "/gdmlogin");
	}

	if (strcmp (key, GDM_KEY_REMOTEGREETER) == 0 &&
	    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)) == FALSE)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 2);
		
	else if (strcmp (val, EXPANDED_LIBEXECDIR "/gdmlogin") == 0) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 0);
		last_remote_login_setting = 0;
	}

	else if (strcmp (val, EXPANDED_LIBEXECDIR "/gdmgreeter") == 0) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 1);
		last_remote_login_setting = 1;
	}
		
	g_free (val);

	g_object_set_data_full (G_OBJECT (combobox), "key",
	                        g_strdup (key), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (combobox), "changed",
	                  G_CALLBACK (combobox_changed), NULL);
}

static void
setup_xdmcp_support (void)
{
	GtkWidget *xdmcp_toggle = glade_helper_get (xml, "enable_xdmcp", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *xdmcp_label = glade_helper_get (xml, "XDMCP_label", GTK_TYPE_WIDGET);
	GtkWidget *xdmcp_frame = glade_helper_get (xml, "xdmcp_frame", GTK_TYPE_WIDGET);
	GtkWidget *xdmcp_vbox = glade_helper_get (xml, "xdmcp_vbox", GTK_TYPE_WIDGET);
	GtkWidget *no_xdmcp_label = glade_helper_get (xml, "no_xdmcp_label", GTK_TYPE_WIDGET);

#ifndef HAVE_LIBXDMCP
	gtk_widget_show (no_xdmcp_label);
	gtk_widget_hide (xdmcp_vbox);
#else /* HAVE_LIBXDMCP */
	gtk_widget_hide (no_xdmcp_label);
	gtk_widget_show (xdmcp_vbox);
#endif /* HAVE_LIBXDMCP */

	gtk_widget_set_sensitive (xdmcp_label, 
				  GTK_TOGGLE_BUTTON (xdmcp_toggle)->active);
	gtk_widget_set_sensitive (xdmcp_frame, 
				  GTK_TOGGLE_BUTTON (xdmcp_toggle)->active);

	g_signal_connect (G_OBJECT (xdmcp_toggle), "toggled",
			  G_CALLBACK (sensitivity_toggled),
			  xdmcp_frame);
	g_signal_connect (G_OBJECT (xdmcp_toggle), "toggled",
			  G_CALLBACK (sensitivity_toggled),
			  xdmcp_label);
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
	if (strncmp (base1, "lib", 3) == 0)
		strcpy (base1, &base1[3]);
	if (strncmp (base2, "lib", 3) == 0)
		strcpy (base2, &base2[3]);
	p = strstr (base1, ".so");
	if (p != NULL)
		*p = '\0';
	p = strstr (base2, ".so");
	if (p != NULL)
		*p = '\0';

	ret = (strcmp (base1, base2) == 0);

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
		if (strcmp (vec[i], theme) == 0) {
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

static void
acc_modules_toggled (GtkWidget *toggle, gpointer data)
{
	VeConfig *cfg = ve_config_get (config_file);
	gboolean add_gtk_modules = ve_config_get_bool (cfg,
	                                               GDM_KEY_ADD_GTK_MODULES);
	char *modules_list = ve_config_get_string (cfg,
	                                           GDM_KEY_GTK_MODULES_LIST);

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
			EXPANDED_LIBDIR "/gtk-2.0/modules/libkeymouselistener",
			GDM_DELIMITER_MODULES);
		modules_list = strings_list_add (modules_list,
			EXPANDED_LIBDIR "/gtk-2.0/modules/libdwellmouselistener",
			GDM_DELIMITER_MODULES);
		add_gtk_modules = TRUE;
	}

	if (ve_string_empty (modules_list))
		add_gtk_modules = FALSE;

	ve_config_set_string (cfg, GDM_KEY_GTK_MODULES_LIST,
	                      ve_sure_string (modules_list));
	ve_config_set_bool (cfg, GDM_KEY_ADD_GTK_MODULES,
	                    add_gtk_modules);
	ve_config_save (cfg, FALSE /* force */);

	g_free (modules_list);

	update_key (GDM_KEY_GTK_MODULES_LIST);
	update_key (GDM_KEY_ADD_GTK_MODULES);
}

static void
test_sound (GtkWidget *button, gpointer data)
{
	GtkWidget *acc_sound_file_label = data;
	const char *file = gtk_label_get_text (GTK_LABEL (acc_sound_file_label));
	const char *argv[3];

	if (strcmp (_("None"), file) == 0 ||
	    access (file, R_OK) != 0 ||
	    ve_string_empty (GdmSoundProgram))
	       return;

	argv[0] = GdmSoundProgram;
	argv[1] = file;
	argv[2] = NULL;

	g_spawn_async ("/" /* working directory */,
		       (char **)argv,
		       NULL /* envp */,
		       0    /* flags */,
		       NULL /* child setup */,
		       NULL /* user data */,
		       NULL /* child pid */,
		       NULL /* error */);
}

static void
no_sound_cb (GtkWidget *widget, gpointer data)
{
	VeConfig *config;
	GtkWidget *acc_no_sound_file, *acc_sound_test;
	GtkWidget *acc_sound_file_label = data;
	const char *key = g_object_get_data (G_OBJECT (acc_sound_file_label),
	                                     "key");
	const char *nosound_button;
	const char *soundtest_button;
	char *sound_key, *val, *config_val;
	
	nosound_button = g_strconcat ("acc_nosound_", key, "_button", NULL);
	soundtest_button = g_strconcat ("acc_soundtest_", key, "_button", NULL);

	acc_no_sound_file = glade_helper_get (xml, nosound_button,
	                                      GTK_TYPE_BUTTON);
	acc_sound_test = glade_helper_get (xml, soundtest_button,
                                              GTK_TYPE_BUTTON);

	config = ve_config_get (config_file);
	gtk_label_set_text (GTK_LABEL (acc_sound_file_label), _("None"));
	gtk_widget_set_sensitive (acc_no_sound_file, FALSE);
	gtk_widget_set_sensitive (acc_sound_test, FALSE);

	if (strcmp (key, "ready") == 0) {
		sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_READY_FILE);
	} else if (strcmp (key, "success") == 0) {
		sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
	} else if (strcmp (key, "failure") == 0) {
		sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);
	} else {
		sound_key = NULL;
	}

	val = ve_config_get_string (config, sound_key);
	config_val = ve_sure_string (val);

	if (config_val != NULL && *config_val != '\0') {
		ve_config_set_string (config, sound_key, "\0");
		ve_config_save (config, FALSE /* force */);
		update_greeters ();
	}

	g_free (sound_key);
	g_free (val);
}

static void
sound_response (GtkWidget *file_dialog, gint response, gpointer data)
{
	GtkWidget *acc_no_sound_file, *acc_sound_test;
	GtkWidget *acc_sound_file_label = data;
	const char *key = g_object_get_data (G_OBJECT (acc_sound_file_label), "key");

	const char *nosound_button;
	const char *soundtest_button;
	nosound_button = g_strconcat("acc_nosound_",key,"_button",NULL);
	soundtest_button = g_strconcat("acc_soundtest_",key,"_button",NULL);

	acc_no_sound_file = glade_helper_get (xml, nosound_button,
		GTK_TYPE_BUTTON);
	acc_sound_test = glade_helper_get (xml, soundtest_button,
		GTK_TYPE_BUTTON);
	if (response == GTK_RESPONSE_ACCEPT) {
		VeConfig *config = ve_config_get (config_file);
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		char *val, *sound_key;

		gtk_label_set_text (GTK_LABEL (acc_sound_file_label), filename);

		gtk_widget_set_sensitive (acc_no_sound_file, TRUE);
		gtk_widget_set_sensitive (acc_sound_test, TRUE);


		if (strcmp (key, "ready") == 0 ) {
			have_sound_ready_file = TRUE;
			sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_READY_FILE);
		} else if (strcmp (key, "success") == 0 ) {
			have_sound_success_file = TRUE;
			sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
		} else if (strcmp (key, "failure") == 0 ) {
			have_sound_failure_file = TRUE;
			sound_key = g_strdup(GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);
		} else {
			sound_key = NULL;
		}

		val = ve_config_get_string (config, sound_key);

		if (strcmp (ve_sure_string (val), ve_sure_string (filename)) != 0) {
			ve_config_set_string (config, sound_key,
		    ve_sure_string (filename));
			ve_config_save (config, FALSE /* force */);
			update_greeters ();
		}
		g_free (val);

	} else {

		if (strcmp (key,"ready") == 0) {
			if (have_sound_ready_file) {
				gtk_widget_set_sensitive (acc_no_sound_file, TRUE);
				gtk_widget_set_sensitive (acc_sound_test, TRUE);
			} else {
				gtk_widget_set_sensitive (acc_no_sound_file, FALSE);
				gtk_widget_set_sensitive (acc_sound_test, FALSE);
			}
		} else if (strcmp (key,"success") == 0) {
			if (have_sound_success_file) {
				gtk_widget_set_sensitive (acc_no_sound_file, TRUE);
				gtk_widget_set_sensitive (acc_sound_test, TRUE);
			} else {
				gtk_widget_set_sensitive (acc_no_sound_file, FALSE);
				gtk_widget_set_sensitive (acc_sound_test, FALSE);
			}
		} else if (strcmp (key,"failure") == 0) {
			if (have_sound_failure_file) {
				gtk_widget_set_sensitive (acc_no_sound_file, TRUE);
				gtk_widget_set_sensitive (acc_sound_test, TRUE);
			} else {
				gtk_widget_set_sensitive (acc_no_sound_file, FALSE);
				gtk_widget_set_sensitive (acc_sound_test, FALSE);
			}
		}
		
	}
	gtk_widget_destroy (file_dialog);
}

static void
browse_sound_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget *setup_dialog = glade_helper_get (xml, "setup_dialog",
		GTK_TYPE_WINDOW);
	GtkWidget *label = data;
        GtkFileFilter *filter;
	GtkWidget *file_dialog = gtk_file_chooser_dialog_new (_("Open File"),
					GTK_WINDOW (setup_dialog),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);

        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
		EXPANDED_DATADIR "/sounds");

	g_signal_connect (G_OBJECT (file_dialog), "destroy",
		G_CALLBACK (gtk_widget_destroyed), &file_dialog);
	g_signal_connect (G_OBJECT (file_dialog), "response",
		G_CALLBACK (sound_response), label);

	gtk_widget_show (file_dialog);
}

static void
setup_accessibility_support (void)
{
	GtkWidget *acc_modules = glade_helper_get (xml, "acc_modules",
	                         GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *acc_sound_ready_file_label = glade_helper_get (xml,
	                                        "acc_sound_ready_file",
	                                        GTK_TYPE_LABEL);
	GtkWidget *acc_sound_ready_file = glade_helper_get (xml,
	                                  "acc_sound_ready_button",
	                                  GTK_TYPE_BUTTON);
	GtkWidget *acc_no_sound_ready_file = glade_helper_get (xml,
	                                     "acc_nosound_ready_button",
	                                     GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_test_ready = glade_helper_get (xml,
	                                  "acc_soundtest_ready_button",
	                                  GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_success_file_label = glade_helper_get (xml,
	                                          "acc_sound_success_file",
	                                          GTK_TYPE_LABEL);
	GtkWidget *acc_sound_success_file = glade_helper_get (xml,
	                                    "acc_sound_success_button",
	                                    GTK_TYPE_BUTTON);
	GtkWidget *acc_no_sound_success_file = glade_helper_get (xml,
	                                       "acc_nosound_success_button",
	                                       GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_test_success = glade_helper_get (xml,
	                                    "acc_soundtest_success_button",
	                                    GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_failure_file_label = glade_helper_get (xml,
	                                          "acc_sound_failure_file",
	                                          GTK_TYPE_LABEL);
	GtkWidget *acc_sound_failure_file = glade_helper_get (xml,
	                                    "acc_sound_failure_button",
	                                    GTK_TYPE_BUTTON);
	GtkWidget *acc_no_sound_failure_file = glade_helper_get (xml,
	                                       "acc_nosound_failure_button",
	                                       GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_test_failure = glade_helper_get (xml,
	                                    "acc_soundtest_failure_button",
	                                    GTK_TYPE_BUTTON);
	                  
	gchar *ready_key = g_strdup("ready");
	gchar *success_key = g_strdup("success");
	gchar *failure_key = g_strdup("failure");
	VeConfig *config = ve_config_get (config_file);
	gboolean add_gtk_modules = ve_config_get_bool (config,
	                                               GDM_KEY_ADD_GTK_MODULES);
	char *modules_list = ve_config_get_string (config,
	                                           GDM_KEY_GTK_MODULES_LIST);
	char *val;

	g_object_set_data (G_OBJECT (acc_sound_ready_file_label), "key",
	                   ready_key);
	g_object_set_data (G_OBJECT (acc_sound_success_file_label), "key",
	                   success_key);
	g_object_set_data (G_OBJECT (acc_sound_failure_file_label), "key",
	                   failure_key);

	if (add_gtk_modules &&
	    modules_list_contains (modules_list, "gail") &&
		modules_list_contains (modules_list, "atk-bridge") &&
		modules_list_contains (modules_list, "dwellmouselistener") &&
		modules_list_contains (modules_list, "keymouselistener")) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (acc_modules),
					      TRUE);
	} else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (acc_modules),
					      FALSE);
	}

	val = ve_config_get_string (config, GDM_KEY_SOUND_ON_LOGIN_READY_FILE);

	if (val != NULL && *val != '\0') {
		gtk_label_set_text (GTK_LABEL (acc_sound_ready_file_label), val);
	} else {
		gtk_label_set_text (GTK_LABEL (acc_sound_ready_file_label), _("None"));
		gtk_widget_set_sensitive (acc_no_sound_ready_file, FALSE);
		gtk_widget_set_sensitive (acc_sound_test_ready, FALSE);
	}

	val = ve_config_get_string (config, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);

	if (val != NULL && *val != '\0') {
		gtk_label_set_text (GTK_LABEL (acc_sound_success_file_label), val);
	} else {
		gtk_label_set_text (GTK_LABEL (acc_sound_success_file_label), _("None"));
		gtk_widget_set_sensitive (acc_no_sound_success_file, FALSE);
		gtk_widget_set_sensitive (acc_sound_test_success, FALSE);
	}

	val = ve_config_get_string (config, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

	if (val != NULL && *val != '\0') {
		gtk_label_set_text (GTK_LABEL (acc_sound_failure_file_label), val);
	} else {
		gtk_label_set_text (GTK_LABEL (acc_sound_failure_file_label), _("None"));
		gtk_widget_set_sensitive (acc_no_sound_failure_file, FALSE);
		gtk_widget_set_sensitive (acc_sound_test_failure, FALSE);
	}

	g_signal_connect (G_OBJECT (acc_modules), "toggled",
			  G_CALLBACK (acc_modules_toggled),
			  NULL);
	g_signal_connect (G_OBJECT (acc_sound_ready_file), "clicked",
			  G_CALLBACK (browse_sound_cb),
			  acc_sound_ready_file_label);
	g_signal_connect (G_OBJECT (acc_no_sound_ready_file), "clicked",
			  G_CALLBACK (no_sound_cb),
			  acc_sound_ready_file_label);
	g_signal_connect (G_OBJECT (acc_sound_test_ready), "clicked",
			  G_CALLBACK (test_sound),
			  acc_sound_ready_file_label);
	g_signal_connect (G_OBJECT (acc_sound_success_file), "clicked",
			  G_CALLBACK (browse_sound_cb),
			  acc_sound_success_file_label);
	g_signal_connect (G_OBJECT (acc_no_sound_success_file), "clicked",
			  G_CALLBACK (no_sound_cb),
			  acc_sound_success_file_label);
	g_signal_connect (G_OBJECT (acc_sound_test_success), "clicked",
			  G_CALLBACK (test_sound),
			  acc_sound_success_file_label);
	g_signal_connect (G_OBJECT (acc_sound_failure_file), "clicked",
			  G_CALLBACK (browse_sound_cb),
			  acc_sound_failure_file_label);
	g_signal_connect (G_OBJECT (acc_no_sound_failure_file), "clicked",
			  G_CALLBACK (no_sound_cb),
			  acc_sound_failure_file_label);
	g_signal_connect (G_OBJECT (acc_sound_test_failure), "clicked",
			  G_CALLBACK (test_sound),
			  acc_sound_failure_file_label);
}

static void
background_toggled (void)
{
	GtkWidget *no_bg = glade_helper_get (xml, "sg_no_bg_rb", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *image_bg = glade_helper_get (xml, "sg_image_bg_rb", GTK_TYPE_TOGGLE_BUTTON);
	/*GtkWidget *color_bg = glade_helper_get (xml, "sg_color_bg_rb", GTK_TYPE_TOGGLE_BUTTON);*/
	GtkWidget *scale = glade_helper_get (xml, "sg_scale_background", GTK_TYPE_WIDGET);
	GtkWidget *image = glade_helper_get (xml, "sg_backimage", GTK_TYPE_WIDGET);
	GtkWidget *onlycolor = glade_helper_get (xml, "sg_remote_color_only", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *color_label = glade_helper_get (xml, "sg_backcolor_label", GTK_TYPE_WIDGET);
	GtkWidget *color = glade_helper_get (xml, "sg_backcolor", GTK_TYPE_WIDGET);
	GtkWidget *browse_button = glade_helper_get (xml, "sg_browsebackimage", GTK_TYPE_WIDGET);

	if (GTK_TOGGLE_BUTTON (no_bg)->active) {
		gtk_widget_set_sensitive (scale, FALSE);
		gtk_widget_set_sensitive (image, FALSE);
		gtk_widget_set_sensitive (onlycolor, FALSE);
		gtk_widget_set_sensitive (color_label, FALSE);
		gtk_widget_set_sensitive (color, FALSE);
		gtk_widget_set_sensitive (browse_button, FALSE);
	} else if (GTK_TOGGLE_BUTTON (image_bg)->active) {
		gtk_widget_set_sensitive (scale, TRUE);
		gtk_widget_set_sensitive (image, TRUE);
		gtk_widget_set_sensitive (onlycolor, TRUE);
		gtk_widget_set_sensitive (browse_button, TRUE);
		if (GTK_TOGGLE_BUTTON (onlycolor)->active) {
			gtk_widget_set_sensitive (color_label, TRUE);
			gtk_widget_set_sensitive (color, TRUE);
		} else {
			gtk_widget_set_sensitive (color_label, FALSE);
			gtk_widget_set_sensitive (color, FALSE);
		}
	} else /* if (GTK_TOGGLE_BUTTON (color_bg)->active) */ {
		gtk_widget_set_sensitive (scale, FALSE);
		gtk_widget_set_sensitive (image, FALSE);
		gtk_widget_set_sensitive (onlycolor, FALSE);
		gtk_widget_set_sensitive (color_label, TRUE);
		gtk_widget_set_sensitive (color, TRUE);
		gtk_widget_set_sensitive (browse_button, FALSE);
	}
}

static void
setup_background_support (void)
{
	GtkWidget *no_bg     = glade_helper_get (xml, "sg_no_bg_rb",
	                                         GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *image_bg  = glade_helper_get (xml, "sg_image_bg_rb",
	                                         GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *color_bg  = glade_helper_get (xml, "sg_color_bg_rb",
	                                         GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *onlycolor = glade_helper_get (xml, "sg_remote_color_only",
	                                         GTK_TYPE_TOGGLE_BUTTON);

	g_signal_connect (G_OBJECT (no_bg), "toggled",
			  G_CALLBACK (background_toggled),
			  NULL);
	g_signal_connect (G_OBJECT (image_bg), "toggled",
			  G_CALLBACK (background_toggled),
			  NULL);
	g_signal_connect (G_OBJECT (color_bg), "toggled",
			  G_CALLBACK (background_toggled),
			  NULL);
	g_signal_connect (G_OBJECT (onlycolor), "toggled",
			  G_CALLBACK (background_toggled),
			  NULL);

	background_toggled ();
}

static char *
get_theme_dir (void)
{
	char *theme_dir;

	theme_dir = ve_config_get_string (ve_config_get (config_file),
					  GDM_KEY_GRAPHICAL_THEME_DIR);

	if (theme_dir == NULL ||
	    theme_dir[0] == '\0' ||
	    access (theme_dir, R_OK) != 0) {
		g_free (theme_dir);
		theme_dir = g_strdup (EXPANDED_DATADIR "/gdm/themes/");
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
	GtkWidget *label;
	GtkWidget *preview    = glade_helper_get (xml, "gg_theme_preview",
	                                          GTK_TYPE_IMAGE);
	GtkWidget *no_preview = glade_helper_get (xml, "gg_theme_no_preview",
	                                          GTK_TYPE_WIDGET);
	GtkWidget *del_button = glade_helper_get (xml, "gg_delete_theme",
	                                          GTK_TYPE_BUTTON);
	char *screenshot;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value  = {0, };

    gboolean GdmGraphicalThemeRand = ve_config_get_bool (
        ve_config_get (config_file),
        GDM_KEY_GRAPHICAL_THEME_RAND);

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_widget_set_sensitive (del_button, FALSE);
		return;
	}

	/* Default to allow deleting of themes */
	gtk_widget_set_sensitive (del_button, TRUE);

	/* Determine if the theme selected is currently active */
	if (GdmGraphicalThemeRand) {
		gtk_tree_model_get_value (model, &iter, THEME_COLUMN_SELECTED_LIST, &value);
	} else {
		gtk_tree_model_get_value (model, &iter, THEME_COLUMN_SELECTED, &value);
	}

	/* Do not allow deleting of active themes */
	if (g_value_get_boolean (&value)) {
		gtk_widget_set_sensitive (del_button, FALSE);
	}
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_AUTHOR,
				  &value);
	label = glade_helper_get (xml, "gg_author_text_view",
				  GTK_TYPE_TEXT_VIEW);
	textview_set_buffer (GTK_TEXT_VIEW (label),
			     ve_sure_string (g_value_get_string (&value)));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_COPYRIGHT,
				  &value);
	label = glade_helper_get (xml, "gg_copyright_text_view",
				  GTK_TYPE_TEXT_VIEW);
	textview_set_buffer (GTK_TEXT_VIEW (label),
			    ve_sure_string (g_value_get_string (&value)));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_DESCRIPTION,
				  &value);
	label = glade_helper_get (xml, "gg_desc_text_view",
				  GTK_TYPE_TEXT_VIEW);
	textview_set_buffer (GTK_TEXT_VIEW (label),
			    ve_sure_string (g_value_get_string (&value)));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_SCREENSHOT,
				  &value);
	screenshot = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	gtk_widget_hide (preview);
	gtk_widget_show (no_preview);

	if ( ! ve_string_empty (screenshot) &&
	    access (screenshot, R_OK) == 0) {
		GdkPixbuf *pb;
		pb = gdk_pixbuf_new_from_file (screenshot, NULL);
		if (pb != NULL) {
			if (gdk_pixbuf_get_width (pb) > 200 ||
			    gdk_pixbuf_get_height (pb) > 150) {
				GdkPixbuf *pb2;
				pb2 = gdk_pixbuf_scale_simple
					(pb, 200, 150,
					 GDK_INTERP_BILINEAR);
				g_object_unref (G_OBJECT (pb));
				pb = pb2;
			}

			gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pb);
			g_object_unref (G_OBJECT (pb));

			gtk_widget_show (preview);
			gtk_widget_hide (no_preview);
		}
	}

	g_free (screenshot);
}

static GtkTreeIter *
read_themes (GtkListStore *store, const char *theme_dir, DIR *dir,
	     const char *select_item)
{
	struct dirent *dent;
	GtkTreeIter *select_iter = NULL;

	while ((dent = readdir (dir)) != NULL) {
		char *n, *file, *name, *desc, *author, *copyright, *ss;
		char *full;
		GtkTreeIter iter;
		gboolean sel_theme;
		gboolean sel_themes;
		VeConfig *theme_file;
		if (dent->d_name[0] == '.')
			continue;
		n = g_strconcat (theme_dir, "/", dent->d_name,
				 "/GdmGreeterTheme.desktop", NULL);
		if (access (n, R_OK) != 0) {
			g_free (n);
			n = g_strconcat (theme_dir, "/", dent->d_name,
					 "/GdmGreeterTheme.info", NULL);
		}
		if (access (n, R_OK) != 0) {
			g_free (n);
			continue;
		}

		theme_file = ve_config_new (n);

		file = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Greeter");
		if (ve_string_empty (file)) {
			g_free (file);
			file = g_strconcat (dent->d_name, ".xml", NULL);
		}

		full = g_strconcat (theme_dir, "/", dent->d_name,
				    "/", file, NULL);
		if (access (full, R_OK) != 0) {
			g_free (file);
			g_free (full);
			g_free (n);
			continue;
		}
		g_free (full);

		if (selected_theme != NULL &&
		    strcmp (dent->d_name, selected_theme) == 0)
			sel_theme = TRUE;
		else
			sel_theme = FALSE;

		if (selected_themes != NULL &&
		    themes_list_contains (selected_themes, dent->d_name))
			sel_themes = TRUE;
		else
			sel_themes = FALSE;

		name = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Name");
		if (ve_string_empty (name)) {
			g_free (name);
			name = g_strdup (dent->d_name);
		}
		desc = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Description");
		author = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Author");
		copyright = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Copyright");
		ss = ve_config_get_translated_string
			(theme_file, "GdmGreeterTheme/Screenshot");

		ve_config_destroy (theme_file);

		if (ss != NULL)
			full = g_strconcat (theme_dir, "/", dent->d_name,
					    "/", ss, NULL);
		else
			full = NULL;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    THEME_COLUMN_SELECTED, sel_theme,
				    THEME_COLUMN_SELECTED_LIST, sel_themes,
				    THEME_COLUMN_DIR, dent->d_name,
				    THEME_COLUMN_FILE, file,
				    THEME_COLUMN_NAME, name,
				    THEME_COLUMN_DESCRIPTION, desc,
				    THEME_COLUMN_AUTHOR, author,
				    THEME_COLUMN_COPYRIGHT, copyright,
				    THEME_COLUMN_SCREENSHOT, full,
				    -1);

		if (select_item != NULL &&
		    strcmp (dent->d_name, select_item) == 0) {
			/* anality */ g_free (select_iter);
			select_iter = g_new0 (GtkTreeIter, 1);
			*select_iter = iter;
		}

		g_free (file);
		g_free (name);
		g_free (desc);
		g_free (author);
		g_free (copyright);
		g_free (ss);
		g_free (full);
		g_free (n);
	}

	return select_iter;
}

static gboolean
greeter_theme_timeout (GtkWidget *toggle)
{
	char *theme;
	char *themes;

	VeConfig *config = ve_config_get (config_file);

	theme  = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME);
	themes = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEMES);

	/* If no checkbox themes selected */
	if (selected_themes == NULL)
		selected_themes = "";

	/* If themes have changed from the config file, update it. */
	if ((strcmp (ve_sure_string (theme),
		ve_sure_string (selected_theme)) != 0) ||
	    (strcmp (ve_sure_string (themes),
		ve_sure_string (selected_themes)) != 0)) {

		ve_config_set_string (config, GDM_KEY_GRAPHICAL_THEME,
			selected_theme);
		ve_config_set_string (config, GDM_KEY_GRAPHICAL_THEMES,
			selected_themes);

		ve_config_save (config, FALSE /* force */);
		update_greeters ();
	}

	g_free (theme);
	g_free (themes);
	return FALSE;
}

static void
selected_toggled (GtkCellRendererToggle *cell,
		  char                  *path_str,
		  gpointer               data)
{
	gchar *theme_name = NULL;
	GtkTreeModel *model = GTK_TREE_MODEL (data);
	GtkTreeIter selected_iter;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreePath *sel_path = gtk_tree_path_new_from_string (path_str);
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);
	GtkWidget *del_button = glade_helper_get (xml, "gg_delete_theme",
						  GTK_TYPE_BUTTON);
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
			} else {
				gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					THEME_COLUMN_SELECTED, FALSE,
					-1); /* Toggle OFF */
			}

			gtk_tree_path_next (path);
		}
	} else { /* Checkboxes */

		/* Clear list of all selected themes */
		g_free(selected_themes);
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
					gtk_widget_set_sensitive (del_button, TRUE);
				} else {
					gtk_list_store_set (GTK_LIST_STORE (model), &iter,
						THEME_COLUMN_SELECTED_LIST,
						TRUE, -1); /* Toggle ON */
					gtk_widget_set_sensitive (del_button, FALSE);
				}
			}
	
			gtk_tree_model_get (model, &iter, THEME_COLUMN_SELECTED_LIST,
				&selected, THEME_COLUMN_DIR, &theme_name, -1);
	
			if (selected)
				selected_themes = strings_list_add (selected_themes,
					theme_name, GDM_DELIMITER_THEMES);
	
			g_free(theme_name);
			gtk_tree_path_next (path);
		}

		if (selected_themes == NULL)
			selected_themes = g_strdup("");
	}

	gtk_tree_path_free (path);
	gtk_tree_path_free (sel_path);

	run_timeout (theme_list, 500, greeter_theme_timeout);
}

static gboolean
is_ext (const char *filename, const char *ext)
{
	const char *dot;

	dot = strrchr (filename, '.');
	if (dot == NULL)
		return FALSE;

	if (strcmp (dot, ext) == 0)
		return TRUE;
	else
		return FALSE;
}

/* sense the right unzip program */
static char *
find_unzip (const char *filename)
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
			if (access (tryb[i], X_OK) == 0)
				return g_strdup (tryb[i]);
		}
	}

	prog = g_find_program_in_path ("gunzip");
	if (prog != NULL)
		return prog;

	for (i = 0; tryg[i] != NULL; i++) {
		if (access (tryg[i], X_OK) == 0)
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
		if (access (try[i], X_OK) == 0)
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
		if (access (try[i], X_OK) == 0)
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
		if (access (try[i], X_OK) == 0)
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

		if (strncmp (buf, dir, dirlen) != 0) {
			*error = _("Archive is not of a single subdirectory");
			g_free (dir);
			return NULL;
		}

		if ( ! got_info) {
			s = g_strconcat (dir, "/GdmGreeterTheme.info", NULL);
			if (strcmp (buf, s) == 0)
				got_info = TRUE;
			g_free (s);
		}

		if ( ! got_info) {
			s = g_strconcat (dir, "/GdmGreeterTheme.desktop", NULL);
			if (strcmp (buf, s) == 0)
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
get_archive_dir (const char *filename, char **untar_cmd, char **error)
{
	char *quoted;
	char *tar;
	char *unzip;
	char *cmd;
	char *dir;
	FILE *fp;

	*untar_cmd = NULL;

	*error = NULL;

	if (access (filename, F_OK) != 0) {
		*error = _("File does not exist");
		return NULL;
	}

	/* Note that this adds './' In front to avoid troubles */
	quoted = ve_shell_quote_filename (filename);
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
		if (strcmp (dent->d_name, dir) == 0) {
			closedir (dp);
			return TRUE;
		}
	}
	closedir (dp);
	return FALSE;
}

static void
theme_install_response (GtkWidget *chooser, gint response, gpointer data)
{
	GtkListStore *store = data;
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);
	char *filename, *dir, *untar_cmd, *theme_dir, *cwd;
	GtkTreeIter *select_iter = NULL;
	GtkTreeSelection *selection;
	char *error;
	DIR *dp;
	gboolean success = FALSE;

	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (chooser);
		return;
	}

	cwd = g_get_current_dir ();
	theme_dir = get_theme_dir ();

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
	if (last_theme_installed != NULL)
		g_free (last_theme_installed);
	last_theme_installed = g_strdup (filename);
	if (filename == NULL) {
		GtkWidget *dlg =
			ve_hig_dialog_new (GTK_WINDOW (chooser),
					   GTK_DIALOG_MODAL | 
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   FALSE /* markup */,
					   _("No file selected"),
					   /* avoid warning */ "%s", "");
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		g_free (cwd);
		g_free (theme_dir);
		return;
	}

	if ( ! g_path_is_absolute (filename)) {
		char *f = g_build_filename (cwd, filename, NULL);
		g_free (filename);
		filename = f;
	}

	dir = get_archive_dir (filename, &untar_cmd, &error);

	/* FIXME: perhaps do a little bit more sanity checking of
	 * the archive */

	if (dir == NULL) {
		GtkWidget *dlg =
			ve_hig_dialog_new (GTK_WINDOW (chooser),
					   GTK_DIALOG_MODAL | 
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   FALSE /* markup */,
					   _("Not a theme archive"),
					   _("Details: %s"),
					   error);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		g_free (filename);
		g_free (cwd);
		g_free (theme_dir);
		return;
	}

	if (dir_exists (theme_dir, dir)) {
		char *fname = ve_filename_to_utf8 (dir);
		char *s;
		GtkWidget *button;
		GtkWidget *dlg;

		/* FIXME: if exists already perhaps we could also have an
		 * option to change the dir name */
		s = g_strdup_printf (_("Theme directory '%s' seems to be already "
				       "installed. Install again anyway?"),
				     fname);
		dlg = ve_hig_dialog_new
			(GTK_WINDOW (chooser),
			 GTK_DIALOG_MODAL | 
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_QUESTION,
			 GTK_BUTTONS_NONE,
			 FALSE /* markup */,
			 s,
			 /* avoid warning */ "%s", "");
		g_free (fname);
		g_free (s);

		button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
		gtk_dialog_add_action_widget (GTK_DIALOG (dlg), button, GTK_RESPONSE_NO);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);

		button = gtk_button_new_from_stock ("_Install Anyway");
		gtk_dialog_add_action_widget (GTK_DIALOG (dlg), button, GTK_RESPONSE_YES);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);

		gtk_dialog_set_default_response (GTK_DIALOG (dlg),
						 GTK_RESPONSE_YES);

		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
		if (gtk_dialog_run (GTK_DIALOG (dlg)) != GTK_RESPONSE_YES) {
			gtk_widget_destroy (dlg);
			g_free (filename);
			g_free (cwd);
			g_free (dir);
			g_free (theme_dir);
			return;
		}
		gtk_widget_destroy (dlg);
	}

	g_assert (untar_cmd != NULL);

	if (chdir (theme_dir) == 0 &&
	    /* this is a security sanity check */
	    strchr (dir, '/') == NULL &&
	    system (untar_cmd) == 0) {
		char *argv[5];
		char *quoted = g_strconcat ("./", dir, NULL);
		char *chown = find_chown ();
		char *chmod = find_chmod ();
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

	if ( ! success) {
		GtkWidget *dlg =
			ve_hig_dialog_new (GTK_WINDOW (chooser),
					   GTK_DIALOG_MODAL | 
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   FALSE /* markup */,
					   _("Some error occurred when "
					     "installing the theme"),
					   /* avoid warning */ "%s", "");
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
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
	g_free (dir);
	g_free (filename);
	g_free (cwd);
	g_free (theme_dir);

	gtk_widget_destroy (GTK_WIDGET (chooser));
}

static void
install_new_theme (GtkWidget *button, gpointer data)
{
	GtkListStore *store = data;
	static GtkWidget *chooser = NULL;
	GtkWidget *setup_dialog = glade_helper_get (xml, "setup_dialog",
		GTK_TYPE_WINDOW);
	
	chooser = gtk_file_chooser_dialog_new (_("Select new theme archive to install"),
					       GTK_WINDOW (setup_dialog),
					       GTK_FILE_CHOOSER_ACTION_OPEN,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       _("_Install"), GTK_RESPONSE_OK,
					       NULL);
	
	g_signal_connect (G_OBJECT (chooser), "destroy",
			  G_CALLBACK (gtk_widget_destroyed), &chooser);
	g_signal_connect (G_OBJECT (chooser), "response",
			  G_CALLBACK (theme_install_response), store);

	if (last_theme_installed != NULL)
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser),
			last_theme_installed);

	gtk_widget_show (chooser);
}

static void
delete_theme (GtkWidget *button, gpointer data)
{
	GtkListStore *store = data;
	GtkWidget *theme_list;
	GtkWidget *setup_dialog;
	GtkWidget *del_button;
	GtkTreeSelection *selection;
        char *dir, *name;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = {0, };
	GtkWidget *dlg;
	char *s;

	setup_dialog = glade_helper_get (xml, "setup_dialog", GTK_TYPE_WINDOW);
	theme_list = glade_helper_get (xml, "gg_theme_list",
				       GTK_TYPE_TREE_VIEW);
	del_button = glade_helper_get (xml, "gg_delete_theme",
				       GTK_TYPE_BUTTON);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter)) {
		/* should never get here since the button shuld not be
		 * enabled */
		return;
	}

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_SELECTED,
				  &value);
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

	s = g_strdup_printf (_("Do you really wish to remove theme '%s' from the system?"),
			     name);
	dlg = ve_hig_dialog_new
		(GTK_WINDOW (setup_dialog),
		 GTK_DIALOG_MODAL | 
		 GTK_DIALOG_DESTROY_WITH_PARENT,
		 GTK_MESSAGE_QUESTION,
		 GTK_BUTTONS_NONE,
		 FALSE /* markup */,
		 s,
		 /* avoid warning */ "%s", "");
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
		if (chdir (theme_dir) == 0 &&
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
		chdir (cwd);
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
	VeConfig *cfg;
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text = gtk_entry_get_text (GTK_ENTRY (entry));

	/* Get xserver section to update */
	GtkWidget *combobox = glade_helper_get (xml, "xserver_mod_combobox",
	                                        GTK_TYPE_COMBO_BOX);
	gchar *section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combobox));
	section = g_strconcat(GDM_KEY_SERVER_PREFIX, section, "/", NULL);

	if (strcmp (key, GDM_KEY_SERVER_NAME) == 0)
		section = g_strconcat(section, GDM_KEY_SERVER_NAME, NULL);
	else if (strcmp (key, GDM_KEY_SERVER_COMMAND) == 0)
		section = g_strconcat(section, GDM_KEY_SERVER_COMMAND, NULL);

	/* Locate this server's section */
	cfg = ve_config_get (config_file);

	/* Update this servers configuration */
	ve_config_set_string (cfg, section, ve_sure_string (text));
	ve_config_save (cfg, FALSE /* force */);
	g_free(section);

	return FALSE;
}

static gboolean
xserver_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");
	VeConfig *cfg;
	gboolean val;

	/* Get xserver section to update */
	GtkWidget *combobox = glade_helper_get (xml, "xserver_mod_combobox",
	                                        GTK_TYPE_COMBO_BOX);
	gchar *section = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combobox));
	section = g_strconcat(GDM_KEY_SERVER_PREFIX, section, "/", NULL);

	if (strcmp (key, GDM_KEY_SERVER_HANDLED) == 0)
		section = g_strconcat(section, GDM_KEY_SERVER_HANDLED, NULL);
	else if (strcmp (key, GDM_KEY_SERVER_FLEXIBLE) == 0)
		section = g_strconcat(section, GDM_KEY_SERVER_FLEXIBLE, NULL);
	else if (strcmp (key, GDM_KEY_SERVER_CHOOSER) == 0)
		section = g_strconcat(section, GDM_KEY_SERVER_CHOOSER, NULL);

	/* Locate this server's section */
	cfg = ve_config_get (config_file);
	val = ve_config_get_bool (cfg, section);

	/* Update this servers configuration */
	if ( ! ve_bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
		ve_config_set_bool (cfg, section,
				    GTK_TOGGLE_BUTTON (toggle)->active);
		ve_config_save (cfg, FALSE /* force */);
	}
	g_free(section);

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
xserver_append_combobox(GdmXServer *xserver, GtkComboBox *combobox)
{
	gtk_combo_box_append_text (combobox, xserver->id);
}

static void
xserver_populate_combobox(GtkComboBox* combobox)
{
    gint i,j;
    GSList *xservers;

	/* Get number of items in combobox */
	i = gtk_tree_model_iter_n_children(
	        gtk_combo_box_get_model (GTK_COMBO_BOX (combobox)), NULL);

    /* Delete all items from combobox */
    for (j = 0; j < i; j++) {
        gtk_combo_box_remove_text(combobox,0);
    }

    /* Populate combobox with list of current servers */
	xservers = xservers_get_server_definitions();
    g_slist_foreach(xservers, (GFunc) xserver_append_combobox, combobox);
}

static void
xserver_init_server_list()
{
	/* Get Widgets from glade */
	GtkWidget *treeview = glade_helper_get (xml, "xserver_tree_view",
	                                        GTK_TYPE_TREE_VIEW);
	GtkWidget *remove_button = glade_helper_get (xml, "xserver_remove_button",
	                                        GTK_TYPE_BUTTON);

	/* create list store */
	GtkListStore *store = gtk_list_store_new (XSERVER_NUM_COLUMNS,
	                            G_TYPE_INT    /* virtual terminal */,
	                            G_TYPE_STRING /* server type */,
	                            G_TYPE_STRING /* options */);

	/* Read all xservers to start from configuration */
	xservers_get_servers(store);
	gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
	                         GTK_TREE_MODEL (store));
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (treeview), TRUE);
	gtk_widget_set_sensitive (remove_button, FALSE);
}

static void
xserver_init_servers()
{
    GtkWidget *spinner, *combo, *entry, *add_button, *remove_button;

    /* Get Widgets from glade */
    spinner      = glade_helper_get (xml, "xserver_spin_button",
                                        GTK_TYPE_SPIN_BUTTON);
    combo = glade_helper_get (xml, "xserver_server_combobox",
                                        GTK_TYPE_COMBO_BOX);
    entry      = glade_helper_get (xml, "xserver_options_entry",
                                        GTK_TYPE_ENTRY);
    add_button   = glade_helper_get (xml, "xserver_add_button",
                                        GTK_TYPE_BUTTON);
    remove_button   = glade_helper_get (xml, "xserver_remove_button",
                                        GTK_TYPE_BUTTON);


    /* Init widget states */
	xserver_init_server_list();
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinner), 1);
    xserver_populate_combobox (GTK_COMBO_BOX (combo));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
    gtk_widget_set_sensitive (add_button, FALSE);
    gtk_widget_set_sensitive (remove_button, FALSE);
}

static void
xserver_row_selected(GtkTreeSelection *selection, gpointer data)
{
    GtkWidget *remove_button = glade_helper_get (xml, "xserver_remove_button",
                                                 GTK_TYPE_BUTTON);
    gtk_widget_set_sensitive (remove_button, TRUE);
}

/* Remove a server from the list of servers to start (not the same as
 * deleting a server definition) */
static void
xserver_remove(gpointer data)
{
	GtkWidget *treeview, *combo;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreeModel *model;
	VeConfig *cfg;
	gint vt;
    char vt_value[3];

    treeview = glade_helper_get (xml, "xserver_tree_view",
                                 GTK_TYPE_TREE_VIEW);
	combo = glade_helper_get (xml, "xserver_server_combobox",
	                          GTK_TYPE_COMBO_BOX);

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

	if (gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		char *key;

		/* Update config */
		cfg = ve_config_get (config_file);
		gtk_tree_model_get (model, &iter, XSERVER_COLUMN_VT, &vt, -1);


		g_snprintf (vt_value,  sizeof (vt_value), "%d", vt);
		key = g_object_get_data (G_OBJECT (combo), "key");
		key = g_strconcat (key, "/", vt_value, "=", NULL);
		ve_config_delete_key (cfg, key);
		ve_config_save (cfg, FALSE /* force */);

		/* Update gdmsetup */
		xserver_init_server_list();
		xserver_update_delete_sensitivity();
	}
}
	
/* Add a server to the list of servers to start (not the same as
 * creating a server definition) */
static void
xserver_add(gpointer data)
{
	VeConfig *cfg;
	GtkWidget *spinner, *combo, *entry, *button;
	gchar *string;
	char spinner_value[3], *key;

	/* Get Widgets from glade */
	spinner      = glade_helper_get (xml, "xserver_spin_button",
	                                 GTK_TYPE_SPIN_BUTTON);
	entry      = glade_helper_get (xml, "xserver_options_entry",
	                              GTK_TYPE_ENTRY);
	combo = glade_helper_get (xml, "xserver_server_combobox",
		                                    GTK_TYPE_COMBO_BOX);
	button   = glade_helper_get (xml, "xserver_add_button",
		                                    GTK_TYPE_BUTTON);

	/* Section in config to modify */
	key = g_object_get_data (G_OBJECT (combo), "key");

	/* String to add to config */
	g_snprintf (spinner_value,  sizeof (spinner_value), "%d", 
	            gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spinner)));

	key = g_strconcat (key, "/", spinner_value, "=", NULL);
	string = g_strconcat (gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo)),
	                      " ", gtk_entry_get_text (GTK_ENTRY (entry)),
	                      NULL);

	/* Add to config */
	cfg = ve_config_get (config_file);
	ve_config_set_string (cfg, key, ve_sure_string(string));
    ve_config_save (cfg, FALSE /* force */);

	/* Reinitialize gdmsetup */
	xserver_init_servers();
	xserver_update_delete_sensitivity();

	/* Free memory */
	g_free(string);
	g_free(key);
}

/* TODO: This section needs a little work until it is ready (mainly config
   section modifications) */
/* Create a server definition (not the same as removing a server
 * from the list of servers to start) */
static void
xserver_create(gpointer data)
{
	VeConfig *cfg;
	gboolean success;

	/* Init Widgets */
	GtkWidget *frame, *modify_combobox;
	GtkWidget *name_entry, *command_entry;
	GtkWidget *handled_check, *flexible_check;
	GtkWidget *greeter_radio, *chooser_radio;
	GtkWidget *create_button, *delete_button;

	/* Get Widgets from glade */
	frame           = glade_helper_get (xml, "xserver_modify_frame",
	                                    GTK_TYPE_FRAME);
	name_entry      = glade_helper_get (xml, "xserver_name_entry",
                                        GTK_TYPE_ENTRY);
	command_entry   = glade_helper_get (xml, "xserver_command_entry",
	                                    GTK_TYPE_ENTRY);
	handled_check   = glade_helper_get (xml, "xserver_handled_checkbutton",
	                                    GTK_TYPE_CHECK_BUTTON);
	flexible_check  = glade_helper_get (xml, "xserver_flexible_checkbutton",
	                                    GTK_TYPE_CHECK_BUTTON);
	greeter_radio   = glade_helper_get (xml, "xserver_greeter_radiobutton",
	                                    GTK_TYPE_RADIO_BUTTON);
	chooser_radio   = glade_helper_get (xml, "xserver_chooser_radiobutton",
	                                    GTK_TYPE_RADIO_BUTTON);
	modify_combobox = glade_helper_get (xml, "xserver_mod_combobox",
	                                    GTK_TYPE_COMBO_BOX);
	create_button   = glade_helper_get (xml, "xserver_create_button",
	                                    GTK_TYPE_BUTTON);
	delete_button   = glade_helper_get (xml, "xserver_delete_button",
	                                    GTK_TYPE_BUTTON);

	gtk_combo_box_append_text (GTK_COMBO_BOX (modify_combobox),
	                           "New Server");

	/* TODO: Create a new section for this server */
	/* TODO: Write this value to the config and update xservers list */
	cfg = ve_config_get (config_file);
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

static void
xserver_init_definitions()
{
    /* Init Widgets */
    GtkWidget *frame, *modify_combobox, *server_combobox;
    GtkWidget *name_entry, *command_entry;
    GtkWidget *handled_check, *flexible_check;
    GtkWidget *greeter_radio, *chooser_radio;
    GtkWidget *create_button, *delete_button;

    /* Get Widgets from glade */
    frame           = glade_helper_get (xml, "xserver_modify_frame",
                                        GTK_TYPE_FRAME);
    name_entry      = glade_helper_get (xml, "xserver_name_entry",
                                        GTK_TYPE_ENTRY);
    command_entry   = glade_helper_get (xml, "xserver_command_entry",
                                        GTK_TYPE_ENTRY);
    handled_check   = glade_helper_get (xml, "xserver_handled_checkbutton",
                                        GTK_TYPE_CHECK_BUTTON);
    flexible_check  = glade_helper_get (xml, "xserver_flexible_checkbutton",
                                        GTK_TYPE_CHECK_BUTTON);
    greeter_radio   = glade_helper_get (xml, "xserver_greeter_radiobutton",
                                        GTK_TYPE_RADIO_BUTTON);
    chooser_radio   = glade_helper_get (xml, "xserver_chooser_radiobutton",
                                        GTK_TYPE_RADIO_BUTTON);
    modify_combobox = glade_helper_get (xml, "xserver_mod_combobox",
                                        GTK_TYPE_COMBO_BOX);
    server_combobox = glade_helper_get (xml, "xserver_server_combobox",
                                        GTK_TYPE_COMBO_BOX);
    create_button   = glade_helper_get (xml, "xserver_create_button",
                                        GTK_TYPE_BUTTON);
    delete_button   = glade_helper_get (xml, "xserver_delete_button",
                                        GTK_TYPE_BUTTON);

    /* Populate comboboxes with servers*/
    xserver_populate_combobox (GTK_COMBO_BOX (server_combobox));
    xserver_populate_combobox (GTK_COMBO_BOX (modify_combobox));

	/* Default sensitivity and settings */
	gtk_widget_set_sensitive (frame, FALSE);
	gtk_widget_set_sensitive (delete_button, FALSE);
	gtk_widget_set_sensitive (create_button, FALSE);
	gtk_widget_grab_focus (modify_combobox);
	gtk_entry_set_text (GTK_ENTRY (name_entry), "");
	gtk_entry_set_text (GTK_ENTRY (command_entry), "");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (greeter_radio),TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chooser_radio),FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (handled_check),TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flexible_check),FALSE);

}

/* Deletes a server definition (not the same as removing a server
 * from the list of servers to start) */
static void
xserver_delete(gpointer data)
{
	/* Get xserver section to delete */
	GtkWidget *combobox = glade_helper_get (xml, "xserver_mod_combobox",
	                                        GTK_TYPE_COMBO_BOX);
	gchar *section = gtk_combo_box_get_active_text ( GTK_COMBO_BOX (combobox));

	/* Delete xserver section */
	VeConfig *cfg = ve_config_get (config_file);
	ve_config_delete_section (cfg, g_strconcat (GDM_KEY_SERVER_PREFIX,
	                                            section, NULL));

	/* Reinitialize definitions */
	xserver_init_definitions();
}

static void
setup_xserver_support (void)
{
	GtkWidget *frame, *treeview;
	GtkWidget *modify_combobox, *server_combobox;
	GtkWidget *name_entry, *command_entry;
	GtkWidget *handled_check, *flexible_check;
	GtkWidget *greeter_radio, *chooser_radio;
	GtkWidget *create_button, *delete_button, *add_button, *remove_button;
    GtkCellRenderer  *renderer;
    GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	/* Initialize the xserver settings */
	xserver_init_definitions();
	xserver_init_servers();

	/* TODO: In the future, resolution/refresh rate configuration */
	/* setup_xrandr_support (); */

	/* Get Widgets from glade */
	frame           = glade_helper_get (xml, "xserver_modify_frame",
	                                    GTK_TYPE_FRAME);
	treeview        = glade_helper_get (xml, "xserver_tree_view",
	                                    GTK_TYPE_TREE_VIEW);
	name_entry      = glade_helper_get (xml, "xserver_name_entry",
                                        GTK_TYPE_ENTRY);
	command_entry   = glade_helper_get (xml, "xserver_command_entry",
	                                    GTK_TYPE_ENTRY);
	handled_check   = glade_helper_get (xml, "xserver_handled_checkbutton",
	                                    GTK_TYPE_CHECK_BUTTON);
	flexible_check  = glade_helper_get (xml, "xserver_flexible_checkbutton",
	                                    GTK_TYPE_CHECK_BUTTON);
	greeter_radio   = glade_helper_get (xml, "xserver_greeter_radiobutton",
	                                    GTK_TYPE_RADIO_BUTTON);
	chooser_radio   = glade_helper_get (xml, "xserver_chooser_radiobutton",
	                                    GTK_TYPE_RADIO_BUTTON);
	modify_combobox = glade_helper_get (xml, "xserver_mod_combobox",
	                                    GTK_TYPE_COMBO_BOX);
	server_combobox = glade_helper_get (xml, "xserver_server_combobox",
	                                    GTK_TYPE_COMBO_BOX);
	create_button   = glade_helper_get (xml, "xserver_create_button",
	                                    GTK_TYPE_BUTTON);
	delete_button   = glade_helper_get (xml, "xserver_delete_button",
	                                    GTK_TYPE_BUTTON);
	remove_button   = glade_helper_get (xml, "xserver_remove_button",
	                                    GTK_TYPE_BUTTON);
	add_button   = glade_helper_get (xml, "xserver_add_button",
	                                    GTK_TYPE_BUTTON);

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
                                         "text",XSERVER_COLUMN_OPTIONS,
                                         NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Setup tree selections */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);


	/* Register these items with keys */
	g_object_set_data_full (G_OBJECT (modify_combobox), "key",
	                        g_strdup (GDM_KEY_SERVER_PREFIX),
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (server_combobox), "key",
	                        g_strdup (GDM_KEY_SECTION_SERVERS),
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
	g_object_set_data_full (G_OBJECT (chooser_radio), "key",
	                        g_strdup (GDM_KEY_SERVER_CHOOSER),
	                        (GDestroyNotify) g_free);

		
	/* Signals Handlers */
    g_signal_connect (G_OBJECT (name_entry), "changed",
	                  G_CALLBACK (xserver_entry_changed),NULL);
    g_signal_connect (G_OBJECT (command_entry), "changed",
	                  G_CALLBACK (xserver_entry_changed), NULL);
	g_signal_connect (modify_combobox, "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (server_combobox, "changed",
	                  G_CALLBACK (combobox_changed), NULL);
	g_signal_connect (G_OBJECT (handled_check), "toggled",
	                  G_CALLBACK (xserver_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (flexible_check), "toggled",
	                  G_CALLBACK (xserver_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (chooser_radio), "toggled",
	                  G_CALLBACK (xserver_toggle_toggled), NULL);
	/* TODO: In the future, allow creation of servers
	g_signal_connect (create_button, "clicked",
			  G_CALLBACK (xserver_create), NULL);
	*/
	g_signal_connect (delete_button, "clicked",
	                  G_CALLBACK (xserver_delete), NULL);
	g_signal_connect (add_button, "clicked",
	                  G_CALLBACK (xserver_add), NULL);
	g_signal_connect (remove_button, "clicked",
	                  G_CALLBACK (xserver_remove), NULL);
	g_signal_connect (G_OBJECT (selection), "changed",
	                  G_CALLBACK (xserver_row_selected), NULL);

}
static void
setup_graphical_themes (void)
{
	gboolean GdmGraphicalThemeRand;
	DIR *dir;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTreeIter *select_iter = NULL;
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);
	GtkWidget *button = glade_helper_get (xml, "gg_install_new_theme",
					      GTK_TYPE_BUTTON);
	GtkWidget *del_button = glade_helper_get (xml, "gg_delete_theme",
						  GTK_TYPE_BUTTON);
	GtkWidget *mode_combobox = glade_helper_get (xml, "gg_mode_combobox",
						     GTK_TYPE_COMBO_BOX);

	char *theme_dir = get_theme_dir ();

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (theme_list), TRUE);

	selected_theme = ve_config_get_string (ve_config_get (config_file),
					       GDM_KEY_GRAPHICAL_THEME);
	selected_themes = ve_config_get_string (ve_config_get (config_file),
						GDM_KEY_GRAPHICAL_THEMES);

	/* FIXME: If a theme directory contains the string GDM_DELIMITER_THEMES
		  in the name, then this theme won't work when trying to load as it
		  will be perceived as two different themes seperated by
		  GDM_DELIMITER_THEMES.  This can be fixed by setting up an escape
		  character for it, but I'm not sure if directories can have the
		  slash (/) character in them, so I just made GDM_DELIMITER_THEMES
		  equal to "/:" instead. */

	GdmGraphicalThemeRand = ve_config_get_bool (
		ve_config_get (config_file),
		GDM_KEY_GRAPHICAL_THEME_RAND);

	/* create list store */
	store = gtk_list_store_new (THEME_NUM_COLUMNS,
				    G_TYPE_BOOLEAN /* selected theme */,
				    G_TYPE_BOOLEAN /* selected themes */,
				    G_TYPE_STRING /* dir */,
				    G_TYPE_STRING /* file */,
				    G_TYPE_STRING /* name */,
				    G_TYPE_STRING /* desc */,
				    G_TYPE_STRING /* author */,
				    G_TYPE_STRING /* copyright */,
				    G_TYPE_STRING /* screenshot */);

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

	/* The text column */
	column   = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
		"text", THEME_COLUMN_NAME, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	/* Selection setup */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
		G_CALLBACK (gg_selection_changed), NULL);

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
		GError *error = NULL;
		GtkWidget *setup_dialog = glade_helper_get
			(xml, "setup_dialog", GTK_TYPE_WINDOW);
		static GtkWidget *dlg = NULL;

		if (dlg != NULL) {
			gtk_window_present (GTK_WINDOW (dlg));
			return;
		}

		if ( ! RUNNING_UNDER_GDM) {
			gnome_help_display_uri ("ghelp:gdm", &error);
			/* FIXME: handle errors nicer */
			if (error == NULL)
				return;
			g_error_free (error);
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
			 config_file);
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
add_to_size_group (GtkSizeGroup *sg, const char *wid)
{
	GtkWidget *w = glade_helper_get (xml, wid, GTK_TYPE_WIDGET);
	gtk_size_group_add_widget (sg, w);
}

static void
setup_gui (void)
{
	GtkWidget *dialog;
	GtkSizeGroup *sg;
	GtkWidget *label_welcome;
	GtkWidget *label_remote_welcome;
	gchar *GdmDefaultWelcome;
	gchar *GdmDefaultRemoteWelcome;

	xml = glade_helper_load ("gdmsetup.glade",
				 "setup_dialog",
				 GTK_TYPE_DIALOG,
				 TRUE /* dump_on_destroy */);

	dialog = glade_helper_get (xml, "setup_dialog", GTK_TYPE_DIALOG);
	label_welcome = glade_helper_get (xml,
		"sg_defaultwelcomelabel", GTK_TYPE_WIDGET);
	label_remote_welcome = glade_helper_get (xml,
		"sg_defaultremotewelcomelabel", GTK_TYPE_WIDGET);

	g_signal_connect (G_OBJECT (dialog), "delete_event",
			  G_CALLBACK (delete_event), NULL);
	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response), NULL);

	/* setup bold thingies */
	glade_helper_tagify_label (xml, "greeter_cat_label", "b");
	glade_helper_tagify_label (xml, "autologin_cat_label", "b");
	glade_helper_tagify_label (xml, "timedlogin_cat_label", "b");
	glade_helper_tagify_label (xml, "sg_logo_cat_label", "b");
	glade_helper_tagify_label (xml, "sg_background_cat_label", "b");
	glade_helper_tagify_label (xml, "options_cat_label", "b");
	glade_helper_tagify_label (xml, "gg_preview_cat_label", "b");
	glade_helper_tagify_label (xml, "gg_author_label", "b");
	glade_helper_tagify_label (xml, "gg_desc_label", "b");
	glade_helper_tagify_label (xml, "gg_copyright_label", "b");
	glade_helper_tagify_label (xml, "acc_options_cat_label", "b");
	glade_helper_tagify_label (xml, "xserver_cat_label", "b");

	setup_accessibility_support ();
	setup_xdmcp_support ();
	setup_background_support ();
	setup_greeter_backselect ();
	setup_graphical_themes ();
	setup_xserver_support ();

	sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	add_to_size_group (sg, "local_greeter");
	add_to_size_group (sg, "remote_greeter");
	add_to_size_group (sg, "welcome");
	add_to_size_group (sg, "remote_welcome");
	add_to_size_group (sg, "autologin_combo");
	add_to_size_group (sg, "timedlogin_combo");
	add_to_size_group (sg, "timedlogin_seconds");
	g_object_unref (G_OBJECT (sg));

	sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	add_to_size_group (sg, "local_greeter_label");
	add_to_size_group (sg, "remote_greeter_label");
	add_to_size_group (sg, "welcome_label");
	add_to_size_group (sg, "remote_welcome_label");
	add_to_size_group (sg, "autologin_label");
	add_to_size_group (sg, "timed_login_label");
	add_to_size_group (sg, "timedlogin_seconds_label");
	g_object_unref (G_OBJECT (sg));

	sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	add_to_size_group (sg, "greeter_table");
	add_to_size_group (sg, "autologin_table");
	add_to_size_group (sg, "timed_login_table");
	g_object_unref (G_OBJECT (sg));

	sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	add_to_size_group (sg, "autologin");
	add_to_size_group (sg, "timedlogin");
	g_object_unref (G_OBJECT (sg));

	/* Note: Just setting up a notify here doesn't mean
	   things will get updated.  You must also wire in
	   the logic for update in the daemon and/or greeters
	   (or chooser) or whereever this is actually being
	   updated. */

	/* General */
	setup_user_combobox ("autologin_combo",
			  GDM_KEY_AUTOMATICLOGIN);
	setup_user_combobox ("timedlogin_combo",
			  GDM_KEY_TIMED_LOGIN);

	setup_notify_toggle ("autologin",
			     GDM_KEY_AUTOMATICLOGIN_ENABLE,
			     NULL /* notify_key */);
	setup_notify_toggle ("timedlogin",
			     GDM_KEY_TIMED_LOGIN_ENABLE,
			     GDM_KEY_TIMED_LOGIN_ENABLE /* notify_key */);

	/* Security */
	setup_notify_toggle ("allowroot",
			     GDM_KEY_ALLOWROOT,
			     GDM_KEY_ALLOWROOT /* notify_key */);
	setup_notify_toggle ("allowremoteroot",
			     GDM_KEY_ALLOWREMOTEROOT,
			     GDM_KEY_ALLOWREMOTEROOT /* notify_key */);
	setup_notify_toggle ("allowremoteauto",
			     GDM_KEY_ALLOWREMOTEAUTOLOGIN,
			     GDM_KEY_ALLOWREMOTEAUTOLOGIN /* notify_key */);
	setup_notify_toggle ("sysmenu",
			     GDM_KEY_SYSMENU,
			     GDM_KEY_SYSMENU /* notify_key */);
	setup_notify_toggle ("config_available",
			     GDM_KEY_CONFIG_AVAILABLE,
			     GDM_KEY_CONFIG_AVAILABLE /* notify_key */);
	setup_notify_toggle ("chooser_button",
			     GDM_KEY_CHOOSER_BUTTON,
			     GDM_KEY_CHOOSER_BUTTON /* notify_key */);
	setup_notify_toggle ("disallow_tcp",
			     GDM_KEY_DISALLOWTCP,
			     GDM_KEY_DISALLOWTCP /* notify_key */);

	/* General */
	GdmDefaultWelcome = g_strdup (_(GDM_DEFAULT_WELCOME_MSG));
	GdmDefaultRemoteWelcome = g_strdup (_(GDM_DEFAULT_REMOTEWELCOME_MSG));

	gtk_label_set_text (GTK_LABEL (label_welcome), GdmDefaultWelcome);
	gtk_label_set_text (GTK_LABEL (label_remote_welcome), GdmDefaultRemoteWelcome);

	/* Security */
	setup_sensitivity_positive_toggle ("sysmenu", "config_available");
	setup_sensitivity_positive_toggle ("sysmenu", "chooser_button");

	setup_notify_toggle ("enable_debug",
			     GDM_KEY_DEBUG,
			     GDM_KEY_DEBUG /* notify_key */);
	setup_notify_toggle ("enable_xdmcp",
			     GDM_KEY_XDMCP,
			     GDM_KEY_XDMCP /* notify_key */);
	setup_notify_toggle ("honour_indirect",
			     GDM_KEY_INDIRECT,
			     "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("timedlogin_seconds",
		       GDM_KEY_TIMED_LOGIN_DELAY,
		       GDM_KEY_TIMED_LOGIN_DELAY /* notify_key */);
	setup_intspin ("retry_delay",
		       GDM_KEY_RETRYDELAY,
		       GDM_KEY_RETRYDELAY /* notify_key */);
	setup_intspin ("udpport",
		       GDM_KEY_UDPPORT,
		       GDM_KEY_UDPPORT /* notify_key */);

	setup_intspin ("maxpending",
		       GDM_KEY_MAXPEND,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("maxpendingindirect",
		       GDM_KEY_MAXINDIR,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("maxremotesessions",
		       GDM_KEY_MAXSESS,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("maxwait",
		       GDM_KEY_MAXWAIT,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("maxwaitindirect",
		       GDM_KEY_MAXINDWAIT,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("displaysperhost",
		       GDM_KEY_DISPERHOST,
		       "xdmcp/PARAMETERS" /* notify_key */);
	setup_intspin ("pinginterval",
		       GDM_KEY_PINGINTERVAL,
		       "xdmcp/PARAMETERS" /* notify_key */);

	setup_greeter_toggle ("fb_browser",
			      GDM_KEY_BROWSER);

	/* General */
	setup_greeter_combobox ("local_greeter", GDM_KEY_GREETER);
	setup_greeter_combobox ("remote_greeter", GDM_KEY_REMOTEGREETER);

	/* Greeter */
	setup_greeter_toggle ("sg_scale_background",
			      GDM_KEY_BACKGROUNDSCALETOFIT);
	setup_greeter_toggle ("sg_remote_color_only",
			      GDM_KEY_BACKGROUNDREMOTEONLYCOLOR);

	setup_greeter_image (dialog);
	setup_greeter_color ("sg_backcolor",
	                     GDM_KEY_BACKGROUNDCOLOR);

	setup_greeter_toggle ("sg_defaultwelcome",
	                      GDM_KEY_DEFAULT_WELCOME);
	setup_greeter_toggle ("sg_defaultremotewelcome",
	                      GDM_KEY_DEFAULT_REMOTEWELCOME);
	setup_greeter_untranslate_entry ("welcome",
	                                 GDM_KEY_WELCOME);
	setup_greeter_untranslate_entry ("remote_welcome",
	                                 GDM_KEY_REMOTEWELCOME);

	/* Accesibility */
	setup_greeter_toggle ("acc_theme",
	                      GDM_KEY_ALLOW_GTK_THEME_CHANGE);
	setup_greeter_toggle ("acc_sound_ready",
	                      GDM_KEY_SOUND_ON_LOGIN_READY);
	setup_greeter_toggle ("acc_sound_success",
	                      GDM_KEY_SOUND_ON_LOGIN_SUCCESS);
	setup_greeter_toggle ("acc_sound_failure",
	                      GDM_KEY_SOUND_ON_LOGIN_FAILURE);

	/* Face browser */
	setup_greeter_toggle ("fb_allusers",
			      GDM_KEY_INCLUDEALL);
	setup_face ();
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
	GtkWidget *setup_dialog = glade_helper_get (xml, "setup_dialog",
						    GTK_TYPE_WINDOW);
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

struct poptOption options [] = {
        { "config", 'c', POPT_ARG_STRING, &config_file, 0, N_("Alternative configuration file"), N_("CONFIGFILE") },
        { NULL, 0, 0, NULL, 0}
};

int 
main (int argc, char *argv[])
{
	GnomeProgram *program;
	poptContext ctx;
	guint sid;

	if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
		DOING_GDM_DEVELOPMENT = TRUE;
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL)
		RUNNING_UNDER_GDM = TRUE;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	program = gnome_program_init ("gdmsetup", VERSION, 
			    LIBGNOMEUI_MODULE /* module_info */,
			    argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    GNOME_PARAM_POPT_TABLE, options,
			    GNOME_PARAM_CREATE_DIRECTORIES, ! RUNNING_UNDER_GDM,
			    NULL);
        g_object_get (G_OBJECT (program),
                      GNOME_PARAM_POPT_CONTEXT, &ctx,
                      NULL);

	glade_gnome_init();

	/* It is not null if config file location is passed in via command line */
        if (config_file == NULL) {
		config_file = gdm_common_get_config_file ();
		if (config_file == NULL) {
			g_print (_("Could not access GDM configuration file.\n"));
			exit (EXIT_FAILURE);
		}
	}

	gdm_running = gdmcomm_check (config_file, FALSE /* gui_bitching */);

	if (RUNNING_UNDER_GDM) {
		char *gtkrc;
		char *theme_name;

		/* Set busy cursor */
		setup_cursor (GDK_WATCH);

		/* parse the given gtk rc first */
		gtkrc = ve_config_get_string (ve_config_get (config_file),
					      GDM_KEY_GTKRC);
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);
		g_free (gtkrc);

		theme_name = g_strdup (g_getenv ("GDM_GTK_THEME"));
		if (ve_string_empty (theme_name)) {
			g_free (theme_name);
			theme_name = ve_config_get_string
				(ve_config_get (config_file), GDM_KEY_GTK_THEME);
		}

		if ( ! ve_string_empty (theme_name)) {
			gdm_set_theme (theme_name);
		}

		/* evil, but oh well */
		g_type_class_ref (GTK_TYPE_WIDGET);	
	}

	glade_helper_add_glade_directory (GDM_GLADE_DIR);

	/* Make sure the user is root. If not, they shouldn't be messing with 
	 * GDM's configuration.
	 */

	if ( ! DOING_GDM_DEVELOPMENT &&
	     geteuid() != 0) {
		GtkWidget *fatal_error = 
			ve_hig_dialog_new (NULL /* parent */,
					   GTK_DIALOG_MODAL /* flags */,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   FALSE /* markup */,
					   _("You must be the root user to configure GDM."),
					   /* avoid warning */ "%s", "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	/* XXX: the setup proggie using a greeter config var for it's
	 * ui?  Say it ain't so.  Our config sections are SUCH A MESS */
	GdmIconMaxHeight = ve_config_get_int (ve_config_get (config_file),
					   GDM_KEY_ICONHEIGHT);
	GdmIconMaxWidth = ve_config_get_int (ve_config_get (config_file),
					   GDM_KEY_ICONWIDTH);
	GdmMinimalUID = ve_config_get_int (ve_config_get (config_file),
					   GDM_KEY_MINIMALUID);
	GdmIncludeAll = ve_config_get_bool (ve_config_get (config_file),
					   GDM_KEY_INCLUDEALL);
	GdmInclude = ve_config_get_string (ve_config_get (config_file),
					   GDM_KEY_INCLUDE);
	GdmExclude = ve_config_get_string (ve_config_get (config_file),
					   GDM_KEY_EXCLUDE);
	GdmSoundProgram = ve_config_get_string (ve_config_get (config_file),
						GDM_KEY_SOUND_PROGRAM);
	GdmAllowRoot = ve_config_get_bool (ve_config_get (config_file),
						GDM_KEY_ALLOWROOT);
	GdmAllowRemoteRoot = ve_config_get_bool (ve_config_get (config_file),
						GDM_KEY_ALLOWREMOTEROOT);
	if (ve_string_empty (GdmSoundProgram) ||
	    access (GdmSoundProgram, X_OK) != 0) {
		g_free (GdmSoundProgram);
		GdmSoundProgram = NULL;
	}

	setup_gui ();

	/* also setup third button to work as first to work in reverse
	 * situations transparently */
	sid = g_signal_lookup ("event",
			       GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    gdm_event,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	if (RUNNING_UNDER_GDM) {
		setup_disable_handler ();

		setup_cursor (GDK_LEFT_PTR);
	}

	gtk_main ();

	return 0;
}

/* EOF */
