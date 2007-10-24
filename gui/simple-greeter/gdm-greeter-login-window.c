/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
#include <sys/time.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <glade/glade-xml.h>

#include "gdm-greeter-login-window.h"
#include "gdm-user-chooser-widget.h"

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#define GLADE_XML_FILE "gdm-greeter-login-window.glade"

#define GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_LOGIN_WINDOW, GdmGreeterLoginWindowPrivate))

enum {
        PAGE_USERLIST = 0,
        PAGE_AUTH
};

struct GdmGreeterLoginWindowPrivate
{
        GladeXML        *xml;
        GtkWidget       *user_chooser;
        gboolean         display_is_local;
        char            *timeformat;
        guint            update_clock_id;
        gboolean         clock_show_seconds;
};

enum {
        PROP_0,
        PROP_DISPLAY_IS_LOCAL,
};

enum {
        BEGIN_VERIFICATION,
        QUERY_ANSWER,
        SESSION_SELECTED,
        LANGUAGE_SELECTED,
        USER_SELECTED,
        HOSTNAME_SELECTED,
        DISCONNECTED,
        CANCELLED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_greeter_login_window_class_init   (GdmGreeterLoginWindowClass *klass);
static void     gdm_greeter_login_window_init         (GdmGreeterLoginWindow      *greeter_login_window);
static void     gdm_greeter_login_window_finalize     (GObject                    *object);

static gboolean update_clock_timeout_cb               (GdmGreeterLoginWindow *login_window);

G_DEFINE_TYPE (GdmGreeterLoginWindow, gdm_greeter_login_window, GTK_TYPE_WINDOW)

static void
set_busy (GdmGreeterLoginWindow *login_window)
{
        GdkCursor *cursor;

        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (GTK_WIDGET (login_window)->window, cursor);
        gdk_cursor_unref (cursor);
}

static void
set_ready (GdmGreeterLoginWindow *login_window)
{
        GdkCursor *cursor;

        cursor = gdk_cursor_new (GDK_LEFT_PTR);
        gdk_window_set_cursor (GTK_WIDGET (login_window)->window, cursor);
        gdk_cursor_unref (cursor);
}

static void
set_sensitive (GdmGreeterLoginWindow *login_window,
               gboolean               sensitive)
{
        GtkWidget *box;

        box = glade_xml_get_widget (login_window->priv->xml, "auth-input-box");
        gtk_widget_set_sensitive (box, sensitive);

        box = glade_xml_get_widget (login_window->priv->xml, "buttonbox");
        gtk_widget_set_sensitive (box, sensitive);
}

static void
set_focus (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *entry;

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");

        gdk_window_focus (GTK_WIDGET (login_window)->window, GDK_CURRENT_TIME);

        if (GTK_WIDGET_REALIZED (entry) && ! GTK_WIDGET_HAS_FOCUS (entry)) {
                gtk_widget_grab_focus (entry);
        }
}


static void
set_message (GdmGreeterLoginWindow *login_window,
             const char            *text)
{
        GtkWidget *label;

        label = glade_xml_get_widget (login_window->priv->xml, "auth-message-label");
        gtk_label_set_text (GTK_LABEL (label), text);
}

static void
switch_page (GdmGreeterLoginWindow *login_window,
             int                    number)
{
        GtkWidget *notebook;

        /* switch page */
        notebook = glade_xml_get_widget (login_window->priv->xml, "notebook");
        gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), number);

}

static void
do_cancel (GdmGreeterLoginWindow *login_window)
{
        switch_page (login_window, PAGE_USERLIST);
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        g_signal_emit (login_window, signals[CANCELLED], 0);

        set_ready (login_window);
}

static void
reset_dialog (GdmGreeterLoginWindow *login_window)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_debug ("Resetting dialog");

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), "");

        set_message (login_window, "");

        switch_page (login_window, PAGE_USERLIST);

        set_sensitive (login_window, TRUE);
        set_ready (login_window);
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));
}

gboolean
gdm_greeter_login_window_ready (GdmGreeterLoginWindow *login_window)
{
        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        return TRUE;
}

gboolean
gdm_greeter_login_window_reset (GdmGreeterLoginWindow *login_window)
{
        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        reset_dialog (GDM_GREETER_LOGIN_WINDOW (login_window));

        return TRUE;
}

