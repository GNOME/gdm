#include "config.h"

#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "gdm.h"
#include "gdmwm.h"
#include "vicious.h"

#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_parser.h"
#include "greeter_geometry.h"
#include "greeter_item_clock.h"
#include "greeter_item_pam.h"
#include "greeter_item_capslock.h"
#include "greeter_item_timed.h"
#include "greeter_events.h"
#include "greeter_action_language.h"
#include "greeter_session.h"
#include "greeter_system.h"

gboolean DOING_GDM_DEVELOPMENT = FALSE;

GtkWidget *window;
GtkWidget *canvas;

char *GdmGraphicalTheme = NULL;
char *GdmGraphicalThemeDir = NULL;
int GdmXineramaScreen = 0;
gboolean GdmShowGnomeChooserSession = FALSE;
gboolean GdmShowGnomeFailsafeSession = FALSE;
gboolean GdmShowXtermFailsafeSession = FALSE;
gboolean GdmShowLastSession = FALSE;
gchar *GdmSessionDir = NULL;
gchar *GdmLocaleFile = NULL;
gchar *GdmHalt = NULL;
gchar *GdmReboot = NULL;
gchar *GdmSuspend = NULL;
gchar *GdmConfigurator = NULL;
gboolean GdmSystemMenu = TRUE;
gboolean GdmConfigAvailable = TRUE;
gboolean GdmTimedLoginEnable;
gboolean GdmUse24Clock;
gchar *GdmTimedLogin;
gchar *GdmGtkRC;
gint GdmTimedLoginDelay;

gboolean GdmUseCirclesInEntry = FALSE;

static gboolean used_defaults = FALSE;
gint greeter_current_delay = 0;

extern gboolean session_dir_whacked_out;


static void 
greeter_parse_config (void)
{
    VeConfig *config;

    if (!g_file_test (GDM_CONFIG_FILE, G_FILE_TEST_EXISTS))
      {
	syslog (LOG_ERR, _("greeter_parse_config: No configuration file: %s. Using defaults."), GDM_CONFIG_FILE);
	used_defaults = TRUE;
      }

    config = ve_config_get (GDM_CONFIG_FILE);

    GdmGraphicalTheme = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME);
    if (GdmGraphicalTheme == NULL ||
	GdmGraphicalTheme[0] == '\0')
      {
	g_free (GdmGraphicalTheme);
	GdmGraphicalTheme = g_strdup ("circles");
      }

    GdmGraphicalThemeDir = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME_DIR);
    if (GdmGraphicalThemeDir == NULL ||
	! g_file_test (GdmGraphicalThemeDir, G_FILE_TEST_IS_DIR))
      {
        g_free (GdmGraphicalThemeDir);
        GdmGraphicalThemeDir = g_strdup (GREETERTHEMEDIR);
      }
    GdmXineramaScreen = ve_config_get_int (config, GDM_KEY_XINERAMASCREEN);
    GdmUseCirclesInEntry = ve_config_get_bool (config, GDM_KEY_ENTRY_CIRCLES);

    GdmShowXtermFailsafeSession = ve_config_get_bool (config, GDM_KEY_SHOW_XTERM_FAILSAFE);
    GdmShowGnomeFailsafeSession = ve_config_get_bool (config, GDM_KEY_SHOW_GNOME_FAILSAFE);
    GdmShowGnomeChooserSession = ve_config_get_bool (config, GDM_KEY_SHOW_GNOME_CHOOSER);
    GdmShowLastSession = ve_config_get_bool (config, GDM_KEY_SHOW_LAST_SESSION);
    GdmSessionDir = ve_config_get_string (config, GDM_KEY_SESSDIR);
    GdmLocaleFile = ve_config_get_string (config, GDM_KEY_LOCFILE);
    GdmSystemMenu = ve_config_get_bool (config, GDM_KEY_SYSMENU);
    GdmConfigAvailable = ve_config_get_bool (config, GDM_KEY_CONFIG_AVAILABLE);
    GdmHalt = ve_config_get_string (config, GDM_KEY_HALT);
    GdmReboot = ve_config_get_string (config, GDM_KEY_REBOOT);
    GdmSuspend = ve_config_get_string (config, GDM_KEY_SUSPEND);
    GdmConfigurator = ve_config_get_string (config, GDM_KEY_CONFIGURATOR);
    GdmGtkRC = ve_config_get_string (config, GDM_KEY_GTKRC);

    GdmTimedLoginEnable = ve_config_get_bool (config, GDM_KEY_TIMED_LOGIN_ENABLE);
    GdmTimedLoginDelay = ve_config_get_int (config, GDM_KEY_TIMED_LOGIN_DELAY);

    /* Note: TimedLogin here is not gotten out of the config
     * but from the daemon since it's been munged on by the daemon a bit
     * already maybe */
    if (GdmTimedLoginEnable)
      {
        GdmTimedLogin = g_strdup (g_getenv("GDM_TIMED_LOGIN_OK"));
	if (ve_string_empty (GdmTimedLogin))
	  {
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
	  }

	if (GdmTimedLoginDelay < 5)
	  {
	    syslog (LOG_WARNING,
		    _("TimedLoginDelay was less then 5.  I'll just use 5."));
	    GdmTimedLoginDelay = 5;
	  }
      }
    else
      {
        GdmTimedLogin = NULL;
      }
    greeter_current_delay = GdmTimedLoginDelay;

    GdmUse24Clock = ve_config_get_bool (config, GDM_KEY_USE_24_CLOCK);

    if (GdmXineramaScreen < 0)
      GdmXineramaScreen = 0;
}

