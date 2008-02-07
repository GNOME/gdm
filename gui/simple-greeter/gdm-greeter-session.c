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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-greeter-session.h"
#include "gdm-greeter-client.h"
#include "gdm-greeter-panel.h"
#include "gdm-greeter-login-window.h"

#include "gdm-session-manager.h"
#include "gdm-session-client.h"

#define GDM_GREETER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SESSION, GdmGreeterSessionPrivate))

struct GdmGreeterSessionPrivate
{
        GdmGreeterClient      *client;
        GdmSessionManager     *manager;

        GtkWidget             *login_window;
        GtkWidget             *panel;
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

        gdm_greeter_panel_hide_user_options (GDM_GREETER_PANEL (session->priv->panel));
        gdm_greeter_login_window_reset (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));
}

static void
on_selected_user_changed (GdmGreeterClient  *client,
                          const char        *text,
                          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: selected user changed: %s", text);
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
                start_settings_daemon (session);
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
        GdmGreeterSession *self;

        self = GDM_GREETER_SESSION (object);

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
        GdmGreeterSession *self;

        self = GDM_GREETER_SESSION (object);

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
        GdmGreeterSessionClass *klass;

        klass = GDM_GREETER_SESSION_CLASS (g_type_class_peek (GDM_TYPE_GREETER_SESSION));

        greeter_session = GDM_GREETER_SESSION (G_OBJECT_CLASS (gdm_greeter_session_parent_class)->constructor (type,
                                                                                                               n_construct_properties,
                                                                                                               construct_properties));

        return G_OBJECT (greeter_session);
}

static void
gdm_greeter_session_dispose (GObject *object)
{
        GdmGreeterSession *greeter_session;

        greeter_session = GDM_GREETER_SESSION (object);

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
