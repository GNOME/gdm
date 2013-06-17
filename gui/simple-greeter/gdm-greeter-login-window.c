/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008, 2009 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Written by: William Jon McCann <mccann@jhu.edu>
 *             Ray Strode <rstrode@redhat.com>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include <gtk/gtk.h>

#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"
#include "gdm-profile.h"

#include "gdm-client.h"
#include "gdm-greeter-login-window.h"
#include "gdm-user-chooser-widget.h"
#include "gdm-session-option-widget.h"
#include "gdm-extension-list.h"

#include "extensions/unified/gdm-unified-extension.h"

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

#define UI_XML_FILE       "gdm-greeter-login-window.ui"

#define LOGIN_SCREEN_SCHEMA         "org.gnome.login-screen"
#define KEY_BANNER_MESSAGE_ENABLED  "banner-message-enable"
#define KEY_BANNER_MESSAGE_TEXT     "banner-message-text"
#define KEY_LOGO                    "fallback-logo"
#define KEY_DISABLE_USER_LIST       "disable-user-list"

#define LSB_RELEASE_COMMAND "lsb_release -d"

#define GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_LOGIN_WINDOW, GdmGreeterLoginWindowPrivate))
#define GDM_CUSTOM_SESSION "custom"

#define INFO_MESSAGE_DURATION 2
#define PROBLEM_MESSAGE_DURATION 3

enum {
        MODE_UNDEFINED = 0,
        MODE_TIMED_LOGIN,
        MODE_SELECTION,
        MODE_AUTHENTICATION,
        MODE_MULTIPLE_AUTHENTICATION,
};

enum {
        LOGIN_BUTTON_HIDDEN = 0,
        LOGIN_BUTTON_ANSWER_QUERY,
        LOGIN_BUTTON_TIMED_LOGIN
};

struct GdmGreeterLoginWindowPrivate
{
        GtkBuilder      *builder;
        GtkWidget       *session_option_widget;
        GtkWidget       *user_chooser;
        GtkWidget       *extension_list;
        GtkWidget       *auth_banner_label;
        GtkWidget       *current_button;
        GtkWidget       *auth_page_box;
        guint            display_is_local : 1;
        guint            user_chooser_loaded : 1;
        GSettings       *settings;
        GList           *extensions;
        GdmLoginExtension *active_extension;
        GList           *extensions_to_enable;
        GList           *extensions_to_stop;

        gboolean         banner_message_enabled;
        gulong           gsettings_cnxn;

        guint            last_mode;
        guint            dialog_mode;
        guint            next_mode;

        gboolean         user_list_disabled;
        guint            num_queries;

        gboolean         timed_login_already_enabled;
        gboolean         timed_login_enabled;
        guint            timed_login_delay;
        char            *timed_login_username;
        guint            timed_login_timeout_id;

        guint            login_button_handler_id;
        guint            start_session_handler_id;

        char            *service_name_of_session_ready_to_start;
};

enum {
        PROP_0,
        PROP_DISPLAY_IS_LOCAL,
};

enum {
        START_CONVERSATION,
        BEGIN_AUTO_LOGIN,
        BEGIN_VERIFICATION,
        BEGIN_VERIFICATION_FOR_USER,
        QUERY_ANSWER,
        START_SESSION,
        USER_SELECTED,
        SESSION_SELECTED,
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

static void     switch_mode                 (GdmGreeterLoginWindow *login_window,
                                             int                    number);
static void     update_banner_message       (GdmGreeterLoginWindow *login_window);
static void     reset_dialog                (GdmGreeterLoginWindow *login_window,
                                             guint                  dialog_mode);
static void     gdm_greeter_login_window_start_session_when_ready (GdmGreeterLoginWindow *login_window,
                                                                   const char            *service_name);
static void     handle_stopped_conversation (GdmGreeterLoginWindow *login_window,
                                             const char            *service_name);

static void     begin_single_service_verification (GdmGreeterLoginWindow *login_window,
                                                   const char            *service_name);

G_DEFINE_TYPE (GdmGreeterLoginWindow, gdm_greeter_login_window, GTK_TYPE_WINDOW)

static void
set_busy (GdmGreeterLoginWindow *login_window)
{
        GdkCursor *cursor;

        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (login_window)), cursor);
        g_object_unref (cursor);
}

static void
set_ready (GdmGreeterLoginWindow *login_window)
{
        gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (login_window)), NULL);
}

static void
set_sensitive (GdmGreeterLoginWindow *login_window,
               gboolean               sensitive)
{
        GtkWidget *box;

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "buttonbox"));
        gtk_widget_set_sensitive (box, sensitive);

        gtk_widget_set_sensitive (login_window->priv->user_chooser, sensitive);
}

static void
set_focus (GdmGreeterLoginWindow *login_window)
{
        gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);

        if (login_window->priv->active_extension != NULL &&
            gdm_login_extension_focus (login_window->priv->active_extension)) {
                char *name;
                name = gdm_login_extension_get_name (login_window->priv->active_extension);
                g_debug ("GdmGreeterLoginWindow: focusing extension %s", name);
                g_free (name);
        } else if (gtk_widget_get_realized (login_window->priv->user_chooser) && ! gtk_widget_has_focus (login_window->priv->user_chooser)) {
                gtk_widget_grab_focus (login_window->priv->user_chooser);
        }

}

static gboolean
queue_message_for_extension (GdmLoginExtension *extension,
                             const char          *message)
{
        gdm_login_extension_queue_message (extension,
                                             GDM_SERVICE_MESSAGE_TYPE_INFO,
                                             message);
        return FALSE;
}

static void
set_message (GdmGreeterLoginWindow *login_window,
             const char            *text)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_list_foreach (login_window->priv->extensions,
                        (GFunc) queue_message_for_extension,
                        (gpointer) text);
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

static gboolean
timed_login_timer (GdmGreeterLoginWindow *login_window)
{
        set_sensitive (login_window, FALSE);
        set_message (login_window, _("Automatically logging in…"));

        g_debug ("GdmGreeterLoginWindow: timer expired");
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

        widget = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, name));
        if (widget != NULL) {
                if (visible) {
                        gtk_widget_show (widget);
                } else {
                        gtk_widget_hide (widget);
                }
        }
}

static void
hide_extension_actions (GdmLoginExtension *extension)
{
        GtkActionGroup *actions;

        actions = gdm_login_extension_get_actions (extension);

        if (actions != NULL) {
                gtk_action_group_set_visible (actions, FALSE);
                gtk_action_group_set_sensitive (actions, FALSE);
                g_object_unref (actions);
        }
}

static void
grab_default_button_for_extension (GdmLoginExtension *extension)
{
        GtkActionGroup *actions;
        GtkAction *action;
        GSList    *proxies, *node;

        actions = gdm_login_extension_get_actions (extension);

        if (actions == NULL) {
                return;
        }

        action = gtk_action_group_get_action (actions, GDM_LOGIN_EXTENSION_DEFAULT_ACTION);
        g_object_unref (actions);

        if (action == NULL) {
                return;
        }

        proxies = gtk_action_get_proxies (action);
        for (node = proxies; node != NULL; node = node->next) {
                GtkWidget *widget;

                widget = GTK_WIDGET (node->data);

                if (gtk_widget_get_can_default (widget) &&
                    gtk_widget_get_visible (widget)) {
                        gtk_widget_grab_default (widget);
                        break;
                }
        }
}

static void
show_extension_actions (GdmLoginExtension *extension)
{
        GtkActionGroup *actions;

        actions = gdm_login_extension_get_actions (extension);
        if (actions != NULL) {
                gtk_action_group_set_sensitive (actions, TRUE);
                gtk_action_group_set_visible (actions, TRUE);
                g_object_unref (actions);
        }
}

