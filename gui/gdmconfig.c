/*
 *    GDMconfig, a graphical configurator for the GNOME display manager
 *    Copyright (C) 1999,2000,2001 Lee Mallabone <lee0@callnetuk.com>
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

#include <config.h>
#include <gnome.h>

#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <syslog.h>

#include <viciousui.h>

#include "gdmconfig.h"
#include "misc.h"

/* set the DOING_GDM_DEVELOPMENT env variable if you don't
 * want to do that root stuff, better then something you have to change
 * in the source and recompile */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;

/* XML Structures for the various configurator components */
GladeXML *GUI = NULL;
GladeXML *basic_notebook = NULL;
GladeXML *expert_notebook = NULL;
GladeXML *system_notebook = NULL;

/* The XML file */
gchar *glade_filename;

/* FIXME: this is a global and immutable, it should change as the
 * user changes the dir, but that has many problems, so for now it
 * stays the same all the time */
static char *sessions_directory = NULL;

/* 3 user levels are present in the CList */
gchar *basic_row[1] = { N_("Basic") };
gchar *expert_row[1] = { N_("Expert") };
gchar *system_row[1] = { N_("System") };

gchar *desc1 = N_("This panel displays the basic options for configuring GDM.\n"
				"\n"
				"If you need finer detail, select 'expert' or 'system setup' from the list above.\n"
				"\n"
				"This will display some of the more complex options of GDM that rarely need to be changed.");
gchar *desc2 = N_("This panel displays the more advanced options of GDM.\n"
				"\n"
				"Be sure to take care when manipulating the security options, or you could be "
				"vulnerable to attackers.\n"
				"\n"
				"Choose \"System\" to change fundamental options in GDM.");
gchar *desc3 = N_("This panel displays GDM's fundamental system settings.\n"
				"\n"
				"You should only change these paths if you really know what you are doing, as an incorrect "
				"setup could stop your machine from booting properly.\n"
				"\n"
				"Choose \"Basic\" if you just want to change your machine's login appearance.");

/* Keep track of X servers, the selected user level and session details */
int number_of_servers = 0;
int selected_server_row = -1;
int selected_server_def_row = -1;
int selected_session_row = -1, default_session_row = -1;
GtkWidget *invisible_notebook = NULL;
GdmConfigSession *current_default_session = NULL;
GdmConfigSession *old_current_default_session = NULL;
char *default_session_link_name = NULL;
GList *deleted_sessions_list = NULL;

const char *names_to_use[] = {
	"foo",
	"bar",
	"bah",
	"blurb",
	"babble",
	"bubbly",
	"buck",
	"bucket",
	"cabbage",
	"cackle",
	"dangle",
	"dashboard",
	"dawn",
	"each",
	"earmarks",
	"eastbound",
	"eclipse",
	"fabric",
	NULL };


/* Main application widget pointer */
GtkWidget *GDMconfigurator = NULL;

/** This is something of a hack, but it keeps the code clean and
 * easily extensible later on. 
 */
GtkWidget *get_widget(const gchar *widget_name)
{
	GtkWidget *ret = NULL;
	ret = glade_xml_get_widget(GUI, widget_name);
	if (!ret) {
		ret = glade_xml_get_widget(basic_notebook, widget_name);
		if (!ret) {
			ret = glade_xml_get_widget(system_notebook, widget_name);
			if (!ret) {
				ret = glade_xml_get_widget(expert_notebook, widget_name);
			}
		}
	}

	if (ret == NULL) {
		char *error =
			g_strdup_printf (_("The glade ui description file "
					   "doesn't seem to contain the\n"
					   "widget \"%s\".  "
					   "Unfortunately I cannot continue.\n"
					   "Please check your installation."),
					 widget_name);
		GtkWidget *fatal_error = gnome_error_dialog (error);
		g_free (error);
		gnome_dialog_run_and_close (GNOME_DIALOG (fatal_error));
		exit (EXIT_FAILURE);
	}

	return ret;
}


/* This function examines a set of commonly named radio buttons and writes out an integer
 * with gnome_config under 'key'. It looks for max_value number of radio buttons, and writes
 * the integer with the value of the earliest named radio button it finds.
 */
void gdm_radio_write (gchar *radio_base_name, 
		      gchar *key, 
		      int max_value)
{
   int i = 0;
   while (i <= max_value) {
      gchar *widget_name = g_strdup_printf ("%s_%d", radio_base_name, i);
      if (GTK_TOGGLE_BUTTON (get_widget(widget_name))->active) {
	 gnome_config_set_int(key, i);
	 g_free (widget_name);
	 return;
      }
      g_free (widget_name);
      i++;
   }
}

static gboolean
gdm_event (GtkObject *object,
	   guint signal_id,
	   guint n_params,
	   GtkArg *params,
	   gpointer data)
{
	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	GdkEvent *event = GTK_VALUE_POINTER(params[0]);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}      

static void
check_binary (GtkEntry *entry)
{
	char *bin = ve_first_word (gtk_entry_get_text (entry));

	if ( ! ve_string_empty (bin) &&
	    access (bin, X_OK) == 0)
		ve_entry_set_red (GTK_WIDGET (entry), FALSE);
	else
		ve_entry_set_red (GTK_WIDGET (entry), TRUE);

	g_free (bin);
}

static void
check_dir (GtkEntry *entry)
{
	char *text = gtk_entry_get_text (entry);

	/* first try access as it's a LOT faster then stat */
	if ( ! ve_string_empty (text) &&
	    access (text, R_OK) == 0) {
		struct stat sbuf;
		/* check for this being a directory */
		if (stat (text, &sbuf) < 0 ||
		    ! S_ISDIR (sbuf.st_mode))
			ve_entry_set_red (GTK_WIDGET (entry), TRUE);
		else
			ve_entry_set_red (GTK_WIDGET (entry), FALSE);
	} else {
		ve_entry_set_red (GTK_WIDGET (entry), TRUE);
	}
}

static void
check_dirname (GtkEntry *entry)
{
	char *text = gtk_entry_get_text (entry);
	char *dir;

	if (text == NULL)
		return;
	dir = g_dirname (text);
	if (dir == NULL)
		return;

	/* first try access as it's a LOT faster then stat */
	if ( ! ve_string_empty (dir) &&
	    access (dir, R_OK) == 0) {
		struct stat sbuf;
		/* check for this being a directory */
		if (stat (dir, &sbuf) < 0 ||
		    ! S_ISDIR (sbuf.st_mode))
			ve_entry_set_red (GTK_WIDGET (entry), TRUE);
		else
			ve_entry_set_red (GTK_WIDGET (entry), FALSE);
	} else {
		ve_entry_set_red (GTK_WIDGET (entry), TRUE);
	}

	g_free (dir);
}

static void
check_file (GtkEntry *entry)
{
	char *text = gtk_entry_get_text (entry);

	if ( ! ve_string_empty (text) &&
	    access (text, R_OK) == 0) {
		ve_entry_set_red (GTK_WIDGET (entry), FALSE);
	} else {
		ve_entry_set_red (GTK_WIDGET (entry), TRUE);
	}
}

static void
connect_binary_checks (void)
{
	int i;
	char *binaries [] = {
		"chooser_binary",
		"config_binary",
		"greeter_binary",
		"halt_command",
		"reboot_command",
		"suspend_command",
		"background_program",
		"failsafe_x_server",
		"x_keeps_crashing",
		NULL
	};
	for (i = 0; binaries[i] != NULL; i++)
		gtk_signal_connect (GTK_OBJECT (get_widget (binaries[i])),
				    "changed",
				    GTK_SIGNAL_FUNC (check_binary), NULL);
}

static void
connect_dir_checks (void)
{
	int i;
	char *dirs [] = {
		"init_dir",
		"log_dir",
		"session_dir",
		"pre_session_dir",
		"post_session_dir",
		"user_auth_dir",
		"user_auth_fb_dir",
		"global_faces_dir",
		"host_images_dir",
		NULL
	};
	for (i = 0; dirs[i] != NULL; i++)
		gtk_signal_connect (GTK_OBJECT (get_widget (dirs[i])),
				    "changed",
				    GTK_SIGNAL_FUNC (check_dir), NULL);
}

static void
connect_dirname_checks (void)
{
	int i;
	char *dirs [] = {
		"pid_file",
		NULL
	};
	for (i = 0; dirs[i] != NULL; i++)
		gtk_signal_connect (GTK_OBJECT (get_widget (dirs[i])),
				    "changed",
				    GTK_SIGNAL_FUNC (check_dirname), NULL);
}

static void
connect_file_checks (void)
{
	int i;
	char *files [] = {
		"gnome_default_session",
		"gtkrc_file",
		"logo_file",
		"background_image",
		"default_face_file",
		"locale_file",
		"default_host_image_file",
		NULL
	};
	for (i = 0; files[i] != NULL; i++)
		gtk_signal_connect (GTK_OBJECT (get_widget (files[i])),
				    "changed",
				    GTK_SIGNAL_FUNC (check_file), NULL);
}


int
main (int argc, char *argv[])
{
	if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
		DOING_GDM_DEVELOPMENT = TRUE;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init ("gdmconfig", VERSION, argc, argv);
	glade_gnome_init();

	/* If we are running under gdm parse the GDM gtkRC */
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
		char *gtkrc;
		gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
		gtkrc = gnome_config_get_string (GDM_KEY_GTKRC);
		gnome_config_pop_prefix ();
		if ( ! ve_string_empty (gtkrc))
			gtk_rc_parse (gtkrc);
		g_free (gtkrc);
	}

	/* Make sure the user is root. If not, they shouldn't be messing with 
	 * GDM's configuration.
	 */

	if ( ! DOING_GDM_DEVELOPMENT) {
		if (geteuid() != 0)
		{
			GtkWidget *fatal_error = 
				gnome_error_dialog(_("You must be the superuser (root) to configure GDM.\n"));
			gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
			exit(EXIT_FAILURE);
		}
	}

	/* Look for the glade file in $(datadir)/gdm or, failing that,
	 * look in the current directory.
	 * Except when doing development, we want the app to use the glade file
	 * in the same directory, so we can actually make changes easily.
	 */

	if ( ! DOING_GDM_DEVELOPMENT) {
		if (g_file_exists (GDM_GLADE_DIR "/gdmconfig.glade")) {
			glade_filename = g_strdup (GDM_GLADE_DIR
						   "/gdmconfig.glade");
		} else {
			glade_filename = gnome_datadir_file ("gdm/gdmconfig.glade");
			if (glade_filename == NULL) {	  
				glade_filename = g_strdup ("gdmconfig.glade");
			}
		}

		glade_helper_add_glade_directory (GDM_GLADE_DIR);
	} else {
		glade_filename = g_strdup ("gdmconfig.glade");
	}

	/* Build the user interface */
	GUI = glade_xml_new(glade_filename, "gdmconfigurator");
	basic_notebook = glade_xml_new(glade_filename, "basic_notebook");
	system_notebook = glade_xml_new(glade_filename, "system_notebook");
	expert_notebook = glade_xml_new(glade_filename, "expert_notebook");

	if (GUI == NULL ||
	    basic_notebook == NULL ||
	    system_notebook == NULL ||
	    expert_notebook == NULL) {
		GtkWidget *fatal_error = 
			gnome_error_dialog(_("Cannot find the glade interface description\n"
					     "file, cannot run gdmconfig.\n"
					     "Please check your installation and the\n"
					     "location of the gdmconfig.glade file."));
		gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
		exit(EXIT_FAILURE);
	}

	invisible_notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (invisible_notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (invisible_notebook), FALSE);
	gtk_notebook_set_tab_border (GTK_NOTEBOOK (invisible_notebook), 0);
	gtk_notebook_append_page (GTK_NOTEBOOK (invisible_notebook),
				  get_widget ("basic_notebook"),
				  NULL);
	gtk_notebook_append_page (GTK_NOTEBOOK (invisible_notebook),
				  get_widget ("expert_notebook"),
				  NULL);
	gtk_notebook_append_page (GTK_NOTEBOOK (invisible_notebook),
				  get_widget ("system_notebook"),
				  NULL);
	gtk_container_add (GTK_CONTAINER (get_widget ("main_container")),
			   invisible_notebook);
	gtk_widget_show (invisible_notebook);

	/* Sanity checking */
	GDMconfigurator = get_widget("gdmconfigurator");
	if (GDMconfigurator == NULL) {
		GtkWidget *fatal_error = 
			gnome_error_dialog(_("Cannot find the gdmconfigurator widget in\n"
					     "the glade interface description file\n"
					     "Please check your installation."));
		gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
		exit(EXIT_FAILURE);
	}

	/* connect the checker signals before parsing */
	connect_binary_checks ();
	connect_dir_checks ();
	connect_dirname_checks ();
	connect_file_checks ();

	/* We set most of the user interface NOT wanting signals to get triggered as
	 * we do it. Then we hook up the signals, and THEN set a few remaining elements.
	 * This ensures sensitivity of some widgets is correct, and that the font picker
	 * gets set properly.
	 */
	gdm_config_parse_most(FALSE);
	glade_xml_signal_autoconnect(GUI);

	/* we hack up our icon entry */
	hack_icon_entry (GNOME_ICON_ENTRY (get_widget ("gdm_icon")));
	{
		GtkWidget *entry = gnome_icon_entry_gtk_entry (GNOME_ICON_ENTRY (get_widget ("gdm_icon")));
		gtk_signal_connect (GTK_OBJECT (entry), "changed",
				    GTK_SIGNAL_FUNC (can_apply_now),
				    NULL);
	}



	glade_xml_signal_autoconnect(basic_notebook);
	glade_xml_signal_autoconnect(expert_notebook);
	glade_xml_signal_autoconnect(system_notebook);

	gdm_config_parse_remaining(FALSE);

