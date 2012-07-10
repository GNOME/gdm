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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#ifdef HAVE_LIBXKLAVIER
#include <libxklavier/xklavier.h>
#include <X11/Xlib.h> /* for Display */
#endif

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-session.h"
#include "gdm-session-private.h"

#include "gdm-session-record.h"
#include "gdm-session-worker-job.h"
#include "gdm-common.h"

#define GDM_SESSION_DBUS_PATH         "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_INTERFACE    "org.gnome.DisplayManager.Session"
#define GDM_SESSION_DBUS_ERROR_CANCEL "org.gnome.DisplayManager.Session.Error.Cancel"

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"
#endif

typedef struct
{
        GdmSession            *session;
        GdmSessionWorkerJob   *job;
        GPid                   worker_pid;
        char                  *service_name;
        DBusConnection        *worker_connection;
        DBusMessage           *message_pending_reply;
        guint32                is_stopping : 1;
} GdmSessionConversation;

struct _GdmSessionPrivate
{
        /* per open scope */
        char                *selected_program;
        char                *selected_session;
        char                *saved_session;
        char                *selected_language;
        char                *saved_language;
        char                *selected_user;
        char                *user_x11_authority_file;

        GHashTable          *conversations;

        GdmSessionConversation *session_conversation;

        GList               *pending_connections;

        GPid                 session_pid;

        /* object lifetime scope */
        char                *display_id;
        char                *display_name;
        char                *display_hostname;
        char                *display_device;
        char                *display_seat_id;
        char                *display_x11_authority_file;
        gboolean             display_is_local;

        char                *fallback_session_name;

        DBusServer          *server;
        char                *server_address;
        GHashTable          *environment;
        DBusGConnection     *connection;
};

enum {
        PROP_0,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_DEVICE,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_X11_AUTHORITY_FILE,
        PROP_USER_X11_AUTHORITY_FILE,
};

enum {
        CONVERSATION_STARTED = 0,
        CONVERSATION_STOPPED,
        SERVICE_UNAVAILABLE,
        SETUP_COMPLETE,
        SETUP_FAILED,
        RESET_COMPLETE,
        RESET_FAILED,
        AUTHENTICATED,
        AUTHENTICATION_FAILED,
        AUTHORIZED,
        AUTHORIZATION_FAILED,
        ACCREDITED,
        ACCREDITATION_FAILED,
        INFO,
        PROBLEM,
        INFO_QUERY,
        SECRET_INFO_QUERY,
        SESSION_OPENED,
        SESSION_OPEN_FAILED,
        SESSION_STARTED,
        SESSION_START_FAILED,
        SESSION_EXITED,
        SESSION_DIED,
        SELECTED_USER_CHANGED,
        DEFAULT_LANGUAGE_NAME_CHANGED,
        DEFAULT_SESSION_NAME_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GdmSession,
               gdm_session,
               G_TYPE_OBJECT);

static gboolean
send_dbus_message (GdmSessionConversation *conversation,
                   DBusMessage            *message)
{
        gboolean is_connected;
        gboolean sent;

        g_return_val_if_fail (message != NULL, FALSE);

        if (conversation->worker_connection == NULL) {
                g_warning ("There is no valid connection");
                return FALSE;
        }

        is_connected = dbus_connection_get_is_connected (conversation->worker_connection);
        if (! is_connected) {
                g_warning ("Not connected!");
                return FALSE;
        }

        sent = dbus_connection_send (conversation->worker_connection, message, NULL);

        return sent;
}

static void
send_dbus_string_signal (GdmSessionConversation *conversation,
                         const char             *name,
                         const char             *text)
{
        DBusMessage    *message;
        DBusMessageIter iter;

        g_return_if_fail (conversation != NULL);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &text);

        if (! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal",
                         name ? name : "(null)");
        }

        dbus_message_unref (message);
}

static void
send_dbus_void_signal (GdmSessionConversation *conversation,
                       const char             *name)
{
        DBusMessage *message;

        g_return_if_fail (conversation != NULL);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           name);

        if (! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal", name);
        }

        dbus_message_unref (message);
}

static void
emit_service_unavailable (GdmSession   *self,
                          const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SERVICE_UNAVAILABLE], 0, service_name);
}

static void
emit_setup_complete (GdmSession   *self,
                     const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SETUP_COMPLETE], 0, service_name);
}

static void
emit_setup_failed (GdmSession   *self,
                   const char   *service_name,
                   const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SETUP_FAILED], 0, service_name, text);
}

static void
emit_reset_complete (GdmSession   *self)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [RESET_COMPLETE], 0);
}

static void
emit_reset_failed (GdmSession   *self,
                   const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [RESET_FAILED], 0, text);
}

static void
emit_authenticated (GdmSession   *self,
                    const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [AUTHENTICATED], 0, service_name);
}

static void
emit_authentication_failed (GdmSession   *self,
                            const char   *service_name,
                            const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [AUTHENTICATION_FAILED], 0, service_name, text);
}

static void
emit_authorized (GdmSession   *self,
                 const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [AUTHORIZED], 0, service_name);
}

static void
emit_authorization_failed (GdmSession   *self,
                           const char   *service_name,
                           const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [AUTHORIZATION_FAILED], 0, service_name, text);
}

static void
emit_accredited (GdmSession   *self,
                 const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [ACCREDITED], 0, service_name);
}

static void
emit_accreditation_failed (GdmSession   *self,
                           const char   *service_name,
                           const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [ACCREDITATION_FAILED], 0, service_name, text);
}

static void
emit_info_query (GdmSession   *self,
                 const char   *service_name,
                 const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [INFO_QUERY], 0, service_name, text);
}

static void
emit_secret_info_query (GdmSession   *self,
                        const char   *service_name,
                        const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SECRET_INFO_QUERY], 0, service_name, text);
}

static void
emit_info (GdmSession   *self,
           const char   *service_name,
           const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [INFO], 0, service_name, text);
}

static void
emit_problem (GdmSession   *self,
              const char   *service_name,
              const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [PROBLEM], 0, service_name, text);
}

static void
emit_session_opened (GdmSession   *self,
                     const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_OPENED], 0, service_name);
}

static void
emit_session_open_failed (GdmSession   *self,
                          const char   *service_name,
                          const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_OPEN_FAILED], 0, service_name, text);
}

static void
emit_session_started (GdmSession   *self,
                      const char   *service_name,
                      int           pid)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_STARTED], 0, service_name, pid);
}

static void
emit_session_start_failed (GdmSession   *self,
                           const char   *service_name,
                           const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_START_FAILED], 0, service_name, text);
}

static void
emit_session_exited (GdmSession   *self,
                     int           exit_code)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_EXITED], 0, exit_code);
}

static void
emit_session_died (GdmSession   *self,
                   int           signal_number)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SESSION_DIED], 0, signal_number);
}

static void
emit_conversation_started (GdmSession   *self,
                           const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [CONVERSATION_STARTED], 0, service_name);
}

static void
emit_conversation_stopped (GdmSession   *self,
                           const char   *service_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [CONVERSATION_STOPPED], 0, service_name);
}