static void
on_login_button_clicked_timed_login (GtkButton             *button,
                                     GdmGreeterLoginWindow *login_window)
{
        set_busy (login_window);
        set_sensitive (login_window, FALSE);
}

static void
set_log_in_button_mode (GdmGreeterLoginWindow *login_window,
                        int                    mode)
{
        GtkWidget *button;
        GtkWidget *login_button;
        GtkWidget *unlock_button;
        char      *item;
        gboolean   in_use;

        in_use = FALSE;
        item = gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));
        if (item != NULL) {
                gboolean res;

                res = gdm_chooser_widget_lookup_item (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                      item,
                                                      NULL, /* image */
                                                      NULL, /* name */
                                                      NULL, /* comment */
                                                      NULL, /* priority */
                                                      &in_use,
                                                      NULL); /* is separate */

                if (!res) {
                        in_use = FALSE;
                }
        }

        if (login_window->priv->current_button != NULL) {
                /* disconnect any signals */
                if (login_window->priv->login_button_handler_id > 0) {
                        g_signal_handler_disconnect (login_window->priv->current_button,
                                                     login_window->priv->login_button_handler_id);
                        login_window->priv->login_button_handler_id = 0;
                }
        }

        unlock_button = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "unlock-button"));
        login_button = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "log-in-button"));

        if (in_use) {
                gtk_widget_hide (login_button);
                button = unlock_button;
        } else {
                gtk_widget_hide (unlock_button);
                button = login_button;
        }
        gtk_widget_grab_default (button);

        login_window->priv->current_button = button;

        g_list_foreach (login_window->priv->extensions, (GFunc) hide_extension_actions, NULL);

        switch (mode) {
        case LOGIN_BUTTON_HIDDEN:
                if (login_window->priv->active_extension != NULL) {
                        hide_extension_actions (login_window->priv->active_extension);
                }

                gtk_widget_hide (button);
                break;
        case LOGIN_BUTTON_ANSWER_QUERY:
                if (login_window->priv->active_extension != NULL) {
                        show_extension_actions (login_window->priv->active_extension);
                        grab_default_button_for_extension (login_window->priv->active_extension);
                }

                gtk_widget_hide (button);
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

static gboolean
user_chooser_has_no_user (GdmGreeterLoginWindow *login_window)
{
        guint num_items;

        num_items = gdm_chooser_widget_get_number_of_items (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));
        g_debug ("GdmGreeterLoginWindow: loaded=%d num_items=%d",
                 login_window->priv->user_chooser_loaded,
                 num_items);
        return (login_window->priv->user_chooser_loaded && num_items == 0);
}

static void
maybe_show_cancel_button (GdmGreeterLoginWindow *login_window)
{
        gboolean show;

        show = FALSE;

        /* only show the cancel button if there is something to go
           back to */

        switch (login_window->priv->dialog_mode) {
        case MODE_SELECTION:
                /* should never have anything to return to from here */
                show = FALSE;
                break;
        case MODE_TIMED_LOGIN:
                /* should always have something to return to from here */
                show = TRUE;
                break;
        case MODE_AUTHENTICATION:
        case MODE_MULTIPLE_AUTHENTICATION:
                if (login_window->priv->num_queries > 1) {
                        /* if we are inside a pam conversation past
                           the first step */
                        show = TRUE;
                } else {
                        if (login_window->priv->user_list_disabled || user_chooser_has_no_user (login_window)) {
                                show = FALSE;
                        } else {
                                show = TRUE;
                        }
                }
                break;
        default:
                g_assert_not_reached ();
        }

        show_widget (login_window, "cancel-button", show);
}

static void
update_extension_list_visibility (GdmGreeterLoginWindow *login_window)
{
        int number_of_extensions;

        if (login_window->priv->dialog_mode != MODE_MULTIPLE_AUTHENTICATION) {
                gtk_widget_hide (login_window->priv->extension_list);
                return;
        }

        number_of_extensions = gdm_extension_list_get_number_of_visible_extensions (GDM_EXTENSION_LIST (login_window->priv->extension_list));
        if (number_of_extensions > 1) {
                gtk_widget_show (login_window->priv->extension_list);
        } else {
                gtk_widget_hide (login_window->priv->extension_list);
        }
}

static void
switch_mode (GdmGreeterLoginWindow *login_window,
             int                    number)
{
        GtkWidget  *box;

        /* Should never switch to MODE_UNDEFINED */
        g_assert (number != MODE_UNDEFINED);

        /* we want to run this even if we're supposed to
           be in the mode already so that we reset everything
           to a known state */
        if (login_window->priv->dialog_mode != number) {
                login_window->priv->last_mode = login_window->priv->dialog_mode;
                login_window->priv->dialog_mode = number;
        }

        login_window->priv->next_mode = MODE_UNDEFINED;

        switch (number) {
        case MODE_SELECTION:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_HIDDEN);
                set_sensitive (login_window, TRUE);
                gtk_widget_hide (login_window->priv->session_option_widget);
                break;
        case MODE_TIMED_LOGIN:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_TIMED_LOGIN);
                set_sensitive (login_window, TRUE);
                gtk_widget_show (login_window->priv->session_option_widget);
                break;
        case MODE_AUTHENTICATION:
        case MODE_MULTIPLE_AUTHENTICATION:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
                set_sensitive (login_window, FALSE);
                gtk_widget_show (login_window->priv->session_option_widget);
                break;
        default:
                g_assert_not_reached ();
        }

        show_widget (login_window, "auth-input-box", FALSE);
        update_extension_list_visibility (login_window);
        maybe_show_cancel_button (login_window);

        /*
         * The rest of this function sets up the user list, so just return if
         * the user list is disabled.
         */
        if (login_window->priv->user_list_disabled && number != MODE_TIMED_LOGIN) {
                return;
        }

        box = gtk_widget_get_parent (login_window->priv->user_chooser);
        if (GTK_IS_BOX (box)) {
                guint       padding;
                GtkPackType pack_type;

                gtk_box_query_child_packing (GTK_BOX (box),
                                             login_window->priv->user_chooser,
                                             NULL,
                                             NULL,
                                             &padding,
                                             &pack_type);
                gtk_box_set_child_packing (GTK_BOX (box),
                                           login_window->priv->user_chooser,
                                           number == MODE_SELECTION,
                                           number == MODE_SELECTION,
                                           padding,
                                           pack_type);
        }
}

static GdmLoginExtension *
find_extension_with_service_name (GdmGreeterLoginWindow *login_window,
                                  const char            *service_name)
{
        GList *node;

        node = login_window->priv->extensions;
        while (node != NULL) {
                GdmLoginExtension *extension;
                char *extension_service_name;
                gboolean has_service_name;

                extension = GDM_LOGIN_EXTENSION (node->data);

                extension_service_name = gdm_login_extension_get_service_name (extension);
                has_service_name = strcmp (service_name, extension_service_name) == 0;
                g_free (extension_service_name);

                if (has_service_name) {
                        return extension;
                }

                node = node->next;
        }

        return NULL;
}

static gboolean
reset_extension (GdmLoginExtension   *extension,
                 GdmGreeterLoginWindow *login_window)
{
        char *name;

        name = gdm_login_extension_get_name (extension);
        g_debug ("Resetting extension '%s'", name);
        g_free (name);

        login_window->priv->extensions_to_enable = g_list_remove (login_window->priv->extensions_to_enable, extension);

        hide_extension_actions (extension);
        gdm_extension_list_remove_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list), extension);
        gdm_login_extension_reset (extension);
        return FALSE;
}