#ifndef HAVE_LIBXDMCP
	gtk_widget_show (get_widget ("no_xdmcp_label"));
	gtk_widget_set_sensitive (get_widget ("enable_xdmcp"), FALSE);
	gtk_widget_set_sensitive (get_widget ("xdmcp_frame"), FALSE);
#endif /* ! HAVE_LIBXDMCP */

	gtk_clist_column_titles_passive (GTK_CLIST (get_widget ("user_level_clist")));
	gtk_clist_column_titles_passive (GTK_CLIST (get_widget ("server_clist")));
	gtk_clist_column_titles_passive (GTK_CLIST (get_widget ("sessions_clist")));

	gtk_clist_append(GTK_CLIST(get_widget("user_level_clist")),
			 basic_row);					 
	gtk_clist_append(GTK_CLIST(get_widget("user_level_clist")),
			 expert_row);					 
	gtk_clist_append(GTK_CLIST(get_widget("user_level_clist")),
			 system_row);

	gtk_clist_select_row(GTK_CLIST(get_widget("user_level_clist")), 0,0);

	gtk_window_set_title(GTK_WINDOW(GDMconfigurator),
			     _("GNOME Display Manager Configurator"));
	gtk_widget_set_sensitive(GTK_WIDGET(get_widget("apply_button")), FALSE);

	gtk_widget_show (GDMconfigurator);

	/* If we are running under gdm and not in a normal session we want to
	 * treat the right mouse button like the first */
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
		guint sid = gtk_signal_lookup ("event",
					       GTK_TYPE_WIDGET);
		gtk_signal_add_emission_hook (sid,
					      gdm_event,
					      NULL);
	}

	gtk_main ();
	return 0;
}

void user_level_row_selected(GtkCList *clist, gint row,
			     gint column, GdkEvent *event, gpointer data) {
   g_assert (row >= 0 && row < 3);
   gtk_notebook_set_page (GTK_NOTEBOOK (invisible_notebook), row);

   switch (row) {
   case 0:
	   gtk_label_set(GTK_LABEL(get_widget("info_label")),
			 _(desc1));
	   break;
   case 1:
	   gtk_label_set(GTK_LABEL(get_widget("info_label")),
			 _(desc2));
	   break;
   case 2:
	   gtk_label_set(GTK_LABEL(get_widget("info_label")),
			 _(desc3));
	   break;
   default:
	   g_assert_not_reached();
   }
}

void
show_about_box (void)
{
	GladeXML *xml;
	static GtkWidget *dialog = NULL;

	if (dialog != NULL) {
		gtk_widget_show_now (dialog);
		gdk_window_raise (dialog->window);
		return;
	}

	xml = glade_helper_load ("gdmconfig.glade",
				 "about_gdmconfig",
				 gnome_about_get_type (),
				 TRUE /* dump on destroy */);
	dialog = glade_helper_get (xml, "about_gdmconfig",
				   gnome_about_get_type ());
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
				 (GtkWindow *) GDMconfigurator);
	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			    &dialog);
}

static int
find_server_def (const char *id)
{
	GtkCList *clist = (GtkCList *) get_widget ("server_def_clist");
	int i;

	for (i = 0; i < clist->rows; i++) {
		char *rowid = gtk_clist_get_row_data (clist, i);

		if ((rowid == NULL &&
		     id == NULL) ||
		    (rowid != NULL &&
		     id != NULL &&
		     strcmp (rowid, id) == 0)) {
			return i;
		}
	}
	return -1;
}

static int
find_default_server_def (void)
{
	GtkCList *clist = (GtkCList *) get_widget ("server_def_clist");
	int i;

	for (i = 0; i < clist->rows; i++) {
		char *rowid = gtk_clist_get_row_data (clist, i);

		if (rowid != NULL &&
		    strcmp (rowid, GDM_STANDARD) == 0) {
			return i;
		}
	}
	return -1;
}

static char *
get_unique_name (const char *except)
{
	int i, ii;

	for (ii = 0;; ii++) {
		for (i = 0; names_to_use [i] != NULL; i++) {
			char *thing;

			if (ii == 0)
				thing = g_strdup (names_to_use[i]);
			else
				thing = g_strdup_printf ("%s%d",
							 names_to_use[i], ii);
			if ((except == NULL ||
			     strcmp (except, thing) != 0) &&
			    find_server_def (thing) < 0)
				return thing;

			g_free (thing);
		}
	}
	g_assert_not_reached ();
	/* never reached */
	return NULL;
}

static char *
get_server_name (const char *id)
{
	GtkCList *clist = (GtkCList *) get_widget ("server_def_clist");
	int row;

	row = find_server_def (id);
	if (row >= 0) {
		char *text = NULL;
		gtk_clist_get_text (clist, row, 0, &text);

		return g_strdup (ve_sure_string (text));
	} else {
		return g_strdup ("");
	}
}

static void
rename_row (int row, const char *to)
{
	GtkCList *clist = (GtkCList *) get_widget ("server_def_clist");
	GtkCList *svr_clist = (GtkCList *) get_widget ("server_clist");
	char *oldid;
	char *new_name;
	int i;

	oldid = g_strdup (gtk_clist_get_row_data (clist, row));

	if (to == NULL)
		new_name = get_unique_name (oldid /* except */);
	else
		new_name = g_strdup (to);

	if (oldid != NULL &&
	    strcmp (oldid, new_name) == 0) {
		g_free (new_name);
		g_free (oldid);
		return;
	}

	gtk_clist_set_row_data_full (clist, row, new_name,
				     (GDestroyNotify) g_free);

	for (i = 0; i < svr_clist->rows; i++) {
		char *rowid = gtk_clist_get_row_data (clist, i);

		if ((rowid == NULL &&
		     oldid == NULL) ||
		    (rowid != NULL &&
		     oldid != NULL &&
		     strcmp (rowid, oldid) == 0)) {
			gtk_clist_set_row_data_full (svr_clist, i, new_name,
						     (GDestroyNotify) g_free);
		}
	}

	g_free (oldid);
}


