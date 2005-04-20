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

static gboolean gdm_running = FALSE;
gint  GdmIconMaxHeight;
gint  GdmIconMaxWidth;
gint GdmMinimalUID = 100;
gchar *GdmExclude = NULL;
gchar *GdmInclude = NULL;
gboolean GdmIncludeAll;
gboolean GdmAllowRoot;
gboolean GdmAllowRemoteRoot;
static char *GdmSoundProgram = NULL;

static GladeXML *xml;

static GList *timeout_widgets = NULL;

static gchar *last_theme_installed = NULL;
static int last_remote_login_setting = -1;
static gboolean have_sound_ready_file = FALSE;
static gboolean have_sound_success_file = FALSE;
static gboolean have_sound_failure_file = FALSE;

enum {
	THEME_COLUMN_SELECTED,
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

static char *selected_theme = NULL;
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
	gdm_running = gdmcomm_check (FALSE /* gui_bitching */);

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
	gdm_running = gdmcomm_check (FALSE /* gui_bitching */);

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
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
	gboolean val;

	val = ve_config_get_bool (config, key);
	if ( ! ve_bool_equal (val, GTK_TOGGLE_BUTTON (toggle)->active)) {
		ve_config_set_bool (config, key,
				    GTK_TOGGLE_BUTTON (toggle)->active);
		ve_config_save (config, FALSE /*force */);

		update_key (notify_key);
	}

	return FALSE;
}

static gboolean
entry_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;
	char *val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	val = ve_config_get_string (config, key);

	if (strcmp (ve_sure_string (val), ve_sure_string (text)) != 0) {
		ve_config_set_string (config, key, ve_sure_string (text));

		ve_config_save (config, FALSE /* force */);

		update_key (key);
	}

	g_free (val);

	return FALSE;
}

static gboolean
intspin_timeout (GtkWidget *spin)
{
	const char *key = g_object_get_data (G_OBJECT (spin), "key");
	const char *notify_key = g_object_get_data (G_OBJECT (spin),
						    "notify_key");
	int val, new_val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	new_val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));

	val = ve_config_get_int (config, key);

	if (val != new_val) {
		ve_config_set_int (config, key, new_val);

		ve_config_save (config, FALSE /* force */);

		update_key (notify_key);
	}

	return FALSE;
}

static gboolean
combobox_timeout (GtkWidget *combo_box)
{
	const char *key = g_object_get_data (G_OBJECT (combo_box), "key");
	char *new_val = NULL;
	gchar *val;
	int selected;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	selected = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));

	if (strcmp (key, GDM_KEY_REMOTEGREETER) == 0 ||
	    strcmp (key, GDM_KEY_GREETER) == 0) {

		if (strcmp (key, GDM_KEY_REMOTEGREETER) == 0 &&
		    selected == 2) {

			/* Disable remote login if selected */
			GtkWidget *toggle = glade_helper_get (xml, "enable_xdmcp",
			      GTK_TYPE_TOGGLE_BUTTON);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), FALSE);
			return FALSE;
		}

		else if (selected == 0) {
			new_val = g_strdup (EXPANDED_LIBEXECDIR "/gdmlogin");
			last_remote_login_setting = 0;
		} else if (selected == 1) {
			new_val = g_strdup (EXPANDED_LIBEXECDIR "/gdmgreeter");
			last_remote_login_setting = 1;
		}
	} else if (strcmp (key, GDM_KEY_AUTOMATICLOGIN) == 0 ||
		   strcmp (key, GDM_KEY_TIMED_LOGIN) == 0) {

		GtkTreeIter iter;
		
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), 
		    &iter)) {
			gtk_tree_model_get (gtk_combo_box_get_model (
				GTK_COMBO_BOX (combo_box)), &iter,
				0, &new_val, -1);
		}
	}

	val = ve_config_get_string (config, key);

	if (new_val && strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {
		ve_config_set_string (config, key, new_val);
		ve_config_save (config, FALSE /* force */);

		update_key (key);
	}

	g_free (new_val);
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
entry_changed (GtkWidget *entry)
{
	run_timeout (entry, 500, entry_timeout);
}

static void
intspin_changed (GtkWidget *spin)
{
	run_timeout (spin, 500, intspin_timeout);
}

static void
combobox_changed (GtkWidget *option_menu)
{
	run_timeout (option_menu, 500, combobox_timeout);
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

	val = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE), key);

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
					   _("Autologin or timed login to the superuser (root) account is not allowed."),
					   /* avoid warning */ "%s", "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		warned = TRUE;
	}
}

