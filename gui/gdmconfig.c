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
#include <dirent.h>
#include <sys/stat.h>
#include <syslog.h>
#include "gdmconfig.h"
#include "icon-entry-hack.h"

/* This should always be undefined before building ANY kind of production release. */
/*#define DOING_DEVELOPMENT 1*/

/* XML Structures for the various configurator components */
GladeXML *GUI = NULL;
GladeXML *basic_notebook = NULL;
GladeXML *expert_notebook = NULL;
GladeXML *system_notebook = NULL;

/* The XML file */
gchar *glade_filename;

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
int selected_session_row = -1, default_session_row = -1;
GtkWidget *invisible_notebook = NULL;
struct GdmConfigSession *current_default_session = NULL;
GList *deleted_sessions_list = NULL;

/* Main application widget pointer */
GtkWidget *GDMconfigurator = NULL;

/** This is something of a hack, but it keeps the code clean and
 * easily extensible later on. 
 */
GtkWidget *get_widget(gchar *widget_name)
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
      if (GTK_TOGGLE_BUTTON (get_widget(widget_name))->active == TRUE) {
	 gnome_config_set_int(key, i);
	 g_free (widget_name);
	 return;
      }
      g_free (widget_name);
      i++;
   }
}


int
main (int argc, char *argv[])
{
    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    gnome_init ("gdmconfig", VERSION, argc, argv);
    glade_gnome_init();

    /* Make sure the user is root. If not, they shouldn't be messing with 
     * GDM's configuration.
     */

#ifndef DOING_DEVELOPMENT
 if (geteuid() != 0)
      {
	  GtkWidget *fatal_error = 
	  gnome_error_dialog(_("You must be the superuser (root) to configure GDM.\n"));
	  gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
	  exit(EXIT_FAILURE);
      }
#endif /* DOING_DEVELOPMENT */

    /* Look for the glade file in $(datadir)/gdm or, failing that,
     * look in the current directory.
	 * Except when doing development, we want the app to use the glade file
	 * in the same directory, so we can actually make changes easily.
     */
	
#ifndef DOING_DEVELOPMENT
    glade_filename = gnome_datadir_file("gdm/gdmconfig.glade");
    if (!glade_filename)
      {	  
	  glade_filename = g_strdup("gdmconfig.glade");
      }
#else
	glade_filename = g_strdup("gdmconfig.glade");
#endif /* DOING_DEVELOPMENT */
	
    /* Build the user interface */
    GUI = glade_xml_new(glade_filename, "gdmconfigurator");
	basic_notebook = glade_xml_new(glade_filename, "basic_notebook");
	system_notebook = glade_xml_new(glade_filename, "system_notebook");
	expert_notebook = glade_xml_new(glade_filename, "expert_notebook");

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
    g_assert(GUI != NULL);

    /* Sanity checking */
    GDMconfigurator = get_widget("gdmconfigurator");
    g_assert(GDMconfigurator != NULL);

    /* We set most of the user interface NOT wanting signals to get triggered as
     * we do it. Then we hook up the signals, and THEN set a few remaining elements.
     * This ensures sensitivity of some widgets is correct, and that the font picker
     * gets set properly.
     */
    gdm_config_parse_most();
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

    gdm_config_parse_remaining();

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

void show_about_box(void) {
	glade_xml_new(glade_filename, "about_gdmconfig");
}

void
gdm_config_parse_most (void)
{
    void *iter; gchar *key, *value;
    DIR *sessdir;
    struct dirent *dent;
    struct stat statbuf;
    gchar *default_session_name = NULL;
    gint linklen;

    /* If the GDM config file does not exist, we have sensible defaults,
     * but make sure the user is warned.
     */
    if (!g_file_exists(GDM_CONFIG_FILE))
      {
	  GtkWidget *error_dialog =
	  gnome_error_dialog(_("The configuration file: " GDM_CONFIG_FILE "\ndoes not exist! Using default values."));
	  gnome_dialog_set_parent(GNOME_DIALOG(error_dialog), 
				  (GtkWindow *) GDMconfigurator);
	  gnome_dialog_run_and_close(GNOME_DIALOG(error_dialog));
      }
    
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
    
    /* Fill the widgets in GDM tab */
    gdm_entry_set("automatic_login", gnome_config_get_string (GDM_KEY_AUTOMATICLOGIN));
    gdm_entry_set("timed_login", gnome_config_get_string (GDM_KEY_TIMED_LOGIN));
    gdm_spin_set("timed_delay", gnome_config_get_int(GDM_KEY_TIMED_LOGIN_DELAY));
    gdm_entry_set("chooser_binary", gnome_config_get_string (GDM_KEY_CHOOSER));
    gdm_entry_set("config_binary", gnome_config_get_string (GDM_KEY_CONFIGURATOR));
    gdm_entry_set("greeter_binary", gnome_config_get_string (GDM_KEY_GREETER));
    gdm_entry_set("halt_command", gnome_config_get_string (GDM_KEY_HALT));
    gdm_entry_set("reboot_command", gnome_config_get_string (GDM_KEY_REBOOT));
    
    gdm_entry_set("init_dir", gnome_config_get_string (GDM_KEY_INITDIR));
    gdm_entry_set("log_dir", gnome_config_get_string (GDM_KEY_LOGDIR));
    gdm_entry_set("session_dir", gnome_config_get_string (GDM_KEY_SESSDIR));
    gdm_entry_set("pre_session_dir", gnome_config_get_string (GDM_KEY_PRESESS));
    gdm_entry_set("post_session_dir", gnome_config_get_string (GDM_KEY_POSTSESS));

    gdm_entry_set("pid_file", gnome_config_get_string (GDM_KEY_PIDFILE));
    gdm_entry_set("gnome_default_session", gnome_config_get_string (GDM_KEY_GNOMEDEFAULTSESSION));
    gdm_entry_set("default_path", gnome_config_get_string (GDM_KEY_PATH));
    gdm_entry_set("root_path", gnome_config_get_string (GDM_KEY_ROOTPATH));
 

    /* Fill the widgets in Security tab */
    gdm_toggle_set("allow_root", gnome_config_get_bool(GDM_KEY_ALLOWROOT));
    gdm_toggle_set("kill_init_clients", gnome_config_get_bool(GDM_KEY_KILLIC));
    gdm_radio_set ("relax_perms", gnome_config_get_int(GDM_KEY_RELAXPERM), 2);
    gdm_toggle_set("verbose_auth", gnome_config_get_bool(GDM_KEY_VERBAUTH));

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
    
    gdm_entry_set("exclude_users", gnome_config_get_string (GDM_KEY_EXCLUDE));
    /* font picker is in parse_remaining() */
    gdm_entry_set("welcome_message", gnome_config_get_string (GDM_KEY_WELCOME));
    

    /* Fill the widgets in Background tab */
    gdm_entry_set ("background_program", gnome_config_get_string (GDM_KEY_BACKGROUNDPROG));
    gdm_entry_set ("background_image", gnome_config_get_string (GDM_KEY_BACKGROUNDIMAGE));
    gdm_color_set ("background_color", gnome_config_get_string (GDM_KEY_BACKGROUNDCOLOR));
    gdm_toggle_set ("background_scale", gnome_config_get_bool (GDM_KEY_BACKGROUNDSCALETOFIT));

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

    gdm_toggle_set("enable_debug", gnome_config_get_bool(GDM_KEY_DEBUG));

    /* Read directory entries in session dir */
    sessdir = opendir (gnome_config_get_string (GDM_KEY_SESSDIR));
    dent = readdir (sessdir);

    while (dent != NULL) {
	gchar *s;

	/* Ignore backups and rpmsave files */
	if ((strstr (dent->d_name, "~")) ||
	    (strstr (dent->d_name, ".rpmsave")) ||
	    (strstr (dent->d_name, ".orig"))) {
	    dent = readdir (sessdir);
	    continue;
	}

	s = g_strconcat (gnome_config_get_string (GDM_KEY_SESSDIR),
			 "/", dent->d_name, NULL);
	lstat (s, &statbuf);

        /* If default session link exists, find out what it points to */
	if (S_ISLNK (statbuf.st_mode) &&
	    g_strcasecmp (dent->d_name, "default") == 0) 
	 {
	    gchar t[_POSIX_PATH_MAX];
	    
	    linklen = readlink (s, t, _POSIX_PATH_MAX);
	    t[linklen] = 0;
	    
	   /* Prevent sym links of default to default from screwing
	    * things up.
	    */
	   if (g_strcasecmp("default", t) != 0) {
	      printf ("recording %s as default session.\n", t);
	     default_session_name = g_strdup(t);
	   }
	 }

	/* If session script is readable/executable add it to the list */
	if (S_ISREG (statbuf.st_mode)) {

	    if ((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
		(statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
		(statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) 
	    {
	       gchar *row[1];
	       int rowNum;
	       struct GdmConfigSession *this_session;
	       FILE *script_file;
	       
	       this_session = g_malloc0 (sizeof (struct GdmConfigSession));
	       this_session->name = g_strdup(dent->d_name);
	       script_file = fopen(s, "r");
	       if (!script_file) {
		  gnome_error_dialog_parented (_("Error reading session script!"),
					       GTK_WINDOW (GDMconfigurator));
		  this_session->script_contents = _("Error reading this session script");
	       } else {
		  gchar buffer[BUFSIZ];
		  int charPos = 0, character;
		  for (character = fgetc(script_file);
		       character != EOF;
		       character = fgetc(script_file)) {
		     buffer[charPos] = (gchar) character;
		     charPos++;
		  }
		  buffer[charPos] = '\0';
		  this_session->script_contents = g_strdup(buffer);
		  /* printf ("got script contents:\n%s.\n", this_session->script_contents); */
	       }
	       fclose (script_file);
	       this_session->changed = FALSE;
	       this_session->is_default = FALSE;

	       row[0] = this_session->name;
	       
	       rowNum = gtk_clist_append (GTK_CLIST (get_widget("sessions_clist")),
					  row);
	       gtk_clist_set_row_data (GTK_CLIST (get_widget ("sessions_clist")),
				       rowNum,
				       this_session);

	    }
	    else 
		syslog (LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
			gnome_config_get_string(GDM_KEY_SESSDIR), dent->d_name);
	}

	dent = readdir (sessdir);
	g_free (s);
    }

   if (default_session_name) {
      int i;
      GdkColor col;

      for (i=0; i< GTK_CLIST(get_widget ("sessions_clist"))->rows; i++) {
	 struct GdmConfigSession *data = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
								 i);
	 g_assert (data != NULL);
	 if (!g_strcasecmp(default_session_name,
			   data->name)) {
	    
	    printf ("coloring session %s.\n", data->name);
	    gdk_color_parse ("#d6e8ff", &col);
	    gdk_color_alloc (gdk_colormap_get_system(), &col);
	    
	    gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
				      i, &col);
	    data->is_default = TRUE;
	    current_default_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							      i);;
	    default_session_row = i;
	 }
      }
   } else {
      /* FIXME: failsafe "no default session" stuff. */
   }

    gnome_config_pop_prefix();
    
    /* Fill the widgets in Servers tab */
    iter=gnome_config_init_iterator("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter=gnome_config_iterator_next (iter, &key, &value);
    
    while (iter) {
	char *a_server[2];
	
	if(isdigit(*key)) 
	  {
	      a_server[0] = key;
	      a_server[1] = value;
	      gtk_clist_prepend(GTK_CLIST(get_widget("server_clist")), a_server);
	      number_of_servers++;
	  }
	else
	  {
	      gnome_warning_dialog_parented(_("gdm_config_parse_most: Invalid server line in config file. Ignoring!"),
					    GTK_WINDOW(GDMconfigurator));
	  }
	iter=gnome_config_iterator_next (iter, &key, &value);
    }
    /* FIXME: Could do something nicer with the 'exclude users' GUI sometime */
}


void
gdm_config_parse_remaining (void)
{
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

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
		if (run_query (_("The applied settings cannot take effect until gdm\n"
						 "is restarted or your computer is rebooted.\n"
						 "Do you wish to restart GDM now?\n"
						 "This will kill all your current sessions\n"
						 "and you will lose any unsaved data!")) &&
		    run_query (_("Are you sure you wish to restart GDM\n"
						 "and lose any unsaved data?"))) {
			kill (pid, SIGHUP);
			/* now what happens :) */
		}
	} else {
		w = gnome_ok_dialog_parented
			(_("The greeter settings will take effect the next time\n"
			   "it is displayed.  The rest of the settings will not\n"
			   "take effect until gdm is restarted or the computer is\n"
			   "rebooted"),
			 GTK_WINDOW (GDMconfigurator));
		gnome_dialog_run_and_close (GNOME_DIALOG (w));
	}
	/* Don't apply identical settings again. */
	gtk_widget_set_sensitive(GTK_WIDGET(get_widget("apply_button")), FALSE);
}


void
write_new_config_file                  (GtkButton *button,
                                        gpointer         user_data)
{
    int i = -1;
    GList *tmp = NULL;
    gchar *sessions_directory;
   
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
    
    sessions_directory = gnome_config_get_string (GDM_KEY_SESSDIR);
   
    /* Write out the widget contents of the GDM tab */
    gdm_entry_write("automatic_login", GDM_KEY_AUTOMATICLOGIN);
    gdm_entry_write("timed_login", GDM_KEY_TIMED_LOGIN);
    gdm_spin_write("timed_delay", GDM_KEY_TIMED_LOGIN_DELAY);
    gdm_entry_write("chooser_binary", GDM_KEY_CHOOSER);
    gdm_entry_write("greeter_binary", GDM_KEY_GREETER);
    gdm_entry_write("config_binary", GDM_KEY_CONFIGURATOR);
    gdm_entry_write("halt_command", GDM_KEY_HALT);
    gdm_entry_write("reboot_command", GDM_KEY_REBOOT);
    
    gdm_entry_write("init_dir", GDM_KEY_INITDIR);
    gdm_entry_write("log_dir", GDM_KEY_LOGDIR);
    gdm_entry_write("session_dir", GDM_KEY_SESSDIR);
    gdm_entry_write("pre_session_dir", GDM_KEY_PRESESS);
    gdm_entry_write("post_session_dir", GDM_KEY_POSTSESS);

    gdm_entry_write("pid_file", GDM_KEY_PIDFILE);
    gdm_entry_write("gnome_default_session", GDM_KEY_GNOMEDEFAULTSESSION);
    gdm_entry_write("default_path", GDM_KEY_PATH);
    gdm_entry_write("root_path", GDM_KEY_ROOTPATH);

    /* Write out the widget contents of the Security tab */
    gdm_toggle_write("allow_root", GDM_KEY_ALLOWROOT);
    gdm_toggle_write("kill_init_clients", GDM_KEY_KILLIC);
    gdm_radio_write ("relax_perms", GDM_KEY_RELAXPERM, 2);
    gdm_toggle_write("verbose_auth", GDM_KEY_VERBAUTH);

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
    
    gdm_entry_write("exclude_users", GDM_KEY_EXCLUDE);
    gdm_font_write("font_picker", GDM_KEY_FONT);
    gdm_entry_write("welcome_message", GDM_KEY_WELCOME);
    
    /* Write out the widget contents of the Background tab */
    gdm_radio_write ("background_type", GDM_KEY_BACKGROUNDTYPE, 2);
    gdm_entry_write("background_image", GDM_KEY_BACKGROUNDIMAGE);
    gdm_color_write("background_color", GDM_KEY_BACKGROUNDCOLOR);
    gdm_entry_write("background_program", GDM_KEY_BACKGROUNDPROG);
    gdm_toggle_write("background_scale", GDM_KEY_BACKGROUNDSCALETOFIT);

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

    gdm_toggle_write("enable_debug", GDM_KEY_DEBUG);

    gnome_config_pop_prefix();
    
    gnome_config_clean_section("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    gnome_config_push_prefix("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS "/");
    
    /* Write out the widget contents of the Servers tab */

    for (i=0; i<number_of_servers; i++)
      {
		  char *current_server;
		  gtk_clist_get_text(GTK_CLIST(get_widget("server_clist")),
							 i, 1, &current_server);
		  gnome_config_set_string(g_strdup_printf("%d", i), current_server);
      }

   /* It would be nice to be able to do some paranoid sanity checking on
    * more of this stuff.
    */
   /* FIXME: check all the return codes on the I/O functions. */
   for (i=0; i<GTK_CLIST(get_widget ("sessions_clist"))->rows; i++)
     {
	struct GdmConfigSession *session;
	session = gtk_clist_get_row_data (GTK_CLIST(get_widget ("sessions_clist")),
					  i);
	g_assert (session != NULL);

	/* Rename any files that need to be. */
	if (session->renamed == TRUE) {
	   printf ("renaming %s to %s.\n", session->old_name, session->name);
	   rename (g_strconcat (sessions_directory,
				session->old_name, NULL),
		   g_strconcat (sessions_directory,
				session->name, NULL));
	}

	/* Set new contents of a changed session file */
	if (session->changed == TRUE) {
	   char *filename;
	   FILE *fp;
	   
	   filename = g_strconcat (sessions_directory, session->name, NULL);
	   printf ("writing changes to: %s.\n", filename);
	   fp = fopen(filename,
		      "w");
	   if (fp == NULL) {
	      gnome_error_dialog_parented (_("There was an error writing changes to the session files.\nThe configuration may not be completely saved."),
					   GTK_WINDOW(GDMconfigurator));
	      return;
	   }
	   /* This isn't so great as we fail silently. */
	   if (fputs (session->script_contents, fp) == EOF)
	     continue;
	   fclose (fp);
	}
	
     }

   /* Remove any deleted session files */
   for (tmp=deleted_sessions_list; tmp; tmp=tmp->next)
     {
	char *file_basename = (char *)tmp->data;
	char *full_file = g_strconcat (sessions_directory, "/", 
				       file_basename, NULL);
	printf ("Would remove: %s.\n", full_file);
/*	if (remove (full_file) != 0)
	  printf ("ERROR! Could not remove file.\n");*/
     }

/*   if (g_strcasecmp (name_Default_sym_points_to, current_default_session->name) != 0)
     {
	 FIXME: relink the Default sym link to point to the right place. 
     }
*/
    gnome_config_pop_prefix();
    gnome_config_sync();

    run_warn_reset_dialog ();
}

void revert_settings_to_file_state (GtkMenuItem *menu_item,
									gpointer user_data)
{
	if (run_query(_("This will destroy any changes made in this session.\n"
					"Are you sure you want to do this?")) == TRUE) {
		gdm_config_parse_most();
		gdm_config_parse_remaining();
	}
}

void
write_and_close                        (GtkButton *button,
										gpointer user_data)
{
	write_new_config_file(button, user_data);
	exit_configurator(NULL, NULL);
}

void
open_help_page                         (GtkButton *button,
                                        gpointer         user_data)
{
	gnome_warning_dialog("Documentation is being written but is not yet finished.\nPlease be patient.");
	/* FIXME: ! */
}


gint
exit_configurator                      (void     *gnomedialog,
                                        gpointer         user_data)
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
change_background_sensitivity_image    (GtkButton       *button,
                                        gpointer         user_data)
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
change_background_sensitivity_color    (GtkButton       *button,
                                        gpointer         user_data)
{
	g_assert (button != NULL);
	g_assert (GTK_IS_TOGGLE_BUTTON (button));

	gtk_widget_set_sensitive (get_widget ("background_color"), 
				  GTK_TOGGLE_BUTTON (button)->active);
	gtk_widget_set_sensitive (get_widget ("background_color_label"), 
				  GTK_TOGGLE_BUTTON (button)->active);
}


void
set_face_sensitivity                   (GtkButton       *button,
                                        gpointer         user_data)
{
    gtk_widget_set_sensitive(get_widget("faces_table"),
			     GTK_TOGGLE_BUTTON(get_widget("enable_face_browser"))->active);
}


void handle_server_add_or_edit         (gchar           *string,
					gpointer         user_data)
{
    if(!string)
      return;

    /* Add a new server to the end of the server CList */
    if (strcmp("add", (char *)user_data) == 0)
      {
	  gchar *new_server[2];

	  new_server[0] = g_strdup_printf("%d", number_of_servers);;
	  new_server[1] = string;
	  /* We now have an extra server */
	  number_of_servers++;
	  gtk_clist_append(GTK_CLIST(get_widget("server_clist")), new_server);
      }
    else
      {
	  /* Update the current server */
	  gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
			     selected_server_row, 1,
			     string);
      }
}