void
gdm_config_parse_most (gboolean factory)
{
    void *iter;
    gchar *key, *value, *prefix;
    DIR *sessdir;
    struct dirent *dent;
    struct stat statbuf;
    gchar *default_session_name = NULL;
    gint linklen;
    const char *config_file;
    gboolean got_standard_server = FALSE;
    gboolean got_any_servers = FALSE;
    char *standard_x_server = NULL;
    int biggest_server, i;

    gtk_clist_clear (GTK_CLIST (get_widget ("sessions_clist")));
    gtk_clist_clear (GTK_CLIST (get_widget ("server_clist")));
    number_of_servers = 0;

    if (factory)
	    config_file = GDM_FACTORY_CONFIG_FILE;
    else
	    config_file = GDM_CONFIG_FILE;

    /* If the GDM config file does not exist, we have sensible defaults,
     * but make sure the user is warned.
     */
    if (!g_file_exists(config_file))
      {
	      char *a_server[3];
	      int row;
	      char *error = g_strdup_printf (_("The configuration file: %s\n"
					       "does not exist! Using "
					       "default values."),
					     config_file);
	      GtkWidget *error_dialog = gnome_error_dialog(error);
	      g_free (error);
	      gnome_dialog_set_parent(GNOME_DIALOG(error_dialog), 
				      (GtkWindow *) GDMconfigurator);
	      gnome_dialog_run_and_close(GNOME_DIALOG(error_dialog));

	      a_server[0] = "0";
	      a_server[1] = _("Standard server");
	      a_server[2] = "";
	      row = gtk_clist_append (GTK_CLIST (get_widget ("server_clist")),
				      a_server);
	      gtk_clist_set_row_data_full
		      (GTK_CLIST (get_widget ("server_clist")), 
		       row,
		       g_strdup (GDM_STANDARD),
		       (GDestroyNotify) g_free);
	      number_of_servers = 1;
      }

    prefix = g_strdup_printf ("=%s=/", config_file);
    gnome_config_push_prefix (prefix);
    g_free (prefix);

    g_free (sessions_directory);
    sessions_directory = gnome_config_get_string (GDM_KEY_SESSDIR);
    
    /* Fill the widgets in GDM tab */
    gdm_entry_set("automatic_login", gnome_config_get_string (GDM_KEY_AUTOMATICLOGIN));
    gdm_entry_set("timed_login", gnome_config_get_string (GDM_KEY_TIMED_LOGIN));
    gdm_spin_set("timed_delay", gnome_config_get_int(GDM_KEY_TIMED_LOGIN_DELAY));
    gdm_entry_set("chooser_binary", gnome_config_get_string (GDM_KEY_CHOOSER));
    gdm_entry_set("config_binary", gnome_config_get_string (GDM_KEY_CONFIGURATOR));
    gdm_entry_set("greeter_binary", gnome_config_get_string (GDM_KEY_GREETER));
    gdm_entry_set("halt_command", gnome_config_get_string (GDM_KEY_HALT));
    gdm_entry_set("reboot_command", gnome_config_get_string (GDM_KEY_REBOOT));
    gdm_entry_set("suspend_command", gnome_config_get_string (GDM_KEY_SUSPEND));
    
    gdm_entry_set("init_dir", gnome_config_get_string (GDM_KEY_INITDIR));
    gdm_entry_set("log_dir", gnome_config_get_string (GDM_KEY_LOGDIR));
    gdm_entry_set("session_dir", gnome_config_get_string (GDM_KEY_SESSDIR));
    gdm_entry_set("pre_session_dir", gnome_config_get_string (GDM_KEY_PRESESS));
    gdm_entry_set("post_session_dir", gnome_config_get_string (GDM_KEY_POSTSESS));
    standard_x_server = gnome_config_get_string (GDM_KEY_STANDARD_XSERVER);
    gdm_entry_set("standard_x_server", standard_x_server);
    gdm_entry_set("xnest_server", gnome_config_get_string (GDM_KEY_XNEST));
    gdm_spin_set("flexible_servers", gnome_config_get_int(GDM_KEY_FLEXIBLE_XSERVERS));
    gdm_entry_set("failsafe_x_server", gnome_config_get_string (GDM_KEY_FAILSAFE_XSERVER));
    gdm_entry_set("x_keeps_crashing", gnome_config_get_string (GDM_KEY_XKEEPSCRASHING));
    gdm_toggle_set("always_restart_server", gnome_config_get_bool (GDM_KEY_ALWAYSRESTARTSERVER));

    gdm_entry_set("pid_file", gnome_config_get_string (GDM_KEY_PIDFILE));
    gdm_entry_set("gnome_default_session", gnome_config_get_string (GDM_KEY_GNOMEDEFAULTSESSION));
    gdm_entry_set("default_path", gnome_config_get_string (GDM_KEY_PATH));
    gdm_entry_set("root_path", gnome_config_get_string (GDM_KEY_ROOTPATH));
 

    /* Fill the widgets in Security tab */
    gdm_toggle_set("allow_root", gnome_config_get_bool(GDM_KEY_ALLOWROOT));
    gdm_toggle_set("allow_remote_root", gnome_config_get_bool(GDM_KEY_ALLOWREMOTEROOT));
    gdm_toggle_set("allow_remote_auto_login", gnome_config_get_bool(GDM_KEY_ALLOWREMOTEAUTOLOGIN));
    gdm_toggle_set("kill_init_clients", gnome_config_get_bool(GDM_KEY_KILLIC));
    gdm_radio_set ("relax_perms", gnome_config_get_int(GDM_KEY_RELAXPERM), 2);

    gdm_entry_set("gdm_runs_as_user", gnome_config_get_string (GDM_KEY_USER));
    gdm_entry_set("gdm_runs_as_group", gnome_config_get_string (GDM_KEY_GROUP));
    gdm_entry_set("user_auth_dir", gnome_config_get_string (GDM_KEY_UAUTHDIR));
    gdm_entry_set("user_auth_fb_dir", gnome_config_get_string (GDM_KEY_UAUTHFB));
    gdm_entry_set("user_auth_file", gnome_config_get_string (GDM_KEY_UAUTHFILE));

    gdm_spin_set("retry_delay", gnome_config_get_int(GDM_KEY_RETRYDELAY));
    gdm_spin_set("max_user_length", gnome_config_get_int(GDM_KEY_MAXFILE));
    gdm_spin_set("max_session_length", gnome_config_get_int(GDM_KEY_SESSIONMAXFILE));
    

    /* Fill the widgets in the XDMCP tab */
    /* enable toggle is in parse_remaining() */
    gdm_toggle_set("honour_indirect", gnome_config_get_bool(GDM_KEY_INDIRECT));
    gdm_spin_set("net_port", gnome_config_get_int(GDM_KEY_UDPPORT));
    gdm_spin_set("net_requests", gnome_config_get_int(GDM_KEY_MAXPEND));
    gdm_spin_set("net_indirect_requests", gnome_config_get_int(GDM_KEY_MAXINDIR));
    gdm_spin_set("max_sessions", gnome_config_get_int(GDM_KEY_MAXSESS));
    gdm_spin_set("max_wait_time", gnome_config_get_int(GDM_KEY_MAXWAIT));
    gdm_spin_set("max_indirect_wait_time", gnome_config_get_int(GDM_KEY_MAXINDWAIT));
    gdm_spin_set("ping_interval", gnome_config_get_int(GDM_KEY_PINGINTERVAL));
    gdm_spin_set("displays_per_host", gnome_config_get_int(GDM_KEY_DISPERHOST));
    gdm_entry_set("xdmcp_willing_entry", gnome_config_get_string (GDM_KEY_WILLING));
    
    /* Fill the widgets in Sessions tab */
    gdm_toggle_set ("gnome_chooser_session", gnome_config_get_bool (GDM_KEY_SHOW_GNOME_CHOOSER));
    gdm_toggle_set ("gnome_failsafe_session", gnome_config_get_bool (GDM_KEY_SHOW_GNOME_FAILSAFE));
    gdm_toggle_set ("xterm_failsafe_session", gnome_config_get_bool (GDM_KEY_SHOW_XTERM_FAILSAFE));
    
    /* Fill the widgets in User Interface tab */
    gdm_entry_set("gtkrc_file", gnome_config_get_string (GDM_KEY_GTKRC));
    gdm_entry_set("logo_file", gnome_config_get_string (GDM_KEY_LOGO));
    gdm_icon_set("gdm_icon", gnome_config_get_string (GDM_KEY_ICON));
    
    gdm_toggle_set("show_system", gnome_config_get_bool (GDM_KEY_SYSMENU));
    gdm_toggle_set("run_configurator", gnome_config_get_bool (GDM_KEY_CONFIG_AVAILABLE));
    gdm_toggle_set("quiver", gnome_config_get_bool (GDM_KEY_QUIVER));
    gdm_toggle_set("title_bar", gnome_config_get_bool(GDM_KEY_TITLE_BAR));
    gdm_toggle_set ("lock_position", gnome_config_get_bool (GDM_KEY_LOCK_POSITION));
    gdm_toggle_set ("set_position", gnome_config_get_bool (GDM_KEY_SET_POSITION));
    gdm_spin_set ("position_x", gnome_config_get_int (GDM_KEY_POSITIONX));
    gdm_spin_set ("position_y", gnome_config_get_int (GDM_KEY_POSITIONY));
    gdm_spin_set ("xinerama_screen", gnome_config_get_int (GDM_KEY_XINERAMASCREEN));
    gdm_toggle_set("use_24_clock", gnome_config_get_bool (GDM_KEY_USE_24_CLOCK));
    
    gdm_entry_set("exclude_users", gnome_config_get_string (GDM_KEY_EXCLUDE));
    /* font picker is in parse_remaining() */
    gdm_entry_set("welcome_message", gnome_config_get_string (GDM_KEY_WELCOME));
    

    /* Fill the widgets in Background tab */
    gdm_entry_set ("background_program", gnome_config_get_string (GDM_KEY_BACKGROUNDPROG));
    gdm_entry_set ("background_image", gnome_config_get_string (GDM_KEY_BACKGROUNDIMAGE));
    gdm_color_set ("background_color", gnome_config_get_string (GDM_KEY_BACKGROUNDCOLOR));
    gdm_toggle_set ("background_scale", gnome_config_get_bool (GDM_KEY_BACKGROUNDSCALETOFIT));
    gdm_toggle_set ("remote_only_color", gnome_config_get_bool (GDM_KEY_BACKGROUNDREMOTEONLYCOLOR));

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
    gdm_entry_set("chooser_hosts", gnome_config_get_string (GDM_KEY_HOSTS));
    gdm_toggle_set("chooser_broadcast", gnome_config_get_bool (GDM_KEY_BROADCAST));

    gdm_toggle_set("enable_debug", gnome_config_get_bool(GDM_KEY_DEBUG));

    /* Read directory entries in session dir */
    if (sessions_directory != NULL)
	    sessdir = opendir (sessions_directory);
    else
	    sessdir = NULL;

    if (sessdir != NULL)
	    dent = readdir (sessdir);
    else
	    dent = NULL;

    while (dent != NULL) {
	gchar *s;

	/* Ignore backups and rpmsave files */
	if ((strstr (dent->d_name, "~")) ||
	    (strstr (dent->d_name, ".rpmsave")) ||
	    (strstr (dent->d_name, ".rpmorig")) ||
	    (strstr (dent->d_name, ".dpkg-old")) ||
	    (strstr (dent->d_name, ".deleted")) ||
	    (strstr (dent->d_name, ".desc")) /* description file */ ||
	    (strstr (dent->d_name, ".orig"))) {
	    dent = readdir (sessdir);
	    continue;
	}

	s = g_strconcat (sessions_directory,
			 "/", dent->d_name, NULL);
	lstat (s, &statbuf);

        /* If default session link exists, find out what it points to */
	if (S_ISLNK (statbuf.st_mode) &&
	    ve_strcasecmp_no_locale (dent->d_name, "default") == 0) 
	 {
	    gchar t[_POSIX_PATH_MAX];
	    
	    linklen = readlink (s, t, _POSIX_PATH_MAX);
	    t[linklen] = 0;
	    
	   /* Prevent sym links of default to default from screwing
	    * things up.
	    */
	   if (strcmp (g_basename (s), t) != 0) {
		   /* printf ("recording %s as default session.\n", t); */
		   g_free (default_session_link_name);
		   default_session_link_name = g_strdup (dent->d_name);
		   g_free (default_session_name);
		   default_session_name = g_strdup(t);
	   }
	 }

	/* If session script is readable/executable add it to the list */
	if (S_ISREG (statbuf.st_mode)) {

	    if ((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
		(statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
		(statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) {
	       gchar *row[1];
	       int rowNum;
	       GdmConfigSession *this_session;
	       FILE *script_file;
	       
	       this_session = g_new0 (GdmConfigSession, 1);
	       this_session->name = g_strdup(dent->d_name);
	       script_file = fopen(s, "r");
	       if (script_file == NULL) {
		       gnome_error_dialog_parented
			       (_("Error reading session script!"),
				GTK_WINDOW (GDMconfigurator));
		       this_session->script_contents =
			       g_strdup (_("Error reading this session script"));
		       this_session->changable = FALSE;
	       } else {
		       GString *str = g_string_new (NULL);
		       gchar buffer[BUFSIZ];

		       while (fgets (buffer, sizeof (buffer),
				     script_file) != NULL) {
			       g_string_append (str, buffer);
		       }
		       this_session->script_contents = str->str;
		       g_string_free (str, FALSE);
		       /* printf ("got script contents:\n%s.\n", this_session->script_contents); */
		       this_session->changable = TRUE;
		       fclose (script_file);
	       }
	       this_session->changed = FALSE;
	       this_session->is_default = FALSE;

	       row[0] = this_session->name;
	       
	       rowNum = gtk_clist_append (GTK_CLIST (get_widget("sessions_clist")),
					  row);
	       gtk_clist_set_row_data (GTK_CLIST (get_widget ("sessions_clist")),
				       rowNum,
				       this_session);

	    } else  {
	        /* FIXME: this should have an error dialog I suppose */
		syslog (LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
			sessions_directory, dent->d_name);
	    }
	}

	dent = readdir (sessdir);
	g_free (s);
    }

   if (default_session_name) {
      GdkColor col;

      for (i=0; i< GTK_CLIST(get_widget ("sessions_clist"))->rows; i++) {
	 GdmConfigSession *data = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
								 i);
	 g_assert (data != NULL);
	 if (strcmp(default_session_name, data->name) == 0) {
	    
	    /*printf ("coloring session %s.\n", data->name);*/
	    gdk_color_parse ("#d6e8ff", &col);
	    gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
				      i, &col);
	    data->is_default = TRUE;
	    current_default_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							      i);;
	    default_session_row = i;
	    /* this is so that we can find out when the default changed */
	    old_current_default_session = current_default_session;
	 }
      }
   } else {
      /* FIXME: failsafe "no default session" stuff. */
	   /* gdmlogin usually tries using Gnome and then Default,
	    * however if it says last then gdm daemon checks for a whole
	    * bunch of filenames.  it should be pretty safe to not do
	    * anything here */
   }

    gnome_config_pop_prefix();

    gtk_clist_set_column_auto_resize
	    (GTK_CLIST (get_widget ("server_def_clist")), 0, TRUE);
    gtk_clist_set_column_auto_resize
	    (GTK_CLIST (get_widget ("server_def_clist")), 1, TRUE);
    gtk_clist_set_column_auto_resize
	    (GTK_CLIST (get_widget ("server_clist")), 0, TRUE);
    gtk_clist_set_column_auto_resize
	    (GTK_CLIST (get_widget ("server_clist")), 1, TRUE);

    got_standard_server = FALSE;
    got_any_servers = FALSE;

    /* Find server definitions */
    iter = gnome_config_init_iterator_sections ("=" GDM_CONFIG_FILE "=/");
    iter = gnome_config_iterator_next (iter, &key, NULL);
    
    while (iter) {
	    if (strncmp (key, "server-", strlen ("server-")) == 0) {
		    char *section;
		    char *id;
		    char *text[3];
		    int row;

		    section = g_strdup_printf ("=" GDM_CONFIG_FILE "=/%s/",
					       key);
		    gnome_config_push_prefix (section);

		    id = g_strdup (key + strlen ("server-"));

		    text[0] = gnome_config_get_string (GDM_KEY_SERVER_NAME);
		    if (text[0] == NULL)
			    text[0] = g_strdup (id);
		    text[1] = gnome_config_get_string (GDM_KEY_SERVER_COMMAND);
		    if (text[1] == NULL)
			    text[1] = g_strdup (standard_x_server);
		    if (text[1] == NULL)
			    text[1] = g_strdup ("/usr/bin/X11/X");

		    if (gnome_config_get_bool (GDM_KEY_SERVER_FLEXIBLE))
			    text[2] = g_strdup (_("Yes"));
		    else
			    text[2] = g_strdup (_("No"));

		    g_free (section);
		    gnome_config_pop_prefix ();

		    row = gtk_clist_append
			    (GTK_CLIST (get_widget ("server_def_clist")), text);
		    gtk_clist_set_row_data_full
			    (GTK_CLIST (get_widget ("server_def_clist")), 
			     row,
			     id,
			     (GDestroyNotify) g_free);

		    g_free (text[0]);
		    g_free (text[1]);
		    g_free (text[2]);

		    got_any_servers = TRUE;
		    if (strcmp (id, GDM_STANDARD) == 0) {
			    GdkColor col;
			    gdk_color_parse ("#d6e8ff", &col);
			    gtk_clist_set_background
				    (GTK_CLIST (get_widget("server_def_clist")),
				     row, &col);
			    got_standard_server = TRUE;
		    }
	    }

	    g_free (key);

	    iter = gnome_config_iterator_next (iter, &key, NULL);
    }

    if ( ! got_any_servers ||
	 ! got_standard_server) {
	    char *text[3];
	    int row;
	    GdkColor col;

	    text[0] = g_strdup (_("Standard server"));
	    text[1] = g_strdup (standard_x_server);
	    if (text[1] == NULL)
		    text[1] = g_strdup ("/usr/bin/X11/X");

	    text[2] = g_strdup (_("Yes"));

	    row = gtk_clist_append
		    (GTK_CLIST (get_widget ("server_def_clist")), text);
	    gtk_clist_set_row_data_full
		    (GTK_CLIST (get_widget ("server_def_clist")), 
		     row,
		     g_strdup (GDM_STANDARD),
		     (GDestroyNotify) g_free);

	    gdk_color_parse ("#d6e8ff", &col);
	    gtk_clist_set_background
		    (GTK_CLIST (get_widget("server_def_clist")), row, &col);

	    g_free (text[0]);
	    g_free (text[1]);
	    g_free (text[2]);
    }

    /* Fill the widgets in Servers tab */
    biggest_server = 0;
    iter=gnome_config_init_iterator("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter=gnome_config_iterator_next (iter, &key, &value);
    
    while (iter != NULL) {
	    if (isdigit (*key)) {
		    int svr = atoi (key);

		    if (svr > biggest_server)
			    biggest_server = svr;
	    } else {
		    gnome_warning_dialog_parented(_("gdm_config_parse_most: Invalid server line in config file. Ignoring!"),
						  GTK_WINDOW(GDMconfigurator));
	    }
	    g_free (key);
	    g_free (value);
	    iter = gnome_config_iterator_next (iter, &key, &value);
    }
    
    /* Fill the widgets in Servers tab */
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");

    for (i = 0; i <= biggest_server; i++) {
	    char *a_server[3];
	    char *first;

	    key = g_strdup_printf ("%d", i);
	    value = gnome_config_get_string (key);

	    if (ve_string_empty (value)) {
		    g_free (value);
		    g_free (key);
		    continue;
	    }

	    /* Make new, in-order, key */
	    if (i != number_of_servers) {
		    g_free (key);
		    key = g_strdup_printf ("%d", number_of_servers);
	    }

	    first = ve_first_word (ve_sure_string (value));
	    a_server[0] = key;
	    if (first[0] == '/') {
		    a_server[1] = value;
		    a_server[2] = "";
		    gtk_clist_append
			    (GTK_CLIST (get_widget ("server_clist")),
			     a_server);
		    g_free (first);
	    } else {
		    int row;
		    char *rest = ve_rest (value);
		    char *name = get_server_name (first);
		    if (rest == NULL)
			    rest = g_strdup ("");
		    a_server[1] = name;
		    a_server[2] = rest;
		    row = gtk_clist_append
			    (GTK_CLIST (get_widget ("server_clist")),
			     a_server);
		    gtk_clist_set_row_data_full
			    (GTK_CLIST (get_widget ("server_clist")),
			     row,
			     first,
			     (GDestroyNotify) g_free);
		    g_free (name);
		    g_free (rest);
	    }
	    number_of_servers++;

	    g_free (key);
	    g_free (value);
    }
    gnome_config_pop_prefix ();

    /* FIXME: Could do something nicer with the 'exclude users' GUI sometime */

    g_free (standard_x_server);
}


void
gdm_config_parse_remaining (gboolean factory)
{
    char *prefix;
    const char *config_file;

    if (factory)
	    config_file = GDM_FACTORY_CONFIG_FILE;
    else
	    config_file = GDM_CONFIG_FILE;

    prefix = g_strdup_printf ("=%s=/", config_file);
    gnome_config_push_prefix (prefix);
    g_free (prefix);

    /* Ensure the XDMCP frame is the correct sensitivity */
    gdm_toggle_set("enable_xdmcp", gnome_config_get_bool(GDM_KEY_XDMCP));

    /* The background radios */
    gdm_radio_set ("background_type",
		   gnome_config_get_int (GDM_KEY_BACKGROUNDTYPE), 2);
    
    /* This should make the font picker to update itself. But for some
     * strange reason, it doesn't.
     */
    gdm_font_set("font_picker", gnome_config_get_string (GDM_KEY_FONT));

    /* Face browser stuff */
    gdm_toggle_set("enable_face_browser", gnome_config_get_bool (GDM_KEY_BROWSER));

    /* Login stuff */
    {
	    char *login = gnome_config_get_string (GDM_KEY_AUTOMATICLOGIN);
	    if (ve_string_empty (login)) {
		    gdm_toggle_set ("enable_automatic_login", FALSE);
	    } else {
		    gdm_toggle_set ("enable_automatic_login", gnome_config_get_bool (GDM_KEY_AUTOMATICLOGIN_ENABLE));
	    }
	    g_free (login);

	    login = gnome_config_get_string (GDM_KEY_TIMED_LOGIN);
	    if (ve_string_empty (login)) {
		    gdm_toggle_set ("enable_timed_login", FALSE);
	    } else {
		    gdm_toggle_set ("enable_timed_login", gnome_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE));
	    }
	    g_free (login);
    }
    
    gnome_config_pop_prefix();
}