static void
emit_default_language_name_changed (GdmSession   *self,
                                    const char   *language_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [DEFAULT_LANGUAGE_NAME_CHANGED], 0, language_name);
}

static void
emit_default_session_name_changed (GdmSession   *self,
                                   const char   *session_name)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [DEFAULT_SESSION_NAME_CHANGED], 0, session_name);
}

static void
emit_selected_user_changed (GdmSession   *self,
                            const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (self, signals [SELECTED_USER_CHANGED], 0, text);
}

static GdmSessionConversation *
find_conversation_by_name (GdmSession *self,
                           const char *service_name)
{
        GdmSessionConversation *conversation;

        conversation = g_hash_table_lookup (self->priv->conversations, service_name);

        if (conversation == NULL) {
                g_warning ("Tried to look up non-existent conversation %s", service_name);
        }

        return conversation;
}

static void
on_authentication_failed (GdmSession *self,
                          const char *service_name,
                          const char *message)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_session_record_failed (conversation->worker_pid,
                                           self->priv->selected_user,
                                           self->priv->display_hostname,
                                           self->priv->display_name,
                                           self->priv->display_device);
        }
}

static void
on_session_started (GdmSession *self,
                    const char *service_name)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_session_record_login (conversation->worker_pid,
                                          self->priv->selected_user,
                                          self->priv->display_hostname,
                                          self->priv->display_name,
                                          self->priv->display_device);
        }
}

static void
on_session_start_failed (GdmSession *self,
                         const char *service_name,
                         const char *message)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_session_record_login (conversation->worker_pid,
                                          self->priv->selected_user,
                                          self->priv->display_hostname,
                                          self->priv->display_name,
                                          self->priv->display_device);
        }
}

static void
on_session_exited (GdmSession *self,
                   int        exit_code)
{

        gdm_session_record_logout (self->priv->session_pid,
                                   self->priv->selected_user,
                                   self->priv->display_hostname,
                                   self->priv->display_name,
                                   self->priv->display_device);
}