static void
setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

static gboolean
greeter_ctrl_handler (GIOChannel *source,
		      GIOCondition cond,
		      gint fd)
{
    gchar buf[PIPE_SIZE];
    gsize len;
    GtkWidget *dlg;
    char *tmp;
    char *session;
    GreeterItemInfo *conversation_info;
    static GnomeCanvasItem *disabled_cover = NULL;
    gchar *language;

    /* If this is not incoming i/o then return */
    if (cond != G_IO_IN) 
      return TRUE;

    /* Read random garbage from i/o channel until STX is found */
    do {
      g_io_channel_read_chars (source, buf, 1, &len, NULL);
      
      if (len != 1)
	return TRUE;
    }  while (buf[0] && buf[0] != STX);

    /* Read opcode */
    g_io_channel_read_chars (source, buf, 1, &len, NULL);

    /* If opcode couldn't be read */
    if (len != 1)
      return TRUE;

    /* Parse opcode */
    switch (buf[0]) {
    case GDM_SETLOGIN:
	/* somebody is trying to fool us this is the user that
	 * wants to log in, and well, we are the gullible kind */
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';
	
	greeter_item_pam_set_user (buf);
	printf ("%c\n", STX);
	fflush (stdout);
	break;
    case GDM_LOGIN:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	greeter_item_pam_prompt (tmp, 32, TRUE, TRUE);
	g_free (tmp);
	break;

    case GDM_PROMPT:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	greeter_item_pam_prompt (tmp, 128, TRUE, FALSE);
	g_free (tmp);
	break;

    case GDM_NOECHO:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	greeter_item_pam_prompt (tmp, 128, FALSE, FALSE);
	g_free (tmp);
	break;

    case GDM_MSG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';
	tmp = ve_locale_to_utf8 (buf);
	greeter_item_pam_message (tmp);
	g_free (tmp);
	printf ("%c\n", STX);
	fflush (stdout);

	break;

    case GDM_ERRBOX:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	greeter_item_pam_error (tmp);
	g_free (tmp);
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_ERRDLG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	tmp = ve_locale_to_utf8 (buf);
	dlg = gtk_message_dialog_new (NULL /* parent */,
				      GTK_DIALOG_MODAL /* flags */,
				      GTK_MESSAGE_ERROR,
				      GTK_BUTTONS_OK,
				      "%s",
				      tmp);
	g_free (tmp);

	gdm_wm_center_window (GTK_WINDOW (dlg));

	gdm_wm_no_login_focus_push ();
	gtk_dialog_run (GTK_DIALOG (dlg));
	gtk_widget_destroy (dlg);
	gdm_wm_no_login_focus_pop ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_SESS:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	session = greeter_session_lookup (tmp);
	g_free (tmp);
	tmp = ve_locale_from_utf8 (session);
	printf ("%c%s\n", STX, tmp);
	fflush (stdout);
	g_free (session);
	g_free (tmp);
	break;

    case GDM_LANG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	buf[len-1] = '\0';
	language = greeter_language_get_language (buf);
	printf ("%c%s\n", STX, language);
	fflush (stdout);
	g_free (language);
	break;

    case GDM_SSESS:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_save_session ())
	  printf ("%cY\n", STX);
	else
	  printf ("%c\n", STX);
	fflush (stdout);
	
	break;

    case GDM_SLANG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_language_get_save_language ())
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);

	break;

    case GDM_RESET:
	/* fall thru to reset */

    case GDM_RESETOK:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	buf[len-1] = '\0';

	conversation_info = greeter_lookup_id ("pam-conversation");
	
	if (conversation_info)
	  {
	    tmp = ve_locale_to_utf8 (buf);
	    g_object_set (G_OBJECT (conversation_info->item),
			  "text", tmp,
			  NULL);
	    g_free (tmp);
	  }

	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_QUIT:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	greeter_item_timed_stop ();
	greeter_item_pam_leftover_messages ();

	gdk_flush ();

	printf ("%c\n", STX);
	fflush (stdout);

	/* screw gtk_main_quit, we want to make sure we definately die */
	_exit (EXIT_SUCCESS);
	break;

    case GDM_GNOMESESS:
	{
		char *sess;
		GString *str = g_string_new (NULL);

		do {
			g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
			buf[len-1] = '\0';
			tmp = ve_locale_to_utf8 (buf);
			g_string_append (str, tmp);
			g_free (tmp);
		} while (len == PIPE_SIZE-1);


		sess = greeter_get_gnome_session (str->str);

		g_string_free (str, TRUE);

		tmp = ve_locale_from_utf8 (sess);
		printf ("%c%s\n", STX, tmp);
		fflush (stdout);
		g_free (tmp);

		g_free (sess);
	}
	break;

    case GDM_SGNOMESESS:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_save_gnome_session ())
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);

	break;

    case GDM_STARTTIMER:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	greeter_item_timed_start ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_STOPTIMER:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	greeter_item_timed_stop ();

	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_DISABLE:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	gtk_widget_set_sensitive (window, FALSE);

	if (disabled_cover == NULL)
	  {
	    disabled_cover = gnome_canvas_item_new
		    (gnome_canvas_root (GNOME_CANVAS (canvas)),
		     GNOME_TYPE_CANVAS_RECT,
		     "x1", 0.0,
		     "y1", 0.0,
		     "x2", (double)canvas->allocation.width,
		     "y2", (double)canvas->allocation.height,
		     "fill_color_rgba", (guint)0x00000088,
		     NULL);
	  }

	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_ENABLE:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	gtk_widget_set_sensitive (window, TRUE);

	if (disabled_cover != NULL)
	  {
	    gtk_object_destroy (GTK_OBJECT (disabled_cover));
	    disabled_cover = NULL;
	  }

	printf ("%c\n", STX);
	fflush (stdout);
	break;

    /* These are handled separately so ignore them here and send
     * back a NULL response so that the daemon quits sending them */
    case GDM_NEEDPIC:
    case GDM_READPIC:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_NOFOCUS:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	gdm_wm_no_login_focus_push ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_FOCUS:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	gdm_wm_no_login_focus_pop ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_SAVEDIE:
	g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	/* Set busy cursor */
	setup_cursor (GDK_WATCH);

	gdm_wm_save_wm_order ();

	gdk_flush ();

	_exit (EXIT_SUCCESS);
	
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
  greeter_item_timed_setup ();
  greeter_item_register_action_callback ("language_button",
					 greeter_action_language,
					 window);
  greeter_item_register_action_callback ("disconnect_button",
					 (ActionFunc)gtk_main_quit,
					 NULL);
  greeter_item_system_setup ();
  greeter_item_session_setup ();
}

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

