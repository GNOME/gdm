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
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
#include <sched.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-common.h"
#include "gdm-greeter-server.h"
#include "gdm-greeter-glue.h"
#include "gdm-dbus-util.h"

#define GDM_GREETER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define GDM_GREETER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

#define GDM_GREETER_SERVER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SERVER, GdmGreeterServerPrivate))

struct GdmGreeterServerPrivate
{
        char           *user_name;
        char           *group_name;
        char           *display_id;

        GDBusServer     *server;
        GDBusConnection *greeter_connection;
        GdmDBusGreeterServer *skeleton;

        guint           using_legacy_service_name : 1;
};

enum {
        PROP_0,
        PROP_USER_NAME,
        PROP_GROUP_NAME,
        PROP_DISPLAY_ID,
};

enum {
        START_CONVERSATION,
        BEGIN_AUTO_LOGIN,
        BEGIN_VERIFICATION,
        BEGIN_VERIFICATION_FOR_USER,
        QUERY_ANSWER,
        SESSION_SELECTED,
        HOSTNAME_SELECTED,
        LANGUAGE_SELECTED,
        USER_SELECTED,
        CANCELLED,
        CONNECTED,
        DISCONNECTED,
        START_SESSION_WHEN_READY,
        START_SESSION_LATER,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_greeter_server_class_init   (GdmGreeterServerClass *klass);
static void     gdm_greeter_server_init         (GdmGreeterServer      *greeter_server);
static void     gdm_greeter_server_finalize     (GObject               *object);

G_DEFINE_TYPE (GdmGreeterServer, gdm_greeter_server, G_TYPE_OBJECT)

static const char *
translate_outgoing_service_name (GdmGreeterServer *greeter_server,
                                 const char       *service_name)
{
#ifndef ENABLE_SPLIT_AUTHENTICATION
        if (strcmp (service_name, "gdm") == 0 && greeter_server->priv->using_legacy_service_name) {
                return "gdm-password";
        }
#endif

        return service_name;
}

static const char *
translate_incoming_service_name (GdmGreeterServer *greeter_server,
                                 const char       *service_name)
{
#ifndef ENABLE_SPLIT_AUTHENTICATION
        if (strcmp (service_name, "gdm-password") == 0) {
                g_debug ("GdmGreeterServer: Adjusting pam service from '%s' to 'gdm' for legacy compatibility", service_name);
                service_name = "gdm";
                greeter_server->priv->using_legacy_service_name = TRUE;
        } else if (g_str_has_prefix (service_name, "gdm-") && strcmp (service_name, "gdm-autologin") != 0) {
                g_debug ("GdmGreeterServer: Rejecting pam service '%s' for legacy compatibility", service_name);
                return NULL;
        }
#endif
        return service_name;
}

gboolean
gdm_greeter_server_info_query (GdmGreeterServer *greeter_server,
                               const char       *service_name,
                               const char       *text)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_info_query (greeter_server->priv->skeleton,
                                                 service_name, text);
        return TRUE;
}

gboolean
gdm_greeter_server_secret_info_query (GdmGreeterServer *greeter_server,
                                      const char       *service_name,
                                      const char       *text)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_secret_info_query (greeter_server->priv->skeleton,
                                                        service_name, text);
        return TRUE;
}

gboolean
gdm_greeter_server_info (GdmGreeterServer *greeter_server,
                         const char       *service_name,
                         const char       *text)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_info (greeter_server->priv->skeleton,
                                           service_name, text);
        return TRUE;
}

gboolean
gdm_greeter_server_problem (GdmGreeterServer *greeter_server,
                            const char       *service_name,
                            const char       *text)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_problem (greeter_server->priv->skeleton,
                                              service_name, text);
        return TRUE;
}

gboolean
gdm_greeter_server_authentication_failed (GdmGreeterServer *greeter_server,
                                          const char       *service_name)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_authentication_failed (greeter_server->priv->skeleton,
                                                            service_name);
        return TRUE;
}

