/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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
#include <locale.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <string.h>
#include <syslog.h>

#include <vicious.h>
#include "viciousui.h"

#include "gdm.h"

#include "gdmwm.h"
#include "gdmcommon.h"

extern gchar *GdmInfoMsgFile;
extern gchar *GdmInfoMsgFont;
extern gchar *GdmSoundProgram;
extern gboolean GdmSoundOnLogin;
extern gchar *GdmSoundOnLoginFile;

void
gdm_common_show_info_msg (void)
{
	GtkWidget *dialog, *label;
	gchar *InfoMsg;
	gsize InfoMsgLength;

	if (ve_string_empty (GdmInfoMsgFile) ||
	    ! g_file_test (GdmInfoMsgFile, G_FILE_TEST_EXISTS) ||
	    ! g_file_get_contents (GdmInfoMsgFile, &InfoMsg, &InfoMsgLength, NULL))
		return;

	if (InfoMsgLength <= 0) {
		g_free (InfoMsg);
		return;
	}

	gdm_wm_focus_new_windows (TRUE);
	dialog = gtk_dialog_new_with_buttons (NULL /* Message */,
					      NULL /* parent */, GTK_DIALOG_MODAL |
					      GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_OK, GTK_RESPONSE_OK,
					      NULL);
	label = gtk_label_new (InfoMsg);

	if (GdmInfoMsgFont && strlen(GdmInfoMsgFont) > 0) {
		PangoFontDescription *GdmInfoMsgFontDesc = pango_font_description_from_string (GdmInfoMsgFont);
		if (GdmInfoMsgFontDesc) {
			gtk_widget_modify_font (label, GdmInfoMsgFontDesc);
			pango_font_description_free (GdmInfoMsgFontDesc);
		}
	}

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), label);
	gtk_widget_show_all (dialog);
	gdm_wm_center_window (GTK_WINDOW (dialog));

	gdm_common_setup_cursor (GDK_LEFT_PTR);

	gdm_wm_no_login_focus_push();
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	gdm_wm_no_login_focus_pop();

	g_free (InfoMsg);
}

void
gdm_common_message (const gchar *msg)
{
	GtkWidget *req = NULL;

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	req = ve_hig_dialog_new (NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_INFO,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 msg,
				 /* avoid warning */ "%s", "");

	gdm_wm_center_window (GTK_WINDOW (req));

	gdm_wm_no_login_focus_push ();
	gtk_dialog_run (GTK_DIALOG (req));
	gtk_widget_destroy (req);
	gdm_wm_no_login_focus_pop ();
}

gint
gdm_common_query (const gchar *msg,
		  gboolean markup,
		  const char *posbutton,
		  const char *negbutton,
		  gboolean has_cancel)
{
	int ret;
	GtkWidget *req;
	GtkWidget *button;

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	req = ve_hig_dialog_new (NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_QUESTION,
				 GTK_BUTTONS_NONE,
				 markup,
				 msg,
				 /* avoid warning */ "%s", "");

	if (negbutton != NULL) {
		button = gtk_button_new_from_stock (negbutton);
		gtk_dialog_add_action_widget (GTK_DIALOG (req), button, GTK_RESPONSE_NO);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);
	}

	if (posbutton != NULL) {
		button = gtk_button_new_from_stock (posbutton);
		gtk_dialog_add_action_widget (GTK_DIALOG (req), button, GTK_RESPONSE_YES);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);
	}

	if (has_cancel == TRUE) {
		button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
		gtk_dialog_add_action_widget (GTK_DIALOG (req), button, GTK_RESPONSE_CANCEL);
		GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
		gtk_widget_show (button);
	}

	if (posbutton != NULL)
		gtk_dialog_set_default_response (GTK_DIALOG (req), GTK_RESPONSE_YES);
	else if (negbutton != NULL)
		gtk_dialog_set_default_response (GTK_DIALOG (req), GTK_RESPONSE_NO);
	else if (has_cancel)
		gtk_dialog_set_default_response (GTK_DIALOG (req), GTK_RESPONSE_CANCEL);

	gdm_wm_center_window (GTK_WINDOW (req));

	gdm_wm_no_login_focus_push ();
	ret = gtk_dialog_run (GTK_DIALOG (req));
	gdm_wm_no_login_focus_pop ();
	gtk_widget_destroy (req);

	return ret;
}

