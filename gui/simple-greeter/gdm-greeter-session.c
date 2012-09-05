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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

#include "gdm-client.h"

#include "gdm-greeter-session.h"
#include "gdm-greeter-panel.h"
#include "gdm-greeter-login-window.h"
#include "gdm-user-chooser-widget.h"

#include "gdm-profile.h"

#define GDM_GREETER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SESSION, GdmGreeterSessionPrivate))

#define MAX_LOGIN_TRIES 3

struct GdmGreeterSessionPrivate
{
        GdmClient             *client;
        GdmUserVerifier       *user_verifier;
        GdmRemoteGreeter      *remote_greeter;
        GdmGreeter            *greeter;


        GtkWidget             *login_window;
        GtkWidget             *panel;

        guint                  num_tries;
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
on_info (GdmClient         *client,
         const char        *service_name,
         const char        *text,
         GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Info: %s", text);

        gdm_greeter_login_window_info (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name, text);
}

static void
on_problem (GdmClient         *client,
            const char        *service_name,
            const char        *text,
            GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Problem: %s", text);

        gdm_greeter_login_window_problem (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name, text);
}

static void
on_service_unavailable (GdmClient         *client,
                        const char        *service_name,
                        GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Service Unavailable: %s", service_name);

        gdm_greeter_login_window_service_unavailable (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name);
}

static void
on_conversation_started (GdmClient         *client,
                         const char        *service_name,
                         GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Ready");

        gdm_greeter_login_window_ready (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window),
                                        service_name);
}

static void
on_conversation_stopped (GdmClient         *client,
                         const char        *service_name,
                         GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Conversation '%s' stopped", service_name);

        gdm_greeter_login_window_conversation_stopped (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window),
                                                       service_name);
}

static void
on_reset (GdmClient         *client,
          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Reset");

        session->priv->num_tries = 0;

        gdm_greeter_login_window_reset (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window));
}

static void
show_or_hide_user_options (GdmGreeterSession *session,
                           const char        *username)
{
    if (username != NULL && strcmp (username, GDM_USER_CHOOSER_USER_OTHER) != 0) {
            //gdm_greeter_panel_show_user_options (GDM_GREETER_PANEL (session->priv->panel));
    } else {
            //gdm_greeter_panel_hide_user_options (GDM_GREETER_PANEL (session->priv->panel));
    }
}

static void
on_selected_user_changed (GdmClient         *client,
                          const char        *text,
                          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: selected user changed: %s", text);
        show_or_hide_user_options (session, text);
}

static void
on_default_language_name_changed (GdmClient         *client,
                                  const char        *text,
                                  GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: default language name changed: %s", text);
}

static void
on_default_session_name_changed (GdmClient         *client,
                                 const char        *text,
                                 GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: default session name changed: %s", text);
        gdm_greeter_login_window_set_default_session_name (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text);
}

static void
on_timed_login_requested (GdmClient         *client,
                          const char        *text,
                          int                delay,
                          GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: timed login requested for user %s (in %d seconds)", text, delay);
        gdm_greeter_login_window_request_timed_login (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), text, delay);
}

static void
on_session_opened (GdmClient         *client,
                   const char        *service_name,
                   GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: session opened");
        gdm_greeter_login_window_session_opened (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name);
}

static void
on_info_query (GdmClient         *client,
               const char        *service_name,
               const char        *text,
               GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Info query: %s", text);

        gdm_greeter_login_window_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name, text);
}

static void
on_secret_info_query (GdmClient         *client,
                      const char        *service_name,
                      const char        *text,
                      GdmGreeterSession *session)
{
        g_debug ("GdmGreeterSession: Secret info query: %s", text);

        gdm_greeter_login_window_secret_info_query (GDM_GREETER_LOGIN_WINDOW (session->priv->login_window), service_name, text);
}

static void
on_begin_auto_login (GdmGreeterLoginWindow *login_window,
                     const char            *username,
                     GdmGreeterSession     *session)
{
        gdm_greeter_call_begin_auto_login_sync (session->priv->greeter,
                                                username,
                                                NULL,
                                                NULL);
}