gboolean
gdm_greeter_server_service_unavailable (GdmGreeterServer *greeter_server,
                                        const char       *service_name)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_service_unavailable (greeter_server->priv->skeleton,
                                                          service_name);
        return TRUE;
}

gboolean
gdm_greeter_server_reset (GdmGreeterServer *greeter_server)
{
        gdm_dbus_greeter_server_emit_reset (greeter_server->priv->skeleton);
        return TRUE;
}

gboolean
gdm_greeter_server_ready (GdmGreeterServer *greeter_server,
                          const char       *service_name)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_ready (greeter_server->priv->skeleton,
                                            service_name);
        return TRUE;
}

gboolean
gdm_greeter_server_conversation_stopped (GdmGreeterServer *greeter_server,
                                         const char       *service_name)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_conversation_stopped (greeter_server->priv->skeleton,
                                                           service_name);
        return TRUE;
}

void
gdm_greeter_server_selected_user_changed (GdmGreeterServer *greeter_server,
                                          const char       *username)
{
        gdm_dbus_greeter_server_emit_selected_user_changed (greeter_server->priv->skeleton,
                                                            username);
}

void
gdm_greeter_server_default_language_name_changed (GdmGreeterServer *greeter_server,
                                                  const char       *language_name)
{
        gdm_dbus_greeter_server_emit_default_language_name_changed (greeter_server->priv->skeleton,
                                                                    language_name);
}

void
gdm_greeter_server_default_session_name_changed (GdmGreeterServer *greeter_server,
                                                 const char       *session_name)
{
        gdm_dbus_greeter_server_emit_default_session_name_changed (greeter_server->priv->skeleton,
                                                                   session_name);
}

void
gdm_greeter_server_request_timed_login (GdmGreeterServer *greeter_server,
                                        const char       *username,
                                        int               delay)
{
        gdm_dbus_greeter_server_emit_timed_login_requested (greeter_server->priv->skeleton,
                                                            username, delay);
}

void
gdm_greeter_server_session_opened (GdmGreeterServer *greeter_server,
                                   const char       *service_name)
{
        service_name = translate_outgoing_service_name (greeter_server, service_name);
        gdm_dbus_greeter_server_emit_session_opened (greeter_server->priv->skeleton,
                                                     service_name);
}


static gboolean
handle_start_conversation (GdmDBusGreeterServer  *skeleton,
                           GDBusMethodInvocation *invocation,
                           const char            *service_name,
                           GdmGreeterServer      *greeter_server)
{
        const char  *translated_service_name;

        g_debug ("GreeterServer: StartConversation");

        g_dbus_method_invocation_return_value (invocation, NULL);

        translated_service_name = translate_incoming_service_name (greeter_server, service_name);

        if (translated_service_name == NULL) {
                gdm_greeter_server_service_unavailable (greeter_server, service_name);
                return TRUE;
        }

        g_signal_emit (greeter_server, signals [START_CONVERSATION], 0, translated_service_name);

        return TRUE;
}

static gboolean
handle_begin_verification (GdmDBusGreeterServer  *skeleton,
                           GDBusMethodInvocation *invocation,
                           const char            *service_name,
                           GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: BeginVerification");

        g_dbus_method_invocation_return_value (invocation, NULL);

        service_name = translate_incoming_service_name (greeter_server, service_name);
        g_signal_emit (greeter_server, signals [BEGIN_VERIFICATION], 0, service_name);

        return TRUE;
}

static gboolean
handle_begin_auto_login (GdmDBusGreeterServer  *skeleton,
                         GDBusMethodInvocation *invocation,
                         const char            *text,
                         GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: BeginAutoLogin for '%s'", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [BEGIN_AUTO_LOGIN], 0, text);

        return TRUE;
}

static gboolean
handle_begin_verification_for_user (GdmDBusGreeterServer  *skeleton,
                                    GDBusMethodInvocation *invocation,
                                    const char            *service_name,
                                    const char            *text,
                                    GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: BeginVerificationForUser for '%s'", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        service_name = translate_incoming_service_name (greeter_server, service_name);
        g_signal_emit (greeter_server, signals [BEGIN_VERIFICATION_FOR_USER], 0, service_name, text);

        return TRUE;
}

