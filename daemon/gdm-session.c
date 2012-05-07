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
#include <gio/gio.h>

#ifdef HAVE_LIBXKLAVIER
#include <libxklavier/xklavier.h>
#include <X11/Xlib.h> /* for Display */
#endif

#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-dbus-util.h"

#include "gdm-session.h"
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
        GDBusConnection       *worker_connection;
        GDBusMethodInvocation *pending_invocation;
        GdmDBusSession        *worker_skeleton;
        char                  *session_id;
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

        GDBusServer         *server;
        GHashTable          *environment;
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

static gboolean
gdm_session_handle_service_unavailable (GdmDBusSession        *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        const char            *message,
                                        GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'service-unavailable' signal");
        g_signal_emit (self, signals[SERVICE_UNAVAILABLE], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_setup_complete (GdmDBusSession        *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   const char            *service_name,
                                   GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'setup-complete' signal");
        g_signal_emit (self, signals[SETUP_COMPLETE], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_setup_failed (GdmDBusSession        *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 const char            *service_name,
                                 const char            *message,
                                 GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'setup-failed' signal");
        g_signal_emit (self, signals[SETUP_FAILED], 0, service_name, message);

        return TRUE;
}


static gboolean
gdm_session_handle_authenticated (GdmDBusSession        *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  const char            *service_name,
                                  GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'authenticated' signal");
        g_signal_emit (self, signals[AUTHENTICATED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_authentication_failed (GdmDBusSession        *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          const char            *service_name,
                                          const char            *message,
                                          GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'authentication-failed' signal");
        g_signal_emit (self, signals[AUTHENTICATION_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_handle_authorized (GdmDBusSession        *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'authorized' signal");
        g_signal_emit (self, signals[AUTHORIZED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_authorization_failed (GdmDBusSession        *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         const char            *message,
                                         GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'authorization-failed' signal");
        g_signal_emit (self, signals[AUTHORIZATION_FAILED], 0, service_name, message);
        return TRUE;
}

static gboolean
gdm_session_handle_accredited (GdmDBusSession        *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'accredited' signal");
        g_signal_emit (self, signals[ACCREDITED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_accreditation_failed (GdmDBusSession        *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         const char            *message,
                                         GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'accreditation-failed' signal");
        g_signal_emit (self, signals[ACCREDITATION_FAILED], 0, service_name, message);
        return TRUE;
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
        g_signal_emit (self,
                       signals[DEFAULT_LANGUAGE_NAME_CHANGED],
                       0,
                       get_default_language_name (self));

        g_signal_emit (self,
                       signals[DEFAULT_SESSION_NAME_CHANGED],
                       0,
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

static gboolean
gdm_session_handle_username_changed (GdmDBusSession        *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     const char            *service_name,
                                     const char            *text,
                                     GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: changing username from '%s' to '%s'",
                 self->priv->selected_user != NULL ? self->priv->selected_user : "<unset>",
                 (strlen (text)) ? text : "<unset>");

        gdm_session_select_user (self, (strlen (text) > 0) ? g_strdup (text) : NULL);
        g_signal_emit (self, signals[SELECTED_USER_CHANGED], 0, self->priv->selected_user);

        gdm_session_defaults_changed (self);

        return TRUE;
}

static void
cancel_pending_query (GdmSessionConversation *conversation)
{
        if (conversation->pending_invocation == NULL) {
                return;
        }

        g_debug ("GdmSession: Cancelling pending query");

        g_dbus_method_invocation_return_dbus_error (conversation->pending_invocation,
                                                    GDM_SESSION_DBUS_ERROR_CANCEL,
                                                    "Operation cancelled");
        conversation->pending_invocation = NULL;
}

static void
answer_pending_query (GdmSessionConversation *conversation,
                      const char             *answer)
{
        g_dbus_method_invocation_return_value (conversation->pending_invocation,
                                               g_variant_new ("(s)", answer));
        conversation->pending_invocation = NULL;
}

static void
set_pending_query (GdmSessionConversation *conversation,
                   GDBusMethodInvocation  *message)
{
        g_assert (conversation->pending_invocation == NULL);

        conversation->pending_invocation = g_object_ref (message);
}

static gboolean
gdm_session_handle_info_query (GdmDBusSession        *skeleton,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               const char            *text,
                               GdmSession            *self)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        set_pending_query (conversation, invocation);

        g_debug ("GdmSession: Emitting 'info-query' signal");
        g_signal_emit (self, signals[INFO_QUERY], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_handle_secret_info_query (GdmDBusSession        *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      const char            *text,
                                      GdmSession            *self)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        set_pending_query (conversation, invocation);

        g_debug ("GdmSession: Emitting 'secret-info-query' signal");
        g_signal_emit (self, signals[SECRET_INFO_QUERY], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_handle_info (GdmDBusSession        *skeleton,
                         GDBusMethodInvocation *invocation,
                         const char            *service_name,
                         const char            *text,
                         GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'info' signal");
        g_signal_emit (self, signals[INFO], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_handle_cancel_pending_query (GdmDBusSession        *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         GdmSession            *self)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (self, service_name);
        cancel_pending_query (conversation);

        g_dbus_method_invocation_return_value (invocation, NULL);

        return TRUE;
}

static gboolean
gdm_session_handle_problem (GdmDBusSession        *skeleton,
                            GDBusMethodInvocation *invocation,
                            const char            *service_name,
                            const char            *text,
                            GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'problem' signal");
        g_signal_emit (self, signals[PROBLEM], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_handle_opened (GdmDBusSession        *skeleton,
                           GDBusMethodInvocation *invocation,
                           const char            *service_name,
                           GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'session-opened' signal");
        g_signal_emit (self, signals[SESSION_OPENED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_handle_open_failed (GdmDBusSession        *skeleton,
                                GDBusMethodInvocation *invocation,
                                const char            *service_name,
                                const char            *message,
                                GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'session-open-failed' signal");
        g_signal_emit (self, signals[SESSION_OPEN_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_handle_session_started (GdmDBusSession        *skeleton,
                                    GDBusMethodInvocation *invocation,
                                    const char            *service_name,
                                    const char            *session_id,
                                    int                    pid,
                                    GdmSession            *self)
{
        GdmSessionConversation *conversation;

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation = find_conversation_by_name (self, service_name);

        self->priv->session_pid = pid;
        self->priv->session_conversation = conversation;

        g_clear_pointer (&conversation->session_id,
                         (GDestroyNotify) g_free);

        if (session_id != NULL && session_id[0] != '\0') {
                conversation->session_id = g_strdup (session_id);
        }

        g_debug ("GdmSession: Emitting 'session-started' signal with pid '%d'", pid);
        g_signal_emit (self, signals[SESSION_STARTED], 0, service_name, pid);

        return TRUE;
}

static gboolean
gdm_session_handle_session_start_failed (GdmDBusSession        *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         const char            *message,
                                         GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSession: Emitting 'session-start-failed' signal");
        g_signal_emit (self, signals[SESSION_START_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_handle_session_exited_or_died (GdmDBusSession        *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           const char            *service_name,
                                           int                    status,
                                           GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        self->priv->session_conversation = NULL;

        if (WIFEXITED (status)) {
                g_debug ("GdmSession: Emitting 'session-exited' signal with exit code '%d'",
                  WEXITSTATUS (status));
                g_signal_emit (self, signals[SESSION_EXITED], 0, WEXITSTATUS (status));
        } else if (WIFSIGNALED (status)) {
                g_debug ("GdmSession: Emitting 'session-died' signal with signal number '%d'",
                  WTERMSIG (status));
                g_signal_emit (self, signals[SESSION_DIED], 0, WTERMSIG (status));
        }

        return TRUE;
}

static gboolean
gdm_session_handle_saved_language_name_read (GdmDBusSession        *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             const char            *language_name,
                                             GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        if (strcmp (language_name,
                    get_default_language_name (self)) != 0) {
                g_free (self->priv->saved_language);
                self->priv->saved_language = g_strdup (language_name);

                g_signal_emit (self, signals[DEFAULT_LANGUAGE_NAME_CHANGED], 0, language_name);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_saved_session_name_read (GdmDBusSession        *skeleton,
                                            GDBusMethodInvocation *invocation,
                                            const char            *session_name,
                                            GdmSession            *self)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

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

                g_signal_emit (self, signals[DEFAULT_SESSION_NAME_CHANGED], 0, session_name);
        }
 out:
        return TRUE;
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

static gboolean
allow_user_function (GDBusAuthObserver *observer,
                     GIOStream         *stream,
                     GCredentials      *credentials)
{
        if (g_credentials_get_unix_user (credentials, NULL) == 0)
                return TRUE;

        g_debug ("GdmSession: User not allowed");

        return FALSE;
}

static GPid
credentials_get_unix_pid (GCredentials *credentials)
{
        GPid pid = 0;
        gpointer native_credentials = NULL;

#ifdef __linux__
        native_credentials = g_credentials_get_native (credentials, G_CREDENTIALS_TYPE_LINUX_UCRED);
        pid = (GPid) ((struct ucred *) native_credentials)->pid;
#elif defined (__FreeBSD__)
        native_credentials = g_credentials_get_native (credentials, G_CREDENTIALS_TYPE_OPENBSD_SOCKPEERCRED);
        pid = (GPid) ((struct cmsgcred *) native_credentials)->cmcred_pid;
#elif defined (__OpenBSD__)
        native_credentials = g_credentials_get_native (credentials, G_CREDENTIALS_TYPE_OPENBSD_SOCKPEERCRED);
        pid = (GPid) ((struct sockpeercred *) native_credentials)->pid;
#else
#error "platform not supported, need mechanism to detect pid of connected process"
#endif

        return pid;
}

static gboolean
register_worker (GdmDBusSession        *skeleton,
                 GDBusMethodInvocation *invocation,
                 GdmSession            *self)
{
        GdmSessionConversation *conversation;
        GDBusConnection *connection;
        GList *connection_node;
        GCredentials *credentials;
        GPid pid;

        g_debug ("GdmSession: Authenticating new connection");

        connection = g_dbus_method_invocation_get_connection (invocation);
        connection_node = g_list_find (self->priv->pending_connections, connection);

        if (connection_node == NULL) {
                g_debug ("GdmSession: Ignoring connection that we aren't tracking");
                return FALSE;
        }

        self->priv->pending_connections =
                g_list_delete_link (self->priv->pending_connections,
                                    connection_node);

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid = credentials_get_unix_pid (credentials);

        conversation = find_conversation_by_pid (self, (GPid) pid);

        if (conversation == NULL) {
                g_warning ("GdmSession: New worker connection is from unknown source");

                g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Connection is not from a known conversation");
                g_dbus_connection_close_sync (connection, NULL, NULL);
                return TRUE;
        }

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation->worker_connection = g_object_ref (connection);
        conversation->worker_skeleton = g_object_ref (skeleton);
        g_debug ("GdmSession: worker connection is %p", connection);

        g_debug ("GdmSession: Emitting conversation-started signal");
        g_signal_emit (self, signals[CONVERSATION_STARTED], 0, conversation->service_name);

        g_debug ("GdmSession: Conversation started");

        return TRUE;
}

static gboolean
handle_connection (GDBusServer      *server,
                   GDBusConnection  *connection,
                   GdmSession       *self)
{
        GdmDBusSession *skeleton;

        g_debug ("GdmSession: Handing new connection");

        /* add to the list of pending connections.  We won't be able to
         * associate it with a specific worker conversation until we have
         * authenticated the connection (from the Hello handler).
         */
        self->priv->pending_connections =
                g_list_prepend (self->priv->pending_connections,
                                g_object_ref (connection));

        skeleton = GDM_DBUS_SESSION (gdm_dbus_session_skeleton_new ());
        g_signal_connect (skeleton,
                          "handle-hello",
                          G_CALLBACK (register_worker),
                          self);
        g_signal_connect (skeleton,
                          "handle-info-query",
                          G_CALLBACK (gdm_session_handle_info_query),
                          self);
        g_signal_connect (skeleton,
                          "handle-secret-info-query",
                          G_CALLBACK (gdm_session_handle_secret_info_query),
                          self);
        g_signal_connect (skeleton,
                          "handle-info",
                          G_CALLBACK (gdm_session_handle_info),
                          self);
        g_signal_connect (skeleton,
                          "handle-problem",
                          G_CALLBACK (gdm_session_handle_problem),
                          self);
        g_signal_connect (skeleton,
                          "handle-cancel-pending-query",
                          G_CALLBACK (gdm_session_handle_cancel_pending_query),
                          self);
        g_signal_connect (skeleton,
                          "handle-service-unavailable",
                          G_CALLBACK (gdm_session_handle_service_unavailable),
                          self);
        g_signal_connect (skeleton,
                          "handle-setup-complete",
                          G_CALLBACK (gdm_session_handle_setup_complete),
                          self);
        g_signal_connect (skeleton,
                          "handle-setup-failed",
                          G_CALLBACK (gdm_session_handle_setup_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-authenticated",
                          G_CALLBACK (gdm_session_handle_authenticated),
                          self);
        g_signal_connect (skeleton,
                          "handle-authentication-failed",
                          G_CALLBACK (gdm_session_handle_authentication_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-authorized",
                          G_CALLBACK (gdm_session_handle_authorized),
                          self);
        g_signal_connect (skeleton,
                          "handle-authorization-failed",
                          G_CALLBACK (gdm_session_handle_authorization_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-accredited",
                          G_CALLBACK (gdm_session_handle_accredited),
                          self);
        g_signal_connect (skeleton,
                          "handle-accreditation-failed",
                          G_CALLBACK (gdm_session_handle_accreditation_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-username-changed",
                          G_CALLBACK (gdm_session_handle_username_changed),
                          self);
        g_signal_connect (skeleton,
                          "handle-opened",
                          G_CALLBACK (gdm_session_handle_opened),
                          self);
        g_signal_connect (skeleton,
                          "handle-open-failed",
                          G_CALLBACK (gdm_session_handle_open_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-session-started",
                          G_CALLBACK (gdm_session_handle_session_started),
                          self);
        g_signal_connect (skeleton,
                          "handle-session-start-failed",
                          G_CALLBACK (gdm_session_handle_session_start_failed),
                          self);
        g_signal_connect (skeleton,
                          "handle-session-exited",
                          G_CALLBACK (gdm_session_handle_session_exited_or_died),
                          self);
        g_signal_connect (skeleton,
                          "handle-saved-language-name-read",
                          G_CALLBACK (gdm_session_handle_saved_language_name_read),
                          self);
        g_signal_connect (skeleton,
                          "handle-saved-session-name-read",
                          G_CALLBACK (gdm_session_handle_saved_session_name_read),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          connection,
                                          "/org/gnome/DisplayManager/Session",
                                          NULL);

        return TRUE;
}

static void
setup_server (GdmSession *self)
{
        GDBusAuthObserver *observer;
        GDBusServer *server;
        GError *error = NULL;

        g_debug ("GdmSession: Creating D-Bus server for session");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect (observer,
                          "authorize-authenticated-peer",
                          G_CALLBACK (allow_user_function),
                          NULL);

        server = gdm_dbus_setup_private_server (observer, &error);
        g_object_unref (observer);

        if (server == NULL) {
                g_warning ("Cannot create D-BUS server for the session: %s", error->message);
                /* FIXME: should probably fail if we can't create the socket */
                return;
        }

        g_signal_connect (server,
                          "new-connection",
                          G_CALLBACK (handle_connection),
                          self);
        self->priv->server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSession: D-Bus server listening on %s",
        g_dbus_server_get_client_address (self->priv->server));
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
        GdmSession *self = conversation->session;

        g_debug ("GdmSession: Worker job exited: %d", code);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_EXITED], 0, code);
        }

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
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
        GdmSession *self = conversation->session;

        g_debug ("GdmSession: Worker job died: %d", signum);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_DIED], 0, signum);
        }

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
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
        gdm_session_worker_job_set_server_address (conversation->job,
                                                   g_dbus_server_get_client_address (self->priv->server));
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
        if (conversation->worker_connection != NULL) {
                g_dbus_connection_close_sync (conversation->worker_connection, NULL, NULL);
                g_clear_object (&conversation->worker_connection);
        }

        conversation->is_stopping = TRUE;
        gdm_session_worker_job_stop (conversation->job);
}

static void
stop_conversation_now (GdmSessionConversation *conversation)
{
        g_clear_object (&conversation->worker_skeleton);

        if (conversation->worker_connection != NULL) {
                g_dbus_connection_close_sync (conversation->worker_connection, NULL, NULL);
                g_clear_object (&conversation->worker_connection);
        }

        gdm_session_worker_job_stop_now (conversation->job);
        g_clear_object (&conversation->job);
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

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_session_emit_setup (conversation->worker_skeleton,
                                             service_name,
                                             display_name,
                                             display_x11_authority_file,
                                             display_device,
                                             display_seat_id,
                                             display_hostname);
        }
}

static void
send_setup_for_user (GdmSession *self,
                     const char *service_name)
{
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        const char     *selected_user;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);

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

        if (conversation != NULL) {
                gdm_dbus_session_emit_setup_for_user (conversation->worker_skeleton,
                                                      service_name,
                                                      selected_user,
                                                      display_name,
                                                      display_x11_authority_file,
                                                      display_device,
                                                      display_seat_id,
                                                      display_hostname);
        }
}

static void
send_setup_for_program (GdmSession *self,
                        const char *service_name,
                        const char *log_file)
{
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

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_session_emit_setup_for_program (conversation->worker_skeleton,
                                                         service_name,
                                                         display_name,
                                                         display_x11_authority_file,
                                                         display_device,
                                                         display_seat_id,
                                                         display_hostname,
                                                         log_file);
        }
}

void
gdm_session_setup (GdmSession *self,
                   const char *service_name)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        send_setup (self, service_name);
        gdm_session_defaults_changed (self);
}

typedef struct {
        GdmSession *instance;
        char       *service_name;
} SetupForUserClosure;

static gboolean
emit_setup_complete (gpointer data)
{
        SetupForUserClosure *closure = data;

        g_signal_emit (closure->instance, signals[SETUP_COMPLETE], 0, closure->service_name);

        g_free (closure->service_name);
        g_object_unref (closure->instance);

        g_slice_free (SetupForUserClosure, data);

        return G_SOURCE_REMOVE;
}

void
gdm_session_setup_for_user (GdmSession *self,
                            const char *service_name,
                            const char *username)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (username != NULL);

        if (self->priv->session_conversation != NULL &&
            g_strcmp0 (self->priv->session_conversation->service_name, service_name) == 0) {
                SetupForUserClosure *closure;

                g_warn_if_fail (g_strcmp0 (self->priv->selected_user, username) == 0);

                closure = g_slice_new (SetupForUserClosure);
                closure->instance = g_object_ref (self);
                closure->service_name = g_strdup (service_name);

                g_idle_add (emit_setup_complete, closure);
        } else {
                gdm_session_select_user (self, username);

                send_setup_for_user (self, service_name);
                gdm_session_defaults_changed (self);
        }
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
                gdm_dbus_session_emit_authenticate (conversation->worker_skeleton,
                                                    service_name);
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
                gdm_dbus_session_emit_authorize (conversation->worker_skeleton,
                                                 service_name);
        }
}

void
gdm_session_accredit (GdmSession *self,
                      const char *service_name,
                      gboolean    refresh)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation == NULL) {
                return;
        }

        if (refresh) {
                /* FIXME: need to support refresh
                 */
                gdm_dbus_session_emit_establish_credentials (conversation->worker_skeleton,
                                                             service_name);
        } else {
                gdm_dbus_session_emit_establish_credentials (conversation->worker_skeleton,
                                                             service_name);
        }
}

static void
send_environment_variable (const char             *key,
                           const char             *value,
                           GdmSessionConversation *conversation)
{
        gdm_dbus_session_emit_set_environment_variable (conversation->worker_skeleton,
                                                        key,
                                                        value);
}

static void
send_environment (GdmSession             *self,
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

        gdm_dbus_session_emit_open_session (conversation->worker_skeleton,
                                            service_name);
}

static void
stop_all_other_conversations (GdmSession             *self,
                              GdmSessionConversation *conversation_to_keep,
                              gboolean                now)
{
        GHashTableIter iter;
        gpointer key, value;

        if (self->priv->conversations == NULL) {
                return;
        }

        if (conversation_to_keep == NULL) {
                g_debug ("GdmSession: Stopping all conversations");
        } else {
                g_debug ("GdmSession: Stopping all conversations except for %s",
                         conversation_to_keep->service_name);
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

        gdm_dbus_session_emit_start_program (conversation->worker_skeleton,
                                             program);
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

        g_list_free_full (self->priv->pending_connections, g_object_unref);
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

                gdm_dbus_session_emit_set_session_type (conversation->worker_skeleton,
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

                gdm_dbus_session_emit_set_session_name (conversation->worker_skeleton,
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

                gdm_dbus_session_emit_set_language_name (conversation->worker_skeleton,
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

        if (self->priv->server != NULL) {
                g_dbus_server_stop (self->priv->server);
                g_clear_object (&self->priv->server);
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
