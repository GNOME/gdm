/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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
#include <unistd.h>
#include <signal.h>
#include <errno.h>

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

#include "gdm-common.h"
#include "gdm-socket-protocol.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmsession.h"
#include "gdmlanguages.h"

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
#include "greeter_session.h"
#include "greeter_system.h"

gboolean DOING_GDM_DEVELOPMENT = FALSE;

GtkWidget *window;
GtkWidget *canvas;

gboolean GDM_IS_LOCAL          = FALSE;
static gboolean ignore_buttons = FALSE;
gboolean GdmHaltFound          = FALSE;
gboolean GdmRebootFound        = FALSE;
gboolean *GdmCustomCmdsFound   = NULL;
gboolean GdmAnyCustomCmdsFound = FALSE;
gboolean GdmSuspendFound       = FALSE;
gboolean GdmConfiguratorFound  = FALSE;

/* FIXME: hack */
GreeterItemInfo *welcome_string_info = NULL;
GreeterItemInfo *root = NULL;

extern gboolean session_dir_whacked_out;
extern gboolean require_quarter;
extern gint gdm_timed_delay;
extern GtkButton *gtk_ok_button;
extern GtkButton *gtk_start_again_button;

gboolean greeter_probably_login_prompt = FALSE;
static gboolean first_prompt = TRUE;

static void process_operation (guchar opcode, const gchar *args);

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

	theme = NULL;

	gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEMES, &themes_list);

	if (ve_string_empty (themes_list)) {
		goto out;
	}

	vec = g_strsplit (themes_list, GDM_DELIMITER_THEMES, -1);
	if (vec == NULL) {
		goto out;
	}

	/* Get Number of elements in vector */
	for (size = 0; vec[size] != NULL; size++) {}

	/* Get Random Theme from list */
	srand (time (NULL));
	i = rand () % size;
	theme = g_strdup (vec[i]);
	g_strfreev (vec);

 out:
	g_free (themes_list);

	return theme;
}

static gboolean
greeter_ctrl_handler (GIOChannel *source,
		      GIOCondition cond,
		      gint fd)
{
	gchar buf[PIPE_SIZE];
	gchar *p;
	gsize len;

	/* If this is not incoming i/o then return */
	if (cond != G_IO_IN)
		return TRUE;

	/* Read random garbage from i/o channel until first STX is found */
	do {
		g_io_channel_read_chars (source, buf, 1, &len, NULL);

		if (len != 1)
			return TRUE;
	} while (buf[0] && buf[0] != STX);

	memset (buf, '\0', sizeof (buf));
	if (g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &len, NULL) !=
	    G_IO_STATUS_NORMAL)
		return TRUE;

	p = memchr (buf, STX, len);
	if (p != NULL) {
		len = p - buf;
		g_io_channel_seek_position (source, -((sizeof (buf) - 1) - len), G_SEEK_CUR, NULL);
		memset (buf + len, '\0', (sizeof (buf) - 1) - len);
	}
	buf[len - 1] = '\0';

	process_operation ((guchar) buf[0], buf + 1);
	return TRUE;
}

static GtkWidget *
hig_dialog_new (GtkWindow      *parent,
		GtkDialogFlags flags,
		GtkMessageType type,
		GtkButtonsType buttons,
		const gchar    *primary_message,
		const gchar    *secondary_message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
		                         GTK_DIALOG_DESTROY_WITH_PARENT,
		                         type,
		                         buttons,
		                         "%s", primary_message);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                  "%s", secondary_message);

	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 14);

  	return dialog;
}

