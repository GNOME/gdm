/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * TODO:                - close should be nicer and shutdown
 *                        pam etc
 *                      - audit libs (linux and solaris) support
 */

#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-session-direct.h"
#include "gdm-session.h"
#include "gdm-session-private.h"

#include "gdm-session-record.h"
#include "gdm-session-worker-job.h"

#include "ck-connector.h"

#define GDM_SESSION_DBUS_PATH         "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_INTERFACE    "org.gnome.DisplayManager.Session"
#define GDM_SESSION_DBUS_ERROR_CANCEL "org.gnome.DisplayManager.Session.Error.Cancel"

struct _GdmSessionDirectPrivate
{
        /* per open scope */
        char                *selected_session;
        char                *selected_language;
        char                *selected_user;
        char                *user_x11_authority_file;

        DBusMessage         *message_pending_reply;
        DBusConnection      *worker_connection;
        CkConnector         *ckc;

        GdmSessionWorkerJob *job;
        GPid                 session_pid;
        guint32              is_authenticated : 1;
        guint32              is_running : 1;

        /* object lifetime scope */
        char                *service_name;
        char                *display_name;
        char                *display_hostname;
        char                *display_device;
        gboolean             display_is_local;

        DBusServer          *server;
        char                *server_address;
        GHashTable          *environment;
};

enum {
        PROP_0,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_DEVICE,
        PROP_USER_X11_AUTHORITY_FILE,
};

static void     gdm_session_iface_init          (GdmSessionIface      *iface);

G_DEFINE_TYPE_WITH_CODE (GdmSessionDirect,
                         gdm_session_direct,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_SESSION,
                                                gdm_session_iface_init));

static gboolean
send_dbus_message (DBusConnection *connection,
                   DBusMessage    *message)
{
        gboolean is_connected;
        gboolean sent;

        g_return_val_if_fail (message != NULL, FALSE);

        if (connection == NULL) {
                g_warning ("There is no valid connection");
                return FALSE;
        }

        is_connected = dbus_connection_get_is_connected (connection);
        if (! is_connected) {
                g_warning ("Not connected!");
                return FALSE;
        }

        sent = dbus_connection_send (connection, message, NULL);

        return sent;
}

static void
send_dbus_string_signal (GdmSessionDirect *session,
                         const char *name,
                         const char *text)
{
        DBusMessage    *message;
        DBusMessageIter iter;

        g_return_if_fail (session != NULL);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &text);

        if (! send_dbus_message (session->priv->worker_connection, message)) {
                g_debug ("GdmSessionDirect: Could not send %s signal", name);
        }

        dbus_message_unref (message);
}

static void
send_dbus_void_signal (GdmSessionDirect *session,
                       const char       *name)
{
        DBusMessage *message;

        g_return_if_fail (session != NULL);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           name);

        if (! send_dbus_message (session->priv->worker_connection, message)) {
                g_debug ("GdmSessionDirect: Could not send %s signal", name);
        }

        dbus_message_unref (message);
}

static void
on_authentication_failed (GdmSession *session,
                          const char *message)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        gdm_session_record_failed (impl->priv->session_pid,
                                   impl->priv->selected_user,
                                   impl->priv->display_hostname,
                                   impl->priv->display_name,
                                   impl->priv->display_device);
}

static void
on_session_started (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        gdm_session_record_login (impl->priv->session_pid,
                                  impl->priv->selected_user,
                                  impl->priv->display_hostname,
                                  impl->priv->display_name,
                                  impl->priv->display_device);
}

static void
on_session_start_failed (GdmSession *session,
                         const char *message)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        gdm_session_record_login (impl->priv->session_pid,
                                  impl->priv->selected_user,
                                  impl->priv->display_hostname,
                                  impl->priv->display_name,
                                  impl->priv->display_device);
}

static void
on_session_exited (GdmSession *session,
                   int        exit_code)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        gdm_session_record_logout (impl->priv->session_pid,
                                   impl->priv->selected_user,
                                   impl->priv->display_hostname,
                                   impl->priv->display_name,
                                   impl->priv->display_device);
}

