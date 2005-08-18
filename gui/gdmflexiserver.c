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
#include <pwd.h>

#include <viciousui.h>

#include "gdm.h"
#include "gdmcomm.h"
#include "gdmcommon.h"

static GSList *xservers = NULL;
static gboolean got_standard = FALSE;
static gboolean use_xnest = FALSE;
static gboolean authenticate = FALSE;
static gboolean no_lock = FALSE;
static gboolean monte_carlo_pi = FALSE;
static const char *send_command = NULL;
static const char *server = NULL;
static const char *chosen_server = NULL;
static gboolean debug = FALSE;
static gboolean startnew = FALSE;
static char *auth_cookie = NULL;

static int
get_cur_vt (void)
{
	char *ret;
	static int cur_vt;
	static gboolean checked = FALSE;

	if (checked)
		return cur_vt;

	ret = gdmcomm_call_gdm ("QUERY_VT", auth_cookie, "2.5.90.0", 5);
	if (ve_string_empty (ret) ||
	    strncmp (ret, "OK ", 3) != 0) {
		g_free (ret);
		return -1;
	}

	if (sscanf (ret, "OK %d", &cur_vt) != 1)
		cur_vt = -1;
	g_free (ret);
	checked = TRUE;
	return cur_vt;
}

/* change to an existing vt */
static void
change_vt (int vt)
{
	char *cmd;
	char *ret;
	cmd = g_strdup_printf (GDM_SUP_SET_VT " %d", vt);
	ret = gdmcomm_call_gdm (cmd, auth_cookie, "2.5.90.0", 5);
	g_free (cmd);
	if (ve_string_empty (ret) ||
	    strcmp (ret, "OK") != 0) {
		GtkWidget *dialog;
		const char *message = gdmcomm_get_error_message (ret, use_xnest);

		dialog = ve_hig_dialog_new
			(NULL /* parent */,
			 GTK_DIALOG_MODAL /* flags */,
			 GTK_MESSAGE_ERROR,
			 GTK_BUTTONS_OK,
			 FALSE /* markup */,
			 _("Cannot change display"),
			 "%s", message);

		gtk_widget_show_all (dialog);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
	g_free (ret);
}

static int
get_vt_num (char **vec, char *vtpart, int depth)
{
	int i;

	if (ve_string_empty (vtpart) || depth <= 0)
		return -1;

	if (strchr (vtpart, ':') == NULL)
		return atoi (vtpart);

	for (i = 0; vec[i] != NULL; i++) {
		char **rvec;
		rvec = g_strsplit (vec[i], ",", -1);
		if (rvec == NULL ||
		    ve_vector_len (rvec) != 3)
			continue;

		if (strcmp (rvec[0], vtpart) == 0) {
			/* could be nested? */
			int r = get_vt_num (vec, rvec[2], depth-1);
			g_strfreev (rvec);
			return r;

		}

		g_strfreev (rvec);
	}
	return -1;
}

enum {
	COLUMN_LOGIN /* human string */,
	COLUMN_DISPLAY /* human string */,
	COLUMN_VT /* vt number */,
	COLUMN_NUM
};

static GtkTreeModel *
create_model (char **vec)
{
	int i;
	GtkListStore *store;
	GtkTreeIter iter;

	/* create list store */
	store = gtk_list_store_new (COLUMN_NUM,
				    G_TYPE_STRING /* login */,
				    G_TYPE_STRING /* display */,
				    G_TYPE_INT /* vt */);

	for (i = 0; vec[i] != NULL; i++) {
		char **rvec;
		int vt;
		rvec = g_strsplit (vec[i], ",", -1);
		if (rvec == NULL ||
		    ve_vector_len (rvec) != 3)
			continue;

		vt = get_vt_num (vec, rvec[2], 5);

		if (strcmp (rvec[0], gdmcomm_get_display ()) != 0 &&
		    vt >= 0) {
			char *user;
			char *disp;

			if (ve_string_empty (rvec[1])) {
				user = g_strdup (_("Nobody"));
			} else {
				struct passwd *pw = getpwnam (rvec[1]);
				if (pw == NULL ||
				    ve_string_empty (pw->pw_gecos)) {
					char *login;
					login = g_markup_escape_text (rvec[1], -1);

					user = g_strdup_printf ("<b>%s</b>",
								login);

					g_free (login);
				} else {
					char *utf8gecos;
					char *gecos, *login;
					login = g_markup_escape_text (rvec[1], -1);
					if ( ! g_utf8_validate (pw->pw_gecos, -1, NULL))
						utf8gecos = ve_locale_to_utf8 (pw->pw_gecos);
					else
						utf8gecos = g_strdup (pw->pw_gecos);

					gecos = g_markup_escape_text (utf8gecos, -1);

					user = g_strdup_printf ("<b>%s</b>\n%s",
								login,
								gecos);

					g_free (login);
					g_free (gecos);
					g_free (utf8gecos);
				}
			}

			if (strchr (rvec[2], ':') == NULL) {
				disp = g_strdup_printf
					(_("Display %s on virtual "
					   "terminal %d"),
					 rvec[0], vt);
			} else {
				disp = g_strdup_printf
					(_("Nested display %s on virtual "
					   "terminal %d"),
					 rvec[0], vt);
			}

			/* this is not the current display */
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					    COLUMN_LOGIN, user,
					    COLUMN_DISPLAY, disp,
					    COLUMN_VT, vt,
					    -1);

			g_free (user);
			g_free (disp);
		}

		g_strfreev (rvec);
	}

	return GTK_TREE_MODEL (store);
}

