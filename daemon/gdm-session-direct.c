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

#include "gdm-session-direct.h"
#include "gdm-session-glue.h"
#include "gdm-dbus-util.h"

#include "gdm-session-record.h"
#include "gdm-session-worker-job.h"
#include "gdm-common.h"

#define GDM_SESSION_DBUS_PATH         "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_INTERFACE    "org.gnome.DisplayManager.Session"
#define GDM_SESSION_DBUS_ERROR_CANCEL "org.gnome.DisplayManager.Session.Error.Cancel"

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"
#endif

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
        CLOSED,
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

static int signals[LAST_SIGNAL];

typedef struct
{
        GdmSessionDirect    *session;
        GdmSessionWorkerJob *job;
        GPid                 worker_pid;
        char                *service_name;
        GDBusConnection     *worker_connection;
        GDBusMethodInvocation *pending_invocation;
        GdmDBusSession      *worker_skeleton;
        guint32              is_stopping : 1;
} GdmSessionConversation;

struct _GdmSessionDirectPrivate
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
        char                *id;
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
        PROP_DISPLAY_ID,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_DEVICE,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_X11_AUTHORITY_FILE,
        PROP_USER_X11_AUTHORITY_FILE,
};

G_DEFINE_TYPE (GdmSessionDirect, gdm_session_direct, G_TYPE_OBJECT)

static GdmSessionConversation *
find_conversation_by_name (GdmSessionDirect *session,
                           const char       *service_name)
{
        GdmSessionConversation *conversation;

        conversation = g_hash_table_lookup (session->priv->conversations, service_name);

        if (conversation == NULL) {
                g_warning ("Tried to look up non-existent conversation %s", service_name);
        }

        return conversation;
}

