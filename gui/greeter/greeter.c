#include "config.h"

#include <string.h>
#include <syslog.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "gdm.h"
#include "gdmwm.h"

#include "greeter.h"
#include "greeter_parser.h"
#include "greeter_item_clock.h"
#include "greeter_item_pam.h"
#include "greeter_events.h"
#include "greeter_action_language.h"

gboolean DOING_GDM_DEVELOPMENT = FALSE;

#define DEBUG_GREETER 1

GtkWidget *window;
GtkWidget *canvas;

static gboolean
greeter_ctrl_handler (GIOChannel *source,
		      GIOCondition cond,
		      gint fd)
{
    gchar buf[PIPE_SIZE];
    gsize len;
    gint i, x, y;
    GtkWidget *dlg;
    static gboolean replace_msg = TRUE;
    static gboolean messages_to_give = FALSE;
    GreeterItemInfo *conversation_info;
    GreeterItemInfo *entry_info;

    /* If this is not incoming i/o then return */
    if (cond != G_IO_IN) 
      return TRUE;

    /* Read random garbage from i/o channel until STX is found */
    do {
      g_io_channel_read (source, buf, 1, &len);
      
      if (len != 1)
	return TRUE;
    }  while (buf[0] && buf[0] != STX);

    /* Read opcode */
    g_io_channel_read (source, buf, 1, &len);

    /* If opcode couldn't be read */
    if (len != 1)
      return TRUE;

    /* Parse opcode */
    switch (buf[0]) {
    case GDM_SETLOGIN:
	/* somebody is trying to fool us this is the user that
	 * wants to log in, and well, we are the gullible kind */
        g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';
	/* TODO: set current user to buf */
	g_print ("%c\n", STX);
	break;
    case GDM_LOGIN:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	conversation_info = greeter_lookup_id ("pam-conversation");
	entry_info = greeter_lookup_id ("user-pw-entry");
	
	if (conversation_info)
	  g_object_set (G_OBJECT (conversation_info->item),
			"text",	buf,
			NULL);

	if (entry_info && entry_info->item &&
	    GNOME_IS_CANVAS_WIDGET (entry_info->item))
	  {
	    GtkWidget *entry;
	    entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
	    gtk_widget_grab_focus (entry);
	    gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	    gtk_entry_set_max_length (GTK_ENTRY (entry), 32);
	    gtk_entry_set_text (GTK_ENTRY (entry), "");
	  }

	break;

    case GDM_PROMPT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	conversation_info = greeter_lookup_id ("pam-conversation");
	entry_info = greeter_lookup_id ("user-pw-entry");
	
	if (conversation_info)
	  g_object_set (G_OBJECT (conversation_info->item),
			"text",	buf,
			NULL);

	if (entry_info && entry_info->item &&
	    GNOME_IS_CANVAS_WIDGET (entry_info->item))
	  {
	    GtkWidget *entry;
	    entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
	    gtk_widget_grab_focus (entry);
	    gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	    gtk_entry_set_text (GTK_ENTRY (entry), "");
	    gtk_entry_set_max_length (GTK_ENTRY (entry), 128);
	  }

	break;

    case GDM_NOECHO:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	conversation_info = greeter_lookup_id ("pam-conversation");
	entry_info = greeter_lookup_id ("user-pw-entry");
	
	if (conversation_info)
	  g_object_set (G_OBJECT (conversation_info->item),
			"text",	buf,
			NULL);

	if (entry_info && entry_info->item &&
	    GNOME_IS_CANVAS_WIDGET (entry_info->item))
	  {
	    GtkWidget *entry;
	    entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
	    gtk_widget_grab_focus (entry);
	    gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	    gtk_entry_set_max_length (GTK_ENTRY (entry), 128);
	    gtk_entry_set_text (GTK_ENTRY (entry), "");
	  }

	break;

    case GDM_MSG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

#ifdef TODO
	/* the user has not yet seen messages */
	messages_to_give = TRUE;

	/* HAAAAAAACK.  Sometimes pam sends many many messages, SO
	 * we try to collect them until the next prompt or reset or
	 * whatnot */
	if ( ! replace_msg) {
		const char *oldtext;
		oldtext = gtk_label_get_text (GTK_LABEL (msg));
		if ( ! ve_string_empty (oldtext)) {
			char *newtext;
			newtext = g_strdup_printf ("%s\n%s", oldtext, buf);
			gtk_label_set_text (GTK_LABEL (msg), newtext);
			g_free (newtext);
		} else {
			gtk_label_set_text (GTK_LABEL (msg), buf);
		}
	} else {
		gtk_label_set_text (GTK_LABEL (msg), buf);
	}
	replace_msg = FALSE;

	gtk_widget_show (GTK_WIDGET (msg));

#endif
	
	g_print ("%c\n", STX);

	break;

    case GDM_ERRBOX:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

#ifdef TODO	
	gtk_label_set_text (GTK_LABEL (err_box), buf);
	if (err_box_clear_handler > 0)
		gtk_timeout_remove (err_box_clear_handler);
	if (ve_string_empty (buf))
		err_box_clear_handler = 0;
	else
		err_box_clear_handler = gtk_timeout_add (30000,
							 err_box_clear,
							 NULL);
#endif
	
	g_print ("%c\n", STX);
	break;

    case GDM_ERRDLG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

#ifdef TODO	
	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	dlg = gtk_message_dialog_new (NULL /* parent */,
				      GTK_DIALOG_MODAL /* flags */,
				      GTK_MESSAGE_ERROR,
				      GTK_BUTTONS_OK,
				      "%s",
				      buf);

	gdm_wm_center_window (GTK_WINDOW (dlg));

	gdm_wm_no_login_focus_push ();
	gtk_dialog_run (GTK_DIALOG (dlg));
	gdm_wm_no_login_focus_pop ();
#endif
	
	g_print ("%c\n", STX);
	break;

    case GDM_SESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	buf[len-1] = '\0';
#ifdef TODO	
	gdm_login_session_lookup (buf);
	g_print ("%c%s\n", STX, session);
#else
	g_print ("%c%s\n", STX, "Gnome");
#endif
	break;

    case GDM_LANG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	buf[len-1] = '\0';
#ifdef TODO	
	gdm_login_language_lookup (buf);
	g_print ("%c%s\n", STX, language);
#else
	g_print ("%c%s\n", STX, "C");
#endif
	break;

    case GDM_SSESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

#ifdef TODO	
	if (savesess)
	    g_print ("%cY\n", STX);
	else
#endif
	    g_print ("%c\n", STX);
	
	break;

    case GDM_SLANG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

#ifdef TODO	
	if (savelang)
	    g_print ("%cY\n", STX);
	else
#endif
	    g_print ("%c\n", STX);

	break;

    case GDM_RESET:
	/* fall thru to reset */

    case GDM_RESETOK:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	conversation_info = greeter_lookup_id ("pam-conversation");
	
	if (conversation_info)
	  g_object_set (G_OBJECT (conversation_info->item),
			"text",	buf,
			NULL);

	g_print ("%c\n", STX);
	break;

    case GDM_QUIT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	/* TODO: Stuff. */

	gdk_flush ();

	g_print ("%c\n", STX);

	/* screw gtk_main_quit, we want to make sure we definately die */
	_exit (EXIT_SUCCESS);
	break;

    case GDM_GNOMESESS:
#ifdef TODO
	{
		char *sess;
		GString *str = g_string_new (NULL);

		do {
			g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
			buf[len-1] = '\0';
			g_string_append (str, buf);
		} while (len == PIPE_SIZE-1);


		sess = get_gnome_session (str->str);

		g_string_free (str, TRUE);

		g_print ("%c%s\n", STX, sess);

		g_free (sess);
	}
#else
		g_print ("%c%s\n", STX, "Gnome");
#endif
	break;

    case GDM_SGNOMESESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

#ifdef TODO
	if (remember_gnome_session)
	    g_print ("%cY\n", STX);
	else
#endif
	    g_print ("%c\n", STX);

	break;

    case GDM_STARTTIMER:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	/*
	 * Timed Login: Start Timer Loop
	 */

	/* TODO */
	
	g_print ("%c\n", STX);
	break;

    case GDM_STOPTIMER:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	/*
	 * Timed Login: Stop Timer Loop
	 */

	/* TODO */
	
	g_print ("%c\n", STX);
	break;

    case GDM_DISABLE:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	/* TODO */
	g_print ("%c\n", STX);
	break;

    case GDM_ENABLE:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	/* TODO */
	g_print ("%c\n", STX);
	break;

    /* These are handled separately so ignore them here and send
     * back a NULL response so that the daemon quits sending them */
    case GDM_NEEDPIC:
    case GDM_READPIC:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	g_print ("%c\n", STX);
	break;
	
    default:
	break;
    }

    return (TRUE);
}