static gboolean
handle_answer_query (GdmDBusGreeterServer  *skeleton,
                     GDBusMethodInvocation *invocation,
                     const char            *service_name,
                     const char            *text,
                     GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: AnswerQuery");

        g_dbus_method_invocation_return_value (invocation, NULL);

        service_name = translate_incoming_service_name (greeter_server, service_name);
        g_signal_emit (greeter_server, signals [QUERY_ANSWER], 0, service_name, text);

        return TRUE;
}

static gboolean
handle_select_session (GdmDBusGreeterServer  *skeleton,
                       GDBusMethodInvocation *invocation,
                       const char            *text,
                       GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: SelectSession: %s", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [SESSION_SELECTED], 0, text);

        return TRUE;
}

static gboolean
handle_select_hostname (GdmDBusGreeterServer  *skeleton,
                        GDBusMethodInvocation *invocation,
                        const char            *text,
                        GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: SelectHostname: %s", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [HOSTNAME_SELECTED], 0, text);

        return TRUE;
}

static gboolean
handle_select_language (GdmDBusGreeterServer  *skeleton,
                        GDBusMethodInvocation *invocation,
                        const char            *text,
                        GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: SelectLanguage: %s", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [LANGUAGE_SELECTED], 0, text);

        return TRUE;
}

static gboolean
handle_select_user (GdmDBusGreeterServer  *skeleton,
                    GDBusMethodInvocation *invocation,
                    const char            *text,
                    GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: SelectUser: %s", text);

        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [USER_SELECTED], 0, text);

        return TRUE;
}

static gboolean
handle_cancel (GdmDBusGreeterServer  *skeleton,
               GDBusMethodInvocation *invocation,
               GdmGreeterServer      *greeter_server)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [CANCELLED], 0);

        return TRUE;
}

static gboolean
handle_disconnect (GdmDBusGreeterServer  *skeleton,
                   GDBusMethodInvocation *invocation,
                   GdmGreeterServer      *greeter_server)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_signal_emit (greeter_server, signals [DISCONNECTED], 0);

        return TRUE;
}

static gboolean
handle_get_display_id (GdmDBusGreeterServer  *skeleton,
                       GDBusMethodInvocation *invocation,
                       GdmGreeterServer      *greeter_server)
{
        gdm_dbus_greeter_server_complete_get_display_id (skeleton,
                                                         invocation,
                                                         greeter_server->priv->display_id);

        return TRUE;
}

static gboolean
handle_start_session_when_ready (GdmDBusGreeterServer  *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 const char            *service_name,
                                 gboolean               should_start_session,
                                 GdmGreeterServer      *greeter_server)
{
        g_debug ("GreeterServer: %sStartSessionWhenReady",
                 should_start_session? "" : "Don't ");

        g_dbus_method_invocation_return_value (invocation, NULL);

        service_name = (char *) translate_incoming_service_name (greeter_server, service_name);
        if (should_start_session) {
                g_signal_emit (greeter_server, signals [START_SESSION_WHEN_READY], 0, service_name);
        } else {
                g_signal_emit (greeter_server, signals [START_SESSION_LATER] ,0, service_name);
        }

        return TRUE;
}

static void
connection_closed (GDBusConnection *connection,
                   gboolean         remote_peer_vanished,
                   GError          *error,
                   gpointer         user_data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (user_data);

        g_debug ("GreeterServer: connection_closed (remote peer vanished? %s)",
                 remote_peer_vanished ? "yes" : "no");

        g_clear_object(&greeter_server->priv->greeter_connection);
}