static gboolean
run_query (const gchar *msg)
{
	GtkWidget *req;

	req = gnome_message_box_new (msg,
				     GNOME_MESSAGE_BOX_QUESTION,
				     GNOME_STOCK_BUTTON_YES,
				     GNOME_STOCK_BUTTON_NO,
				     NULL);

	gtk_window_set_modal (GTK_WINDOW (req), TRUE);
	gnome_dialog_set_parent (GNOME_DIALOG (req),
				 GTK_WINDOW (GDMconfigurator));
	return (!gnome_dialog_run (GNOME_DIALOG(req)));
}

static int
run_3_query (const gchar *msg, const char *b1, const char *b2, const char *b3)
{
	GtkWidget *req;

	req = gnome_message_box_new (msg,
				     GNOME_MESSAGE_BOX_QUESTION,
				     b1,
				     b2,
				     b3,
				     NULL);

	gtk_window_set_modal (GTK_WINDOW (req), TRUE);
	gnome_dialog_set_parent (GNOME_DIALOG (req),
				 GTK_WINDOW (GDMconfigurator));
	return gnome_dialog_run (GNOME_DIALOG(req));
}

static void
run_warn_reset_dialog (void)
{
	GtkWidget *w;
	char *pidfile;
	long pid = 0;
	FILE *fp = NULL;

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	pidfile = gnome_config_get_string (GDM_KEY_PIDFILE);
	gnome_config_pop_prefix ();

	if (pidfile != NULL)
		fp = fopen (pidfile, "r");
	if (fp != NULL) {
		fscanf (fp, "%ld", &pid);
		fclose (fp);
	}

	g_free (pidfile);

	if (pid > 1 &&
	    kill (pid, 0) == 0) {
		int reply = run_3_query
			(_("The applied settings cannot take effect until gdm\n"
			   "is restarted or your computer is rebooted.\n"
			   "You can restart GDM when all sessions are\n"
			   "closed (when all users log out) or you can\n"
			   "restart GDM now (which will kill all current\n"
			   "sessions)"),
			 _("Restart after logout"),
			 _("Restart now"),
			 GNOME_STOCK_BUTTON_CANCEL);

		if (reply == 0) {
			kill (pid, SIGUSR1);
		} else if (reply == 1) {
			/* this level of paranoia isn't needed in case of
			 * running from gdm */
			if (g_getenv ("RUNNING_UNDER_GDM") != NULL ||
			    run_query
			    (_("Are you sure you wish to restart GDM\n"
			       "now and lose any unsaved data?"))) {
				kill (pid, SIGHUP);
				/* now what happens :) */
			}
		}
	} else {
		w = gnome_ok_dialog_parented
		    (_("The greeter settings will take effect the next time\n"
		       "it is displayed.  The rest of the settings will not\n"
		       "take effect until gdm is restarted or the computer is\n"
		       "rebooted"),
		     GTK_WINDOW (GDMconfigurator));
		gtk_window_set_modal (GTK_WINDOW (w), TRUE);
		gnome_dialog_run_and_close (GNOME_DIALOG (w));
	}
	/* Don't apply identical settings again. */
	gtk_widget_set_sensitive(GTK_WIDGET(get_widget("apply_button")), FALSE);
}