#ifdef DEBUG_GREETER
static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *key, gpointer data)
{
	if (key->keyval == GDK_Escape) {
		gtk_main_quit ();

		return TRUE;
	}

	return FALSE;
}
#endif

static void
greeter_setup_items (void)
{

  greeter_item_clock_setup ();
  greeter_item_pam_setup ();
  greeter_item_capslock_setup (window);
  greeter_item_register_action_callback ("language_button",
					 greeter_action_language,
					 window);

}

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

static int
verify_gdm_version ()
{
  const char *gdm_version;
  const char *gdm_protocol_version;
  
  gdm_version = g_getenv ("GDM_VERSION");
  gdm_protocol_version = g_getenv ("GDM_GREETER_PROTOCOL_VERSION");

  if (! DOING_GDM_DEVELOPMENT &&
      ((gdm_protocol_version != NULL &&
	strcmp (gdm_protocol_version, GDM_GREETER_PROTOCOL_VERSION) != 0) ||
       (gdm_protocol_version == NULL &&
	(gdm_version == NULL ||
	 strcmp (gdm_version, VERSION) != 0))) &&
      (g_getenv ("GDM_IS_LOCAL") != NULL))
    {
      GtkWidget *dialog;
    
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("The greeter version (%s) does not match the daemon "
					 "version.\n"
					 "You have probably just upgraded gdm.\n"
					 "Please restart the gdm daemon or reboot the computer."),
				       VERSION);
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
    
      gtk_dialog_run (GTK_DIALOG (dialog));
    
      return EXIT_SUCCESS;
    }
  
  if (! DOING_GDM_DEVELOPMENT &&
      gdm_protocol_version == NULL &&
      gdm_version == NULL)
    {
      GtkWidget *dialog;
    
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_NONE,
				       _("The greeter version (%s) does not match the daemon "
					 "version.\n"
					 "You have probably just upgraded gdm.\n"
					 "Please restart the gdm daemon or reboot the computer."),
				       VERSION);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			      _("Reboot"),
			      RESPONSE_REBOOT,
			      GTK_STOCK_CLOSE,
			      RESPONSE_CLOSE,
			      NULL);
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
      
      switch (gtk_dialog_run (GTK_DIALOG (dialog)))
	{
	case RESPONSE_REBOOT:
	  return DISPLAY_REBOOT;
	default:
	  return DISPLAY_ABORT;
	}
    }
  
  if (! DOING_GDM_DEVELOPMENT &&
      ((gdm_protocol_version != NULL &&
	strcmp (gdm_protocol_version, GDM_GREETER_PROTOCOL_VERSION) != 0) ||
       (gdm_protocol_version == NULL &&
	strcmp (gdm_version, VERSION) != 0)))
    {
      GtkWidget *dialog;
    
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_NONE,
				       _("The greeter version (%s) does not match the daemon "
					 "version (%s).\n"
					 "You have probably just upgraded gdm.\n"
					 "Please restart the gdm daemon or reboot the computer."),
				       VERSION, gdm_version);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			      _("Restart"),
			      RESPONSE_RESTART,
			      _("Reboot"),
			      RESPONSE_REBOOT,
			      GTK_STOCK_CLOSE,
			      RESPONSE_CLOSE,
			      NULL);
      
      
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
    
      gtk_dialog_set_default_response (GTK_DIALOG (dialog), RESPONSE_RESTART);
      
      switch (gtk_dialog_run (GTK_DIALOG (dialog)))
	{
	case RESPONSE_RESTART:
	  return DISPLAY_RESTARTGDM;
	case RESPONSE_REBOOT:
	  return DISPLAY_REBOOT;
	default:
	  return DISPLAY_ABORT;
	}
    }
  
  return 0;
}

