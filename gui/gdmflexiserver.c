/*
 *    GDMflexiserver - run a flexible server
 *    (c)2001 Queen of England
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

#include "config.h"
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <gdk/gdkx.h>
#include <X11/Xauth.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>

#include <viciousui.h>

#include "gdm.h"
#include "gdmcomm.h"

static GSList *xservers = NULL;
static gboolean got_standard = FALSE;
static gboolean use_xnest = FALSE;
static gboolean authenticate = FALSE;
static gboolean no_lock = FALSE;
static const char *send_command = NULL;
static const char *server = NULL;
static const char *chosen_server = NULL;
static gboolean debug = FALSE;

static void
read_servers (void)
{
	gpointer iter;
	char *k;

	/* Find server definitions */
	iter = gnome_config_init_iterator_sections ("=" GDM_CONFIG_FILE "=/");
	iter = gnome_config_iterator_next (iter, &k, NULL);

	while (iter) {
		if (strncmp (k, "server-", strlen ("server-")) == 0) {
			char *section;
			GdmXServer *svr;

			section = g_strdup_printf ("=" GDM_CONFIG_FILE "=/%s/", k);
			gnome_config_push_prefix (section);

			if ( ! gnome_config_get_bool
			     (GDM_KEY_SERVER_FLEXIBLE)) {
				gnome_config_pop_prefix ();
				g_free (section);
				g_free (k);
				iter = gnome_config_iterator_next (iter,
								   &k, NULL);
				continue;
			}

			svr = g_new0 (GdmXServer, 1);

			svr->id = g_strdup (k + strlen ("server-"));
			svr->name = gnome_config_get_string
				(GDM_KEY_SERVER_NAME);
			svr->command = gnome_config_get_string
				(GDM_KEY_SERVER_COMMAND);
			svr->flexible = TRUE;
			svr->choosable = gnome_config_get_bool
				(GDM_KEY_SERVER_CHOOSABLE);

			g_free (section);
			gnome_config_pop_prefix ();

			if (strcmp (svr->id, GDM_STANDARD) == 0)
				got_standard = TRUE;

			if (server != NULL &&
			    strcmp (svr->id, server) == 0)
				chosen_server = g_strdup (svr->id);

			xservers = g_slist_append (xservers, svr);
		}

		g_free (k);

		iter = gnome_config_iterator_next (iter, &k, NULL);
	}
}

static char *
choose_server (void)
{
	GtkWidget *dialog, *vbox;
	GtkWidget *w;
	GSList *group = NULL;
	GSList *li;

	if (chosen_server != NULL)
		return g_strdup (chosen_server);

	if (xservers == NULL)
		return NULL;

	if (xservers->next == NULL &&
	    got_standard)
		return g_strdup (GDM_STANDARD);

	dialog = gtk_dialog_new_with_buttons (_("Choose server"),
					      NULL /* parent */,
					      0 /* flags */,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      NULL);
	vbox = GTK_DIALOG (dialog)->vbox;

	w = gtk_label_new (_("Choose the X server to start"));
	gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

	group = NULL;
	if ( ! got_standard) {
		w = gtk_radio_button_new_with_label (group,
						     _("Standard server"));
		gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (w));
	}

	for (li = xservers; li != NULL; li = li->next) {
		GdmXServer *svr = li->data;
		w = gtk_radio_button_new_with_label
			(group, svr->name ? svr->name : svr->id);
		gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
		if (got_standard &&
		    strcmp (svr->id, GDM_STANDARD) == 0)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
						      TRUE);
		g_object_set_data (G_OBJECT (w), "ServerID", svr->id);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (w));
	}

	gtk_widget_show_all (dialog);

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:	
		/* OK */
		break;
	default:
		gtk_widget_destroy (dialog);
		/* cancel, or close */
		exit (0);
		break;
	}

	for (li = group; li != NULL; li = li->next) {
		GtkWidget *w = li->data;
		char *name = g_object_get_data (G_OBJECT (w), "ServerID");
		if (GTK_TOGGLE_BUTTON (w)->active) {
			gtk_widget_destroy (dialog);
			return g_strdup (name);
		}
	}

	gtk_widget_destroy (dialog);

	/* should never get here really */
	return NULL;
}

struct poptOption options [] = {
	{ "command", 'c', POPT_ARG_STRING, &send_command, 0, N_("Send the specified protocol command to gdm"), N_("COMMAND") },
	{ "xnest", 'n', POPT_ARG_NONE, &use_xnest, 0, N_("Xnest mode"), NULL },
	{ "no-lock", 'l', POPT_ARG_NONE, &no_lock, 0, N_("Do not lock current screen"), NULL },
	{ "debug", 'd', POPT_ARG_NONE, &debug, 0, N_("Debugging output"), NULL },
	{ "authenticate", 'a', POPT_ARG_NONE, &authenticate, 0, N_("Authenticate before running --command"), NULL },
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0}
};


