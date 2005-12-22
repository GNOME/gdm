/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#include "config.h"

#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "vicious.h"

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"

#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_parser.h"
#include "greeter_geometry.h"
#include "greeter_item_clock.h"
#include "greeter_item_pam.h"
#include "greeter_item_ulist.h"
#include "greeter_item_customlist.h"
#include "greeter_item_capslock.h"
#include "greeter_item_timed.h"
#include "greeter_events.h"
#include "greeter_action_language.h"
#include "greeter_session.h"
#include "greeter_system.h"

gboolean DOING_GDM_DEVELOPMENT = FALSE;

GtkWidget *window;
GtkWidget *canvas;

gboolean GDM_IS_LOCAL = FALSE;

gint greeter_current_delay = 0;
static gboolean ignore_buttons = FALSE;

/* FIXME: hack */
GreeterItemInfo *welcome_string_info = NULL;

extern gboolean session_dir_whacked_out;
extern gboolean require_quarter;

gboolean greeter_probably_login_prompt = FALSE;

void
greeter_ignore_buttons (gboolean val)
{
   ignore_buttons = val;
}

/* If in random theme mode then grab a random theme from those selected */
static char *
get_random_theme ()
{
    char **vec;
	char *themes_list;
	char *theme;
    int size;
	int i;

    themes_list = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEMES);

    if (ve_string_empty (themes_list))
        return NULL;

    vec = g_strsplit (themes_list, GDM_DELIMITER_THEMES, -1);
    if (vec == NULL)
        return NULL;

	/* Get Number of elements in vector */
    for (size = 0; vec[size] != NULL; size++) {}

	/* Get Random Theme from list */
	srand (time (NULL));
	i = rand () % size;
	theme = g_strdup (vec[i]);
    g_strfreev (vec);

    return theme;
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
	greeter_item_ulist_enable ();
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_PROMPT:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	if (tmp != NULL && strcmp (tmp, _("Username:")) == 0) {
		gdm_common_login_sound (gdm_config_get_string (GDM_KEY_SOUND_PROGRAM),
					gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE),
					gdm_config_get_bool (GDM_KEY_SOUND_ON_LOGIN));
		greeter_probably_login_prompt = TRUE;
	} else {
		greeter_probably_login_prompt = FALSE;
	}
	greeter_ignore_buttons (FALSE);
	greeter_item_pam_prompt (tmp, PW_ENTRY_SIZE, TRUE);
	g_free (tmp);
	break;

    case GDM_NOECHO:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
	greeter_ignore_buttons (FALSE);
	greeter_item_pam_prompt (tmp, PW_ENTRY_SIZE, FALSE);
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
	dlg = ve_hig_dialog_new (NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 tmp,
				 "");
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

	if (greeter_save_session () == GTK_RESPONSE_CANCEL) {
	     printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
	} else {
	    tmp = ve_locale_from_utf8 (session);
	    printf ("%c%s\n", STX, tmp);
	    g_free (tmp);
	}
	fflush (stdout);
	g_free (session);
	break;

    case GDM_LANG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */
	buf[len-1] = '\0';
	language = greeter_language_get_language (buf);
	if (greeter_language_get_save_language () == GTK_RESPONSE_CANCEL)
	    printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
	else
	    printf ("%c%s\n", STX, language);
	fflush (stdout);
	g_free (language);
	break;

    case GDM_SSESS:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_save_session () == GTK_RESPONSE_YES)
	  printf ("%cY\n", STX);
	else
	  printf ("%c\n", STX);
	fflush (stdout);
	
	break;

    case GDM_SLANG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_language_get_save_language () == GTK_RESPONSE_YES)
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
	greeter_ignore_buttons (FALSE);
	break;

    case GDM_QUIT:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	greeter_item_timed_stop ();

	if (require_quarter) {
		/* we should be now fine for focusing new windows */
		gdm_wm_focus_new_windows (TRUE);

		dlg = ve_hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_OK,
					 /* translators:  This is a nice and evil eggie text, translate
					  * to your favourite currency */
					 _("Please insert 25 cents "
					   "to log in."),
					 "");
		gdm_wm_center_window (GTK_WINDOW (dlg));

		gdm_wm_no_login_focus_push ();
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		gdm_wm_no_login_focus_pop ();
	}

	greeter_item_pam_leftover_messages ();

	gdk_flush ();

	printf ("%c\n", STX);
	fflush (stdout);

	/* screw gtk_main_quit, we want to make sure we definately die */
	_exit (EXIT_SUCCESS);
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
	gdm_common_setup_cursor (GDK_WATCH);

	gdm_wm_save_wm_order ();

	gdk_flush ();

	_exit (EXIT_SUCCESS);

    case GDM_QUERY_CAPSLOCK:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_is_capslock_on ())
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);

	break;
	
    default:
	gdm_common_abort ("Unexpected greeter command received: '%c'", buf[0]);
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
greeter_action_ok (GreeterItemInfo *info,
                       gpointer         user_data)
{
   if (ignore_buttons == FALSE)
     {
       GreeterItemInfo *entry_info = greeter_lookup_id ("user-pw-entry");
       if (entry_info && entry_info->item &&
           GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
           GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
         {
           GtkWidget *entry;
           entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
           greeter_ignore_buttons (TRUE);
           greeter_item_pam_login (GTK_ENTRY (entry), entry_info);
         }
    }
}

static void
greeter_action_cancel (GreeterItemInfo *info,
                       gpointer         user_data)
{
   if (ignore_buttons == FALSE)
     {
       greeter_item_ulist_disable ();
       greeter_ignore_buttons (TRUE);
       printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CANCEL);
       fflush (stdout);
     }
}