static int
verify_gdm_version (void)
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
    
      gdm_wm_init (0);
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

      setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));
    
      return EXIT_SUCCESS;
    }
  
  if (! DOING_GDM_DEVELOPMENT &&
      gdm_protocol_version == NULL &&
      gdm_version == NULL)
    {
      GtkWidget *dialog;
    
      gdm_wm_init (0);
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
      
      setup_cursor (GDK_LEFT_PTR);

      switch (gtk_dialog_run (GTK_DIALOG (dialog)))
	{
	case RESPONSE_REBOOT:
	  gtk_widget_destroy (dialog);
	  return DISPLAY_REBOOT;
	default:
	  gtk_widget_destroy (dialog);
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
    
      gdm_wm_init (0);
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
      
      setup_cursor (GDK_LEFT_PTR);

      switch (gtk_dialog_run (GTK_DIALOG (dialog)))
	{
	case RESPONSE_RESTART:
	  gtk_widget_destroy (dialog);
	  return DISPLAY_RESTARTGDM;
	case RESPONSE_REBOOT:
	  gtk_widget_destroy (dialog);
	  return DISPLAY_REBOOT;
	default:
	  gtk_widget_destroy (dialog);
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
	gtk_label_set_use_markup
		(GTK_LABEL (GTK_MESSAGE_DIALOG (req)->label), TRUE);

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

static gboolean
string_same (VeConfig *config, const char *cur, const char *key)
{
	char *val = ve_config_get_string (config, key);
	if (strcmp (ve_sure_string (cur), ve_sure_string (val)) == 0) {
		g_free (val);
		return TRUE;
	} else {
		syslog (LOG_ERR, "string not same: cur '%s' val '%s' key '%s'",
			cur, val, key);
		g_free (val);
		return FALSE;
	}
}

static gboolean
bool_same (VeConfig *config, gboolean cur, const char *key)
{
	gboolean val = ve_config_get_bool (config, key);
	if (ve_bool_equal (cur, val)) {
		return TRUE;
	} else {
		syslog (LOG_ERR, "bool not same: cur '%d' val '%d' key '%s'",
			cur, val, key);
		return FALSE;
	}
}

static gboolean
int_same (VeConfig *config, int cur, const char *key)
{
	int val = ve_config_get_int (config, key);
	if (cur == val) {
		return TRUE;
	} else {
		syslog (LOG_ERR, "int not same: cur '%d' val '%d' key '%s'",
			cur, val, key);
		return FALSE;
	}
}


static gboolean
greeter_reread_config (int sig, gpointer data)
{
	VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
	char *theme, *theme_dir;

	theme = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME);
	if (ve_string_empty (theme)) {
		g_free (theme);
		theme = g_strdup ("circles");
	}
	theme_dir = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEME_DIR);
	if (theme_dir == NULL ||
	    ! g_file_test (theme_dir, G_FILE_TEST_IS_DIR)) {
		g_free (theme_dir);
		theme_dir = g_strdup (GREETERTHEMEDIR);
	}

	/* FIXME: The following is evil, we should update on the fly rather
	 * then just restarting */
	/* Also we may not need to check ALL those keys but just a few */
	if (strcmp (theme, GdmGraphicalTheme) != 0 ||
	    strcmp (theme_dir, GdmGraphicalThemeDir) != 0 ||
	     ! string_same (config,
			    GdmGtkRC,
			    GDM_KEY_GTKRC) ||
	     ! int_same (config,
			 GdmXineramaScreen,
			 GDM_KEY_XINERAMASCREEN) ||
	     ! bool_same (config,
			  GdmUseCirclesInEntry,
			  GDM_KEY_ENTRY_CIRCLES) ||
	     ! bool_same (config,
			  GdmShowXtermFailsafeSession,
			  GDM_KEY_SHOW_XTERM_FAILSAFE) ||
	     ! bool_same (config,
			  GdmShowGnomeFailsafeSession,
			  GDM_KEY_SHOW_GNOME_FAILSAFE) ||
	     ! bool_same (config,
			  GdmShowGnomeChooserSession,
			  GDM_KEY_SHOW_GNOME_CHOOSER) ||
	     ! string_same (config,
			    GdmSessionDir,
			    GDM_KEY_SESSDIR) ||
	     ! string_same (config,
			    GdmLocaleFile,
			    GDM_KEY_LOCFILE) ||
	     ! bool_same (config,
			  GdmSystemMenu,
			  GDM_KEY_SYSMENU) ||
	     ! string_same (config,
			    GdmHalt,
			    GDM_KEY_HALT) ||
	     ! string_same (config,
			    GdmReboot,
			    GDM_KEY_REBOOT) ||
	     ! string_same (config,
			    GdmSuspend,
			    GDM_KEY_SUSPEND) ||
	     ! string_same (config,
			    GdmConfigurator,
			    GDM_KEY_CONFIGURATOR) ||
	     ! bool_same (config,
			  GdmTimedLoginEnable,
			  GDM_KEY_TIMED_LOGIN_ENABLE) ||
	     ! int_same (config,
			 GdmTimedLoginDelay,
			 GDM_KEY_TIMED_LOGIN_DELAY)) {
		syslog (LOG_ERR, "something not same: "
			"theme '%s' theme_dir '%s'",
			theme, theme_dir);
		/* Set busy cursor */
		setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		_exit (DISPLAY_RESTARTGREETER);
	}

	if ( ! bool_same (config,
			  GdmUse24Clock,
			  GDM_KEY_USE_24_CLOCK)) {
		GdmUse24Clock = ve_config_get_bool (config,
						    GDM_KEY_USE_24_CLOCK);
		greeter_item_clock_update ();
	}

	g_free (theme);
	g_free (theme_dir);

	return TRUE;
}

static void
greeter_done (int sig)
{
    _exit (EXIT_SUCCESS);
}


static char *
get_theme_file (const char *in, char **theme_dir)
{
  char *file, *dir, *info, *s;
  VeConfig *config;

  if (in == NULL)
    in = "circles";

  *theme_dir = NULL;

  if (g_path_is_absolute (in))
    {
      dir = g_strdup (in);
    }
  else
    {
      dir = NULL;
      if (DOING_GDM_DEVELOPMENT)
        {
	  if (access (in, F_OK) == 0)
	    {
	      dir = g_strdup (in);
	    }
	  else
	    {
              dir = g_build_filename ("themes", in, NULL);
	      if (access (dir, F_OK) != 0)
	        {
	          g_free (dir);
	          dir = NULL;
	        }
	    }
	}
      if (dir == NULL)
        dir = g_build_filename (GdmGraphicalThemeDir, in, NULL);
    }

  *theme_dir = dir;

  info = g_build_filename (dir, "GdmGreeterTheme.desktop", NULL);
  if (access (info, F_OK) != 0) {
	  g_free (info);
	  info = g_build_filename (dir, "GdmGreeterTheme.info", NULL);
  }
  if (access (info, F_OK) != 0)
    {
      char *base = g_path_get_basename (in);
      /* just guess the name, we have no info about the theme at
       * this point */
      g_free (info);
      file = g_strdup_printf ("%s/%s.xml", dir, base);
      g_free (base);
      return file;
    }

  config = ve_config_new (info);

  s = ve_config_get_translated_string (config, "GdmGreeterTheme/Greeter");
  if (s == NULL || s[0] == '\0')
    {
      g_free (s);
      s = g_strdup_printf ("%s.xml", in);
    }

  file = g_build_filename (dir, s, NULL);
  g_free (s);

  return file;
}

/* Not to look too shaby on Xinerama setups */
static void
setup_background_color (void)
{
  GdkColormap *colormap;
  GdkColor color;
  VeConfig *config = ve_config_get (GDM_CONFIG_FILE);
  char *bg_color = ve_config_get_string (config, GDM_KEY_BACKGROUNDCOLOR);

  if (bg_color == NULL ||
      bg_color[0] == '\0' ||
      ! gdk_color_parse (bg_color, &color))
    {
      gdk_color_parse ("#007777", &color);
    }

  g_free (bg_color);

  colormap = gdk_drawable_get_colormap
	  (gdk_get_default_root_window ());
  /* paranoia */
  if (colormap != NULL)
    {
      gboolean success;
      gdk_error_trap_push ();

      gdk_colormap_alloc_colors (colormap, &color, 1,
				 FALSE, TRUE, &success);
      gdk_window_set_background (gdk_get_default_root_window (), &color);
      gdk_window_clear (gdk_get_default_root_window ());

      gdk_flush ();
      gdk_error_trap_pop ();
    }
}

int
main (int argc, char *argv[])
{
  struct sigaction hup;
  struct sigaction term;
  sigset_t mask;
  GIOChannel *ctrlch;
  GError *error;
  GreeterItemInfo *root;
  char *theme_file;
  char *theme_dir;
  int r;

  if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
    DOING_GDM_DEVELOPMENT = TRUE;

  openlog ("gdmgreeter", LOG_PID, LOG_DAEMON);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  greeter_parse_config ();

  setlocale (LC_ALL, "");

  gtk_init (&argc, &argv);

  /* Should be a watch already, but anyway */
  setup_cursor (GDK_WATCH);

  if ( ! ve_string_empty (GdmGtkRC))
    gtk_rc_parse (GdmGtkRC);
  
  gdm_wm_screen_init (GdmXineramaScreen);
  
  r = verify_gdm_version ();
  if (r != 0)
    return r;

  greeter_session_init ();
  greeter_language_init ();

  ve_signal_add (SIGHUP, greeter_reread_config, NULL);

  hup.sa_handler = ve_signal_notify;
  hup.sa_flags = 0;
  sigemptyset(&hup.sa_mask);
  sigaddset (&hup.sa_mask, SIGCHLD);
  
  if (sigaction (SIGHUP, &hup, NULL) < 0) 
    g_error (_("main: Error setting up HUP signal handler"));

  term.sa_handler = greeter_done;
  term.sa_flags = 0;
  sigemptyset(&term.sa_mask);
  sigaddset (&term.sa_mask, SIGCHLD);
  
  if (sigaction (SIGINT, &term, NULL) < 0) 
    g_error (_("main: Error setting up INT signal handler"));
  
  if (sigaction (SIGTERM, &term, NULL) < 0) 
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
    g_io_channel_set_encoding (ctrlch, NULL, NULL);
    g_io_channel_set_buffered (ctrlch, FALSE);
    g_io_add_watch (ctrlch, 
		    G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		    (GIOFunc) greeter_ctrl_handler,
		    NULL);
    g_io_channel_unref (ctrlch);
  }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  if (DOING_GDM_DEVELOPMENT)
    g_signal_connect (G_OBJECT (window), "key_press_event",
		      G_CALLBACK (key_press_event), NULL);
  
  canvas = gnome_canvas_new_aa ();
  GTK_WIDGET_UNSET_FLAGS (canvas, GTK_CAN_FOCUS);
  gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas),
				  0.0, 0.0,
				  (double) gdm_wm_screen.width,
				  (double) gdm_wm_screen.height);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (window), 
			       gdm_wm_screen.width, 
			       gdm_wm_screen.height);
  gtk_container_add (GTK_CONTAINER (window), canvas);

  if (g_getenv ("GDM_THEME") != NULL)
    {
      g_free (GdmGraphicalTheme);
      GdmGraphicalTheme = g_strdup (g_getenv ("GDM_THEME"));
    }
  if (DOING_GDM_DEVELOPMENT &&
      g_getenv ("GDM_FAKE_TIMED") != NULL)
    {
      g_free (GdmTimedLogin);
      GdmTimedLogin = g_strdup ("fake");
    }

  theme_file = get_theme_file (GdmGraphicalTheme, &theme_dir);
  
  error = NULL;
  root = greeter_parse (theme_file, theme_dir,
			GNOME_CANVAS (canvas), 
			gdm_wm_screen.width,
			gdm_wm_screen.height,
			&error);

    if (root == NULL &&
	g_getenv ("GDM_THEME") != NULL &&
	DOING_GDM_DEVELOPMENT)
      {
        GtkWidget *dialog;

        gdm_wm_init (0);
        gdm_wm_focus_new_windows (TRUE);
    
        dialog = gtk_message_dialog_new (NULL /* parent */,
				         GTK_DIALOG_MODAL /* flags */,
				         GTK_MESSAGE_ERROR,
				         GTK_BUTTONS_OK,
				         _("There was an error loading the "
					   "theme %s"),
					 g_getenv ("GDM_THEME"));
    
        gtk_widget_show_all (dialog);
        gdm_wm_center_window (GTK_WINDOW (dialog));

        setup_cursor (GDK_LEFT_PTR);
    
        gtk_dialog_run (GTK_DIALOG (dialog));

        exit(1);
      }

  /* Try circles.xml */
  if (root == NULL)
    {
      g_free (theme_file);
      g_free (theme_dir);
      theme_file = get_theme_file ("circles", &theme_dir);
      root = greeter_parse (theme_file, theme_dir,
			    GNOME_CANVAS (canvas), 
			    gdm_wm_screen.width,
			    gdm_wm_screen.height,
			    NULL);
    }

  if (greeter_lookup_id ("user-pw-entry") == NULL)
    {
      GtkWidget *dialog;

      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("The theme for the graphical greeter "
					 "is corrupt.  It does not contain "
					 "definition for the username/password "
					 "entry element."));

      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);

      gtk_dialog_run (GTK_DIALOG (dialog));

      root = NULL;
    }

  /* FIXME: beter information should be printed */
  if (DOING_GDM_DEVELOPMENT && root == NULL)
    {
      g_warning ("No theme could be loaded");
      exit(1);
    }

  if (root == NULL)
    {
      GtkWidget *dialog;

      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("There was an error loading the "
					 "theme, and the default theme "
					 "also could not have been loaded, "
					 "I will attempt to start the "
					 "standard greeter"));
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));

      execl (EXPANDED_BINDIR "/gdmlogin", EXPANDED_BINDIR "/gdmlogin", NULL);
      execlp ("gdmlogin", "gdmlogin", NULL);

      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("I could not start the standard "
					 "greeter.  This display will abort "
					 "and you may have to login another "
					 "way and fix the installation of "
					 "gdm"));
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));

      _exit (DISPLAY_ABORT);
    }

  greeter_layout (root, GNOME_CANVAS (canvas));
  
  greeter_setup_items ();

  gtk_widget_show_all (window);
  gtk_window_move (GTK_WINDOW (window), gdm_wm_screen.x, gdm_wm_screen.y);
  gtk_widget_show_now (window);

  setup_background_color ();

  /* can it ever happen that it'd be NULL here ??? */
  if (window->window != NULL)
    {
      gdm_wm_init (GDK_WINDOW_XWINDOW (window->window));

      /* Run the focus, note that this will work no matter what
       * since gdm_wm_init will set the display to the gdk one
       * if it fails */
      gdm_wm_focus_window (GDK_WINDOW_XWINDOW (window->window));
    }

  if (session_dir_whacked_out)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("Your session directory is missing or empty!\n\n"
					 "There are two available sessions you can use, but\n"
					 "you should log in and correct the gdm configuration."));
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
    }

  if (g_getenv ("GDM_WHACKED_GREETER_CONFIG") != NULL)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("The configuration file contains an invalid command\n"
					 "line for the login dialog, and thus I ran the\n"
					 "default command.  Please fix your configuration."));
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
  }

  /* There was no config file */
  if (used_defaults)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = gtk_message_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("The configuration was not found.  GDM is using\n"
					 "defaults to run this session.  You should log in\n"
					 "and create a configuration file with the GDM\n"
					 "configuration program."));
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
    }

  gdm_wm_restore_wm_order ();

  setup_cursor (GDK_LEFT_PTR);

  gtk_main ();

  return 0;
}
