#include "config.h"

#include <string.h>
#include <syslog.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "gdm.h"
#include "gdmwm.h"

#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_parser.h"
#include "greeter_geometry.h"
#include "greeter_item_clock.h"
#include "greeter_item_pam.h"
#include "greeter_events.h"
#include "greeter_action_language.h"
#include "greeter_session.h"

gboolean DOING_GDM_DEVELOPMENT = FALSE;

GtkWidget *window;
GtkWidget *canvas;

static char *GreeterConfTheme = NULL;
static char *GdmDefaultLocale = NULL;
gboolean GdmShowGnomeChooserSession = FALSE;
gboolean GdmShowGnomeFailsafeSession = FALSE;
gboolean GdmShowXtermFailsafeSession = FALSE;
gboolean GdmShowLastSession = FALSE;
gchar *GdmSessionDir = NULL;
int greeter_use_circles_in_entry = FALSE;



static void 
greeter_parse_config (void)
{
    if (!g_file_test (GDM_CONFIG_FILE, G_FILE_TEST_EXISTS)) {
	syslog (LOG_ERR, _("greeter_parse_config: No configuration file: %s. Using defaults."), GDM_CONFIG_FILE);
    }

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GreeterConfTheme = gnome_config_get_string (GREETER_KEY_THEME);
    GdmDefaultLocale = gnome_config_get_string (GDM_KEY_LOCALE);
    greeter_use_circles_in_entry = gnome_config_get_bool (GREETER_KEY_ENTRY_CIRCLES);

    GdmShowXtermFailsafeSession = gnome_config_get_bool (GDM_KEY_SHOW_XTERM_FAILSAFE);
    GdmShowGnomeFailsafeSession = gnome_config_get_bool (GDM_KEY_SHOW_GNOME_FAILSAFE);
    GdmShowGnomeChooserSession = gnome_config_get_bool (GDM_KEY_SHOW_GNOME_CHOOSER);
    GdmShowLastSession = gnome_config_get_bool (GDM_KEY_SHOW_LAST_SESSION);
    GdmSessionDir = gnome_config_get_string (GDM_KEY_SESSDIR);
    
    gnome_config_pop_prefix();
}

static gboolean
greeter_ctrl_handler (GIOChannel *source,
		      GIOCondition cond,
		      gint fd)
{
    gchar buf[PIPE_SIZE];
    gsize len;
    GtkWidget *dlg;
    char *session;
    GreeterItemInfo *conversation_info;

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
	
	greeter_item_pam_set_user (buf);
	
	g_print ("%c\n", STX);
	break;
    case GDM_LOGIN:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	greeter_item_pam_prompt (buf, 32, TRUE, TRUE);
	break;

    case GDM_PROMPT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	greeter_item_pam_prompt (buf, 128, TRUE, FALSE);
	break;

    case GDM_NOECHO:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	greeter_item_pam_prompt (buf, 128, FALSE, FALSE);
	break;

    case GDM_MSG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	greeter_item_pam_message (buf);

	g_print ("%c\n", STX);

	break;

    case GDM_ERRBOX:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	greeter_item_pam_error (buf);
	
	g_print ("%c\n", STX);
	break;

    case GDM_ERRDLG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

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
	
	g_print ("%c\n", STX);
	break;

    case GDM_SESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	buf[len-1] = '\0';

	session = greeter_session_lookup (buf);
	g_print ("%c%s\n", STX, session);
	g_free (session);
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

	if (greeter_save_session ())
	  g_print ("%cY\n", STX);
	else
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
	{
		char *sess;
		GString *str = g_string_new (NULL);

		do {
			g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
			buf[len-1] = '\0';
			g_string_append (str, buf);
		} while (len == PIPE_SIZE-1);


		sess = greeter_get_gnome_session (str->str);

		g_string_free (str, TRUE);

		g_print ("%c%s\n", STX, sess);

		g_free (sess);
	}
	break;

    case GDM_SGNOMESESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	if (greeter_save_gnome_session ())
	    g_print ("%cY\n", STX);
	else
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

static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *key, gpointer data)
{
  if (DOING_GDM_DEVELOPMENT && (key->keyval == GDK_Escape))
    {
      gtk_main_quit ();
      
      return TRUE;
    }
  
  return FALSE;
}