static gboolean
extensions_are_enabled (GdmGreeterLoginWindow *login_window)
{

        GList *node;

        node = login_window->priv->extensions;
        while (node != NULL) {
                GdmLoginExtension *extension;

                extension = GDM_LOGIN_EXTENSION (node->data);

                if (!gdm_login_extension_is_enabled (extension)) {
                        return FALSE;
                }

                node = node->next;
        }

        return TRUE;
}

static gboolean
can_jump_to_authenticate (GdmGreeterLoginWindow *login_window)
{
        gboolean res;

        if (!login_window->priv->user_chooser_loaded) {
                res = FALSE;
        } else if (!extensions_are_enabled (login_window)) {
                res = FALSE;
        } else if (login_window->priv->dialog_mode == MODE_AUTHENTICATION) {
                res = FALSE;
        } else if (login_window->priv->dialog_mode == MODE_MULTIPLE_AUTHENTICATION) {
                res = FALSE;
        } else if (login_window->priv->user_list_disabled) {
                res = (login_window->priv->timed_login_username == NULL);
        } else {
                res = user_chooser_has_no_user (login_window);
        }

        return res;
}

static void
begin_other_verification (GdmGreeterLoginWindow *login_window)
{
        /* FIXME: we should drop this code and do all OTHER handling
         * entirely from within the extension
         * (ala how smart card manages its "Smartcard Authentication" item)
         */
        if (find_extension_with_service_name (login_window, "gdm-password") != NULL) {
                begin_single_service_verification (login_window, "gdm-password");
        } else {
                begin_single_service_verification (login_window, "gdm");
        }
}

static void
set_extension_active (GdmGreeterLoginWindow *login_window,
                      GdmLoginExtension     *extension)
{
        GtkWidget *container;
        char *name;

        name = gdm_login_extension_get_name (extension);
        g_debug ("GdmGreeterLoginWindow: extension '%s' activated", name);
        g_free (name);

        container = g_object_get_data (G_OBJECT (extension),
                                       "gdm-greeter-login-window-page-container");

        if (container == NULL) {
                GtkWidget *page;

                container = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
                gtk_container_add (GTK_CONTAINER (login_window->priv->auth_page_box),
                                   container);

                page = gdm_login_extension_get_page (extension);
                if (page != NULL) {
                        gtk_container_add (GTK_CONTAINER (container), page);
                        gtk_widget_show (page);
                }
                g_object_set_data (G_OBJECT (extension),
                                   "gdm-greeter-login-window-page-container",
                                   container);
        }

        gtk_widget_show (container);

        login_window->priv->active_extension = extension;
        switch_mode (login_window, login_window->priv->dialog_mode);
}

static void
clear_active_extension (GdmGreeterLoginWindow *login_window)
{

        GtkWidget *container;
        GtkActionGroup *actions;

        if (login_window->priv->active_extension == NULL) {
                return;
        }

        container = g_object_get_data (G_OBJECT (login_window->priv->active_extension),
                                       "gdm-greeter-login-window-page-container");

        if (container != NULL) {
                gtk_widget_hide (container);
        }

        actions = gdm_login_extension_get_actions (login_window->priv->active_extension);

        if (actions != NULL) {
                gtk_action_group_set_sensitive (actions, FALSE);
                gtk_action_group_set_visible (actions, FALSE);
                g_object_unref (actions);
        }

        login_window->priv->active_extension = NULL;
}

static void
reset_dialog (GdmGreeterLoginWindow *login_window,
              guint                  dialog_mode)
{
        g_debug ("GdmGreeterLoginWindow: Resetting dialog to mode %u", dialog_mode);
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        login_window->priv->num_queries = 0;

        g_free (login_window->priv->service_name_of_session_ready_to_start);
        login_window->priv->service_name_of_session_ready_to_start = NULL;

        if (dialog_mode == MODE_SELECTION) {
                if (login_window->priv->timed_login_enabled) {
                        gdm_chooser_widget_set_item_timer (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                           GDM_USER_CHOOSER_USER_AUTO, 0);
                        remove_timed_login_timeout (login_window);
                        login_window->priv->timed_login_enabled = FALSE;
                }

                g_signal_handlers_block_by_func (G_OBJECT (login_window->priv->user_chooser),
                                                 G_CALLBACK (on_user_unchosen), login_window);
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), NULL);
                g_signal_handlers_unblock_by_func (G_OBJECT (login_window->priv->user_chooser),
                                                   G_CALLBACK (on_user_unchosen), login_window);

                if (login_window->priv->start_session_handler_id > 0) {
                        g_signal_handler_disconnect (login_window, login_window->priv->start_session_handler_id);
                        login_window->priv->start_session_handler_id = 0;
                }

                set_message (login_window, "");
        }

        g_list_foreach (login_window->priv->extensions, (GFunc) reset_extension, login_window);

        if (can_jump_to_authenticate (login_window)) {
                /* If we don't have a user list jump straight to authenticate */
                g_debug ("GdmGreeterLoginWindow: jumping straight to authenticate");
                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_other_verification (login_window);
        } else {
                clear_active_extension (login_window);
                switch_mode (login_window, dialog_mode);
        }

        gtk_widget_set_sensitive (login_window->priv->extension_list, TRUE);
        set_ready (login_window);
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));
        update_banner_message (login_window);

        if (gdm_chooser_widget_get_number_of_items (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser)) >= 1) {
                gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));
        }
}

static void
restart_conversations (GdmGreeterLoginWindow *login_window)
{
        set_busy (login_window);
        set_sensitive (login_window, FALSE);
        g_signal_emit (login_window, signals[CANCELLED], 0);
}

static gboolean
has_queued_messages (GdmGreeterLoginWindow *login_window)
{
        GList *node;

        node = login_window->priv->extensions;
        while (node != NULL) {
                GdmLoginExtension *extension;

                extension = (GdmLoginExtension *) node->data;

                if (gdm_login_extension_has_queued_messages (extension)) {
                        return TRUE;
                }
                node = node->next;
        }

        return FALSE;
}

static void
reset_dialog_after_messages (GdmGreeterLoginWindow *login_window,
                             guint                  dialog_mode)
{
        if (has_queued_messages (login_window)) {
                g_debug ("GdmGreeterLoginWindow: will reset dialog after pending messages");
                login_window->priv->next_mode = dialog_mode;
        } else {
                g_debug ("GdmGreeterLoginWindow: resetting dialog");
                reset_dialog (login_window, dialog_mode);
        }

}

static void
do_cancel (GdmGreeterLoginWindow *login_window)
{
        /* need to wait for response from backend */
        set_message (login_window, _("Cancelling…"));
        restart_conversations (login_window);
        reset_dialog_after_messages (login_window, MODE_SELECTION);
}

gboolean
gdm_greeter_login_window_ready (GdmGreeterLoginWindow *login_window,
                                const char            *service_name)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                if (!login_window->priv->user_chooser_loaded) {
                        g_debug ("GdmGreeterLoginWindow: Ignoring daemon Ready event since not loaded yet");
                        login_window->priv->extensions_to_enable = g_list_prepend (login_window->priv->extensions_to_enable,
                                                                              extension);
                        return TRUE;
                } else if (login_window->priv->next_mode != MODE_UNDEFINED) {
                        g_debug ("GdmGreeterLoginWindow: Ignoring daemon Ready event since still showing messages");
                        login_window->priv->extensions_to_enable = g_list_prepend (login_window->priv->extensions_to_enable,
                                                                              extension);
                        return TRUE;
                }

                gdm_login_extension_set_ready (extension);
        }

        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));
        gdk_window_beep (gtk_widget_get_window (GTK_WIDGET (login_window)));

        /* If the user list is disabled, then start the PAM conversation */
        if (can_jump_to_authenticate (login_window)) {
                g_debug ("Starting PAM conversation since user list disabled or no local users");
                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_other_verification (login_window);
        }

        return TRUE;
}

