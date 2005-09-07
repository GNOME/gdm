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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "vicious.h"
#include "viciousui.h"

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
static char *greeter_DefaultWelcome_key = GDM_KEY_DEFAULT_WELCOME;
static char *greeter_DefaultWelcomeBacktest_key = GDM_KEY_DEFAULT_WELCOME_BACKTEST;
static char *greeter_Welcome_key = GDM_KEY_WELCOME;

GtkWidget *window;
GtkWidget *canvas;

gboolean GDM_IS_LOCAL = FALSE;
char *GdmGraphicalTheme = NULL;
char *GdmGraphicalThemes = NULL;
char *GdmGraphicalThemeDir = NULL;
gboolean GdmGraphicalThemeRand = FALSE;
int GdmXineramaScreen = 0;
gboolean GdmShowGnomeFailsafeSession = FALSE;
gboolean GdmShowXtermFailsafeSession = FALSE;
gboolean GdmShowLastSession = FALSE;
gchar *GdmSessionDir = NULL;
gchar *GdmDefaultSession = NULL;
gchar *GdmLocaleFile = NULL;
gchar *GdmHalt = NULL;
gchar *GdmReboot = NULL;
gchar *GdmSuspend = NULL;
gchar *GdmConfigurator = NULL;
gboolean GdmHaltFound = FALSE;
gboolean GdmRebootFound = FALSE;
gboolean GdmSuspendFound = FALSE;
gboolean GdmConfiguratorFound = FALSE;
gboolean GdmSystemMenu = TRUE;
gboolean GdmConfigAvailable = TRUE;
gboolean GdmChooserButton = TRUE;
gboolean GdmTimedLoginEnable;
gboolean GdmUse24Clock;
gchar *GdmGlobalFaceDir;
gchar *GdmDefaultFace;
gint  GdmIconMaxHeight;
gint  GdmIconMaxWidth;
gchar *GdmTimedLogin;
gchar *GdmGtkRC;
gchar *GdmGtkTheme;
gint GdmTimedLoginDelay;
int GdmMinimalUID;
gchar *GdmInclude = NULL;
gchar *GdmExclude = NULL;
gboolean GdmIncludeAll;
gboolean GdmAllowRoot;
gboolean GdmAllowRemoteRoot;
static gboolean GdmDefaultWelcome;
static gboolean GdmDefaultWelcomeBacktest;
gchar *GdmWelcome;
gchar *GdmServAuthDir;
gchar *GdmInfoMsgFile;
gchar *GdmInfoMsgFont;
gint GdmFlexiReapDelayMinutes;
gboolean GdmSoundOnLoginReady;
gboolean GdmSoundOnLoginSuccess;
gboolean GdmSoundOnLoginFailure;
gchar *GdmSoundOnLoginReadyFile;
gchar *GdmSoundOnLoginSuccessFile;
gchar *GdmSoundOnLoginFailureFile;
gchar *GdmSoundProgram;

gboolean GdmUseCirclesInEntry = FALSE;
gboolean GdmUseInvisibleInEntry = FALSE;

static gchar *config_file;
static gboolean used_defaults = FALSE;
gint greeter_current_delay = 0;

/* FIXME: hack */
GreeterItemInfo *welcome_string_info = NULL;

extern gboolean session_dir_whacked_out;
extern gboolean require_quarter;

gboolean greeter_probably_login_prompt = FALSE;

/* If in random theme mode then grab a random theme from those selected */
static char *
get_random_theme ()
{
    char **vec;
	char *themes_list;
	char *theme;
    int size;
	int i;

    VeConfig *config = ve_config_get (config_file);
	themes_list = ve_config_get_string (config, GDM_KEY_GRAPHICAL_THEMES);

    if (ve_string_empty (themes_list))
        return NULL;

    vec = g_strsplit (themes_list, GDM_DELIMITER_THEMES, -1);
    if (vec == NULL)
        return NULL;

	/* Get Number of elements in vector */
    for (size = 0; vec[size] != NULL; size++) {}

	/* Get Random Theme from list */
	srand ( time(NULL) );
	i = rand() % size;
	theme = g_strdup (vec[i]);
    g_strfreev (vec);

    return theme;
}