static gboolean
check_exclude (struct passwd *pwent, char **excludes)
{
	const char * const lockout_passes[] = { "!!", NULL };
	gint i;

	if (pwent->pw_uid == 0)
		return TRUE;

	if (pwent->pw_uid < GdmMinimalUID)
		return TRUE;

	for (i=0 ; lockout_passes[i] != NULL ; i++)  {
		if (strcmp (lockout_passes[i], pwent->pw_passwd) == 0) {
			return TRUE;
		}
	}

	if (excludes != NULL) {
		for (i=0 ; excludes[i] != NULL ; i++)  {
			if (g_ascii_strcasecmp (excludes[i],
						pwent->pw_name) == 0) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void
setup_user_combobox (const char *name, const char *key)
{
	GtkWidget *combobox_entry = glade_helper_get (xml, name, GTK_TYPE_COMBO_BOX_ENTRY);
	GtkListStore *combobox_store = gtk_list_store_new (USERLIST_NUM_COLUMNS,
		G_TYPE_STRING);
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	GList *users = NULL;
	GList *users_string = NULL;
	GList *li;
	static gboolean GDM_IS_LOCAL = FALSE;
	char *selected_user;
	gint size_of_users = 0;
	int selected = -1;
	int cnt;

	selected_user = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

	/* normally empty */
	users_string = g_list_append (users_string, g_strdup (""));

	if ( ! ve_string_empty (selected_user))
		users_string = g_list_append (users_string, g_strdup (selected_user));

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
		GDM_IS_LOCAL = FALSE;
	} else {
		GDM_IS_LOCAL = TRUE;
	}

        gdm_users_init (&users, &users_string, selected_user, NULL, &size_of_users,
		GDM_IS_LOCAL, FALSE);

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

        g_object_set_data_full (G_OBJECT (combobox_entry),
                                "key", g_strdup (key),
                                (GDestroyNotify) g_free);
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

	val = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE), key);

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
	char *val;
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
	const char *text, *model_text;
	GtkTreeIter iter;
	GtkWidget *message;
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
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreeModel *model;
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
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
	GString *userlist = g_string_new (NULL);
	const char *model_text;
	char *val;
	GtkTreeIter iter;
	gboolean valid;
	gboolean first = TRUE;
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
setup_face (void)
{
	static FaceCommon fc;
	static FaceData fd_include;
	static FaceData fd_exclude;
	static FaceApply face_apply;

	GtkTreeSelection *selection;

	fc.include_add = glade_helper_get (xml, "fb_includeadd",
		GTK_TYPE_WIDGET);
	fc.include_del = glade_helper_get (xml, "fb_includedelete",
		GTK_TYPE_WIDGET);
	fc.exclude_add = glade_helper_get (xml, "fb_excludeadd",
		GTK_TYPE_WIDGET);
	fc.exclude_del = glade_helper_get (xml, "fb_excludedelete",
		GTK_TYPE_WIDGET);
	fc.to_include_button = glade_helper_get (xml, "fb_toinclude",
		GTK_TYPE_WIDGET);
	fc.to_exclude_button = glade_helper_get (xml, "fb_toexclude",
		GTK_TYPE_WIDGET);
	fc.apply = glade_helper_get (xml, "fb_faceapply", GTK_TYPE_WIDGET);
	fc.label = glade_helper_get (xml, "fb_message", GTK_TYPE_WIDGET);
	fc.include_entry = glade_helper_get (xml, "fb_includeentry",
		GTK_TYPE_WIDGET);
	fc.exclude_entry = glade_helper_get (xml, "fb_excludeentry",
		GTK_TYPE_WIDGET);
        fc.include_treeview = glade_helper_get (xml, "fb_include_treeview",
		GTK_TYPE_TREE_VIEW);
	fc.exclude_treeview = glade_helper_get (xml, "fb_exclude_treeview",
		GTK_TYPE_TREE_VIEW);

	fc.include_store = setup_include_exclude (fc.include_treeview,
		GDM_KEY_INCLUDE);
	fc.exclude_store = setup_include_exclude (fc.exclude_treeview,
		GDM_KEY_EXCLUDE);

	fc.include_model = gtk_tree_view_get_model (GTK_TREE_VIEW (fc.include_treeview));
	fc.exclude_model = gtk_tree_view_get_model (GTK_TREE_VIEW (fc.exclude_treeview));

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
        g_signal_connect (selection, "changed", G_CALLBACK (face_selection_changed),
		&fd_include);
	selection = gtk_tree_view_get_selection (
		GTK_TREE_VIEW (fc.exclude_treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
        g_signal_connect (selection, "changed", G_CALLBACK (face_selection_changed),
		&fd_exclude);

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
	gboolean val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	val = ve_config_get_bool (config, key);

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
	gboolean val;

	val = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE), key);

	g_object_set_data_full (G_OBJECT (toggle),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	if (strcmp ("sg_defaultwelcome", name) == 0) {
		GtkWidget *welcome = glade_helper_get (xml,
			"welcome", GTK_TYPE_ENTRY);

		if (val == TRUE) {
			gtk_widget_set_sensitive (welcome, FALSE);
		}

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), welcome);
	} else if (strcmp ("sg_defaultremotewelcome", name) == 0) {
		GtkWidget *remotewelcome = glade_helper_get (xml,
			"remote_welcome", GTK_TYPE_ENTRY);

		if (val == TRUE) {
			gtk_widget_set_sensitive (remotewelcome, FALSE);
		}

		g_signal_connect (G_OBJECT (toggle), "toggled",	
			G_CALLBACK (sensitive_entry_toggled), remotewelcome);
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
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

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

	val = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

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

static gboolean
greeter_editable_timeout (GtkWidget *editable)
{
	const char *key = g_object_get_data (G_OBJECT (editable), "key");
	char *text, *val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	text = gtk_editable_get_chars (GTK_EDITABLE (editable), 0, -1);

	val = ve_config_get_string (config, key);

	if (strcmp (ve_sure_string (val), ve_sure_string (text)) != 0) {
		ve_config_set_string (config, key, ve_sure_string (text));

		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	g_free (text);
	g_free (val);

	return FALSE;
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
		VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

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

        if (image_data->filename != NULL && *(image_data->filename) != NULL)
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
	GtkWidget *preview;
	char *val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

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
	GtkWidget *logo_button      = glade_helper_get (xml, "sg_browselogo",
		GTK_TYPE_WIDGET);
	GtkWidget *backimage_button = glade_helper_get (xml, "sg_browsebackimage",
		GTK_TYPE_WIDGET);
	GtkWidget *nologo_button = glade_helper_get (xml, "sg_nologo",
		GTK_TYPE_WIDGET);
	GtkWidget *nobackimage_button = glade_helper_get (xml, "sg_nobackimage",
		GTK_TYPE_WIDGET);

	logo_data.image    = glade_helper_get (xml, "sg_logo", GTK_TYPE_WIDGET);
	logo_data.filename = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
		GDM_KEY_LOGO);
	logo_data.key = GDM_KEY_LOGO;

	backimage_data.image    = glade_helper_get (xml, "sg_backimage",
		GTK_TYPE_WIDGET);
	backimage_data.filename = ve_config_get_string (
		ve_config_get (GDM_CONFIG_FILE), GDM_KEY_BACKGROUNDIMAGE);
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
	g_signal_connect (G_OBJECT (nobackimage_button), "clicked",
		G_CALLBACK (noimage_button_cb), &backimage_data);
}

static gboolean
greeter_entry_untranslate_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

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

	val = ve_config_get_translated_string (ve_config_get (GDM_CONFIG_FILE),
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
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

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

	val = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE),
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


static void
setup_greeter_combobox (const char *name,
		      const char *key)
{
	GtkWidget *menu;
	GtkWidget *selected = NULL;
	GtkWidget *combobox = glade_helper_get (xml, name, GTK_TYPE_COMBO_BOX);
	GtkWidget *toggle   = glade_helper_get (xml, "enable_xdmcp",
	      GTK_TYPE_TOGGLE_BUTTON);
	char *val;

	val = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

	if (val != NULL &&
	    strcmp (val, EXPANDED_LIBEXECDIR "/gdmlogin --disable-sound --disable-crash-dialog") == 0) {
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

        g_object_set_data_full (G_OBJECT (combobox),
                                "key", g_strdup (key),
                                (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (combobox), "changed",
		  G_CALLBACK (combobox_changed), NULL);
}

static void
xdmcp_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *frame = data;

	gtk_widget_set_sensitive (frame,
				  GTK_TOGGLE_BUTTON (toggle)->active);
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
			  G_CALLBACK (xdmcp_toggled),
			  xdmcp_frame);
	g_signal_connect (G_OBJECT (xdmcp_toggle), "toggled",
			  G_CALLBACK (xdmcp_toggled),
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

static char *
modules_list_add (char *modules_list, const char *module)
{
	char *n;
	if (ve_string_empty (modules_list))
		n = g_strdup (module);
	else
		n = g_strconcat (modules_list, ":", module, NULL);
	g_free (modules_list);
	return n;
}

static void
acc_modules_toggled (GtkWidget *toggle, gpointer data)
{
	gboolean add_gtk_modules = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE),
						       GDM_KEY_ADD_GTK_MODULES);
	char *modules_list = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
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

		modules_list = modules_list_add (modules_list, "gail");
		modules_list = modules_list_add (modules_list, "atk-bridge");
		modules_list = modules_list_add (modules_list, EXPANDED_LIBDIR "/gtk-2.0/modules/libkeymouselistener");
		modules_list = modules_list_add (modules_list, EXPANDED_LIBDIR "/gtk-2.0/modules/libdwellmouselistener");

		add_gtk_modules = TRUE;
	}

	if (ve_string_empty (modules_list))
		add_gtk_modules = FALSE;

	ve_config_set_string (ve_config_get (GDM_CONFIG_FILE),
			      GDM_KEY_GTK_MODULES_LIST,
			      ve_sure_string (modules_list));
	ve_config_set_bool (ve_config_get (GDM_CONFIG_FILE),
			    GDM_KEY_ADD_GTK_MODULES,
			    add_gtk_modules);

	ve_config_save (ve_config_get (GDM_CONFIG_FILE), FALSE /*force */);

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
		       0 /* flags */,
		       NULL /* child setup */,
		       NULL /* user data */,
		       NULL /* child pid */,
		       NULL /* error */);
}