static void
handle_stopped_conversation (GdmGreeterLoginWindow *login_window,
                             const char            *service_name)
{
        GdmLoginExtension *extension;

        /* If the password conversation failed, then start over
         *
         * FIXME: we need to get this policy out of the source code
         */
        if (strcmp (service_name, "gdm-password") == 0 ||
            strcmp (service_name, "gdm") == 0) {
                g_debug ("GdmGreeterLoginWindow: main conversation failed, starting over");
                restart_conversations (login_window);
                reset_dialog_after_messages (login_window, MODE_SELECTION);
                return;
        }

        if (login_window->priv->dialog_mode == MODE_AUTHENTICATION) {
                g_debug ("GdmGreeterLoginWindow: conversation failed, starting over");
                restart_conversations (login_window);
                reset_dialog_after_messages (login_window, MODE_AUTHENTICATION);
                return;
        } else if (login_window->priv->dialog_mode != MODE_MULTIPLE_AUTHENTICATION) {
                g_warning ("conversation %s stopped when it shouldn't have been running (mode %d)",
                           service_name, login_window->priv->dialog_mode);
                restart_conversations (login_window);
                return;
        }

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                gdm_login_extension_reset (extension);

                login_window->priv->extensions_to_stop = g_list_remove (login_window->priv->extensions_to_stop, extension);
        }

        /* If every conversation has failed, then just start over.
         */
        extension = gdm_extension_list_get_active_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list));

        if (extension == NULL || !gdm_login_extension_is_enabled (extension)) {
                g_debug ("GdmGreeterLoginWindow: No conversations left, starting over");
                restart_conversations (login_window);
                reset_dialog_after_messages (login_window, MODE_SELECTION);
        }

        if (extension != NULL) {
                g_object_unref (extension);
        }

        update_extension_list_visibility (login_window);
}

gboolean
gdm_greeter_login_window_conversation_stopped (GdmGreeterLoginWindow *login_window,
                                               const char            *service_name)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("GdmGreeterLoginWindow: conversation '%s' has stopped", service_name);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL && gdm_login_extension_is_enabled (extension)) {
                if (gdm_login_extension_has_queued_messages (extension)) {
                        login_window->priv->extensions_to_stop = g_list_prepend (login_window->priv->extensions_to_stop, extension);
                } else {
                        handle_stopped_conversation (login_window, service_name);
                }
        }

        return TRUE;
}

static gboolean
restart_extension_conversation (GdmLoginExtension     *extension,
                                GdmGreeterLoginWindow *login_window)
{
        char *service_name;

        login_window->priv->extensions_to_stop = g_list_remove (login_window->priv->extensions_to_stop, extension);

        service_name = gdm_login_extension_get_service_name (extension);
        if (service_name != NULL) {
                char *name;

                name = gdm_login_extension_get_name (extension);
                g_debug ("GdmGreeterLoginWindow: restarting '%s' conversation", name);
                g_free (name);

                g_signal_emit (login_window, signals[START_CONVERSATION], 0, service_name);
                g_free (service_name);
        }

        return FALSE;
}

gboolean
gdm_greeter_login_window_reset (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: window reset");

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        reset_dialog_after_messages (login_window, MODE_SELECTION);
        g_list_foreach (login_window->priv->extensions,
                        (GFunc) restart_extension_conversation,
                        login_window);

        g_free (login_window->priv->service_name_of_session_ready_to_start);
        login_window->priv->service_name_of_session_ready_to_start = NULL;

        return TRUE;
}

gboolean
gdm_greeter_login_window_info (GdmGreeterLoginWindow *login_window,
                               const char            *service_name,
                               const char            *text)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: info: %s", text);

        maybe_show_cancel_button (login_window);
        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                gdm_login_extension_queue_message (extension,
                                                     GDM_SERVICE_MESSAGE_TYPE_INFO,
                                                     text);
                show_extension_actions (extension);
        }

        return TRUE;
}

gboolean
gdm_greeter_login_window_problem (GdmGreeterLoginWindow *login_window,
                                  const char            *service_name,
                                  const char            *text)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: problem: %s", text);
        maybe_show_cancel_button (login_window);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                gdm_login_extension_queue_message (extension,
                                                     GDM_SERVICE_MESSAGE_TYPE_PROBLEM,
                                                     text);
                show_extension_actions (extension);
        }

        return TRUE;
}

static void
request_timed_login (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: requesting timed login");

        gtk_widget_show (login_window->priv->user_chooser);

        if (login_window->priv->dialog_mode != MODE_SELECTION) {
                reset_dialog (login_window, MODE_SELECTION);
        }

        if (!login_window->priv->timed_login_already_enabled) {
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                              GDM_USER_CHOOSER_USER_AUTO);
        }

        login_window->priv->timed_login_already_enabled = TRUE;
}

gboolean
gdm_greeter_login_window_service_unavailable (GdmGreeterLoginWindow *login_window,
                                              const char            *service_name)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: service unavailable: %s", service_name);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                GdmLoginExtension *active_extension;

                gdm_login_extension_set_enabled (extension, FALSE);

                active_extension = gdm_extension_list_get_active_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list));

                if (active_extension == extension) {
                        restart_conversations (login_window);
                }

                if (active_extension != NULL) {
                        g_object_unref (active_extension);
                }
        }

        return TRUE;
}

void
gdm_greeter_login_window_request_timed_login (GdmGreeterLoginWindow *login_window,
                                              const char            *username,
                                              int                    delay)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: requested automatic login for user '%s' in %d seconds", username, delay);

        g_free (login_window->priv->timed_login_username);
        login_window->priv->timed_login_username = g_strdup (username);
        login_window->priv->timed_login_delay = delay;

        /* add the auto user right away so we won't trigger a mode
           switch to authenticate when the user list is disabled */
        gdm_user_chooser_widget_set_show_user_auto (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        /* if the users aren't loaded then we'll handle it in when they are */
        if (login_window->priv->user_chooser_loaded) {
                g_debug ("Handling timed login request since users are already loaded.");
                request_timed_login (login_window);
        } else {
                g_debug ("Waiting to handle timed login request until users are loaded.");
        }
}

static void
gdm_greeter_login_window_start_session (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: starting session");
        g_signal_emit (login_window,
                       signals[START_SESSION],
                       0,
                       login_window->priv->service_name_of_session_ready_to_start);
        g_free (login_window->priv->service_name_of_session_ready_to_start);
        login_window->priv->service_name_of_session_ready_to_start = NULL;
}

static void
gdm_greeter_login_window_start_session_when_ready (GdmGreeterLoginWindow *login_window,
                                                   const char            *service_name)
{
        GdmLoginExtension *extension;

        extension = find_extension_with_service_name (login_window, service_name);

        login_window->priv->service_name_of_session_ready_to_start = g_strdup (service_name);

        if (!gdm_login_extension_has_queued_messages (extension)) {
                g_debug ("GdmGreeterLoginWindow: starting session");
                g_signal_emit (login_window, signals[START_SESSION], 0, service_name);
                gdm_greeter_login_window_start_session (login_window);
        }
}

gboolean
gdm_greeter_login_window_info_query (GdmGreeterLoginWindow *login_window,
                                     const char            *service_name,
                                     const char            *text)
{
        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        login_window->priv->num_queries++;
        maybe_show_cancel_button (login_window);

        g_debug ("GdmGreeterLoginWindow: info query: %s", text);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                gdm_login_extension_ask_question (extension, text);
        }

        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));

        return TRUE;
}

