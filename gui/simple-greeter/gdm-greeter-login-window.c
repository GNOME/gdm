/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008 Red Hat, Inc.
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
#include <pwd.h>

#ifdef ENABLE_RBAC_SHUTDOWN
#include <auth_attr.h>
#include <secdb.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include <gtk/gtk.h>

#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef HAVE_POLKIT_GNOME
#include <polkit-gnome/polkit-gnome.h>
#endif

#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"
#include "gdm-profile.h"

#include "gdm-greeter-login-window.h"
#include "gdm-user-chooser-widget.h"

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define GPM_DBUS_NAME      "org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH      "/org/freedesktop/PowerManagement"
#define GPM_DBUS_INTERFACE "org.freedesktop.PowerManagement"

#define GLADE_XML_FILE       "gdm-greeter-login-window.glade"

#define KEY_GREETER_DIR             "/apps/gdm/simple-greeter"
#define KEY_BANNER_MESSAGE_ENABLED  KEY_GREETER_DIR "/banner_message_enable"
#define KEY_BANNER_MESSAGE_TEXT     KEY_GREETER_DIR "/banner_message_text"
#define KEY_LOGO                    KEY_GREETER_DIR "/logo_icon_name"
#define KEY_DISABLE_RESTART_BUTTONS KEY_GREETER_DIR "/disable_restart_buttons"
#define GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_LOGIN_WINDOW, GdmGreeterLoginWindowPrivate))

enum {
        MODE_SELECTION = 0,
        MODE_AUTHENTICATION
};

enum {
        LOGIN_BUTTON_HIDDEN = 0,
        LOGIN_BUTTON_ANSWER_QUERY,
        LOGIN_BUTTON_TIMED_LOGIN
};

struct GdmGreeterLoginWindowPrivate
{
        GladeXML        *xml;
        GtkWidget       *user_chooser;
        GtkWidget       *auth_capslock_label;
        GtkWidget       *auth_banner_label;
        guint            display_is_local : 1;
        guint            is_interactive : 1;
        GConfClient     *client;

        gboolean         caps_lock_on;

        gboolean         banner_message_enabled;
        guint            gconf_cnxn;

        guint            dialog_mode;

        gboolean         timed_login_enabled;
        guint            timed_login_delay;
        char            *timed_login_username;
        guint            timed_login_timeout_id;

        guint            animation_timeout_id;

        guint            login_button_handler_id;
        guint            start_session_handler_id;
};

enum {
        PROP_0,
        PROP_DISPLAY_IS_LOCAL,
        PROP_IS_INTERACTIVE,
};

enum {
        BEGIN_AUTO_LOGIN,
        BEGIN_VERIFICATION,
        BEGIN_VERIFICATION_FOR_USER,
        QUERY_ANSWER,
        START_SESSION,
        USER_SELECTED,
        DISCONNECTED,
        CANCELLED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_greeter_login_window_class_init   (GdmGreeterLoginWindowClass *klass);
static void     gdm_greeter_login_window_init         (GdmGreeterLoginWindow      *greeter_login_window);
static void     gdm_greeter_login_window_finalize     (GObject                    *object);

static void     restart_timed_login_timeout (GdmGreeterLoginWindow *login_window);
static void     on_user_unchosen            (GdmUserChooserWidget *user_chooser,
                                             GdmGreeterLoginWindow *login_window);

G_DEFINE_TYPE (GdmGreeterLoginWindow, gdm_greeter_login_window, GTK_TYPE_WINDOW)

static void
capslock_update (GdmGreeterLoginWindow *login_window,
                 gboolean               is_on)
{

        login_window->priv->caps_lock_on = is_on;

        if (login_window->priv->auth_capslock_label == NULL) {
                return;
        }

        if (is_on) {
                gtk_label_set_text (GTK_LABEL (login_window->priv->auth_capslock_label),
                                    _("You have the Caps Lock key on."));
        } else {
                gtk_label_set_text (GTK_LABEL (login_window->priv->auth_capslock_label),
                                    "");
        }
}

static gboolean
is_capslock_on (void)
{
        XkbStateRec states;
        Display     *dsp;

        dsp = GDK_DISPLAY ();

        if (XkbGetState (dsp, XkbUseCoreKbd, &states) != Success) {
              return FALSE;
        }

        return (states.locked_mods & LockMask) != 0;
}

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