static void
get_user_verifier (GdmGreeterSession     *session,
                   const char            *username)
{
        GError *error = NULL;

        g_clear_object (&session->priv->user_verifier);

        if (username != NULL) {
                session->priv->user_verifier = gdm_client_open_reauthentication_channel_sync (session->priv->client,
                                                                                              username,
                                                                                              NULL,
                                                                                              &error);

                if (error != NULL) {
                        g_debug ("GdmGreeterSession: could not get reauthentication channel for user %s: %s", username, error->message);
                        g_clear_error (&error);
                }
        }

        if (session->priv->user_verifier == NULL) {
                session->priv->user_verifier = gdm_client_get_user_verifier_sync (session->priv->client,
                                                                                  NULL,
                                                                                  &error);

                if (error != NULL) {
                        g_debug ("GdmGreeterSession: could not get user verifier %s", error->message);
                        g_clear_error (&error);
                }

                if (session->priv->user_verifier == NULL) {

                        return;
                }
        }
        g_signal_connect (session->priv->user_verifier,
                          "info-query",
                          G_CALLBACK (on_info_query),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "secret-info-query",
                          G_CALLBACK (on_secret_info_query),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "info",
                          G_CALLBACK (on_info),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "problem",
                          G_CALLBACK (on_problem),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "service-unavailable",
                          G_CALLBACK (on_service_unavailable),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "conversation-started",
                          G_CALLBACK (on_conversation_started),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "conversation-stopped",
                          G_CALLBACK (on_conversation_stopped),
                          session);
        g_signal_connect (session->priv->user_verifier,
                          "reset",
                          G_CALLBACK (on_reset),
                          session);
        g_signal_connect (session->priv->greeter,
                          "selected-user-changed",
                          G_CALLBACK (on_selected_user_changed),
                          session);
        g_signal_connect (session->priv->greeter,
                          "default-language-name-changed",
                          G_CALLBACK (on_default_language_name_changed),
                          session);
        g_signal_connect (session->priv->greeter,
                          "default-session-name-changed",
                          G_CALLBACK (on_default_session_name_changed),
                          session);
        g_signal_connect (session->priv->greeter,
                          "timed-login-requested",
                          G_CALLBACK (on_timed_login_requested),
                          session);
        g_signal_connect (session->priv->greeter,
                          "session-opened",
                          G_CALLBACK (on_session_opened),
                          session);
}

static void
on_begin_verification (GdmGreeterLoginWindow *login_window,
                       const char            *service_name,
                       GdmGreeterSession     *session)
{
        get_user_verifier (session, NULL);
        gdm_user_verifier_call_begin_verification_sync (session->priv->user_verifier,
                                                        service_name,
                                                        NULL,
                                                        NULL);
}

static void
on_begin_verification_for_user (GdmGreeterLoginWindow *login_window,
                                const char            *service_name,
                                const char            *username,
                                GdmGreeterSession     *session)
{
        get_user_verifier (session, NULL);
        gdm_user_verifier_call_begin_verification_for_user_sync (session->priv->user_verifier,
                                                                 service_name,
                                                                 username,
                                                                 NULL,
                                                                 NULL);
}

static void
on_query_answer (GdmGreeterLoginWindow *login_window,
                 const char            *service_name,
                 const char            *text,
                 GdmGreeterSession     *session)
{
        gdm_user_verifier_call_answer_query_sync (session->priv->user_verifier,
                                                  service_name,
                                                  text,
                                                  NULL,
                                                  NULL);
}

static void
on_select_session (GdmGreeterLoginWindow *login_window,
                   const char            *text,
                   GdmGreeterSession     *session)
{
        gdm_greeter_call_select_session_sync (session->priv->greeter,
                                              text,
                                              NULL,
                                              NULL);
}

static void
on_select_user (GdmGreeterLoginWindow *login_window,
                const char            *text,
                GdmGreeterSession     *session)
{
        show_or_hide_user_options (session, text);
        gdm_greeter_call_select_user_sync (session->priv->greeter,
                                           text,
                                           NULL,
                                           NULL);
}

static void
on_cancelled (GdmGreeterLoginWindow *login_window,
              GdmGreeterSession     *session)
{
        gdm_user_verifier_call_cancel_sync (session->priv->user_verifier, NULL, NULL);
}

static void
on_disconnected (GdmGreeterSession     *session)
{
        if (session->priv->remote_greeter != NULL) {
                gdm_remote_greeter_call_disconnect_sync (session->priv->remote_greeter, NULL, NULL);
        }
}

static void
on_start_session (GdmGreeterLoginWindow *login_window,
                  const char            *service_name,
                  GdmGreeterSession     *session)
{
        gdm_greeter_call_start_session_when_ready_sync (session->priv->greeter, service_name, TRUE, NULL, NULL);
}