static void 
greeter_parse_config (void)
{
    VeConfig *config;

    if (!g_file_test (config_file, G_FILE_TEST_EXISTS))
      {
	syslog (LOG_ERR, _("%s: No configuration file: %s. Using defaults."), 
		"greeter_parse_config", config_file);
	used_defaults = TRUE;
      }

    if (ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
	    greeter_Welcome_key = GDM_KEY_REMOTEWELCOME;
	    greeter_DefaultWelcome_key = GDM_KEY_DEFAULT_REMOTEWELCOME;
            greeter_DefaultWelcomeBacktest_key = GDM_KEY_DEFAULT_REMOTEWELCOME_BACKTEST;
    } else {
	    greeter_Welcome_key = GDM_KEY_WELCOME;
	    greeter_DefaultWelcome_key = GDM_KEY_DEFAULT_WELCOME;
            greeter_DefaultWelcomeBacktest_key = GDM_KEY_DEFAULT_WELCOME_BACKTEST;
    }

    config = ve_config_get (config_file);

	GdmGraphicalThemeRand = ve_config_get_bool (config,
	                                            GDM_KEY_GRAPHICAL_THEME_RAND);

	if (GdmGraphicalThemeRand)
		GdmGraphicalTheme = get_random_theme(); 
	else
		GdmGraphicalTheme = ve_config_get_string (config,
		                                          GDM_KEY_GRAPHICAL_THEME);

	if (GdmGraphicalTheme == NULL || GdmGraphicalTheme[0] == '\0')
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
    GdmUseInvisibleInEntry = ve_config_get_bool (config, GDM_KEY_ENTRY_INVISIBLE);

    GdmShowXtermFailsafeSession = ve_config_get_bool (config, GDM_KEY_SHOW_XTERM_FAILSAFE);
    GdmShowGnomeFailsafeSession = ve_config_get_bool (config, GDM_KEY_SHOW_GNOME_FAILSAFE);
    GdmShowLastSession = ve_config_get_bool (config, GDM_KEY_SHOW_LAST_SESSION);
    GdmSessionDir = ve_config_get_string (config, GDM_KEY_SESSDIR);
    GdmDefaultSession = ve_config_get_string (config, GDM_KEY_DEFAULTSESSION);
    GdmLocaleFile = ve_config_get_string (config, GDM_KEY_LOCFILE);
    GdmSystemMenu = ve_config_get_bool (config, GDM_KEY_SYSMENU);
    GdmConfigAvailable = ve_config_get_bool (config, GDM_KEY_CONFIG_AVAILABLE);
    GdmChooserButton = ve_config_get_bool (config, GDM_KEY_CHOOSER_BUTTON);
    GdmHalt = ve_config_get_string (config, GDM_KEY_HALT);
    GdmReboot = ve_config_get_string (config, GDM_KEY_REBOOT);
    GdmSuspend = ve_config_get_string (config, GDM_KEY_SUSPEND);
    GdmConfigurator = ve_config_get_string (config, GDM_KEY_CONFIGURATOR);
    GdmGtkRC = ve_config_get_string (config, GDM_KEY_GTKRC);
    GdmGtkTheme = ve_config_get_string (config, GDM_KEY_GTK_THEME);
    GdmServAuthDir = ve_config_get_string (config, GDM_KEY_SERVAUTH);
    GdmInfoMsgFile = ve_config_get_string (config, GDM_KEY_INFO_MSG_FILE);
    GdmInfoMsgFont = ve_config_get_string (config, GDM_KEY_INFO_MSG_FONT);

    GdmHaltFound = gdm_working_command_exists (GdmHalt);
    GdmRebootFound = gdm_working_command_exists (GdmReboot);
    GdmSuspendFound = gdm_working_command_exists (GdmSuspend);
    GdmConfiguratorFound = gdm_working_command_exists (GdmConfigurator);

    GdmWelcome = ve_config_get_translated_string (config, greeter_Welcome_key);
    GdmDefaultWelcome = ve_config_get_bool (config, greeter_DefaultWelcome_key);

    /*
     * For backwards compatibility.  If DefaultWelcome isn't in config file, then
     * assume GdmDefaultWelcome is FALSE unless the string matches the default
     */
    GdmDefaultWelcomeBacktest = ve_config_get_bool (config, greeter_DefaultWelcomeBacktest_key);
    if (GdmDefaultWelcomeBacktest == FALSE) {
            if (strcmp (ve_sure_string (GdmWelcome), GDM_DEFAULT_WELCOME_MSG) == 0)
                GdmDefaultWelcome == TRUE;
            else if (strcmp (ve_sure_string (GdmWelcome), GDM_DEFAULT_REMOTEWELCOME_MSG) == 0)
                GdmDefaultWelcome == TRUE;
            else
                GdmDefaultWelcome = FALSE;
    }

    /* Replace default welcome message with one specified in the config file, if
     * use default is set to no */
    if (GdmDefaultWelcome && strcmp (greeter_Welcome_key, GDM_KEY_WELCOME) == 0) {
	    g_free (GdmWelcome);
	    GdmWelcome = g_strdup (_(GDM_DEFAULT_WELCOME_MSG));
    } else if (GdmDefaultWelcome && strcmp (greeter_Welcome_key, GDM_KEY_REMOTEWELCOME) == 0) {
	    g_free (GdmWelcome);
	    GdmWelcome = g_strdup (_(GDM_DEFAULT_REMOTEWELCOME_MSG));
    }

    GdmTimedLoginEnable = ve_config_get_bool (config, GDM_KEY_TIMED_LOGIN_ENABLE);
    GdmIncludeAll = ve_config_get_bool (config, GDM_KEY_INCLUDEALL);
    GdmInclude = ve_config_get_string (config, GDM_KEY_INCLUDE);
    GdmExclude = ve_config_get_string (config, GDM_KEY_EXCLUDE);
    GdmMinimalUID = ve_config_get_int (config, GDM_KEY_MINIMALUID);
    GdmAllowRoot = ve_config_get_bool (config, GDM_KEY_ALLOWROOT);
    GdmAllowRemoteRoot = ve_config_get_bool (config, GDM_KEY_ALLOWREMOTEROOT);
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
		    _("TimedLoginDelay was less than 5.  I'll just use 5."));
	    GdmTimedLoginDelay = 5;
	  }
      }
    else
      {
        GdmTimedLogin = NULL;
      }
    greeter_current_delay = GdmTimedLoginDelay;

    GdmFlexiReapDelayMinutes = ve_config_get_int (config, GDM_KEY_FLEXI_REAP_DELAY_MINUTES);

    GdmUse24Clock = gdm_common_select_time_format (config);

    GdmIconMaxWidth = ve_config_get_int (config, GDM_KEY_ICONWIDTH);
    GdmIconMaxHeight = ve_config_get_int (config, GDM_KEY_ICONHEIGHT);
    GdmGlobalFaceDir = ve_config_get_string (config, GDM_KEY_FACEDIR);
    GdmDefaultFace = ve_config_get_string (config, GDM_KEY_FACE);

    GdmSoundProgram = ve_config_get_string (config, GDM_KEY_SOUND_PROGRAM);
    GdmSoundOnLoginReady = ve_config_get_bool (config,
	                       GDM_KEY_SOUND_ON_LOGIN_READY);
    GdmSoundOnLoginSuccess = ve_config_get_bool (config,
	                         GDM_KEY_SOUND_ON_LOGIN_SUCCESS);
    GdmSoundOnLoginFailure = ve_config_get_bool (config,
	                         GDM_KEY_SOUND_ON_LOGIN_FAILURE);
    GdmSoundOnLoginReadyFile = ve_config_get_string (config,
	                           GDM_KEY_SOUND_ON_LOGIN_READY_FILE);
    GdmSoundOnLoginSuccessFile = ve_config_get_string (config,
	                      GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
    GdmSoundOnLoginFailureFile = ve_config_get_string (config,
	                      GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

    if (GdmXineramaScreen < 0)
      GdmXineramaScreen = 0;
    if (GdmIconMaxWidth < 0) GdmIconMaxWidth = 128;
    if (GdmIconMaxHeight < 0) GdmIconMaxHeight = 128;
    if (ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
	    GDM_IS_LOCAL = FALSE;
    } else {
	    GDM_IS_LOCAL = TRUE;
    }
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
		gdm_common_login_sound (GdmSoundProgram,
					GdmSoundOnLoginReadyFile,
					GdmSoundOnLoginReady);
		greeter_probably_login_prompt = TRUE;
	} else {
		greeter_probably_login_prompt = FALSE;
	}
	greeter_item_pam_prompt (tmp, PW_ENTRY_SIZE, TRUE);
	g_free (tmp);
	break;

    case GDM_NOECHO:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL);
	buf[len-1] = '\0';

	tmp = ve_locale_to_utf8 (buf);
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

	if (greeter_save_session() == GTK_RESPONSE_CANCEL) {
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
	if (greeter_language_get_save_language() == GTK_RESPONSE_CANCEL)
	    printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
	else
	    printf ("%c%s\n", STX, language);
	fflush (stdout);
	g_free (language);
	break;

    case GDM_SSESS:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_save_session() == GTK_RESPONSE_YES)
	  printf ("%cY\n", STX);
	else
	  printf ("%c\n", STX);
	fflush (stdout);
	
	break;

    case GDM_SLANG:
        g_io_channel_read_chars (source, buf, PIPE_SIZE-1, &len, NULL); /* Empty */

	if (greeter_language_get_save_language() == GTK_RESPONSE_YES)
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
   GreeterItemInfo *entry_info = greeter_lookup_id ("user-pw-entry");
   if (entry_info && entry_info->item &&
       GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
       GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
     {
       GtkWidget *entry;
       entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
       greeter_item_pam_login (GTK_ENTRY (entry), entry_info);
     }
}