gboolean
gdm_greeter_login_window_secret_info_query (GdmGreeterLoginWindow *login_window,
                                            const char            *service_name,
                                            const char            *text)
{

        GdmLoginExtension *extension;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        login_window->priv->num_queries++;
        maybe_show_cancel_button (login_window);

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension != NULL) {
                gdm_login_extension_ask_secret (extension, text);
        }

        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));

        return TRUE;
}

void
gdm_greeter_login_window_session_opened (GdmGreeterLoginWindow *login_window,
                                          const char            *service_name)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: session now opened via service %s",
                 service_name);

        gdm_greeter_login_window_start_session_when_ready (login_window,
                                                           service_name);
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
cancel_button_clicked (GtkButton             *button,
                       GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
on_user_chooser_visibility_changed (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: Chooser visibility changed");
        update_banner_message (login_window);
}

static gboolean
begin_extension_verification_for_selected_user (GdmLoginExtension     *extension,
                                                GdmGreeterLoginWindow *login_window)
{
        char *user_name;
        char *service_name;

        user_name = gdm_user_chooser_widget_get_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));

        if (user_name == NULL) {
                return TRUE;
        }

        service_name = gdm_login_extension_get_service_name (extension);
        if (service_name != NULL) {
                g_signal_emit (login_window, signals[BEGIN_VERIFICATION_FOR_USER], 0, service_name, user_name);
                g_free (service_name);
        }

        gdm_extension_list_add_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list),
                                          extension);

        g_free (user_name);
        return FALSE;
}

static void
enable_waiting_extensions (GdmGreeterLoginWindow *login_window)
{
        GList *node;

        node = login_window->priv->extensions_to_enable;
        while (node != NULL) {
                GdmLoginExtension *extension;

                extension = GDM_LOGIN_EXTENSION (node->data);

                gdm_login_extension_set_ready (extension);

                node = node->next;
        }

        login_window->priv->extensions_to_enable = NULL;
}

static void
on_users_loaded (GdmUserChooserWidget  *user_chooser,
                 GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: users loaded");
        login_window->priv->user_chooser_loaded = TRUE;

        update_banner_message (login_window);

        gtk_widget_show (login_window->priv->user_chooser);

        enable_waiting_extensions (login_window);

        if (login_window->priv->timed_login_username != NULL
            && !login_window->priv->timed_login_already_enabled) {
                request_timed_login (login_window);
        } else if (can_jump_to_authenticate (login_window)) {

                gtk_widget_hide (login_window->priv->user_chooser);

                /* jump straight to authenticate */
                g_debug ("GdmGreeterLoginWindow: jumping straight to authenticate");
                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_other_verification (login_window);
        }
}

static void
choose_user (GdmGreeterLoginWindow *login_window,
             const char            *user_name)
{
        GdmLoginExtension *extension;

        g_assert (user_name != NULL);
        g_debug ("GdmGreeterLoginWindow: user chosen '%s'", user_name);

        g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                       0, user_name);

        g_list_foreach (login_window->priv->extensions,
                        (GFunc) begin_extension_verification_for_selected_user,
                        login_window);

        extension = gdm_extension_list_get_active_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list));
        set_extension_active (login_window, extension);
        g_object_unref (extension);

        switch_mode (login_window, MODE_MULTIPLE_AUTHENTICATION);
        update_extension_list_visibility (login_window);
}

static void
begin_auto_login (GdmGreeterLoginWindow *login_window)
{
        g_signal_emit (login_window, signals[BEGIN_AUTO_LOGIN], 0,
                       login_window->priv->timed_login_username);

        login_window->priv->timed_login_enabled = TRUE;
        restart_timed_login_timeout (login_window);

        /* just wait for the user to select language and stuff */
        set_message (login_window, _("Select language and click Log In"));

        clear_active_extension (login_window);
        switch_mode (login_window, MODE_TIMED_LOGIN);

        show_widget (login_window, "conversation-list", FALSE);
        g_list_foreach (login_window->priv->extensions,
                        (GFunc) reset_extension,
                        login_window);
}

static void
reset_extension_if_not_given (GdmLoginExtension  *extension,
                              GdmLoginExtension  *given_extension)
{
        if (extension == given_extension) {
                return;
        }

        gdm_login_extension_reset (extension);
}

static void
reset_every_extension_but_given_extension (GdmGreeterLoginWindow *login_window,
                                 GdmLoginExtension   *extension)
{
        g_list_foreach (login_window->priv->extensions,
                        (GFunc) reset_extension_if_not_given,
                        extension);

}

static void
begin_single_service_verification (GdmGreeterLoginWindow *login_window,
                                   const char            *service_name)
{
        GdmLoginExtension *extension;

        extension = find_extension_with_service_name (login_window, service_name);

        if (extension == NULL) {
                g_debug ("GdmGreeterLoginWindow: %s has no extension associated with it", service_name);
                return;
        }

        g_debug ("GdmGreeterLoginWindow: Beginning %s auth conversation", service_name);

        /* FIXME: we should probably give the plugin more say for
         * what happens here.
         */
        g_signal_emit (login_window, signals[BEGIN_VERIFICATION], 0, service_name);

        reset_every_extension_but_given_extension (login_window, extension);

        set_extension_active (login_window, extension);
        switch_mode (login_window, MODE_AUTHENTICATION);

        show_widget (login_window, "conversation-list", FALSE);
}

static void
on_user_chooser_activated (GdmUserChooserWidget  *user_chooser,
                           GdmGreeterLoginWindow *login_window)
{
        char *user_name;
        char *item_id;

        user_name = gdm_user_chooser_widget_get_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));

        if (user_name != NULL) {
                g_debug ("GdmGreeterLoginWindow: user chosen '%s'", user_name);
                choose_user (login_window, user_name);
                g_free (user_name);
                return;
        }

        item_id = gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (user_chooser));
        g_debug ("GdmGreeterLoginWindow: item chosen '%s'", item_id);

        g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                       0, item_id);

        if (strcmp (item_id, GDM_USER_CHOOSER_USER_OTHER) == 0) {
                g_debug ("GdmGreeterLoginWindow: Starting all auth conversations");
                g_free (item_id);

                begin_other_verification (login_window);
        } else if (strcmp (item_id, GDM_USER_CHOOSER_USER_GUEST) == 0) {
                /* FIXME: handle guest account stuff */
                g_free (item_id);
        } else if (strcmp (item_id, GDM_USER_CHOOSER_USER_AUTO) == 0) {
                g_debug ("GdmGreeterLoginWindow: Starting auto login");
                g_free (item_id);

                begin_auto_login (login_window);
        } else {
                g_debug ("GdmGreeterLoginWindow: Starting single auth conversation");
                begin_single_service_verification (login_window, item_id);
                g_free (item_id);
        }
}

static void
on_user_unchosen (GdmUserChooserWidget  *user_chooser,
                  GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
on_session_activated (GdmSessionOptionWidget *session_option_widget,
                      GdmGreeterLoginWindow  *login_window)
{
        char *session;

        session = gdm_session_option_widget_get_current_session (GDM_SESSION_OPTION_WIDGET (login_window->priv->session_option_widget));
        if (session == NULL) {
                return;
        }

        g_signal_emit (login_window, signals[SESSION_SELECTED], 0, session);

        g_free (session);
}

void
gdm_greeter_login_window_set_default_session_name (GdmGreeterLoginWindow *login_window,
                                                   const char            *session_name)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        if (session_name != NULL && !gdm_option_widget_lookup_item (GDM_OPTION_WIDGET (login_window->priv->session_option_widget),
                                            session_name, NULL, NULL, NULL)) {
                if (strcmp (session_name, GDM_CUSTOM_SESSION) == 0) {
                        gdm_option_widget_add_item (GDM_OPTION_WIDGET (login_window->priv->session_option_widget),
                                                    GDM_CUSTOM_SESSION,
                                                    C_("customsession", "Custom"),
                                                    _("Custom session"),
                                                    GDM_OPTION_WIDGET_POSITION_TOP);
                } else {
                        g_warning ("Default session is not available");
                        return;
                }
        }

        gdm_option_widget_set_default_item (GDM_OPTION_WIDGET (login_window->priv->session_option_widget),
                                            session_name);
}

