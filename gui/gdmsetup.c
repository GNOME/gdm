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
#include "misc.h"
#include "gdmcomm.h"

/* set the DOING_GDM_DEVELOPMENT env variable if you want to
 * search for the glade file in the current dir and not the system
 * install dir, better then something you have to change
 * in the source and recompile */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;

static gboolean RUNNING_UNDER_GDM = FALSE;

static gboolean gdm_running = FALSE;
static int GdmMinimalUID = 100;

static GladeXML *xml;

static GList *timeout_widgets = NULL;

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
			gtk_message_dialog_new (GTK_WINDOW (setup_dialog),
						GTK_DIALOG_MODAL | 
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						_("An error occured while "
						  "trying to contact the "
						  "login screens.  Not all "
						  "updates may have taken "
						  "effect."));
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

#define ITEM_STRING "GdmSetup:ItemString"

static const char *
get_str_from_option (GtkOptionMenu *option)
{
	GtkWidget *menu, *active;

	menu = gtk_option_menu_get_menu (option);
	if (menu == NULL)
		return NULL;

	active = gtk_menu_get_active (GTK_MENU (menu));
	if (active == NULL)
		return NULL;

	return g_object_get_data (G_OBJECT (active), ITEM_STRING);
}

static gboolean
option_timeout (GtkWidget *option_menu)
{
	const char *key = g_object_get_data (G_OBJECT (option_menu), "key");
	const char *new_val;
	char *val;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	new_val = get_str_from_option (GTK_OPTION_MENU (option_menu));

	val = ve_config_get_string (config, key);

	if (strcmp (ve_sure_string (val), ve_sure_string (new_val)) != 0) {
		ve_config_set_string (config, key, new_val);

		ve_config_save (config, FALSE /* force */);

		update_key (key);
	}

	g_free (val);

	return FALSE;
}

static void
toggle_toggled (GtkWidget *toggle)
{
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
option_changed (GtkWidget *option_menu)
{
	run_timeout (option_menu, 500, option_timeout);
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
		g_print ("Removing %p!\n", li->data);
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

	g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (toggle_toggled), NULL);
}

static void
setup_user_combo (const char *name, const char *key)
{
	GtkWidget *combo = glade_helper_get (xml, name, GTK_TYPE_COMBO);
	GtkWidget *entry= GTK_COMBO (combo)->entry;
	GList *users = NULL;
	struct passwd *pwent;
	char *str;

	str = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

	/* normally empty */
	users = g_list_append (users, g_strdup (""));

	if ( ! ve_string_empty (str))
		users = g_list_append (users, g_strdup (str));

	setpwent ();

	pwent = getpwent();
	
	while (pwent != NULL) {
		if (pwent->pw_uid >= GdmMinimalUID &&
		    strcmp (ve_sure_string (str), pwent->pw_name) != 0) {
			users = g_list_append (users,
					       g_strdup (pwent->pw_name));
		}
	
		pwent = getpwent();
	}

	endpwent ();

	gtk_combo_set_popdown_strings (GTK_COMBO (combo), users);

	gtk_entry_set_text (GTK_ENTRY (entry),
			    ve_sure_string (str));

	g_object_set_data_full (G_OBJECT (entry),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (entry_changed), NULL);

	g_list_foreach (users, (GFunc)g_free, NULL);
	g_list_free (users);
	g_free (str);
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

static void
greeter_editable_changed (GtkWidget *editable)
{
	run_timeout (editable, 500, greeter_editable_timeout);
}

static void
setup_greeter_editable (const char *name,
			const char *key)
{
	GtkWidget *editable = glade_helper_get (xml, name, GTK_TYPE_EDITABLE);
	char *val;
	int pos;

	val = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

	g_object_set_data_full (G_OBJECT (editable),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_editable_delete_text (GTK_EDITABLE (editable), 0, -1);
	pos = 0;
	gtk_editable_insert_text (GTK_EDITABLE (editable),
				  ve_sure_string (val), -1,
				  &pos);

	g_signal_connect (G_OBJECT (editable), "changed",
			  G_CALLBACK (greeter_editable_changed), NULL);

	g_free (val);
}

static gboolean
greeter_entry_untranslate_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);

	g_print ("Timeout %p!\n", entry);

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
	g_print ("Changed %p!\n", entry);
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
add_menuitem (GtkWidget *menu, const char *str, const char *label,
	      const char *select, GtkWidget **selected)
{
	GtkWidget *item = gtk_menu_item_new_with_label (label);
	gtk_widget_show (item);
	g_object_set_data_full (G_OBJECT (item), ITEM_STRING,
				g_strdup (str),
				(GDestroyNotify)g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	if (select != NULL &&
	    strcmp (str, select) == 0)
		*selected = item;
}

static void
setup_greeter_option (const char *name,
		      const char *key)
{
	GtkWidget *menu;
	GtkWidget *selected = NULL;
	char *val;
	GtkWidget *option_menu = glade_helper_get (xml, name,
						   GTK_TYPE_OPTION_MENU);

	val = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE), key);

	if (val != NULL &&
	    strcmp (val, EXPANDED_BINDIR "/gdmlogin --disable-sound --disable-crash-dialog") == 0) {
		g_free (val);
		val = g_strdup (EXPANDED_BINDIR "/gdmlogin");
	}

	menu = gtk_menu_new ();

	add_menuitem (menu, EXPANDED_BINDIR "/gdmlogin",
		      _("Standard greeter"), val, &selected);
	add_menuitem (menu, EXPANDED_BINDIR "/gdmgreeter",
		      _("Graphical greeter"), val, &selected);

	if (val != NULL &&
	    selected == NULL)
		add_menuitem (menu, val, val, val, &selected);

	/* FIXME: Evil, but GtkOptionMenu is SOOOO FUCKING STUPID! */
	if (selected != NULL) {
		if (GTK_MENU (menu)->old_active_menu_item)
			gtk_widget_unref (GTK_MENU (menu)->old_active_menu_item);
		GTK_MENU (menu)->old_active_menu_item = selected;
		gtk_widget_ref (GTK_MENU (menu)->old_active_menu_item);
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);

	g_free (val);

	g_object_set_data_full (G_OBJECT (option_menu),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (option_menu), "changed",
			  G_CALLBACK (option_changed), NULL);
}