        gtk_widget_set_sensitive (login_window->priv->user_chooser, sensitive);
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
on_user_interaction (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: user is interacting with session!\n");
        restart_timed_login_timeout (login_window);
}

static GdkFilterReturn
on_xevent (XEvent                *xevent,
           GdkEvent              *event,
           GdmGreeterLoginWindow *login_window)
{
        switch (xevent->xany.type) {
                case KeyPress:
                case KeyRelease:
                case ButtonPress:
                case ButtonRelease:
                        on_user_interaction (login_window);
                        break;
                case  PropertyNotify:
                        if (xevent->xproperty.atom == gdk_x11_get_xatom_by_name ("_NET_WM_USER_TIME")) {
                                on_user_interaction (login_window);
                        }
                        break;

                default:
                        break;
        }

        return GDK_FILTER_CONTINUE;
}

static void
stop_watching_for_user_interaction (GdmGreeterLoginWindow *login_window)
{
        gdk_window_remove_filter (NULL,
                                  (GdkFilterFunc) on_xevent,
                                  login_window);
}

static void
remove_timed_login_timeout (GdmGreeterLoginWindow *login_window)
{
        if (login_window->priv->timed_login_timeout_id > 0) {
                g_debug ("GdmGreeterLoginWindow: removing timed login timer");
                g_source_remove (login_window->priv->timed_login_timeout_id);
                login_window->priv->timed_login_timeout_id = 0;
        }

        stop_watching_for_user_interaction (login_window);
}

static void
_gdm_greeter_login_window_set_interactive (GdmGreeterLoginWindow *login_window,
                                           gboolean               is_interactive)
{
        if (login_window->priv->is_interactive != is_interactive) {
                login_window->priv->is_interactive = is_interactive;
                g_object_notify (G_OBJECT (login_window), "is-interactive");
        }
}

static gboolean
timed_login_timer (GdmGreeterLoginWindow *login_window)
{
        set_sensitive (login_window, FALSE);
        set_message (login_window, _("Automatically logging in..."));

        g_debug ("GdmGreeterLoginWindow: timer expired");
        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
        login_window->priv->timed_login_timeout_id = 0;

        return FALSE;
}

static void
watch_for_user_interaction (GdmGreeterLoginWindow *login_window)
{
        gdk_window_add_filter (NULL,
                               (GdkFilterFunc) on_xevent,
                               login_window);
}

static void
restart_timed_login_timeout (GdmGreeterLoginWindow *login_window)
{
        remove_timed_login_timeout (login_window);

        if (login_window->priv->timed_login_enabled) {
                g_debug ("GdmGreeterLoginWindow: adding timed login timer");
                watch_for_user_interaction (login_window);
                login_window->priv->timed_login_timeout_id = g_timeout_add_seconds (login_window->priv->timed_login_delay,
                                                                                    (GSourceFunc)timed_login_timer,
                                                                                    login_window);

                gdm_chooser_widget_set_item_timer (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                   GDM_USER_CHOOSER_USER_AUTO,
                                                   login_window->priv->timed_login_delay * 1000);
        }
}

static void
show_widget (GdmGreeterLoginWindow *login_window,
             const char            *name,
             gboolean               visible)
{
        GtkWidget *widget;

        widget = glade_xml_get_widget (login_window->priv->xml, name);
        if (widget != NULL) {
                if (visible) {
                        gtk_widget_show (widget);
                } else {
                        gtk_widget_hide (widget);
                }
        }
}

static gboolean
get_show_restart_buttons (GdmGreeterLoginWindow *login_window)
{
        gboolean     show;
        GError      *error;

        error = NULL;
        show = ! gconf_client_get_bool (login_window->priv->client, KEY_DISABLE_RESTART_BUTTONS, &error);
        if (error != NULL) {
                g_debug ("GdmGreeterLoginWindow: unable to get disable-restart-buttons configuration: %s", error->message);
                g_error_free (error);
        }

#ifdef ENABLE_RBAC_SHUTDOWN
        {
                char *username;

                username = g_get_user_name ();
                if (username == NULL || !chkauthattr (RBAC_SHUTDOWN_KEY, username)) {
                        show = FALSE;
                        g_debug ("GdmGreeterLoginWindow: Not showing stop/restart buttons for user %s due to RBAC key %s",
                                 username, RBAC_SHUTDOWN_KEY);
                } else {
                        g_debug ("GdmGreeterLoginWindow: Showing stop/restart buttons for user %s due to RBAC key %s",
                                 username, RBAC_SHUTDOWN_KEY);
                }
        }
#endif
        return show;
}

static void
on_login_button_clicked_answer_query (GtkButton             *button,
                                      GdmGreeterLoginWindow *login_window)
{
        GtkWidget  *entry;
        const char *text;

        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        entry = glade_xml_get_widget (login_window->priv->xml, "auth-prompt-entry");
        text = gtk_entry_get_text (GTK_ENTRY (entry));

        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
        g_signal_emit (login_window, signals[QUERY_ANSWER], 0, text);
}

static void
on_login_button_clicked_timed_login (GtkButton             *button,
                                     GdmGreeterLoginWindow *login_window)
{
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
}

static void
set_log_in_button_mode (GdmGreeterLoginWindow *login_window,
                        int                    mode)
{
        GtkWidget *button;

        button = glade_xml_get_widget (login_window->priv->xml, "log-in-button");
        gtk_widget_grab_default (button);

        /* disconnect any signals */
        if (login_window->priv->login_button_handler_id > 0) {
                g_signal_handler_disconnect (button, login_window->priv->login_button_handler_id);
                login_window->priv->login_button_handler_id = 0;
       }

        switch (mode) {
        case LOGIN_BUTTON_HIDDEN:
                gtk_widget_hide (button);
                break;
        case LOGIN_BUTTON_ANSWER_QUERY:
                login_window->priv->login_button_handler_id = g_signal_connect (button, "clicked", G_CALLBACK (on_login_button_clicked_answer_query), login_window);
                gtk_widget_show (button);
                break;
        case LOGIN_BUTTON_TIMED_LOGIN:
                login_window->priv->login_button_handler_id = g_signal_connect (button, "clicked", G_CALLBACK (on_login_button_clicked_timed_login), login_window);
                gtk_widget_show (button);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
switch_mode (GdmGreeterLoginWindow *login_window,
             int                    number)
{
        const char *default_name;
        GtkWidget  *user_chooser;
        GtkWidget  *box;
        gboolean    show_restart_buttons;

        /* we want to run this even if we're supposed to
           be in the mode already so that we reset everything
           to a known state */
        login_window->priv->dialog_mode = number;

        /* FIXME: do animation */
        default_name = NULL;

        show_restart_buttons = get_show_restart_buttons (login_window);

        switch (number) {
        case MODE_SELECTION:
                gtk_widget_set_size_request (GTK_WIDGET (login_window), -1, -1);
                set_log_in_button_mode (login_window, LOGIN_BUTTON_HIDDEN);

                show_widget (login_window, "cancel-button", FALSE);
                show_widget (login_window, "shutdown-button",
                             login_window->priv->display_is_local && show_restart_buttons);
                show_widget (login_window, "restart-button",
                             login_window->priv->display_is_local && show_restart_buttons);
                show_widget (login_window, "suspend-button",
                             login_window->priv->display_is_local && show_restart_buttons);
                show_widget (login_window, "disconnect-button",
                             ! login_window->priv->display_is_local);
                show_widget (login_window, "auth-input-box", FALSE);
                default_name = NULL;
                break;
        case MODE_AUTHENTICATION:
                gtk_widget_set_size_request (GTK_WIDGET (login_window),
                                             GTK_WIDGET (login_window)->allocation.width,
                                             -1);
                show_widget (login_window, "cancel-button", TRUE);
                show_widget (login_window, "shutdown-button", FALSE);
                show_widget (login_window, "restart-button", FALSE);
                show_widget (login_window, "suspend-button", FALSE);
                show_widget (login_window, "disconnect-button", FALSE);
                default_name = "log-in-button";
                break;
        default:
                g_assert_not_reached ();
        }

        box = glade_xml_get_widget (login_window->priv->xml, "buttonbox");
        gtk_button_box_set_layout (GTK_BUTTON_BOX (box),
                                   (number == MODE_SELECTION) ? GTK_BUTTONBOX_SPREAD : GTK_BUTTONBOX_END );

        user_chooser = glade_xml_get_widget (login_window->priv->xml, "user-chooser");
        box = gtk_widget_get_parent (user_chooser);
        if (GTK_IS_BOX (box)) {
                guint       padding;
                GtkPackType pack_type;

                gtk_box_query_child_packing (GTK_BOX (box),
                                             user_chooser,
                                             NULL,
                                             NULL,
                                             &padding,
                                             &pack_type);
                gtk_box_set_child_packing (GTK_BOX (box),
                                           user_chooser,
                                           number == MODE_SELECTION,
                                           number == MODE_SELECTION,
                                           padding,
                                           pack_type);
        }

        if (default_name != NULL) {
                GtkWidget *widget;

                widget = glade_xml_get_widget (login_window->priv->xml, default_name);
                gtk_widget_grab_default (widget);
        }
}

static void
do_disconnect (GdmGreeterLoginWindow *login_window)
{
        gtk_main_quit ();
}

static void
do_suspend (GdmGreeterLoginWindow *login_window)
{
        GError          *error;
        DBusGConnection *connection;
        DBusGProxy      *proxy;

        g_debug ("GdmGreeterLoginWindow: Suspend button clicked");

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (error != NULL) {
                g_warning ("Couldn't suspend: %s", error->message);
                g_error_free (error);
                return;
        }
        proxy = dbus_g_proxy_new_for_name (connection,
                                           GPM_DBUS_NAME,
                                           GPM_DBUS_PATH,
                                           GPM_DBUS_INTERFACE);
        error = NULL;
        dbus_g_proxy_call (proxy,
                           "Suspend",
                           &error,
                           G_TYPE_INVALID,
                           G_TYPE_INVALID);
        if (error != NULL) {
                g_warning ("Couldn't suspend: %s", error->message);
                g_error_free (error);
                return;
        }

        g_object_unref (proxy);
}

static void
delete_entry_text (GtkWidget *entry)
{
        const char *typed_text;
        char       *null_text;

        /* try to scrub out any secret info */
        typed_text = gtk_entry_get_text (GTK_ENTRY (entry));
        null_text = g_strnfill (strlen (typed_text) + 1, '\b');
        gtk_entry_set_text (GTK_ENTRY (entry), null_text);
        gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static void
reset_dialog (GdmGreeterLoginWindow *login_window)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_debug ("GdmGreeterLoginWindow: Resetting dialog");
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        if (login_window->priv->timed_login_enabled) {
                gdm_chooser_widget_set_item_timer (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                   GDM_USER_CHOOSER_USER_AUTO, 0);
                remove_timed_login_timeout (login_window);
                login_window->priv->timed_login_enabled = FALSE;
        }
        _gdm_greeter_login_window_set_interactive (login_window, FALSE);

        g_signal_handlers_block_by_func (G_OBJECT (login_window->priv->user_chooser),
                                         G_CALLBACK (on_user_unchosen), login_window);
        gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), NULL);
        g_signal_handlers_unblock_by_func (G_OBJECT (login_window->priv->user_chooser),
                                           G_CALLBACK (on_user_unchosen), login_window);

        if (login_window->priv->start_session_handler_id > 0) {
                g_signal_handler_disconnect (login_window, login_window->priv->start_session_handler_id);
                login_window->priv->start_session_handler_id = 0;
        }

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");

        delete_entry_text (entry);

        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
        set_message (login_window, "");

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), "");

        switch_mode (login_window, MODE_SELECTION);

        set_sensitive (login_window, TRUE);
        set_ready (login_window);
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));
}