void
add_new_server                         (GtkButton       *button,
                                        gpointer         user_data)
{
    /* Request the command line for this new server */
    gnome_request_dialog(FALSE, _("Enter the path to the X server,and\nany parameters that should be passed to it."),
			 "/usr/bin/X11/X",
			 -1, handle_server_add_or_edit,
			 (gpointer)"add", (GtkWindow *)GDMconfigurator);
}


void
edit_selected_server                   (GtkButton       *button,
                                        gpointer         user_data)
{
    char *current_server;
    
    if (selected_server_row < 0)
      return;

    /* Ask for the new name of this server */
    gtk_clist_get_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 1, &current_server);
    gnome_request_dialog(FALSE, _("Enter the path to the X server,and\nany parameters that should be passed to it."),
			 current_server,
			 -1, handle_server_add_or_edit,
			 (gpointer)"edit", (GtkWindow *)GDMconfigurator);

}


void
delete_selected_server                 (GtkButton       *button,
                                        gpointer         user_data)
{
    int tmp = selected_server_row;
    
    if (selected_server_row < 0)
      return;

    /* Remove the server from the list */
    gtk_clist_remove(GTK_CLIST(get_widget("server_clist")), 
		     selected_server_row);
    number_of_servers--;
    
    /* The server numbers will be out of sync now, so correct that */ 
    while (tmp < number_of_servers)
      {
	  gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")), tmp, 0,
			     g_strdup_printf("%d", tmp));
	  tmp++;
      }
    selected_server_row = -1;
}