static void
process_operation (guchar       op_code,
		   const gchar *args)
{
	GtkWidget *dlg;
	char *tmp;
	char *session;
	GreeterItemInfo *conversation_info;
	static GnomeCanvasItem *disabled_cover = NULL;
	gint lookup_status = SESSION_LOOKUP_SUCCESS;
	gchar *firstmsg = NULL;
	gchar *secondmsg = NULL;
	gint save_session = GTK_RESPONSE_NO;

	/* Parse opcode */
	switch (op_code) {
	case GDM_SETLOGIN:
		/* somebody is trying to fool us this is the user that
		 * wants to log in, and well, we are the gullible kind */

		greeter_item_pam_set_user (args);
		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_PROMPT:
		tmp = ve_locale_to_utf8 (args);
		if (tmp != NULL && strcmp (tmp, _("Username:")) == 0) {
			char *sound_prog;
			char *sound_file;
			gboolean sound_on_login;

			gdm_settings_client_get_string (GDM_KEY_SOUND_PROGRAM, &sound_prog);
			gdm_settings_client_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE, &sound_file);
			gdm_settings_client_get_boolean (GDM_KEY_SOUND_ON_LOGIN, &sound_on_login);

			gdm_common_login_sound (sound_prog, sound_file, sound_on_login);
			g_free (sound_file);
			g_free (sound_prog);

			greeter_probably_login_prompt = TRUE;
		}
		if (gtk_ok_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);

		if (gtk_start_again_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), !first_prompt);

		first_prompt = FALSE;

		greeter_ignore_buttons (FALSE);

		greeter_item_pam_prompt (tmp, PW_ENTRY_SIZE, TRUE);
		g_free (tmp);
		break;

	case GDM_NOECHO:
		tmp = ve_locale_to_utf8 (args);

		greeter_probably_login_prompt = FALSE;

		if (gtk_ok_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);

		if (gtk_start_again_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), !first_prompt);

		first_prompt = FALSE;

		greeter_ignore_buttons (FALSE);
		greeter_item_pam_prompt (tmp, PW_ENTRY_SIZE, FALSE);
		g_free (tmp);

		break;

	case GDM_MSG:
		tmp = ve_locale_to_utf8 (args);
		greeter_item_pam_message (tmp);
		g_free (tmp);
		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_ERRBOX:
		tmp = ve_locale_to_utf8 (args);
		greeter_item_pam_error (tmp);
		g_free (tmp);

		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_ERRDLG:
		/* we should be now fine for focusing new windows */
		gdm_wm_focus_new_windows (TRUE);

		tmp = ve_locale_to_utf8 (args);
		dlg = hig_dialog_new (NULL /* parent */,
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
		tmp = ve_locale_to_utf8 (args);
		session = gdm_session_lookup (tmp, &lookup_status);
		if (lookup_status != SESSION_LOOKUP_SUCCESS) {
			switch (lookup_status) {
			case SESSION_LOOKUP_PREFERRED_MISSING:
				firstmsg = g_strdup_printf (_("Do you wish to make %s the default for "
							      "future sessions?"),
							    gdm_session_name (tmp));
				secondmsg = g_strdup_printf (_("Your preferred session type %s is not "
							       "installed on this computer."),
							     gdm_session_name (gdm_get_default_session ()));
				save_session = gdm_wm_query_dialog (firstmsg, secondmsg,
								    _("Make _Default"), _("Just _Log In"), TRUE);

				g_free (firstmsg);
				g_free (secondmsg);
				gdm_set_save_session (save_session);
				break;

			case SESSION_LOOKUP_DEFAULT_MISMATCH:
				firstmsg = g_strdup_printf (_("Do you wish to make %s the default for "
							      "future sessions?"),
							    gdm_session_name (session));
				secondmsg = g_strdup_printf (_("You have chosen %s for this "
							       "session, but your default "
							       "setting is %s."),
							     gdm_session_name (session),
							     gdm_session_name (tmp));
				save_session = gdm_wm_query_dialog (firstmsg, secondmsg,
								    _("Make _Default"), _("Just For _This Session"), TRUE);

				g_free (firstmsg);
				g_free (secondmsg);
				gdm_set_save_session (save_session);
				break;
			case SESSION_LOOKUP_USE_SWITCHDESK:
				firstmsg = g_strdup_printf (_("You have chosen %s for this "
							      "session"),
							    gdm_session_name (session));
				secondmsg = g_strdup_printf (_("If you wish to make %s "
							       "the default for future sessions, "
							       "run the 'switchdesk' utility "
							       "(System->Desktop Switching Tool from "
							       "the panel menu)."),
							     gdm_session_name (session));
				gdm_wm_message_dialog (firstmsg, secondmsg);
				g_free (firstmsg);
				g_free (secondmsg);
				break;

			default:
				break;
			}
		}
		g_free (tmp);
		if (gdm_get_save_session () == GTK_RESPONSE_CANCEL) {
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
		gdm_lang_op_lang (args);
		break;

	case GDM_SSESS:
		if (gdm_get_save_session () == GTK_RESPONSE_YES)
			printf ("%cY\n", STX);
		else
			printf ("%c\n", STX);
		fflush (stdout);

		break;

	case GDM_SLANG:
		gdm_lang_op_slang (args);
		break;

	case GDM_SETLANG:
		gdm_lang_op_setlang (args);
		break;

	case GDM_ALWAYS_RESTART:
		gdm_lang_op_always_restart (args);
		break;

	case GDM_RESET:
		/* fall thru to reset */

	case GDM_RESETOK:

		if (gtk_ok_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);
		if (gtk_start_again_button != NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), FALSE);

		first_prompt = TRUE;

		conversation_info = greeter_lookup_id ("pam-conversation");

		if (conversation_info) {
			tmp = ve_locale_to_utf8 (args);
			g_object_set (G_OBJECT (conversation_info->item),
				      "text", tmp,
				      NULL);
			g_free (tmp);
		}

		printf ("%c\n", STX);
		fflush (stdout);
		greeter_ignore_buttons (FALSE);
		greeter_item_ulist_enable ();

		break;

	case GDM_QUIT:
		greeter_item_timed_stop ();

		if (require_quarter) {
			/* we should be now fine for focusing new windows */
			gdm_wm_focus_new_windows (TRUE);

			dlg = hig_dialog_new (NULL /* parent */,
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

		if (greeter_show_only_background (root)) {
			GdkPixbuf *background;
			int width, height;

			gtk_window_get_size (GTK_WINDOW (window), &width, &height);
			background = gdk_pixbuf_get_from_drawable (NULL, gtk_widget_get_root_window(window), NULL, 0, 0, 0, 0 ,width, height);
			if (background) {
				gdm_common_set_root_background (background);
				g_object_unref (background);
			}
		}

		printf ("%c\n", STX);
		fflush (stdout);

		/* screw gtk_main_quit, we want to make sure we definately die */
		_exit (EXIT_SUCCESS);
		break;

	case GDM_STARTTIMER:
		greeter_item_timed_start ();

		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_STOPTIMER:
		greeter_item_timed_stop ();

		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_DISABLE:
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
		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_NOFOCUS:
		gdm_wm_no_login_focus_push ();

		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_FOCUS:
		gdm_wm_no_login_focus_pop ();

		printf ("%c\n", STX);
		fflush (stdout);
		break;

	case GDM_SAVEDIE:
		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();

		gdk_flush ();

		_exit (EXIT_SUCCESS);

	case GDM_QUERY_CAPSLOCK:
		if (greeter_is_capslock_on ())
			printf ("%cY\n", STX);
		else
			printf ("%c\n", STX);
		fflush (stdout);

		break;

	default:
		gdm_common_fail_greeter ("Unexpected greeter command received: '%c'", op_code);
		break;
	}
}

static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *key, gpointer data)
{
	if (DOING_GDM_DEVELOPMENT && (key->keyval == GDK_Escape)) {
		process_operation (GDM_QUIT, NULL);

		return TRUE;
	}

	return FALSE;
}