static void
rotate_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *notebook;
        int        current_page;
        int        n_pages;

        /* switch page */
        notebook = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-notebook"));
        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
        n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

        if (current_page + 1 < n_pages) {
                gtk_notebook_next_page (GTK_NOTEBOOK (notebook));
        } else {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
        }

}

static gboolean
on_computer_info_label_button_press (GtkWidget             *widget,
                                     GdkEventButton        *event,
                                     GdmGreeterLoginWindow *login_window)
{
        rotate_computer_info (login_window);
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
                goto out;
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
        "system-release",
        NULL
};


static char *
get_system_version (void)
{
        char *version;
        char *output;
        int i;

        version = NULL;

        output = NULL;
        if (g_spawn_command_line_sync (LSB_RELEASE_COMMAND, &output, NULL, NULL, NULL)) {
                if (g_str_has_prefix (output, "Description:")) {
                        version = g_strdup (output + strlen ("Description:"));
                } else {
                        version = g_strdup (output);
                }
                version = g_strstrip (version);

                /* lsb_release returns (none) if it doesn't know,
                 * so return NULL in that case */
                if (strcmp (version, "(none)") == 0) {
                        g_free (version);
                        version = NULL;
                }

                g_free (output);

                goto out;
        }

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
                output = NULL;
                if (g_spawn_command_line_sync ("uname -sr", &output, NULL, NULL, NULL)) {
                        version = g_strchomp (output);
                }
        }
 out:
        return version;
}

static void
create_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *label;

        gdm_profile_start (NULL);

        label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-name-label"));
        if (label != NULL) {
                char localhost[HOST_NAME_MAX + 1] = "";

                if (gethostname (localhost, HOST_NAME_MAX) == 0) {
                        gtk_label_set_text (GTK_LABEL (label), localhost);
                }

                /* If this isn't actually unique identifier for the computer, then
                 * don't bother showing it by default.
                 */
                if (strcmp (localhost, "localhost") == 0 ||
                    strcmp (localhost, "localhost.localdomain") == 0) {

                    rotate_computer_info (login_window);
                }
        }

        label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-version-label"));
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

static void
on_extension_activated (GdmGreeterLoginWindow *login_window,
                        GdmLoginExtension   *extension)
{
        set_extension_active (login_window, extension);
}

static void
on_extension_deactivated (GdmGreeterLoginWindow *login_window,
                          GdmLoginExtension   *extension)
{
        char *name;

        if (login_window->priv->active_extension != extension) {
                g_warning ("inactive extension has been deactivated");
                return;
        }

        name = gdm_login_extension_get_name (extension);
        g_debug ("GdmGreeterLoginWindow: extension '%s' now in background", name);
        g_free (name);

        clear_active_extension (login_window);

        login_window->priv->active_extension = gdm_extension_list_get_active_extension (GDM_EXTENSION_LIST (login_window->priv->extension_list));
        g_object_unref (login_window->priv->active_extension);
}

static void
register_custom_types (GdmGreeterLoginWindow *login_window)
{
        GType types[] = { GDM_TYPE_USER_CHOOSER_WIDGET,
                          GDM_TYPE_SESSION_OPTION_WIDGET,
                          GDM_TYPE_EXTENSION_LIST };
        int i;

        for (i = 0; i < G_N_ELEMENTS (types); i++) {
                g_debug ("Registering type '%s'", g_type_name (types[i]));
        }
}

static void
load_theme (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *button;
        GtkWidget *box;
        GtkWidget *image;
        GError* error = NULL;

        gdm_profile_start (NULL);

        register_custom_types (login_window);

        login_window->priv->builder = gtk_builder_new ();
        if (!gtk_builder_add_from_file (login_window->priv->builder, UIDIR "/" UI_XML_FILE, &error)) {
                g_warning ("Couldn't load builder file: %s", error->message);
                g_error_free (error);
        }

        g_assert (login_window->priv->builder != NULL);

        image = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "logo-image"));
        if (image != NULL) {
                GdkPixbuf *pixbuf;
                char *path;

                path = g_settings_get_string (login_window->priv->settings, KEY_LOGO);
                g_debug ("GdmGreeterLoginWindow: Got greeter logo '%s'", path);

                pixbuf = gdk_pixbuf_new_from_file_at_scale (path, -1, 48, TRUE, NULL);
                g_free (path);

                if (pixbuf != NULL) {
                        gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
                        g_object_unref (pixbuf);
                }
        }

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "window-frame"));
        gtk_container_add (GTK_CONTAINER (login_window), box);
        gtk_widget_grab_default(GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder,
                                                                    "log-in-button")));

        login_window->priv->user_chooser = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "user-chooser"));

        gdm_user_chooser_widget_set_show_only_chosen (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        g_signal_connect (login_window->priv->user_chooser,
                          "loaded",
                          G_CALLBACK (on_users_loaded),
                          login_window);
        g_signal_connect (login_window->priv->user_chooser,
                          "activated",
                          G_CALLBACK (on_user_chooser_activated),
                          login_window);
        g_signal_connect (login_window->priv->user_chooser,
                          "deactivated",
                          G_CALLBACK (on_user_unchosen),
                          login_window);

        g_signal_connect_swapped (login_window->priv->user_chooser,
                                 "notify::list-visible",
                                 G_CALLBACK (on_user_chooser_visibility_changed),
                                 login_window);

        login_window->priv->session_option_widget = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "session-option-widget"));

        g_signal_connect (login_window->priv->session_option_widget,
                          "activated",
                          G_CALLBACK (on_session_activated),
                          login_window);

        login_window->priv->extension_list = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "extension-list"));

        g_signal_connect_swapped (GDM_EXTENSION_LIST (login_window->priv->extension_list),
                                  "activated",
                                  G_CALLBACK (on_extension_activated),
                                  login_window);
        g_signal_connect_swapped (GDM_EXTENSION_LIST (login_window->priv->extension_list),
                                  "deactivated",
                                  G_CALLBACK (on_extension_deactivated),
                                  login_window);

        login_window->priv->auth_banner_label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "auth-banner-label"));
        /*make_label_small_italic (login_window->priv->auth_banner_label);*/
        login_window->priv->auth_page_box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "auth-page-box"));

        button = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "cancel-button"));
        g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), login_window);

        create_computer_info (login_window);

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-event-box"));
        g_signal_connect (box, "button-press-event", G_CALLBACK (on_computer_info_label_button_press), login_window);

        clear_active_extension (login_window);
        switch_mode (login_window, MODE_SELECTION);

        gdm_profile_end (NULL);
}

static gboolean
gdm_greeter_login_window_key_press_event (GtkWidget   *widget,
                                          GdkEventKey *event)
{
        GdmGreeterLoginWindow *login_window;

        login_window = GDM_GREETER_LOGIN_WINDOW (widget);

        if (event->keyval == GDK_KEY_Escape) {
                if (login_window->priv->dialog_mode == MODE_AUTHENTICATION
                    || login_window->priv->dialog_mode == MODE_TIMED_LOGIN) {
                        do_cancel (GDM_GREETER_LOGIN_WINDOW (widget));
                }
        }

        return GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->key_press_event (widget, event);
}