static void
no_sound_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget *acc_sound_file_label = data;
	const char *key = g_object_get_data (G_OBJECT (acc_sound_file_label),
	                                     "key");

	const char *nosound_button;
	const char *soundtest_button;
	nosound_button = g_strconcat("acc_nosound_",key,"_button",NULL);
	soundtest_button = g_strconcat("acc_soundtest_",key,"_button",NULL);

	GtkWidget *acc_no_sound_file = glade_helper_get (xml, nosound_button,
	                                                 GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_test = glade_helper_get (xml, soundtest_button,
	                                              GTK_TYPE_BUTTON);

	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
	char *val, *config_val;

	gtk_label_set_text (GTK_LABEL (acc_sound_file_label), _("None"));

	gtk_widget_set_sensitive (acc_no_sound_file, FALSE);
	gtk_widget_set_sensitive (acc_sound_test, FALSE);

	const gchar* sound_key;
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

	if (config_val != NULL && *config_val != NULL) {
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
	GtkWidget *acc_sound_file_label = data;
	const char *key = g_object_get_data (G_OBJECT (acc_sound_file_label), "key");

	const char *nosound_button;
	const char *soundtest_button;
	nosound_button = g_strconcat("acc_nosound_",key,"_button",NULL);
	soundtest_button = g_strconcat("acc_soundtest_",key,"_button",NULL);

	GtkWidget *acc_no_sound_file = glade_helper_get (xml, nosound_button,
		GTK_TYPE_BUTTON);
	GtkWidget *acc_sound_test = glade_helper_get (xml, soundtest_button,
		GTK_TYPE_BUTTON);
	if (response == GTK_RESPONSE_ACCEPT) {
		VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		char *val;

		gtk_label_set_text (GTK_LABEL (acc_sound_file_label), filename);

		gtk_widget_set_sensitive (acc_no_sound_file, TRUE);
		gtk_widget_set_sensitive (acc_sound_test, TRUE);


		char* sound_key;
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
	g_object_set_data (G_OBJECT (acc_sound_ready_file_label), "key",
	                   ready_key);
	g_object_set_data (G_OBJECT (acc_sound_success_file_label), "key",
	                   success_key);
	g_object_set_data (G_OBJECT (acc_sound_failure_file_label), "key",
	                   failure_key);

	gboolean add_gtk_modules = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE),
					       GDM_KEY_ADD_GTK_MODULES);
	char *modules_list = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
						   GDM_KEY_GTK_MODULES_LIST);
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
	char *val;

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