static gboolean
gdm_session_direct_handle_service_unavailable (GdmDBusSession        *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               const char            *service_name,
                                               const char            *message,
                                               GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'service-unavailable' signal");
        g_signal_emit (session, signals[SERVICE_UNAVAILABLE], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_setup_complete (GdmDBusSession        *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          const char            *service_name,
                                          GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'setup-complete' signal");
        g_signal_emit (session, signals[SETUP_COMPLETE], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_setup_failed (GdmDBusSession        *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        const char            *message,
                                        GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'setup-failed' signal");
        g_signal_emit (session, signals[SETUP_FAILED], 0, service_name, message);

        return TRUE;
}


static gboolean
gdm_session_direct_handle_authenticated (GdmDBusSession        *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         const char            *service_name,
                                         GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'authenticated' signal");
        g_signal_emit (session, signals[AUTHENTICATED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_authentication_failed (GdmDBusSession        *skeleton,
                                                 GDBusMethodInvocation *invocation,
                                                 const char            *service_name,
                                                 const char            *message,
                                                 GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation = find_conversation_by_name (session, service_name);
        gdm_session_record_failed (conversation->worker_pid,
                                   session->priv->selected_user,
                                   session->priv->display_hostname,
                                   session->priv->display_name,
                                   session->priv->display_device);

        g_debug ("GdmSessionDirect: Emitting 'authentication-failed' signal");
        g_signal_emit (session, signals[AUTHENTICATION_FAILED], 0, service_name, message);

        return TRUE;
}


static gboolean
gdm_session_direct_handle_authorized (GdmDBusSession        *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'authorized' signal");
        g_signal_emit (session, signals[AUTHORIZED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_authorization_failed (GdmDBusSession        *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                const char            *service_name,
                                                const char            *message,
                                                GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'authorization-failed' signal");
        g_signal_emit (session, signals[AUTHORIZATION_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_accredited (GdmDBusSession        *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'accredited' signal");
        g_signal_emit (session, signals[ACCREDITED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_accreditation_failed (GdmDBusSession        *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                const char            *service_name,
                                                const char            *message,
                                                GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'accreditation-failed' signal");
        g_signal_emit (session, signals[ACCREDITATION_FAILED], 0, service_name, message);

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

        g_debug ("GdmSessionDirect: looking for session file '%s'", file);

        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         get_system_session_dirs (),
                                         NULL,
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

        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC,
                                      NULL);
        if (exec != NULL) {
                res = is_prog_in_path (exec);
                g_free (exec);
                exec = NULL;

                if (! res) {
                        g_debug ("GdmSessionDirect: Command not found: %s",
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
get_default_language_name (GdmSessionDirect *session)
{
    if (session->priv->saved_language != NULL) {
                return session->priv->saved_language;
    }

    return setlocale (LC_MESSAGES, NULL);
}

static const char *
get_fallback_session_name (GdmSessionDirect *session_direct)
{
        const char  **search_dirs;
        int           i;
        char         *name;
        GSequence    *sessions;
        GSequenceIter *session;

        if (session_direct->priv->fallback_session_name != NULL) {
                /* verify that the cached version still exists */
                if (get_session_command_for_name (session_direct->priv->fallback_session_name, NULL)) {
                        goto out;
                }
        }

        name = g_strdup ("gnome");
        if (get_session_command_for_name (name, NULL)) {
                g_free (session_direct->priv->fallback_session_name);
                session_direct->priv->fallback_session_name = name;
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

        g_free (session_direct->priv->fallback_session_name);
        session_direct->priv->fallback_session_name = name;

        g_sequence_free (sessions);

 out:
        return session_direct->priv->fallback_session_name;
}

static const char *
get_default_session_name (GdmSessionDirect *session)
{
        if (session->priv->saved_session != NULL) {
                return session->priv->saved_session;
        }

        return get_fallback_session_name (session);
}

static void
gdm_session_direct_defaults_changed (GdmSessionDirect *session)
{
        g_signal_emit (session, signals[DEFAULT_LANGUAGE_NAME_CHANGED], 0,
                       get_default_language_name (session));
        g_signal_emit (session, signals[DEFAULT_SESSION_NAME_CHANGED], 0,
                       get_default_session_name (session));
}

void
gdm_session_direct_select_user (GdmSessionDirect *session,
                                const char       *text)
{
        g_debug ("GdmSessionDirect: Setting user: '%s'", text);

        g_free (session->priv->selected_user);
        session->priv->selected_user = g_strdup (text);

        g_free (session->priv->saved_session);
        session->priv->saved_session = NULL;

        g_free (session->priv->saved_language);
        session->priv->saved_language = NULL;
}

static gboolean
gdm_session_direct_handle_username_changed (GdmDBusSession        *skeleton,
                                            GDBusMethodInvocation *invocation,
                                            const char            *service_name,
                                            const char            *text,
                                            GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: changing username from '%s' to '%s'",
                 session->priv->selected_user != NULL ? session->priv->selected_user : "<unset>",
                 (strlen (text)) ? text : "<unset>");

        gdm_session_direct_select_user (session, (strlen (text) > 0) ? g_strdup (text) : NULL);
        g_signal_emit (session, signals[SELECTED_USER_CHANGED], 0, session->priv->selected_user);

        gdm_session_direct_defaults_changed (session);

        return TRUE;
}

static void
cancel_pending_query (GdmSessionConversation *conversation)
{
        if (conversation->pending_invocation == NULL) {
                return;
        }

        g_debug ("GdmSessionDirect: Cancelling pending query");

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
gdm_session_direct_handle_info_query (GdmDBusSession        *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      const char            *text,
                                      GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (session, service_name);
        set_pending_query (conversation, invocation);

        g_debug ("GdmSessionDirect: Emitting 'info-query' signal");
        g_signal_emit (session, signals[INFO_QUERY], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_secret_info_query (GdmDBusSession        *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             const char            *service_name,
                                             const char            *text,
                                             GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (session, service_name);
        set_pending_query (conversation, invocation);

        g_debug ("GdmSessionDirect: Emitting 'secret-info-query' signal");
        g_signal_emit (session, signals[SECRET_INFO_QUERY], 0, service_name, text);

        return TRUE;
}


static gboolean
gdm_session_direct_handle_info (GdmDBusSession        *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   const char            *service_name,
                                   const char            *text,
                                   GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'info' signal");
        g_signal_emit (session, signals[INFO], 0, service_name, text);

        return TRUE;
}


static gboolean
gdm_session_direct_handle_cancel_pending_query (GdmDBusSession        *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                const char            *service_name,
                                                GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        conversation = find_conversation_by_name (session, service_name);
        cancel_pending_query (conversation);

        g_dbus_method_invocation_return_value (invocation, NULL);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_problem (GdmDBusSession        *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   const char            *service_name,
                                   const char            *text,
                                   GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'problem' signal");
        g_signal_emit (session, signals[PROBLEM], 0, service_name, text);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_opened (GdmDBusSession        *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  const char            *service_name,
                                  GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'session-opened' signal");
        g_signal_emit (session, signals[SESSION_OPENED], 0, service_name);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_open_failed (GdmDBusSession        *skeleton,
                                       GDBusMethodInvocation *invocation,
                                       const char            *service_name,
                                       const char            *message,
                                       GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_debug ("GdmSessionDirect: Emitting 'session-open-failed' signal");
        g_signal_emit (session, signals[SESSION_OPEN_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_session_started (GdmDBusSession        *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           const char            *service_name,
                                           int                    pid,
                                           GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation = find_conversation_by_name (session, service_name);
        session->priv->session_pid = pid;
        session->priv->session_conversation = conversation;

        gdm_session_record_login (conversation->worker_pid,
                                  session->priv->selected_user,
                                  session->priv->display_hostname,
                                  session->priv->display_name,
                                  session->priv->display_device);

        g_debug ("GdmSessionDirect: Emitting 'session-started' signal with pid '%d'", pid);
        g_signal_emit (session, signals[SESSION_STARTED], 0, service_name, pid);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_session_start_failed (GdmDBusSession        *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                const char            *service_name,
                                                const char            *message,
                                                GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation = find_conversation_by_name (session, service_name);
        gdm_session_record_login (conversation->worker_pid,
                                  session->priv->selected_user,
                                  session->priv->display_hostname,
                                  session->priv->display_name,
                                  session->priv->display_device);

        g_debug ("GdmSessionDirect: Emitting 'session-start-failed' signal");
        g_signal_emit (session, signals[SESSION_START_FAILED], 0, service_name, message);

        return TRUE;
}

static gboolean
gdm_session_direct_handle_session_exited_or_died (GdmDBusSession        *skeleton,
                                                  GDBusMethodInvocation *invocation,
                                                  const char            *service_name,
                                                  int                    status,
                                                  GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        session->priv->session_conversation = NULL;

        if (WIFEXITED (status)) {
                g_debug ("GdmSessionDirect: Emitting 'session-exited' signal with exit code '%d'",
                         WEXITSTATUS (status));
                g_signal_emit (session, signals[SESSION_EXITED], 0, WEXITSTATUS (status));
        } else if (WIFSIGNALED (status)) {
                g_debug ("GdmSessionDirect: Emitting 'session-died' signal with signal number '%d'",
                         WTERMSIG (status));
                g_signal_emit (session, signals[SESSION_DIED], 0, WTERMSIG (status));
        }

        return TRUE;
}

static gboolean
gdm_session_direct_handle_saved_language_name_read (GdmDBusSession        *skeleton,
                                                    GDBusMethodInvocation *invocation,
                                                    const char            *language_name,
                                                    GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        if (strcmp (language_name,
                    get_default_language_name (session)) != 0) {
                g_free (session->priv->saved_language);
                session->priv->saved_language = g_strdup (language_name);

                g_signal_emit (session, signals[DEFAULT_LANGUAGE_NAME_CHANGED], 0, language_name);
        }

        return TRUE;
}

static gboolean
gdm_session_direct_handle_saved_session_name_read (GdmDBusSession        *skeleton,
                                                   GDBusMethodInvocation *invocation,
                                                   const char            *session_name,
                                                   GdmSessionDirect      *session)
{
        g_dbus_method_invocation_return_value (invocation, NULL);

        if (! get_session_command_for_name (session_name, NULL)) {
                /* ignore sessions that don't exist */
                g_debug ("GdmSessionDirect: not using invalid .dmrc session: %s", session_name);
                g_free (session->priv->saved_session);
                session->priv->saved_session = NULL;
                goto out;
        }

        if (strcmp (session_name,
                    get_default_session_name (session)) != 0) {
                g_free (session->priv->saved_session);
                session->priv->saved_session = g_strdup (session_name);

                g_signal_emit (session, signals[DEFAULT_SESSION_NAME_CHANGED], 0, session_name);
        }
 out:
        return TRUE;
}

static GdmSessionConversation *
find_conversation_by_pid (GdmSessionDirect *session,
                          GPid              pid)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, session->priv->conversations);
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

        g_debug ("GdmSessionDirect: User not allowed");

        return FALSE;
}

/* FIXME: this should be in GIO */
static pid_t
g_credentials_get_unix_process (GCredentials *credentials)
{
#ifdef __linux__
        struct ucred *u;
        u = g_credentials_get_native (credentials, G_CREDENTIALS_TYPE_LINUX_UCRED);

        return u->pid;
#else
        /* FIXME */
        g_assert_if_reached ();
#endif
}

static gboolean
register_worker (GdmDBusSession        *skeleton,
                 GDBusMethodInvocation *invocation,
                 GdmSessionDirect      *session)
{
        GdmSessionConversation *conversation;
        GDBusConnection *connection;
        GList *connection_node;
        GCredentials *credentials;
        pid_t pid;

        g_debug ("GdmSessionDirect: Authenticating new connection");

        connection = g_dbus_method_invocation_get_connection (invocation);
        connection_node = g_list_find (session->priv->pending_connections, connection);

        if (connection_node == NULL) {
                g_debug ("GdmSessionDirect: Ignoring connection that we aren't tracking");
                return FALSE;
        }

        session->priv->pending_connections =
                g_list_delete_link (session->priv->pending_connections,
                                    connection_node);

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid = g_credentials_get_unix_process (credentials);

        conversation = find_conversation_by_pid (session, (GPid) pid);

        if (conversation == NULL) {
                g_warning ("GdmSessionDirect: New worker connection is from unknown source");

                g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Connection is not from a known conversation");
                g_dbus_connection_close_sync (connection, NULL, NULL);
                return TRUE;
        }

        g_dbus_method_invocation_return_value (invocation, NULL);

        conversation->worker_connection = g_object_ref (connection);
        conversation->worker_skeleton = g_object_ref (skeleton);
        g_debug ("GdmSessionDirect: worker connection is %p", connection);

        g_debug ("GdmSessionDirect: Emitting conversation-started signal");
        g_signal_emit (session, signals[CONVERSATION_STARTED], 0, conversation->service_name);

        g_debug ("GdmSessionDirect: Conversation started");

        return TRUE;
}

static gboolean
handle_connection (GDBusServer      *server,
                   GDBusConnection  *connection,
                   GdmSessionDirect *session)
{
        GdmDBusSession *skeleton;

        g_debug ("GdmSessionDirect: Handing new connection");

        /* add to the list of pending connections.  We won't be able to
         * associate it with a specific worker conversation until we have
         * authenticated the connection (from the Hello handler).
         */
        session->priv->pending_connections =
                g_list_prepend (session->priv->pending_connections,
                                g_object_ref (connection));

        skeleton = GDM_DBUS_SESSION (gdm_dbus_session_skeleton_new ());
        g_signal_connect (skeleton, "handle-hello",
                          G_CALLBACK (register_worker), session);
        g_signal_connect (skeleton, "handle-info-query",
                          G_CALLBACK (gdm_session_direct_handle_info_query), session);
        g_signal_connect (skeleton, "handle-secret-info-query",
                          G_CALLBACK (gdm_session_direct_handle_secret_info_query), session);
        g_signal_connect (skeleton, "handle-info",
                          G_CALLBACK (gdm_session_direct_handle_info), session);
        g_signal_connect (skeleton, "handle-problem",
                          G_CALLBACK (gdm_session_direct_handle_problem), session);
        g_signal_connect (skeleton, "handle-cancel-pending-query",
                          G_CALLBACK (gdm_session_direct_handle_cancel_pending_query), session);
        g_signal_connect (skeleton, "handle-service-unavailable",
                          G_CALLBACK (gdm_session_direct_handle_service_unavailable), session);
        g_signal_connect (skeleton, "handle-setup-complete",
                          G_CALLBACK (gdm_session_direct_handle_setup_complete), session);
        g_signal_connect (skeleton, "handle-setup-failed",
                          G_CALLBACK (gdm_session_direct_handle_setup_failed), session);
        g_signal_connect (skeleton, "handle-authenticated",
                          G_CALLBACK (gdm_session_direct_handle_authenticated), session);
        g_signal_connect (skeleton, "handle-authentication-failed",
                          G_CALLBACK (gdm_session_direct_handle_authentication_failed), session);
        g_signal_connect (skeleton, "handle-authorized",
                          G_CALLBACK (gdm_session_direct_handle_authorized), session);
        g_signal_connect (skeleton, "handle-authorization-failed",
                          G_CALLBACK (gdm_session_direct_handle_authorization_failed), session);
        g_signal_connect (skeleton, "handle-accredited",
                          G_CALLBACK (gdm_session_direct_handle_accredited), session);
        g_signal_connect (skeleton, "handle-accreditation-failed",
                          G_CALLBACK (gdm_session_direct_handle_accreditation_failed), session);
        g_signal_connect (skeleton, "handle-username-changed",
                          G_CALLBACK (gdm_session_direct_handle_username_changed), session);
        g_signal_connect (skeleton, "handle-opened",
                          G_CALLBACK (gdm_session_direct_handle_opened), session);
        g_signal_connect (skeleton, "handle-open-failed",
                          G_CALLBACK (gdm_session_direct_handle_open_failed), session);
        g_signal_connect (skeleton, "handle-session-started",
                          G_CALLBACK (gdm_session_direct_handle_session_started), session);
        g_signal_connect (skeleton, "handle-session-start-failed",
                          G_CALLBACK (gdm_session_direct_handle_session_start_failed), session);
        g_signal_connect (skeleton, "handle-session-exited",
                          G_CALLBACK (gdm_session_direct_handle_session_exited_or_died), session);
        g_signal_connect (skeleton, "handle-saved-language-name-read",
                          G_CALLBACK (gdm_session_direct_handle_saved_language_name_read), session);
        g_signal_connect (skeleton, "handle-saved-session-name-read",
                          G_CALLBACK (gdm_session_direct_handle_saved_session_name_read), session);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          connection,
                                          "/org/gnome/DisplayManager/Session",
                                          NULL);

        return TRUE;
}

static void
setup_server (GdmSessionDirect *session)
{
        GDBusAuthObserver *observer;
        GDBusServer *server;
        GError *error = NULL;

        g_debug ("GdmSessionDirect: Creating D-Bus server for session");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect (observer, "authorize-authenticated-peer",
                          G_CALLBACK (allow_user_function), NULL);

        server = gdm_dbus_setup_private_server (observer, &error);
        g_object_unref (observer);

        if (server == NULL) {
                g_warning ("Cannot create D-BUS server for the session: %s", error->message);
                /* FIXME: should probably fail if we can't create the socket */
                return;
        }

        g_signal_connect (server, "new-connection",
                          G_CALLBACK (handle_connection), session);
        session->priv->server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSessionDirect: D-Bus server listening on %s",
                 g_dbus_server_get_client_address (session->priv->server));
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
gdm_session_direct_init (GdmSessionDirect *session)
{
        session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session,
                                                     GDM_TYPE_SESSION_DIRECT,
                                                     GdmSessionDirectPrivate);

        session->priv->conversations = g_hash_table_new_full (g_str_hash,
                                                              g_str_equal,
                                                              (GDestroyNotify) g_free,
                                                              (GDestroyNotify)
                                                              free_conversation);
        session->priv->environment = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            (GDestroyNotify) g_free,
                                                            (GDestroyNotify) g_free);

        setup_server (session);
}

static void
worker_started (GdmSessionWorkerJob *job,
                GdmSessionConversation *conversation)
{
        g_debug ("GdmSessionDirect: Worker job started");
}

static void
worker_exited (GdmSessionWorkerJob *job,
               int                  code,
               GdmSessionConversation *conversation)
{
        GdmSessionDirect *self = conversation->session;

        g_debug ("GdmSessionDirect: Worker job exited: %d", code);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                gdm_session_record_logout (self->priv->session_pid,
                                           self->priv->selected_user,
                                           self->priv->display_hostname,
                                           self->priv->display_name,
                                           self->priv->display_device);

                g_signal_emit (self, signals[SESSION_EXITED], 0, code);
        }

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_debug ("GdmSessionDirect: Emitting conversation-stopped signal");
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
        GdmSessionDirect *self = conversation->session;

        g_debug ("GdmSessionDirect: Worker job died: %d", signum);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_DIED], 0, signum);
        }

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_debug ("GdmSessionDirect: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
        g_object_unref (conversation->job);

        if (conversation->is_stopping) {
                g_object_unref (conversation->job);
                conversation->job = NULL;
        }

        free_conversation (conversation);
}

static GdmSessionConversation *
start_conversation (GdmSessionDirect *session,
                    const char       *service_name)
{
        GdmSessionConversation *conversation;
        char                   *job_name;

        conversation = g_new0 (GdmSessionConversation, 1);
        conversation->session = session;
        conversation->service_name = g_strdup (service_name);
        conversation->worker_pid = -1;
        conversation->job = gdm_session_worker_job_new ();
        gdm_session_worker_job_set_server_address (conversation->job,
                                                   g_dbus_server_get_client_address (session->priv->server));
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
gdm_session_direct_start_conversation (GdmSessionDirect *session,
                                       const char       *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = g_hash_table_lookup (session->priv->conversations,
                                            service_name);

        if (conversation != NULL) {
                if (!conversation->is_stopping) {
                        g_warning ("GdmSessionDirect: conversation %s started more than once", service_name);
                        return;
                }
                g_debug ("GdmSessionDirect: stopping old conversation %s", service_name);
                gdm_session_worker_job_stop_now (conversation->job);
                g_object_unref (conversation->job);
                conversation->job = NULL;
        }

        g_debug ("GdmSessionDirect: starting conversation %s", service_name);

        conversation = start_conversation (session, service_name);

        g_hash_table_insert (session->priv->conversations,
                             g_strdup (service_name), conversation);
}

void
gdm_session_direct_stop_conversation (GdmSessionDirect *session,
                                      const char       *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        g_debug ("GdmSessionDirect: stopping conversation %s", service_name);

        conversation = find_conversation_by_name (session, service_name);

        if (conversation != NULL) {
                stop_conversation (conversation);
        }
}

static void
send_setup (GdmSessionDirect *session,
            const char       *service_name)
{
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

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
        if (session->priv->display_seat_id != NULL) {
                display_seat_id = session->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (session->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = session->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }

        g_debug ("GdmSessionDirect: Beginning setup");

        conversation = find_conversation_by_name (session, service_name);
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
send_setup_for_user (GdmSessionDirect *session,
                     const char       *service_name)
{
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        const char     *selected_user;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

        conversation = find_conversation_by_name (session, service_name);

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
        if (session->priv->display_seat_id != NULL) {
                display_seat_id = session->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (session->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = session->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }
        if (session->priv->selected_user != NULL) {
                selected_user = session->priv->selected_user;
        } else {
                selected_user = "";
        }

        g_debug ("GdmSessionDirect: Beginning setup for user %s", session->priv->selected_user);

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
send_setup_for_program (GdmSessionDirect *session,
                        const char       *service_name,
                        const char       *log_file)
{
        const char     *display_name;
        const char     *display_device;
        const char     *display_seat_id;
        const char     *display_hostname;
        const char     *display_x11_authority_file;
        GdmSessionConversation *conversation;

        g_assert (service_name != NULL);

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
        if (session->priv->display_seat_id != NULL) {
                display_seat_id = session->priv->display_seat_id;
        } else {
                display_seat_id = "";
        }
        if (session->priv->display_x11_authority_file != NULL) {
                display_x11_authority_file = session->priv->display_x11_authority_file;
        } else {
                display_x11_authority_file = "";
        }

        g_debug ("GdmSessionDirect: Beginning setup for session for program with log '%s'", log_file);

        conversation = find_conversation_by_name (session, service_name);
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
gdm_session_direct_setup (GdmSessionDirect *session,
                          const char       *service_name)
{
        g_return_if_fail (session != NULL);

        send_setup (session, service_name);
        gdm_session_direct_defaults_changed (session);
}

void
gdm_session_direct_setup_for_user (GdmSessionDirect *session,
                                   const char       *service_name,
                                   const char       *username)
{
        g_return_if_fail (session != NULL);
        g_return_if_fail (username != NULL);

        gdm_session_direct_select_user (session, username);

        send_setup_for_user (session, service_name);
        gdm_session_direct_defaults_changed (session);
}

void
gdm_session_direct_setup_for_program (GdmSessionDirect *session,
                                      const char       *service_name,
                                      const char       *log_file)
{
        g_return_if_fail (session != NULL);

        send_setup_for_program (session, service_name, log_file);
}

void
gdm_session_direct_authenticate (GdmSessionDirect *session,
                                 const char       *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = find_conversation_by_name (session, service_name);
        if (conversation != NULL) {
                gdm_dbus_session_emit_authenticate (conversation->worker_skeleton,
                                                    service_name);
        }
}

void
gdm_session_direct_authorize (GdmSessionDirect *session,
                              const char       *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = find_conversation_by_name (session, service_name);
        if (conversation != NULL) {
                gdm_dbus_session_emit_authorize (conversation->worker_skeleton,
                                                 service_name);
        }
}

void
gdm_session_direct_accredit (GdmSessionDirect *session,
                             const char       *service_name,
                             gboolean          refresh)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = find_conversation_by_name (session, service_name);
        if (conversation == NULL) {
                return;
        }

        if (refresh) {
                gdm_dbus_session_emit_refresh_credentials (conversation->worker_skeleton,
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
                                                        key, value);
}

static void
send_environment (GdmSessionDirect       *session,
                  GdmSessionConversation *conversation)
{

        g_hash_table_foreach (session->priv->environment,
                              (GHFunc) send_environment_variable,
                              conversation);
}

static const char *
get_language_name (GdmSessionDirect *session)
{
        if (session->priv->selected_language != NULL) {
                return session->priv->selected_language;
        }

        return get_default_language_name (session);
}

static const char *
get_session_name (GdmSessionDirect *session)
{
        /* FIXME: test the session names before we use them? */

        if (session->priv->selected_session != NULL) {
                return session->priv->selected_session;
        }

        return get_default_session_name (session);
}

static char *
get_session_command (GdmSessionDirect *session)
{
        gboolean    res;
        char       *command;
        const char *session_name;

        session_name = get_session_name (session);

        command = NULL;
        res = get_session_command_for_name (session_name, &command);
        if (! res) {
                g_critical ("Cannot find a command for specified session: %s", session_name);
                exit (1);
        }

        return command;
}

void
gdm_session_direct_set_environment_variable (GdmSessionDirect *session,
                                             const char       *key,
                                             const char       *value)
{
        g_return_if_fail (key != NULL);
        g_return_if_fail (value != NULL);

        g_hash_table_replace (session->priv->environment,
                              g_strdup (key),
                              g_strdup (value));
}

static void
setup_session_environment (GdmSessionDirect *session)
{
        const char *locale;

        gdm_session_direct_set_environment_variable (session,
                                                     "GDMSESSION",
                                                     get_session_name (session));
        gdm_session_direct_set_environment_variable (session,
                                                     "DESKTOP_SESSION",
                                                     get_session_name (session));

        locale = get_language_name (session);

        if (locale != NULL && locale[0] != '\0') {
                gdm_session_direct_set_environment_variable (session,
                                                             "LANG",
                                                             locale);
                gdm_session_direct_set_environment_variable (session,
                                                             "GDM_LANG",
                                                             locale);
        }

        gdm_session_direct_set_environment_variable (session,
                                                     "DISPLAY",
                                                     session->priv->display_name);

        if (session->priv->user_x11_authority_file != NULL) {
                gdm_session_direct_set_environment_variable (session,
                                                             "XAUTHORITY",
                                                             session->priv->user_x11_authority_file);
        }

        if (g_getenv ("WINDOWPATH") != NULL) {
                gdm_session_direct_set_environment_variable (session,
                                                             "WINDOWPATH",
                                                             g_getenv ("WINDOWPATH"));
        }


        /* FIXME: We do this here and in the session worker.  We should consolidate
         * somehow.
         */
        gdm_session_direct_set_environment_variable (session,
                                                     "PATH",
                                                     strcmp (BINDIR, "/usr/bin") == 0?
                                                     GDM_SESSION_DEFAULT_PATH :
                                                     BINDIR ":" GDM_SESSION_DEFAULT_PATH);

}

void
gdm_session_direct_open_session (GdmSessionDirect *session,
                                 const char       *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = find_conversation_by_name (session, service_name);

        gdm_dbus_session_emit_open_session (conversation->worker_skeleton,
                                            service_name);
}

static void
stop_all_other_conversations (GdmSessionDirect        *session,
                              GdmSessionConversation  *conversation_to_keep,
                              gboolean                 now)
{
        GHashTableIter iter;
        gpointer key, value;

        if (session->priv->conversations == NULL) {
                return;
        }

        if (conversation_to_keep == NULL) {
                g_debug ("GdmSessionDirect: Stopping all conversations");
        } else {
                g_debug ("GdmSessionDirect: Stopping all conversations "
                         "except for %s", conversation_to_keep->service_name);
        }

        g_hash_table_iter_init (&iter, session->priv->conversations);
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
                g_hash_table_remove_all (session->priv->conversations);

                if (conversation_to_keep != NULL) {
                        g_hash_table_insert (session->priv->conversations,
                                             g_strdup (conversation_to_keep->service_name),
                                             conversation_to_keep);
                }

                if (session->priv->session_conversation != conversation_to_keep) {
                        session->priv->session_conversation = NULL;
                }
        }

}

void
gdm_session_direct_start_session (GdmSessionDirect *session,
                                  const char       *service_name)
{
        GdmSessionConversation *conversation;
        char             *command;
        char             *program;

        g_return_if_fail (session != NULL);
        g_return_if_fail (session->priv->session_conversation == NULL);

        conversation = find_conversation_by_name (session, service_name);

        if (conversation == NULL) {
                g_warning ("GdmSessionDirect: Tried to start session of "
                           "nonexistent conversation %s", service_name);
                return;
        }

        stop_all_other_conversations (session, conversation, FALSE);

        if (session->priv->selected_program == NULL) {
                command = get_session_command (session);

                if (gdm_session_direct_bypasses_xsession (session)) {
                        program = g_strdup (command);
                } else {
                        program = g_strdup_printf (GDMCONFDIR "/Xsession \"%s\"", command);
                }

                g_free (command);
        } else {
                program = g_strdup (session->priv->selected_program);
        }

        setup_session_environment (session);
        send_environment (session, conversation);

        gdm_dbus_session_emit_start_program (conversation->worker_skeleton,
                                             program);
        g_free (program);
}

static void
stop_all_conversations (GdmSessionDirect *session)
{
        stop_all_other_conversations (session, NULL, TRUE);
}

void
gdm_session_direct_close (GdmSessionDirect *session)
{
        g_return_if_fail (session != NULL);

        g_debug ("GdmSessionDirect: Closing session");

        if (session->priv->session_conversation != NULL) {
                gdm_session_record_logout (session->priv->session_pid,
                                           session->priv->selected_user,
                                           session->priv->display_hostname,
                                           session->priv->display_name,
                                           session->priv->display_device);
        }

        stop_all_conversations (session);

        g_list_free_full (session->priv->pending_connections, g_object_unref);
        session->priv->pending_connections = NULL;

        g_free (session->priv->selected_user);
        session->priv->selected_user = NULL;

        g_free (session->priv->selected_session);
        session->priv->selected_session = NULL;

        g_free (session->priv->saved_session);
        session->priv->saved_session = NULL;

        g_free (session->priv->selected_language);
        session->priv->selected_language = NULL;

        g_free (session->priv->saved_language);
        session->priv->saved_language = NULL;

        g_free (session->priv->user_x11_authority_file);
        session->priv->user_x11_authority_file = NULL;

        g_hash_table_remove_all (session->priv->environment);

        session->priv->session_pid = -1;
        session->priv->session_conversation = NULL;
}

void
gdm_session_direct_answer_query  (GdmSessionDirect *session,
                                  const char       *service_name,
                                  const char       *text)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (session != NULL);

        conversation = find_conversation_by_name (session, service_name);
        answer_pending_query (conversation, text);
}

void
gdm_session_direct_cancel  (GdmSessionDirect *session)
{
        g_return_if_fail (session != NULL);

        stop_all_conversations (session);
}

char *
gdm_session_direct_get_username (GdmSessionDirect *session)
{
        g_return_val_if_fail (session != NULL, NULL);

        return g_strdup (session->priv->selected_user);
}

char *
gdm_session_direct_get_display_device (GdmSessionDirect *session)
{
        g_return_val_if_fail (session != NULL, NULL);

        return g_strdup (session->priv->display_device);
}

char *
gdm_session_direct_get_display_seat_id (GdmSessionDirect *session)
{
        g_return_val_if_fail (session != NULL, NULL);

        return g_strdup (session->priv->display_seat_id);
}

gboolean
gdm_session_direct_bypasses_xsession (GdmSessionDirect *session_direct)
{
        GError     *error;
        GKeyFile   *key_file;
        gboolean    res;
        gboolean    bypasses_xsession = FALSE;
        char       *filename;

        g_return_val_if_fail (session_direct != NULL, FALSE);
        g_return_val_if_fail (GDM_IS_SESSION_DIRECT (session_direct), FALSE);

        filename = g_strdup_printf ("%s.desktop", get_session_name (session_direct));

        key_file = g_key_file_new ();
        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         filename,
                                         get_system_session_dirs (),
                                         NULL,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSessionDirect: File '%s' not found: %s", filename, error->message);
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
                        g_debug ("GdmSessionDirect: Session %s bypasses Xsession wrapper script", filename);
                }
        }

out:
        g_free (filename);
        return bypasses_xsession;
}

void
gdm_session_direct_select_program (GdmSessionDirect *session,
                                   const char       *text)
{
        g_free (session->priv->selected_program);
        session->priv->selected_program = g_strdup (text);
}

void
gdm_session_direct_select_session_type (GdmSessionDirect *session,
                                        const char       *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, session->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation = value;

                gdm_dbus_session_emit_set_session_type (conversation->worker_skeleton,
                                                        text);
        }
}

void
gdm_session_direct_select_session (GdmSessionDirect *session,
                                   const char       *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_free (session->priv->selected_session);

        if (strcmp (text, "__previous") == 0) {
                session->priv->selected_session = NULL;
        } else {
                session->priv->selected_session = g_strdup (text);
        }

        g_hash_table_iter_init (&iter, session->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation = value;

                gdm_dbus_session_emit_set_session_name (conversation->worker_skeleton,
                                                        text);
        }
}

void
gdm_session_direct_select_language (GdmSessionDirect *session,
                                    const char       *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_free (session->priv->selected_language);

        if (strcmp (text, "__previous") == 0) {
                session->priv->selected_language = NULL;
        } else {
                session->priv->selected_language = g_strdup (text);
        }

        g_hash_table_iter_init (&iter, session->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation = value;

                gdm_dbus_session_emit_set_language_name (conversation->worker_skeleton,
                                                         text);
        }
}

static void
_gdm_session_direct_set_display_id (GdmSessionDirect *session,
                                    const char       *id)
{
        g_free (session->priv->display_id);
        session->priv->display_id = g_strdup (id);
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
_gdm_session_direct_set_display_seat_id (GdmSessionDirect *session,
                                         const char       *name)
{
        g_free (session->priv->display_seat_id);
        session->priv->display_seat_id = g_strdup (name);
}

static void
_gdm_session_direct_set_user_x11_authority_file (GdmSessionDirect *session,
                                                 const char       *name)
{
        g_free (session->priv->user_x11_authority_file);
        session->priv->user_x11_authority_file = g_strdup (name);
}

static void
_gdm_session_direct_set_display_x11_authority_file (GdmSessionDirect *session,
                                                    const char       *name)
{
        g_free (session->priv->display_x11_authority_file);
        session->priv->display_x11_authority_file = g_strdup (name);
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
        case PROP_DISPLAY_ID:
                _gdm_session_direct_set_display_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_NAME:
                _gdm_session_direct_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                _gdm_session_direct_set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_DEVICE:
                _gdm_session_direct_set_display_device (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_SEAT_ID:
                _gdm_session_direct_set_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                _gdm_session_direct_set_user_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                _gdm_session_direct_set_display_x11_authority_file (self, g_value_get_string (value));
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
        case PROP_DISPLAY_ID:
                g_value_set_string (value, self->priv->display_id);
                break;
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
gdm_session_direct_dispose (GObject *object)
{
        GdmSessionDirect *session;

        session = GDM_SESSION_DIRECT (object);

        g_debug ("GdmSessionDirect: Disposing session");

        gdm_session_direct_close (session);

        g_free (session->priv->display_id);
        session->priv->display_id = NULL;

        g_free (session->priv->display_name);
        session->priv->display_name = NULL;

        g_free (session->priv->display_hostname);
        session->priv->display_hostname = NULL;

        g_free (session->priv->display_device);
        session->priv->display_device = NULL;

        g_free (session->priv->display_seat_id);
        session->priv->display_seat_id = NULL;

        g_free (session->priv->display_x11_authority_file);
        session->priv->display_x11_authority_file = NULL;

        if (session->priv->server != NULL) {
                g_dbus_server_stop (session->priv->server);
                g_clear_object (&session->priv->server);
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

        g_free (session->priv->id);

        g_free (session->priv->selected_user);
        g_free (session->priv->selected_session);
        g_free (session->priv->saved_session);
        g_free (session->priv->selected_language);
        g_free (session->priv->saved_language);

        g_free (session->priv->fallback_session_name);

        parent_class = G_OBJECT_CLASS (gdm_session_direct_parent_class);

        if (parent_class->finalize != NULL)
                parent_class->finalize (object);
}

#if 0
/* FIXME: this will change */
static gboolean
register_session (GdmSessionDirect *session)
{
        GError *error;

        error = NULL;
        session->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (session->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (session->priv->connection, session->priv->id, G_OBJECT (session));

        return TRUE;
}
#endif

static GObject *
gdm_session_direct_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmSessionDirect *session;
        const char       *id;

        session = GDM_SESSION_DIRECT (G_OBJECT_CLASS (gdm_session_direct_parent_class)->constructor (type,
                                                                                          n_construct_properties,
                                                                                          construct_properties));
        if (session->priv->display_id != NULL) {
                /* Always match the session id with the master */
                id = NULL;
                if (g_str_has_prefix (session->priv->display_id, "/org/gnome/DisplayManager/Displays/")) {
                        id = session->priv->display_id + strlen ("/org/gnome/DisplayManager/Displays/");
                }

                g_assert (id != NULL);

                session->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Sessions/%s", id);
                g_debug ("GdmSessionDirect: Registering %s", session->priv->id);

#if 0
                res = register_session (session);
                if (! res) {
                        g_warning ("Unable to register session with system bus");
                }
#endif
        }

        return G_OBJECT (session);
}

static void
gdm_session_direct_class_init (GdmSessionDirectClass *session_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (session_class);

        object_class->get_property = gdm_session_direct_get_property;
        object_class->set_property = gdm_session_direct_set_property;
        object_class->constructor = gdm_session_direct_constructor;
        object_class->dispose = gdm_session_direct_dispose;
        object_class->finalize = gdm_session_direct_finalize;

        g_type_class_add_private (session_class, sizeof (GdmSessionDirectPrivate));

        signals [CONVERSATION_STARTED] =
                g_signal_new ("conversation-started",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CONVERSATION_STOPPED] =
                g_signal_new ("conversation-stopped",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SERVICE_UNAVAILABLE] =
                g_signal_new ("service-unavailable",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SETUP_COMPLETE] =
                g_signal_new ("setup-complete",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SETUP_FAILED] =
                g_signal_new ("setup-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [AUTHENTICATED] =
                g_signal_new ("authenticated",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [AUTHENTICATION_FAILED] =
                g_signal_new ("authentication-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [AUTHORIZED] =
                g_signal_new ("authorized",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [AUTHORIZATION_FAILED] =
                g_signal_new ("authorization-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [ACCREDITED] =
                g_signal_new ("accredited",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [ACCREDITATION_FAILED] =
                g_signal_new ("accreditation-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
         signals [INFO_QUERY] =
                g_signal_new ("info-query",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SECRET_INFO_QUERY] =
                g_signal_new ("secret-info-query",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [INFO] =
                g_signal_new ("info",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [PROBLEM] =
                g_signal_new ("problem",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_OPENED] =
                g_signal_new ("session-opened",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SESSION_OPEN_FAILED] =
                g_signal_new ("session-open-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_STARTED] =
                g_signal_new ("session-started",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_INT);
        signals [SESSION_START_FAILED] =
                g_signal_new ("session-start-failed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING, G_TYPE_STRING);
        signals [SESSION_EXITED] =
                g_signal_new ("session-exited",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [SESSION_DIED] =
                g_signal_new ("session-died",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [CLOSED] =
                g_signal_new ("closed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [SELECTED_USER_CHANGED] =
                g_signal_new ("selected-user-changed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [DEFAULT_LANGUAGE_NAME_CHANGED] =
                g_signal_new ("default-language-name-changed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [DEFAULT_SESSION_NAME_CHANGED] =
                g_signal_new ("default-session-name-changed",
                              G_TYPE_FROM_CLASS (session_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_ID,
                                         g_param_spec_string ("display-id",
                                                              "display id",
                                                              "display id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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

GdmSessionDirect *
gdm_session_direct_new (const char *display_id,
                        const char *display_name,
                        const char *display_hostname,
                        const char *display_device,
                        const char *display_seat_id,
                        const char *display_x11_authority_file,
                        gboolean    display_is_local)
{
        GdmSessionDirect *session;

        session = g_object_new (GDM_TYPE_SESSION_DIRECT,
                                "display-id", display_id,
                                "display-name", display_name,
                                "display-hostname", display_hostname,
                                "display-device", display_device,
                                "display-seat-id", display_seat_id,
                                "display-x11-authority-file", display_x11_authority_file,
                                "display-is-local", display_is_local,
                                NULL);

        return session;
}

gboolean
gdm_session_direct_restart (GdmSessionDirect *session,
                            GError          **error)
{
        gboolean ret;

        ret = TRUE;
        g_debug ("GdmSessionDirect: Request to restart session");

        return ret;
}

gboolean
gdm_session_direct_stop (GdmSessionDirect *session,
                         GError          **error)
{
        gboolean ret;

        ret = TRUE;

        g_debug ("GdmSessionDirect: Request to stop session");

        return ret;
}

gboolean
gdm_session_direct_detach (GdmSessionDirect *session,
                           GError          **error)
{
        gboolean ret;

        ret = TRUE;

        g_debug ("GdmSessionDirect: Request to detach session");

        return ret;
}
