#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>

#include "vicious.h"

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"

#include "greeter.h"
#include "greeter_session.h"
#include "greeter_item_pam.h"
#include "greeter_configuration.h"
#include "greeter_events.h"

#define LAST_SESSION "Last"
#define LAST_LANGUAGE "Last"
#define SESSION_NAME "SessionName"

static gboolean save_session = FALSE;
static char *current_session = NULL;
static gchar *default_session = NULL;
static GSList *sessions = NULL;
static GHashTable *sessnames = NULL;

/* This is true if session dir doesn't exist or is whacked out
 * in some way or another */
gboolean session_dir_whacked_out = FALSE;
static GtkWidget *session_dialog;

static GSList *session_group = NULL;

static gboolean 
greeter_login_list_lookup (GSList *l, const gchar *data)
{
    GSList *list = l;

    if (list == NULL || data == NULL)
	return FALSE;

    /* FIXME: Hack, will support these builtin types later */
    if (strcmp (data, GDM_SESSION_DEFAULT ".desktop") == 0 ||
	strcmp (data, GDM_SESSION_CUSTOM ".desktop") == 0 ||
	strcmp (data, GDM_SESSION_FAILSAFE ".desktop") == 0) {
	    return TRUE;
    }

    while (list) {

	if (strcmp (list->data, data) == 0)
	    return TRUE;
	
	list = list->next;
    }

    return FALSE;
}

static const char *
session_name (const char *name)
{
	const char *nm;

	/* eek */
	if G_UNLIKELY (name == NULL)
		return "(null)";

	nm = g_hash_table_lookup (sessnames, name);
	if (nm != NULL)
		return nm;
	else
		return name;
}

char *
greeter_session_lookup (const char *saved_session)
{
  gchar *session = NULL;
  
  /* Don't save session unless told otherwise */
  save_session = FALSE;

  /* Previously saved session not found in ~/.dmrc */
  if ( ! (saved_session != NULL &&
	  strcmp ("(null)", saved_session) != 0 &&
	  saved_session[0] != '\0')) {
    /* If "Last" is chosen run default,
     * else run user's current selection */
    if (current_session == NULL || strcmp (current_session, LAST_SESSION) == 0)
      session = g_strdup (default_session);
    else
      session = g_strdup (current_session);
    
    save_session = TRUE;
    return session;
  }

  /* If "Last" session is selected */
  if (current_session == NULL ||
      strcmp (current_session, LAST_SESSION) == 0)
    { 
      session = g_strdup (saved_session);
      
      /* Check if user's saved session exists on this box */
      if (!greeter_login_list_lookup (sessions, session))
	{
	  gchar *msg;
	  
	  session = g_strdup (default_session);
	  msg = g_strdup_printf (_("Your preferred session type %s is not "
				   "installed on this machine.\n"
				   "Do you wish to make %s the default for "
				   "future sessions?"),
				 session_name (saved_session),
				 session_name (default_session));	    
	  save_session = gdm_common_query (msg, FALSE /* markup */, _("Make _Default"), _("Just _Log In"));
	  g_free (msg);
	}
    }
  else /* One of the other available session types is selected */
    { 
      session = g_strdup (current_session);
    
      /* User's saved session is not the chosen one */
      if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	  strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
	  g_ascii_strcasecmp (session, GDM_SESSION_FAILSAFE ".desktop") == 0 ||
	  g_ascii_strcasecmp (session, GDM_SESSION_FAILSAFE) == 0)
	{
	  save_session = FALSE;
	}
      else if (strcmp (saved_session, session) != 0)
	{
	  gchar *msg = NULL;
	  
	  if (GdmShowLastSession)
	    {
	      msg = g_strdup_printf (_("You have chosen %s for this "
				       "session, but your default "
				       "setting is %s.\nDo you wish "
				       "to make %s the default for "
				       "future sessions?"),
				     session_name (session),
				     session_name (saved_session),
				     session_name (session));
	      save_session = gdm_common_query (msg, FALSE /* markup */, _("Make _Default"), _("Just For _This Session"));
	    }
	  else if (strcmp (session, default_session) != 0 &&
		   strcmp (session, saved_session) != 0 &&
		   strcmp (session, LAST_SESSION) != 0)
	    {
	      /* if !GdmShowLastSession then our saved session is
	       * irrelevant, we are in "switchdesk mode"
	       * and the relevant thing is the saved session
	       * in .Xclients
	       */
	      if (access ("/usr/bin/switchdesk", F_OK) == 0)
	        {
	          msg = g_strdup_printf (_("You have chosen %s for this "
				           "session.\nIf you wish to make %s "
				           "the default for future sessions,\n"
				           "run the 'switchdesk' utility\n"
					   "(System->Desktop Switching Tool from "
					   "the panel menu)."),
					 session_name (session),
					 session_name (session));
		  gdm_common_message (msg);
		}
	      save_session = FALSE;
	    }
	  g_free (msg);
	}
    }
  
  return session;
}

