/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gdm-greeter.h"
#include "gdm-themed-greeter.h"
#include "gdm-common.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include <libgnomecanvas/libgnomecanvas.h>

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif


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

#define GDM_THEMED_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_THEMED_GREETER, GdmThemedGreeterPrivate))

/* FIXME: hack */
extern gboolean session_dir_whacked_out;
extern gboolean require_quarter;
extern gint gdm_timed_delay;
extern GtkButton *gtk_ok_button;
extern GtkButton *gtk_start_again_button;

gboolean greeter_probably_login_prompt = FALSE;
static gboolean first_prompt = TRUE;

static gboolean ignore_buttons = FALSE;

gboolean   GdmHaltFound = FALSE;
gboolean   GdmRebootFound = FALSE;
gboolean  *GdmCustomCmdsFound = FALSE;
gboolean   GdmAnyCustomCmdsFound = FALSE;
gboolean   GdmSuspendFound = FALSE;
gboolean   GdmConfiguratorFound = FALSE;
GreeterItemInfo *welcome_string_info;
GreeterItemInfo *root;
GtkWidget *window = NULL;
GtkWidget *canvas = NULL;

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

struct GdmThemedGreeterPrivate
{
};

enum {
	PROP_0,
};

static void	gdm_themed_greeter_class_init	(GdmThemedGreeterClass *klass);
static void	gdm_themed_greeter_init	        (GdmThemedGreeter      *themed_greeter);
static void	gdm_themed_greeter_finalize	(GObject	       *object);

G_DEFINE_TYPE (GdmThemedGreeter, gdm_themed_greeter, GDM_TYPE_GREETER)

void
greeter_ignore_buttons (gboolean val)
{
	ignore_buttons = val;
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
#if 0
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CANCEL);
		fflush (stdout);
#endif

	}
}

static void
greeter_language_handler (GreeterItemInfo *info,
                          gpointer         user_data)
{
	gdm_lang_handler (user_data);
}

static void
greeter_setup_items (GdmThemedGreeter *greeter)
{
	greeter_item_clock_setup ();
	greeter_item_pam_setup (GDM_GREETER (greeter));

#if 0
	/* This will query the daemon for pictures through stdin/stdout! */
	greeter_item_ulist_setup ();
#endif

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

static void
gdm_set_welcomemsg (GdmThemedGreeter *greeter)
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
		char *graphical_theme_dir;

		gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEME_DIR, &graphical_theme_dir);
		dir = g_build_filename (graphical_theme_dir, in, NULL);
		g_free (graphical_theme_dir);
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

static gboolean
gdm_themed_greeter_start (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->start (greeter);

	return TRUE;
}

static gboolean
gdm_themed_greeter_stop (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->stop (greeter);

	return TRUE;
}

static gboolean
gdm_themed_greeter_info (GdmGreeter *greeter,
			 const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->info (greeter, text);

	g_debug ("THEMED GREETER: info: %s", text);

	greeter_item_pam_message (text);

	return TRUE;
}

static gboolean
gdm_themed_greeter_problem (GdmGreeter *greeter,
			    const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->problem (greeter, text);

	g_debug ("THEMED GREETER: problem: %s", text);

	greeter_item_pam_error (text);

	return TRUE;
}

static gboolean
gdm_themed_greeter_info_query (GdmGreeter *greeter,
			       const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->info_query (greeter, text);

	g_debug ("THEMED GREETER: info query: %s", text);

	if (text != NULL && strcmp (text, _("Username:")) == 0) {
#if 0
		gdm_common_login_sound (gdm_config_get_string (GDM_KEY_SOUND_PROGRAM),
					gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE),
					gdm_config_get_bool (GDM_KEY_SOUND_ON_LOGIN));
#endif
		greeter_probably_login_prompt = TRUE;
	}

	if (gtk_ok_button != NULL)
                gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);

	if (gtk_start_again_button != NULL)
                gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), !first_prompt);

	first_prompt = FALSE;

	greeter_ignore_buttons (FALSE);

	greeter_item_pam_prompt (text, PW_ENTRY_SIZE, TRUE);

	return TRUE;
}

static gboolean
gdm_themed_greeter_secret_info_query (GdmGreeter *greeter,
				      const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_themed_greeter_parent_class)->secret_info_query (greeter, text);

	g_debug ("THEMED GREETER: secret info query: %s", text);

	greeter_probably_login_prompt = FALSE;

	if (gtk_ok_button != NULL)
                gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);

	if (gtk_start_again_button != NULL)
                gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), !first_prompt);

	first_prompt = FALSE;

	greeter_ignore_buttons (FALSE);
	greeter_item_pam_prompt (text, PW_ENTRY_SIZE, FALSE);

	return TRUE;
}

