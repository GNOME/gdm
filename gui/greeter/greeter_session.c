#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>

#include "gdm.h"
#include "gdmwm.h"

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

/* This is true if session dir doesn't exist or is whacked out
 * in some way or another */
gboolean session_dir_whacked_out = FALSE;
static GtkWidget *session_dialog;

static GSList *session_group = NULL;

static gboolean 
greeter_login_list_lookup (GSList *l, const gchar *data)
{
    GSList *list = l;

    if (!list || !data)
	return(FALSE);

    while (list) {

	if (strcmp (list->data, data) == 0)
	    return (TRUE);
	
	list = list->next;
    }

    return (FALSE);
}

static const char *
translate_session (const char *name)
{
	/* eek */
	if (name == NULL)
		return "(null)";

	if (strcmp (name, GDM_SESSION_FAILSAFE_GNOME) == 0)
	  return _("Failsafe Gnome");
	else if (strcmp (name, GDM_SESSION_FAILSAFE_XTERM) == 0)
	  return _("Failsafe xterm");
	else
	  return _(name);
}

char *
greeter_session_lookup (const char *saved_session)
{
  gchar *session = NULL;
  
  if (greeter_current_user == NULL)
    greeter_abort("greeter_session_lookup: greeter_current_user==NULL. Mail <mkp@mkp.net> with " \
		  "information on your PAM and user database setup");
  
  /* Don't save session unless told otherwise */
  save_session = FALSE;

  /* Previously saved session not found in ~user/.gnome2/gdm */
  if ( ! (saved_session != NULL &&
	  strcmp ("(null)", saved_session) != 0 &&
	  saved_session[0] != '\0')) {
    /* If "Last" is chosen run Default,
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
				 translate_session (saved_session),
				 translate_session (default_session));	    
	  save_session = greeter_query (msg);
	  g_free (msg);
	}
    }
  else /* One of the other available session types is selected */
    { 
      session = g_strdup (current_session);
    
      /* User's saved session is not the chosen one */
      if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
	  strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
	  /* bad hack, "Failsafe" is just a name in the session dir */
	  strcmp (session, "Failsafe") == 0)
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
				     translate_session (session),
				     translate_session (saved_session),
				     translate_session (session));
	      save_session = greeter_query (msg);
	    }
	  else if (strcmp (session, "Default") != 0 &&
		   strcmp (session, LAST_SESSION) != 0)
	    {
	      /* if !GdmShowLastSession then our saved session is
	       * irrelevant, we are in "switchdesk mode"
	       * and the relevant thing is the saved session
	       * in .Xclients
	       */
	      msg = g_strdup_printf (_("You have chosen %s for this "
				       "session.\nIf you wish to make %s "
				       "the default for future sessions,\n"
				       "run the 'switchdesk' utility\n"
				       "(System->Desktop Switching Tool from "
				       "the panel menu)."),
				     translate_session (session),
				     translate_session (session));
	      save_session = FALSE;
	      greeter_message (msg);
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


/* At the moment we don't support the gnome session stuff */
char *
greeter_get_gnome_session (const char *sess_string)
{
  return g_strdup ("Default");
}

gboolean
greeter_save_gnome_session (void)
{
  return FALSE;
}

void 
greeter_session_init (void)
{
  GtkWidget *radio;
  GtkWidget *dialog;
  DIR *sessdir;
  struct dirent *dent;
  struct stat statbuf;
  gint linklen;
  gboolean got_default_link = FALSE;
  GtkTooltips *tooltips;

  g_free (current_session);
  current_session = NULL;
  
  session_dialog = dialog = gtk_dialog_new ();
  tooltips = gtk_tooltips_new ();

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_OK,
			 GTK_RESPONSE_OK);
  
  if (GdmShowLastSession)
    {
      current_session = g_strdup (LAST_SESSION);

      radio = gtk_radio_button_new_with_mnemonic (session_group, _(LAST_SESSION));
      g_object_set_data (G_OBJECT (radio),
			 SESSION_NAME,
			 LAST_SESSION);
      session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
      gtk_tooltips_set_tip (tooltips, radio,
			    _("Log in using the session that you have used "
			      "last time you logged in"),
			    NULL);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			  radio,
			  FALSE, FALSE, 4);
      gtk_widget_show (radio);
    }
    
    /* Check that session dir is readable */
    if (GdmSessionDir == NULL ||
	access (GdmSessionDir, R_OK|X_OK))
      {
	syslog (LOG_ERR, _("gdm_login_session_init: Session script directory not found!"));
	session_dir_whacked_out = TRUE;
      }

    /* Read directory entries in session dir */
    if (GdmSessionDir == NULL)
	    sessdir = NULL;
    else
	    sessdir = opendir (GdmSessionDir);

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
	    (strstr (dent->d_name, ".orig")))
	  {
	    dent = readdir (sessdir);
	    continue;
	  }

	s = g_build_filename (GdmSessionDir, dent->d_name, NULL);
	lstat (s, &statbuf);

	/* If default session link exists, find out what it points to */
	if (S_ISLNK (statbuf.st_mode))
	  {
	    if (g_ascii_strcasecmp (dent->d_name, "default") == 0)
	      {
	        gchar t[_POSIX_PATH_MAX];
	        
	        linklen = readlink (s, t, _POSIX_PATH_MAX);
	        t[linklen] = 0;
	        g_free (default_session);
	        default_session = g_strdup (t);
	        
	        got_default_link = TRUE;
	      }
	    else
	      {
		/* This may just be a link to somewhere so
		 * stat the file itself */
		stat (s, &statbuf);
	      }
	  }

	/* If session script is readable/executable add it to the list */
	if (S_ISREG (statbuf.st_mode))
	  {
	    
	    if ((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
		(statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
		(statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) 
	      {
		radio = gtk_radio_button_new_with_mnemonic (session_group, _(dent->d_name));
		g_object_set_data_full (G_OBJECT (radio),
					SESSION_NAME,
					g_strdup (dent->d_name),
					(GDestroyNotify) g_free);
		session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
				    radio,
				    FALSE, FALSE, 4);
		gtk_widget_show (radio);
		
		sessions = g_slist_append (sessions, g_strdup (dent->d_name));
		
		/* if there is a session called Default, use that as default
		 * if no link has been made */
		if ( ! got_default_link &&
		     g_ascii_strcasecmp (dent->d_name, "Default") == 0) {
		  g_free (default_session);
		  default_session = g_strdup (dent->d_name);
		}
		
		if (g_ascii_strcasecmp (dent->d_name, "Gnome") == 0) {
		  /* Just in case there is no Default session and
		   * no default link, make Gnome the default */
		  if (default_session == NULL)
		    default_session = g_strdup (dent->d_name);
		  
		  /* FIXME: when we get descriptions in session files,
		   * take this out */
		  gtk_tooltips_set_tip
		    (tooltips, GTK_WIDGET (radio),
		     _("This session will log you directly into "
		       "GNOME, into your current session."),
		     NULL);
		  
#ifdef NOT_SUPPORTED_YET
		  if (GdmShowGnomeChooserSession)
		    {
		      /* Add the chooser session, this one doesn't have a
		       * script really, it's a fake, it runs the Gnome
		       * script */
		      /* For translators:  This is the login that lets
		       * users choose the specific gnome session they
		       * want to use */
		      item = gtk_radio_menu_item_new_with_label
			(sessgrp, _("Gnome Chooser"));
		      gtk_tooltips_set_tip
			(tooltips, GTK_WIDGET (item),
			 _("This session will log you into "
			   "GNOME and it will let you choose which "
			   "one of the GNOME sessions you want to "
			   "use."),
			 NULL);
		      g_object_set_data (G_OBJECT (item),
					 SESSION_NAME,
					 GDM_SESSION_GNOME_CHOOSER);
		      
		      sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
		      sessions = g_slist_append (sessions,
						 g_strdup (GDM_SESSION_GNOME_CHOOSER));
		      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		      gtk_widget_show (GTK_WIDGET (item));
		    }
#endif
		}
	      }
	    else 
	      syslog (LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
		      GdmSessionDir, dent->d_name);
	    
	  }
	
	dent = readdir (sessdir);
	g_free (s);
    }

    if (sessdir != NULL)
	    closedir (sessdir);

    if (sessions == NULL)
      {
	syslog (LOG_WARNING, _("Yaikes, nothing found in the session directory."));
	session_dir_whacked_out = TRUE;
	
	default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
      }

    if (GdmShowGnomeFailsafeSession)
      {
	/* For translators:  This is the failsafe login when the user
	 * can't login otherwise */
	radio = gtk_radio_button_new_with_mnemonic (session_group,
						    _("Failsafe Gnome"));
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
	
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    radio,
			    FALSE, FALSE, 4);
	gtk_widget_show (radio);
      }

    if (GdmShowXtermFailsafeSession)
      {
	/* For translators:  This is the failsafe login when the user
	 * can't login otherwise */
	radio = gtk_radio_button_new_with_mnemonic (session_group,
						    _("Failsafe xterm"));
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
	
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    radio,
			    FALSE, FALSE, 4);
	gtk_widget_show (radio);
      }
                    
    if (default_session == NULL)
      {
	default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	syslog (LOG_WARNING, _("No default session link found. Using Failsafe GNOME.\n"));
      }
    
    if (current_session == NULL)
            current_session = g_strdup (default_session);


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