gboolean
greeter_save_session (void)
{
  return save_session;
}

void 
greeter_session_init (void)
{
  GtkWidget *w = NULL;
  GtkWidget *hbox = NULL;
  GtkWidget *main_vbox = NULL;
  GtkWidget *vbox = NULL;
  GtkWidget *cat_vbox = NULL;
  GtkWidget *radio;
  GtkWidget *dialog;
  DIR *sessdir;
  struct dirent *dent;
  gboolean searching_for_default = FALSE;
  static GtkTooltips *tooltips = NULL;
  GtkRequisition req;
  char *s;
  int num = 1;
  int i;
  char **vec;
  gboolean some_dir_exists = FALSE;

  g_free (current_session);
  current_session = NULL;
  
  session_dialog = dialog = gtk_dialog_new ();
  if (tooltips == NULL)
	  tooltips = gtk_tooltips_new ();

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_OK,
			 GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				   GTK_RESPONSE_OK);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  main_vbox = gtk_vbox_new (FALSE, 18);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      main_vbox,
		      FALSE, FALSE, 0);

  cat_vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox),
		      cat_vbox,
		      FALSE, FALSE, 0);

  s = g_strdup_printf ("<b>%s</b>",
		       _("Choose a Session"));
  w = gtk_label_new (s);
  gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
  g_free (s);
  gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (cat_vbox), w, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_vbox),
		      hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox),
		      gtk_label_new ("    "),
		      FALSE, FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 6);
  /* we will pack this later depending on size */

  sessnames = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (sessnames, GDM_SESSION_FAILSAFE_GNOME, _("Failsafe Gnome"));
  g_hash_table_insert (sessnames, GDM_SESSION_FAILSAFE_XTERM, _("Failsafe xterm"));

  if (GdmShowLastSession)
    {
      current_session = g_strdup (LAST_SESSION);

      radio = gtk_radio_button_new_with_mnemonic (session_group, _("_Last"));
      g_object_set_data (G_OBJECT (radio),
			 SESSION_NAME,
			 LAST_SESSION);
      session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
      gtk_tooltips_set_tip (tooltips, radio,
			    _("Log in using the session that you have used "
			      "last time you logged in"),
			    NULL);
      gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 4);
      gtk_widget_show (radio);
    }

    vec = g_strsplit (GdmSessionDir, ":", -1);
    for (i = 0; vec != NULL && vec[i] != NULL; i++)
      {
        const char *dir = vec[i];

	/* Check that session dir is readable */
	if G_UNLIKELY (dir == NULL || access (dir, R_OK|X_OK) != 0)
		continue;

	some_dir_exists = TRUE;

	/* Read directory entries in session dir */
	sessdir = opendir (dir);

	if G_LIKELY (sessdir != NULL)
		dent = readdir (sessdir);
	else
		dent = NULL;

	while (dent != NULL) {
		VeConfig *cfg;
		char *exec;
		char *name;
		char *comment;
		char *label;
		char *tryexec;
		char *ext;

		/* ignore everything bug the .desktop files */
		ext = strstr (dent->d_name, ".desktop");
		if (ext == NULL ||
		    strcmp (ext, ".desktop") != 0) {
			dent = readdir (sessdir);
			continue;
		}

		/* already found this session, ignore */
		if (g_hash_table_lookup (sessnames, dent->d_name) != NULL) {
			dent = readdir (sessdir);
			continue;
		}

		s = g_strconcat (dir, "/", dent->d_name, NULL);
		cfg = ve_config_new (s);
		g_free (s);

		if (ve_config_get_bool (cfg, "Desktop Entry/Hidden=false")) {
			g_hash_table_insert (sessnames, g_strdup (dent->d_name), "foo");
			ve_config_destroy (cfg);
			dent = readdir (sessdir);
			continue;
		}

		tryexec = ve_config_get_string (cfg, "Desktop Entry/TryExec");
		if ( ! ve_string_empty (tryexec)) {
			char *full = g_find_program_in_path (tryexec);
			if (full == NULL) {
				g_hash_table_insert (sessnames, g_strdup (dent->d_name), "foo");
				g_free (tryexec);
				ve_config_destroy (cfg);
				dent = readdir (sessdir);
				continue;
			}
			g_free (full);
		}
		g_free (tryexec);

		exec = ve_config_get_string (cfg, "Desktop Entry/Exec");
		name = ve_config_get_translated_string (cfg, "Desktop Entry/Name");
		comment = ve_config_get_translated_string (cfg, "Desktop Entry/Comment");

		ve_config_destroy (cfg);

		if G_UNLIKELY (ve_string_empty (exec) || ve_string_empty (name)) {
			g_hash_table_insert (sessnames, g_strdup (dent->d_name), "foo");
			g_free (exec);
			g_free (name);
			g_free (comment);
			dent = readdir (sessdir);
			continue;
		}

		if (num < 10)
			label = g_strdup_printf ("_%d. %s", num, name);
		else
			label = g_strdup (name);
		num ++;

		radio = gtk_radio_button_new_with_mnemonic (session_group, label);
		g_free (label);
		g_object_set_data_full (G_OBJECT (radio),
					SESSION_NAME,
					g_strdup (dent->d_name),
					(GDestroyNotify) g_free);
		session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
		gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 4);
		gtk_widget_show (radio);

		if ( ! ve_string_empty (comment))
			gtk_tooltips_set_tip
				(tooltips, GTK_WIDGET (radio), comment, NULL);


		sessions = g_slist_append (sessions, g_strdup (dent->d_name));

		/* if we found the default session */
		if ( ! ve_string_empty (GdmDefaultSession) &&
		     strcmp (dent->d_name, GdmDefaultSession) == 0) {
			g_free (default_session);
			default_session = g_strdup (dent->d_name);
			searching_for_default = FALSE;
		}

		/* if there is a session called default */
		if (searching_for_default &&
		    g_ascii_strcasecmp (dent->d_name, "default.desktop") == 0) {
			g_free (default_session);
			default_session = g_strdup (dent->d_name);
		}

		if (searching_for_default &&
		    g_ascii_strcasecmp (dent->d_name, "gnome.desktop") == 0) {
			/* Just in case there is no default session and
			 * no default link, make Gnome the default */
			if (default_session == NULL)
				default_session = g_strdup (dent->d_name);
		}

		g_hash_table_insert (sessnames, g_strdup (dent->d_name), name);

		g_free (exec);
		g_free (comment);

		dent = readdir (sessdir);
	}

	if (sessdir != NULL)
		closedir (sessdir);
      }

    g_strfreev (vec);

    /* Check that session dir is readable */
    if G_UNLIKELY ( ! some_dir_exists)
      {
	syslog (LOG_ERR, _("%s: Session directory %s not found!"), "gdm_login_session_init", ve_sure_string (GdmSessionDir));
	session_dir_whacked_out = TRUE;
	GdmShowXtermFailsafeSession = TRUE;
      }


    if G_UNLIKELY (sessions == NULL)
      {
	syslog (LOG_WARNING, _("Yaikes, nothing found in the session directory."));
	session_dir_whacked_out = TRUE;
	GdmShowXtermFailsafeSession = TRUE;
	
	default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
      }

    if (GdmShowGnomeFailsafeSession)
      {
	/* For translators:  This is the failsafe login when the user
	 * can't login otherwise */
	radio = gtk_radio_button_new_with_mnemonic (session_group,
						    _("Failsafe _Gnome"));
	gtk_tooltips_set_tip (tooltips, GTK_WIDGET (radio),
			      _("This is a failsafe session that will log you "
				"into GNOME.  No startup scripts will be read "
				"and it is only to be used when you can't log "
				"in otherwise.  GNOME will use the 'Default' "
				"session."),
			      NULL);
	g_object_set_data (G_OBJECT (radio),
			   SESSION_NAME, GDM_SESSION_FAILSAFE_GNOME);
	
	session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
	sessions = g_slist_append (sessions,
				   g_strdup (GDM_SESSION_FAILSAFE_GNOME));
	
	gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 4);
	gtk_widget_show (radio);
      }

    if (GdmShowXtermFailsafeSession)
      {
	/* For translators:  This is the failsafe login when the user
	 * can't login otherwise */
	radio = gtk_radio_button_new_with_mnemonic (session_group,
						    _("Failsafe _Terminal"));
	gtk_tooltips_set_tip (tooltips, GTK_WIDGET (radio),
			      _("This is a failsafe session that will log you "
				"into a terminal.  No startup scripts will be read "
				"and it is only to be used when you can't log "
				"in otherwise.  To exit the terminal, "
				"type 'exit'."),
			      NULL);
	g_object_set_data (G_OBJECT (radio),
			   SESSION_NAME, GDM_SESSION_FAILSAFE_XTERM);

	session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
	sessions = g_slist_append (sessions,
				   g_strdup (GDM_SESSION_FAILSAFE_XTERM));
	
	gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 4);
	gtk_widget_show (radio);
      }
                    
    if G_UNLIKELY (default_session == NULL)
      {
	default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	syslog (LOG_WARNING, _("No default session link found. Using Failsafe GNOME.\n"));
      }
    
    if (current_session == NULL)
            current_session = g_strdup (default_session);

    gtk_widget_show_all (vbox);
    gtk_widget_size_request (vbox, &req);

    /* if too large */
    if (req.height > 0.7 * gdm_wm_screen.height) {
	    GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
	    gtk_widget_set_size_request (sw,
					 req.width, 
					 0.7 * gdm_wm_screen.height);
	    gtk_scrolled_window_set_shadow_type
		    (GTK_SCROLLED_WINDOW (sw),
		     GTK_SHADOW_NONE);
	    gtk_scrolled_window_set_policy
		    (GTK_SCROLLED_WINDOW (sw),
		     GTK_POLICY_NEVER,
		     GTK_POLICY_AUTOMATIC);
	    gtk_scrolled_window_add_with_viewport
		    (GTK_SCROLLED_WINDOW (sw), vbox);
	    gtk_widget_show (sw);
	    gtk_box_pack_start (GTK_BOX (hbox),
				sw,
				TRUE, TRUE, 0);
    } else {
	    gtk_box_pack_start (GTK_BOX (hbox),
				vbox,
				TRUE, TRUE, 0);
    }
}