static void
add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Username"),
							   renderer,
							   "markup",
							   COLUMN_LOGIN,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_LOGIN);
	gtk_tree_view_append_column (treeview, column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Display"),
							   renderer,
							   "text",
							   COLUMN_DISPLAY,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_DISPLAY);
	gtk_tree_view_append_column (treeview, column);
}

enum {
	RESPONSE_OPEN_NEW_DISPLAY,
	RESPONSE_OPEN_EXISTING_DISPLAY
};

static void
selection_changed (GtkTreeSelection *selection, gpointer data)
{
	GtkWidget *dialog = data;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
						   RESPONSE_OPEN_EXISTING_DISPLAY,
						   TRUE);
	} else {
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
						   RESPONSE_OPEN_EXISTING_DISPLAY,
						   FALSE);
	}
}

static void
row_activated (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data)
{
	GtkWidget *dialog = data;
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	gtk_tree_selection_select_path (selection, path);
	gtk_dialog_response (GTK_DIALOG (dialog), RESPONSE_OPEN_EXISTING_DISPLAY);
}

static void
run_logged_in_dialogue (char **vec)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	GtkWidget *w;
	GtkWidget *sw;
	GtkTreeModel *model;
	GtkWidget *treeview;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gint response;

	if (startnew == TRUE) {
		/* Just return if the user doesn't want to see the dialog */
		return;
	} else {
		dialog = gtk_dialog_new_with_buttons (_("Open Displays"),
					      NULL /* parent */,
					      0 /* flags */,
					      _("_Open New Display"),
					      RESPONSE_OPEN_NEW_DISPLAY,
					      _("Change to _Existing Display"),
					      RESPONSE_OPEN_EXISTING_DISPLAY,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      NULL);
		gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
		vbox = GTK_DIALOG (dialog)->vbox;

		w = gtk_label_new (_("There are some displays already open.  You can select "
			     "one from the list below or open a new one."));
		gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

		sw = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
						     GTK_SHADOW_ETCHED_IN);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);
		gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);

		/* create tree model */
		model = create_model (vec);

		/* create tree view */
		treeview = gtk_tree_view_new_with_model (model);
		gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (treeview), TRUE);

		g_object_unref (model);

		gtk_container_add (GTK_CONTAINER (sw), treeview);

		/* add columns to the tree view */
		add_columns (GTK_TREE_VIEW (treeview));

		/* finish & show */
		gtk_window_set_default_size (GTK_WINDOW (dialog), 280, 250);

	        g_signal_connect (G_OBJECT (treeview), "row_activated",
				  G_CALLBACK (row_activated),
				  dialog);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

		g_signal_connect (selection, "changed",
				  G_CALLBACK (selection_changed),
				  dialog);

		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
					   RESPONSE_OPEN_EXISTING_DISPLAY,
					   FALSE);

		gtk_widget_show_all (dialog);
		response = gtk_dialog_run (GTK_DIALOG (dialog));
	}