static void
greeter_action_cancel (GreeterItemInfo *info,
                       gpointer         user_data)
{
   greeter_item_ulist_disable ();
   printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CANCEL);
   fflush (stdout);
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
                                "You have probably just upgraded gdm.  "
                                "Please restart the gdm daemon or the computer."),
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
                               "You have probably just upgraded gdm. "
                               "Please restart the gdm daemon or the computer."),
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
                               "You have probably just upgraded gdm.  "
                               "Please restart the gdm daemon or the computer."),
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

static gboolean
greeter_reread_config (int sig, gpointer data)
{
	VeConfig *config = ve_config_get (config_file);
	char *theme, *theme_dir;
	char *str;

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
	     ! gdm_common_string_same (config, GdmGtkRC, GDM_KEY_GTKRC) ||
	     ! gdm_common_string_same (config, GdmGtkTheme, GDM_KEY_GTK_THEME) ||
	     ! gdm_common_int_same (config, GdmXineramaScreen, GDM_KEY_XINERAMASCREEN) ||
	     ! gdm_common_bool_same (config, GdmUseCirclesInEntry, GDM_KEY_ENTRY_CIRCLES) ||
	     ! gdm_common_bool_same (config, GdmUseInvisibleInEntry, GDM_KEY_ENTRY_INVISIBLE) ||
	     ! gdm_common_bool_same (config, GdmShowXtermFailsafeSession, GDM_KEY_SHOW_XTERM_FAILSAFE) ||
	     ! gdm_common_bool_same (config, GdmShowGnomeFailsafeSession, GDM_KEY_SHOW_GNOME_FAILSAFE) ||
	     ! gdm_common_bool_same (config, GdmIncludeAll, GDM_KEY_INCLUDEALL) ||
	     ! gdm_common_string_same (config, GdmInclude, GDM_KEY_INCLUDE) ||
	     ! gdm_common_string_same (config, GdmExclude, GDM_KEY_EXCLUDE) ||
	     ! gdm_common_string_same (config, GdmSessionDir, GDM_KEY_SESSDIR) ||
	     ! gdm_common_string_same (config, GdmLocaleFile, GDM_KEY_LOCFILE) ||
	     ! gdm_common_bool_same (config, GdmSystemMenu, GDM_KEY_SYSMENU) ||
	     ! gdm_common_string_same (config, GdmHalt, GDM_KEY_HALT) ||
	     ! gdm_common_string_same (config, GdmReboot, GDM_KEY_REBOOT) ||
	     ! gdm_common_string_same (config, GdmSuspend, GDM_KEY_SUSPEND) ||
	     ! gdm_common_string_same (config, GdmConfigurator, GDM_KEY_CONFIGURATOR) ||
	     ! gdm_common_string_same (config, GdmInfoMsgFile, GDM_KEY_INFO_MSG_FILE) ||
	     ! gdm_common_string_same (config, GdmInfoMsgFont, GDM_KEY_INFO_MSG_FONT) ||
	     ! gdm_common_bool_same (config, GdmConfigAvailable, GDM_KEY_CONFIG_AVAILABLE) ||
	     ! gdm_common_bool_same (config, GdmChooserButton, GDM_KEY_CHOOSER_BUTTON) ||
	     ! gdm_common_bool_same (config, GdmTimedLoginEnable, GDM_KEY_TIMED_LOGIN_ENABLE) ||
	     ! gdm_common_int_same (config, GdmTimedLoginDelay, GDM_KEY_TIMED_LOGIN_DELAY)) {

		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		_exit (DISPLAY_RESTARTGREETER);
	}

	GdmSoundProgram = ve_config_get_string (config, GDM_KEY_SOUND_PROGRAM);
	GdmSoundOnLoginReady = ve_config_get_bool (config,
	                  GDM_KEY_SOUND_ON_LOGIN_READY);
	GdmSoundOnLoginSuccess = ve_config_get_bool (config,
	                  GDM_KEY_SOUND_ON_LOGIN_SUCCESS);
	GdmSoundOnLoginFailure = ve_config_get_bool (config,
	                  GDM_KEY_SOUND_ON_LOGIN_FAILURE);
	GdmSoundOnLoginReadyFile = ve_config_get_string (config,
	                             GDM_KEY_SOUND_ON_LOGIN_READY_FILE);
	GdmSoundOnLoginSuccessFile = ve_config_get_string (config,
	                             GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE);
	GdmSoundOnLoginFailureFile = ve_config_get_string (config,
	                             GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE);

	if ( gdm_common_select_time_format (config) != GdmUse24Clock) {
		GdmUse24Clock = gdm_common_select_time_format (config);
		greeter_item_clock_update ();
	}

        if (! gdm_common_bool_same (config, GdmDefaultWelcome, greeter_DefaultWelcome_key) ||
	   (!GdmDefaultWelcome && 
	    ! gdm_common_string_same (config, GdmWelcome, greeter_Welcome_key))) {

		GdmWelcome = ve_config_get_string (config, greeter_Welcome_key);
		GdmDefaultWelcome = ve_config_get_bool (config, greeter_DefaultWelcome_key);
                GdmDefaultWelcomeBacktest = ve_config_get_bool (config, greeter_DefaultWelcomeBacktest_key);

                /*
                 * For backwards compatibility.  If DefaultWelcome isn't in config file, then
                 * assume GdmDefaultWelcome is FALSE unless the string matches the default
                 */
                if (GdmDefaultWelcomeBacktest == FALSE) {
                        if (strcmp (ve_sure_string (GdmWelcome), GDM_DEFAULT_WELCOME_MSG) == 0)
                                GdmDefaultWelcome == TRUE;
                        else if (strcmp (ve_sure_string (GdmWelcome), GDM_DEFAULT_REMOTEWELCOME_MSG) == 0)
                                GdmDefaultWelcome == TRUE;
                        else
                                GdmDefaultWelcome = FALSE;
                }

		if (GdmDefaultWelcome) {
			if (strcmp (greeter_Welcome_key, GDM_KEY_WELCOME) == 0) {
				g_free (GdmWelcome);
				GdmWelcome = g_strdup (_(GDM_DEFAULT_WELCOME_MSG));
			} else if (strcmp (greeter_Welcome_key, GDM_KEY_REMOTEWELCOME) == 0) {
				g_free (GdmWelcome);
				GdmWelcome = g_strdup (_(GDM_DEFAULT_REMOTEWELCOME_MSG));
			}

			if (welcome_string_info != NULL) {
				g_free (welcome_string_info->data.text.orig_text);
				welcome_string_info->data.text.orig_text = g_strdup (GdmWelcome);
				greeter_item_update_text (welcome_string_info);
			}

		} else {
			str = ve_config_get_translated_string (config, greeter_Welcome_key);

			if (strcmp (ve_sure_string (str), ve_sure_string (GdmWelcome)) != 0 && 
			    welcome_string_info != NULL) {
					g_free (welcome_string_info->data.text.orig_text);
					welcome_string_info->data.text.orig_text = g_strdup (GdmWelcome);
					greeter_item_update_text (welcome_string_info);
			
			} else {
				g_free (str);
			}
		}

		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		_exit (DISPLAY_RESTARTGREETER);
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
  g_free (info);

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
  VeConfig *config = ve_config_get (config_file);
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
	if (GdmFlexiReapDelayMinutes > 0 &&
	    ((time (NULL) - last_reap_delay) / 60) > GdmFlexiReapDelayMinutes) {
		_exit (DISPLAY_REMANAGE);
	}
	return TRUE;
}