static void
greeter_session_handler (GreeterItemInfo *info,
			 gpointer         user_data)
{
  GSList *tmp;
  int ret;
  
  /* Select the proper session */
  tmp = session_group;
  while (tmp != NULL)
    {
      GtkWidget *w = tmp->data;
      const char *n;
      
      n = g_object_get_data (G_OBJECT (w), SESSION_NAME);
      
      if (n && strcmp (n, current_session) == 0)
	{
	  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
					TRUE);
	  break;
	}
      
      tmp = tmp->next;
    }

  gtk_widget_show_all (session_dialog);

  gdm_wm_center_window (GTK_WINDOW (session_dialog));
  
  gdm_wm_no_login_focus_push ();
  ret = gtk_dialog_run (GTK_DIALOG (session_dialog));
  gdm_wm_no_login_focus_pop ();
  gtk_widget_hide (session_dialog);

  if (ret == GTK_RESPONSE_OK)
    {
      tmp = session_group;
      while (tmp != NULL)
	{
	  GtkWidget *w = tmp->data;
	  const char *n;
	  

	  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
	    {
	      n = g_object_get_data (G_OBJECT (w), SESSION_NAME);
	      g_free (current_session);
	      current_session = g_strdup (n);
	      break;
	    }
	  
	  tmp = tmp->next;
	}
    }
}

void
greeter_item_session_setup ()
{
  greeter_item_register_action_callback ("session_button",
					 (ActionFunc)greeter_session_handler,
					 NULL);
}