gboolean
gdm_greeter_login_window_info (GdmGreeterLoginWindow *login_window,
                               const char            *text)
{
        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("SIMPLE GREETER: info: %s", text);

        set_message (GDM_GREETER_LOGIN_WINDOW (login_window), text);

        return TRUE;
}

gboolean
gdm_greeter_login_window_problem (GdmGreeterLoginWindow *login_window,
                                  const char            *text)
{
        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("SIMPLE GREETER: problem: %s", text);

        set_message (GDM_GREETER_LOGIN_WINDOW (login_window), text);

        return TRUE;
}

gboolean
gdm_greeter_login_window_info_query (GdmGreeterLoginWindow *login_window,
                                     const char            *text)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("SIMPLE GREETER: info query: %s", text);

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        return TRUE;
}

gboolean
gdm_greeter_login_window_secret_info_query (GdmGreeterLoginWindow *login_window,
                                            const char            *text)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        return TRUE;
}

static void
_gdm_greeter_login_window_set_display_is_local (GdmGreeterLoginWindow *login_window,
                                                gboolean               is)
{
        login_window->priv->display_is_local = is;
}

static void
gdm_greeter_login_window_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
        GdmGreeterLoginWindow *self;

        self = GDM_GREETER_LOGIN_WINDOW (object);

        switch (prop_id) {
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_greeter_login_window_set_display_is_local (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_login_window_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
        GdmGreeterLoginWindow *self;

        self = GDM_GREETER_LOGIN_WINDOW (object);

        switch (prop_id) {
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
log_in_button_clicked (GtkButton             *button,
                       GdmGreeterLoginWindow *login_window)
{
        GtkWidget  *entry;
        const char *text;

        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        entry = glade_xml_get_widget (login_window->priv->xml, "auth-prompt-entry");
        text = gtk_entry_get_text (GTK_ENTRY (entry));

        g_signal_emit (login_window, signals[QUERY_ANSWER], 0, text);
}

static void
cancel_button_clicked (GtkButton             *button,
                       GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
on_user_activated (GdmUserChooserWidget  *user_chooser,
                   GdmGreeterLoginWindow *login_window)
{
        char *user_name;

        user_name = gdm_user_chooser_widget_get_current_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));

        g_signal_emit (login_window, signals[BEGIN_VERIFICATION], 0, user_name);

        switch_page (login_window, PAGE_AUTH);
}

static void
update_clock (GtkLabel   *label,
              const char *format)
{
        time_t     t;
        struct tm *tm;
        char       buf[256];
        char      *utf8;

        time (&t);
        tm = localtime (&t);
        if (tm == NULL) {
                g_warning ("Unable to get broken down local time");
                return;
        }
        if (strftime (buf, sizeof (buf), format, tm) == 0) {
                g_warning ("Couldn't format time: %s", format);
                strcpy (buf, "???");
        }
        utf8 = g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
        gtk_label_set_text (label, utf8);
        g_free (utf8);
}

static void
set_clock_timeout (GdmGreeterLoginWindow *login_window,
                   time_t                 now)
{
        struct timeval tv;
        int            timeouttime;

        if (login_window->priv->update_clock_id > 0) {
                g_source_remove (login_window->priv->update_clock_id);
                login_window->priv->update_clock_id = 0;
        }

        gettimeofday (&tv, NULL);
        timeouttime = (G_USEC_PER_SEC - tv.tv_usec) / 1000 + 1;

        /* timeout of one minute if we don't care about the seconds */
        if (! login_window->priv->clock_show_seconds) {
                timeouttime += 1000 * (59 - now % 60);
        }

        login_window->priv->update_clock_id = g_timeout_add (timeouttime,
                                                             (GSourceFunc)update_clock_timeout_cb,
                                                             login_window);

}

static gboolean
update_clock_timeout_cb (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *label;
        time_t     new_time;

        time (&new_time);

        label = glade_xml_get_widget (login_window->priv->xml, "computer-info-time-label");
        if (label != NULL) {
                update_clock (GTK_LABEL (label), login_window->priv->timeformat);
        }

        set_clock_timeout (login_window, new_time);

        return FALSE;
}

static void
remove_clock_timeout (GdmGreeterLoginWindow *login_window)
{
        if (login_window->priv->update_clock_id > 0) {
                g_source_remove (login_window->priv->update_clock_id);
                login_window->priv->update_clock_id = 0;
        }
}

static gboolean
on_computer_info_label_button_press (GtkWidget             *widget,
                                     GdkEventButton        *event,
                                     GdmGreeterLoginWindow *login_window)
{
        GtkWidget *notebook;
        int        current_page;
        int        n_pages;
        GtkWidget *label;

        /* switch page */
        notebook = glade_xml_get_widget (login_window->priv->xml, "computer-info-notebook");
        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
        n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

        if (current_page + 1 < n_pages) {
                gtk_notebook_next_page (GTK_NOTEBOOK (notebook));
        } else {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
        }

        /* if the clock is visible then start it */
        label = glade_xml_get_widget (login_window->priv->xml, "computer-info-time-label");
        if (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == gtk_notebook_page_num (GTK_NOTEBOOK (notebook), label)) {
                time_t now;
                time (&now);
                set_clock_timeout (login_window, now);
        } else {
                remove_clock_timeout (login_window);
        }

        return FALSE;
}

static char *
file_read_one_line (const char *filename)
{
        FILE *f;
        char *line;
        char buf[4096];

        line = NULL;

        f = fopen (filename, "r");
        if (f == NULL) {
                g_warning ("Unable to open file %s: %s", filename, g_strerror (errno));
                goto out;
        }

        if (fgets (buf, sizeof (buf), f) == NULL) {
                g_warning ("Unable to read from file %s", filename);
        }

        line = g_strdup (buf);
        g_strchomp (line);

 out:
        fclose (f);

        return line;
}

static char *
get_system_version (void)
{
        char *version;

        version = NULL;
        if (g_access (SYSCONFDIR "/redhat-release", R_OK) == 0) {
                version = file_read_one_line (SYSCONFDIR "/redhat-release");
        } else if (g_access ("/etc/redhat-release", R_OK) == 0) {
                version = file_read_one_line ("/etc/redhat-release");
        }

        return version;
}

static char *
get_time_format (GdmGreeterLoginWindow *login_window)
{
        const char *time_format;
        const char *date_format;
        char       *clock_format;
        char       *result;

        time_format = login_window->priv->clock_show_seconds ? _("%l:%M:%S %p") : _("%l:%M %p");
        /* translators: replace %e with %d if, when the day of the
         *              month as a decimal number is a single digit, it
         *              should begin with a 0 in your locale (e.g. "May
         *              01" instead of "May  1").
         */
        date_format = _("%a %b %e");
        /* translators: reverse the order of these arguments
         *              if the time should come before the
         *              date on a clock in your locale.
         */
        clock_format = g_strdup_printf (_("%1$s, %2$s"),
                                        date_format,
                                        time_format);

        result = g_locale_from_utf8 (clock_format, -1, NULL, NULL, NULL);
        g_free (clock_format);

        return result;
}

static void
create_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *label;

        label = glade_xml_get_widget (login_window->priv->xml, "computer-info-name-label");
        if (label != NULL) {
                gtk_label_set_text (GTK_LABEL (label), g_get_host_name ());
        }

        label = glade_xml_get_widget (login_window->priv->xml, "computer-info-version-label");
        if (label != NULL) {
                char *version;
                version = get_system_version ();
                gtk_label_set_text (GTK_LABEL (label), version);
                g_free (version);
        }


        label = glade_xml_get_widget (login_window->priv->xml, "computer-info-time-label");
        if (label != NULL) {
                login_window->priv->timeformat = get_time_format (login_window);
                update_clock (GTK_LABEL (label), login_window->priv->timeformat);
        }
}

#define INVISIBLE_CHAR_DEFAULT       '*'
#define INVISIBLE_CHAR_BLACK_CIRCLE  0x25cf
#define INVISIBLE_CHAR_WHITE_BULLET  0x25e6
#define INVISIBLE_CHAR_BULLET        0x2022
#define INVISIBLE_CHAR_NONE          0

static void
load_theme (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *box;

        login_window->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE,
                                                 "window-box",
                                                 PACKAGE);

        g_assert (login_window->priv->xml != NULL);

        box = glade_xml_get_widget (login_window->priv->xml, "window-box");
        gtk_container_add (GTK_CONTAINER (login_window), box);

        box = glade_xml_get_widget (login_window->priv->xml, "userlist-box");
        if (box == NULL) {
                g_critical ("Userlist box not found");
        }
        gtk_container_add (GTK_CONTAINER (box), login_window->priv->user_chooser);

        button = glade_xml_get_widget (login_window->priv->xml, "log-in-button");
        gtk_widget_grab_default (button);
        g_signal_connect (button, "clicked", G_CALLBACK (log_in_button_clicked), login_window);

        button = glade_xml_get_widget (login_window->priv->xml, "cancel-button");
        g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), login_window);

        entry = glade_xml_get_widget (login_window->priv->xml, "auth-prompt-entry");
        /* Only change the invisible character if it '*' otherwise assume it is OK */
        if ('*' == gtk_entry_get_invisible_char (GTK_ENTRY (entry))) {
                gunichar invisible_char;
                invisible_char = INVISIBLE_CHAR_BLACK_CIRCLE;
                gtk_entry_set_invisible_char (GTK_ENTRY (entry), invisible_char);
        }

        create_computer_info (login_window);

        box = glade_xml_get_widget (login_window->priv->xml, "computer-info-event-box");
        g_signal_connect (box, "button-press-event", G_CALLBACK (on_computer_info_label_button_press), login_window);
}