static void
xdmcp_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *frame = data;

	gtk_widget_set_sensitive (GTK_BIN (frame)->child,
				  GTK_TOGGLE_BUTTON (toggle)->active);
}

static void
setup_xdmcp_support (void)
{
	GtkWidget *xdmcp_toggle = glade_helper_get (xml, "enable_xdmcp", GTK_TYPE_TOGGLE_BUTTON);
	GtkWidget *xdmcp_frame = glade_helper_get (xml, "xdmcp_frame", GTK_TYPE_FRAME);

#ifndef HAVE_LIBXDMCP
	gtk_widget_show (glade_helper_get (xml, "no_xdmcp_label", GTK_TYPE_LABEL));
	gtk_widget_set_sensitive (xdmcp_toggle, FALSE);
	gtk_widget_set_sensitive (xdmcp_frame, FALSE);
#else /* ! HAVE_LIBXDMCP */
	gtk_widget_hide (glade_helper_get (xml, "no_xdmcp_label", GTK_TYPE_LABEL));
#endif /* HAVE_LIBXDMCP */

	gtk_widget_set_sensitive (GTK_BIN (xdmcp_frame)->child, 
				  GTK_TOGGLE_BUTTON (xdmcp_toggle)->active);

	g_signal_connect (G_OBJECT (xdmcp_toggle), "toggled",
			  G_CALLBACK (xdmcp_toggled),
			  xdmcp_frame);
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

	if (GTK_TOGGLE_BUTTON (no_bg)->active) {
		gtk_widget_set_sensitive (scale, FALSE);
		gtk_widget_set_sensitive (image, FALSE);
		gtk_widget_set_sensitive (onlycolor, FALSE);
		gtk_widget_set_sensitive (color_label, FALSE);
		gtk_widget_set_sensitive (color, FALSE);
	} else if (GTK_TOGGLE_BUTTON (image_bg)->active) {
		gtk_widget_set_sensitive (scale, TRUE);
		gtk_widget_set_sensitive (image, TRUE);
		gtk_widget_set_sensitive (onlycolor, TRUE);
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

static char *selected_theme = NULL;

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
        char *screenshot;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = {0, };

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

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
			file = g_strconcat (dent->d_name, ".xml");
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
		}
		g_free (dir);
		if (ret != 0) {
			*error = NULL;
		}
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
			}
			g_free (dir);
			if (ret != 0) {
				*error = NULL;
			}
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
install_ok (GtkWidget *button, gpointer data)
{
	GtkFileSelection *fs = GTK_FILE_SELECTION (data);
	GtkListStore *store = g_object_get_data (G_OBJECT (fs), "ListStore");
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);
	char *filename, *dir, *untar_cmd, *theme_dir, *cwd;
	GtkTreeIter *select_iter = NULL;
	GtkTreeSelection *selection;
	char *error;
	DIR *dp;
	gboolean success = FALSE;

	cwd = g_get_current_dir ();
	theme_dir = get_theme_dir ();

	filename = g_strdup (gtk_file_selection_get_filename (fs));
	if (filename == NULL) {
		GtkWidget *dlg =
			gtk_message_dialog_new (GTK_WINDOW (fs),
						GTK_DIALOG_MODAL | 
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						_("No file selected"));
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
			gtk_message_dialog_new (GTK_WINDOW (fs),
						GTK_DIALOG_MODAL | 
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						_("Not a theme archive\n"
						  "Details: %s"),
						error);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		g_free (filename);
		g_free (cwd);
		g_free (theme_dir);
		return;
	}

	if (dir_exists (theme_dir, dir)) {
		char *fname = g_filename_to_utf8 (dir, -1, NULL, NULL, NULL);
		/* FIXME: if exists already perhaps we could also have an
		 * option to change the dir name */
		GtkWidget *dlg =
			gtk_message_dialog_new
			(GTK_WINDOW (fs),
			 GTK_DIALOG_MODAL | 
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_QUESTION,
			 GTK_BUTTONS_YES_NO,
			 _("Theme directory '%s' seems to be already "
			   "installed, install again anyway?"),
			 fname);
		g_free (fname);
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

	if (chdir (theme_dir) == 0) {
		if (system (untar_cmd) == 0) {
			char *cmd;
			success = TRUE;

			/* HACK! */
			cmd = g_strdup_printf ("/bin/chown -R root.root %s", dir);
			system (cmd);
			g_free (cmd);

			cmd = g_strdup_printf ("/bin/chmod -R a+r %s", dir);
			system (cmd);
			g_free (cmd);

			cmd = g_strdup_printf ("/bin/chmod a+x %s", dir);
			system (cmd);
			g_free (cmd);
		}
		chdir (cwd);
	}

	if ( ! success) {
		GtkWidget *dlg =
			gtk_message_dialog_new (GTK_WINDOW (fs),
						GTK_DIALOG_MODAL | 
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						_("Some error occured when "
						  "installing the theme"));
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

	gtk_widget_destroy (GTK_WIDGET (fs));
}

static void
install_new_theme (GtkWidget *button, gpointer data)
{
	GtkListStore *store = data;
	static GtkWidget *fs = NULL;
	GtkWidget *setup_dialog;

	if (fs != NULL) {
		gtk_window_present (GTK_WINDOW (fs));
		return;
	}
       
	setup_dialog = glade_helper_get (xml, "setup_dialog", GTK_TYPE_WINDOW);
	
	fs = gtk_file_selection_new (_("Select new theme archive to install"));
	gtk_window_set_transient_for (GTK_WINDOW (fs),
				      GTK_WINDOW (setup_dialog));
	g_object_set_data (G_OBJECT (fs), "ListStore", store);
	g_signal_connect (G_OBJECT (fs), "destroy",
			  G_CALLBACK (gtk_widget_destroyed), &fs);
	g_signal_connect (GTK_FILE_SELECTION (fs)->ok_button, "clicked",
			  G_CALLBACK (install_ok), fs);
	g_signal_connect_swapped (GTK_FILE_SELECTION (fs)->cancel_button,
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy), fs);

	gtk_widget_show (fs);
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

	char *theme_dir = get_theme_dir ();

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
		/* FIXME: when help gets written connect it here */
		GtkWidget *setup_dialog = glade_helper_get
			(xml, "setup_dialog", GTK_TYPE_WINDOW);
		static GtkWidget *dlg;

		if (dlg != NULL) {
			gtk_window_present (GTK_WINDOW (dlg));
			return;
		}
	
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
			   "if you cannot find what you are looking for."),
			 GDM_CONFIG_FILE);
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
setup_gui (void)
{
	GtkWidget *dialog;

	xml = glade_helper_load ("gdmsetup.glade",
				 "setup_dialog",
				 GTK_TYPE_DIALOG,
				 TRUE /* dump_on_destroy */);
	dialog = glade_helper_get (xml, "setup_dialog", GTK_TYPE_DIALOG);
	g_signal_connect (G_OBJECT (dialog), "delete_event",
			  G_CALLBACK (delete_event), NULL);
	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response), NULL);

	setup_xdmcp_support ();
	setup_background_support ();
	setup_greeter_backselect ();
	setup_graphical_themes ();

	setup_user_combo ("autologin_combo",
			  GDM_KEY_AUTOMATICLOGIN);
	setup_user_combo ("timedlogin_combo",
			  GDM_KEY_TIMED_LOGIN);

	setup_notify_toggle ("autologin",
			     GDM_KEY_AUTOMATICLOGIN_ENABLE,
			     NULL /* notify_key */);
	setup_notify_toggle ("timedlogin",
			     GDM_KEY_TIMED_LOGIN_ENABLE,
			     NULL /* notify_key */);

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

	setup_greeter_option ("local_greeter", GDM_KEY_GREETER);
	setup_greeter_option ("remote_greeter", GDM_KEY_REMOTEGREETER);

	/* Greeter configurations */

	setup_greeter_toggle ("sg_use_24_clock",
			      GDM_KEY_USE_24_CLOCK);
	setup_greeter_toggle ("sg_browser",
			      GDM_KEY_BROWSER);
	setup_greeter_toggle ("sg_scale_background",
			      GDM_KEY_BACKGROUNDSCALETOFIT);
	setup_greeter_toggle ("sg_remote_color_only",
			      GDM_KEY_BACKGROUNDREMOTEONLYCOLOR);

	setup_greeter_editable ("sg_logo",
				GDM_KEY_LOGO);
	setup_greeter_editable ("sg_backimage",
				GDM_KEY_BACKGROUNDIMAGE);
	setup_greeter_color ("sg_backcolor",
			     GDM_KEY_BACKGROUNDCOLOR);

	setup_greeter_untranslate_entry ("sg_welcome",
					 GDM_KEY_WELCOME);
}