static int
get_tallest_monitor_at_point (GdkScreen *screen,
                              int        x,
                              int        y)
{
        cairo_rectangle_int_t area;
        cairo_region_t *region;
        int i;
        int monitor;
        int n_monitors;
        int tallest_height;

        tallest_height = 0;
        n_monitors = gdk_screen_get_n_monitors (screen);
        monitor = -1;
        for (i = 0; i < n_monitors; i++) {
                gdk_screen_get_monitor_geometry (screen, i, &area);
                region = cairo_region_create_rectangle (&area);

                if (cairo_region_contains_point (region, x, y)) {
                        if (area.height > tallest_height) {
                                monitor = i;
                                tallest_height = area.height;
                        }
                }
                cairo_region_destroy (region);
        }

        if (monitor == -1) {
                monitor = gdk_screen_get_monitor_at_point (screen, x, y);
        }

        return monitor;
}

static void
toggle_panel (GdmGreeterSession *session,
              gboolean           enabled)
{
        gdm_profile_start (NULL);

        if (enabled) {
                GdkDisplay *display;
                GdkScreen  *screen;
                int         monitor;
                int         x, y;
                gboolean    is_local;

                display = gdk_display_get_default ();
                gdk_display_get_pointer (display, &screen, &x, &y, NULL);

                monitor = get_tallest_monitor_at_point (screen, x, y);

                is_local = session->priv->remote_greeter != NULL;
                session->priv->panel = gdm_greeter_panel_new (screen, monitor, is_local);

                g_signal_connect_swapped (session->priv->panel,
                                          "disconnected",
                                          G_CALLBACK (on_disconnected),
                                          session);

                gtk_widget_show (session->priv->panel);
        } else {
                gtk_widget_destroy (session->priv->panel);
                session->priv->panel = NULL;
        }

        gdm_profile_end (NULL);
}

static void
toggle_login_window (GdmGreeterSession *session,
                     gboolean           enabled)
{
        gdm_profile_start (NULL);

        if (enabled) {
                gboolean is_local;

                is_local = session->priv->remote_greeter != NULL;
                g_debug ("GdmGreeterSession: Starting a login window local:%d", is_local);
                session->priv->login_window = gdm_greeter_login_window_new (is_local);
                g_signal_connect (session->priv->login_window,
                                  "begin-auto-login",
                                  G_CALLBACK (on_begin_auto_login),
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
                                  "session-selected",
                                  G_CALLBACK (on_select_session),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "cancelled",
                                  G_CALLBACK (on_cancelled),
                                  session);
                g_signal_connect (session->priv->login_window,
                                  "start-session",
                                  G_CALLBACK (on_start_session),
                                  session);
                gtk_widget_show (session->priv->login_window);
        } else {
                gtk_widget_destroy (session->priv->login_window);
                session->priv->login_window = NULL;
        }
        gdm_profile_end (NULL);
}

gboolean
gdm_greeter_session_start (GdmGreeterSession *session,
                           GError           **error)
{
        g_return_val_if_fail (GDM_IS_GREETER_SESSION (session), FALSE);

        gdm_profile_start (NULL);


        session->priv->greeter = gdm_client_get_greeter_sync (session->priv->client,
                                                              NULL,
                                                              error);

        if (session->priv->greeter == NULL) {
                return FALSE;
        }

        session->priv->remote_greeter = gdm_client_get_remote_greeter_sync (session->priv->client,
                                                                            NULL,
                                                                            error);


        toggle_panel (session, TRUE);
        toggle_login_window (session, TRUE);

        gdm_profile_end (NULL);

        return TRUE;
}

void
gdm_greeter_session_stop (GdmGreeterSession *session)
{
        g_return_if_fail (GDM_IS_GREETER_SESSION (session));

        toggle_panel (session, FALSE);
        toggle_login_window (session, FALSE);
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
gdm_greeter_session_event_handler (GdkEvent          *event,
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
        gdm_profile_start (NULL);

        session->priv = GDM_GREETER_SESSION_GET_PRIVATE (session);

        session->priv->client = gdm_client_new ();
        /* We want to listen for panel mnemonics even if the
         * login window is focused, so we intercept them here.
         */
        gdk_event_handler_set ((GdkEventFunc) gdm_greeter_session_event_handler,
                               session, NULL);

        gdm_profile_end (NULL);
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
