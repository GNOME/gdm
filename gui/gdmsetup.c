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

static GladeXML *xml;

static void
update_greeters (void)
{
	char *p, *ret;
	long pid;

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
			return;
		}
		kill (pid, SIGHUP);
		p = strchr (p, ';');
		if (p == NULL) {
			g_free (ret);
			return;
		}
		p++;
	}
}

static void
run_timeout (GtkWidget *widget, guint tm, gboolean (*func) (GtkWidget *))
{
	guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget),
							"change_timeout"));
	if (id != 0) {
		g_source_remove (id);
	}

	id = g_timeout_add (tm, (GSourceFunc)func, widget);
	g_object_set_data (G_OBJECT (widget), "timeout_func", func);

	g_object_set_data (G_OBJECT (widget), "change_timeout",
			   GUINT_TO_POINTER (id));
}

static void
update_key (const char *notify_key)
{
	if (notify_key != NULL && gdm_running) {
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

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_bool (key, GTK_TOGGLE_BUTTON (toggle)->active);
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_key (notify_key);

	return FALSE;
}

static gboolean
entry_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_string (key, ve_sure_string (text));
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_key (key);

	return FALSE;
}

static gboolean
intspin_timeout (GtkWidget *spin)
{
	const char *key = g_object_get_data (G_OBJECT (spin), "key");
	const char *notify_key = g_object_get_data (G_OBJECT (spin),
						    "notify_key");
	int val;

	val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_int (key, val);
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_key (notify_key);

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
	const char *val;

	val = get_str_from_option (GTK_OPTION_MENU (option_menu));

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_string (key, val);
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_key (key);

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
	}
	g_object_set_data (G_OBJECT (widget), "change_timeout", NULL);

	func = g_object_get_data (G_OBJECT (widget), "timeout_func");

	(*func) (widget);
}

static void
setup_notify_toggle (const char *name,
		     const char *key,
		     const char *notify_key)
{
	GtkWidget *toggle = glade_helper_get (xml, name,
					      GTK_TYPE_TOGGLE_BUTTON);
	gboolean val;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_bool (key);
	gnome_config_pop_prefix ();

	g_object_set_data_full (G_OBJECT (toggle),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (toggle),
				"notify_key", g_strdup (notify_key),
				(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (toggle), "destroy",
			  G_CALLBACK (timeout_remove), NULL);
}

static void
setup_user_combo (const char *name, const char *key)
{
	GtkWidget *combo = glade_helper_get (xml, name, GTK_TYPE_COMBO);
	GtkWidget *entry= GTK_COMBO (combo)->entry;
	GList *users = NULL;
	struct passwd *pwent;
	char *str;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	str = gnome_config_get_string (key);
	gnome_config_pop_prefix ();

	/* normally empty */
	users = g_list_append (users, g_strdup (""));

	if ( ! ve_string_empty (str))
		users = g_list_append (users, g_strdup (str));

	setpwent ();

	pwent = getpwent();
	
	while (pwent != NULL) {
		/* FIXME: 100 is a pretty arbitrary constant */
		if (pwent->pw_uid >= 100 &&
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
	g_signal_connect (G_OBJECT (entry), "destroy",
			  G_CALLBACK (timeout_remove), NULL);

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

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_int (key);
	gnome_config_pop_prefix ();

	g_object_set_data_full (G_OBJECT (spin),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (spin),
				"notify_key", g_strdup (notify_key),
				(GDestroyNotify) g_free);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), val);

	g_signal_connect (G_OBJECT (spin), "value_changed",
			  G_CALLBACK (intspin_changed), NULL);
	g_signal_connect (G_OBJECT (spin), "destroy",
			  G_CALLBACK (timeout_remove), NULL);
}

static gboolean
greeter_toggle_timeout (GtkWidget *toggle)
{
	const char *key = g_object_get_data (G_OBJECT (toggle), "key");

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_bool (key, GTK_TOGGLE_BUTTON (toggle)->active);
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_greeters ();

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

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_bool (key);
	gnome_config_pop_prefix ();

	g_object_set_data_full (G_OBJECT (toggle),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), val);

	g_signal_connect (G_OBJECT (toggle), "toggled",
			  G_CALLBACK (greeter_toggle_toggled), NULL);
	g_signal_connect (G_OBJECT (toggle), "destroy",
			  G_CALLBACK (timeout_remove), NULL);
}