static DBusHandlerResult
gdm_session_handle_service_unavailable (GdmSession             *self,
                                        GdmSessionConversation *conversation,
                                        DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'service-unavailable' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_service_unavailable (self, conversation->service_name);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_setup_complete (GdmSession             *self,
                                   GdmSessionConversation *conversation,
                                   DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'setup-complete' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_setup_complete (self, conversation->service_name);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_setup_failed (GdmSession             *self,
                                 GdmSessionConversation *conversation,
                                 DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'setup-failed' signal");

        emit_setup_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
gdm_session_handle_reset_complete (GdmSession             *self,
                                   GdmSessionConversation *conversation,
                                   DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'reset-complete' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_reset_complete (self);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_reset_failed (GdmSession             *self,
                                 GdmSessionConversation *conversation,
                                 DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'reset-failed' signal");

        emit_reset_failed (self, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_authenticated (GdmSession             *self,
                                  GdmSessionConversation *conversation,
                                  DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'authenticated' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_authenticated (self, conversation->service_name);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_authentication_failed (GdmSession             *self,
                                          GdmSessionConversation *conversation,
                                          DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'authentication-failed' signal");

        emit_authentication_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_authorized (GdmSession             *self,
                               GdmSessionConversation *conversation,
                               DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'authorized' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_authorized (self, conversation->service_name);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_authorization_failed (GdmSession             *self,
                                         GdmSessionConversation *conversation,
                                         DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'authorization-failed' signal");

        emit_authorization_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_accredited (GdmSession             *self,
                               GdmSessionConversation *conversation,
                               DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: Emitting 'accredited' signal");

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        emit_accredited (self, conversation->service_name);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_accreditation_failed (GdmSession             *self,
                                         GdmSessionConversation *conversation,
                                         DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'accreditation-failed' signal");

        emit_accreditation_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static const char **
get_system_session_dirs (void)
{
        static const char *search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/xsessions/",
                DATADIR "/gdm/BuiltInSessions/",
                NULL
        };

        return search_dirs;
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
        char       *exec;
        gboolean    ret;
        gboolean    res;

        exec = NULL;
        ret = FALSE;
        if (command != NULL) {
                *command = NULL;
        }

        key_file = g_key_file_new ();

        g_debug ("GdmSession: looking for session file '%s'", file);

        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         get_system_session_dirs (),
                                         NULL,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSession: File '%s' not found: %s", file, error->message);
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
                g_debug ("GdmSession: Session %s is marked as hidden", file);
                goto out;
        }

        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC,
                                      NULL);
        if (exec != NULL) {
                res = is_prog_in_path (exec);
                g_free (exec);
                exec = NULL;

                if (! res) {
                        g_debug ("GdmSession: Command not found: %s",
                                 G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
                        goto out;
                }
        }

        error = NULL;
        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_EXEC,
                                      &error);
        if (error != NULL) {
                g_debug ("GdmSession: %s key not found: %s",
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

static gboolean
get_session_command_for_name (const char *name,
                              char      **command)
{
        gboolean res;
        char    *filename;

        filename = g_strdup_printf ("%s.desktop", name);
        res = get_session_command_for_file (filename, command);
        g_free (filename);

        /*
         * The GDM Xsession script honors "custom" as a valid session.  If the
         * session is one of these, no file is needed, then just run the
         * command as "custom".
         */
        if (!res && strcmp (name, GDM_CUSTOM_SESSION) == 0) {
                g_debug ("No custom desktop file, but accepting it anyway.");
                if (command != NULL) {
                        *command = g_strdup (GDM_CUSTOM_SESSION);
                }
                res = TRUE;
        }

        return res;
}

static const char *
get_default_language_name (GdmSession *self)
{
    if (self->priv->saved_language != NULL) {
                return self->priv->saved_language;
    }

    return setlocale (LC_MESSAGES, NULL);
}

static const char *
get_fallback_session_name (GdmSession *self)
{
        const char    **search_dirs;
        int             i;
        char           *name;
        GSequence      *sessions;
        GSequenceIter  *session;

        if (self->priv->fallback_session_name != NULL) {
                /* verify that the cached version still exists */
                if (get_session_command_for_name (self->priv->fallback_session_name, NULL)) {
                        goto out;
                }
        }

        name = g_strdup ("gnome");
        if (get_session_command_for_name (name, NULL)) {
                g_free (self->priv->fallback_session_name);
                self->priv->fallback_session_name = name;
                goto out;
        }
        g_free (name);

        sessions = g_sequence_new (g_free);

        search_dirs = get_system_session_dirs ();
        for (i = 0; search_dirs[i] != NULL; i++) {
                GDir       *dir;
                const char *base_name;

                dir = g_dir_open (search_dirs[i], 0, NULL);

                if (dir == NULL) {
                        continue;
                }

                do {
                        base_name = g_dir_read_name (dir);

                        if (base_name == NULL) {
                                break;
                        }

                        if (!g_str_has_suffix (base_name, ".desktop")) {
                                continue;
                        }

                        if (get_session_command_for_file (base_name, NULL)) {

                                g_sequence_insert_sorted (sessions, g_strdup (base_name), (GCompareDataFunc) g_strcmp0, NULL);
                        }
                } while (base_name != NULL);

                g_dir_close (dir);
        }

        name = NULL;
        session = g_sequence_get_begin_iter (sessions);
        do {
               if (g_sequence_get (session)) {
                       char *base_name;

                       g_free (name);
                       base_name = g_sequence_get (session);
                       name = g_strndup (base_name,
                                         strlen (base_name) -
                                         strlen (".desktop"));

                       break;
               }
               session = g_sequence_iter_next (session);
        } while (!g_sequence_iter_is_end (session));

        g_free (self->priv->fallback_session_name);
        self->priv->fallback_session_name = name;

        g_sequence_free (sessions);

 out:
        return self->priv->fallback_session_name;
}

static const char *
get_default_session_name (GdmSession *self)
{
        if (self->priv->saved_session != NULL) {
                return self->priv->saved_session;
        }

        return get_fallback_session_name (self);
}

static void
gdm_session_defaults_changed (GdmSession *self)
{
        emit_default_language_name_changed (self,
                                            get_default_language_name (self));
        emit_default_session_name_changed (self,
                                           get_default_session_name (self));
}

void
gdm_session_select_user (GdmSession *self,
                         const char *text)
{

        g_debug ("GdmSession: Setting user: '%s'", text);

        g_free (self->priv->selected_user);
        self->priv->selected_user = g_strdup (text);

        g_free (self->priv->saved_session);
        self->priv->saved_session = NULL;

        g_free (self->priv->saved_language);
        self->priv->saved_language = NULL;
}

static DBusHandlerResult
gdm_session_handle_username_changed (GdmSession             *self,
                                     GdmSessionConversation *conversation,
                                     DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: changing username from '%s' to '%s'",
                 self->priv->selected_user != NULL ? self->priv->selected_user : "<unset>",
                 (strlen (text)) ? text : "<unset>");

        gdm_session_select_user (self, (strlen (text) > 0) ? g_strdup (text) : NULL);

        emit_selected_user_changed (self, self->priv->selected_user);

        gdm_session_defaults_changed (self);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static void
cancel_pending_query (GdmSessionConversation *conversation)
{
        DBusMessage *reply;

        if (conversation->message_pending_reply == NULL) {
                return;
        }

        g_debug ("GdmSession: Cancelling pending query");

        reply = dbus_message_new_error (conversation->message_pending_reply,
                                        GDM_SESSION_DBUS_ERROR_CANCEL,
                                        "Operation cancelled");
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_connection_flush (conversation->worker_connection);

        dbus_message_unref (reply);
        dbus_message_unref (conversation->message_pending_reply);
        conversation->message_pending_reply = NULL;
}

static void
answer_pending_query (GdmSessionConversation *conversation,
                      const char             *answer)
{
        DBusMessage    *reply;
        DBusMessageIter iter;

        reply = dbus_message_new_method_return (conversation->message_pending_reply);
        dbus_message_iter_init_append (reply, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &answer);

        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        dbus_message_unref (conversation->message_pending_reply);
        conversation->message_pending_reply = NULL;
}

static void
set_pending_query (GdmSessionConversation *conversation,
                   DBusMessage            *message)
{
        g_assert (conversation->message_pending_reply == NULL);

        conversation->message_pending_reply = dbus_message_ref (message);
}

static DBusHandlerResult
gdm_session_handle_info_query (GdmSession             *self,
                               GdmSessionConversation *conversation,
                               DBusMessage            *message)
{
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        set_pending_query (conversation, message);

        g_debug ("GdmSession: Emitting 'info-query' signal");
        emit_info_query (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_secret_info_query (GdmSession             *self,
                                      GdmSessionConversation *conversation,
                                      DBusMessage            *message)
{
        DBusError    error;
        const char  *text;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &text,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        set_pending_query (conversation, message);

        g_debug ("GdmSession: Emitting 'secret-info-query' signal");
        emit_secret_info_query (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_info (GdmSession             *self,
                         GdmSessionConversation *conversation,
                         DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'info' signal");
        emit_info (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_cancel_pending_query (GdmSession             *self,
                                         GdmSessionConversation *conversation,
                                         DBusMessage            *message)
{
        DBusMessage *reply;

        g_debug ("GdmSession: worker cancelling pending query");

        cancel_pending_query (conversation);

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_problem (GdmSession             *self,
                            GdmSessionConversation *conversation,
                            DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'problem' signal");
        emit_problem (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_opened (GdmSession             *self,
                                   GdmSessionConversation *conversation,
                                   DBusMessage            *message)
{
        DBusMessage *reply;
        DBusError    error;

        g_debug ("GdmSession: Handling SessionOpened");

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error, DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        g_debug ("GdmSession: Emitting 'session-opened' signal");

        emit_session_opened (self, conversation->service_name);

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_open_failed (GdmSession             *self,
                                GdmSessionConversation *conversation,
                                DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'session-open-failed' signal");
        emit_session_open_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_started (GdmSession             *self,
                                    GdmSessionConversation *conversation,
                                    DBusMessage            *message)
{
        DBusMessage *reply;
        DBusError    error;
        int          pid;

        pid = 0;

        g_debug ("GdmSession: Handling SessionStarted");

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_INT32, &pid,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'session-started' signal with pid '%d'",
                 pid);

        self->priv->session_pid = pid;
        self->priv->session_conversation = conversation;

        emit_session_started (self, conversation->service_name, pid);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_start_failed (GdmSession             *self,
                                 GdmSessionConversation *conversation,
                                 DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'session-start-failed' signal");
        emit_session_start_failed (self, conversation->service_name, text);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_exited (GdmSession             *self,
                                   GdmSessionConversation *conversation,
                                   DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'session-exited' signal with exit code '%d'",
                 code);

        self->priv->session_conversation = NULL;
        emit_session_exited (self, code);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_died (GdmSession             *self,
                                 GdmSessionConversation *conversation,
                                 DBusMessage            *message)
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
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        g_debug ("GdmSession: Emitting 'session-died' signal with signal number '%d'",
                 code);

        self->priv->session_conversation = NULL;
        emit_session_died (self, code);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_saved_language_name_read (GdmSession             *self,
                                             GdmSessionConversation *conversation,
                                             DBusMessage            *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *language_name;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &language_name,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        if (strcmp (language_name,
                    get_default_language_name (self)) != 0) {
                g_free (self->priv->saved_language);
                self->priv->saved_language = g_strdup (language_name);

                emit_default_language_name_changed (self, language_name);
        }


        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_saved_session_name_read (GdmSession             *self,
                                            GdmSessionConversation *conversation,
                                            DBusMessage            *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *session_name;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &session_name,
                                     DBUS_TYPE_INVALID)) {
                g_warning ("ERROR: %s", error.message);
        }

        reply = dbus_message_new_method_return (message);
        dbus_connection_send (conversation->worker_connection, reply, NULL);
        dbus_message_unref (reply);

        if (! get_session_command_for_name (session_name, NULL)) {
                /* ignore sessions that don't exist */
                g_debug ("GdmSession: not using invalid .dmrc session: %s", session_name);
                g_free (self->priv->saved_session);
                self->priv->saved_session = NULL;
                goto out;
        }

        if (strcmp (session_name,
                    get_default_session_name (self)) != 0) {
                g_free (self->priv->saved_session);
                self->priv->saved_session = g_strdup (session_name);

                emit_default_session_name_changed (self, session_name);
        }
 out:
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_worker_message (DBusConnection *connection,
                        DBusMessage    *message,
                        void           *user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *session;

        session = conversation->session;

        if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "InfoQuery")) {
                return gdm_session_handle_info_query (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SecretInfoQuery")) {
                return gdm_session_handle_secret_info_query (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Info")) {
                return gdm_session_handle_info (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Problem")) {
                return gdm_session_handle_problem (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "CancelPendingQuery")) {
                return gdm_session_handle_cancel_pending_query (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "ServiceUnavailable")) {
                return gdm_session_handle_service_unavailable (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SetupComplete")) {
                return gdm_session_handle_setup_complete (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SetupFailed")) {
                return gdm_session_handle_setup_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "ResetComplete")) {
                return gdm_session_handle_reset_complete (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "ResetFailed")) {
                return gdm_session_handle_reset_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Authenticated")) {
                return gdm_session_handle_authenticated (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AuthenticationFailed")) {
                return gdm_session_handle_authentication_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Authorized")) {
                return gdm_session_handle_authorized (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AuthorizationFailed")) {
                return gdm_session_handle_authorization_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Accredited")) {
                return gdm_session_handle_accredited (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "AccreditationFailed")) {
                return gdm_session_handle_accreditation_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "UsernameChanged")) {
                return gdm_session_handle_username_changed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionOpened")) {
                return gdm_session_handle_session_opened (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "OpenFailed")) {
                return gdm_session_handle_open_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionStarted")) {
                return gdm_session_handle_session_started (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "StartFailed")) {
                return gdm_session_handle_start_failed (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionExited")) {
                return gdm_session_handle_session_exited (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionDied")) {
                return gdm_session_handle_session_died (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SavedLanguageNameRead")) {
                return gdm_session_handle_saved_language_name_read (session, conversation, message);
        } else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SavedSessionNameRead")) {
                return gdm_session_handle_saved_session_name_read (session, conversation, message);
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

        g_debug ("GdmSession: Do introspect");

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
                               "      <arg name=\"environment\" direction=\"in\" type=\"as\"/>\n"
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
                               "      <arg name=\"display_seat\" type=\"s\"/>\n"
                               "      <arg name=\"hostname\" type=\"s\"/>\n"
                               "      <arg name=\"x11_authority_file\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetupForUser\">\n"
                               "      <arg name=\"service_name\" type=\"s\"/>\n"
                               "      <arg name=\"x11_display_name\" type=\"s\"/>\n"
                               "      <arg name=\"display_device\" type=\"s\"/>\n"
                               "      <arg name=\"display_seat\" type=\"s\"/>\n"
                               "      <arg name=\"hostname\" type=\"s\"/>\n"
                               "      <arg name=\"x11_authority_file\" type=\"s\"/>\n"
                               "      <arg name=\"username\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetupForProgram\">\n"
                               "      <arg name=\"service_name\" type=\"s\"/>\n"
                               "      <arg name=\"x11_display_name\" type=\"s\"/>\n"
                               "      <arg name=\"display_device\" type=\"s\"/>\n"
                               "      <arg name=\"display_seat\" type=\"s\"/>\n"
                               "      <arg name=\"hostname\" type=\"s\"/>\n"
                               "      <arg name=\"x11_authority_file\" type=\"s\"/>\n"
                               "      <arg name=\"log_file\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"Authenticate\">\n"
                               "    </signal>\n"
                               "    <signal name=\"Authorize\">\n"
                               "    </signal>\n"
                               "    <signal name=\"EstablishCredentials\">\n"
                               "    </signal>\n"
                               "    <signal name=\"RefreshCredentials\">\n"
                               "    </signal>\n"
                               "    <signal name=\"SetEnvironmentVariable\">\n"
                               "      <arg name=\"name\" type=\"s\"/>\n"
                               "      <arg name=\"value\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetLanguageName\">\n"
                               "      <arg name=\"language_name\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetSessionName\">\n"
                               "      <arg name=\"session_name\" type=\"s\"/>\n"
                               "    </signal>\n"
                               "    <signal name=\"SetSessionType\">\n"
                               "      <arg name=\"session_type\" type=\"s\"/>\n"
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

                g_debug ("GdmSession: Disconnected");

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

static GdmSessionConversation *
find_conversation_by_pid (GdmSession *self,
                          GPid        pid)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                if (conversation->worker_pid == pid) {
                        return conversation;
                }
        }

        return NULL;
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
        if (0 == uid) {
                return TRUE;
        }

        g_debug ("GdmSession: User not allowed");

        return FALSE;
}

static gboolean
register_worker (GdmSession *self,
                 DBusConnection   *connection)
{
        GdmSessionConversation *conversation;
        DBusObjectPathVTable vtable = { &session_unregister_handler,
                                        &session_message_handler,
                                        NULL, NULL, NULL, NULL };
        GList *connection_node;
        gulong pid;

        g_debug ("GdmSession: Authenticating new connection");

        connection_node = g_list_find (self->priv->pending_connections, connection);

        if (connection_node == NULL) {
                g_debug ("GdmSession: Ignoring connection that we aren't tracking");
                return FALSE;
        }

        self->priv->pending_connections =
                g_list_delete_link (self->priv->pending_connections,
                                    connection_node);

        if (!dbus_connection_get_unix_process_id (connection, &pid)) {
                g_warning ("GdmSession: Unable to read pid on new worker connection");
                dbus_connection_unref (connection);
                return FALSE;
        }

        conversation = find_conversation_by_pid (self, (GPid) pid);

        if (conversation == NULL) {
                g_warning ("GdmSession: New worker connection is from unknown source");
                dbus_connection_unref (connection);
                return FALSE;
        }

        conversation->worker_connection = connection;

        g_debug ("GdmSession: worker connection is %p", connection);

        dbus_connection_register_object_path (connection,
                                              GDM_SESSION_DBUS_PATH,
                                              &vtable,
                                              conversation);

        g_debug ("GdmSession: Emitting conversation-started signal");
        emit_conversation_started (self, conversation->service_name);

        g_debug ("GdmSession: Conversation started");

        return TRUE;
}

static DBusHandlerResult
on_message (DBusConnection *connection,
            DBusMessage    *message,
            void           *user_data)
{
        GdmSession *self = GDM_SESSION (user_data);

        g_debug ("GdmSession: got message");

        if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Hello")) {
                DBusMessage *reply;

                if (register_worker (self, connection)) {
                        reply = dbus_message_new_method_return (message);
                } else {
                        reply = dbus_message_new_error (message, DBUS_ERROR_FAILED, "");
                }

                dbus_connection_send (connection, reply, NULL);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
handle_connection (DBusServer      *server,
                   DBusConnection  *new_connection,
                   void            *user_data)
{
        GdmSession *self = GDM_SESSION (user_data);
        g_debug ("GdmSession: Handing new connection");

        /* add to the list of pending connections.  We won't be able to
         * associate it with a specific worker conversation until we have
         * authenticated the connection (from the Hello handler).
         */
        self->priv->pending_connections =
                g_list_prepend (self->priv->pending_connections,
                                dbus_connection_ref (new_connection));
        dbus_connection_setup_with_g_main (new_connection, NULL);
        dbus_connection_set_exit_on_disconnect (new_connection, FALSE);

        dbus_connection_set_unix_user_function (new_connection,
                                                allow_user_function,
                                                self,
                                                NULL);
        dbus_connection_add_filter (new_connection, on_message, self, NULL);
}

static gboolean
setup_server (GdmSession *self)
{
        DBusError   error;
        gboolean    ret;
        char       *address;
        const char *auth_mechanisms[] = {"EXTERNAL", NULL};

        ret = FALSE;

        g_debug ("GdmSession: Creating D-Bus server for session");

        address = generate_address ();

        dbus_error_init (&error);
        self->priv->server = dbus_server_listen (address, &error);
        g_free (address);

        if (self->priv->server == NULL) {
                g_warning ("Cannot create D-BUS server for the session: %s", error.message);
                /* FIXME: should probably fail if we can't create the socket */
                goto out;
        }

        dbus_server_setup_with_g_main (self->priv->server, NULL);
        dbus_server_set_auth_mechanisms (self->priv->server, auth_mechanisms);
        dbus_server_set_new_connection_function (self->priv->server,
                                                 handle_connection,
                                                 self,
                                                 NULL);
        ret = TRUE;

        g_free (self->priv->server_address);
        self->priv->server_address = dbus_server_get_address (self->priv->server);

        g_debug ("GdmSession: D-Bus server listening on %s", self->priv->server_address);

 out:

        return ret;
}

static void
free_conversation (GdmSessionConversation *conversation)
{
        if (conversation->job != NULL) {
                g_warning ("Freeing conversation '%s' with active job", conversation->service_name);
        }

        g_free (conversation->service_name);
        g_free (conversation);
}

static void
gdm_session_init (GdmSession *self)
{
        self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                  GDM_TYPE_SESSION,
                                                  GdmSessionPrivate);

        g_signal_connect (self,
                          "authentication-failed",
                          G_CALLBACK (on_authentication_failed),
                          NULL);
        g_signal_connect (self,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          NULL);
        g_signal_connect (self,
                          "session-start-failed",
                          G_CALLBACK (on_session_start_failed),
                          NULL);
        g_signal_connect (self,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          NULL);

        self->priv->conversations = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           (GDestroyNotify) g_free,
                                                           (GDestroyNotify)
                                                           free_conversation);
        self->priv->environment = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         (GDestroyNotify) g_free,
                                                         (GDestroyNotify) g_free);

        setup_server (self);

}

static void
worker_started (GdmSessionWorkerJob    *job,
                GdmSessionConversation *conversation)
{
        g_debug ("GdmSession: Worker job started");
}

static void
worker_exited (GdmSessionWorkerJob    *job,
               int                     code,
               GdmSessionConversation *conversation)
{

        g_debug ("GdmSession: Worker job exited: %d", code);

        g_object_ref (conversation->job);
        if (conversation->session->priv->session_conversation == conversation) {
                emit_session_exited (GDM_SESSION (conversation->session), code);
        }

        g_hash_table_steal (conversation->session->priv->conversations, conversation->service_name);

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        emit_conversation_stopped (GDM_SESSION (conversation->session),
                                   conversation->service_name);
        g_object_unref (conversation->job);

        if (conversation->is_stopping) {
                g_object_unref (conversation->job);
                conversation->job = NULL;
        }

        free_conversation (conversation);
}

static void
worker_died (GdmSessionWorkerJob    *job,
             int                     signum,
             GdmSessionConversation *conversation)
{

        g_debug ("GdmSession: Worker job died: %d", signum);

        g_object_ref (conversation->job);
        if (conversation->session->priv->session_conversation == conversation) {
                emit_session_died (GDM_SESSION (conversation->session), signum);
        }

        g_hash_table_steal (conversation->session->priv->conversations, conversation->service_name);

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        emit_conversation_stopped (GDM_SESSION (conversation->session),
                                   conversation->service_name);
        g_object_unref (conversation->job);

        if (conversation->is_stopping) {
                g_object_unref (conversation->job);
                conversation->job = NULL;
        }

        free_conversation (conversation);
}

static GdmSessionConversation *
start_conversation (GdmSession *self,
                    const char *service_name)
{
        GdmSessionConversation *conversation;
        char                   *job_name;

        conversation = g_new0 (GdmSessionConversation, 1);
        conversation->session = self;
        conversation->service_name = g_strdup (service_name);
        conversation->worker_pid = -1;
        conversation->job = gdm_session_worker_job_new ();
        gdm_session_worker_job_set_server_address (conversation->job, self->priv->server_address);
        g_signal_connect (conversation->job,
                          "started",
                          G_CALLBACK (worker_started),
                          conversation);
        g_signal_connect (conversation->job,
                          "exited",
                          G_CALLBACK (worker_exited),
                          conversation);
        g_signal_connect (conversation->job,
                          "died",
                          G_CALLBACK (worker_died),
                          conversation);

        job_name = g_strdup_printf ("gdm-session-worker [pam/%s]", service_name);
        if (!gdm_session_worker_job_start (conversation->job, job_name)) {
                g_object_unref (conversation->job);
                g_free (conversation->service_name);
                g_free (conversation);
                g_free (job_name);
                return NULL;
        }

        g_free (job_name);

        conversation->worker_pid = gdm_session_worker_job_get_pid (conversation->job);

        return conversation;
}

static void
stop_conversation (GdmSessionConversation *conversation)
{
        GdmSession *session;

        session = conversation->session;

        if (conversation->worker_connection != NULL) {
                dbus_connection_remove_filter (conversation->worker_connection, on_message, session);

                dbus_connection_close (conversation->worker_connection);
                conversation->worker_connection = NULL;
        }

        conversation->is_stopping = TRUE;
        gdm_session_worker_job_stop (conversation->job);
}

static void
stop_conversation_now (GdmSessionConversation *conversation)
{
        GdmSession *session;

        session = conversation->session;

        if (conversation->worker_connection != NULL) {
                dbus_connection_remove_filter (conversation->worker_connection, on_message, session);

                dbus_connection_close (conversation->worker_connection);
                conversation->worker_connection = NULL;
        }

        gdm_session_worker_job_stop_now (conversation->job);
        g_object_unref (conversation->job);
        conversation->job = NULL;
}

void
gdm_session_start_conversation (GdmSession *self,
                                const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = g_hash_table_lookup (self->priv->conversations,
                                            service_name);

        if (conversation != NULL) {
                if (!conversation->is_stopping) {
                        g_warning ("GdmSession: conversation %s started more than once", service_name);
                        return;
                }
                g_debug ("GdmSession: stopping old conversation %s", service_name);
                gdm_session_worker_job_stop_now (conversation->job);
                g_object_unref (conversation->job);
                conversation->job = NULL;
        }

        g_debug ("GdmSession: starting conversation %s", service_name);

        conversation = start_conversation (self, service_name);

        g_hash_table_insert (self->priv->conversations,
                             g_strdup (service_name), conversation);
}

void
gdm_session_stop_conversation (GdmSession *self,
                               const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        g_debug ("GdmSession: stopping conversation %s", service_name);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                stop_conversation (conversation);
        }
}

static void
send_setup (GdmSession *self,
            const char *service_name)
{
        DBusMessage    *message;
        DBusMessageIter iter;
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

        if (self->priv->display_name != NULL) {
                display_name = self->priv->display_name;
        } else {
                display_name = "";
        }
        if (self->priv->display_hostname != NULL) {
                display_hostname = self->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (self->priv->display_device != NULL) {
                display_device = self->priv->display_device;
        } else {
                display_device = "";
        }
        if (self->priv->display_seat_id != NULL) {
                display_seat_id = self->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (self->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = self->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }

        g_debug ("GdmSession: Beginning setup");

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "Setup");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &service_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_device);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_seat_id);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_hostname);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_x11_authority_file);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL && ! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal", "Setup");
        }

        dbus_message_unref (message);
}

static void
send_setup_for_user (GdmSession *self,
                     const char *service_name)
{
        DBusMessage    *message;
        DBusMessageIter iter;
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        const char     *selected_user;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

        if (self->priv->display_name != NULL) {
                display_name = self->priv->display_name;
        } else {
                display_name = "";
        }
        if (self->priv->display_hostname != NULL) {
                display_hostname = self->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (self->priv->display_device != NULL) {
                display_device = self->priv->display_device;
        } else {
                display_device = "";
        }
        if (self->priv->display_seat_id != NULL) {
                display_seat_id = self->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (self->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = self->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }
        if (self->priv->selected_user != NULL) {
                selected_user = self->priv->selected_user;
        } else {
                selected_user = "";
        }

        g_debug ("GdmSession: Beginning setup for user %s", self->priv->selected_user);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "SetupForUser");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &service_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_device);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_seat_id);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_hostname);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_x11_authority_file);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &selected_user);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL && ! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal", "SetupForUser");
        }

        dbus_message_unref (message);
}

static void
send_setup_for_program (GdmSession *self,
                        const char *service_name,
                        const char *log_file)
{
        DBusMessage    *message;
        DBusMessageIter iter;
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

        if (self->priv->display_name != NULL) {
                display_name = self->priv->display_name;
        } else {
                display_name = "";
        }
        if (self->priv->display_hostname != NULL) {
                display_hostname = self->priv->display_hostname;
        } else {
                display_hostname = "";
        }
        if (self->priv->display_device != NULL) {
                display_device = self->priv->display_device;
        } else {
                display_device = "";
        }
        if (self->priv->display_seat_id != NULL) {
                display_seat_id = self->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (self->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = self->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }

        g_debug ("GdmSession: Beginning setup for session for program with log '%s'", log_file);

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "SetupForProgram");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &service_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_name);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_device);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_seat_id);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_hostname);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &display_x11_authority_file);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &log_file);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL && ! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal", "SetupForProgram");
        }

        dbus_message_unref (message);
}

void
gdm_session_setup (GdmSession *self,
                   const char *service_name)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        send_setup (self, service_name);
        gdm_session_defaults_changed (self);
}

void
gdm_session_setup_for_user (GdmSession *self,
                            const char *service_name,
                            const char *username)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (username != NULL);

        gdm_session_select_user (self, username);

        send_setup_for_user (self, service_name);
        gdm_session_defaults_changed (self);
}

void
gdm_session_setup_for_program (GdmSession *self,
                               const char *service_name,
                               const char *log_file)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        send_setup_for_program (self, service_name, log_file);
}

void
gdm_session_authenticate (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                send_dbus_void_signal (conversation, "Authenticate");
        }
}

void
gdm_session_authorize (GdmSession *self,
                       const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                send_dbus_void_signal (conversation, "Authorize");
        }
}

void
gdm_session_accredit (GdmSession *self,
                      const char *service_name,
                      int         cred_flag)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation == NULL) {
                return;
        }

        switch (cred_flag) {
        case GDM_SESSION_CRED_ESTABLISH:
                send_dbus_void_signal (conversation, "EstablishCredentials");
                break;
        case GDM_SESSION_CRED_REFRESH:
                send_dbus_void_signal (conversation, "RefreshCredentials");
                break;
        default:
                g_assert_not_reached ();
        }
}

static void
send_environment_variable (const char             *key,
                           const char             *value,
                           GdmSessionConversation *conversation)
{
        DBusMessage    *message;
        DBusMessageIter iter;

        message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
                                           GDM_SESSION_DBUS_INTERFACE,
                                           "SetEnvironmentVariable");

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &value);

        if (! send_dbus_message (conversation, message)) {
                g_debug ("GdmSession: Could not send %s signal", "SetEnvironmentVariable");
        }

        dbus_message_unref (message);
}

static void
send_environment (GdmSession       *self,
                  GdmSessionConversation *conversation)
{

        g_hash_table_foreach (self->priv->environment,
                              (GHFunc) send_environment_variable,
                              conversation);
}

static const char *
get_language_name (GdmSession *self)
{
        if (self->priv->selected_language != NULL) {
                return self->priv->selected_language;
        }

        return get_default_language_name (self);
}

static const char *
get_session_name (GdmSession *self)
{
        /* FIXME: test the session names before we use them? */

        if (self->priv->selected_session != NULL) {
                return self->priv->selected_session;
        }

        return get_default_session_name (self);
}

static char *
get_session_command (GdmSession *self)
{
        gboolean    res;
        char       *command;
        const char *session_name;

        session_name = get_session_name (self);

        command = NULL;
        res = get_session_command_for_name (session_name, &command);
        if (! res) {
                g_critical ("Cannot find a command for specified session: %s", session_name);
                exit (1);
        }

        return command;
}

void
gdm_session_set_environment_variable (GdmSession *self,
                                      const char *key,
                                      const char *value)
{

        g_return_if_fail (key != NULL);
        g_return_if_fail (value != NULL);

        g_hash_table_replace (self->priv->environment,
                              g_strdup (key),
                              g_strdup (value));
}

static void
setup_session_environment (GdmSession *self)
{
        const char *locale;

        gdm_session_set_environment_variable (self,
                                              "GDMSESSION",
                                              get_session_name (self));
        gdm_session_set_environment_variable (self,
                                              "DESKTOP_SESSION",
                                              get_session_name (self));

        locale = get_language_name (self);

        if (locale != NULL && locale[0] != '\0') {
                gdm_session_set_environment_variable (self,
                                                      "LANG",
                                                      locale);
                gdm_session_set_environment_variable (self,
                                                      "GDM_LANG",
                                                      locale);
        }

        gdm_session_set_environment_variable (self,
                                              "DISPLAY",
                                              self->priv->display_name);

        if (self->priv->user_x11_authority_file != NULL) {
                gdm_session_set_environment_variable (self,
                                                      "XAUTHORITY",
                                                      self->priv->user_x11_authority_file);
        }

        if (g_getenv ("WINDOWPATH") != NULL) {
                gdm_session_set_environment_variable (self,
                                                      "WINDOWPATH",
                                                      g_getenv ("WINDOWPATH"));
        }


        /* FIXME: We do this here and in the session worker.  We should consolidate
         * somehow.
         */
        gdm_session_set_environment_variable (self,
                                              "PATH",
                                              strcmp (BINDIR, "/usr/bin") == 0?
                                              GDM_SESSION_DEFAULT_PATH :
                                              BINDIR ":" GDM_SESSION_DEFAULT_PATH);

}

void
gdm_session_open_session (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);

        send_dbus_string_signal (conversation, "OpenSession", service_name);
}