static void
do_cancel (GdmGreeterLoginWindow *login_window)
{
        reset_dialog (login_window);
        g_signal_emit (login_window, signals[CANCELLED], 0);
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

        g_debug ("GdmGreeterLoginWindow: info: %s", text);

        set_message (GDM_GREETER_LOGIN_WINDOW (login_window), text);

        return TRUE;
}

gboolean
gdm_greeter_login_window_problem (GdmGreeterLoginWindow *login_window,
                                  const char            *text)
{
        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("GdmGreeterLoginWindow: problem: %s", text);

        set_message (GDM_GREETER_LOGIN_WINDOW (login_window), text);
        gdk_window_beep (GTK_WIDGET (login_window)->window);

        return TRUE;
}

void
gdm_greeter_login_window_request_timed_login (GdmGreeterLoginWindow *login_window,
                                              const char            *username,
                                              int                    delay)
{
        static gboolean timed_login_already_enabled;

        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: requested automatic login for user '%s' in %d seconds", username, delay);

        if (login_window->priv->timed_login_username != NULL) {
                timed_login_already_enabled = TRUE;
                g_free (login_window->priv->timed_login_username);
        } else {
                timed_login_already_enabled = FALSE;
        }
        login_window->priv->timed_login_username = g_strdup (username);
        login_window->priv->timed_login_delay = delay;

        if (login_window->priv->dialog_mode != MODE_SELECTION) {
                reset_dialog (login_window);
        }
        gdm_user_chooser_widget_set_show_auto_user (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        if (!timed_login_already_enabled) {
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                              GDM_USER_CHOOSER_USER_AUTO);
        }
}