static gboolean
greeter_editable_timeout (GtkWidget *editable)
{
	const char *key = g_object_get_data (G_OBJECT (editable), "key");
	char *text;

	text = gtk_editable_get_chars (GTK_EDITABLE (editable), 0, -1);

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_string (key, ve_sure_string (text));
	gnome_config_pop_prefix ();

	g_free (text);

	gnome_config_sync ();

	update_greeters ();

	return FALSE;
}

static void
greeter_editable_changed (GtkWidget *toggle)
{
	run_timeout (toggle, 500, greeter_editable_timeout);
}

static void
setup_greeter_editable (const char *name,
			const char *key)
{
	GtkWidget *editable = glade_helper_get (xml, name, GTK_TYPE_EDITABLE);
	char *val;
	int pos;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_string (key);
	gnome_config_pop_prefix ();

	g_object_set_data_full (G_OBJECT (editable),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	/* FIXME: hmm? */
	/* gtk_editable_delete_text (GTK_EDITABLE (editable), 0, -1);
	pos = 0;
	gtk_editable_insert_text (GTK_EDITABLE (editable),
				  ve_sure_string (val), -1,
				  &pos); */

	g_signal_connect (G_OBJECT (editable), "changed",
			  G_CALLBACK (greeter_editable_changed), NULL);
	g_signal_connect (G_OBJECT (editable), "destroy",
			  G_CALLBACK (timeout_remove), NULL);

	g_free (val);
}

static void
whack_translations (const char *fullkey)
{
	char *section, *key, *p, *k, *v;
	void *iterator;
	GSList *to_clean, *li;

	section = g_strdup (fullkey);
	p = strchr (section, '/');
	if (p == NULL) {
		g_free (section);
		return;
	}
	*p = '\0';
	key = p+1;
	p = strchr (key, '=');
	if (p != NULL)
		*p = '\0';


	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

	to_clean = NULL;
	iterator = gnome_config_init_iterator (section);
	while ((iterator = gnome_config_iterator_next (iterator, &k, &v))
	       != NULL) {
		p = strchr (k, '[');
		if (p != NULL) {
			*p = '\0';
			if (strcmp (key, k) == 0) {
				*p = '[';
				to_clean = g_slist_prepend
					(to_clean,
					 g_strconcat (section, "/", k, NULL));
			}
		}
		g_free (k);
		g_free (v);
	}

	for (li = to_clean; li != NULL; li = li->next) {
		char *key = li->data;
		li->data = NULL;

		gnome_config_clean_key (key);

		g_free (key);
	}

	g_slist_free (to_clean);

	gnome_config_pop_prefix ();

	g_free (section);
}

static gboolean
greeter_entry_untranslate_timeout (GtkWidget *entry)
{
	const char *key = g_object_get_data (G_OBJECT (entry), "key");
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	whack_translations (key);

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	gnome_config_set_string (key, ve_sure_string (text));
	gnome_config_pop_prefix ();

	gnome_config_sync ();

	update_greeters ();

	return FALSE;
}

static void
greeter_entry_untranslate_changed (GtkWidget *toggle)
{
	run_timeout (toggle, 500, greeter_entry_untranslate_timeout);
}

static void
setup_greeter_untranslate_entry (const char *name,
				 const char *key)
{
	GtkWidget *entry = glade_helper_get (xml, name, GTK_TYPE_ENTRY);
	char *val;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_translated_string (key);
	gnome_config_pop_prefix ();

	g_object_set_data_full (G_OBJECT (entry),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	gtk_entry_set_text (GTK_ENTRY (entry), ve_sure_string (val));

	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (greeter_entry_untranslate_changed),
			  NULL);
	g_signal_connect (G_OBJECT (entry), "destroy",
			  G_CALLBACK (timeout_remove), NULL);

	g_free (val);
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

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	val = gnome_config_get_string (key);
	gnome_config_pop_prefix ();

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

	if (selected != NULL)
		gtk_menu_item_activate (GTK_MENU_ITEM (selected));

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);

	g_free (val);

	g_object_set_data_full (G_OBJECT (option_menu),
				"key", g_strdup (key),
				(GDestroyNotify) g_free);

	g_signal_connect (G_OBJECT (option_menu), "changed",
			  G_CALLBACK (option_changed), NULL);
	g_signal_connect (G_OBJECT (option_menu), "destroy",
			  G_CALLBACK (timeout_remove), NULL);
}