static void
gdm_greeter_login_window_size_request (GtkWidget      *widget,
                                       GtkRequisition *requisition)
{
        int screen_h;

        if (GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request (widget, requisition);
        }

        screen_h = gdk_screen_get_height (gtk_widget_get_screen (widget));

        requisition->height = screen_h * 0.6;
}

static GObject *
gdm_greeter_login_window_constructor (GType                  type,
                                      guint                  n_construct_properties,
                                      GObjectConstructParam *construct_properties)
{
        GdmGreeterLoginWindow      *login_window;
        GdmGreeterLoginWindowClass *klass;

        klass = GDM_GREETER_LOGIN_WINDOW_CLASS (g_type_class_peek (GDM_TYPE_GREETER_LOGIN_WINDOW));

        login_window = GDM_GREETER_LOGIN_WINDOW (G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->constructor (type,
                                                                                                                      n_construct_properties,
                                                                                                                      construct_properties));


        return G_OBJECT (login_window);
}

static void
gdm_greeter_login_window_class_init (GdmGreeterLoginWindowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_login_window_get_property;
        object_class->set_property = gdm_greeter_login_window_set_property;
        object_class->constructor = gdm_greeter_login_window_constructor;
        object_class->finalize = gdm_greeter_login_window_finalize;

        widget_class->size_request = gdm_greeter_login_window_size_request;

        signals [BEGIN_VERIFICATION] =
                g_signal_new ("begin-verification",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [QUERY_ANSWER] =
                g_signal_new ("query-answer",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, query_answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [LANGUAGE_SELECTED] =
                g_signal_new ("language-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, language_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [USER_SELECTED] =
                g_signal_new ("user-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, user_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SESSION_SELECTED] =
                g_signal_new ("session-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, session_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, hostname_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CANCELLED] =
                g_signal_new ("cancelled",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, cancelled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_type_class_add_private (klass, sizeof (GdmGreeterLoginWindowPrivate));
}

static void
gdm_greeter_login_window_init (GdmGreeterLoginWindow *login_window)
{
        login_window->priv = GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE (login_window);

        login_window->priv->clock_show_seconds = TRUE;

        login_window->priv->user_chooser = gdm_user_chooser_widget_new ();
        g_signal_connect (login_window->priv->user_chooser,
                          "user-activated",
                          G_CALLBACK (on_user_activated),
                          login_window);

        gtk_widget_show_all (login_window->priv->user_chooser);

        load_theme (login_window);

        gtk_window_set_opacity (GTK_WINDOW (login_window), 0.75);
        gtk_window_set_position (GTK_WINDOW (login_window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_deletable (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_decorated (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_stick (GTK_WINDOW (login_window));
        gtk_container_set_border_width (GTK_CONTAINER (login_window), 12);

}

static void
gdm_greeter_login_window_finalize (GObject *object)
{
        GdmGreeterLoginWindow *login_window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (object));

        login_window = GDM_GREETER_LOGIN_WINDOW (object);

        g_return_if_fail (login_window->priv != NULL);

        remove_clock_timeout (login_window);

        G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_login_window_new (gboolean is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_LOGIN_WINDOW,
                               "display-is-local", is_local,
                               NULL);

        return GTK_WIDGET (object);
}