static void
gdm_themed_greeter_set_property (GObject      *object,
				 guint	       prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	GdmThemedGreeter *self;

	self = GDM_THEMED_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_themed_greeter_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmThemedGreeter *self;

	self = GDM_THEMED_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
create_greeter (GdmThemedGreeter *greeter)
{
	char *bg_color;
	struct sigaction hup;
	struct sigaction term;
	sigset_t mask;
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

	gdm_common_setup_cursor (GDK_WATCH);

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

	/* Load the background as early as possible so GDM does not leave  */
	/* the background unfilled.   The cursor should be a watch already */
	/* but just in case */
	bg_color = NULL;
	gdm_settings_client_get_string (GDM_KEY_GRAPHICAL_THEMED_COLOR, &bg_color);

	/* If a graphical theme color does not exist fallback to the plain color */
	if (ve_string_empty (bg_color)) {
		g_free (bg_color);
		bg_color = NULL;
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
		key_string = g_strdup_printf ("%s%d", GDM_KEY_CUSTOM_CMD_TEMPLATE, i);

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
	root = greeter_parse (theme_file,
			      theme_dir,
			      GNOME_CANVAS (canvas),
			      gdm_wm_screen.width,
			      gdm_wm_screen.height,
			      &error);

	if (root == NULL) {
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

	}

	g_free (gdm_graphical_theme);

	if G_UNLIKELY (error)
		g_clear_error (&error);

	/* Try circles.xml */
	if (root == NULL) {
		g_free (theme_file);
		g_free (theme_dir);
		theme_file = get_theme_file ("circles", &theme_dir);
		root = greeter_parse (theme_file,
				      theme_dir,
				      GNOME_CANVAS (canvas),
				      gdm_wm_screen.width,
				      gdm_wm_screen.height,
				      NULL);
	}

	g_free (theme_file);

	if (root != NULL && greeter_lookup_id ("user-pw-entry") == NULL) {
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

	if (root == NULL) {
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

	greeter_setup_items (greeter);


	gdm_common_setup_blinking ();

	gtk_widget_show_all (window);
	gtk_window_move (GTK_WINDOW (window), gdm_wm_screen.x, gdm_wm_screen.y);
	gtk_widget_show_now (window);

	greeter_item_ulist_unset_selected_user ();
	greeter_item_ulist_enable ();
	greeter_item_ulist_check_show_userlist ();

	/* can it ever happen that it'd be NULL here ??? */
	if (window->window != NULL) {
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
}

static GObject *
gdm_themed_greeter_constructor (GType                  type,
				guint                  n_construct_properties,
				GObjectConstructParam *construct_properties)
{
        GdmThemedGreeter      *greeter;
        GdmThemedGreeterClass *klass;

        klass = GDM_THEMED_GREETER_CLASS (g_type_class_peek (GDM_TYPE_THEMED_GREETER));

        greeter = GDM_GREETER (G_OBJECT_CLASS (gdm_themed_greeter_parent_class)->constructor (type,
											      n_construct_properties,
											      construct_properties));
	create_greeter (greeter);

        return G_OBJECT (greeter);
}

static void
gdm_themed_greeter_class_init (GdmThemedGreeterClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	GdmGreeterClass *greeter_class = GDM_GREETER_CLASS (klass);

	object_class->get_property = gdm_themed_greeter_get_property;
	object_class->set_property = gdm_themed_greeter_set_property;
	object_class->constructor = gdm_themed_greeter_constructor;
	object_class->finalize = gdm_themed_greeter_finalize;

	greeter_class->start = gdm_themed_greeter_start;
	greeter_class->stop = gdm_themed_greeter_stop;
	greeter_class->info = gdm_themed_greeter_info;
	greeter_class->problem = gdm_themed_greeter_problem;
	greeter_class->info_query = gdm_themed_greeter_info_query;
	greeter_class->secret_info_query = gdm_themed_greeter_secret_info_query;

	g_type_class_add_private (klass, sizeof (GdmThemedGreeterPrivate));
}

static void
gdm_themed_greeter_init (GdmThemedGreeter *themed_greeter)
{

	themed_greeter->priv = GDM_THEMED_GREETER_GET_PRIVATE (themed_greeter);

}

static void
gdm_themed_greeter_finalize (GObject *object)
{
	GdmThemedGreeter *themed_greeter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_THEMED_GREETER (object));

	themed_greeter = GDM_THEMED_GREETER (object);

	g_return_if_fail (themed_greeter->priv != NULL);

	G_OBJECT_CLASS (gdm_themed_greeter_parent_class)->finalize (object);
}

GdmGreeter *
gdm_themed_greeter_new (void)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_THEMED_GREETER,
			       NULL);

	return GDM_GREETER (object);
}