static void
greeter_setup_items (void)
{

  greeter_item_clock_setup ();
  greeter_item_pam_setup ();
  greeter_item_capslock_setup (window);
  greeter_item_register_action_callback ("language_button",
					 greeter_action_language,
					 window);
  greeter_item_system_setup ();
  greeter_item_session_setup ();
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

void
greeter_message (const gchar *msg)
{
 GtkWidget *req = NULL;
  
  /* we should be now fine for focusing new windows */
  gdm_wm_focus_new_windows (TRUE);
  
  req = gtk_message_dialog_new (NULL /* parent */,
				GTK_DIALOG_MODAL /* flags */,
				GTK_MESSAGE_INFO,
				GTK_BUTTONS_CLOSE,
				"%s",
				msg);
  g_signal_connect (G_OBJECT (req), "destroy",
		    G_CALLBACK (gtk_widget_destroyed),
		    &req);
  
  gdm_wm_center_window (GTK_WINDOW (req));
  
  gdm_wm_no_login_focus_push ();
  gtk_dialog_run (GTK_DIALOG (req));
  gdm_wm_no_login_focus_pop ();

  if (req)
    gtk_widget_destroy (req);
}


gboolean
greeter_query (const gchar *msg)
{
	int ret;
	GtkWidget *req;

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	req = gtk_message_dialog_new (NULL /* parent */,
				      GTK_DIALOG_MODAL /* flags */,
				      GTK_MESSAGE_QUESTION,
				      GTK_BUTTONS_YES_NO,
				      "%s",
				      msg);

	g_signal_connect (G_OBJECT (req), "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
			  &req);

	gdm_wm_center_window (GTK_WINDOW (req));

	gdm_wm_no_login_focus_push ();
	ret = gtk_dialog_run (GTK_DIALOG (req));
	gdm_wm_no_login_focus_pop ();

	if (req != NULL)
	  gtk_widget_destroy (req);

	if (ret == GTK_RESPONSE_YES)
		return TRUE;
	else /* this includes window close */
		return FALSE;
}

void
greeter_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	_exit (DISPLAY_ABORT);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    closelog();

    g_free (s);

    _exit (DISPLAY_ABORT);
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
  GError *error;
  GreeterItemInfo *root;
  char *theme_file;
  char *theme_dir;
  int r;

  if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
    DOING_GDM_DEVELOPMENT = TRUE;

  openlog ("gdmgreeter", LOG_PID, LOG_DAEMON);

  setlocale (LC_ALL, "");

  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  greeter_parse_config ();

  /* no language set, use the GdmDefaultLocale */
  if (GdmDefaultLocale != NULL &&
      strlen (GdmDefaultLocale) != 0 &&
      g_getenv ("LANG") == NULL &&
      g_getenv ("LC_ALL") == NULL)
    setlocale (LC_ALL, GdmDefaultLocale);
  else
    setlocale (LC_ALL, "");
  
  gtk_init (&argc, &argv);

  gdm_wm_screen_init (0);
  
  r = verify_gdm_version ();
  if (r != 0)
    return r;

  greeter_session_init ();
  
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

  if (DOING_GDM_DEVELOPMENT)
    gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
			GTK_SIGNAL_FUNC (key_press_event), NULL);
  
  canvas = gnome_canvas_new_aa ();
  gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas),
				  0.0, 0.0,
				  (double) w,
				  (double) h);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (window), w, h);
  gtk_container_add (GTK_CONTAINER (window), canvas);

  if (g_path_is_absolute (GreeterConfTheme))
    {
      theme_file = g_strdup (GreeterConfTheme);
    }
  else
    {
      theme_file = g_build_filename (GREETERTHEMEDIR,
				     GreeterConfTheme,
				     NULL);
    }

  theme_dir = g_path_get_dirname (theme_file);
  
  error = NULL;
  root = greeter_parse (theme_file, theme_dir,
			GNOME_CANVAS (canvas), w, h, &error);

  if (root == NULL)
    g_warning ("Failed to parse file: %s!", error->message);

  greeter_layout (root, GNOME_CANVAS (canvas));
  
  greeter_setup_items ();
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