static gboolean
allow_user_function (GDBusConnection *connection,
                     GIOStream       *stream,
                     GCredentials    *peer_credentials,
                     gpointer         data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (data);
        struct passwd    *pwent;
        uid_t             uid;

        if (greeter_server->priv->user_name == NULL) {
                return FALSE;
        }

        gdm_get_pwent_for_name (greeter_server->priv->user_name, &pwent);

        if (pwent == NULL) {
                return FALSE;
        }

        uid = pwent->pw_uid;

        if (uid == g_credentials_get_unix_user (peer_credentials, NULL)) {
                return TRUE;
        }

        return FALSE;
}

static gboolean
handle_connection (GDBusServer      *server,
                   GDBusConnection  *new_connection,
                   gpointer          user_data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (user_data);

        g_debug ("GreeterServer: Handing new connection");

        if (greeter_server->priv->greeter_connection == NULL) {
                greeter_server->priv->greeter_connection = g_object_ref (new_connection);

                g_signal_connect (new_connection, "closed",
                                  G_CALLBACK (connection_closed),
                                  greeter_server);

                greeter_server->priv->skeleton = GDM_DBUS_GREETER_SERVER (gdm_dbus_greeter_server_skeleton_new ());

                g_signal_connect (greeter_server->priv->skeleton, "handle-start-conversation",
                                  G_CALLBACK (handle_start_conversation), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-begin-verification",
                                  G_CALLBACK (handle_begin_verification), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-begin-verification-for-user",
                                  G_CALLBACK (handle_begin_verification_for_user), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-begin-auto-login",
                                  G_CALLBACK (handle_begin_auto_login), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-answer-query",
                                  G_CALLBACK (handle_answer_query), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-select-session",
                                  G_CALLBACK (handle_select_session), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-select-hostname",
                                  G_CALLBACK (handle_select_hostname), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-select-language",
                                  G_CALLBACK (handle_select_language), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-select-user",
                                  G_CALLBACK (handle_select_user), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-cancel",
                                  G_CALLBACK (handle_cancel), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-disconnect",
                                  G_CALLBACK (handle_disconnect), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-get-display-id",
                                  G_CALLBACK (handle_get_display_id), greeter_server);
                g_signal_connect (greeter_server->priv->skeleton, "handle-start-session-when-ready",
                                  G_CALLBACK (handle_start_session_when_ready), greeter_server);

                g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (greeter_server->priv->skeleton),
                                                  new_connection,
                                                  GDM_GREETER_SERVER_DBUS_PATH,
                                                  NULL);

                g_signal_emit (greeter_server, signals[CONNECTED], 0);

                return TRUE;
        }

        return FALSE;
}

gboolean
gdm_greeter_server_start (GdmGreeterServer *greeter_server)
{
        GError *error = NULL;
        gboolean ret;
        GDBusAuthObserver *observer;

        ret = FALSE;

        g_debug ("GreeterServer: Creating D-Bus server for greeter");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect_object (observer,
                                 "authorize-authenticated-peer",
                                 G_CALLBACK (allow_user_function),
                                 greeter_server,
                                 0);

        greeter_server->priv->server = gdm_dbus_setup_private_server (observer,
                                                                      &error);

        g_object_unref (observer);

        if (greeter_server->priv->server == NULL) {
                g_warning ("Cannot create D-BUS server for the greeter: %s", error->message);
                g_error_free (error);

                /* FIXME: should probably fail if we can't create the socket */
                goto out;
        }

        g_signal_connect_object (greeter_server->priv->server,
                                 "new-connection",
                                 G_CALLBACK (handle_connection),
                                 greeter_server,
                                 0);

        ret = TRUE;

        g_dbus_server_start (greeter_server->priv->server);

        g_debug ("GreeterServer: D-Bus server listening");

 out:
        return ret;
}

gboolean
gdm_greeter_server_stop (GdmGreeterServer *greeter_server)
{
        g_debug ("GreeterServer: Stopping greeter server...");

        g_dbus_server_stop (greeter_server->priv->server);
        g_clear_object (&greeter_server->priv->server);

        g_clear_object (&greeter_server->priv->greeter_connection);
        g_clear_object (&greeter_server->priv->skeleton);

        return TRUE;
}

