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