static gboolean
get_sensitivity (void)
{
	static Atom atom = 0;
	Display *disp = gdk_x11_get_default_xdisplay ();
	Window root = gdk_x11_get_default_root_xwindow ();
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
				(unsigned char **)&data) != Success)
		return TRUE;

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
			    /* *GNOME_PARAM_POPT_TABLE, options, */
			    GNOME_PARAM_CREATE_DIRECTORIES, ! RUNNING_UNDER_GDM,
			    NULL);

	glade_gnome_init();

	gdm_running = gdmcomm_check (FALSE /* gui_bitching */);

	if (RUNNING_UNDER_GDM) {
		char *gtkrc;
		guint sid;

		/* Set busy cursor */
		setup_cursor (GDK_WATCH);

		/* If we are running under gdm parse the GDM gtkRC */
		gtkrc = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					      GDM_KEY_GTKRC);
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);
		g_free (gtkrc);

		/* evil, but oh well */
		g_type_class_ref (GTK_TYPE_WIDGET);	

		/* also setup third button to work as first to work in reverse
		 * situations transparently */
		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);
	}

	glade_helper_add_glade_directory (GDM_GLADE_DIR);

	/* Make sure the user is root. If not, they shouldn't be messing with 
	 * GDM's configuration.
	 */

	if ( ! DOING_GDM_DEVELOPMENT &&
	     geteuid() != 0) {
		GtkWidget *fatal_error = 
			gtk_message_dialog_new (NULL /* parent */,
						GTK_DIALOG_MODAL /* flags */,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						_("You must be the superuser (root) to configure GDM.\n"));
		if (RUNNING_UNDER_GDM)
			setup_cursor (GDK_LEFT_PTR);
		gtk_dialog_run (GTK_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	/* XXX: the setup proggie using a greeter config var for it's
	 * ui?  Say it ain't so.  Our config sections are SUCH A MESS */
	GdmMinimalUID = ve_config_get_int (ve_config_get (GDM_CONFIG_FILE),
					   GDM_KEY_MINIMALUID);

	setup_gui ();

	if (RUNNING_UNDER_GDM) {
		setup_disable_handler ();

		setup_cursor (GDK_LEFT_PTR);
	}

	gtk_main ();

	return 0;
}

/* EOF */