static gboolean
write_config (void)
{
    int i = -1;
    GList *tmp = NULL;
    GString *errors = NULL;
    GtkCList *clist;

    if (number_of_servers == 0 &&
	! run_query (_("You have not defined any local servers.\n"
		       "Usually this is not a good idea unless you\n"
		       "are sure you do not want users to be able to\n"
		       "log in with the graphical interface on the\n"
		       "local console and only use the xdmcp service.\n\n"
		       "Are you sure you wish to apply these settings?"))) {
	    return FALSE;
    }
   
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
    
    /* Write out the widget contents of the GDM tab */
    gdm_toggle_write("enable_automatic_login", GDM_KEY_AUTOMATICLOGIN_ENABLE);
    gdm_entry_write("automatic_login", GDM_KEY_AUTOMATICLOGIN);
    gdm_toggle_write("enable_timed_login", GDM_KEY_TIMED_LOGIN_ENABLE);
    gdm_entry_write("timed_login", GDM_KEY_TIMED_LOGIN);
    gdm_spin_write("timed_delay", GDM_KEY_TIMED_LOGIN_DELAY);
    gdm_entry_write("chooser_binary", GDM_KEY_CHOOSER);
    gdm_entry_write("greeter_binary", GDM_KEY_GREETER);
    gdm_entry_write("config_binary", GDM_KEY_CONFIGURATOR);
    gdm_entry_write("halt_command", GDM_KEY_HALT);
    gdm_entry_write("reboot_command", GDM_KEY_REBOOT);
    gdm_entry_write("suspend_command", GDM_KEY_SUSPEND);
    
    /* misc */
    gdm_entry_write("init_dir", GDM_KEY_INITDIR);
    gdm_entry_write("log_dir", GDM_KEY_LOGDIR);
    gdm_entry_write("session_dir", GDM_KEY_SESSDIR);
    gdm_entry_write("pre_session_dir", GDM_KEY_PRESESS);
    gdm_entry_write("post_session_dir", GDM_KEY_POSTSESS);
    gdm_entry_write("failsafe_x_server", GDM_KEY_FAILSAFE_XSERVER);
    gdm_entry_write("x_keeps_crashing", GDM_KEY_XKEEPSCRASHING);
    gdm_toggle_write("always_restart_server", GDM_KEY_ALWAYSRESTARTSERVER);

    gdm_entry_write("pid_file", GDM_KEY_PIDFILE);
    gdm_entry_write("gnome_default_session", GDM_KEY_GNOMEDEFAULTSESSION);
    gdm_entry_write("default_path", GDM_KEY_PATH);
    gdm_entry_write("root_path", GDM_KEY_ROOTPATH);

    /* Write out the widget contents of the Security tab */
    gdm_toggle_write("allow_root", GDM_KEY_ALLOWROOT);
    gdm_toggle_write("allow_remote_root", GDM_KEY_ALLOWREMOTEROOT);
    gdm_toggle_write("allow_remote_auto_login", GDM_KEY_ALLOWREMOTEAUTOLOGIN);
    gdm_toggle_write("kill_init_clients", GDM_KEY_KILLIC);
    gdm_radio_write ("relax_perms", GDM_KEY_RELAXPERM, 2);

    gdm_entry_write("gdm_runs_as_user", GDM_KEY_USER);
    gdm_entry_write("gdm_runs_as_group", GDM_KEY_GROUP);
    gdm_entry_write("user_auth_dir", GDM_KEY_UAUTHDIR);
    gdm_entry_write("user_auth_fb_dir", GDM_KEY_UAUTHFB);
    gdm_entry_write("user_auth_file", GDM_KEY_UAUTHFILE);

    gdm_spin_write("retry_delay", GDM_KEY_RETRYDELAY);
    gdm_spin_write("max_user_length", GDM_KEY_MAXFILE);
    gdm_spin_write("max_session_length", GDM_KEY_SESSIONMAXFILE);
    

    /* Write out the widget contents of the XDMCP tab */
    gdm_toggle_write("enable_xdmcp", GDM_KEY_XDMCP);
    gdm_toggle_write("honour_indirect", GDM_KEY_INDIRECT);
    gdm_spin_write("net_port", GDM_KEY_UDPPORT);
    gdm_spin_write("net_requests", GDM_KEY_MAXPEND);
    gdm_spin_write("net_indirect_requests", GDM_KEY_MAXINDIR);
    gdm_spin_write("max_sessions", GDM_KEY_MAXSESS);
    gdm_spin_write("max_wait_time", GDM_KEY_MAXWAIT);
    gdm_spin_write("max_indirect_wait_time", GDM_KEY_MAXINDWAIT);
    gdm_spin_write("ping_interval", GDM_KEY_PINGINTERVAL);
    gdm_spin_write("displays_per_host", GDM_KEY_DISPERHOST);
    gdm_entry_write("xdmcp_willing_entry", GDM_KEY_WILLING);
    
    /* write out the widget contents of the Sessions tab */
    gdm_toggle_write ("gnome_chooser_session", GDM_KEY_SHOW_GNOME_CHOOSER);
    gdm_toggle_write ("gnome_failsafe_session", GDM_KEY_SHOW_GNOME_FAILSAFE);
    gdm_toggle_write ("xterm_failsafe_session", GDM_KEY_SHOW_XTERM_FAILSAFE);
    
    /* Write out the widget contents of the User Interface tab */
    gdm_entry_write("gtkrc_file", GDM_KEY_GTKRC);
    gdm_entry_write("logo_file", GDM_KEY_LOGO);
    gdm_icon_write("gdm_icon", GDM_KEY_ICON);
    
    gdm_toggle_write("show_system", GDM_KEY_SYSMENU);
    gdm_toggle_write("run_configurator", GDM_KEY_CONFIG_AVAILABLE);
    gdm_toggle_write("quiver", GDM_KEY_QUIVER);
    gdm_toggle_write("title_bar", GDM_KEY_TITLE_BAR);
    gdm_toggle_write("lock_position", GDM_KEY_LOCK_POSITION);
    gdm_toggle_write("set_position", GDM_KEY_SET_POSITION);
    gdm_spin_write("position_x", GDM_KEY_POSITIONX);
    gdm_spin_write("position_y", GDM_KEY_POSITIONY);
    gdm_spin_write("xinerama_screen", GDM_KEY_XINERAMASCREEN);
    gdm_toggle_write("use_24_clock", GDM_KEY_USE_24_CLOCK);
    
    gdm_entry_write("exclude_users", GDM_KEY_EXCLUDE);
    gdm_font_write("font_picker", GDM_KEY_FONT);
    gdm_entry_write("welcome_message", GDM_KEY_WELCOME);
    
    /* Write out the widget contents of the Background tab */
    gdm_radio_write ("background_type", GDM_KEY_BACKGROUNDTYPE, 2);
    gdm_entry_write("background_image", GDM_KEY_BACKGROUNDIMAGE);
    gdm_color_write("background_color", GDM_KEY_BACKGROUNDCOLOR);
    gdm_entry_write("background_program", GDM_KEY_BACKGROUNDPROG);
    gdm_toggle_write("background_scale", GDM_KEY_BACKGROUNDSCALETOFIT);
    gdm_toggle_write("remote_only_color", GDM_KEY_BACKGROUNDREMOTEONLYCOLOR);

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
    gdm_entry_write("chooser_hosts", GDM_KEY_HOSTS);
    gdm_toggle_write("chooser_broadcast", GDM_KEY_BROADCAST);

    gdm_toggle_write("enable_debug", GDM_KEY_DEBUG);

    gnome_config_pop_prefix();
    
    gnome_config_clean_section("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    gnome_config_push_prefix("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    
    /* Write out the widget contents of the Servers tab */

    clist = GTK_CLIST (get_widget ("server_clist"));
    for (i = 0; i < number_of_servers; i++) {
	    char *key;
	    char *val;
	    char *extra_args = NULL;
	    char *current_server =
		    gtk_clist_get_row_data (clist, i);

	    if (ve_string_empty (current_server))
		    continue;

	    gtk_clist_get_text (clist, i, 2, &extra_args);

	    if (ve_string_empty (extra_args))
		    val = g_strdup (current_server);
	    else
		    val = g_strdup_printf ("%s %s", current_server,
					   extra_args);


	    key = g_strdup_printf ("%d", i);
	    gnome_config_set_string (key, val);
	    g_free (key);
	    g_free (val);
    }

    clist = GTK_CLIST (get_widget ("server_def_clist"));
    /* XXXXXXXXXXXXXXXXXX */
    for (i = 0; i < clist->rows; i++) {
	    char *section;
	    char *name = NULL;
	    char *cmd = NULL;
	    char *flexible = NULL;
	    char *id = gtk_clist_get_row_data (clist, i);

	    if (ve_string_empty (id))
		    continue;

	    gtk_clist_get_text (clist, i, 0, &name);
	    gtk_clist_get_text (clist, i, 1, &cmd);
	    gtk_clist_get_text (clist, i, 2, &flexible);

	    section = g_strdup_printf ("=" GDM_CONFIG_FILE "=/server-%s/", id);
	    gnome_config_push_prefix (section);
	    g_free (section);

	    gnome_config_set_string (GDM_KEY_SERVER_NAME, ve_sure_string (name));
	    gnome_config_set_string (GDM_KEY_SERVER_COMMAND, ve_sure_string (cmd));
	    if (strcmp (ve_sure_string (flexible), _("Yes")) == 0)
		    gnome_config_set_bool (GDM_KEY_SERVER_FLEXIBLE, TRUE);
	    else
		    gnome_config_set_bool (GDM_KEY_SERVER_FLEXIBLE, FALSE);

	    gnome_config_pop_prefix ();
    }

   /* It would be nice to be able to do some paranoid sanity checking on
    * more of this stuff.
    */

   /* Remove any deleted session files */
   for (tmp = deleted_sessions_list; tmp != NULL; tmp = tmp->next)
     {
	char *file_basename = (char *)tmp->data;
	char *full_file, *full_deleted_file;

	/* previously deleted file */
	if (tmp->data == NULL)
		continue;

	full_file = g_strconcat (sessions_directory, "/", 
				 file_basename, NULL);
	full_deleted_file = g_strconcat (sessions_directory, "/", 
					 file_basename, ".deleted",
					 NULL);
	/*printf ("deleting: %s.\n", full_file);*/
	/* remove old backup file */
	unlink (full_deleted_file);
	errno = 0;
	/* instead of just deleting, move to .deleted */
	if (rename (full_file, full_deleted_file) < 0) {
		   if (errors == NULL)
			   errors = g_string_new (NULL);
		   g_string_sprintfa (errors,
				      _("\nCould not delete session %s\n"
					"   Error: %s"),
				      file_basename,
				      g_strerror (errno));
	} else {
		/* all went well */
		/* we don't remove from the list, just null the element,
		 * removing fromt he list would screw up our current
		 * traversing of the list */
		tmp->data = NULL;
		g_free (file_basename);
	}
	g_free (full_file);
	g_free (full_deleted_file);
     }


   /* first remove old renamed files */
   for (i=0; i<GTK_CLIST(get_widget ("sessions_clist"))->rows; i++)
     {
	GdmConfigSession *session;
	session = gtk_clist_get_row_data (GTK_CLIST(get_widget ("sessions_clist")),
					  i);
	g_assert (session != NULL);

	/* first delete old renamed files and mark them changed */
	if (session->renamed) {
		char *old;
		old = g_strconcat (sessions_directory, "/",
				   session->old_name, NULL);
		/* printf ("removing %s\n", session->old_name); */
		errno = 0;
		if (unlink (old) < 0) {
			if (errors == NULL)
				errors = g_string_new (NULL);
			g_string_sprintfa (errors,
					   _("\nCould not remove session %s\n"
					     "   Error: %s"),
					   session->old_name,
					   g_strerror (errno));
		} else {
			/* all went well */
			session->renamed = FALSE;
			g_free (session->old_name);
			session->old_name = NULL;
			/* mark as changed since we want to write out
			 * this file to the new name */
			session->changed = TRUE;
		}
		g_free (old);
	}
     }

   /* Now write out files that were changed (or renamed above) */
   for (i=0; i<GTK_CLIST(get_widget ("sessions_clist"))->rows; i++) {
	GdmConfigSession *session;
	session = gtk_clist_get_row_data (GTK_CLIST(get_widget ("sessions_clist")),
					  i);
	g_assert (session != NULL);

	/* Set new contents of a changed session file */
	if (session->changed) {
	   char *filename;
	   FILE *fp;
	   
	   /* if renaming failed */
	   if (session->old_name != NULL)
		   filename = g_strconcat (sessions_directory, "/",
					   session->old_name, NULL);
	   else
		   filename = g_strconcat (sessions_directory, "/",
					   session->name, NULL);
	   /* printf ("writing changes to: %s.\n", filename); */
	   errno = 0;
	   fp = fopen(filename, "w");
	   if (fp == NULL) {
		   if (errors == NULL)
			   errors = g_string_new (NULL);
		   g_string_sprintfa (errors,
				      _("\nCould not write session %s\n"
					"   Error: %s"),
				      session->name,
				      g_strerror (errno));
		   continue;
	   }
	   errno = 0;
	   if (fputs (session->script_contents, fp) == EOF) {
		   if (errors == NULL)
			   errors = g_string_new (NULL);
		   g_string_sprintfa (errors,
				      _("\nCould not write contents to session %s\n"
					"   Error: %s"),
				      session->name,
				      g_strerror (errno));
		   fclose (fp);
		   chmod (filename, 0755);
		   continue;
	   }
	   fclose (fp);
	   chmod (filename, 0755);
	   /* all went well */
	   session->changed = FALSE;
	}
   }

   if (old_current_default_session != current_default_session) {
	if (default_session_link_name != NULL) {
		char *link_name = g_strconcat (sessions_directory, "/",
					       default_session_link_name,
					       NULL);
		errno = 0;
		if (unlink (link_name) < 0) {
			if (errors == NULL)
				errors = g_string_new (NULL);
			g_string_sprintfa (errors,
					   _("\nCould not unlink old default session\n"
					     "   Error: %s"),
					   g_strerror (errno));
		} else {
			/* all went well */
			g_free (default_session_link_name);
			default_session_link_name = NULL;
			old_current_default_session = NULL;
		}
		g_free (link_name);
	}

	/* if no session linked now */
	if (default_session_link_name == NULL &&
	    current_default_session != NULL) {
		char *link_name = g_strconcat (sessions_directory, "/default",
					       NULL);
		/* eek! */
		if (g_file_exists (link_name)) {
			g_free (link_name);
			link_name = g_strconcat (sessions_directory,
						 "/Default", NULL);
			/* double eek! */
			if (g_file_exists (link_name)) {
				g_free (link_name);
				link_name = g_strconcat (sessions_directory,
							 "/DEFAULT", NULL);
				/* tripple eek !!! */
				if (g_file_exists (link_name)) {
					g_free (link_name);
					link_name = NULL;
				}
			}
		}

		if (link_name == NULL) {
			if (errors == NULL)
				errors = g_string_new (NULL);
			g_string_sprintfa (errors,
					   _("\nCould not find a suitable name "
					     "for the default session link"));
		} else {
			errno = 0;
			if (symlink (current_default_session->name,
				     link_name) < 0) {
				if (errors == NULL)
					errors = g_string_new (NULL);
				g_string_sprintfa (errors,
						   _("\nCould not link new default session\n"
						     "   Error: %s"),
						   g_strerror (errno));
			} else {
				/* all went well */
				old_current_default_session =
					current_default_session;
				default_session_link_name =
					g_strdup (g_basename (link_name));
			}
		}
	}
   }

    gnome_config_pop_prefix();
    gnome_config_sync();

    if (errors != NULL) {
	    GtkWidget *dlg;
	    g_string_prepend (errors,
			      _("There were errors writing changes to the "
				"session files.\n"
				"The configuration may not be completely "
				"saved.\n"));
	    dlg = gnome_error_dialog_parented (errors->str,
					       GTK_WINDOW(GDMconfigurator));
	    gtk_window_set_modal (GTK_WINDOW (dlg), TRUE);
	    gnome_dialog_run_and_close (GNOME_DIALOG (dlg));
	    g_string_free (errors, TRUE);
    }

    run_warn_reset_dialog ();

    return TRUE;
}

void
revert_settings_to_file_state (GtkMenuItem *menu_item,
			       gpointer user_data)
{
	if (run_query(_("This will destroy any changes made in this session.\n"
			"Are you sure you want to do this?"))) {
		gdm_config_parse_most(FALSE);
		gdm_config_parse_remaining(FALSE);
	}
}

void
revert_to_factory_settings (GtkMenuItem *menu_item,
			    gpointer user_data)
{
	if (run_query (_("This will destroy any changes made in the configuration.\n"
			"Are you sure you want to do this?"))) {
		gdm_config_parse_most (TRUE);
		gdm_config_parse_remaining (TRUE);
	}
}

void
write_new_config_file (GtkButton *button, gpointer user_data)
{
	write_config ();
}

void
write_and_close (GtkButton *button,
		 gpointer user_data)
{
	if (write_config ())
		exit_configurator(NULL, NULL);
}

void
open_help_page (GtkButton *button,
		gpointer user_data)
{
	gchar *tmp;
	tmp = gnome_help_file_find_file ("gdmconfig", "index.html");
	if (tmp != NULL) {
		/* If we are running under gdm, try the help browser */
		if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
			char *argv[] = {
				"gnome-help-browser",
				tmp,
				NULL
			};
			gnome_execute_async (NULL, 2, argv);
		} else {
			gnome_help_goto (0, tmp);
		}
		g_free (tmp);
	}
}