if (val != NULL && *val != NULL) {
		gtk_label_set_text (GTK_LABEL (acc_sound_ready_file_label), val);
	} else {
		gtk_label_set_text (GTK_LABEL (acc_sound_ready_file_label), _("None"));
		gtk_widget_set_sensitive (acc_no_sound_ready_file, FALSE);
		gtk_widget_set_sensitive (acc_sound_test_ready, FALSE);
	}

	val = ve_config_get_string (config, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);

	if (val != NULL && *val != NULL) {
		gtk_label_set_text (GTK_LABEL (acc_sound_success_file_label), val);
	} else {
		gtk_label_set_text (GTK_LABEL (acc_sound_success_file_label), _("None"));
		gtk_widget_set_sensitive (acc_no_sound_success_file, FALSE);
		gtk_widget_set_sensitive (acc_sound_test_success, FALSE);
	}

	val = ve_config_get_string (config, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

	if (val != NULL && *val != NULL) {
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
	GtkWidget *no_image_button = glade_helper_get (xml, "sg_nobackimage", GTK_TYPE_WIDGET);

	if (GTK_TOGGLE_BUTTON (no_bg)->active) {
		gtk_widget_set_sensitive (scale, FALSE);
		gtk_widget_set_sensitive (image, FALSE);
		gtk_widget_set_sensitive (onlycolor, FALSE);
		gtk_widget_set_sensitive (color_label, FALSE);
		gtk_widget_set_sensitive (color, FALSE);
		gtk_widget_set_sensitive (browse_button, FALSE);
		gtk_widget_set_sensitive (no_image_button, FALSE);
	} else if (GTK_TOGGLE_BUTTON (image_bg)->active) {
		gtk_widget_set_sensitive (scale, TRUE);
		gtk_widget_set_sensitive (image, TRUE);
		gtk_widget_set_sensitive (onlycolor, TRUE);
		gtk_widget_set_sensitive (browse_button, TRUE);
		gtk_widget_set_sensitive (no_image_button, TRUE);
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
		gtk_widget_set_sensitive (no_image_button, FALSE);
	}
}

static void
setup_background_support (void)
{
	GtkWidget *no_bg = glade_helper_get (xml, "sg_no_bg_rb", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *image_bg = glade_helper_get (xml, "sg_image_bg_rb", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *color_bg = glade_helper_get (xml, "sg_color_bg_rb", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *onlycolor = glade_helper_get (xml, "sg_remote_color_only", GTK_TYPE_TOGGLE_BUTTON);

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

	theme_dir = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
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

static void
selection_changed (GtkTreeSelection *selection, gpointer data)
{
        GtkWidget *label;
        GtkWidget *preview = glade_helper_get (xml, "gg_theme_preview",
					       GTK_TYPE_IMAGE);
        GtkWidget *no_preview = glade_helper_get (xml, "gg_theme_no_preview",
						  GTK_TYPE_WIDGET);
	GtkWidget *del_button = glade_helper_get (xml, "gg_delete_theme",
						  GTK_TYPE_BUTTON);
        char *screenshot;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = {0, };

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_widget_set_sensitive (del_button, FALSE);
		return;
	}

	gtk_widget_set_sensitive (del_button, TRUE);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_SELECTED,
				  &value);
	/* Do not allow deleting of selected theme */
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
		gboolean sel;
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
			sel = TRUE;
		else
			sel = FALSE;

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
				    THEME_COLUMN_SELECTED, sel,
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
	char *val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	val = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME);

	if (strcmp (ve_sure_string (val),
		    ve_sure_string (selected_theme)) != 0) {
		ve_config_set_string (config, GDM_KEY_GRAPHICAL_THEME,
				      selected_theme);
		ve_config_save (config, FALSE /* force */);

		update_greeters ();
	}

	g_free (val);

	return FALSE;
}

static void
selected_toggled (GtkCellRendererToggle *cell,
		  char                  *path_str,
		  gpointer               data)
{
	GtkTreeModel *model = GTK_TREE_MODEL (data);
	GtkTreeIter selected_iter;
	GtkTreeIter iter;
	GtkTreePath *sel_path = gtk_tree_path_new_from_string (path_str);
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);
	GtkTreePath *path;

	gtk_tree_model_get_iter (model, &selected_iter, sel_path);

	g_free (selected_theme);
	gtk_tree_model_get (model, &selected_iter,
			    THEME_COLUMN_DIR, &selected_theme,
			    -1);

	path = gtk_tree_path_new_first ();
	while (gtk_tree_model_get_iter (model, &iter, path)) {

		if (gtk_tree_path_compare (path, sel_path) == 0) {
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    THEME_COLUMN_SELECTED, TRUE,
					    -1);
		} else {
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    THEME_COLUMN_SELECTED, FALSE,
					    -1);
		}

		gtk_tree_path_next (path);
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

static void
setup_graphical_themes (void)
{
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

	char *theme_dir = get_theme_dir ();

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (theme_list), TRUE);

	selected_theme = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					       GDM_KEY_GRAPHICAL_THEME);

	/* create list store */
	store = gtk_list_store_new (THEME_NUM_COLUMNS,
				    G_TYPE_BOOLEAN /* selected */,
				    G_TYPE_STRING /* dir */,
				    G_TYPE_STRING /* file */,
				    G_TYPE_STRING /* name */,
				    G_TYPE_STRING /* desc */,
				    G_TYPE_STRING /* author */,
				    G_TYPE_STRING /* copyright */,
				    G_TYPE_STRING /* screenshot */);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (install_new_theme), store);
	g_signal_connect (del_button, "clicked",
			  G_CALLBACK (delete_theme), store);
	gtk_widget_set_sensitive (del_button, FALSE);

	dir = opendir (theme_dir);

	if (dir != NULL) {
		select_iter = read_themes (store, theme_dir, dir,
					   selected_theme);
		closedir (dir);
	}

	g_free (theme_dir);

	gtk_tree_view_set_model (GTK_TREE_VIEW (theme_list), 
				 GTK_TREE_MODEL (store));

	/* The toggle column */

	column = gtk_tree_view_column_new ();

	renderer = gtk_cell_renderer_toggle_new ();
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer),
					    TRUE);
        gtk_tree_view_column_pack_start (column, renderer, FALSE);

        gtk_tree_view_column_set_attributes (column, renderer,
                                             "active", THEME_COLUMN_SELECTED,
                                             NULL);

	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (selected_toggled), store);

	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	/* The text column */

	column = gtk_tree_view_column_new ();


	renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);

        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", THEME_COLUMN_NAME,
                                             NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	/* Selection setup */

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));

	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

        g_signal_connect (selection, "changed",
			  G_CALLBACK (selection_changed),
			  NULL);

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
			   "Under the category GNOME/System."),
			 GDM_CONFIG_FILE);
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

	setup_accessibility_support ();
	setup_xdmcp_support ();
	setup_background_support ();
	setup_greeter_backselect ();
	setup_graphical_themes ();

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
	add_to_size_group (sg, "sg_use_24_clock");
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

	setup_greeter_combobox ("local_greeter", GDM_KEY_GREETER);
	setup_greeter_combobox ("remote_greeter", GDM_KEY_REMOTEGREETER);

	/* Greeter */
	setup_greeter_toggle ("sg_use_24_clock",
			      GDM_KEY_USE_24_CLOCK);
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
	setup_face ();
	setup_greeter_toggle ("fb_browser",
			      GDM_KEY_BROWSER);
	setup_greeter_toggle ("fb_allusers",
			      GDM_KEY_INCLUDEALL);
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

