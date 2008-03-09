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
#include <unistd.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gconf/gconf-client.h>

#include "gdm-greeter-session.h"
#include "gdm-greeter-client.h"
#include "gdm-greeter-panel.h"
#include "gdm-greeter-login-window.h"

#include "gdm-session-manager.h"
#include "gdm-session-client.h"

#define GDM_GREETER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SESSION, GdmGreeterSessionPrivate))

#define GSD_DBUS_NAME      "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH      "/org/gnome/SettingsDaemon"
#define GSD_DBUS_INTERFACE "org.gnome.SettingsDaemon"

#define KEY_GDM_A11Y_DIR            "/apps/gdm/simple-greeter/accessibility"
#define KEY_SCREEN_KEYBOARD_ENABLED  KEY_GDM_A11Y_DIR "/screen_keyboard_enabled"
#define KEY_SCREEN_MAGNIFIER_ENABLED KEY_GDM_A11Y_DIR "/screen_magnifier_enabled"
#define KEY_SCREEN_READER_ENABLED    KEY_GDM_A11Y_DIR "/screen_reader_enabled"

struct GdmGreeterSessionPrivate
{
        GdmGreeterClient      *client;
        GdmSessionManager     *manager;

        GdmSessionClient      *screen_reader_client;
        GdmSessionClient      *screen_keyboard_client;
        GdmSessionClient      *screen_magnifier_client;

        GtkWidget             *login_window;
        GtkWidget             *panel;

        guint                  was_interactive : 1;
};

enum {
        PROP_0,
};

static void     gdm_greeter_session_class_init  (GdmGreeterSessionClass *klass);
static void     gdm_greeter_session_init        (GdmGreeterSession      *greeter_session);
static void     gdm_greeter_session_finalize    (GObject                *object);

G_DEFINE_TYPE (GdmGreeterSession, gdm_greeter_session, G_TYPE_OBJECT)

static gpointer session_object = NULL;