/*
 * The buttons with these handlers never appear in the F10 menu,
 * so they can make use of callback data.
 */
static void
greeter_ok_handler (GreeterItemInfo *info,
                    gpointer         user_data)
{
	if (ignore_buttons == FALSE) {
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
greeter_cancel_handler (GreeterItemInfo *info,
                        gpointer         user_data)
{
	if (ignore_buttons == FALSE) {
		greeter_item_ulist_unset_selected_user ();
		greeter_item_ulist_disable ();
		greeter_ignore_buttons (TRUE);
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CANCEL);
		fflush (stdout);
	}
}

static void
greeter_language_handler (GreeterItemInfo *info,
                          gpointer         user_data)
{
	gdm_lang_handler (user_data);
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
					       greeter_ok_handler,
					       (gpointer) window);
	greeter_item_register_action_callback ("cancel_button",
					       greeter_cancel_handler,
					       (gpointer) window);
	greeter_item_register_action_callback ("language_button",
					       greeter_language_handler,
					       NULL);
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
	    (g_getenv ("GDM_IS_LOCAL") != NULL)) {
		GtkWidget *dialog;
		gchar *msg;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		msg =  g_strdup_printf (_("The greeter version (%s) does not match the daemon "
					  "version. "
					  "You have probably just upgraded GDM. "
					  "Please restart the GDM daemon or the computer."),
					VERSION);

		dialog = hig_dialog_new (NULL /* parent */,
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
	    gdm_version == NULL) {
		GtkWidget *dialog;
		gchar *msg;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
					 "version. "
					 "You have probably just upgraded GDM. "
					 "Please restart the GDM daemon or the computer."),
				       VERSION);

		dialog = hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Cannot start the greeter"),
					 msg);
		g_free (msg);

		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("Restart Machine"),
					RESPONSE_REBOOT,
					GTK_STOCK_CLOSE,
					RESPONSE_CLOSE,
					NULL);

		gtk_widget_show_all (dialog);
		gdm_wm_center_window (GTK_WINDOW (dialog));

		gdm_common_setup_cursor (GDK_LEFT_PTR);

		switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
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
	      strcmp (gdm_version, VERSION) != 0))) {
		GtkWidget *dialog;
		gchar *msg;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
					 "version (%s).  "
					 "You have probably just upgraded GDM.  "
					 "Please restart the GDM daemon or the computer."),
				       VERSION, gdm_version);

		dialog = hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Cannot start the greeter"),
					 msg);
		g_free (msg);

		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("Restart GDM"),
					RESPONSE_RESTART,
					_("Restart Machine"),
					RESPONSE_REBOOT,
					GTK_STOCK_CLOSE,
					RESPONSE_CLOSE,
					NULL);

		gtk_widget_show_all (dialog);
		gdm_wm_center_window (GTK_WINDOW (dialog));

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), RESPONSE_RESTART);

		gdm_common_setup_cursor (GDK_LEFT_PTR);

		switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
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
	char *welcomemsg = gdm_common_get_welcomemsg ();

	if (welcome_string_info->data.text.orig_text != NULL)
		g_free (welcome_string_info->data.text.orig_text);

	welcome_string_info->data.text.orig_text = welcomemsg;
	greeter_item_update_text (welcome_string_info);
}