gint
exit_configurator (GtkWidget *gnomedialog, gpointer user_data)
{
    gtk_main_quit();
    return 0;
}


void
can_apply_now                          (GtkEditable     *editable,
                                        gpointer         user_data)
{
	gtk_widget_set_sensitive(get_widget("apply_button"), TRUE);
}


void
change_xdmcp_sensitivity               (GtkButton       *button,
					gpointer         user_data)
{
	g_assert(button != NULL);
	g_assert(GTK_IS_TOGGLE_BUTTON(button));

	gtk_widget_set_sensitive(get_widget("xdmcp_frame"), 
				 GTK_TOGGLE_BUTTON(button)->active);
}

void
change_background_sensitivity_image (GtkButton *button,
                                     gpointer user_data)
{
	g_assert (button != NULL);
	g_assert (GTK_IS_TOGGLE_BUTTON (button));

	gtk_widget_set_sensitive (get_widget ("background_image_pixmap_entry"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("background_image_label"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("background_scale"), 
				  GTK_TOGGLE_BUTTON (button)->active);
}

void
change_background_sensitivity_none (GtkButton *button,
				    gpointer user_data)
{
	g_assert (button != NULL);
	g_assert (GTK_IS_TOGGLE_BUTTON (button));

	gtk_widget_set_sensitive (get_widget ("background_color"), 
				  ! GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("background_color_label"), 
				  ! GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("remote_only_color"), 
				  ! GTK_TOGGLE_BUTTON (button)->active);
}


void
set_face_sensitivity                   (GtkButton       *button,
                                        gpointer         user_data)
{
    gtk_widget_set_sensitive(get_widget("faces_table"),
			     GTK_TOGGLE_BUTTON(get_widget("enable_face_browser"))->active);
}

void
change_automatic_sensitivity (GtkButton *button,
			      gpointer user_data)
{
	g_assert (button != NULL);
	g_assert (GTK_IS_TOGGLE_BUTTON (button));

	gtk_widget_set_sensitive (get_widget ("automatic_login"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("automatic_login_label"), 
				  GTK_TOGGLE_BUTTON (button)->active);
}

void
change_timed_sensitivity (GtkButton *button,
			  gpointer user_data)
{
	g_assert (button != NULL);
	g_assert (GTK_IS_TOGGLE_BUTTON (button));

	gtk_widget_set_sensitive (get_widget ("timed_login"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("timed_login_label"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("timed_delay"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("timed_delay_label"), 
				  GTK_TOGGLE_BUTTON (button)->active);
}

static void
handle_server_def_edit (gboolean edit)
{
	GladeXML *xml;
	GtkWidget *dialog, *name, *command_line, *flexible, *standard;
	GtkCList *server_def_clist;
	char *current_name, *current_command, *current_flexible, *current_id;

	if (edit && selected_server_def_row < 0)
		return;

	server_def_clist = GTK_CLIST (get_widget ("server_def_clist"));

	xml = glade_helper_load ("gdmconfig.glade", "svr_def",
				 gnome_dialog_get_type (),
				 TRUE /* dump on destroy */);
	dialog = glade_helper_get (xml, "svr_def",
				   gnome_dialog_get_type ());
	name = glade_helper_get (xml, "svr_def_name",
				 gtk_entry_get_type ());
	command_line = glade_helper_get (xml, "svr_def_command_line",
					 gtk_entry_get_type ());
	flexible = glade_helper_get (xml, "svr_def_flexible",
				     gtk_toggle_button_get_type ());
	standard = glade_helper_get (xml, "svr_def_standard",
				     gtk_toggle_button_get_type ());

	if (edit) {
		gtk_clist_get_text (server_def_clist,
				    selected_server_def_row, 0,
				    &current_name);
		gtk_clist_get_text (server_def_clist,
				    selected_server_def_row, 1,
				    &current_command);
		gtk_clist_get_text (server_def_clist,
				    selected_server_def_row, 2,
				    &current_flexible);
		current_id = gtk_clist_get_row_data (server_def_clist,
						     selected_server_def_row);

		gtk_entry_set_text (GTK_ENTRY (name),
				    ve_sure_string (current_name));
		gtk_entry_set_text (GTK_ENTRY (command_line),
				    ve_sure_string (current_command));
		if (strcmp (_("Yes"), current_flexible) == 0)
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (flexible), TRUE);
		else
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (flexible), FALSE);
		if (current_id == NULL ||
		    strcmp (current_id, GDM_STANDARD) == 0) {
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (standard), TRUE);
		} else {
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (standard), FALSE);
		}
	} else {
		gtk_entry_set_text (GTK_ENTRY (name),
				    _("Standard server"));
		gtk_entry_set_text (GTK_ENTRY (command_line),
				    "/usr/bin/X11/X");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (flexible), TRUE);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (standard), TRUE);
	}

	gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
				 (GtkWindow *) GDMconfigurator);
	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	for (;;) {
		if (gnome_dialog_run (GNOME_DIALOG (dialog)) == 0) {
			char *errmsg = NULL;
			char *cmdline = gtk_entry_get_text
					(GTK_ENTRY (command_line));
			char *thename = gtk_entry_get_text (GTK_ENTRY (name));
			if (cmdline[0] != '/') {
				errmsg = _("A command line must start "
					   "with a forward slash "
					   "('/')");
			} else if (ve_string_empty (thename)) {
				errmsg = _("A descriptive server name must "
					   "be supplied");
			}

			if (errmsg != NULL) {
				GtkWidget *error_dialog =
					gnome_error_dialog (errmsg);
				gnome_dialog_set_parent
					(GNOME_DIALOG (error_dialog), 
					 (GtkWindow *) dialog);
				gtk_window_set_modal
					(GTK_WINDOW (error_dialog),
					 TRUE);
				gnome_dialog_run_and_close
					(GNOME_DIALOG (error_dialog));
				continue;
			}
			break;
		} else {
			gtk_widget_destroy (dialog);
			return;
		}
	}

	current_name = gtk_entry_get_text (GTK_ENTRY (name));
	current_command = gtk_entry_get_text (GTK_ENTRY (command_line));

	if (edit) {
		gtk_clist_set_text (server_def_clist,
				    selected_server_def_row, 0,
				    ve_sure_string (current_name));
		gtk_clist_set_text (server_def_clist,
				    selected_server_def_row, 1,
				    ve_sure_string (current_command));
		if (GTK_TOGGLE_BUTTON (flexible)->active)
			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_def_clist")),
				 selected_server_def_row, 2, _("Yes"));
		else
			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_def_clist")),
				 selected_server_def_row, 2, _("No"));

		if (GTK_TOGGLE_BUTTON (standard)->active) {
			make_server_def_default (NULL, NULL);
		}
	} else {
		int row;
		char *server[3];

		server[0] = current_name;
		server[1] = current_command;
		if (GTK_TOGGLE_BUTTON (flexible)->active)
			server[2] = _("Yes");
		else
			server[2] = _("No");

		row = gtk_clist_append (server_def_clist, server);

		if (GTK_TOGGLE_BUTTON (standard)->active) {
			/* UGLY! */
			int tmp = selected_server_def_row;
			selected_server_def_row = row;
			make_server_def_default (NULL, NULL);
			selected_server_def_row = tmp;
		} else {
			char *id = get_unique_name (NULL);
			gtk_clist_set_row_data_full (server_def_clist, row, id,
						     (GDestroyNotify) g_free);
		}
	}

	gtk_widget_destroy (dialog);
}