static void
gdm_kill_thingies (void)
{
	/* Empty kill thingies */
	return;
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
  struct sigaction hup;
  struct sigaction term;
  sigset_t mask;
  GIOChannel *ctrlch;
  GError *error;
  GreeterItemInfo *root;
  char *theme_file;
  char *theme_dir;
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

  config_file = gdm_common_get_config_file ();
  if (config_file == NULL) {
	   g_print (_("Could not access GDM configuration file.\n"));
	   exit (EXIT_FAILURE);
  }

  greeter_parse_config ();

  gtk_init (&argc, &argv);

  /* Should be a watch already, but anyway */
  gdm_common_setup_cursor (GDK_WATCH);

  if( ! ve_string_empty (GdmGtkRC))
	  gtk_rc_parse (GdmGtkRC);

  gdm_gtk_theme = g_getenv ("GDM_GTK_THEME");
  if (ve_string_empty (gdm_gtk_theme))
	  gdm_gtk_theme = GdmGtkTheme;

  if ( ! ve_string_empty (gdm_gtk_theme)) {
	  gdm_set_theme (gdm_gtk_theme);
  }

  gdm_wm_screen_init (GdmXineramaScreen);
  
  r = verify_gdm_version ();
  if (r != 0)
    return r;

  greeter_session_init ();

  ve_signal_add (SIGHUP, greeter_reread_config, NULL);

  hup.sa_handler = ve_signal_notify;
  hup.sa_flags = 0;
  sigemptyset(&hup.sa_mask);
  sigaddset (&hup.sa_mask, SIGCHLD);
  
  if (sigaction (SIGHUP, &hup, NULL) < 0) 
    gdm_common_abort (_("%s: Error setting up %s signal handler: %s"), "main", "HUP", strerror (errno));

  term.sa_handler = greeter_done;
  term.sa_flags = 0;
  sigemptyset(&term.sa_mask);
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

    if G_UNLIKELY (root == NULL)
      {
        GtkWidget *dialog;
	char *s;
	char *tmp;

        gdm_wm_init (0);
        gdm_wm_focus_new_windows (TRUE);
    
	tmp = ve_filename_to_utf8 (ve_sure_string (GdmGraphicalTheme));
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
				    "installation of gdm"),
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

  setup_background_color ();

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

  /* There was no config file */
  if G_UNLIKELY (used_defaults)
    {
      GtkWidget *dialog;

      gdm_wm_focus_new_windows (TRUE);

      dialog = ve_hig_dialog_new (NULL /* parent */,
				  GTK_DIALOG_MODAL /* flags */,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  _("No configuration was found"),
				  _("The configuration was not found.  GDM is using "
				    "defaults to run this session.  You should log in "
				    "and create a configuration file with the GDM "
				    "configuration application."));

      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));

      gdm_common_setup_cursor (GDK_LEFT_PTR);

      gdm_wm_no_login_focus_push ();
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      gdm_wm_no_login_focus_pop ();
    }

    /* if a flexiserver, reap self after some time */
  if (GdmFlexiReapDelayMinutes > 0 &&
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

  gdm_common_show_info_msg (GdmInfoMsgFile, GdmInfoMsgFont);

  gdm_common_setup_cursor (GDK_LEFT_PTR);

  gtk_main ();

  return 0;
}