run_again:
	switch (response) {
	case RESPONSE_OPEN_NEW_DISPLAY:
		gtk_widget_destroy (dialog);

		/* just continue what you are doing */
		return;

	case RESPONSE_OPEN_EXISTING_DISPLAY:
		if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
			GValue value = {0};
			int vt;
			gtk_tree_model_get_value (model, &iter,
						  COLUMN_VT,
						  &value);
			vt = g_value_get_int (&value);
			g_value_unset (&value);

			/* we switched to a different screen as a result of this,
			 * lock the current screen */
			if ( ! no_lock && vt != get_cur_vt () && vt >= 0) {
				char *argv[3] = {"xscreensaver-command",
					"-lock", NULL};

				if (gnome_execute_async (g_get_home_dir (),
				    2, argv) < 0)
					g_warning (_("Can't lock screen"));

				argv[1] = "-throttle";

				if (gnome_execute_async (g_get_home_dir (),
				    2, argv) < 0)
					g_warning (_("Can't disable xscreensaver display hacks"));
			}

			change_vt (vt);

			/* FIXME: wait + disturb the pointer (need SUP?), 
			 * perhaps part of the sup command to CHVT ?? */

			exit (0);
		} else {
			/* EEK */
			goto run_again;
		}
		break;

	default:
		gtk_widget_destroy (dialog);
		/* cancel, or close */
		exit (0);
		break;
	}
}

static void
check_for_users (void)
{
	char *ret;
	char **vec;
	int i;
	int extra;

	/* only for console logins on vt supporting systems */
	if (auth_cookie == NULL ||
	    get_cur_vt () < 0)
		return;

	ret = gdmcomm_call_gdm ("CONSOLE_SERVERS", auth_cookie, "2.2.4.0", 5);
	if (ve_string_empty (ret) ||
	    strncmp (ret, "OK ", 3) != 0) {
		g_free (ret);
		return;
	}

	vec = g_strsplit (&ret[3], ";", -1);
	g_free (ret);
	if (vec == NULL)
		return;

	extra = 0;

	for (i = 0; vec[i] != NULL; i++) {
		char **rvec;
		int vt;
		rvec = g_strsplit (vec[i], ",", -1);
		if (rvec == NULL ||
		    ve_vector_len (rvec) != 3)
			continue;

		vt = get_vt_num (vec, rvec[2], 5);

		if (strcmp (rvec[0], gdmcomm_get_display ()) != 0 &&
		    vt >= 0) {
			/* this is not the current display */
			extra ++;
		}

		g_strfreev (rvec);
	}

	if (extra == 0) {
		g_strfreev (vec);
		return;
	}

	run_logged_in_dialogue (vec);

	g_strfreev (vec);
}