static void
stop_all_other_conversations (GdmSession              *self,
                              GdmSessionConversation  *conversation_to_keep,
                              gboolean                 now)
{
        GHashTableIter iter;
        gpointer key, value;

        if (self->priv->conversations == NULL) {
                return;
        }

        if (conversation_to_keep == NULL) {
                g_debug ("GdmSession: Stopping all conversations");
        } else {
                g_debug ("GdmSession: Stopping all conversations "
                         "except for %s", conversation_to_keep->service_name);
        }

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                if (conversation == conversation_to_keep) {
                        if (now) {
                                g_hash_table_iter_steal (&iter);
                                g_free (key);
                        }
                } else {
                        if (now) {
                                stop_conversation_now (conversation);
                        } else {
                                stop_conversation (conversation);
                        }
                }
        }

        if (now) {
                g_hash_table_remove_all (self->priv->conversations);

                if (conversation_to_keep != NULL) {
                        g_hash_table_insert (self->priv->conversations,
                                             g_strdup (conversation_to_keep->service_name),
                                             conversation_to_keep);
                }

                if (self->priv->session_conversation != conversation_to_keep) {
                        self->priv->session_conversation = NULL;
                }
        }

}

void
gdm_session_start_session (GdmSession *self,
                           const char *service_name)
{
        GdmSessionConversation *conversation;
        char             *command;
        char             *program;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (self->priv->session_conversation == NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation == NULL) {
                g_warning ("GdmSession: Tried to start session of "
                           "nonexistent conversation %s", service_name);
                return;
        }

        stop_all_other_conversations (self, conversation, FALSE);

        if (self->priv->selected_program == NULL) {
                command = get_session_command (self);

                if (gdm_session_bypasses_xsession (self)) {
                        program = g_strdup (command);
                } else {
                        program = g_strdup_printf (GDMCONFDIR "/Xsession \"%s\"", command);
                }

                g_free (command);
        } else {
                program = g_strdup (self->priv->selected_program);
        }

        setup_session_environment (self);
        send_environment (self, conversation);

        send_dbus_string_signal (conversation, "StartProgram", program);
        g_free (program);
}

