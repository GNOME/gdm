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
#include "gdm-greeter-background.h"
#include "gdm-greeter-login-window.h"

typedef enum {
        GDM_GREETER_SESSION_LEVEL_NONE,
        GDM_GREETER_SESSION_LEVEL_STARTUP,
        GDM_GREETER_SESSION_LEVEL_CONFIGURATION,
        GDM_GREETER_SESSION_LEVEL_LOGIN_WINDOW,
        GDM_GREETER_SESSION_LEVEL_HOST_CHOOSER,
        GDM_GREETER_SESSION_LEVEL_REMOTE_HOST,
        GDM_GREETER_SESSION_LEVEL_SHUTDOWN,
} GdmGreeterSessionLevel;

#define GDM_GREETER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SESSION, GdmGreeterSessionPrivate))

struct GdmGreeterSessionPrivate
{
        GdmGreeterClient      *client;
        GtkWidget             *login_window;
        GtkWidget             *panel;
        GtkWidget             *background;

        GdmGreeterSessionLevel level;
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
        g_debug ("GREETER INFO: %s", text);

        gdm_greeter_login_window_info (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_problem (GdmGreeterClient  *client,
            const char        *text,
            GdmGreeterSession *session)
{
        g_debug ("GREETER PROBLEM: %s", text);

        gdm_greeter_login_window_problem (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_ready (GdmGreeterClient  *client,
          GdmGreeterSession *session)
{
        g_debug ("GREETER SERVER READY");

        gdm_greeter_login_window_ready (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));
}

static void
on_reset (GdmGreeterClient  *client,
          GdmGreeterSession *session)
{
        g_debug ("GREETER RESET");

        gdm_greeter_login_window_reset (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));
}

static void
on_info_query (GdmGreeterClient  *client,
               const char        *text,
               GdmGreeterSession *session)
{
        g_debug ("GREETER Info query: %s", text);

        gdm_greeter_login_window_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_secret_info_query (GdmGreeterClient  *client,
                      const char        *text,
                      GdmGreeterSession *session)
{
        g_debug ("GREETER Secret info query: %s", text);

        gdm_greeter_login_window_secret_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_begin_verification (GdmGreeterLoginWindow *login_window,
                       const char            *username,
                       GdmGreeterSession     *session)
{
        gdm_greeter_client_call_begin_verification (session->priv->client,
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
on_select_session (GdmGreeterLoginWindow *login_window,
                   const char            *text,
                   GdmGreeterSession     *session)
{
        gdm_greeter_client_call_select_session (session->priv->client,
                                                text);
}

static void
on_select_language (GdmGreeterLoginWindow *login_window,
                    const char            *text,
                    GdmGreeterSession     *session)
{
        gdm_greeter_client_call_select_language (session->priv->client,
                                                 text);
}

static void
on_select_user (GdmGreeterLoginWindow *login_window,
                const char            *text,
                GdmGreeterSession     *session)
{
        gdm_greeter_client_call_select_user (session->priv->client,
                                             text);
}

static void
on_select_hostname (GdmGreeterLoginWindow *login_window,
                    const char            *text,
                    GdmGreeterSession     *session)
{
        gdm_greeter_client_call_select_hostname (session->priv->client,
                                                 text);
}

static void
on_cancelled (GdmGreeterLoginWindow *login_window,
              GdmGreeterSession     *session)
{
        gdm_greeter_client_call_cancel (session->priv->client);
}

static void
on_disconnected (GdmGreeterLoginWindow *login_window,
                 GdmGreeterSession     *session)
{
        gdm_greeter_client_call_disconnect (session->priv->client);
}

static void
start_login_window (GdmGreeterSession *session)
{
        gboolean is_local;

        is_local = gdm_greeter_client_get_display_is_local (session->priv->client);

        session->priv->login_window = gdm_greeter_login_window_new (is_local);

        g_signal_connect (session->priv->login_window,
                          "begin-verification",
                          G_CALLBACK (on_begin_verification),
                          session);
        g_signal_connect (session->priv->login_window,
                          "query-answer",
                          G_CALLBACK (on_query_answer),
                          session);
        g_signal_connect (session->priv->login_window,
                          "session-selected",
                          G_CALLBACK (on_select_session),
                          session);
        g_signal_connect (session->priv->login_window,
                          "language-selected",
                          G_CALLBACK (on_select_language),
                          session);
        g_signal_connect (session->priv->login_window,
                          "user-selected",
                          G_CALLBACK (on_select_user),
                          session);
        g_signal_connect (session->priv->login_window,
                          "hostname-selected",
                          G_CALLBACK (on_select_hostname),
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
}

static void
stop_login_window (GdmGreeterSession *session)
{
}

static gboolean
launch_compiz (GdmGreeterSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("Launching compiz");

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

        g_debug ("Launching metacity");

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
start_panel (GdmGreeterSession *session)
{
        session->priv->panel = gdm_greeter_panel_new ();
        gtk_widget_show (session->priv->panel);
}

static void
start_background (GdmGreeterSession *session)
{
        session->priv->background = gdm_greeter_background_new ();
        gtk_widget_show (session->priv->background);
}

static void
start_window_manager (GdmGreeterSession *session)
{
        if (! launch_compiz (session)) {
                launch_metacity (session);
        }
}

static gboolean
start_settings_daemon (GdmGreeterSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("Launching settings daemon");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async (LIBEXECDIR "/gnome-settings-daemon", &error);
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
gdm_greeter_session_set_level (GdmGreeterSession     *session,
                               GdmGreeterSessionLevel level)
{
        switch (level) {
        case GDM_GREETER_SESSION_LEVEL_NONE:
                break;
        case GDM_GREETER_SESSION_LEVEL_STARTUP:
                start_settings_daemon (session);
                start_window_manager (session);
                start_background (session);
                break;
        case GDM_GREETER_SESSION_LEVEL_CONFIGURATION:
                start_panel (session);
                break;
        case GDM_GREETER_SESSION_LEVEL_LOGIN_WINDOW:
                start_login_window (session);
                break;
        case GDM_GREETER_SESSION_LEVEL_SHUTDOWN:
                stop_login_window (session);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        session->priv->level = level;
}

static void
gdm_greeter_session_goto_level (GdmGreeterSession     *session,
                                GdmGreeterSessionLevel level)
{
        while (level != session->priv->level) {
                if (level < session->priv->level) {
                        gdm_greeter_session_set_level (session, session->priv->level - 1);
                } else {
                        gdm_greeter_session_set_level (session, session->priv->level + 1);
                }
        }
}

gboolean
gdm_greeter_session_start (GdmGreeterSession *session,
                           GError           **error)
{
        gboolean res;

        g_return_val_if_fail (GDM_IS_GREETER_SESSION (session), FALSE);

        res = gdm_greeter_client_start (session->priv->client, error);

        gdm_greeter_session_goto_level (session, GDM_GREETER_SESSION_LEVEL_LOGIN_WINDOW);

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

        g_debug ("Disposing greeter_session");

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

        session->priv->level = GDM_GREETER_SESSION_LEVEL_NONE;

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