static void
read_servers (gchar *config_file)
{
	gpointer iter;
	gchar *config_sections;
	char *k;

	/* Find server definitions */
	config_sections = g_strdup_printf ("=%s=/", config_file);
	iter = gnome_config_init_iterator_sections (config_sections);
	iter = gnome_config_iterator_next (iter, &k, NULL);

	while (iter) {
		if (strncmp (k, "server-", strlen ("server-")) == 0) {
			char *section;
			GdmXServer *svr;

			section = g_strdup_printf ("=%s=/", config_file);
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
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
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

static void
calc_pi (void)
{
	unsigned long n = 0, h = 0;
	double x, y;
	printf ("\n");
	for (;;) {
		x = g_random_double ();
		y = g_random_double ();
		if (x*x + y*y <= 1)
			h++;
		n++;
		if ( ! (n & 0xfff))
			printf ("pi ~~ %1.10f\t(%lu/%lu * 4) iteration: %lu \r",
				((double)h)/(double)n * 4.0, h, n, n);
	}
}

struct poptOption options [] = {
	{ "command", 'c', POPT_ARG_STRING, &send_command, 0, N_("Send the specified protocol command to gdm"), N_("COMMAND") },
	{ "xnest", 'n', POPT_ARG_NONE, &use_xnest, 0, N_("Xnest mode"), NULL },
	{ "no-lock", 'l', POPT_ARG_NONE, &no_lock, 0, N_("Do not lock current screen"), NULL },
	{ "debug", 'd', POPT_ARG_NONE, &debug, 0, N_("Debugging output"), NULL },
	{ "authenticate", 'a', POPT_ARG_NONE, &authenticate, 0, N_("Authenticate before running --command"), NULL },
	{ "startnew", 's', POPT_ARG_NONE, &startnew, 0, N_("Start new flexible session, do not show popup"), NULL },
	{ "monte-carlo-pi", 0, POPT_ARG_NONE, &monte_carlo_pi, 0, NULL, NULL },
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0}
};


int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	gchar *config_file;
	char *command;
	char *version;
	char *ret;
	const char *message;
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

	if (monte_carlo_pi) {
		calc_pi ();
		return 0;
	}

	gdmcomm_set_debug (debug);

	args = poptGetArgs (ctx);
	if (args != NULL && args[0] != NULL)
		server = args[0];

	config_file = gdm_common_get_config_file ();
	if (config_file == NULL) {
		g_print (_("Could not access GDM configuration file.\n"));
		exit (0);
	}

	if ( ! gdmcomm_check (config_file, TRUE /* gui_bitching */)) {
		g_free (config_file);
		return 1;
	}

	if (send_command != NULL) {
		if (authenticate)
			auth_cookie = gdmcomm_get_auth_cookie ();
		ret = gdmcomm_call_gdm (send_command, auth_cookie,
					"2.2.4.0", 5);
		if (ret != NULL) {
			g_print ("%s\n", ret);
			g_free (config_file);
			return 0;
		} else {
			dialog = ve_hig_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 _("Cannot communicate with GDM "
				   "(The GNOME Display Manager)"),
				 "%s",
				 _("Perhaps you have an old version "
				   "of GDM running."));
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			g_free (config_file);
			return 1;
		}
	}

	/* always attempt to get cookie and authenticate.  On remote
	servers */
	auth_cookie = gdmcomm_get_auth_cookie ();

	/* check for other displays/logged in users */
	check_for_users ();

	if (use_xnest) {
		char *cookie = gdmcomm_get_a_cookie (FALSE /* binary */);
		if (cookie == NULL) {
			dialog = ve_hig_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 _("You do not seem to have the "
				   "authentication needed for this "
				   "operation"),
				 "%s",
				 _("Perhaps your .Xauthority "
				   "file is not set up correctly."));

			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			g_free (config_file);
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
		if (auth_cookie == NULL) {
			dialog = ve_hig_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 _("You do not seem to be logged in on the "
				   "console"),
				 "%s",
				 _("Starting a new login only "
				   "works correctly on the console."));
			gtk_dialog_set_has_separator (GTK_DIALOG (dialog),
						      FALSE);
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			g_free (config_file);
			return 1;
		}

		read_servers (config_file);
		server = choose_server ();
		if (server == NULL)
			command = g_strdup (GDM_SUP_FLEXI_XSERVER);
		else
			command = g_strdup_printf (GDM_SUP_FLEXI_XSERVER " %s",
						   server);
		version = "2.2.4.0";
	}

	g_free (config_file);

	ret = gdmcomm_call_gdm (command, auth_cookie, version, 5);
	g_free (command);

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

	dialog = ve_hig_dialog_new
		(NULL /* parent */,
		 GTK_DIALOG_MODAL /* flags */,
		 GTK_MESSAGE_ERROR,
		 GTK_BUTTONS_OK,
		 FALSE /* markup */,
		 _("Cannot start new display"),
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