char *
gdm_greeter_server_get_address (GdmGreeterServer *greeter_server)
{
        return g_strdup (g_dbus_server_get_client_address (greeter_server->priv->server));
}

static void
_gdm_greeter_server_set_display_id (GdmGreeterServer *greeter_server,
                                    const char       *display_id)
{
        g_free (greeter_server->priv->display_id);
        greeter_server->priv->display_id = g_strdup (display_id);
}

static void
_gdm_greeter_server_set_user_name (GdmGreeterServer *greeter_server,
                                  const char *name)
{
        g_free (greeter_server->priv->user_name);
        greeter_server->priv->user_name = g_strdup (name);
}

static void
_gdm_greeter_server_set_group_name (GdmGreeterServer *greeter_server,
                                    const char *name)
{
        g_free (greeter_server->priv->group_name);
        greeter_server->priv->group_name = g_strdup (name);
}

static void
gdm_greeter_server_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GdmGreeterServer *self;

        self = GDM_GREETER_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                _gdm_greeter_server_set_display_id (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_greeter_server_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_GROUP_NAME:
                _gdm_greeter_server_set_group_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_server_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GdmGreeterServer *self;

        self = GDM_GREETER_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                g_value_set_string (value, self->priv->display_id);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, self->priv->user_name);
                break;
        case PROP_GROUP_NAME:
                g_value_set_string (value, self->priv->group_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_server_class_init (GdmGreeterServerClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_greeter_server_get_property;
        object_class->set_property = gdm_greeter_server_set_property;
        object_class->finalize = gdm_greeter_server_finalize;

        g_type_class_add_private (klass, sizeof (GdmGreeterServerPrivate));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_ID,
                                         g_param_spec_string ("display-id",
                                                              "display id",
                                                              "display id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
         g_object_class_install_property (object_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "user name",
                                                              "user name",
                                                              GDM_USERNAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_GROUP_NAME,
                                         g_param_spec_string ("group-name",
                                                              "group name",
                                                              "group name",
                                                              GDM_GROUPNAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals [START_CONVERSATION] =
                g_signal_new ("start-conversation",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, start_conversation),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [BEGIN_VERIFICATION] =
                g_signal_new ("begin-verification",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, begin_verification),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [BEGIN_AUTO_LOGIN] =
                g_signal_new ("begin-auto-login",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, begin_auto_login),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [BEGIN_VERIFICATION_FOR_USER] =
                g_signal_new ("begin-verification-for-user",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, begin_verification_for_user),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [QUERY_ANSWER] =
                g_signal_new ("query-answer",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, query_answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_SELECTED] =
                g_signal_new ("session-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, session_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, hostname_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [LANGUAGE_SELECTED] =
                g_signal_new ("language-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, language_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [USER_SELECTED] =
                g_signal_new ("user-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, user_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [CANCELLED] =
                g_signal_new ("cancelled",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, cancelled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [CONNECTED] =
                g_signal_new ("connected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, connected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [START_SESSION_WHEN_READY] =
                g_signal_new ("start-session-when-ready",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, start_session_when_ready),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals [START_SESSION_LATER] =
                g_signal_new ("start-session-later",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterServerClass, start_session_later),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
}

static void
gdm_greeter_server_init (GdmGreeterServer *greeter_server)
{

        greeter_server->priv = GDM_GREETER_SERVER_GET_PRIVATE (greeter_server);
}

static void
gdm_greeter_server_finalize (GObject *object)
{
        GdmGreeterServer *greeter_server;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_SERVER (object));

        greeter_server = GDM_GREETER_SERVER (object);

        g_return_if_fail (greeter_server->priv != NULL);

        gdm_greeter_server_stop (greeter_server);

        G_OBJECT_CLASS (gdm_greeter_server_parent_class)->finalize (object);
}

GdmGreeterServer *
gdm_greeter_server_new (const char *display_id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_SERVER,
                               "display-id", display_id,
                               NULL);

        return GDM_GREETER_SERVER (object);
}