int 
main (int argc, char *argv[])
{
	guint sid;

	if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
		DOING_GDM_DEVELOPMENT = TRUE;
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL)
		RUNNING_UNDER_GDM = TRUE;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gdmsetup", VERSION, 
			    LIBGNOMEUI_MODULE /* module_info */,
			    argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    /* *GNOME_PARAM_POPT_TABLE, options, */
			    GNOME_PARAM_CREATE_DIRECTORIES, ! RUNNING_UNDER_GDM,
			    NULL);

	glade_gnome_init();

	gdm_running = gdmcomm_check (FALSE /* gui_bitching */);

	if (RUNNING_UNDER_GDM) {
		char *gtkrc;
		char *theme_name;

		/* Set busy cursor */
		setup_cursor (GDK_WATCH);

		/* parse the given gtk rc first */
		gtkrc = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					      GDM_KEY_GTKRC);
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);
		g_free (gtkrc);

		theme_name = g_strdup (g_getenv ("GDM_GTK_THEME"));
		if (ve_string_empty (theme_name)) {
			g_free (theme_name);
			theme_name = ve_config_get_string
				(ve_config_get (GDM_CONFIG_FILE), GDM_KEY_GTK_THEME);
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
					   _("You must be the superuser (root) to configure GDM."),
					   /* avoid warning */ "%s", "");
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	/* XXX: the setup proggie using a greeter config var for it's
	 * ui?  Say it ain't so.  Our config sections are SUCH A MESS */
	GdmIconMaxHeight = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_ICONHEIGHT);
	GdmIconMaxWidth = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_ICONWIDTH);
	GdmMinimalUID = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_MINIMALUID);
	GdmIncludeAll = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_INCLUDEALL);
	GdmInclude = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_INCLUDE);
	GdmExclude = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_EXCLUDE);
	GdmSoundProgram = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
						GDM_KEY_SOUND_PROGRAM);
	GdmAllowRoot = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE),
						GDM_KEY_ALLOWROOT);
	GdmAllowRemoteRoot = ve_config_get_bool (ve_config_get (GDM_CONFIG_FILE),
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