void
add_new_server_def (GtkButton *button, gpointer user_data)
{
	handle_server_def_edit (FALSE /*edit*/);
}


void
edit_selected_server_def (GtkButton *button, gpointer user_data)
{
	handle_server_def_edit (TRUE /*edit*/);
}

void
delete_selected_server_def (GtkButton *button, gpointer user_data)
{
	int tmp = selected_server_def_row;
	int i;
	char *id;
	char *cmdline = NULL;
	GtkCList *server_def_clist;
	GtkCList *server_clist;

	if (selected_server_def_row < 0)
		return;

	server_clist = GTK_CLIST (get_widget ("server_clist"));
	server_def_clist = GTK_CLIST (get_widget ("server_def_clist"));

	id = gtk_clist_get_row_data (server_def_clist, tmp);
	gtk_clist_get_text (server_def_clist, tmp, 1, &cmdline);

	for (i = 0; i < server_clist->rows; i++) {
		char *rowid = gtk_clist_get_row_data (server_clist, i);
		if ((rowid == NULL && id == NULL) ||
		    (rowid != NULL && id != NULL && strcmp (rowid, id) == 0)) {
			char *newcmd;
			char *extra = NULL;

			gtk_clist_get_text (server_clist, i, 2, &extra);
			if (ve_string_empty (extra))
				newcmd = g_strdup (ve_sure_string (cmdline));
			else
				newcmd = g_strconcat (ve_sure_string (cmdline),
						      " ", extra, NULL);

			gtk_clist_set_text (server_clist, i, 1, newcmd);
			gtk_clist_set_text (server_clist, i, 2, "");

			g_free (newcmd);
		}
	}

	/* Remove the server from the list */
	selected_server_def_row = -1;
	gtk_clist_remove (server_def_clist, tmp);
}

void
make_server_def_default (GtkButton *button, gpointer user_data)
{
	int old_dfl;
	GdkColor col;

	if (selected_server_def_row < 0)
		return;

	old_dfl = find_default_server_def ();

	if (old_dfl == selected_server_def_row)
		return;

	if (old_dfl >= 0) {
		rename_row (old_dfl, NULL);
		gtk_clist_set_background
			(GTK_CLIST (get_widget ("server_def_clist")),
			 old_dfl, NULL);
	}
	rename_row (selected_server_def_row, GDM_STANDARD);

	gdk_color_parse ("#d6e8ff", &col);
	gtk_clist_set_background (GTK_CLIST (get_widget ("server_def_clist")),
				  selected_server_def_row, &col);
}

void
record_selected_server_def (GtkCList *clist,
			    gint row,
			    gint column,
			    GdkEventButton *event,
			    gpointer user_data)
{
	selected_server_def_row = row;
}

static void
fill_svr_select_list (GladeXML *xml)
{
	int i;
	GtkCList *svr_clist = (GtkCList *)get_widget ("server_def_clist");
	GtkCList *clist =
		(GtkCList *)glade_helper_get_clist (xml,
						    "svr_select_clist",
						    gtk_clist_get_type (),
						    1);
	for (i = 0; i < svr_clist->rows; i++) {
		int row;
		char *text = NULL;
		char *id = NULL;
		gtk_clist_get_text (svr_clist, i, 0, &text);
		id = gtk_clist_get_row_data (svr_clist, i);
		if (text != NULL) {
			row = gtk_clist_append (clist, &text);
			gtk_clist_set_row_data_full
				(clist, row,
				 g_strdup (id),
				 (GDestroyNotify) g_free);
		}
	}

	if (i == 0) {
		int row;
		char *text = _("Standard server");
		row = gtk_clist_append (clist, &text);
		gtk_clist_set_row_data_full
			(clist, row,
			 g_strdup (GDM_STANDARD),
			 (GDestroyNotify) g_free);
	}
}

static void
custom_toggled (GtkWidget *w, gpointer data)
{
	GladeXML *xml = data;
	GtkWidget *clist = glade_helper_get_clist (xml, "svr_select_clist",
						   gtk_clist_get_type (), 1);
	GtkWidget *cmdline = glade_helper_get (xml, "svr_select_command_line",
					       gtk_entry_get_type ());
	GtkWidget *extra = glade_helper_get (xml, "svr_select_arguments",
					     gtk_entry_get_type ());

	if (GTK_TOGGLE_BUTTON (w)->active) {
		gtk_widget_set_sensitive (clist, FALSE);
		gtk_widget_set_sensitive (extra, FALSE);
		gtk_widget_set_sensitive (cmdline, TRUE);
	} else {
		gtk_widget_set_sensitive (clist, TRUE);
		gtk_widget_set_sensitive (extra, TRUE);
		gtk_widget_set_sensitive (cmdline, FALSE);
	}
}

static void
connect_custom (GladeXML *xml)
{
	GtkWidget *button = glade_helper_get (xml, "svr_select_custom_cb",
					      gtk_toggle_button_get_type ());

	gtk_object_ref (GTK_OBJECT (xml));
	gtk_signal_connect_full (GTK_OBJECT (button), "toggled",
				 GTK_SIGNAL_FUNC (custom_toggled),
				 NULL /* marshal*/,
				 xml,
				 (GtkDestroyNotify) gtk_object_unref,
				 FALSE /*object_signal*/,
				 FALSE /*after*/);

	/* setup sensitivities */
	custom_toggled (button, xml);
}

static void
select_svr (GtkCList *clist, const char *current_id)
{
	int i;

	for (i = 0; i < clist->rows; i++) {
		char *id = NULL;
		id = gtk_clist_get_row_data (clist, i);
		if (id != NULL && current_id != NULL &&
		    strcmp (id, current_id) == 0) {
			gtk_clist_select_row (clist, i, 0);
			return;
		}
	}
}

static void
handle_server_edit (gboolean edit)
{
	GladeXML *xml;
	GtkWidget *dialog, *custom_cb, *clist, *cmdline, *extra;
	char *current_server = NULL, *current_extra = NULL;
	char *current_id;
    
	if (edit && selected_server_row < 0)
		return;

	xml = glade_helper_load ("gdmconfig.glade", "svr_select",
				 gnome_dialog_get_type (),
				 TRUE /* dump on destroy */);
	dialog = glade_helper_get (xml, "svr_select",
				   gnome_dialog_get_type ());
	custom_cb = glade_helper_get (xml, "svr_select_custom_cb",
				      gtk_toggle_button_get_type ());
	clist = glade_helper_get_clist (xml, "svr_select_clist",
					gtk_clist_get_type (), 1);
	cmdline = glade_helper_get (xml, "svr_select_command_line",
				    gtk_entry_get_type ());
	extra = glade_helper_get (xml, "svr_select_arguments",
				  gtk_entry_get_type ());

	fill_svr_select_list (xml);
	connect_custom (xml);

	gtk_entry_set_text (GTK_ENTRY (cmdline), "/usr/bin/X11/X");

	if (edit) {
		gtk_clist_get_text (GTK_CLIST (get_widget ("server_clist")),
				    selected_server_row, 1, &current_server);
		gtk_clist_get_text (GTK_CLIST (get_widget ("server_clist")),
				    selected_server_row, 2, &current_extra);
		current_id = gtk_clist_get_row_data
			(GTK_CLIST (get_widget ("server_clist")),
			 selected_server_row);

		gtk_entry_set_text (GTK_ENTRY (extra),
				    ve_sure_string (current_extra));
		if (current_id == NULL) {
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (custom_cb), TRUE);
			gtk_entry_set_text (GTK_ENTRY (cmdline),
					    ve_sure_string (current_server));
		} else {
			select_svr (GTK_CLIST (clist), current_id);
		}
	}

	gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
				 (GtkWindow *) GDMconfigurator);
	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	for (;;) {
		if (gnome_dialog_run (GNOME_DIALOG (dialog)) == 0) {
			if (GTK_TOGGLE_BUTTON (custom_cb)->active) {
				char *server = gtk_entry_get_text
					(GTK_ENTRY (cmdline));
				if (server[0] != '/') {
					GtkWidget *error_dialog =
						gnome_error_dialog
						(_("A command line must start "
						   "with a forward slash "
						   "('/')"));
					gnome_dialog_set_parent
						(GNOME_DIALOG (error_dialog), 
						 (GtkWindow *) dialog);
					gtk_window_set_modal
						(GTK_WINDOW (error_dialog),
						 TRUE);
					gnome_dialog_run_and_close
						(GNOME_DIALOG (error_dialog));
					continue;
				}
			} else {
				if (GTK_CLIST (clist)->selection == NULL)
					continue;
			}
			break;
		} else {
			gtk_widget_destroy (dialog);
			return;
		}
	}

	if (edit) {
		if (GTK_TOGGLE_BUTTON (custom_cb)->active) {
			char *server = gtk_entry_get_text (GTK_ENTRY (cmdline));
			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, 1, server);
			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, 2, "");
			gtk_clist_set_row_data
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, NULL);
		} else {
			char *args = gtk_entry_get_text (GTK_ENTRY (extra));
			char *server, *id;
			int sel = GPOINTER_TO_INT
				(GTK_CLIST (clist)->selection->data);
			server = NULL;
			gtk_clist_get_text (GTK_CLIST (clist), sel, 0, &server);
			id = gtk_clist_get_row_data (GTK_CLIST (clist), sel);

			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, 1,
				 ve_sure_string (server));
			gtk_clist_set_text
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, 2,
				 ve_sure_string (args));
			gtk_clist_set_row_data_full
				(GTK_CLIST (get_widget ("server_clist")),
				 selected_server_row, 
				 g_strdup (id),
				 (GDestroyNotify) g_free);
		}
	} else {
		if (GTK_TOGGLE_BUTTON (custom_cb)->active) {
			char *server = gtk_entry_get_text (GTK_ENTRY (cmdline));
			char *new_server[3];

			new_server[0] =
				g_strdup_printf("%d", number_of_servers);
			new_server[1] = server;
			new_server[2] = "";

			/* We now have an extra server */
			number_of_servers++;

			gtk_clist_append(GTK_CLIST(get_widget("server_clist")),
					 new_server);

			g_free (new_server[0]);
		} else {
			char *new_server[3];
			char *args = gtk_entry_get_text (GTK_ENTRY (extra));
			char *server, *id;
			int row;
			int sel = GPOINTER_TO_INT
				(GTK_CLIST (clist)->selection->data);
			server = NULL;
			gtk_clist_get_text (GTK_CLIST (clist), sel, 0, &server);
			id = gtk_clist_get_row_data (GTK_CLIST (clist), sel);


			new_server[0] =
				g_strdup_printf("%d", number_of_servers);
			new_server[1] = ve_sure_string (server);
			new_server[2] = ve_sure_string (args);

			/* We now have an extra server */
			number_of_servers++;

			row = gtk_clist_append
				(GTK_CLIST (get_widget ("server_clist")),
				 new_server);

			g_free (new_server[0]);


			gtk_clist_set_row_data_full
				(GTK_CLIST (get_widget ("server_clist")),
				 row, 
				 g_strdup (id),
				 (GDestroyNotify) g_free);
		}
	}

	gtk_widget_destroy (dialog);
}