static void
on_info (GdmGreeterClient  *client,
         const char        *text,
         GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Info: %s", text);

        gdm_greeter_login_window_info (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_problem (GdmGreeterClient  *client,
            const char        *text,
            GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Problem: %s", text);

        gdm_greeter_login_window_problem (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_ready (GdmGreeterClient  *client,
          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Ready");

        gdm_greeter_login_window_ready (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));
}

static void
on_reset (GdmGreeterClient  *client,
          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Reset");

        gdm_greeter_panel_reset (GDM_GREETER_PANEL (session->priv->panel));
        gdm_greeter_login_window_reset (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));

        session->priv->was_interactive = FALSE;
}

static void
on_selected_user_changed (GdmGreeterClient  *client,
                          const char        *text,
                          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: selected user changed: %s", text);
}

static void
on_default_language_name_changed (GdmGreeterClient  *client,
                                  const char        *text,
                                  GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: default language name changed: %s", text);
        gdm_greeter_panel_set_default_language_name (GDM_GREETER_PANEL (session->priv->panel),
                                                     text);
}

static void
on_default_session_name_changed (GdmGreeterClient  *client,
                                 const char        *text,
                                 GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: default session name changed: %s", text);
        gdm_greeter_panel_set_default_session_name (GDM_GREETER_PANEL (session->priv->panel),
                                                    text);
}

static void
on_info_query (GdmGreeterClient  *client,
               const char        *text,
               GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Info query: %s", text);

        gdm_greeter_login_window_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_secret_info_query (GdmGreeterClient  *client,
                      const char        *text,
                      GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Secret info query: %s", text);

        gdm_greeter_login_window_secret_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_begin_timed_login (GdmGreeterLoginWindow *login_window,
                    GdmGreeterSession     *session)
{
        gdm_greeter_client_call_begin_timed_login (session->priv->client);
}

static void
on_begin_verification (GdmGreeterLoginWindow *login_window,
                       GdmGreeterSession     *session)
{
        gdm_greeter_client_call_begin_verification (session->priv->client);
}

static void
on_begin_verification_for_user (GdmGreeterLoginWindow *login_window,
                                const char            *username,
                                GdmGreeterSession     *session)
{
        gdm_greeter_client_call_begin_verification_for_user (session->priv->client,
                                                             username);
}

static void
on_query_answer (GdmGreeterLoginWindow *login_window,
                 const char            *text,
                 GdmGreeterSession     *session)
{
        gdm_greeter_client_call_answer_query (session->priv->client,
                                              text);
}

static void
on_select_session (GdmGreeterSession     *session,
                   const char            *text)
{
        gdm_greeter_client_call_select_session (session->priv->client,
                                                text);
}

static void
on_select_language (GdmGreeterSession     *session,
                    const char            *text)
{
        gdm_greeter_client_call_select_language (session->priv->client,
                                                 text);
}

static void
on_select_user (GdmGreeterLoginWindow *login_window,
                const char            *text,
                GdmGreeterSession     *session)
{
        gdm_greeter_panel_show_user_options (GDM_GREETER_PANEL (session->priv->panel));
        gdm_greeter_client_call_select_user (session->priv->client,
                                             text);
}

static void
on_cancelled (GdmGreeterLoginWindow *login_window,
              GdmGreeterSession     *session)
{
        gdm_greeter_panel_hide_user_options (GDM_GREETER_PANEL (session->priv->panel));
        gdm_greeter_client_call_cancel (session->priv->client);
}

static void
on_disconnected (GdmGreeterLoginWindow *login_window,
                 GdmGreeterSession     *session)
{
        gdm_greeter_client_call_disconnect (session->priv->client);
}

static void
on_interactive (GdmGreeterLoginWindow *login_window,
                  GdmGreeterSession     *session)
{
        if (!session->priv->was_interactive) {
                g_debug ("GdmGreeterSession: session was interactive\n");

                /* We've blocked the UI already for the user to answer a question,
                 * so we know they've had an opportunity to interact with
                 * language chooser session selector, etc, and we can start the
                 * session right away.
                 */
                gdm_greeter_client_call_start_session_when_ready (session->priv->client,
                                                                  TRUE);
                session->priv->was_interactive = TRUE;
        }
}

static void
toggle_panel (GdmSessionManager *manager,
              gboolean           enabled,
              GdmGreeterSession *session)
{
        if (enabled) {
                session->priv->panel = gdm_greeter_panel_new ();

                g_signal_connect_swapped (session->priv->panel,
                                          "language-selected",
                                          G_CALLBACK (on_select_language),
                                          session);

                g_signal_connect_swapped (session->priv->panel,
                                          "session-selected",
                                          G_CALLBACK (on_select_session),
                                          session);

                gtk_widget_show (session->priv->panel);
        } else {
                gtk_widget_destroy (session->priv->panel);
                session->priv->panel = NULL;
        }
}

static void
toggle_login_window (GdmSessionManager *manager,
                     gboolean           enabled,
                     GdmGreeterSession *session)
{
        if (enabled) {
                gboolean is_local;

                is_local = gdm_greeter_client_get_display_is_local (session->priv->client);
                g_debug ("GdmGreeterSession: Starting a login window local:%d", is_local);
                session->priv->login_window = gdm_greeter_login_window_new (is_local);

                g_signal_connect (session->priv->login_window,
                                  "begin-timed-login",
                                  G_CALLBACK (on_begin_timed_login),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "begin-verification",
                                  G_CALLBACK (on_begin_verification),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "begin-verification-for-user",
                                  G_CALLBACK (on_begin_verification_for_user),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "query-answer",
                                  G_CALLBACK (on_query_answer),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "user-selected",
                                  G_CALLBACK (on_select_user),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "cancelled",
                                  G_CALLBACK (on_cancelled),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "disconnected",
                                  G_CALLBACK (on_disconnected),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "interactive",
                                  G_CALLBACK (on_interactive),
                                  session);
                gtk_widget_show (session->priv->login_window);
        } else {
                gtk_widget_destroy (session->priv->login_window);
                session->priv->login_window = NULL;
        }
}

static gboolean
launch_compiz (GdmGreeterSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmGreeterSession: Launching compiz");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("gtk-window-decorator --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        error = NULL;
        g_spawn_command_line_async ("compiz --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

        /* FIXME: should try to detect if it actually works */

 out:
        return ret;
}

static gboolean
launch_metacity (GdmGreeterSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmGreeterSession: Launching metacity");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("metacity --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
start_window_manager (GdmGreeterSession *session)
{
        if (! launch_metacity (session)) {
                launch_compiz (session);
        }
}

static void
toggle_screen_reader (GdmGreeterSession *session,
                      gboolean           enabled)
{
        g_debug ("GdmGreeterSession: screen reader toggled: %d", enabled);
        gdm_session_client_set_enabled (session->priv->screen_reader_client, enabled);
}

static void
toggle_screen_magnifier (GdmGreeterSession *session,
                         gboolean           enabled)
{
        g_debug ("GdmGreeterSession: screen magnifier toggled: %d", enabled);
        gdm_session_client_set_enabled (session->priv->screen_magnifier_client, enabled);
}

static void
toggle_screen_keyboard (GdmGreeterSession *session,
                        gboolean           enabled)
{
        g_debug ("GdmGreeterSession: screen keyboard toggled: %d", enabled);
        gdm_session_client_set_enabled (session->priv->screen_keyboard_client, enabled);
}

static void
on_a11y_key_changed (GConfClient       *client,
                     guint              cnxn_id,
                     GConfEntry        *entry,
                     GdmGreeterSession *session)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);
        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_SCREEN_READER_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        toggle_screen_reader (session, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }

        } else if (strcmp (key, KEY_SCREEN_MAGNIFIER_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        toggle_screen_magnifier (session, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }

        } else if (strcmp (key, KEY_SCREEN_KEYBOARD_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        toggle_screen_keyboard (session, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }

        } else {
        }
}

static void
setup_at_tools (GdmGreeterSession *session)
{
        GConfClient *client;
        gboolean     enabled;

        client = gconf_client_get_default ();
        gconf_client_add_dir (client,
                              KEY_GDM_A11Y_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        gconf_client_notify_add (client,
                                 KEY_GDM_A11Y_DIR,
                                 (GConfClientNotifyFunc)on_a11y_key_changed,
                                 session,
                                 NULL,
                                 NULL);

        session->priv->screen_keyboard_client = gdm_session_client_new ();
        gdm_session_client_set_name (session->priv->screen_keyboard_client,
                                     "On-screen Keyboard");
        gdm_session_client_set_try_exec (session->priv->screen_keyboard_client,
                                         "gok");
        gdm_session_client_set_command (session->priv->screen_keyboard_client,
                                        "gok --login");
        enabled = gconf_client_get_bool (client, KEY_SCREEN_KEYBOARD_ENABLED, NULL);
        gdm_session_client_set_enabled (session->priv->screen_keyboard_client,
                                        enabled);


        session->priv->screen_reader_client = gdm_session_client_new ();
        gdm_session_client_set_name (session->priv->screen_reader_client,
                                     "Screen Reader");
        gdm_session_client_set_try_exec (session->priv->screen_reader_client,
                                         "orca");
        gdm_session_client_set_command (session->priv->screen_reader_client,
                                        "orca -n");
        enabled = gconf_client_get_bool (client, KEY_SCREEN_READER_ENABLED, NULL);
        gdm_session_client_set_enabled (session->priv->screen_reader_client,
                                        enabled);


        session->priv->screen_magnifier_client = gdm_session_client_new ();
        gdm_session_client_set_name (session->priv->screen_magnifier_client,
                                     "Screen Magnifier");
        gdm_session_client_set_try_exec (session->priv->screen_magnifier_client,
                                         "magnifier");
        gdm_session_client_set_command (session->priv->screen_magnifier_client,
                                        "magnifier -v -m");
        enabled = gconf_client_get_bool (client, KEY_SCREEN_MAGNIFIER_ENABLED, NULL);
        gdm_session_client_set_enabled (session->priv->screen_magnifier_client,
                                        enabled);

        gdm_session_manager_add_client (session->priv->manager,
                                        session->priv->screen_reader_client,
                                        GDM_SESSION_LEVEL_LOGIN_WINDOW);
        gdm_session_manager_add_client (session->priv->manager,
                                        session->priv->screen_keyboard_client,
                                        GDM_SESSION_LEVEL_LOGIN_WINDOW);
        gdm_session_manager_add_client (session->priv->manager,
                                        session->priv->screen_magnifier_client,
                                        GDM_SESSION_LEVEL_LOGIN_WINDOW);

        g_object_unref (client);
}

static gboolean
send_dbus_string_method (DBusConnection *connection,
                         const char     *method,
                         const char     *payload)
{
        DBusError       error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        const char     *str;

        if (payload != NULL) {
                str = payload;
        } else {
                str = "";
        }

        g_debug ("GdmGreeterSession: Calling %s", method);
        message = dbus_message_new_method_call (GSD_DBUS_NAME,
                                                GSD_DBUS_PATH,
                                                GSD_DBUS_INTERFACE,
                                                method);
        if (message == NULL) {
                g_warning ("Couldn't allocate the D-Bus message");
                return FALSE;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter,
                                        DBUS_TYPE_STRING,
                                        &str);

        dbus_error_init (&error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &error);

        dbus_message_unref (message);

        if (dbus_error_is_set (&error)) {
                g_warning ("%s %s raised: %s\n",
                           method,
                           error.name,
                           error.message);
                return FALSE;
        }
        dbus_message_unref (reply);
        dbus_connection_flush (connection);

        return TRUE;
}

static gboolean
activate_settings_daemon (GdmGreeterSession *session)
{
        gboolean         ret;
        gboolean         res;
        DBusError        local_error;
        DBusConnection  *connection;

        g_debug ("GdmGreeterLoginWindow: activating settings daemon");

        dbus_error_init (&local_error);
        connection = dbus_bus_get (DBUS_BUS_SESSION, &local_error);
        if (connection == NULL) {
                g_debug ("Failed to connect to the D-Bus daemon: %s", local_error.message);
                dbus_error_free (&local_error);
                return FALSE;
        }

        res = send_dbus_string_method (connection,
                                       "StartWithSettingsPrefix",
                                       "/apps/gdm/simple-greeter/settings-manager-plugins");
        if (! res) {
                g_warning ("Couldn't start settings daemon");
                goto out;
        }
        ret = TRUE;
        g_debug ("GdmGreeterLoginWindow: settings daemon started");
 out:
        return ret;
}

static gboolean
start_settings_daemon (GdmGreeterSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmGreeterSession: Launching settings daemon");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async (LIBEXECDIR "/gnome-settings-daemon --gconf-prefix=/apps/gdm/simple-greeter/settings-manager-plugins", &error);
        if (error != NULL) {
                g_warning ("Error starting settings daemon: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
toggle_all_levels (GdmSessionManager *manager,
                   gboolean           enabled,
                   GdmGreeterSession *session)
{
        if (enabled) {
                activate_settings_daemon (session);
                start_window_manager (session);
        } else {
        }
}

gboolean
gdm_greeter_session_start (GdmGreeterSession *session,
                           GError           **error)
{
        gboolean res;

        g_return_val_if_fail (GDM_IS_GREETER_SESSION (session), FALSE);

        res = gdm_greeter_client_start (session->priv->client, error);

        gdm_session_manager_set_level (session->priv->manager, GDM_SESSION_LEVEL_LOGIN_WINDOW);

        return res;
}

void
gdm_greeter_session_stop (GdmGreeterSession *session)
{
        g_return_if_fail (GDM_IS_GREETER_SESSION (session));

}

static void
gdm_greeter_session_set_property (GObject        *object,
                                  guint           prop_id,
                                  const GValue   *value,
                                  GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_session_get_property (GObject        *object,
                                  guint           prop_id,
                                  GValue         *value,
                                  GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_greeter_session_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        GdmGreeterSession      *greeter_session;

        greeter_session = GDM_GREETER_SESSION (G_OBJECT_CLASS (gdm_greeter_session_parent_class)->constructor (type,
                                                                                                               n_construct_properties,
                                                                                                               construct_properties));

        return G_OBJECT (greeter_session);
}

static void
gdm_greeter_session_dispose (GObject *object)
{
        g_debug ("GdmGreeterSession: Disposing greeter_session");

        G_OBJECT_CLASS (gdm_greeter_session_parent_class)->dispose (object);
}

static void
gdm_greeter_session_class_init (GdmGreeterSessionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_greeter_session_get_property;
        object_class->set_property = gdm_greeter_session_set_property;
        object_class->constructor = gdm_greeter_session_constructor;
        object_class->dispose = gdm_greeter_session_dispose;
        object_class->finalize = gdm_greeter_session_finalize;

        g_type_class_add_private (klass, sizeof (GdmGreeterSessionPrivate));
}

static void
gdm_greeter_session_event_handler(GdkEvent          *event,
                                  GdmGreeterSession *session)
{
        g_assert (GDM_IS_GREETER_SESSION (session));

        if (event->type == GDK_KEY_PRESS) {
                GdkEventKey *key_event;

                key_event = (GdkEventKey *) event;
                if (session->priv->panel != NULL) {
                        if (gtk_window_activate_key (GTK_WINDOW (session->priv->panel),
                                                     key_event)) {
                                gtk_window_present_with_time (GTK_WINDOW (session->priv->panel),
                                                              key_event->time);
                                return;
                        }
                }

                if (session->priv->login_window != NULL) {
                        if (gtk_window_activate_key (GTK_WINDOW (session->priv->login_window),
                                                     ((GdkEventKey *) event))) {
                                gtk_window_present_with_time (GTK_WINDOW (session->priv->login_window),
                                                              key_event->time);
                                return;
                        }
                }
        }

        gtk_main_do_event (event);
}

static void
gdm_greeter_session_init (GdmGreeterSession *session)
{

        session->priv = GDM_GREETER_SESSION_GET_PRIVATE (session);

        session->priv->manager = gdm_session_manager_new ();
        gdm_session_manager_load_system_dirs (session->priv->manager);

        gdm_session_manager_add_notify (session->priv->manager,
                                        GDM_SESSION_LEVEL_LOGIN_WINDOW,
                                        (GdmSessionLevelNotifyFunc)toggle_login_window,
                                        session);
        gdm_session_manager_add_notify (session->priv->manager,
                                        GDM_SESSION_LEVEL_LOGIN_WINDOW,
                                        (GdmSessionLevelNotifyFunc)toggle_panel,
                                        session);
        gdm_session_manager_add_notify (session->priv->manager,
                                        GDM_SESSION_ALL_LEVELS,
                                        (GdmSessionLevelNotifyFunc)toggle_all_levels,
                                        session);

        session->priv->client = gdm_greeter_client_new ();
        g_signal_connect (session->priv->client,
                          "info-query",
                          G_CALLBACK (on_info_query),
                          session);
        g_signal_connect (session->priv->client,
                          "secret-info-query",
                          G_CALLBACK (on_secret_info_query),
                          session);
        g_signal_connect (session->priv->client,
                          "info",
                          G_CALLBACK (on_info),
                          session);
        g_signal_connect (session->priv->client,
                          "problem",
                          G_CALLBACK (on_problem),
                          session);
        g_signal_connect (session->priv->client,
                          "ready",
                          G_CALLBACK (on_ready),
                          session);
        g_signal_connect (session->priv->client,
                          "reset",
                          G_CALLBACK (on_reset),
                          session);
        g_signal_connect (session->priv->client,
                          "selected-user-changed",
                          G_CALLBACK (on_selected_user_changed),
                          session);
        g_signal_connect (session->priv->client,
                          "default-language-name-changed",
                          G_CALLBACK (on_default_language_name_changed),
                          session);
        g_signal_connect (session->priv->client,
                          "default-session-name-changed",
                          G_CALLBACK (on_default_session_name_changed),
                          session);

        /* We want to listen for panel mnemonics even if the
         * login window is focused, so we intercept them here.
         */
        gdk_event_handler_set ((GdkEventFunc) gdm_greeter_session_event_handler,
                               session, NULL);

        /* FIXME: we should really do this in settings daemon */
        setup_at_tools (session);
}

static void
gdm_greeter_session_finalize (GObject *object)
{
        GdmGreeterSession *greeter_session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_SESSION (object));

        greeter_session = GDM_GREETER_SESSION (object);

        g_return_if_fail (greeter_session->priv != NULL);

        G_OBJECT_CLASS (gdm_greeter_session_parent_class)->finalize (object);
}

GdmGreeterSession *
gdm_greeter_session_new (void)
{
        if (session_object != NULL) {
                g_object_ref (session_object);
        } else {
                session_object = g_object_new (GDM_TYPE_GREETER_SESSION, NULL);
                g_object_add_weak_pointer (session_object,
                                           (gpointer *) &session_object);
        }

        return GDM_GREETER_SESSION (session_object);
}