static void
greeter_done (int sig)
{
	_exit (EXIT_SUCCESS);
}

static char *
get_theme_greeter (const gchar *file,
		   const char *fallback)
{
	GKeyFile *config;
	gchar *s;

	config = g_key_file_new ();
	if (! g_key_file_load_from_file (config, file, 0, NULL)) {
		return NULL;
	}

	s = g_key_file_get_locale_string (config, "GdmGreeterTheme", "Greeter", NULL, NULL);

	if (s == NULL || s[0] == '\0') {
		g_free (s);
		s = g_strdup_printf ("%s.xml", fallback);
	}

	g_key_file_free (config);

	return s;
}
static char *
get_theme_file (const char *in, char **theme_dir)
{
	char *file, *dir, *info, *s;

	if (in == NULL)
		in = "circles";

	*theme_dir = NULL;

	if (g_path_is_absolute (in)) {
		dir = g_strdup (in);
	} else {
		dir = NULL;
		if (DOING_GDM_DEVELOPMENT) {
			if (g_access (in, F_OK) == 0) {
				dir = g_strdup (in);
			} else {
				dir = g_build_filename ("themes", in, NULL);
				if (g_access (dir, F_OK) != 0) {
					g_free (dir);
					dir = NULL;
				}
			}
		}

		if (dir == NULL) {
			char *graphical_theme_dir;

			gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEME_DIR, &graphical_theme_dir);
			dir = g_build_filename (graphical_theme_dir, in, NULL);
			g_free (graphical_theme_dir);
		}
	}

	*theme_dir = dir;

	info = g_build_filename (dir, "GdmGreeterTheme.desktop", NULL);
	if (g_access (info, F_OK) != 0) {
		g_free (info);
		info = g_build_filename (dir, "GdmGreeterTheme.info", NULL);
	}
	if (g_access (info, F_OK) != 0) {
		char *base = g_path_get_basename (in);
		/* just guess the name, we have no info about the theme at
		 * this point */
		g_free (info);
		file = g_strdup_printf ("%s/%s.xml", dir, base);
		g_free (base);
		return file;
	}

	s = get_theme_greeter (info, in);
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
	int reapminutes;

	gdm_settings_client_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES, &reapminutes);

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
	char *theme_file;
	char *theme_dir;
	char *gdm_graphical_theme;
	char *gdm_gtk_theme;
	guint sid;
	int r;
	gint i;
	int delay;
	gboolean rand_theme;

	if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
		DOING_GDM_DEVELOPMENT = TRUE;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	setlocale (LC_ALL, "");

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
		GDM_IS_LOCAL = FALSE;
	else
		GDM_IS_LOCAL = TRUE;

	/*
	 * gdm_common_atspi_launch () needs gdk initialized.
	 * We cannot start gtk before the registry is running
	 * because the atk-bridge will crash.
	 */
	gdk_init (&argc, &argv);
	if (! DOING_GDM_DEVELOPMENT) {
		gdm_common_atspi_launch ();
	}

	gtk_init (&argc, &argv);

        if (! gdm_settings_client_init (GDMCONFDIR "/gdm.schemas", "/")) {
                exit (1);
        }

	gdm_common_setup_cursor (GDK_WATCH);

	gdm_common_log_init ();

	/*gdm_common_log_set_debug (gdm_settings_client_get_bool (GDM_KEY_DEBUG));*/
	gdm_common_log_set_debug (TRUE);

	{
		char *gtkrc;
		gdm_settings_client_get_string (GDM_KEY_GTKRC, &gtkrc);
		if (gtkrc != NULL) {
			gtk_rc_parse (gtkrc);
		}
		g_free (gtkrc);
	}

	gdm_gtk_theme = g_strdup (g_getenv ("GDM_GTK_THEME"));
	if (ve_string_empty (gdm_gtk_theme)) {
		gdm_settings_client_get_string (GDM_KEY_GTK_THEME, &gdm_gtk_theme);
	}

	if ( ! ve_string_empty (gdm_gtk_theme)) {
		gdm_set_theme (gdm_gtk_theme);
	}
	g_free (gdm_gtk_theme);

	{
		int screen;
		gdm_settings_client_get_int (GDM_KEY_XINERAMA_SCREEN, &screen);
		gdm_wm_screen_init (screen);
	}

	r = verify_gdm_version ();
	if (r != 0)
		return r;

	/* Load the background as early as possible so GDM does not leave  */
	/* the background unfilled.   The cursor should be a watch already */
	/* but just in case */
	gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEMED_COLOR, &bg_color);

	/* If a graphical theme color does not exist fallback to the plain color */
	if (ve_string_empty (bg_color)) {
		g_free (bg_color);
		gdm_settings_client_get_string (GDM_KEY_BACKGROUND_COLOR, &bg_color);
	}
	gdm_common_setup_background_color (bg_color);
	g_free (bg_color);

	greeter_session_init ();

	{
		char *locale_file;
		gdm_settings_client_get_string (GDM_KEY_LOCALE_FILE, &locale_file);
		gdm_lang_initialize_model (locale_file);
		g_free (locale_file);
	}

	hup.sa_handler = ve_signal_notify;
	hup.sa_flags = 0;
	sigemptyset (&hup.sa_mask);
	sigaddset (&hup.sa_mask, SIGCHLD);

	if (sigaction (SIGHUP, &hup, NULL) < 0) {
		gdm_common_fail_greeter ("%s: Error setting up %s signal handler: %s", "main",
					 "HUP", strerror (errno));
	}

	term.sa_handler = greeter_done;
	term.sa_flags = 0;
	sigemptyset (&term.sa_mask);
	sigaddset (&term.sa_mask, SIGCHLD);

	if G_UNLIKELY (sigaction (SIGINT, &term, NULL) < 0) {
		gdm_common_fail_greeter ("%s: Error setting up %s signal handler: %s", "main",
					 "INT", strerror (errno));
	}

	if G_UNLIKELY (sigaction (SIGTERM, &term, NULL) < 0) {
		gdm_common_fail_greeter ("%s: Error setting up %s signal handler: %s", "main",
					 "TERM", strerror (errno));
	}

	sigemptyset (&mask);
	sigaddset (&mask, SIGTERM);
	sigaddset (&mask, SIGHUP);
	sigaddset (&mask, SIGINT);

	if G_UNLIKELY (sigprocmask (SIG_UNBLOCK, &mask, NULL) == -1) {
		gdm_common_fail_greeter ("Could not set signal mask!");
	}

	/* ignore SIGCHLD */
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);

	if G_UNLIKELY (sigprocmask (SIG_BLOCK, &mask, NULL) == -1) {
		gdm_common_fail_greeter ("Could not set signal mask!");
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	if G_UNLIKELY (DOING_GDM_DEVELOPMENT) {
		g_signal_connect (G_OBJECT (window), "key_press_event",
				  G_CALLBACK (key_press_event), NULL);
	}

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

	/*
	 * Initialize the value with the default value so the first time it
	 * is displayed it doesn't show as 0.  Also determine if the Halt,
	 * Reboot, Suspend and Configurator commands work.
	 */
	gdm_settings_client_get_int (GDM_KEY_TIMED_LOGIN_DELAY, &gdm_timed_delay);

	{
		char *halt;
		char *reboot;
		char *suspend;
		char *configurator;

		gdm_settings_client_get_string (GDM_KEY_HALT, &halt);
		gdm_settings_client_get_string (GDM_KEY_REBOOT, &reboot);
		gdm_settings_client_get_string (GDM_KEY_SUSPEND, &suspend);
		gdm_settings_client_get_string (GDM_KEY_CONFIGURATOR, &configurator);
		GdmHaltFound            = gdm_working_command_exists (halt);
		GdmRebootFound          = gdm_working_command_exists (reboot);
		GdmSuspendFound         = gdm_working_command_exists (suspend);
		GdmConfiguratorFound    = gdm_working_command_exists (configurator);

		g_free (configurator);
		g_free (suspend);
		g_free (reboot);
		g_free (halt);
	}

	GdmCustomCmdsFound = g_new0 (gboolean, GDM_CUSTOM_COMMAND_MAX);
	for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
		char *key_string = NULL;
		char *val;

		/*  For each possible custom command */
		key_string = g_strdup_printf ("%s%d=", GDM_KEY_CUSTOM_CMD_TEMPLATE, i);

		gdm_settings_client_get_string (key_string, &val);
		GdmCustomCmdsFound[i] = gdm_working_command_exists (val);
		if (GdmCustomCmdsFound[i])
			GdmAnyCustomCmdsFound = TRUE;

		g_free (val);
		g_free (key_string);
	}

	gdm_settings_client_get_boolean (GDM_KEY_GRAPHICAL_THEME_RAND, &rand_theme);
	if (g_getenv ("GDM_THEME") != NULL) {
		gdm_graphical_theme = g_strdup (g_getenv ("GDM_THEME"));
	} else if (rand_theme) {
		gdm_graphical_theme = get_random_theme ();
	} else {
		gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEME, &gdm_graphical_theme);
	}

	theme_file = get_theme_file (gdm_graphical_theme, &theme_dir);

	error = NULL;
	root = greeter_parse (theme_file, theme_dir,
			      GNOME_CANVAS (canvas),
			      gdm_wm_screen.width,
			      gdm_wm_screen.height,
			      &error);

	if G_UNLIKELY (root == NULL) {
		GtkWidget *dialog;
		char *s;
		char *tmp;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		tmp = ve_filename_to_utf8 (ve_sure_string (gdm_graphical_theme));
		s = g_strdup_printf (_("There was an error loading the "
				       "theme %s"), tmp);
		g_free (tmp);
		dialog = hig_dialog_new (NULL /* parent */,
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

		if (DOING_GDM_DEVELOPMENT) {
			exit (1);
		}
	}

	g_free (gdm_graphical_theme);

	if G_UNLIKELY (error)
		g_clear_error (&error);

	/* Try circles.xml */
	if G_UNLIKELY (root == NULL) {
		g_free (theme_file);
		g_free (theme_dir);
		theme_file = get_theme_file ("circles", &theme_dir);
		root = greeter_parse (theme_file, theme_dir,
				      GNOME_CANVAS (canvas),
				      gdm_wm_screen.width,
				      gdm_wm_screen.height,
				      NULL);
	}

	g_free (theme_file);

	if G_UNLIKELY (root != NULL && greeter_lookup_id ("user-pw-entry") == NULL) {
		GtkWidget *dialog;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		dialog = hig_dialog_new (NULL /* parent */,
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
	if G_UNLIKELY (DOING_GDM_DEVELOPMENT && root == NULL) {
		g_warning ("No theme could be loaded");
		exit (1);
	}

	if G_UNLIKELY (root == NULL) {
		GtkWidget *dialog;

		gdm_wm_init (0);
		gdm_wm_focus_new_windows (TRUE);

		dialog = hig_dialog_new (NULL /* parent */,
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

		execl (LIBEXECDIR "/gdmlogin", LIBEXECDIR "/gdmlogin", NULL);
		execlp ("gdmlogin", "gdmlogin", NULL);

		dialog = hig_dialog_new (NULL /* parent */,
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

	if G_LIKELY (! DOING_GDM_DEVELOPMENT) {
		ctrlch = g_io_channel_unix_new (STDIN_FILENO);
		g_io_channel_set_encoding (ctrlch, NULL, NULL);
		g_io_channel_set_buffered (ctrlch, TRUE);
		g_io_channel_set_flags (ctrlch,
					g_io_channel_get_flags (ctrlch) | G_IO_FLAG_NONBLOCK,
					NULL);
		g_io_add_watch (ctrlch,
				G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				(GIOFunc) greeter_ctrl_handler,
				NULL);
		g_io_channel_unref (ctrlch);
	}

	gdm_common_setup_blinking ();

	gtk_widget_show_all (window);
	gtk_window_move (GTK_WINDOW (window), gdm_wm_screen.x, gdm_wm_screen.y);
	gtk_widget_show_now (window);

	greeter_item_ulist_unset_selected_user ();
	greeter_item_ulist_enable ();
	greeter_item_ulist_check_show_userlist ();

	/* can it ever happen that it'd be NULL here ??? */
	if G_UNLIKELY (window->window != NULL) {
		gdm_wm_init (GDK_WINDOW_XWINDOW (window->window));

		/* Run the focus, note that this will work no matter what
		 * since gdm_wm_init will set the display to the gdk one
		 * if it fails */
		gdm_wm_focus_window (GDK_WINDOW_XWINDOW (window->window));
	}

	if G_UNLIKELY (session_dir_whacked_out) {
		GtkWidget *dialog;

		gdm_wm_focus_new_windows (TRUE);

		dialog = hig_dialog_new (NULL /* parent */,
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

	if G_UNLIKELY (g_getenv ("GDM_WHACKED_GREETER_CONFIG") != NULL) {
		GtkWidget *dialog;

		gdm_wm_focus_new_windows (TRUE);

		dialog = hig_dialog_new (NULL /* parent */,
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
	gdm_settings_client_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES, &delay);
	if (delay > 0 && ! ve_string_empty (g_getenv ("GDM_FLEXI_SERVER")) &&
	    /* but don't reap Xnest flexis */
	    ve_string_empty (g_getenv ("GDM_PARENT_DISPLAY"))) {
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

	{
		char *msg_file;
		char *msg_font;

		gdm_settings_client_get_string (GDM_KEY_INFO_MSG_FILE, &msg_file);
		gdm_settings_client_get_string (GDM_KEY_INFO_MSG_FONT, &msg_font);

		gdm_wm_show_info_msg_dialog (msg_file, msg_font);
		g_free (msg_font);
		g_free (msg_file);
	}

	gdm_common_setup_cursor (GDK_LEFT_PTR);
	gdm_wm_center_cursor ();
	gdm_common_pre_fetch_launch ();
	gtk_main ();

	return 0;
}