static void
gdm_greeter_login_window_start_session_when_ready (GdmGreeterLoginWindow *login_window)
{
        if (login_window->priv->is_interactive) {
                g_debug ("GdmGreeterLoginWindow: starting session");
                g_signal_emit (login_window, signals[START_SESSION], 0);
        } else {
                g_debug ("GdmGreeterLoginWindow: not starting session since "
                         "user hasn't had an opportunity to pick language "
                         "and session yet.");

                /* Call back when we're ready to go
                 */
                login_window->priv->start_session_handler_id =
                    g_signal_connect (login_window, "notify::is-interactive",
                                      G_CALLBACK (gdm_greeter_login_window_start_session_when_ready),
                                      NULL);

                /* FIXME: If the user wasn't asked any questions by pam but
                 * pam still authorized them (passwd -d, or the questions got
                 * asked on an external device) then we need to let them log in.
                 * Right now we just log them in right away, but we really should
                 * set a timer up like timed login (but shorter, say ~5 seconds),
                 * so they can pick language/session.  Will need to refactor things
                 * a bit so we can share code with timed login.
                 */
                if (!login_window->priv->timed_login_enabled) {

                        g_debug ("GdmGreeterLoginWindow: Okay, we'll start the session anyway,"
                                 "because the user isn't ever going to get an opportunity to"
                                 "interact with session");
                        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
                }

        }
}

gboolean
gdm_greeter_login_window_info_query (GdmGreeterLoginWindow *login_window,
                                     const char            *text)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("GdmGreeterLoginWindow: info query: %s", text);

        entry = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-entry");
        delete_entry_text (entry);
        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        show_widget (login_window, "auth-input-box", TRUE);
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
        delete_entry_text (entry);
        gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);

        label = glade_xml_get_widget (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        show_widget (login_window, "auth-input-box", TRUE);
        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        return TRUE;
}

void
gdm_greeter_login_window_user_authorized (GdmGreeterLoginWindow *login_window)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: user now authorized");

        gdm_greeter_login_window_start_session_when_ready (login_window);
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
        case PROP_IS_INTERACTIVE:
                _gdm_greeter_login_window_set_interactive (self, g_value_get_boolean (value));
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
        case PROP_IS_INTERACTIVE:
                g_value_set_boolean (value, self->priv->is_interactive);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
suspend_button_clicked (GtkButton             *button,
                        GdmGreeterLoginWindow *login_window)
{
        do_suspend (login_window);
}