static void
xdmcp_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *frame = data;

	gtk_widget_set_sensitive (frame, GTK_TOGGLE_BUTTON (toggle)->active);
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

	gtk_widget_set_sensitive (xdmcp_frame, 
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
	THEME_COLUMN_FILE,
	THEME_COLUMN_NAME,
	THEME_COLUMN_DESCRIPTION,
	THEME_COLUMN_AUTHOR,
	THEME_COLUMN_COPYRIGHT,
	THEME_COLUMN_SCREENSHOT,
	THEME_NUM_COLUMNS
};

static char *
get_theme_dir (void)
{
	char *theme_dir;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	theme_dir = gnome_config_get_string (GDM_KEY_GRAPHICAL_THEME);
	gnome_config_pop_prefix ();

	if (theme_dir == NULL ||
	    theme_dir[0] == '\0' ||
	    access (theme_dir, R_OK) != 0) {
		g_free (theme_dir);
		theme_dir = g_strdup (EXPANDED_DATADIR "/gdm/themes/");
	}

	return theme_dir;
}

static void
selection_changed (GtkTreeSelection *selection,
		   GtkWidget        *dialog)
{
        GtkWidget *label = glade_helper_get (xml, "gg_desc_label",
					     GTK_TYPE_LABEL);
        GtkWidget *preview = glade_helper_get (xml, "gg_theme_preview",
					       GTK_TYPE_IMAGE);
        GtkWidget *no_preview = glade_helper_get (xml, "gg_theme_no_preview",
						  GTK_TYPE_WIDGET);
        char *author, *copyright, *desc, *screenshot;
        char *str;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = {0, };

	if ( ! gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_AUTHOR,
				  &value);
	author = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_COPYRIGHT,
				  &value);
	copyright = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	gtk_tree_model_get_value (model, &iter,
				  THEME_COLUMN_DESCRIPTION,
				  &value);
	desc = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	str = g_strdup_printf (_("<b>Author:</b> %s\n"
				 "<b>Copyright:</b> %s\n"
				 "<b>Description:</b> %s"),
			       ve_sure_string (author),
			       ve_sure_string (copyright),
			       ve_sure_string (desc));
	g_free (author);
	g_free (copyright);
	g_free (desc);

	gtk_label_set_markup (GTK_LABEL (label), str);
	g_free (str);

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

	/* FIXME: save this choice */
}