static void
greeter_setup_items (void)
{
  greeter_item_clock_setup ();
  greeter_item_pam_setup ();

  /* This will query the daemon for pictures through stdin/stdout! */
  greeter_item_ulist_setup ();

  greeter_item_capslock_setup (window);
  greeter_item_timed_setup ();
  greeter_item_register_action_callback ("ok_button",
					 greeter_action_ok,
					 window);
  greeter_item_register_action_callback ("cancel_button",
					 greeter_action_cancel,
					 window);
  greeter_item_register_action_callback ("language_button",
					 greeter_action_language,
					 window);
  greeter_item_register_action_callback ("disconnect_button",
					 (ActionFunc)gtk_main_quit,
					 NULL);
  greeter_item_system_setup ();
  greeter_item_session_setup ();

  /* Setup the custom widgets */
  greeter_item_customlist_setup ();
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
      gchar *msg;
    
      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      msg =  g_strdup_printf (_("The greeter version (%s) does not match the daemon "
                                "version.  "
                                "You have probably just upgraded GDM.  "
                                "Please restart the GDM daemon or the computer."),
                              VERSION);
    
      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("Cannot start the greeter"),
				  msg);
      g_free (msg);
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    
      return EXIT_SUCCESS;
    }
  
  if (! DOING_GDM_DEVELOPMENT &&
      gdm_protocol_version == NULL &&
      gdm_version == NULL)
    {
      GtkWidget *dialog;
      gchar *msg;
    
      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
                               "version. "
                               "You have probably just upgraded GDM. "
                               "Please restart the GDM daemon or the computer."),
                             VERSION);
    
      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_WARNING,
				  GTK_BUTTONS_NONE,
				  _("Cannot start the greeter"),
				  msg);
      g_free (msg);
			  
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			      _("Reboot"),
			      RESPONSE_REBOOT,
			      GTK_STOCK_CLOSE,
			      RESPONSE_CLOSE,
			      NULL);
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
      
      gdm_common_setup_cursor (GDK_LEFT_PTR);

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
      gchar *msg;
    
      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
                               "version (%s).  "
                               "You have probably just upgraded GDM.  "
                               "Please restart the GDM daemon or the computer."),
                             VERSION, gdm_version);
    
      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_WARNING,
				  GTK_BUTTONS_NONE,
				  _("Cannot start the greeter"),
				  msg);
      g_free (msg);

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
      
      gdm_common_setup_cursor (GDK_LEFT_PTR);

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

static void
gdm_set_welcomemsg (void)
{
	char *welcomemsg = gdm_get_welcomemsg ();

	if (welcome_string_info->data.text.orig_text != NULL)
		g_free (welcome_string_info->data.text.orig_text);

	welcome_string_info->data.text.orig_text = welcomemsg;
	greeter_item_update_text (welcome_string_info);
}


