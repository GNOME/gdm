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

/* set the DOING_GDM_DEVELOPMENT env variable if you want to
 * search for the glade file in the current dir and not the system
 * install dir, better then something you have to change
 * in the source and recompile */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;

static gboolean RUNNING_UNDER_GDM = FALSE;

static GladeXML *xml;

static void
setup_user_combo (const char *name)
{
	GtkWidget *combo = glade_helper_get (xml, name, GTK_TYPE_COMBO);
	GList *users = NULL;
	struct passwd *pwent;

	/* normally empty */
	users = g_list_append (users, g_strdup (""));

	setpwent ();

	pwent = getpwent();
	
	while (pwent != NULL) {
		/* FIXME: 100 is a pretty arbitrary constant */
		if (pwent->pw_uid >= 100) {
			users = g_list_append (users,
					       g_strdup (pwent->pw_name));
		}
	
		pwent = getpwent();
	}

	endpwent ();

	gtk_combo_set_popdown_strings (GTK_COMBO (combo), users);

	g_list_foreach (users, (GFunc)g_free, NULL);
	g_list_free (users);
}

static void
setup_xdmcp_support (void)
{
#ifndef HAVE_LIBXDMCP
	gtk_widget_show (glade_helper_get (xml, "no_xdmcp_label", GTK_TYPE_LABEL));
	gtk_widget_set_sensitive (glade_helper_get (xml, "enable_xdmcp", GTK_TYPE_TOGGLE_BUTTON), FALSE);
	gtk_widget_set_sensitive (glade_helper_get (xml, "xdmcp_frame", GTK_TYPE_FRAME), FALSE);
#else /* ! HAVE_LIBXDMCP */
	gtk_widget_hide (glade_helper_get (xml, "no_xdmcp_label", GTK_TYPE_LABEL));
#endif /* HAVE_LIBXDMCP */
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

	setup_user_combo ("autologin_combo");
	setup_user_combo ("timedlogin_combo");
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