static DBusHandlerResult
gdm_session_direct_handle_setup_complete (GdmSessionDirect *session,
                                          DBusConnection   *connection,
                                          DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: Emitting 'setup-complete' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        _gdm_session_setup_complete (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_setup_failed (GdmSessionDirect *session,
                                        DBusConnection   *connection,
                                        DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'setup-failed' signal");

        _gdm_session_setup_failed (GDM_SESSION (session), NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
gdm_session_direct_handle_reset_complete (GdmSessionDirect *session,
                                          DBusConnection   *connection,
                                          DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: Emitting 'reset-complete' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        _gdm_session_reset_complete (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_reset_failed (GdmSessionDirect *session,
                                        DBusConnection   *connection,
                                        DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'reset-failed' signal");

        _gdm_session_reset_failed (GDM_SESSION (session), NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_authenticated (GdmSessionDirect *session,
                                         DBusConnection   *connection,
                                         DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: Emitting 'authenticated' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        _gdm_session_authenticated (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_authentication_failed (GdmSessionDirect *session,
                                                 DBusConnection   *connection,
                                                 DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'authentication-failed' signal");

        _gdm_session_authentication_failed (GDM_SESSION (session), NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_authorized (GdmSessionDirect *session,
                                      DBusConnection   *connection,
                                      DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: Emitting 'authorized' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        _gdm_session_authorized (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_authorization_failed (GdmSessionDirect *session,
                                                DBusConnection   *connection,
                                                DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'authorization-failed' signal");

        _gdm_session_authorization_failed (GDM_SESSION (session), NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_accredited (GdmSessionDirect *session,
                                      DBusConnection   *connection,
                                      DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: Emitting 'accredited' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        _gdm_session_accredited (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_accreditation_failed (GdmSessionDirect *session,
                                                DBusConnection   *connection,
                                                DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'accreditation-failed' signal");

        _gdm_session_accreditation_failed (GDM_SESSION (session), NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_username_changed (GdmSessionDirect *session,
                                            DBusConnection   *connection,
                                            DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: changing username from '%s' to '%s'",
                 session->priv->selected_user != NULL ? session->priv->selected_user : "<unset>",
                 (strlen (text)) ? text : "<unset>");

        g_free (session->priv->selected_user);
        session->priv->selected_user = (strlen (text) > 0) ? g_strdup (text) : NULL;

        _gdm_session_selected_user_changed (GDM_SESSION (session), session->priv->selected_user);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static void
cancel_pending_query (GdmSessionDirect *session)
{
        DBusMessage *reply;

        if (session->priv->message_pending_reply == NULL) {
                return;
        }

        g_debug ("GdmSessionDirect: Cancelling pending query");

        reply = dbus_message_new_error (session->priv->message_pending_reply,
                                        GDM_SESSION_DBUS_ERROR_CANCEL,
                                        "Operation cancelled");
        dbus_connection_send (session->priv->worker_connection, reply, NULL);
        dbus_connection_flush (session->priv->worker_connection);

        dbus_message_unref (reply);
        dbus_message_unref (session->priv->message_pending_reply);
        session->priv->message_pending_reply = NULL;
}

static void
answer_pending_query (GdmSessionDirect *session,
                      const char       *answer)
{
        DBusMessage    *reply;
        DBusMessageIter iter;

        g_assert (session->priv->message_pending_reply != NULL);

        reply = dbus_message_new_method_return (session->priv->message_pending_reply);
        dbus_message_iter_init_append (reply, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &answer);

        dbus_connection_send (session->priv->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        dbus_message_unref (session->priv->message_pending_reply);
        session->priv->message_pending_reply = NULL;
}

static void
set_pending_query (GdmSessionDirect *session,
                   DBusMessage      *message)
{
        g_assert (session->priv->message_pending_reply == NULL);

        session->priv->message_pending_reply = dbus_message_ref (message);
}

static DBusHandlerResult
gdm_session_direct_handle_info_query (GdmSessionDirect *session,
                                      DBusConnection   *connection,
                                      DBusMessage      *message)
{
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        set_pending_query (session, message);

        g_debug ("GdmSessionDirect: Emitting 'info-query' signal");
        _gdm_session_info_query (GDM_SESSION (session), text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_secret_info_query (GdmSessionDirect *session,
                                             DBusConnection   *connection,
                                             DBusMessage      *message)
{
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        set_pending_query (session, message);

        g_debug ("GdmSessionDirect: Emitting 'secret-info-query' signal");
        _gdm_session_secret_info_query (GDM_SESSION (session), text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_info (GdmSessionDirect *session,
                                DBusConnection   *connection,
                                DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'info' signal");
        _gdm_session_info (GDM_SESSION (session), text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_cancel_pending_query (GdmSessionDirect *session,
                                                DBusConnection   *connection,
                                                DBusMessage      *message)
{
        DBusMessage *reply;

        g_debug ("GdmSessionDirect: worker cancelling pending query");
        cancel_pending_query (session);

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_problem (GdmSessionDirect *session,
                                   DBusConnection   *connection,
                                   DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'problem' signal");
        _gdm_session_problem (GDM_SESSION (session), text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_session_started (GdmSessionDirect *session,
                                           DBusConnection   *connection,
                                           DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        int          pid;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_INT32, &pid,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'session-started' signal with pid '%d'",
                 pid);

        session->priv->session_pid = pid;
        session->priv->is_running = TRUE;

        _gdm_session_session_started (GDM_SESSION (session));

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_start_failed (GdmSessionDirect *session,
                                        DBusConnection   *connection,
                                        DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'session-start-failed' signal");
        _gdm_session_session_start_failed (GDM_SESSION (session), text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_session_exited (GdmSessionDirect *session,
                                          DBusConnection   *connection,
                                          DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        int          code;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_INT32, &code,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'session-exited' signal with exit code '%d'",
                 code);

        session->priv->is_running = FALSE;
        _gdm_session_session_exited (GDM_SESSION (session), code);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_direct_handle_session_died (GdmSessionDirect *session,
                                        DBusConnection   *connection,
                                        DBusMessage      *message)
{
        DBusMessage *reply;
        DBusError    error;
        int          code;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_INT32, &code,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSessionDirect: Emitting 'session-died' signal with signal number '%d'",
                 code);

        session->priv->is_running = FALSE;
        _gdm_session_session_died (GDM_SESSION (session), code);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_worker_message (DBusConnection *connection,
                        DBusMessage    *message,
                        void           *user_data)
{
        GdmSessionDirect *session = GDM_SESSION_DIRECT (user_data);

        if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "InfoQuery")) {
                return gdm_session_direct_handle_info_query (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SecretInfoQuery")) {
                return gdm_session_direct_handle_secret_info_query (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Info")) {
                return gdm_session_direct_handle_info (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Problem")) {
                return gdm_session_direct_handle_problem (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "CancelPendingQuery")) {
                return gdm_session_direct_handle_cancel_pending_query (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SetupComplete")) {
                return gdm_session_direct_handle_setup_complete (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SetupFailed")) {
                return gdm_session_direct_handle_setup_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "ResetComplete")) {
                return gdm_session_direct_handle_reset_complete (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "ResetFailed")) {
                return gdm_session_direct_handle_reset_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Authenticated")) {
                return gdm_session_direct_handle_authenticated (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AuthenticationFailed")) {
                return gdm_session_direct_handle_authentication_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Authorized")) {
                return gdm_session_direct_handle_authorized (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AuthorizationFailed")) {
                return gdm_session_direct_handle_authorization_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Accredited")) {
                return gdm_session_direct_handle_accredited (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AccreditationFailed")) {
                return gdm_session_direct_handle_accreditation_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "UsernameChanged")) {
                return gdm_session_direct_handle_username_changed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionStarted")) {
                return gdm_session_direct_handle_session_started (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "StartFailed")) {
                return gdm_session_direct_handle_start_failed (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionExited")) {
                return gdm_session_direct_handle_session_exited (session, connection, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionDied")) {
                return gdm_session_direct_handle_session_died (session, connection, message);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult
do_introspect (DBusConnection *connection,
               DBusMessage    *message)
{
        DBusMessage *reply;
        GString     *xml;
        char        *xml_string;

        g_debug ("GdmSessionDirect: Do introspect");

        /* standard header */
        xml = g_string_new ("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
                            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
                            "<node>\n"
                            "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
                            "    <method name=\"Introspect\">\n"
                            "      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
                            "    </method>\n"
                            "  </interface>\n");

        /* interface */
        xml = g_string_append (xml,
                               "  <interface name=\"org.gnome.DisplayManager.Session\">\n"
                               "    <method name=\"SetupComplete\">\n"
                               "    </method>\n"
                               "    <method name=\"SetupFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"ResetComplete\">\n"
                               "    </method>\n"
                               "    <method name=\"ResetFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Authenticated\">\n"
                               "    </method>\n"
                               "    <method name=\"AuthenticationFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Authorized\">\n"
                               "    </method>\n"
                               "    <method name=\"AuthorizationFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Accredited\">\n"
                               "    </method>\n"
                               "    <method name=\"AccreditationFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"CancelPendingQuery\">\n"
                               "    </method>\n"
                               "    <method name=\"InfoQuery\">\n"
                               "      <arg name=\"query\" direction=\"in\" type=\"s\"/>\n"
                               "      <arg name=\"answer\" direction=\"out\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SecretInfoQuery\">\n"
                               "      <arg name=\"query\" direction=\"in\" type=\"s\"/>\n"
                               "      <arg name=\"answer\" direction=\"out\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Info\">\n"
                               "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Problem\">\n"
                               "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"UsernameChanged\">\n"
                               "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"StartFailed\">\n"
                               "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SessionStarted\">\n"
                               "      <arg name=\"pid\" direction=\"in\" type=\"i\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SessionExited\">\n"
                               "      <arg name=\"code\" direction=\"in\" type=\"i\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SessionDied\">\n"
                               "      <arg name=\"signal\" direction=\"in\" type=\"i\"/>\n"
                               "    </method>\n"
                               "    <signal name=\"Reset\">\n"
                               "    </signal>\n"
                               "    <signal name=\"Setup\">\n"
                               "      <arg name=\"service_name\" type=\"s\"/>\n"
                               "      <arg name=\"x11_display_name\" type=\"s\"/>\n"
                               "      <arg name=\"display_device\" type=\"s\"/>\n"
                               "      <arg name=\"hostname\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetupForUser\">\n"
                               "      <arg name=\"service_name\" type=\"s\"/>\n"
                               "      <arg name=\"x11_display_name\" type=\"s\"/>\n"
                               "      <arg name=\"display_device\" type=\"s\"/>\n"
                               "      <arg name=\"hostname\" type=\"s\"/>\n"
                               "      <arg name=\"username\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"Authenticate\">\n"
                               "    </signal>\n"
                               "    <signal name=\"Authorize\">\n"
                               "    </signal>\n"
                               "    <signal name=\"EstablishCredentials\">\n"
                               "    </signal>\n"
                               "    <signal name=\"RenewCredentials\">\n"
                               "    </signal>\n"
                               "    <signal name=\"SetEnvironmentVariable\">\n"
                               "      <arg name=\"name\" type=\"s\"/>\n"
                               "      <arg name=\"value\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"StartProgram\">\n"
                               "      <arg name=\"command\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "  </interface>\n");

        reply = dbus_message_new_method_return (message);

        xml = g_string_append (xml, "</node>\n");
        xml_string = g_string_free (xml, FALSE);

        dbus_message_append_args (reply,
                                  DBUS_TYPE_STRING, &xml_string,
                                  DBUS_TYPE_INVALID);

        g_free (xml_string);

        if (reply == NULL) {
                g_error ("No memory");
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_message_handler (DBusConnection  *connection,
                         DBusMessage     *message,
                         void            *user_data)
{
        const char *dbus_destination = dbus_message_get_destination (message);
        const char *dbus_path        = dbus_message_get_path (message);
        const char *dbus_interface   = dbus_message_get_interface (message);
        const char *dbus_member      = dbus_message_get_member (message);

        g_debug ("session_message_handler: destination=%s obj_path=%s interface=%s method=%s",
                 dbus_destination ? dbus_destination : "(null)",
                 dbus_path        ? dbus_path        : "(null)",
                 dbus_interface   ? dbus_interface   : "(null)",
                 dbus_member      ? dbus_member      : "(null)");

        if (dbus_message_is_method_call (message, "org.freedesktop.DBus", "AddMatch")) {
                DBusMessage *reply;

                reply = dbus_message_new_method_return (message);

                if (reply == NULL) {
                        g_error ("No memory");
                }

                if (! dbus_connection_send (connection, reply, NULL)) {
                        g_error ("No memory");
                }

                dbus_message_unref (reply);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
                   strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {

                g_debug ("GdmSessionDirect: Disconnected");

                /*dbus_connection_unref (connection);*/

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_method_call (message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
                return do_introspect (connection, message);
        } else {
                return session_worker_message (connection, message, user_data);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* Note: Use abstract sockets like dbus does by default on Linux. Abstract
 * sockets are only available on Linux.
 */
static char *
generate_address (void)
{
        char *path;
#if defined (__linux__)
        int   i;
        char  tmp[9];

        for (i = 0; i < 8; i++) {
                if (g_random_int_range (0, 2) == 0) {
                        tmp[i] = g_random_int_range ('a', 'z' + 1);
                } else {
                        tmp[i] = g_random_int_range ('A', 'Z' + 1);
                }
        }
        tmp[8] = '\0';

        path = g_strdup_printf ("unix:abstract=/tmp/gdm-session-%s", tmp);
#else
        path = g_strdup ("unix:tmpdir=/tmp");
#endif

        return path;
}

static void
session_unregister_handler (DBusConnection  *connection,
                            void            *user_data)
{
        g_debug ("session_unregister_handler");
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
        if (0 == uid) {
                return TRUE;
        }

        g_debug ("GdmSessionDirect: User not allowed");

        return FALSE;
}

static void
handle_connection (DBusServer      *server,
                   DBusConnection  *new_connection,
                   void            *user_data)
{
        GdmSessionDirect *session = GDM_SESSION_DIRECT (user_data);

        g_debug ("GdmSessionDirect: Handing new connection");

        if (session->priv->worker_connection == NULL) {
                DBusObjectPathVTable vtable = { &session_unregister_handler,
                                                &session_message_handler,
                                                NULL, NULL, NULL, NULL
                };

                session->priv->worker_connection = new_connection;
                dbus_connection_ref (new_connection);
                dbus_connection_setup_with_g_main (new_connection, NULL);

                g_debug ("GdmSessionDirect: worker connection is %p", new_connection);
                dbus_connection_set_exit_on_disconnect (new_connection, FALSE);

                dbus_connection_set_unix_user_function (new_connection,
                                                        allow_user_function,
                                                        session,
                                                        NULL);

                dbus_connection_register_object_path (new_connection,
                                                      GDM_SESSION_DBUS_PATH,
                                                      &vtable,
                                                      session);

                g_debug ("GdmSessionDirect: Emitting opened signal");
                _gdm_session_opened (GDM_SESSION (session));
        }
}

static gboolean
setup_server (GdmSessionDirect *session)
{
        DBusError   error;
        gboolean    ret;
        char       *address;
        const char *auth_mechanisms[] = {"EXTERNAL", NULL};

        ret = FALSE;

        g_debug ("GdmSessionDirect: Creating D-Bus server for session");

        address = generate_address ();

        dbus_error_init (&error);
        session->priv->server = dbus_server_listen (address, &error);
        g_free (address);

        if (session->priv->server == NULL) {
                g_warning ("Cannot create D-BUS server for the session: %s", error.message);
                /* FIXME: should probably fail if we can't create the socket */
                goto out;
        }

        dbus_server_setup_with_g_main (session->priv->server, NULL);
        dbus_server_set_auth_mechanisms (session->priv->server, auth_mechanisms);
        dbus_server_set_new_connection_function (session->priv->server,
                                                 handle_connection,
                                                 session,
                                                 NULL);
        ret = TRUE;

        g_free (session->priv->server_address);
        session->priv->server_address = dbus_server_get_address (session->priv->server);

        g_debug ("GdmSessionDirect: D-Bus server listening on %s", session->priv->server_address);

 out:

        return ret;
}

static void
gdm_session_direct_init (GdmSessionDirect *session)
{
        const char * const *languages;

        session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session,
                                                     GDM_TYPE_SESSION_DIRECT,
                                                     GdmSessionDirectPrivate);

        g_signal_connect (session,
                          "authentication-failed",
                          G_CALLBACK (on_authentication_failed),
                          NULL);
        g_signal_connect (session,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          NULL);
        g_signal_connect (session,
                          "session-start-failed",
                          G_CALLBACK (on_session_start_failed),
                          NULL);
        g_signal_connect (session,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          NULL);

        languages = g_get_language_names ();
        if (languages != NULL) {
                session->priv->selected_language = g_strdup (languages[0]);
        }

        session->priv->session_pid = -1;
        session->priv->selected_session = g_strdup ("gnome.desktop");
        session->priv->service_name = g_strdup ("gdm");

        session->priv->environment = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            (GDestroyNotify) g_free,
                                                            (GDestroyNotify) g_free);

        setup_server (session);
}

static void
worker_stopped (GdmSessionWorkerJob *job,
                GdmSessionDirect    *session)
{
        g_debug ("GdmSessionDirect: Worker job stopped");
}

static void
worker_started (GdmSessionWorkerJob *job,
                GdmSessionDirect    *session)
{
        g_debug ("GdmSessionDirect: Worker job started");
}

static void
worker_exited (GdmSessionWorkerJob *job,
               int                  code,
               GdmSessionDirect    *session)
{
        g_debug ("GdmSessionDirect: Worker job exited: %d", code);

        if (!session->priv->is_authenticated) {
                char *msg;

                msg = g_strdup_printf (_("worker exited with status %d"), code);
                _gdm_session_authentication_failed (GDM_SESSION (session), msg);
                g_free (msg);
        } else if (session->priv->is_running) {
                _gdm_session_session_exited (GDM_SESSION (session), code);
        }
}

static void
worker_died (GdmSessionWorkerJob *job,
             int                  signum,
             GdmSessionDirect    *session)
{
        g_debug ("GdmSessionDirect: Worker job died: %d", signum);

        if (!session->priv->is_authenticated) {
                char *msg;

                msg = g_strdup_printf (_("worker exited with status %d"), signum);
                _gdm_session_authentication_failed (GDM_SESSION (session), msg);
                g_free (msg);
        } else if (session->priv->is_running) {
                _gdm_session_session_died (GDM_SESSION (session), signum);
        }
}

static gboolean
start_worker (GdmSessionDirect *session)
{
        gboolean res;

        session->priv->job = gdm_session_worker_job_new ();
        gdm_session_worker_job_set_server_address (session->priv->job, session->priv->server_address);
        g_signal_connect (session->priv->job,
                          "stopped",
                          G_CALLBACK (worker_stopped),
                          session);
        g_signal_connect (session->priv->job,
                          "started",
                          G_CALLBACK (worker_started),
                          session);
        g_signal_connect (session->priv->job,
                          "exited",
                          G_CALLBACK (worker_exited),
                          session);
        g_signal_connect (session->priv->job,
                          "died",
                          G_CALLBACK (worker_died),
                          session);

        res = gdm_session_worker_job_start (session->priv->job);

        return res;
}

static void
stop_worker (GdmSessionDirect *session)
{
        cancel_pending_query (session);

        if (session->priv->worker_connection != NULL) {
                dbus_connection_close (session->priv->worker_connection);
                session->priv->worker_connection = NULL;
        }

        gdm_session_worker_job_stop (session->priv->job);
        session->priv->job = NULL;
}

static void
gdm_session_direct_open (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        gboolean          res;

        g_return_if_fail (session != NULL);

        g_debug ("GdmSessionDirect: Opening session");

        res = start_worker (impl);
}

static void
send_setup (GdmSessionDirect *session)
{
        DBusMessage    *message;
        DBusMessageIter iter;
        const char     *display_name;
        const char     *display_device;
        const char     *display_hostname;

        if (session->priv->display_name != NULL) {
                display_name = session->priv->display_name;
        } else {
                display_name = "";
        }
        if (session->priv->display_hostname != NULL) {
                display_hostname = session->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (session->priv->display_device != NULL) {
                display_device = session->priv->display_device;
        } else {
                display_device = "";
        }

        g_debug ("GdmSessionDirect: Beginning setup");

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "Setup");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->service_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_device);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_hostname);

        if (! send_dbus_message (session->priv->worker_connection, message)) {
                g_debug ("GdmSessionDirect: Could not send %s signal", "Setup");
        }

        dbus_message_unref (message);
}

static void
send_setup_for_user (GdmSessionDirect *session)
{
        DBusMessage    *message;
        DBusMessageIter iter;
        const char     *display_name;
        const char     *display_device;
        const char     *display_hostname;
        const char     *selected_user;

        if (session->priv->display_name != NULL) {
                display_name = session->priv->display_name;
        } else {
                display_name = "";
        }
        if (session->priv->display_hostname != NULL) {
                display_hostname = session->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (session->priv->display_device != NULL) {
                display_device = session->priv->display_device;
        } else {
                display_device = "";
        }
        if (session->priv->selected_user != NULL) {
                selected_user = session->priv->selected_user;
        } else {
                selected_user = "";
        }

        g_debug ("GdmSessionDirect: Beginning setup for user %s", session->priv->selected_user);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "SetupForUser");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->service_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_device);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_hostname);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &selected_user);

        if (! send_dbus_message (session->priv->worker_connection, message)) {
                g_debug ("GdmSessionDirect: Could not send %s signal", "SetupForUser");
        }

        dbus_message_unref (message);
}

static void
gdm_session_direct_setup (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);
        g_return_if_fail (dbus_connection_get_is_connected (impl->priv->worker_connection));

        send_setup (impl);
}

static void
gdm_session_direct_setup_for_user (GdmSession  *session,
                                   const char  *username)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);
        g_return_if_fail (dbus_connection_get_is_connected (impl->priv->worker_connection));
        g_return_if_fail (username != NULL);

        impl->priv->selected_user = g_strdup (username);

        send_setup_for_user (impl);
}

static void
gdm_session_direct_authenticate (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);
        g_return_if_fail (dbus_connection_get_is_connected (impl->priv->worker_connection));

        send_dbus_void_signal (impl, "Authenticate");
}

static void
gdm_session_direct_authorize (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);
        g_return_if_fail (dbus_connection_get_is_connected (impl->priv->worker_connection));

        send_dbus_void_signal (impl, "Authorize");
}

static void
gdm_session_direct_accredit (GdmSession *session,
                             int         cred_flag)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);
        g_return_if_fail (dbus_connection_get_is_connected (impl->priv->worker_connection));

        switch (cred_flag) {
        case GDM_SESSION_CRED_ESTABLISH:
                send_dbus_void_signal (impl, "EstablishCredentials");
                break;
        case GDM_SESSION_CRED_RENEW:
                send_dbus_void_signal (impl, "RenewCredentials");
                break;
        default:
                g_assert_not_reached ();
        }
}

static void
send_environment_variable (const char       *key,
                           const char       *value,
                           GdmSessionDirect *session)
{
        DBusMessage    *message;
        DBusMessageIter iter;

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "SetEnvironmentVariable");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &value);

        if (! send_dbus_message (session->priv->worker_connection, message)) {
                g_debug ("GdmSessionDirect: Could not send %s signal", "SetEnvironmentVariable");
        }

        dbus_message_unref (message);
}

static void
send_environment (GdmSessionDirect *session)
{

        g_hash_table_foreach (session->priv->environment,
                              (GHFunc) send_environment_variable,
                              session);
}

static gboolean
is_prog_in_path (const char *prog)
{
        char    *f;
        gboolean ret;

        f = g_find_program_in_path (prog);
        ret = (f != NULL);
        g_free (f);
        return ret;
}

static gboolean
get_session_command_for_file (const char *file,
                              char      **command)
{
        GKeyFile   *key_file;
        GError     *error;
        char       *full_path;
        char       *exec;
        gboolean    ret;
        gboolean    res;
        const char *search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
                NULL
        };

        exec = NULL;
        ret = FALSE;
        if (command != NULL) {
                *command = NULL;
        }

        key_file = g_key_file_new ();

        error = NULL;
        full_path = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         search_dirs,
                                         &full_path,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSessionDirect: File '%s' not found: %s", file, error->message);
                g_error_free (error);
                if (command != NULL) {
                        *command = NULL;
                }
                goto out;
        }

        error = NULL;
        res = g_key_file_get_boolean (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_HIDDEN,
                                      &error);
        if (error == NULL && res) {
                g_debug ("GdmSessionDirect: Session %s is marked as hidden", file);
                goto out;
        }

        error = NULL;
        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC,
                                      &error);
        if (exec == NULL) {
                g_debug ("GdmSessionDirect: %s key not found", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
                goto out;
        }

        res = is_prog_in_path (exec);
        g_free (exec);

        if (! res) {
                g_debug ("GdmSessionDirect: Command not found: %s", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
                goto out;
        }

        error = NULL;
        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_EXEC,
                                      &error);
        if (error != NULL) {
                g_debug ("GdmSessionDirect: %s key not found: %s",
                         G_KEY_FILE_DESKTOP_KEY_EXEC,
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (command != NULL) {
                *command = g_strdup (exec);
        }
        ret = TRUE;

out:
        g_free (exec);

        return ret;
}

static char *
get_session_command (GdmSessionDirect *session)
{
        gboolean res;
        char    *command;

        command = NULL;
        res = get_session_command_for_file (session->priv->selected_session,
                                            &command);
        if (! res) {
                g_critical ("Cannot read specified session file: %s", session->priv->selected_session);
                exit (1);
        }

        return command;
}

static gboolean
open_ck_session (GdmSessionDirect *session)
{
        struct passwd *pwent;
        gboolean       ret;
        int            res;
        DBusError      error;
        const char     *display_name;
        const char     *display_device;
        const char     *display_hostname;

        if (session->priv->display_name != NULL) {
                display_name = session->priv->display_name;
        } else {
                display_name = "";
        }
        if (session->priv->display_hostname != NULL) {
                display_hostname = session->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (session->priv->display_device != NULL) {
                display_device = session->priv->display_device;
        } else {
                display_device = "";
        }

        pwent = getpwnam (session->priv->selected_user);
        if (pwent == NULL) {
                return FALSE;
        }

        session->priv->ckc = ck_connector_new ();
        if (session->priv->ckc == NULL) {
                g_warning ("Couldn't create new ConsoleKit connector");
                goto out;
        }

        dbus_error_init (&error);
        res = ck_connector_open_session_with_parameters (session->priv->ckc,
                                                         &error,
                                                         "unix-user", &pwent->pw_uid,
                                                         "x11-display", &display_name,
                                                         "x11-display-device", &display_device,
                                                         "remote-host-name", &display_hostname,
                                                         "is-local", &session->priv->display_is_local,
                                                         NULL);

        if (! res) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("%s\n", error.message);
                        dbus_error_free (&error);
                } else {
                        g_warning ("cannot open CK session: OOM, D-Bus system bus not available,\n"
                                   "ConsoleKit not available or insufficient privileges.\n");
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
gdm_session_direct_set_environment_variable (GdmSessionDirect *session,
                                             const char       *key,
                                             const char       *value)
{
        g_return_if_fail (session != NULL);
        g_return_if_fail (session != NULL);
        g_return_if_fail (key != NULL);
        g_return_if_fail (value != NULL);

        g_hash_table_replace (session->priv->environment,
                              g_strdup (key),
                              g_strdup (value));
}

static void
setup_session_environment (GdmSessionDirect *session)
{
        const char *session_cookie;
        gboolean    res;

        session_cookie = NULL;
        res = open_ck_session (session);
        if (res) {
                session_cookie = ck_connector_get_cookie (session->priv->ckc);
        }

        gdm_session_direct_set_environment_variable (session,
                                                     "GDMSESSION",
                                                     session->priv->selected_session);
        gdm_session_direct_set_environment_variable (session,
                                                     "DESKTOP_SESSION",
                                                     session->priv->selected_session);

        gdm_session_direct_set_environment_variable (session,
                                                     "LANG",
                                                     session->priv->selected_language);
        gdm_session_direct_set_environment_variable (session,
                                                     "GDM_LANG",
                                                     session->priv->selected_language);

        gdm_session_direct_set_environment_variable (session,
                                                     "DISPLAY",
                                                     session->priv->display_name);
        if (session_cookie != NULL) {
                gdm_session_direct_set_environment_variable (session,
                                                             "XDG_SESSION_COOKIE",
                                                             session_cookie);
        }

        if (session->priv->user_x11_authority_file != NULL) {
                gdm_session_direct_set_environment_variable (session,
                                                             "XAUTHORITY",
                                                             session->priv->user_x11_authority_file);
        }

        gdm_session_direct_set_environment_variable (session,
                                                     "PATH",
                                                     "/bin:/usr/bin:" BINDIR);

}

static void
gdm_session_direct_start_session (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);
        char             *command;
        char             *program;

        g_return_if_fail (session != NULL);
        g_return_if_fail (impl->priv->is_running == FALSE);

        command = get_session_command (impl);
        program = g_strdup_printf (GDMCONFDIR "/Xsession \"%s\"", command);
        g_free (command);

        setup_session_environment (impl);
        send_environment (impl);

        send_dbus_string_signal (impl, "StartProgram", program);
        g_free (program);
}

static void
gdm_session_direct_close (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);

        g_debug ("GdmSessionDirect: Closing session");

        if (impl->priv->ckc != NULL) {
                ck_connector_close_session (impl->priv->ckc, NULL);
                ck_connector_unref (impl->priv->ckc);
                impl->priv->ckc = NULL;
        }

        if (impl->priv->job != NULL) {
                if (impl->priv->is_running) {
                        gdm_session_record_logout (impl->priv->session_pid,
                                                   impl->priv->selected_user,
                                                   impl->priv->display_hostname,
                                                   impl->priv->display_name,
                                                   impl->priv->display_device);
                }

                stop_worker (impl);
        }

        g_free (impl->priv->selected_user);
        impl->priv->selected_user = NULL;

        g_free (impl->priv->selected_session);
        impl->priv->selected_session = NULL;

        g_free (impl->priv->selected_language);
        impl->priv->selected_language = NULL;

        g_free (impl->priv->user_x11_authority_file);
        impl->priv->user_x11_authority_file = NULL;

        g_hash_table_remove_all (impl->priv->environment);

        impl->priv->session_pid = -1;
        impl->priv->is_authenticated = FALSE;
        impl->priv->is_running = FALSE;
}

static void
gdm_session_direct_answer_query  (GdmSession *session,
                                  const char *text)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);

        answer_pending_query (impl, text);
}

static void
gdm_session_direct_cancel  (GdmSession *session)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_return_if_fail (session != NULL);

        cancel_pending_query (impl);
}

char *
gdm_session_direct_get_username (GdmSessionDirect *session)
{
        g_return_val_if_fail (session != NULL, NULL);

        return g_strdup (session->priv->selected_user);
}

static void
gdm_session_direct_select_session (GdmSession *session,
                                   const char *text)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_free (impl->priv->selected_session);
        impl->priv->selected_session = g_strdup (text);
}

static void
gdm_session_direct_select_language (GdmSession *session,
                                    const char *text)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_free (impl->priv->selected_language);
        impl->priv->selected_language = g_strdup (text);
}

static void
gdm_session_direct_select_user (GdmSession *session,
                                const char *text)
{
        GdmSessionDirect *impl = GDM_SESSION_DIRECT (session);

        g_free (impl->priv->selected_user);
        impl->priv->selected_user = g_strdup (text);
}

/* At some point we may want to read these right from
 * the slave but for now I don't want the dependency */
static void
_gdm_session_direct_set_display_name (GdmSessionDirect *session,
                                      const char       *name)
{
        g_free (session->priv->display_name);
        session->priv->display_name = g_strdup (name);
}

static void
_gdm_session_direct_set_display_hostname (GdmSessionDirect *session,
                                          const char       *name)
{
        g_free (session->priv->display_hostname);
        session->priv->display_hostname = g_strdup (name);
}

static void
_gdm_session_direct_set_display_device (GdmSessionDirect *session,
                                        const char       *name)
{
        g_debug ("GdmSessionDirect: Setting display device: %s", name);
        g_free (session->priv->display_device);
        session->priv->display_device = g_strdup (name);
}

static void
_gdm_session_direct_set_user_x11_authority_file (GdmSessionDirect *session,
                                                 const char       *name)
{
        g_free (session->priv->user_x11_authority_file);
        session->priv->user_x11_authority_file = g_strdup (name);
}

static void
_gdm_session_direct_set_display_is_local (GdmSessionDirect *session,
                                          gboolean          is)
{
        session->priv->display_is_local = is;
}

static void
gdm_session_direct_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        GdmSessionDirect *self;

        self = GDM_SESSION_DIRECT (object);

        switch (prop_id) {
        case PROP_DISPLAY_NAME:
                _gdm_session_direct_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                _gdm_session_direct_set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_DEVICE:
                _gdm_session_direct_set_display_device (self, g_value_get_string (value));
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                _gdm_session_direct_set_user_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_session_direct_set_display_is_local (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_direct_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
        GdmSessionDirect *self;

        self = GDM_SESSION_DIRECT (object);

        switch (prop_id) {
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->priv->display_name);
                break;
        case PROP_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->priv->display_hostname);
                break;
        case PROP_DISPLAY_DEVICE:
                g_value_set_string (value, self->priv->display_device);
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->user_x11_authority_file);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_direct_dispose (GObject *object)
{
        GdmSessionDirect *session;

        session = GDM_SESSION_DIRECT (object);

        g_debug ("GdmSessionDirect: Disposing session");

        gdm_session_direct_close (GDM_SESSION (session));

        g_free (session->priv->service_name);
        session->priv->service_name = NULL;

        g_free (session->priv->display_name);
        session->priv->display_name = NULL;

        g_free (session->priv->display_hostname);
        session->priv->display_hostname = NULL;

        g_free (session->priv->display_device);
        session->priv->display_device = NULL;

        g_free (session->priv->server_address);
        session->priv->server_address = NULL;

        if (session->priv->server != NULL) {
                dbus_server_disconnect (session->priv->server);
                dbus_server_unref (session->priv->server);
                session->priv->server = NULL;
        }

        if (session->priv->environment != NULL) {
                g_hash_table_destroy (session->priv->environment);
                session->priv->environment = NULL;
        }

        G_OBJECT_CLASS (gdm_session_direct_parent_class)->dispose (object);
}

static void
gdm_session_direct_finalize (GObject *object)
{
        GdmSessionDirect *session;
        GObjectClass     *parent_class;

        session = GDM_SESSION_DIRECT (object);

        g_free (session->priv->selected_user);

        parent_class = G_OBJECT_CLASS (gdm_session_direct_parent_class);

        if (parent_class->finalize != NULL)
                parent_class->finalize (object);
}

static void
gdm_session_iface_init (GdmSessionIface *iface)
{
        iface->open = gdm_session_direct_open;
        iface->setup = gdm_session_direct_setup;
        iface->setup_for_user = gdm_session_direct_setup_for_user;
        iface->authenticate = gdm_session_direct_authenticate;
        iface->authorize = gdm_session_direct_authorize;
        iface->accredit = gdm_session_direct_accredit;
        iface->close = gdm_session_direct_close;

        iface->cancel = gdm_session_direct_cancel;
        iface->start_session = gdm_session_direct_start_session;
        iface->answer_query = gdm_session_direct_answer_query;
        iface->select_session = gdm_session_direct_select_session;
        iface->select_language = gdm_session_direct_select_language;
        iface->select_user = gdm_session_direct_select_user;
}

static void
gdm_session_direct_class_init (GdmSessionDirectClass *session_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (session_class);

        object_class->get_property = gdm_session_direct_get_property;
        object_class->set_property = gdm_session_direct_set_property;
        object_class->dispose = gdm_session_direct_dispose;
        object_class->finalize = gdm_session_direct_finalize;

        g_type_class_add_private (session_class, sizeof (GdmSessionDirectPrivate));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "display hostname",
                                                              "display hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        /* not construct only */
        g_object_class_install_property (object_class,
                                         PROP_USER_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("user-x11-authority-file",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_DEVICE,
                                         g_param_spec_string ("display-device",
                                                              "display device",
                                                              "display device",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


}

GdmSessionDirect *
gdm_session_direct_new (const char *display_name,
                        const char *display_hostname,
                        const char *display_device,
                        gboolean    display_is_local)
{
        GdmSessionDirect *session;

        session = g_object_new (GDM_TYPE_SESSION_DIRECT,
                                "display-name", display_name,
                                "display-hostname", display_hostname,
                                "display-device", display_device,
                                "display-is-local", display_is_local,
                                NULL);

        return session;
}