void
record_selected_server                  (GtkCList *clist,
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
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));
    
    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 --selected_server_row, 0);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));

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
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));

    gtk_clist_select_row(GTK_CLIST(get_widget("server_clist")), 
			 ++selected_server_row, 0);
    gtk_clist_set_text(GTK_CLIST(get_widget("server_clist")),
		       selected_server_row, 0, g_strdup_printf("%d", selected_server_row));
}

void
sessions_clist_row_selected                  (GtkCList *clist,
					      gint row,
					      gint column,
					      GdkEventButton *event,
					      gpointer user_data)
{
   gint pos = 0;
   struct GdmConfigSession *sess_details = (struct GdmConfigSession *)
     gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
			     row);
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
   gtk_signal_handler_unblock_by_func (GTK_OBJECT(get_widget("session_text")),
				       session_text_edited, NULL);
   gtk_signal_handler_unblock_by_func (GTK_OBJECT(get_widget("session_name_entry")),
				       modify_session_name, NULL);
}

void
set_new_default_session (GtkButton *button,
			 gpointer user_data)
{
   GdkColor *col = malloc(sizeof(GdkColor));
   
   if (current_default_session != NULL)
      current_default_session->is_default = FALSE;
   current_default_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
						     selected_session_row);
   current_default_session->is_default = TRUE;
   
   gdk_color_parse ("#d6e8ff", col);
   gdk_color_alloc (gdk_colormap_get_system(),
		    col);
   gtk_clist_freeze (GTK_CLIST (get_widget("sessions_clist")));
   gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
			     default_session_row,
			     NULL);
   gtk_clist_set_background (GTK_CLIST (get_widget("sessions_clist")),
			     selected_session_row,
			     col);
   default_session_row = selected_session_row;
   gtk_clist_thaw (GTK_CLIST (get_widget("sessions_clist")));
}