static void
stop_all_conversations (GdmSession *self)
{
        stop_all_other_conversations (self, NULL, TRUE);
}

void
gdm_session_close (GdmSession *self)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        g_debug ("GdmSession: Closing session");

        if (self->priv->session_conversation != NULL) {
                gdm_session_record_logout (self->priv->session_pid,
                                           self->priv->selected_user,
                                           self->priv->display_hostname,
                                           self->priv->display_name,
                                           self->priv->display_device);
        }

        stop_all_conversations (self);

        g_list_foreach (self->priv->pending_connections,
                        (GFunc) dbus_connection_unref, NULL);
        g_list_free (self->priv->pending_connections);
        self->priv->pending_connections = NULL;

        g_free (self->priv->selected_user);
        self->priv->selected_user = NULL;

        g_free (self->priv->selected_session);
        self->priv->selected_session = NULL;

        g_free (self->priv->saved_session);
        self->priv->saved_session = NULL;

        g_free (self->priv->selected_language);
        self->priv->selected_language = NULL;

        g_free (self->priv->saved_language);
        self->priv->saved_language = NULL;

        g_free (self->priv->user_x11_authority_file);
        self->priv->user_x11_authority_file = NULL;

        g_hash_table_remove_all (self->priv->environment);

        self->priv->session_pid = -1;
        self->priv->session_conversation = NULL;
}