int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	char *command;
	char *version;
	char *ret;
	const char *message;
	char *auth_cookie = NULL;
	poptContext ctx;
	const char **args;
	GnomeProgram *program;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	program = gnome_program_init ("gdmflexiserver", VERSION, 
				      LIBGNOMEUI_MODULE /* module_info */,
				      argc, argv,
				      GNOME_PARAM_POPT_TABLE, options,
				      NULL);
	g_object_get (G_OBJECT (program),
		      GNOME_PARAM_POPT_CONTEXT, &ctx,
		      NULL);	

	gdmcomm_set_debug (debug);

	args = poptGetArgs (ctx);
	if (args != NULL && args[0] != NULL)
		server = args[0];

	if ( ! gdmcomm_check (TRUE /* gui_bitching */))
		return 1;

	if (send_command != NULL) {
		if (authenticate)
			auth_cookie = gdmcomm_get_auth_cookie ();
		ret = gdmcomm_call_gdm (send_command, auth_cookie,
					"2.2.4.0", 5);
		if (ret != NULL) {
			g_print ("%s\n", ret);
			return 0;
		} else {
			dialog = gtk_message_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 _("Cannot communicate with gdm, perhaps "
				   "you have an old version running."));
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			return 1;
		}
	}

	if (use_xnest) {
		char *cookie = gdmcomm_get_a_cookie (FALSE /* binary */);
		if (cookie == NULL) {
			dialog = gtk_message_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 _("You do not seem to have the "
				   "authentication needed for this "
				   "operation.  Perhaps your .Xauthority "
				   "file is not set up correctly."));
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			return 1;
		}
		command = g_strdup_printf (GDM_SUP_FLEXI_XNEST " %s %d %s %s",
					   gdmcomm_get_display (),
					   (int)getuid (),
					   cookie,
					   XauFileName ());
		g_free (cookie);
		version = "2.3.90.4";
		auth_cookie = NULL;
	} else {
		auth_cookie = gdmcomm_get_auth_cookie ();

		if (auth_cookie == NULL) {
			dialog = gtk_message_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 _("You do not seem to be logged in on the "
				   "console.  Starting a new login only "
				   "works correctly on the console."));
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			return 1;
		}

		read_servers ();
		server = choose_server ();
		if (server == NULL)
			command = g_strdup (GDM_SUP_FLEXI_XSERVER);
		else
			command = g_strdup_printf (GDM_SUP_FLEXI_XSERVER " %s",
						   server);
		version = "2.2.4.0";
	}

	ret = gdmcomm_call_gdm (command, auth_cookie, version, 5);
	if (ret != NULL &&
	    strncmp (ret, "OK ", 3) == 0) {

		/* if we switched to a different screen as a result of this,
		 * lock the current screen */
		if ( ! no_lock && ! use_xnest) {
			char *argv[3] = {"xscreensaver-command", "-lock", NULL};
			if (gnome_execute_async (g_get_home_dir (), 2, argv) < 0)
				g_warning (_("Can't lock screen"));
			argv[1] = "-throttle";
			if (gnome_execute_async (g_get_home_dir (), 2, argv) < 0)
				g_warning (_("Can't disable xscreensaver display hacks"));
		}

		/* all fine and dandy */
		return 0;
	}

	message = gdmcomm_get_error_message (ret, use_xnest);

	dialog = gtk_message_dialog_new
		(NULL /* parent */,
		 GTK_DIALOG_MODAL /* flags */,
		 GTK_MESSAGE_WARNING,
		 GTK_BUTTONS_OK,
		 "%s", message);
	gtk_widget_show_all (dialog);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return 1;
}

/* Used for torture testing the socket */
#if 0
static void
torture (void)
{
	struct sockaddr_un addr;
	int fd;
	int i;
	int times;

	srand (getpid () * time (NULL));

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}

	strcpy (addr.sun_path, "/tmp/.gdm_socket");
	addr.sun_family = AF_UNIX;

	if (connect (fd, &addr, sizeof (addr)) < 0) {
		close (fd);
		return;
	}

	g_print ("OPEN ");

	times = rand () % 500;
	for (i = 0; i < rand () % 500; i++) {
		int len = rand () % 5000;
		char *buf = g_new (char, len);
		int ii;
		for (ii = 0; ii < len; ii ++)
			buf[ii] = rand () % 256;
		write (fd, buf, len); 
		g_free (buf);
		g_print ("SENT(%d) ", len);
	}

	close (fd);
}

static void
torture_test (void)
{
	int i;

	srand (getpid () * time (NULL));

	for (i = 0; i < 500; i ++) {
		if (fork () == 0) {
			torture ();
			_exit (0);
		}
		usleep (1000);
	}
}
#endif