static void
cancel_button_clicked (GtkButton             *button,
                       GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
disconnect_button_clicked (GtkButton             *button,
                           GdmGreeterLoginWindow *login_window)
{
        do_disconnect (login_window);
}

static gboolean
try_system_stop (DBusGConnection *connection,
                 GError         **error)
{
        DBusGProxy      *proxy;
        gboolean         res;

        g_debug ("GdmGreeterLoginWindow: trying to stop system");

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "Stop",
                                              INT_MAX,
                                              error,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_INVALID);
        return res;
}

static gboolean
try_system_restart (DBusGConnection *connection,
                    GError         **error)
{
        DBusGProxy      *proxy;
        gboolean         res;

        g_debug ("GdmGreeterLoginWindow: trying to restart system");

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        res = dbus_g_proxy_call_with_timeout (proxy,
                                              "Restart",
                                              INT_MAX,
                                              error,
                                              /* parameters: */
                                              G_TYPE_INVALID,
                                              /* return values: */
                                              G_TYPE_INVALID);
        return res;
}

#ifdef HAVE_POLKIT_GNOME
static void
system_restart_auth_cb (PolKitAction          *action,
                        gboolean               gained_privilege,
                        GError                *error,
                        GdmGreeterLoginWindow *login_window)
{
        GError          *local_error;
        DBusGConnection *connection;
        gboolean         res;

        g_debug ("GdmGreeterLoginWindow: system restart auth callback gained=%s", gained_privilege ? "yes" : "no");

        if (! gained_privilege) {
                if (error != NULL) {
                        g_warning ("GdmGreeterLoginWindow: system restart error: %s", error->message);
                }
                return;
        }

        local_error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &local_error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", local_error->message);
                g_error_free (local_error);
                return;
        }

        res = try_system_restart (connection, &local_error);
        if (! res) {
                g_warning ("Unable to restart system: %s", local_error->message);
                g_error_free (local_error);
                return;
        }
}

static void
system_stop_auth_cb (PolKitAction          *action,
                     gboolean               gained_privilege,
                     GError                *error,
                     GdmGreeterLoginWindow *login_window)
{
        GError          *local_error;
        DBusGConnection *connection;
        gboolean         res;

        g_debug ("GdmGreeterLoginWindow: system stop auth callback gained=%s", gained_privilege ? "yes" : "no");

        if (! gained_privilege) {
                if (error != NULL) {
                        g_warning ("GdmGreeterLoginWindow: system stop error: %s", error->message);
                }
                return;
        }

        local_error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &local_error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", local_error->message);
                g_error_free (local_error);
                return;
        }

        res = try_system_stop (connection, &local_error);
        if (! res) {
                g_warning ("Unable to stop system: %s", local_error->message);
                g_error_free (local_error);
                return;
        }
}

static PolKitAction *
get_action_from_error (GError *error)
{
        PolKitAction *action;
        const char   *paction;

        action = polkit_action_new ();

        paction = NULL;
        if (g_str_has_prefix (error->message, "Not privileged for action: ")) {
                paction = error->message + strlen ("Not privileged for action: ");
        }
        g_debug ("GdmGreeterLoginWindow: Requesting priv for '%s'", paction);

        polkit_action_set_action_id (action, paction);

        return action;
}
#endif

static void
do_system_restart (GdmGreeterLoginWindow *login_window)
{
        gboolean         res;
        GError          *error;
        DBusGConnection *connection;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", error->message);
                g_error_free (error);
                return;
        }

        res = try_system_restart (connection, &error);
#ifdef HAVE_POLKIT_GNOME
        if (! res) {
                g_debug ("GdmGreeterLoginWindow: unable to restart system: %s: %s",
                         dbus_g_error_get_name (error),
                         error->message);

                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        PolKitAction *action;
                        guint         xid;
                        pid_t         pid;

                        action = get_action_from_error (error);

                        xid = 0;
                        pid = getpid ();

                        g_error_free (error);
                        error = NULL;
                        res = polkit_gnome_auth_obtain (action,
                                                        xid,
                                                        pid,
                                                        (PolKitGnomeAuthCB) system_restart_auth_cb,
                                                        login_window,
                                                        &error);
                        polkit_action_unref (action);

                        if (! res) {
                                g_warning ("Unable to request privilege for action: %s", error->message);
                                g_error_free (error);
                        }

                }
        }
#endif
}

static void
do_system_stop (GdmGreeterLoginWindow *login_window)
{
        gboolean         res;
        GError          *error;
        DBusGConnection *connection;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", error->message);
                g_error_free (error);
                return;
        }

        res = try_system_stop (connection, &error);
#ifdef HAVE_POLKIT_GNOME
        if (! res) {
                g_debug ("GdmGreeterLoginWindow: unable to stop system: %s: %s",
                         dbus_g_error_get_name (error),
                         error->message);

                if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
                        PolKitAction *action;
                        guint         xid;
                        pid_t         pid;

                        xid = 0;
                        pid = getpid ();

                        action = get_action_from_error (error);

                        g_error_free (error);
                        error = NULL;
                        res = polkit_gnome_auth_obtain (action,
                                                        xid,
                                                        pid,
                                                        (PolKitGnomeAuthCB) system_stop_auth_cb,
                                                        login_window,
                                                        &error);
                        polkit_action_unref (action);

                        if (! res) {
                                g_warning ("Unable to request privilege for action: %s", error->message);
                                g_error_free (error);
                        }

                }
        }