static gboolean
greeter_reread_config (int sig, gpointer data)
{
	/* FIXME: The following is evil, we should update on the fly rather
	 * then just restarting */
	/* Also we may not need to check ALL those keys but just a few */
	if (gdm_config_reload_string (GDM_KEY_GRAPHICAL_THEME) ||
	    gdm_config_reload_string (GDM_KEY_GRAPHICAL_THEME_DIR) ||
	    gdm_config_reload_string (GDM_KEY_GTKRC) ||
	    gdm_config_reload_string (GDM_KEY_GTK_THEME) ||
	    gdm_config_reload_int    (GDM_KEY_XINERAMA_SCREEN) ||
	    gdm_config_reload_bool   (GDM_KEY_ENTRY_CIRCLES) ||
	    gdm_config_reload_bool   (GDM_KEY_ENTRY_INVISIBLE) ||
	    gdm_config_reload_bool   (GDM_KEY_SHOW_XTERM_FAILSAFE) ||
	    gdm_config_reload_bool   (GDM_KEY_SHOW_GNOME_FAILSAFE) ||
	    gdm_config_reload_bool   (GDM_KEY_INCLUDE_ALL) ||
	    gdm_config_reload_string (GDM_KEY_INCLUDE) ||
	    gdm_config_reload_string (GDM_KEY_EXCLUDE) ||
	    gdm_config_reload_string (GDM_KEY_SESSION_DESKTOP_DIR) ||
	    gdm_config_reload_string (GDM_KEY_LOCALE_FILE) ||
	    gdm_config_reload_bool   (GDM_KEY_SYSTEM_MENU) ||
	    gdm_config_reload_string (GDM_KEY_HALT) ||
	    gdm_config_reload_string (GDM_KEY_REBOOT) ||
	    gdm_config_reload_string (GDM_KEY_SUSPEND) ||
	    gdm_config_reload_string (GDM_KEY_CONFIGURATOR) ||
	    gdm_config_reload_string (GDM_KEY_INFO_MSG_FILE) ||
	    gdm_config_reload_string (GDM_KEY_INFO_MSG_FONT) ||
	    gdm_config_reload_bool   (GDM_KEY_CONFIG_AVAILABLE) ||
	    gdm_config_reload_bool   (GDM_KEY_CHOOSER_BUTTON) ||
	    gdm_config_reload_bool   (GDM_KEY_TIMED_LOGIN_ENABLE) ||
	    gdm_config_reload_int    (GDM_KEY_TIMED_LOGIN_DELAY)) {

		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		_exit (DISPLAY_RESTARTGREETER);
	}

	gdm_config_reload_string (GDM_KEY_SOUND_PROGRAM);
        gdm_config_reload_bool   (GDM_KEY_SOUND_ON_LOGIN);
        gdm_config_reload_bool   (GDM_KEY_SOUND_ON_LOGIN_SUCCESS);
        gdm_config_reload_bool   (GDM_KEY_SOUND_ON_LOGIN_FAILURE);
        gdm_config_reload_string (GDM_KEY_SOUND_ON_LOGIN_FILE);
        gdm_config_reload_string (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
        gdm_config_reload_string (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);
	gdm_config_reload_string (GDM_KEY_USE_24_CLOCK);

	if (gdm_config_reload_string (GDM_KEY_WELCOME) ||
	    gdm_config_reload_bool   (GDM_KEY_DEFAULT_WELCOME) ||
	    gdm_config_reload_string (GDM_KEY_REMOTE_WELCOME) ||
	    gdm_config_reload_bool   (GDM_KEY_DEFAULT_REMOTE_WELCOME)) {

		gdm_set_welcomemsg ();

		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		_exit (DISPLAY_RESTARTGREETER);
	}

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
	  if (g_access (in, F_OK) == 0)
	    {
	      dir = g_strdup (in);
	    }
	  else
	    {
              dir = g_build_filename ("themes", in, NULL);
	      if (g_access (dir, F_OK) != 0)
	        {
	          g_free (dir);
	          dir = NULL;
	        }
	    }
	}
      if (dir == NULL)
        dir = g_build_filename (gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME_DIR), in, NULL);
    }

  *theme_dir = dir;

  info = g_build_filename (dir, "GdmGreeterTheme.desktop", NULL);
  if (g_access (info, F_OK) != 0) {
	  g_free (info);
	  info = g_build_filename (dir, "GdmGreeterTheme.info", NULL);
  }
  if (g_access (info, F_OK) != 0)
    {
      char *base = g_path_get_basename (in);
      /* just guess the name, we have no info about the theme at
       * this point */
      g_free (info);
      file = g_strdup_printf ("%s/%s.xml", dir, base);
      g_free (base);
      return file;
    }

  s    = gdm_get_theme_greeter (info, in);
  file = g_build_filename (dir, s, NULL);

  g_free (info);
  g_free (s);

  return file;
}