static void
read_themes (GtkListStore *store, const char *theme_dir, DIR *dir)
{
	struct dirent *dent;

	while ((dent = readdir (dir)) != NULL) {
		char *n, *key, *file, *name, *desc, *author, *copyright, *ss;
		char *full;
		GtkTreeIter iter;
		if (dent->d_name[0] == '.')
			continue;
		n = g_strconcat (theme_dir, "/", dent->d_name,
				 "/GdmGreeterTheme.info", NULL);
		if (access (n, R_OK) != 0) {
			g_free (n);
			continue;
		}

		key = g_strconcat ("=", n, "=/GdmGreeterTheme/", NULL);
		gnome_config_push_prefix (key);
		g_free (key);

		file = gnome_config_get_translated_string ("Greeter");
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
			gnome_config_pop_prefix ();
			continue;
		}
		g_free (full);

		name = gnome_config_get_translated_string ("Name");
		if (ve_string_empty (name)) {
			g_free (name);
			name = g_strdup (dent->d_name);
		}
		desc = gnome_config_get_translated_string ("Description");
		author = gnome_config_get_translated_string ("Author");
		copyright = gnome_config_get_translated_string ("Copyright");
		ss = gnome_config_get_translated_string ("Screenshot");
		gnome_config_pop_prefix ();

		if (ss != NULL)
			full = g_strconcat (theme_dir, "/", dent->d_name,
					    "/", ss, NULL);
		else
			full = NULL;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    THEME_COLUMN_FILE, file,
				    THEME_COLUMN_NAME, name,
				    THEME_COLUMN_DESCRIPTION, desc,
				    THEME_COLUMN_AUTHOR, author,
				    THEME_COLUMN_COPYRIGHT, copyright,
				    THEME_COLUMN_SCREENSHOT, full,
				    -1);

		g_free (file);
		g_free (name);
		g_free (desc);
		g_free (author);
		g_free (copyright);
		g_free (ss);
		g_free (full);
		g_free (n);
	}
}

static void
setup_graphical_themes (void)
{
	DIR *dir;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkWidget *theme_list = glade_helper_get (xml, "gg_theme_list",
						  GTK_TYPE_TREE_VIEW);

	char *theme_dir = get_theme_dir ();

	/* create list store */
	store = gtk_list_store_new (THEME_NUM_COLUMNS,
				    G_TYPE_STRING /* file */,
				    G_TYPE_STRING /* name */,
				    G_TYPE_STRING /* desc */,
				    G_TYPE_STRING /* author */,
				    G_TYPE_STRING /* copyright */,
				    G_TYPE_STRING /* screenshot */);

	dir = opendir (theme_dir);

	if (dir != NULL) {
		read_themes (store, theme_dir, dir);
		closedir (dir);
	}

	g_free (theme_dir);

	gtk_tree_view_set_model (GTK_TREE_VIEW (theme_list), 
				 GTK_TREE_MODEL (store));

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);

        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", THEME_COLUMN_NAME,
                                             NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (theme_list), column);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (theme_list));

	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

        g_signal_connect (selection, "changed",
			  G_CALLBACK (selection_changed),
			  NULL);

}

static void
dialog_response (GtkWidget *dlg, int response, gpointer data)
{
	if (response == GTK_RESPONSE_CLOSE) {
		gtk_main_quit ();
	} else {
		/* FIXME: display help */
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
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (gtk_main_quit), NULL);
	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response), NULL);

	setup_xdmcp_support ();
	setup_background_support ();
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

	setup_greeter_untranslate_entry ("sg_welcome",
					 GDM_KEY_WELCOME);
}

static gboolean
gdm_event (GSignalInvocationHint *ihint,
	   guint		n_param_values,
	   const GValue	       *param_values,
	   gpointer		data)
{
	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	GdkEvent *event = g_value_get_pointer ((GValue *)param_values);
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
			    GNOME_PARAM_CREATE_DIRECTORIES, FALSE,
			    NULL);

	glade_gnome_init();

	gdm_running = gdmcomm_check (FALSE /* gui_bitching */);

	if (RUNNING_UNDER_GDM) {
		char *gtkrc;
		guint sid;

		/* If we are running under gdm parse the GDM gtkRC */
		gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
		gtkrc = gnome_config_get_string (GDM_KEY_GTKRC);
		gnome_config_pop_prefix ();
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);
		g_free (gtkrc);

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
		gtk_dialog_run (GTK_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	setup_gui ();

	gtk_main ();

	return 0;
}

/* EOF */