#endif
}

static void
restart_button_clicked (GtkButton             *button,
                        GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: restart button clicked");
        do_system_restart (login_window);
}

static void
shutdown_button_clicked (GtkButton             *button,
                         GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: stop button clicked");
        do_system_stop (login_window);
}

static void
on_user_chosen (GdmUserChooserWidget  *user_chooser,
                GdmGreeterLoginWindow *login_window)
{
        char *user_name;

        user_name = gdm_user_chooser_widget_get_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));
        if (user_name == NULL) {
                return;
        }

        g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                       0, user_name);

        if (strcmp (user_name, GDM_USER_CHOOSER_USER_OTHER) == 0) {
                g_signal_emit (login_window, signals[BEGIN_VERIFICATION], 0);
        } else if (strcmp (user_name, GDM_USER_CHOOSER_USER_GUEST) == 0) {
                /* FIXME: handle guest account stuff */
        } else if (strcmp (user_name, GDM_USER_CHOOSER_USER_AUTO) == 0) {
                g_signal_emit (login_window, signals[BEGIN_AUTO_LOGIN], 0,
                               login_window->priv->timed_login_username);

                login_window->priv->timed_login_enabled = TRUE;
                restart_timed_login_timeout (login_window);

                /* just wait for the user to select language and stuff */
                set_log_in_button_mode (login_window, LOGIN_BUTTON_TIMED_LOGIN);
                set_message (login_window, _("Select language and click Log In"));
        } else {
                g_signal_emit (login_window, signals[BEGIN_VERIFICATION_FOR_USER], 0, user_name);
        }

        switch_mode (login_window, MODE_AUTHENTICATION);

        g_free (user_name);
}