void
gdm_common_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	gdm_kill_thingies ();
	_exit (DISPLAY_GREETERFAILED);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    closelog();

    g_free (s);

    gdm_kill_thingies ();
    _exit (DISPLAY_GREETERFAILED);
}

void
gdm_common_setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

gboolean
gdm_common_string_same (VeConfig *config, const char *cur, const char *key)
{
	char *val = ve_config_get_string (config, key);
	if (strcmp (ve_sure_string (cur), ve_sure_string (val)) == 0) {
		g_free (val);
		return TRUE;
	} else {
		g_free (val);
		return FALSE;
	}
}

gboolean
gdm_common_bool_same (VeConfig *config, gboolean cur, const char *key)
{
	gboolean val = ve_config_get_bool (config, key);
	if (ve_bool_equal (cur, val)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
gdm_common_int_same (VeConfig *config, int cur, const char *key)
{
	int val = ve_config_get_int (config, key);
	if (cur == val) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void
gdm_common_login_sound (void)
{
	if ( ! GdmSoundOnLogin)
		return;

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")) ||
	    ve_string_empty (GdmSoundProgram) ||
	    ve_string_empty (GdmSoundOnLoginFile) ||
	    access (GdmSoundProgram, F_OK) != 0 ||
	    access (GdmSoundOnLoginFile, F_OK) != 0) {
		gdk_beep ();
	} else {
		/* login sound interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_LOGIN_SOUND);
		fflush (stdout);
	}
}

typedef struct {
	GtkWidget *entry;
	gboolean   blink;
} EntryBlink;

static GSList *entries = NULL;
static guint noblink_timeout = 0;

#define NOBLINK_TIMEOUT (20*1000)

static void
setup_blink (gboolean blink)
{
	GSList *li;
	for (li = entries; li != NULL; li = li->next) {
		EntryBlink *eb = li->data;
		if (eb->blink) {
			GtkSettings *settings
				= gtk_widget_get_settings (eb->entry);
			g_object_set (settings,
				      "gtk-cursor-blink", blink, NULL);
			gtk_widget_queue_resize (eb->entry);
		}
	}
}

static gboolean
no_blink (gpointer data)
{
	noblink_timeout = 0;
	setup_blink (FALSE);
	return FALSE;
}

static gboolean
delay_noblink (GSignalInvocationHint *ihint,
	       guint	           n_param_values,
	       const GValue	  *param_values,
	       gpointer		   data)
{
	setup_blink (TRUE);
	if (noblink_timeout > 0)
		g_source_remove (noblink_timeout);
	noblink_timeout
		= g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
	return TRUE;
}      


void
gdm_setup_blinking (void)
{
	guint sid;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	sid = g_signal_lookup ("activate",
			       GTK_TYPE_MENU_ITEM);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	sid = g_signal_lookup ("key_press_event",
			       GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	sid = g_signal_lookup ("button_press_event",
			       GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid,
				    0 /* detail */,
				    delay_noblink,
				    NULL /* data */,
				    NULL /* destroy_notify */);

	noblink_timeout = g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
}

void
gdm_setup_blinking_entry (GtkWidget *entry)
{
	EntryBlink *eb;
	GtkSettings *settings;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	eb = g_new0 (EntryBlink, 1);

	eb->entry = entry;
	settings = gtk_widget_get_settings (eb->entry);
	g_object_get (settings, "gtk-cursor-blink", &(eb->blink), NULL);

	entries = g_slist_prepend (entries, eb);
}