void
gdm_session_answer_query (GdmSession *self,
                          const char *service_name,
                          const char *text)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);

        answer_pending_query (conversation, text);
}

void
gdm_session_cancel  (GdmSession *self)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        stop_all_conversations (self);
}

char *
gdm_session_get_username (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_strdup (self->priv->selected_user);
}

char *
gdm_session_get_display_device (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_strdup (self->priv->display_device);
}

char *
gdm_session_get_display_seat_id (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_strdup (self->priv->display_seat_id);
}

gboolean
gdm_session_bypasses_xsession (GdmSession *self)
{
        GError     *error;
        GKeyFile   *key_file;
        gboolean    res;
        gboolean    bypasses_xsession = FALSE;
        char       *filename;

        g_return_val_if_fail (self != NULL, FALSE);
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        filename = g_strdup_printf ("%s.desktop", get_session_name (self));

        key_file = g_key_file_new ();
        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         filename,
                                         get_system_session_dirs (),
                                         NULL,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSession: File '%s' not found: %s", filename, error->message);
                goto out;
        }

        error = NULL;
        res = g_key_file_has_key (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GDM-BypassXsession", NULL);
        if (!res) {
                goto out;
        } else {
                bypasses_xsession = g_key_file_get_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GDM-BypassXsession", &error);
                if (error) {
                        bypasses_xsession = FALSE;
                        g_error_free (error);
                        goto out;
                }
                if (bypasses_xsession) {
                        g_debug ("GdmSession: Session %s bypasses Xsession wrapper script", filename);
                }
        }

out:
        g_free (filename);
        return bypasses_xsession;
}