static void
on_user_unchosen (GdmUserChooserWidget  *user_chooser,
                  GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static gboolean
on_computer_info_label_button_press (GtkWidget             *widget,
                                     GdkEventButton        *event,
                                     GdmGreeterLoginWindow *login_window)
{
        GtkWidget *notebook;
        int        current_page;
        int        n_pages;

        /* switch page */
        notebook = glade_xml_get_widget (login_window->priv->xml, "computer-info-notebook");
        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
        n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

        if (current_page + 1 < n_pages) {
                gtk_notebook_next_page (GTK_NOTEBOOK (notebook));
        } else {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
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

static const char *known_etc_info_files [] = {
        "redhat-release",
        "SuSE-release",
        "gentoo-release",
        "arch-release",
        "debian_version",
        "mandriva-release",
        "slackware-version",
        NULL
};


static char *
get_system_version (void)
{
        char *version;
        int i;

        version = NULL;

        for (i = 0; known_etc_info_files [i]; i++) {
                char *path1;
                char *path2;

                path1 = g_build_filename (SYSCONFDIR, known_etc_info_files [i], NULL);
                path2 = g_build_filename ("/etc", known_etc_info_files [i], NULL);
                if (g_access (path1, R_OK) == 0) {
                        version = file_read_one_line (path1);
                } else if (g_access (path2, R_OK) == 0) {
                        version = file_read_one_line (path2);
                }
                g_free (path2);
                g_free (path1);
                if (version != NULL) {
                        break;
                }
        }

        if (version == NULL) {
                char *output;
                output = NULL;
                if (g_spawn_command_line_sync ("uname -sr", &output, NULL, NULL, NULL)) {
                        version = g_strchomp (output);
                }
        }

        return version;
}

static void
create_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *label;

        gdm_profile_start (NULL);

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

        gdm_profile_end (NULL);
}

#define INVISIBLE_CHAR_DEFAULT       '*'
#define INVISIBLE_CHAR_BLACK_CIRCLE  0x25cf
#define INVISIBLE_CHAR_WHITE_BULLET  0x25e6
#define INVISIBLE_CHAR_BULLET        0x2022
#define INVISIBLE_CHAR_NONE          0

static GtkWidget *
custom_widget_constructor (GladeXML              *xml,
                           char                  *func_name,
                           char                  *name,
                           char                  *string1,
                           char                  *string2,
                           int                    int1,
                           int                    int2,
                           GdmGreeterLoginWindow *login_window)
{
        GtkWidget *widget;

        g_assert (GLADE_IS_XML (xml));
        g_assert (name != NULL);
        g_assert (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        gdm_profile_start (NULL);

        widget = NULL;

        if (strcmp (name, "user-chooser") == 0) {
               widget = gdm_user_chooser_widget_new ();
        }

        gdm_profile_end (NULL);

        return widget;
}

static void
load_theme (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *box;
        GtkWidget *image;

        gdm_profile_start (NULL);

        glade_set_custom_handler ((GladeXMLCustomWidgetHandler) custom_widget_constructor,
                                  login_window);
        login_window->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE,
                                                 "window-box",
                                                 PACKAGE);

        g_assert (login_window->priv->xml != NULL);

        image = glade_xml_get_widget (login_window->priv->xml, "logo-image");
        if (image != NULL) {
                char        *icon_name;
                GError      *error;

                error = NULL;
                icon_name = gconf_client_get_string (login_window->priv->client, KEY_LOGO, &error);
                if (error != NULL) {
                        g_debug ("GdmGreeterLoginWindow: unable to get logo icon name: %s", error->message);
                        g_error_free (error);
                }

                g_debug ("GdmGreeterLoginWindow: Got greeter logo '%s'",
                          icon_name ? icon_name : "(null)");
                if (icon_name != NULL) {
                        gtk_image_set_from_icon_name (GTK_IMAGE (image),
                                                      icon_name,
                                                      GTK_ICON_SIZE_DIALOG);
                        g_free (icon_name);
                }
        }

        box = glade_xml_get_widget (login_window->priv->xml, "window-box");
        gtk_container_add (GTK_CONTAINER (login_window), box);

        login_window->priv->user_chooser =
                glade_xml_get_widget (login_window->priv->xml, "user-chooser");

        if (login_window->priv->user_chooser == NULL) {
                g_critical ("Userlist box not found");
        }

        gdm_user_chooser_widget_set_show_only_chosen (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        g_signal_connect (login_window->priv->user_chooser,
                          "activated",
                          G_CALLBACK (on_user_chosen),
                          login_window);
        g_signal_connect (login_window->priv->user_chooser,
                          "deactivated",
                          G_CALLBACK (on_user_unchosen),
                          login_window);

        gtk_widget_show (login_window->priv->user_chooser);

        login_window->priv->auth_banner_label = glade_xml_get_widget (login_window->priv->xml, "auth-banner-label");
        /*make_label_small_italic (login_window->priv->auth_banner_label);*/

        login_window->priv->auth_capslock_label = glade_xml_get_widget (login_window->priv->xml, "auth-capslock-label");

        button = glade_xml_get_widget (login_window->priv->xml, "suspend-button");
        g_signal_connect (button, "clicked", G_CALLBACK (suspend_button_clicked), login_window);

        button = glade_xml_get_widget (login_window->priv->xml, "cancel-button");
        g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), login_window);

        button = glade_xml_get_widget (login_window->priv->xml, "disconnect-button");
        g_signal_connect (button, "clicked", G_CALLBACK (disconnect_button_clicked), login_window);

        button = glade_xml_get_widget (login_window->priv->xml, "restart-button");
        g_signal_connect (button, "clicked", G_CALLBACK (restart_button_clicked), login_window);
        button = glade_xml_get_widget (login_window->priv->xml, "shutdown-button");
        g_signal_connect (button, "clicked", G_CALLBACK (shutdown_button_clicked), login_window);

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

        switch_mode (login_window, MODE_SELECTION);

        gdm_profile_end (NULL);
}

static gboolean
gdm_greeter_login_window_key_press_event (GtkWidget   *widget,
                                          GdkEventKey *event)
{
        GdmGreeterLoginWindow *login_window;
        gboolean               capslock_on;

        login_window = GDM_GREETER_LOGIN_WINDOW (widget);

        if (event->keyval == GDK_Escape) {
                if (login_window->priv->dialog_mode == MODE_AUTHENTICATION) {
                        do_cancel (GDM_GREETER_LOGIN_WINDOW (widget));
                }
        }

        capslock_on = is_capslock_on ();

        if (capslock_on != login_window->priv->caps_lock_on) {
                capslock_update (login_window, capslock_on);
        }

        return GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->key_press_event (widget, event);
}

static void
gdm_greeter_login_window_size_request (GtkWidget      *widget,
                                       GtkRequisition *requisition)
{
        int            screen_w;
        int            screen_h;
        GtkRequisition child_requisition;

        if (GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request (widget, requisition);
        }

        screen_w = gdk_screen_get_width (gtk_widget_get_screen (widget));
        screen_h = gdk_screen_get_height (gtk_widget_get_screen (widget));

        gtk_widget_get_child_requisition (GTK_BIN (widget)->child, &child_requisition);
        *requisition = child_requisition;

        requisition->width += 2 * GTK_CONTAINER (widget)->border_width;
        requisition->height += 2 * GTK_CONTAINER (widget)->border_width;

        requisition->width = MIN (requisition->width, .50 * screen_w);
        requisition->height = MIN (requisition->height, .80 * screen_h);
}

static void
update_banner_message (GdmGreeterLoginWindow *login_window)
{
        GError      *error;
        gboolean     enabled;

        if (login_window->priv->auth_banner_label == NULL) {
                /* if the theme doesn't have a banner message */
                g_debug ("GdmGreeterLoginWindow: theme doesn't support a banner message");
                return;
        }

        error = NULL;
        enabled = gconf_client_get_bool (login_window->priv->client, KEY_BANNER_MESSAGE_ENABLED, &error);
        if (error != NULL) {
                g_debug ("GdmGreeterLoginWindow: unable to get configuration: %s", error->message);
                g_error_free (error);
        }

        login_window->priv->banner_message_enabled = enabled;

        if (! enabled) {
                g_debug ("GdmGreeterLoginWindow: banner message disabled");
                gtk_widget_hide (login_window->priv->auth_banner_label);
        } else {
                char *message;
                error = NULL;
                message = gconf_client_get_string (login_window->priv->client, KEY_BANNER_MESSAGE_TEXT, &error);
                if (message != NULL) {
                        char *markup;
                        markup = g_markup_printf_escaped ("<small><i>%s</i></small>", message);
                        gtk_label_set_markup (GTK_LABEL (login_window->priv->auth_banner_label),
                                              markup);
                        g_free (markup);
                }
                g_debug ("GdmGreeterLoginWindow: banner message: %s", message);

                gtk_widget_show (login_window->priv->auth_banner_label);
        }
}

static GObject *
gdm_greeter_login_window_constructor (GType                  type,
                                      guint                  n_construct_properties,
                                      GObjectConstructParam *construct_properties)
{
        GdmGreeterLoginWindow      *login_window;

        gdm_profile_start (NULL);

        login_window = GDM_GREETER_LOGIN_WINDOW (G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->constructor (type,
                                                                                                                      n_construct_properties,
                                                                                                                      construct_properties));


        load_theme (login_window);
        update_banner_message (login_window);

        gdm_profile_end (NULL);

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

        widget_class->key_press_event = gdm_greeter_login_window_key_press_event;
        widget_class->size_request = gdm_greeter_login_window_size_request;

        signals [BEGIN_AUTO_LOGIN] =
                g_signal_new ("begin-auto-login",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_auto_login),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);
        signals [BEGIN_VERIFICATION] =
                g_signal_new ("begin-verification",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [BEGIN_VERIFICATION_FOR_USER] =
                g_signal_new ("begin-verification-for-user",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification_for_user),
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
        signals [START_SESSION] =
                g_signal_new ("start-session",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, start_session),
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
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_IS_INTERACTIVE,
                                         g_param_spec_boolean ("is-interactive",
                                                               "Is Interactive",
                                                               "Use has had an oppurtunity to interact with window",
                                                               FALSE,
                                                               G_PARAM_READABLE));

        g_type_class_add_private (klass, sizeof (GdmGreeterLoginWindowPrivate));
}