static void
gdm_greeter_login_window_get_preferred_width (GtkWidget *widget,
                                              gint      *minimum_size,
                                              gint      *natural_size)
{
        int             monitor;
        GdkScreen      *screen;
        GdkWindow      *window;
        GdkRectangle    area;
        GtkAllocation   widget_allocation;
        int             min_size;
        int             nat_size;

        gtk_widget_get_preferred_width (gtk_bin_get_child (GTK_BIN (widget)),
                                        &min_size,
                                        &nat_size);

        /* Make width be at least 33% screen width */
        screen = gtk_widget_get_screen (widget);
        window = gtk_widget_get_window (widget);
        if (window == NULL) {
                window = gdk_screen_get_root_window (screen);
        }
        monitor = gdk_screen_get_monitor_at_window (screen, window);
        gdk_screen_get_monitor_geometry (screen, monitor, &area);
        min_size = MAX (min_size, .33 * area.width);
        nat_size = MAX (nat_size, .33 * area.width);

       /* Don't ever shrink window width */
        gtk_widget_get_allocation (widget, &widget_allocation);

        min_size = MAX (min_size, widget_allocation.width);
        nat_size = MAX (nat_size, widget_allocation.width);

        if (minimum_size)
                *minimum_size = min_size;
        if (natural_size)
                *natural_size = nat_size;
}

static void
gdm_greeter_login_window_get_preferred_height (GtkWidget *widget,
                                               gint      *minimum_size,
                                               gint      *natural_size)
{
        int             monitor;
        GdkScreen      *screen;
        GdkWindow      *window;
        GdkRectangle    area;
        int             min_size;
        int             nat_size;

        gtk_widget_get_preferred_height (gtk_bin_get_child (GTK_BIN (widget)),
                                        &min_size,
                                        &nat_size);

        /* Make height be at most 80% of screen height */
        screen = gtk_widget_get_screen (widget);
        window = gtk_widget_get_window (widget);
        if (window == NULL) {
                window = gdk_screen_get_root_window (screen);
        }
        monitor = gdk_screen_get_monitor_at_window (screen, window);
        gdk_screen_get_monitor_geometry (screen, monitor, &area);
        min_size = MIN (min_size, .8 * area.height);
        nat_size = MIN (nat_size, .8 * area.height);

        if (minimum_size)
                *minimum_size = min_size;
        if (natural_size)
                *natural_size = nat_size;
}

static void
update_banner_message (GdmGreeterLoginWindow *login_window)
{
        gboolean     enabled;

        if (login_window->priv->auth_banner_label == NULL) {
                /* if the theme doesn't have a banner message */
                g_debug ("GdmGreeterLoginWindow: theme doesn't support a banner message");
                return;
        }

        enabled = g_settings_get_boolean (login_window->priv->settings, KEY_BANNER_MESSAGE_ENABLED);

        login_window->priv->banner_message_enabled = enabled;

        if (! enabled) {
                g_debug ("GdmGreeterLoginWindow: banner message disabled");
                gtk_widget_hide (login_window->priv->auth_banner_label);
        } else {
                char *message;

                message = g_settings_get_string (login_window->priv->settings,
                                                 KEY_BANNER_MESSAGE_TEXT);

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
        GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

        object_class->get_property = gdm_greeter_login_window_get_property;
        object_class->set_property = gdm_greeter_login_window_set_property;
        object_class->constructor = gdm_greeter_login_window_constructor;
        object_class->finalize = gdm_greeter_login_window_finalize;

        widget_class->key_press_event = gdm_greeter_login_window_key_press_event;
        widget_class->get_preferred_width = gdm_greeter_login_window_get_preferred_width;
        widget_class->get_preferred_height = gdm_greeter_login_window_get_preferred_height;

        gtk_container_class_handle_border_width (container_class);

        signals [START_CONVERSATION] =
                g_signal_new ("start-conversation",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, start_conversation),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);
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
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [BEGIN_VERIFICATION_FOR_USER] =
                g_signal_new ("begin-verification-for-user",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification_for_user),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2, G_TYPE_STRING, G_TYPE_STRING);
        signals [QUERY_ANSWER] =
                g_signal_new ("query-answer",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, query_answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2, G_TYPE_STRING, G_TYPE_STRING);
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
        signals [START_SESSION] =
                g_signal_new ("start-session",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, start_session),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_type_class_add_private (klass, sizeof (GdmGreeterLoginWindowPrivate));
}

static void
on_gsettings_key_changed (GSettings             *settings,
                          gchar                 *key,
                          gpointer               user_data)
{
        GdmGreeterLoginWindow *login_window;

        login_window = GDM_GREETER_LOGIN_WINDOW (user_data);

        if (strcmp (key, KEY_BANNER_MESSAGE_ENABLED) == 0) {
                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);

                g_debug ("setting key %s = %d", key, enabled);

                login_window->priv->banner_message_enabled = enabled;
                update_banner_message (login_window);

        } else if (strcmp (key, KEY_BANNER_MESSAGE_TEXT) == 0) {
                if (login_window->priv->banner_message_enabled) {
                        update_banner_message (login_window);
                }
        } else {
                g_debug ("GdmGreeterLoginWindow: Config key not handled: %s", key);
        }
}

static void
on_login_extension_answer (GdmGreeterLoginWindow *login_window,
                             const char            *text,
                             GdmLoginExtension   *extension)
{
        if (text != NULL) {
                char *service_name;

                service_name = gdm_login_extension_get_service_name (extension);
                if (service_name != NULL) {
                        g_signal_emit (login_window, signals[QUERY_ANSWER], 0, service_name, text);
                        g_free (service_name);
                }
        }

        set_sensitive (login_window, TRUE);
        set_ready (login_window);
}

static void
on_login_extension_cancel (GdmGreeterLoginWindow *login_window,
                             GdmLoginExtension   *extension)
{
        restart_conversations (login_window);
}

static gboolean
on_login_extension_chose_user (GdmGreeterLoginWindow *login_window,
                                 const char            *username,
                                 GdmLoginExtension   *extension)
{
        if (!login_window->priv->user_chooser_loaded) {
                char *name;

                name = gdm_login_extension_get_name (extension);
                g_warning ("Task %s is trying to choose user before list is loaded", name);
                g_free (name);
                return FALSE;
        }

        /* If we're already authenticating then we can't pick a user
         */
        if (login_window->priv->dialog_mode == MODE_AUTHENTICATION || login_window->priv->dialog_mode == MODE_MULTIPLE_AUTHENTICATION) {
                return FALSE;
        }

        gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                      username);

        return TRUE;
}

static void
on_login_extension_message_queue_empty (GdmGreeterLoginWindow *login_window,
                                          GdmLoginExtension   *extension)
{
        gboolean needs_to_be_stopped;

        needs_to_be_stopped = g_list_find (login_window->priv->extensions_to_stop, extension) != NULL;

        if (needs_to_be_stopped) {
                char *service_name;

                service_name = gdm_login_extension_get_service_name (extension);
                handle_stopped_conversation (login_window, service_name);
                g_free (service_name);
        }

        if (login_window->priv->service_name_of_session_ready_to_start != NULL) {
                if (login_window->priv->active_extension == extension) {
                        gdm_greeter_login_window_start_session (login_window);
                }
        } else if (login_window->priv->next_mode != MODE_UNDEFINED) {
                reset_dialog_after_messages (login_window, login_window->priv->next_mode);
        }
}