void
gdm_session_select_program (GdmSession *self,
                            const char *text)
{

        g_free (self->priv->selected_program);

        self->priv->selected_program = g_strdup (text);
}

void
gdm_session_select_session_type (GdmSession *self,
                                 const char *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                send_dbus_string_signal (conversation, "SetSessionType",
                                         text);
        }
}

void
gdm_session_select_session (GdmSession *self,
                            const char *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_free (self->priv->selected_session);

        if (strcmp (text, "__previous") == 0) {
                self->priv->selected_session = NULL;
        } else {
                self->priv->selected_session = g_strdup (text);
        }

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                send_dbus_string_signal (conversation, "SetSessionName",
                                         get_session_name (self));
        }
}

void
gdm_session_select_language (GdmSession *self,
                             const char *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_free (self->priv->selected_language);

        if (strcmp (text, "__previous") == 0) {
                self->priv->selected_language = NULL;
        } else {
                self->priv->selected_language = g_strdup (text);
        }

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                send_dbus_string_signal (conversation, "SetLanguageName",
                                         get_language_name (self));
        }
}

/* At some point we may want to read these right from
 * the slave but for now I don't want the dependency */
static void
set_display_name (GdmSession *self,
                  const char *name)
{
        g_free (self->priv->display_name);
        self->priv->display_name = g_strdup (name);
}