void
add_new_server (GtkButton *button, gpointer user_data)
{
	handle_server_edit (FALSE /*edit*/);
}

void
edit_selected_server (GtkButton *button, gpointer user_data)
{
	handle_server_edit (TRUE /*edit*/);
}

static void
redo_server_numbers (void)
{
	int i;

	for (i = 0; i < number_of_servers; i++) {
		char *num = g_strdup_printf("%d", i);
		gtk_clist_set_text (GTK_CLIST (get_widget("server_clist")),
				    i, 0, num);
		g_free (num);
	}
}


void
delete_selected_server                 (GtkButton       *button,
                                        gpointer         user_data)
{
    int tmp = selected_server_row;
    
    if (selected_server_row < 0)
      return;

    selected_server_row = -1;

    /* Remove the server from the list */
    gtk_clist_remove(GTK_CLIST(get_widget("server_clist")), 
		     tmp);
    number_of_servers--;
    
    /* The server numbers will be out of sync now, so correct that */ 
    redo_server_numbers ();
}


void
record_selected_server (GtkCList *clist,
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

    /* The server numbers will be out of sync now, so correct that */ 
    redo_server_numbers ();

    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 --selected_server_row, 0);
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
    /* The server numbers will be out of sync now, so correct that */ 
    redo_server_numbers ();

    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 ++selected_server_row, 0);
}

void
sessions_clist_row_selected                  (GtkCList *clist,
					      gint row,
					      gint column,
					      GdkEventButton *event,
					      gpointer user_data)
{
   gint pos = 0;
   GdmConfigSession *sess_details = (GdmConfigSession *)
     gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
			     row);
   if (sess_details == NULL)
	   return;

   /* Stop silly things happening while we fill the text widget. */
   gtk_signal_handler_block_by_func (GTK_OBJECT(get_widget("session_text")),
				     session_text_edited, NULL);
   gtk_signal_handler_block_by_func (GTK_OBJECT(get_widget("session_name_entry")),
				     modify_session_name, NULL);
   selected_session_row = row;
   
   gtk_text_freeze (GTK_TEXT (get_widget("session_text")));
   if (sess_details->script_contents != NULL) {
      gtk_editable_delete_text (GTK_EDITABLE (get_widget ("session_text")),
				0, gtk_text_get_length(GTK_TEXT (get_widget ("session_text"))));
      gtk_editable_insert_text (GTK_EDITABLE (get_widget ("session_text")),
				sess_details->script_contents,
				strlen (sess_details->script_contents), &pos);
   } else {
      gtk_editable_delete_text (GTK_EDITABLE (get_widget ("session_text")),
				0, -1);
   }
   gtk_text_thaw (GTK_TEXT (get_widget("session_text")));
   
   gtk_entry_set_text (GTK_ENTRY (get_widget("session_name_entry")),
		       sess_details->name);

   /* set up editablity according to readability/writability */
   gtk_editable_set_editable (GTK_EDITABLE (get_widget ("session_text")),
			      sess_details->changable);
   /* set up editablity according to readability/writability */
   gtk_editable_set_editable (GTK_EDITABLE (get_widget ("session_name_entry")),
			      sess_details->changable);
   /* not red by default */
   ve_entry_set_red (get_widget ("session_name_entry"), FALSE);

   gtk_signal_handler_unblock_by_func (GTK_OBJECT(get_widget("session_text")),
				       session_text_edited, NULL);
   gtk_signal_handler_unblock_by_func (GTK_OBJECT(get_widget("session_name_entry")),
				       modify_session_name, NULL);
}

void
set_new_default_session (GtkButton *button,
			 gpointer user_data)
{
   GdkColor col;
   
   if (current_default_session != NULL) {
      current_default_session->is_default = FALSE;
      gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
				default_session_row,
				NULL);
   }
   current_default_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
						     selected_session_row);
   current_default_session->is_default = TRUE;
   
   gdk_color_parse ("#d6e8ff", &col);
   gtk_clist_freeze (GTK_CLIST (get_widget("sessions_clist")));

   gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
			     selected_session_row,
			     &col);
   default_session_row = selected_session_row;
   gtk_clist_thaw (GTK_CLIST (get_widget("sessions_clist")));
}

static gboolean
is_legal_session_name (const char *text, GdmConfigSession *this_session)
{
	int i;

	if (ve_string_empty (text))
		return FALSE;

	for (i = 0; i < GTK_CLIST(get_widget ("sessions_clist"))->rows; i++) {
		GdmConfigSession *session;
		session = gtk_clist_get_row_data
			(GTK_CLIST(get_widget ("sessions_clist")), i);
		g_assert (session != NULL);

		if (session == this_session)
			continue;

		if (strcmp (session->name, text) == 0)
			return FALSE;
	}

	return TRUE;
}

void
add_session_real (gchar *new_session_name, gpointer data)
{
   GdmConfigSession *a_session;
   if (is_legal_session_name (new_session_name, NULL)) {
	   char *label[1];
	   int rowNum;

	   /* printf ("Will add session with %s.\n", new_session_name); */
	   a_session = g_new0 (GdmConfigSession, 1);
	   a_session->name = g_strdup (new_session_name);

	   label[0] = a_session->name;

	   rowNum = gtk_clist_append (GTK_CLIST (get_widget("sessions_clist")),
				      label);
	   gtk_clist_set_row_data (GTK_CLIST (get_widget ("sessions_clist")),
				   rowNum,
				   a_session);
   } else {
	   GtkWidget *error_dialog =
		   gnome_error_dialog (_("A session name must be unique and "
					 "not empty"));
	   gnome_dialog_set_parent (GNOME_DIALOG (error_dialog), 
				   (GtkWindow *) GDMconfigurator);
	   gtk_window_set_modal (GTK_WINDOW (error_dialog), TRUE);
	   gnome_dialog_run_and_close (GNOME_DIALOG (error_dialog));
   }
}

void
add_session (GtkButton *button,
		gpointer user_data)
{
   gnome_request_dialog (FALSE, _("Enter a name for the new session"),
			 NULL, 50, add_session_real,
			 NULL, GTK_WINDOW(GDMconfigurator));
}

void
remove_session (GtkButton *button,
		gpointer user_data)
{
   if (selected_session_row >= 0) {
      GdmConfigSession *data = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")), 
							      selected_session_row);
      g_assert (data != NULL);

      /* note that the old_ can stay as it is, even pointed to
       * freed data, I know it's not nice, but it's NEVER accessed,
       * and if we changed it it would screw us up. */
      if (data == current_default_session) {
	      current_default_session = NULL;
	      default_session_row = -1;
      }

      deleted_sessions_list = g_list_append (deleted_sessions_list,
					     g_strdup(data->name));
      g_free (data->name);
      g_free (data->old_name);
      g_free (data->script_contents);
      /* for sanity sake */
      memset (data, 0xff, sizeof (GdmConfigSession));
      g_free (data);

      gtk_clist_remove (GTK_CLIST (get_widget ("sessions_clist")),
			selected_session_row);
   }
}

void
session_text_edited (GtkEditable *text, gpointer data)
{
   GdmConfigSession *selected_session;
   if (selected_session_row < 0)
	   return;
   selected_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							 selected_session_row);
   g_assert (selected_session != NULL);
					      
   g_free (selected_session->script_contents);
   
   /* The editable_get_chars g_strdups it's result already, note that this
    * is different from entry_get_text which doesn't.  Chaulk that up to
    * too much crack use. */
   selected_session->script_contents =
	   gtk_editable_get_chars (text, 0,
				   gtk_text_get_length(GTK_TEXT(text)));
   selected_session->changed = TRUE;
}

void
modify_session_name (GtkEntry *entry, gpointer data)
{
   GdmConfigSession *selected_session;
   gchar *text;
   if (selected_session_row < 0)
	   return;
   selected_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							 selected_session_row);
   g_assert (selected_session != NULL);

   text = gtk_entry_get_text (entry);

   /* Note: if we got to empty or matching one of the other sessions,
    * just ignore this change, the user will type
    * something in, if not, use the last change, perhaps there should be
    * a warning/error on Apply ... Showing a dialog here would be evil and
    * bad UI.  The user probably wiped the entry to typoe in something
    * different*/
   if (is_legal_session_name (text, selected_session)) {
	ve_entry_set_red (GTK_WIDGET (entry), FALSE);
	/* We need to track the first name of this session to rename
	 * the actual session file.
	 */
	if ( ! selected_session->renamed) {
	   selected_session->old_name = g_strdup (selected_session->name);
	   selected_session->renamed = TRUE;
	}
	g_free (selected_session->name);
	selected_session->name = g_strdup (text);
	gtk_clist_set_text (GTK_CLIST (get_widget ("sessions_clist")),
			    selected_session_row, 0,
			    selected_session->name);
   } else {
	ve_entry_set_red (GTK_WIDGET (entry), TRUE);
   }
}

void 
session_directory_modified (GtkEntry *entry, gpointer data) 
{
	char *str;
	static gboolean shown_warning = FALSE;
	/* FIXME: Ask user if they wish to reload the sessions clist based on
	 * the new directory. ISSUE: what to do if they say no, especially if
	 * the new dir is invalid. Could get icky.
	 */

	/* don't warn if we haven't really changed it */
	str = gtk_entry_get_text (GTK_ENTRY (entry));
	if (str != NULL &&
	    sessions_directory != NULL &&
	    strcmp (str, sessions_directory) == 0)
		return;

	/* Right now just run a warning */
	if ( ! shown_warning) {
		gnome_warning_dialog_parented
			(_("You have modified the sessions directory.\n"
			   "Your session changes will still get written\n"
			   "to the old directory however, until you reload\n"
			   "the configuration dialog again."),
			 GTK_WINDOW (GDMconfigurator));
		shown_warning = TRUE;
	}
}