static void
on_button_action_label_changed (GtkWidget *button)
{
        GtkAction *action;
        char *text;

        action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (button));

        g_object_get (G_OBJECT (action), "label", &text, NULL);

        gtk_button_set_label (GTK_BUTTON (button), text);
        g_free (text);
}

static void
on_button_action_icon_name_changed (GtkWidget *button)
{
        GtkAction *action;
        GtkWidget *image;

        action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (button));

        if (gtk_action_get_is_important (action)) {
                image = gtk_action_create_icon (GTK_ACTION (action), GTK_ICON_SIZE_BUTTON);
        } else {
                image = NULL;
        }

        gtk_button_set_image (GTK_BUTTON (button), image);

}

static void
on_button_action_tooltip_changed (GtkWidget *button)
{
        GtkAction *action;
        char *text;

        action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (button));

        g_object_get (G_OBJECT (action), "tooltip", &text, NULL);

        gtk_widget_set_tooltip_text (button, text);
        g_free (text);
}

static GtkWidget *
create_button_from_action (GtkAction *action)
{
        GtkWidget *button;

        button = gtk_button_new ();

        gtk_activatable_set_related_action (GTK_ACTIVATABLE (button), action);

        g_signal_connect_swapped (action,
                                  "notify::label",
                                  G_CALLBACK (on_button_action_label_changed),
                                  button);
        g_signal_connect_swapped (action,
                                  "notify::icon-name",
                                  G_CALLBACK (on_button_action_icon_name_changed),
                                  button);
        g_signal_connect_swapped (action,
                                  "notify::tooltip",
                                  G_CALLBACK (on_button_action_tooltip_changed),
                                  button);

        on_button_action_label_changed (button);
        on_button_action_icon_name_changed (button);
        on_button_action_tooltip_changed (button);

        if (strcmp (gtk_action_get_name (action),
                    GDM_LOGIN_EXTENSION_DEFAULT_ACTION) == 0) {
                gtk_widget_set_can_default (button, TRUE);
        }

        return button;
}

static void
create_buttons_for_actions (GdmGreeterLoginWindow *login_window,
                            GtkActionGroup        *actions)
{
        GList *action_list;
        GList *node;
        GtkWidget *box;

        action_list = gtk_action_group_list_actions (actions);

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "buttonbox"));
        for (node = action_list; node != NULL; node = node->next) {
                GtkAction *action;
                GtkWidget *button;

                action = node->data;

                button = create_button_from_action (action);
                gtk_container_add (GTK_CONTAINER (box), button);
        }

        g_list_free (action_list);
}

static void
gdm_greeter_login_window_add_extension (GdmGreeterLoginWindow *login_window,
                                        GdmLoginExtension     *extension)
{
        char *name;
        char *description;
        char *service_name;
        GtkActionGroup *actions;

        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));
        g_return_if_fail (GDM_IS_LOGIN_EXTENSION (extension));

        name = gdm_login_extension_get_name (extension);
        description = gdm_login_extension_get_description (extension);

        if (!gdm_login_extension_is_visible (extension)) {
                g_debug ("GdmGreeterLoginWindow: new extension '%s - %s' won't be added",
                         name, description);
                g_free (name);
                g_free (description);
                return;
        }

        actions = gdm_login_extension_get_actions (extension);

        create_buttons_for_actions (login_window, actions);
        hide_extension_actions (extension);

        g_object_unref (actions);

        g_signal_connect_swapped (extension,
                                  "answer",
                                  G_CALLBACK (on_login_extension_answer),
                                  login_window);
        g_signal_connect_swapped (extension,
                                  "cancel",
                                  G_CALLBACK (on_login_extension_cancel),
                                  login_window);
        g_signal_connect_swapped (extension,
                                  "user-chosen",
                                  G_CALLBACK (on_login_extension_chose_user),
                                  login_window);
        g_signal_connect_swapped (extension,
                                  "message-queue-empty",
                                  G_CALLBACK (on_login_extension_message_queue_empty),
                                  login_window);

        g_debug ("GdmGreeterLoginWindow: new extension '%s - %s' added",
                name, description);

        login_window->priv->extensions = g_list_append (login_window->priv->extensions, extension);
        service_name = gdm_login_extension_get_service_name (extension);

        if (gdm_login_extension_is_choosable (extension)) {
                gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                             service_name, NULL, name, description, ~0,
                                             FALSE, TRUE, NULL, NULL);
        }

        g_free (name);
        g_free (description);

        g_debug ("GdmGreeterLoginWindow: starting conversation with '%s'", service_name);
        g_signal_emit (login_window, signals[START_CONVERSATION], 0, service_name);
        g_free (service_name);
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

static gboolean
load_login_extensions (GdmGreeterLoginWindow *login_window)
{
        GList *extensions, *node;
        GIOExtensionPoint *extension_point;

        g_debug ("GdmGreeterLoginWindow: loading extensions");

        extension_point = g_io_extension_point_register (GDM_LOGIN_EXTENSION_POINT_NAME);
        g_io_extension_point_set_required_type (extension_point,
                                                GDM_TYPE_LOGIN_EXTENSION);

        g_io_modules_load_all_in_directory (GDM_SIMPLE_GREETER_PLUGINS_DIR);

        extensions = g_io_extension_point_get_extensions (extension_point);

        if (extensions == NULL) {
                gdm_unified_extension_load ();
                extensions = g_io_extension_point_get_extensions (extension_point);
        }

        for (node = extensions; node != NULL; node = node->next) {
                GIOExtension *extension;
                GdmLoginExtension *login_extension;

                extension = (GIOExtension *) node->data;

                g_debug ("GdmGreeterLoginWindow: adding extension '%s'",
                         g_io_extension_get_name (extension));

                login_extension = g_object_new (g_io_extension_get_type (extension), NULL);

                gdm_greeter_login_window_add_extension (GDM_GREETER_LOGIN_WINDOW (login_window),
                                                        login_extension);
        }

        g_debug ("GdmGreeterLoginWindow: done loading extensions");

        return FALSE;
}

static void
gdm_greeter_login_window_init (GdmGreeterLoginWindow *login_window)
{
        GSettings *settings;
        gboolean   user_list_disable;

        gdm_profile_start (NULL);

        login_window->priv = GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE (login_window);
        login_window->priv->timed_login_enabled = FALSE;
        login_window->priv->dialog_mode = MODE_UNDEFINED;
        login_window->priv->next_mode = MODE_UNDEFINED;

        settings = g_settings_new (LOGIN_SCREEN_SCHEMA);

        /* The user list is not shown only if the user list is disabled and
         * timed login is also not being used.
         */
        user_list_disable = g_settings_get_boolean (settings, KEY_DISABLE_USER_LIST);

        login_window->priv->user_list_disabled = user_list_disable;

        gtk_window_set_title (GTK_WINDOW (login_window), _("Login Window"));
        /*gtk_window_set_opacity (GTK_WINDOW (login_window), 0.85);*/
        gtk_window_set_position (GTK_WINDOW (login_window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_deletable (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_decorated (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_keep_below (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_stick (GTK_WINDOW (login_window));
        gtk_container_set_border_width (GTK_CONTAINER (login_window), 0);

        g_signal_connect (login_window,
                          "window-state-event",
                          G_CALLBACK (on_window_state_event),
                          NULL);

        login_window->priv->settings = g_settings_new (LOGIN_SCREEN_SCHEMA);

        login_window->priv->gsettings_cnxn = g_signal_connect (login_window->priv->settings,
                                                               "changed",
                                                               G_CALLBACK (on_gsettings_key_changed),
                                                               login_window);

        g_idle_add ((GSourceFunc) load_login_extensions, login_window);
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

        if (login_window->priv->settings != NULL) {
                g_object_unref (login_window->priv->settings);
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