static void
set_display_hostname (GdmSession *self,
                      const char *name)
{
        g_free (self->priv->display_hostname);
        self->priv->display_hostname = g_strdup (name);
}

static void
set_display_device (GdmSession *self,
                    const char *name)
{
        g_debug ("GdmSession: Setting display device: %s", name);
        g_free (self->priv->display_device);
        self->priv->display_device = g_strdup (name);
}

static void
set_display_seat_id (GdmSession *self,
                     const char *name)
{
        g_free (self->priv->display_seat_id);
        self->priv->display_seat_id = g_strdup (name);
}

static void
set_user_x11_authority_file (GdmSession *self,
                             const char *name)
{
        g_free (self->priv->user_x11_authority_file);
        self->priv->user_x11_authority_file = g_strdup (name);
}

static void
set_display_x11_authority_file (GdmSession *self,
                                const char *name)
{
        g_free (self->priv->display_x11_authority_file);
        self->priv->display_x11_authority_file = g_strdup (name);
}

static void
set_display_is_local (GdmSession *self,
                      gboolean    is_local)
{
        self->priv->display_is_local = is_local;
}

static void
gdm_session_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
        GdmSession *self;

        self = GDM_SESSION (object);

        switch (prop_id) {
        case PROP_DISPLAY_NAME:
                set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_DEVICE:
                set_display_device (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_SEAT_ID:
                set_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                set_user_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                set_display_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                set_display_is_local (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GdmSession *self;

        self = GDM_SESSION (object);

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
        case PROP_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->priv->display_seat_id);
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->user_x11_authority_file);
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->display_x11_authority_file);
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
gdm_session_dispose (GObject *object)
{
        GdmSession *self;

        self = GDM_SESSION (object);

        g_debug ("GdmSession: Disposing session");

        gdm_session_close (self);

        g_free (self->priv->display_id);
        self->priv->display_id = NULL;

        g_free (self->priv->display_name);
        self->priv->display_name = NULL;

        g_free (self->priv->display_hostname);
        self->priv->display_hostname = NULL;

        g_free (self->priv->display_device);
        self->priv->display_device = NULL;

        g_free (self->priv->display_seat_id);
        self->priv->display_seat_id = NULL;

        g_free (self->priv->display_x11_authority_file);
        self->priv->display_x11_authority_file = NULL;

        g_free (self->priv->server_address);
        self->priv->server_address = NULL;

        if (self->priv->server != NULL) {
                dbus_server_disconnect (self->priv->server);
                dbus_server_unref (self->priv->server);
                self->priv->server = NULL;
        }

        if (self->priv->environment != NULL) {
                g_hash_table_destroy (self->priv->environment);
                self->priv->environment = NULL;
        }

        G_OBJECT_CLASS (gdm_session_parent_class)->dispose (object);
}

static void
gdm_session_finalize (GObject *object)
{
        GdmSession   *self;
        GObjectClass *parent_class;

        self = GDM_SESSION (object);

        g_free (self->priv->selected_user);
        g_free (self->priv->selected_session);
        g_free (self->priv->saved_session);
        g_free (self->priv->selected_language);
        g_free (self->priv->saved_language);

        g_free (self->priv->fallback_session_name);

        parent_class = G_OBJECT_CLASS (gdm_session_parent_class);

        if (parent_class->finalize != NULL)
                parent_class->finalize (object);
}

static GObject *
gdm_session_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GdmSession *self;

        self = GDM_SESSION (G_OBJECT_CLASS (gdm_session_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));
        return G_OBJECT (self);
}

static void
gdm_session_class_init (GdmSessionClass *session_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (session_class);

        object_class->get_property = gdm_session_get_property;
        object_class->set_property = gdm_session_set_property;
        object_class->constructor = gdm_session_constructor;
        object_class->dispose = gdm_session_dispose;
        object_class->finalize = gdm_session_finalize;

        g_type_class_add_private (session_class, sizeof (GdmSessionPrivate));

        signals [CONVERSATION_STARTED] =
                g_signal_new ("conversation-started",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, conversation_started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CONVERSATION_STOPPED] =
                g_signal_new ("conversation-stopped",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, conversation_stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SERVICE_UNAVAILABLE] =
                g_signal_new ("service-unavailable",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, service_unavailable),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SETUP_COMPLETE] =
                g_signal_new ("setup-complete",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, setup_complete),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SETUP_FAILED] =
                g_signal_new ("setup-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, setup_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [RESET_COMPLETE] =
                g_signal_new ("reset-complete",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, reset_complete),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [RESET_FAILED] =
                g_signal_new ("reset-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, reset_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [AUTHENTICATED] =
                g_signal_new ("authenticated",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, authenticated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [AUTHENTICATION_FAILED] =
                g_signal_new ("authentication-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, authentication_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [AUTHORIZED] =
                g_signal_new ("authorized",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, authorized),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [AUTHORIZATION_FAILED] =
                g_signal_new ("authorization-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, authorization_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [ACCREDITED] =
                g_signal_new ("accredited",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, accredited),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [ACCREDITATION_FAILED] =
                g_signal_new ("accreditation-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, accreditation_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);

         signals [INFO_QUERY] =
                g_signal_new ("info-query",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SECRET_INFO_QUERY] =
                g_signal_new ("secret-info-query",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, secret_info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [INFO] =
                g_signal_new ("info",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, info),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [PROBLEM] =
                g_signal_new ("problem",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, problem),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_OPENED] =
                g_signal_new ("session-opened",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_opened),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SESSION_OPEN_FAILED] =
                g_signal_new ("session-open-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_open_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_STARTED] =
                g_signal_new ("session-started",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_INT);
        signals [SESSION_START_FAILED] =
                g_signal_new ("session-start-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_start_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_EXITED] =
                g_signal_new ("session-exited",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_exited),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [SESSION_DIED] =
                g_signal_new ("session-died",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [SELECTED_USER_CHANGED] =
                g_signal_new ("selected-user-changed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, selected_user_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [DEFAULT_LANGUAGE_NAME_CHANGED] =
                g_signal_new ("default-language-name-changed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, default_language_name_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [DEFAULT_SESSION_NAME_CHANGED] =
                g_signal_new ("default-session-name-changed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, default_session_name_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

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
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("display-x11-authority-file",
                                                              "display x11 authority file",
                                                              "display x11 authority file",
                                                              NULL,
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

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "display seat id",
                                                              "display seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

GdmSession *
gdm_session_new (const char *display_name,
                 const char *display_hostname,
                 const char *display_device,
                 const char *display_seat_id,
                 const char *display_x11_authority_file,
                 gboolean    display_is_local)
{
        GdmSession *self;

        self = g_object_new (GDM_TYPE_SESSION,
                             "display-name", display_name,
                             "display-hostname", display_hostname,
                             "display-device", display_device,
                             "display-seat-id", display_seat_id,
                             "display-x11-authority-file", display_x11_authority_file,
                             "display-is-local", display_is_local,
                              NULL);

        return self;
}