static void
greeter_done (int sig)
{
    _exit (EXIT_SUCCESS);
}

int
main (int argc, char *argv[])
{
  struct sigaction hup;
  sigset_t mask;
  GIOChannel *ctrlch;
  gint w, h;
  gboolean res;
  int r;

  if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
    DOING_GDM_DEVELOPMENT = TRUE;

  openlog ("gdmgreeter", LOG_PID, LOG_DAEMON);

  setlocale (LC_ALL, "");

  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  gtk_init (&argc, &argv);

  gdm_wm_screen_init (0);
  
  r = verify_gdm_version ();
  if (r != 0)
    return r;

  hup.sa_handler = greeter_done;
  hup.sa_flags = 0;
  sigemptyset(&hup.sa_mask);
  sigaddset (&hup.sa_mask, SIGCHLD);
  
  if (sigaction (SIGHUP, &hup, NULL) < 0) 
    g_error (_("main: Error setting up HUP signal handler"));
  
  if (sigaction (SIGINT, &hup, NULL) < 0) 
    g_error (_("main: Error setting up INT signal handler"));
  
  if (sigaction (SIGTERM, &hup, NULL) < 0) 
    g_error (_("main: Error setting up TERM signal handler"));
  
  sigfillset (&mask);
  sigdelset (&mask, SIGTERM);
  sigdelset (&mask, SIGHUP);
  sigdelset (&mask, SIGINT);
  sigdelset (&mask, SIGCHLD);
  
  if (sigprocmask (SIG_SETMASK, &mask, NULL) == -1) 
    g_error (_("Could not set signal mask!"));

  
  if (! DOING_GDM_DEVELOPMENT) {
    ctrlch = g_io_channel_unix_new (STDIN_FILENO);
    g_io_channel_init (ctrlch);
    g_io_add_watch (ctrlch, 
		    G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		    (GIOFunc) greeter_ctrl_handler,
		    NULL);
    g_io_channel_unref (ctrlch);
  }
  
  w = gdk_screen_width ();
  h = gdk_screen_height ();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

#ifdef DEBUG_GREETER
  gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
		      GTK_SIGNAL_FUNC (key_press_event), NULL);
#endif

  canvas = gnome_canvas_new_aa ();
  gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas),
				  0.0, 0.0,
				  (double) w,
				  (double) h);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (window), w, h);
  gtk_container_add (GTK_CONTAINER (window), canvas);

  
  
  res = greeter_parse (argv[1], GNOME_CANVAS (canvas), w, h);

  if (!res)
    g_warning ("Failed to parse file!");

  greeter_setup_items ();
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