static void
on_gconf_key_changed (GConfClient           *client,
                      guint                  cnxn_id,
                      GConfEntry            *entry,
                      GdmGreeterLoginWindow *login_window)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);
        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_BANNER_MESSAGE_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        login_window->priv->banner_message_enabled = enabled;
                        update_banner_message (login_window);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_BANNER_MESSAGE_TEXT) == 0) {
                if (login_window->priv->banner_message_enabled) {
                        update_banner_message (login_window);
                }
        } else {
                g_debug ("Config key not handled: %s", key);
        }
}

static gboolean
on_window_state_event (GtkWidget           *widget,
                       GdkEventWindowState *event,
                       gpointer             data)
{
        if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
                g_debug ("GdmGreeterLoginWindow: window iconified");
                gtk_window_deiconify (GTK_WINDOW (widget));
        }

        return FALSE;
}

static void
gdm_greeter_login_window_init (GdmGreeterLoginWindow *login_window)
{
        gdm_profile_start (NULL);

        login_window->priv = GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE (login_window);

        login_window->priv->timed_login_enabled = FALSE;

        login_window->priv->dialog_mode = MODE_SELECTION;

        gtk_window_set_title (GTK_WINDOW (login_window), _("Login Window"));
        /*gtk_window_set_opacity (GTK_WINDOW (login_window), 0.85);*/
        gtk_window_set_position (GTK_WINDOW (login_window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_deletable (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_decorated (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_stick (GTK_WINDOW (login_window));
        gtk_container_set_border_width (GTK_CONTAINER (login_window), 25);


        g_signal_connect (login_window,
                          "window-state-event",
                          G_CALLBACK (on_window_state_event),
                          NULL);

        login_window->priv->client = gconf_client_get_default ();
        gconf_client_add_dir (login_window->priv->client,
                              KEY_GREETER_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        login_window->priv->gconf_cnxn = gconf_client_notify_add (login_window->priv->client,
                                                                  KEY_GREETER_DIR,
                                                                  (GConfClientNotifyFunc)on_gconf_key_changed,
                                                                  login_window,
                                                                  NULL,
                                                                  NULL);
        gdm_profile_end (NULL);
}

static void
gdm_greeter_login_window_finalize (GObject *object)
{
        GdmGreeterLoginWindow *login_window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (object));

        login_window = GDM_GREETER_LOGIN_WINDOW (object);

        g_return_if_fail (login_window->priv != NULL);

        if (login_window->priv->client != NULL) {
                g_object_unref (login_window->priv->client);
        }

        G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_login_window_new (gboolean is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_LOGIN_WINDOW,
                               "display-is-local", is_local,
                               "resizable", FALSE,
                               NULL);

        return GTK_WIDGET (object);
}