void
add_session_real (gchar *new_session_name, gpointer data)
{
   struct GdmConfigSession *a_session;
   if (new_session_name && strlen(new_session_name) > 0)
     {
	char *label[1];
	int rowNum;
	
	printf ("Will add session with %s.\n", new_session_name);
	a_session = g_malloc0 (sizeof (struct GdmConfigSession));
	a_session->name = g_strdup(new_session_name);

	label[0] = a_session->name;
	       
	rowNum = gtk_clist_append (GTK_CLIST (get_widget("sessions_clist")),
				   label);
	gtk_clist_set_row_data (GTK_CLIST (get_widget ("sessions_clist")),
				rowNum,
				a_session);
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
      struct GdmConfigSession *data = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")), 
							      selected_session_row);
      g_assert (data != NULL);

      deleted_sessions_list = g_list_append (deleted_sessions_list,
					     g_strdup(data->name));
      if (data->name)
	g_free (data->name);
      if (data->script_contents)
	g_free (data->script_contents);
      g_free (data);

      gtk_clist_remove (GTK_CLIST (get_widget ("sessions_clist")),
			selected_session_row);
   }
}

void
session_text_edited (GtkEditable *text, gpointer data)
{
   struct GdmConfigSession *selected_session;
   selected_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							 selected_session_row);
   g_assert (selected_session != NULL);
					      
   if (selected_session->script_contents)
     g_free (selected_session->script_contents);
   
   selected_session->script_contents = g_strdup(gtk_editable_get_chars (text, 0,
									gtk_text_get_length(GTK_TEXT(text))));
   selected_session->changed = TRUE;
}

void
modify_session_name (GtkEntry *entry, gpointer data)
{
   struct GdmConfigSession *selected_session;
   gchar *text;
   selected_session = gtk_clist_get_row_data (GTK_CLIST (get_widget ("sessions_clist")),
							 selected_session_row);
   g_assert (selected_session != NULL);

   text = gtk_entry_get_text (entry);
   
   if (text && strlen(text) > 0)
     {
	/* We need to track the first name of this session to rename
	 * the actual session file.
	 */
	if (!selected_session->renamed) {
	   selected_session->old_name = selected_session->name;
	   selected_session->renamed = TRUE;
	}
	selected_session->name = text;
	gtk_clist_set_text (GTK_CLIST (get_widget ("sessions_clist")),
			    selected_session_row, 0,
			    selected_session->name);
     }
   else
     {
	gnome_warning_dialog_parented (_("You must give each session a unique name."),
				       GTK_WINDOW (GDMconfigurator));
	return;
     }
}

void 
session_directory_modified (GtkEntry *entry, gpointer data) 
{
   /* FIXME: Ask user if they wish to reload the sessions clist based on the
    * new directory. ISSUE: what to do if they say no, especially if the
    * new dir is invalid. Could get icky.
    */
}