/* The reaping stuff */
static time_t last_reap_delay = 0;

static gboolean
delay_reaping (GSignalInvocationHint *ihint,
	       guint	           n_param_values,
	       const GValue	  *param_values,
	       gpointer		   data)
{
	last_reap_delay = time (NULL);
	return TRUE;
}      

static gboolean
reap_flexiserver (gpointer data)
{
	int reapminutes = gdm_config_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES);

	if (reapminutes > 0 &&
	    ((time (NULL) - last_reap_delay) / 60) > reapminutes) {
		_exit (DISPLAY_REMANAGE);
	}
	return TRUE;
}

static gboolean
gdm_event (GSignalInvocationHint *ihint,
           guint                n_param_values,
           const GValue        *param_values,
           gpointer             data)
{
        GdkEvent *event;

        /* HAAAAAAAAAAAAAAAAACK */
        /* Since the user has not logged in yet and may have left/right
         * mouse buttons switched, we just translate every right mouse click
         * to a left mouse click */
        if (n_param_values != 2 ||
            !G_VALUE_HOLDS (&param_values[1], GDK_TYPE_EVENT))
          return FALSE;

        event = g_value_get_boxed (&param_values[1]);
        if ((event->type == GDK_BUTTON_PRESS ||
             event->type == GDK_2BUTTON_PRESS ||
             event->type == GDK_3BUTTON_PRESS ||
             event->type == GDK_BUTTON_RELEASE)
            && event->button.button == 3)
                event->button.button = 1;

        /* Support Ctrl-U for blanking the username/password entry */
        if (event->type == GDK_KEY_PRESS &&
            (event->key.state & GDK_CONTROL_MASK) &&
            (event->key.keyval == GDK_u ||
             event->key.keyval == GDK_U)) {

		GreeterItemInfo *entry_info = greeter_lookup_id ("user-pw-entry");
		if (entry_info && entry_info->item &&
		    GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
		    GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
		{
			GtkWidget *entry;
			entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
			gtk_entry_set_text (GTK_ENTRY (entry), "");
		}
	}

        return TRUE;
}

int
main (int argc, char *argv[])
{
  char *bg_color;
  struct sigaction hup;
  struct sigaction term;
  sigset_t mask;
  GIOChannel *ctrlch;
  GError *error;
  GreeterItemInfo *root;
  const gchar *theme_name;
  char *theme_file;
  char *theme_dir;
  gchar *gdm_graphical_theme;
  const char *gdm_gtk_theme;
  guint sid;
  int r;

  if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
    DOING_GDM_DEVELOPMENT = TRUE;

  openlog ("gdmgreeter", LOG_PID, LOG_DAEMON);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  setlocale (LC_ALL, "");

  if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
	   GDM_IS_LOCAL = FALSE;
  else
	   GDM_IS_LOCAL = TRUE;

  gtk_init (&argc, &argv);

  /* Should be a watch already, but anyway */
  gdm_common_setup_cursor (GDK_WATCH);

  if ( ! ve_string_empty (gdm_config_get_string (GDM_KEY_GTKRC)))
	  gtk_rc_parse (gdm_config_get_string (GDM_KEY_GTKRC));

  gdm_gtk_theme = g_getenv ("GDM_GTK_THEME");
  if (ve_string_empty (gdm_gtk_theme))
	  gdm_gtk_theme = gdm_config_get_string (GDM_KEY_GTK_THEME);

  if ( ! ve_string_empty (gdm_gtk_theme)) {
	  gdm_set_theme (gdm_gtk_theme);
  }

  gdm_wm_screen_init (gdm_config_get_int (GDM_KEY_XINERAMA_SCREEN));
  
  r = verify_gdm_version ();
  if (r != 0)
    return r;

  /* Load the background as early as possible so GDM does not leave  */
  /* the background unfilled.   The cursor should be a watch already */
  /* but just in case */
  bg_color = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME_COLOR);
  /* If a graphical theme color does not exist fallback to the plain color */
  if (ve_string_empty (bg_color)) {
    bg_color = gdm_config_get_string (GDM_KEY_BACKGROUND_COLOR);
  }
  setup_background_color (bg_color);
  greeter_session_init ();

  ve_signal_add (SIGHUP, greeter_reread_config, NULL);

  hup.sa_handler = ve_signal_notify;
  hup.sa_flags = 0;
  sigemptyset (&hup.sa_mask);
  sigaddset (&hup.sa_mask, SIGCHLD);
  
  if (sigaction (SIGHUP, &hup, NULL) < 0) 
    gdm_common_abort (_("%s: Error setting up %s signal handler: %s"), "main", "HUP", strerror (errno));

  term.sa_handler = greeter_done;
  term.sa_flags = 0;
  sigemptyset (&term.sa_mask);
  sigaddset (&term.sa_mask, SIGCHLD);
  
  if G_UNLIKELY (sigaction (SIGINT, &term, NULL) < 0) 
    gdm_common_abort (_("%s: Error setting up %s signal handler: %s"), "main", "INT", strerror (errno));
  
  if G_UNLIKELY (sigaction (SIGTERM, &term, NULL) < 0) 
    gdm_common_abort (_("%s: Error setting up %s signal handler: %s"), "main", "TERM", strerror (errno));
  
  sigemptyset (&mask);
  sigaddset (&mask, SIGTERM);
  sigaddset (&mask, SIGHUP);
  sigaddset (&mask, SIGINT);

  if G_UNLIKELY (sigprocmask (SIG_UNBLOCK, &mask, NULL) == -1) 
	  gdm_common_abort (_("Could not set signal mask!"));

  /* ignore SIGCHLD */
  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD);

  if G_UNLIKELY (sigprocmask (SIG_BLOCK, &mask, NULL) == -1) 
	  gdm_common_abort (_("Could not set signal mask!"));
  
  if G_LIKELY (! DOING_GDM_DEVELOPMENT) {
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

  if G_UNLIKELY (DOING_GDM_DEVELOPMENT)
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
     gdm_graphical_theme = g_strdup (g_getenv ("GDM_THEME"));
  else if (gdm_config_get_bool (GDM_KEY_GRAPHICAL_THEME_RAND))
     gdm_graphical_theme = get_random_theme ();
  else
     gdm_graphical_theme = gdm_config_get_string (GDM_KEY_GRAPHICAL_THEME);

  theme_file = get_theme_file (gdm_graphical_theme, &theme_dir);
  
  error = NULL;
  root = greeter_parse (theme_file, theme_dir,
			GNOME_CANVAS (canvas), 
			gdm_wm_screen.width,
			gdm_wm_screen.height,
			&error);

    if G_UNLIKELY (root == NULL)
      {
        GtkWidget *dialog;
	char *s;
	char *tmp;

        gdm_wm_init (0);
        gdm_wm_focus_new_windows (TRUE);
    
	tmp = ve_filename_to_utf8 (ve_sure_string (gdm_graphical_theme));
	s = g_strdup_printf (_("There was an error loading the "
			       "theme %s"), tmp);
	g_free (tmp);
        dialog = ve_hig_dialog_new (NULL /* parent */,
				    GTK_DIALOG_MODAL /* flags */,
				    GTK_MESSAGE_ERROR,
				    GTK_BUTTONS_OK,
				    s,
				    (error && error->message) ? error->message : "");
	g_free (s);
    
        gtk_widget_show_all (dialog);
        gdm_wm_center_window (GTK_WINDOW (dialog));

        gdm_common_setup_cursor (GDK_LEFT_PTR);
    
        gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (DOING_GDM_DEVELOPMENT)
	  {
	    exit (1);
	  }
      }

  if G_UNLIKELY (error)
    g_clear_error (&error);

  /* Try circles.xml */
  if G_UNLIKELY (root == NULL)
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

  if G_UNLIKELY (root != NULL && greeter_lookup_id ("user-pw-entry") == NULL)
    {
      GtkWidget *dialog;

      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("The greeter theme is corrupt"),
				  _("The theme does not contain "
				    "definition for the username/password "
				    "entry element."));

      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);

      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      root = NULL;
    }

  /* FIXME: beter information should be printed */
  if G_UNLIKELY (DOING_GDM_DEVELOPMENT && root == NULL)
    {
      g_warning ("No theme could be loaded");
      exit (1);
    }

  if G_UNLIKELY (root == NULL)
    {
      GtkWidget *dialog;

      gdm_wm_init (0);
      gdm_wm_focus_new_windows (TRUE);
    
      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("There was an error loading the "
				    "theme, and the default theme "
				    "could not be loaded. "
				    "Attempting to start the "
				    "standard greeter"),
				  "");
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      execl (EXPANDED_LIBEXECDIR "/gdmlogin", EXPANDED_LIBEXECDIR "/gdmlogin", NULL);
      execlp ("gdmlogin", "gdmlogin", NULL);

      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("The GTK+ greeter could not be started.  "
				    "This display will abort and you may "
				    "have to login another way and fix the "
				    "installation of GDM"),
				  "");
    
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);
    
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      _exit (DISPLAY_ABORT);
    }

  greeter_layout (root, GNOME_CANVAS (canvas));
  
  greeter_setup_items ();

  gdm_setup_blinking ();

  gtk_widget_show_all (window);
  gtk_window_move (GTK_WINDOW (window), gdm_wm_screen.x, gdm_wm_screen.y);
  gtk_widget_show_now (window);

  /* can it ever happen that it'd be NULL here ??? */
  if G_UNLIKELY (window->window != NULL)
    {
      gdm_wm_init (GDK_WINDOW_XWINDOW (window->window));

      /* Run the focus, note that this will work no matter what
       * since gdm_wm_init will set the display to the gdk one
       * if it fails */
      gdm_wm_focus_window (GDK_WINDOW_XWINDOW (window->window));
    }

  if G_UNLIKELY (session_dir_whacked_out)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("Session directory is missing"),
				  _("Your session directory is missing or empty!  "
				    "There are two available sessions you can use, but "
				    "you should log in and correct the gdm configuration."));
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
    }

  if G_UNLIKELY (g_getenv ("GDM_WHACKED_GREETER_CONFIG") != NULL)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("Configuration is not correct"),
				  _("The configuration file contains an invalid command "
				    "line for the login dialog, so running the "
				    "default command.  Please fix your configuration."));

      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
  }

  /* if a flexiserver, reap self after some time */
  if (gdm_config_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES) > 0 &&
      ! ve_string_empty (g_getenv ("GDM_FLEXI_SERVER")) &&
      /* but don't reap Xnest flexis */
      ve_string_empty (g_getenv ("GDM_PARENT_DISPLAY")))
    {
      sid = g_signal_lookup ("activate",
			     GTK_TYPE_MENU_ITEM);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  delay_reaping,
				  NULL /* data */,
				  NULL /* destroy_notify */);

      sid = g_signal_lookup ("key_press_event",
			     GTK_TYPE_WIDGET);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  delay_reaping,
				  NULL /* data */,
				  NULL /* destroy_notify */);

      sid = g_signal_lookup ("button_press_event",
			     GTK_TYPE_WIDGET);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  delay_reaping,
				  NULL /* data */,
				  NULL /* destroy_notify */);

      last_reap_delay = time (NULL);
      g_timeout_add (60*1000, reap_flexiserver, NULL);
    }

  sid = g_signal_lookup ("event",
                               GTK_TYPE_WIDGET);
  g_signal_add_emission_hook (sid,
                              0 /* detail */,
                              gdm_event,
                              NULL /* data */,
                              NULL /* destroy_notify */);

  gdm_wm_restore_wm_order ();

  gdm_common_show_info_msg (gdm_config_get_string (GDM_KEY_INFO_MSG_FILE),
     gdm_config_get_string (GDM_KEY_INFO_MSG_FONT));

  gdm_common_setup_cursor (GDK_LEFT_PTR);
  gdm_post_display_launch ();
  gtk_main ();

  return 0;
}
