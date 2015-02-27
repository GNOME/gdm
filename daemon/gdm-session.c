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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
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

#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-dbus-util.h"

#include "gdm-session.h"
#include "gdm-session-enum-types.h"
#include "gdm-session-worker-common.h"
#include "gdm-session-worker-job.h"
#include "gdm-session-worker-glue.h"
#include "gdm-common.h"

#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_SESSION_DBUS_ERROR_CANCEL "org.gnome.DisplayManager.Session.Error.Cancel"
#define GDM_SESSION_DBUS_OBJECT_PATH "/org/gnome/DisplayManager/Session"

#define GDM_WORKER_DBUS_PATH "/org/gnome/DisplayManager/Worker"

typedef struct
{
        GdmSession            *session;
        GdmSessionWorkerJob   *job;
        GPid                   worker_pid;
        char                  *service_name;
        GDBusMethodInvocation *starting_invocation;
        char                  *starting_username;
        GDBusMethodInvocation *pending_invocation;
        GdmDBusWorkerManager  *worker_manager_interface;
        GdmDBusWorker         *worker_proxy;
        char                  *session_id;
        guint32                is_stopping : 1;

        GPid                   reauth_pid_of_caller;
} GdmSessionConversation;

struct _GdmSessionPrivate
{
        /* per open scope */
        char                *selected_program;
        char                *selected_session;
        char                *saved_session;
        char                *saved_language;
        char                *selected_user;
        char                *user_x11_authority_file;

        char                *timed_login_username;
        int                  timed_login_delay;
        GList               *pending_timed_login_invocations;

        GHashTable          *conversations;

        GdmSessionConversation *session_conversation;

        char                 **conversation_environment;

        GdmDBusUserVerifier   *user_verifier_interface;
        GdmDBusGreeter        *greeter_interface;
        GdmDBusRemoteGreeter  *remote_greeter_interface;
        GdmDBusChooser        *chooser_interface;

        GList               *pending_worker_connections;
        GList               *outside_connections;

        GPid                 session_pid;

        /* object lifetime scope */
        char                *session_type;
        char                *display_name;
        char                *display_hostname;
        char                *display_device;
        char                *display_seat_id;
        char                *display_x11_authority_file;
        gboolean             display_is_local;

        GdmSessionVerificationMode verification_mode;

        uid_t                allowed_user;

        char                *fallback_session_name;

        GDBusServer         *worker_server;
        GDBusServer         *outside_server;
        GHashTable          *environment;

        guint32              is_program_session : 1;
        guint32              display_is_initial : 1;
};

enum {
        PROP_0,
        PROP_VERIFICATION_MODE,
        PROP_ALLOWED_USER,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_IS_INITIAL,
        PROP_SESSION_TYPE,
        PROP_DISPLAY_DEVICE,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_X11_AUTHORITY_FILE,
        PROP_USER_X11_AUTHORITY_FILE,
        PROP_CONVERSATION_ENVIRONMENT,
};

enum {
        CONVERSATION_STARTED = 0,
        CONVERSATION_STOPPED,
        SETUP_COMPLETE,
        CANCELLED,
        HOSTNAME_SELECTED,
        CLIENT_REJECTED,
        CLIENT_CONNECTED,
        CLIENT_DISCONNECTED,
        CLIENT_READY_FOR_SESSION_TO_START,
        DISCONNECTED,
        AUTHENTICATION_FAILED,
        VERIFICATION_COMPLETE,
        SESSION_OPENED,
        SESSION_STARTED,
        SESSION_START_FAILED,
        SESSION_EXITED,
        SESSION_DIED,
        REAUTHENTICATION_STARTED,
        REAUTHENTICATED,
        LAST_SIGNAL
};

#ifdef ENABLE_WAYLAND_SUPPORT
static gboolean gdm_session_is_wayland_session (GdmSession *self);
#endif
static void set_session_type (GdmSession *self,
                              const char *session_type);
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
report_and_stop_conversation (GdmSession *self,
                              const char *service_name,
                              GError     *error)
{
        g_dbus_error_strip_remote_error (error);

        if (self->priv->user_verifier_interface != NULL) {
                if (g_error_matches (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE)) {
                        gdm_dbus_user_verifier_emit_service_unavailable (self->priv->user_verifier_interface,
                                                                         service_name,
                                                                         error->message);
                } else {
                        gdm_dbus_user_verifier_emit_problem (self->priv->user_verifier_interface,
                                                             service_name,
                                                             error->message);
                }
                gdm_dbus_user_verifier_emit_verification_failed (self->priv->user_verifier_interface,
                                                                 service_name);
        }

        gdm_session_stop_conversation (self, service_name);
}

static void
on_authenticate_cb (GdmDBusWorker *proxy,
                    GAsyncResult  *res,
                    gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        gboolean worked;

        worked = gdm_dbus_worker_call_authenticate_finish (proxy, res, &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (worked) {
                gdm_session_authorize (self, service_name);
        } else {
                g_signal_emit (self,
                               signals[AUTHENTICATION_FAILED],
                               0,
                               service_name,
                               conversation->worker_pid);
                report_and_stop_conversation (self, service_name, error);
        }
}

static void
on_authorize_cb (GdmDBusWorker *proxy,
                 GAsyncResult  *res,
                 gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        gboolean worked;

        worked = gdm_dbus_worker_call_authorize_finish (proxy, res, &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (worked) {
                gdm_session_accredit (self, service_name);
        } else {
                report_and_stop_conversation (self, service_name, error);
        }
}

static void
on_establish_credentials_cb (GdmDBusWorker *proxy,
                             GAsyncResult  *res,
                             gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        gboolean worked;

        worked = gdm_dbus_worker_call_establish_credentials_finish (proxy, res, &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (worked) {
                switch (self->priv->verification_mode) {
                case GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE:
                        if (self->priv->user_verifier_interface != NULL) {
                                gdm_dbus_user_verifier_emit_verification_complete (self->priv->user_verifier_interface,
                                                                                   service_name);
                                g_signal_emit (self, signals[VERIFICATION_COMPLETE], 0, service_name);
                        }
                        break;

                case GDM_SESSION_VERIFICATION_MODE_LOGIN:
                case GDM_SESSION_VERIFICATION_MODE_CHOOSER:
                        gdm_session_open_session (self, service_name);
                        break;
                default:
                        break;
                }
        } else {
                report_and_stop_conversation (self, service_name, error);
        }
}

static const char **
get_system_session_dirs (void)
{
        static const char *search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
#ifdef ENABLE_WAYLAND_SUPPORT
                DATADIR "/wayland-sessions/",
#endif
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

static GKeyFile *
load_key_file_for_file (const char *file, char **full_path)
{
        GKeyFile   *key_file;
        GError     *error;
        gboolean    res;

        key_file = g_key_file_new ();

        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         get_system_session_dirs (),
                                         full_path,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSession: File '%s' not found: %s", file, error->message);
                g_error_free (error);
                g_key_file_free (key_file);
                key_file = NULL;
        }

        return key_file;
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

        g_debug ("GdmSession: getting session command for file '%s'", file);
        key_file = load_key_file_for_file (file, NULL);
        if (key_file == NULL) {
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

        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_emit_default_language_name_changed (self->priv->greeter_interface,
                                                                     get_default_language_name (self));
                gdm_dbus_greeter_emit_default_session_name_changed (self->priv->greeter_interface,
                                                                    get_default_session_name (self));
        }
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
gdm_session_handle_info_query (GdmDBusWorkerManager  *worker_manager_interface,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               const char            *query,
                               GdmSession            *self)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (self->priv->user_verifier_interface != NULL, FALSE);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                gdm_dbus_user_verifier_emit_info_query (self->priv->user_verifier_interface,
                                                        service_name,
                                                        query);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_secret_info_query (GdmDBusWorkerManager  *worker_manager_interface,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      const char            *query,
                                      GdmSession            *self)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (self->priv->user_verifier_interface != NULL, FALSE);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                gdm_dbus_user_verifier_emit_secret_info_query (self->priv->user_verifier_interface,
                                                               service_name,
                                                               query);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_info (GdmDBusWorkerManager  *worker_manager_interface,
                         GDBusMethodInvocation *invocation,
                         const char            *service_name,
                         const char            *info,
                         GdmSession            *self)
{
        gdm_dbus_worker_manager_complete_info (worker_manager_interface,
                                               invocation);

        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_info (self->priv->user_verifier_interface,
                                                  service_name,
                                                  info);
        }

        return TRUE;
}

static void
worker_on_cancel_pending_query (GdmDBusWorker          *worker,
                                GdmSessionConversation *conversation)
{
        cancel_pending_query (conversation);
}

static gboolean
gdm_session_handle_problem (GdmDBusWorkerManager  *worker_manager_interface,
                            GDBusMethodInvocation *invocation,
                            const char            *service_name,
                            const char            *problem,
                            GdmSession            *self)
{
        gdm_dbus_worker_manager_complete_problem (worker_manager_interface,
                                                  invocation);

        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_problem (self->priv->user_verifier_interface,
                                                     service_name,
                                                     problem);
        }
        return TRUE;
}

static void
on_opened (GdmDBusWorker *worker,
           GAsyncResult  *res,
           gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        gboolean worked;
        char *session_id;

        worked = gdm_dbus_worker_call_open_finish (worker,
                                                   &session_id,
                                                   res,
                                                   &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (worked) {
                g_clear_pointer (&conversation->session_id,
                                 (GDestroyNotify) g_free);

                conversation->session_id = g_strdup (session_id);

                if (self->priv->greeter_interface != NULL) {
                        gdm_dbus_greeter_emit_session_opened (self->priv->greeter_interface,
                                                              service_name);
                }

                if (self->priv->user_verifier_interface != NULL) {
                        gdm_dbus_user_verifier_emit_verification_complete (self->priv->user_verifier_interface,
                                                                           service_name);
                        g_signal_emit (self, signals[VERIFICATION_COMPLETE], 0, service_name);
                }

                g_debug ("GdmSession: Emitting 'session-opened' signal");
                g_signal_emit (self, signals[SESSION_OPENED], 0, service_name, session_id);
        } else {
                report_and_stop_conversation (self, service_name, error);

                g_debug ("GdmSession: Emitting 'session-start-failed' signal");
                g_signal_emit (self, signals[SESSION_START_FAILED], 0, service_name, error->message);
        }
}

static void
worker_on_username_changed (GdmDBusWorker          *worker,
                            const char             *username,
                            GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        g_debug ("GdmSession: changing username from '%s' to '%s'",
                 self->priv->selected_user != NULL ? self->priv->selected_user : "<unset>",
                 (strlen (username)) ? username : "<unset>");

        gdm_session_select_user (self, (strlen (username) > 0) ? g_strdup (username) : NULL);
        gdm_session_defaults_changed (self);
}

static void
worker_on_session_exited (GdmDBusWorker          *worker,
                          const char             *service_name,
                          int                     status,
                          GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

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
}

static void
on_reauthentication_started_cb (GdmDBusWorker *worker,
                                GAsyncResult  *res,
                                gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;

        GError *error = NULL;
        gboolean worked;
        char *address;

        worked = gdm_dbus_worker_call_start_reauthentication_finish (worker,
                                                                     &address,
                                                                     res,
                                                                     &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;

        if (worked) {
                GPid pid_of_caller = conversation->reauth_pid_of_caller;
                g_debug ("GdmSession: Emitting 'reauthentication-started' signal for caller pid '%d'", pid_of_caller);
                g_signal_emit (self, signals[REAUTHENTICATION_STARTED], 0, pid_of_caller, address);
        }

        conversation->reauth_pid_of_caller = 0;
}

static void
worker_on_reauthenticated (GdmDBusWorker          *worker,
                           const char             *service_name,
                           GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;
        g_debug ("GdmSession: Emitting 'reauthenticated' signal ");
        g_signal_emit (self, signals[REAUTHENTICATED], 0, service_name);
}

static void
worker_on_saved_language_name_read (GdmDBusWorker          *worker,
                                    const char             *language_name,
                                    GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        if (strlen (language_name) > 0 &&
            strcmp (language_name, get_default_language_name (self)) != 0) {
                g_free (self->priv->saved_language);
                self->priv->saved_language = g_strdup (language_name);

                if (self->priv->greeter_interface != NULL) {
                        gdm_dbus_greeter_emit_default_language_name_changed (self->priv->greeter_interface,
                                                                             language_name);
                }
        }
}

static void
worker_on_saved_session_name_read (GdmDBusWorker          *worker,
                                   const char             *session_name,
                                   GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        if (! get_session_command_for_name (session_name, NULL)) {
                /* ignore sessions that don't exist */
                g_debug ("GdmSession: not using invalid .dmrc session: %s", session_name);
                g_free (self->priv->saved_session);
                self->priv->saved_session = NULL;
                return;
        }

        if (strcmp (session_name,
                    get_default_session_name (self)) != 0) {
                g_free (self->priv->saved_session);
                self->priv->saved_session = g_strdup (session_name);

                if (self->priv->greeter_interface != NULL) {
                        gdm_dbus_greeter_emit_default_session_name_changed (self->priv->greeter_interface,
                                                                            session_name);
                }
        }
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
allow_worker_function (GDBusAuthObserver *observer,
                       GIOStream         *stream,
                       GCredentials      *credentials,
                       GdmSession        *self)
{
        uid_t connecting_user;

        connecting_user = g_credentials_get_unix_user (credentials, NULL);

        if (connecting_user == 0) {
                return TRUE;
        }

        if (connecting_user == self->priv->allowed_user) {
                return TRUE;
        }

        g_debug ("GdmSession: User not allowed");

        return FALSE;
}

static void
on_worker_connection_closed (GDBusConnection *connection,
                             gboolean         remote_peer_vanished,
                             GError          *error,
                             GdmSession      *self)
{
        self->priv->pending_worker_connections =
            g_list_remove (self->priv->pending_worker_connections,
                           connection);
        g_object_unref (connection);
}

static gboolean
register_worker (GdmDBusWorkerManager  *worker_manager_interface,
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
        connection_node = g_list_find (self->priv->pending_worker_connections, connection);

        if (connection_node == NULL) {
                g_debug ("GdmSession: Ignoring connection that we aren't tracking");
                return FALSE;
        }

        /* connection was ref'd when it was added to list, we're taking that
         * reference over and removing it from the list
         */
        self->priv->pending_worker_connections =
                g_list_delete_link (self->priv->pending_worker_connections,
                                    connection_node);

        g_signal_handlers_disconnect_by_func (connection,
                                              G_CALLBACK (on_worker_connection_closed),
                                              self);

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid = g_credentials_get_unix_pid (credentials, NULL);

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

        conversation->worker_proxy = gdm_dbus_worker_proxy_new_sync (connection,
                                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                                     NULL,
                                                                     GDM_WORKER_DBUS_PATH,
                                                                     NULL, NULL);
        /* drop the reference we stole from the pending connections list
         * since the proxy owns the connection now */
        g_object_unref (connection);

        g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (conversation->worker_proxy), G_MAXINT);

        g_signal_connect (conversation->worker_proxy,
                          "username-changed",
                          G_CALLBACK (worker_on_username_changed), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "session-exited",
                          G_CALLBACK (worker_on_session_exited), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "reauthenticated",
                          G_CALLBACK (worker_on_reauthenticated), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "saved-language-name-read",
                          G_CALLBACK (worker_on_saved_language_name_read), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "saved-session-name-read",
                          G_CALLBACK (worker_on_saved_session_name_read), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "cancel-pending-query",
                          G_CALLBACK (worker_on_cancel_pending_query), conversation);

        conversation->worker_manager_interface = g_object_ref (worker_manager_interface);
        g_debug ("GdmSession: worker connection is %p", connection);

        g_debug ("GdmSession: Emitting conversation-started signal");
        g_signal_emit (self, signals[CONVERSATION_STARTED], 0, conversation->service_name);

        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_started (self->priv->user_verifier_interface,
                                                                  conversation->service_name);
        }

        if (conversation->starting_invocation != NULL) {
                if (conversation->starting_username != NULL) {
                        gdm_session_setup_for_user (self, conversation->service_name, conversation->starting_username);

                        g_clear_pointer (&conversation->starting_username,
                                         (GDestroyNotify)
                                         g_free);
                } else {
                        gdm_session_setup (self, conversation->service_name);
                }
        }

        g_debug ("GdmSession: Conversation started");

        return TRUE;
}

static void
export_worker_manager_interface (GdmSession      *self,
                                 GDBusConnection *connection)
{
        GdmDBusWorkerManager *worker_manager_interface;

        worker_manager_interface = GDM_DBUS_WORKER_MANAGER (gdm_dbus_worker_manager_skeleton_new ());
        g_signal_connect (worker_manager_interface,
                          "handle-hello",
                          G_CALLBACK (register_worker),
                          self);
        g_signal_connect (worker_manager_interface,
                          "handle-info-query",
                          G_CALLBACK (gdm_session_handle_info_query),
                          self);
        g_signal_connect (worker_manager_interface,
                          "handle-secret-info-query",
                          G_CALLBACK (gdm_session_handle_secret_info_query),
                          self);
        g_signal_connect (worker_manager_interface,
                          "handle-info",
                          G_CALLBACK (gdm_session_handle_info),
                          self);
        g_signal_connect (worker_manager_interface,
                          "handle-problem",
                          G_CALLBACK (gdm_session_handle_problem),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (worker_manager_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);
}

static void
unexport_worker_manager_interface (GdmSession           *self,
                                   GdmDBusWorkerManager *worker_manager_interface)
{

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (worker_manager_interface));

        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (register_worker),
                                              self);
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_info_query),
                                              self);
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_secret_info_query),
                                              self);
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_info),
                                              self);
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_problem),
                                              self);
}

static gboolean
handle_connection_from_worker (GDBusServer      *server,
                               GDBusConnection  *connection,
                               GdmSession       *self)
{

        g_debug ("GdmSession: Handling new connection from worker");

        /* add to the list of pending connections.  We won't be able to
         * associate it with a specific worker conversation until we have
         * authenticated the connection (from the Hello handler).
         */
        self->priv->pending_worker_connections =
                g_list_prepend (self->priv->pending_worker_connections,
                                g_object_ref (connection));

        g_signal_connect_object (connection,
                                 "closed",
                                 G_CALLBACK (on_worker_connection_closed),
                                 self,
                                 0);

        export_worker_manager_interface (self, connection);

        return TRUE;
}

static GdmSessionConversation *
begin_verification_conversation (GdmSession            *self,
                                 GDBusMethodInvocation *invocation,
                                 const char            *service_name)
{
        GdmSessionConversation *conversation = NULL;
        gboolean conversation_started;

        conversation_started = gdm_session_start_conversation (self, service_name);

        if (conversation_started) {
                conversation = find_conversation_by_name (self, service_name);
        }

        if (conversation == NULL) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_SPAWN_FAILED,
                                                       _("Could not create authentication helper process"));
        }

        return conversation;
}

static gboolean
gdm_session_handle_client_begin_verification (GdmDBusUserVerifier    *user_verifier_interface,
                                              GDBusMethodInvocation  *invocation,
                                              const char             *service_name,
                                              GdmSession             *self)
{
        GdmSessionConversation *conversation;

        conversation = begin_verification_conversation (self, invocation, service_name);

        if (conversation != NULL) {
                conversation->starting_invocation = g_object_ref (invocation);
                conversation->starting_username = NULL;
        }

        return TRUE;
}

static gboolean
gdm_session_handle_client_begin_verification_for_user (GdmDBusUserVerifier    *user_verifier_interface,
                                                       GDBusMethodInvocation  *invocation,
                                                       const char             *service_name,
                                                       const char             *username,
                                                       GdmSession             *self)
{
        GdmSessionConversation *conversation;

        conversation = begin_verification_conversation (self, invocation, service_name);

        if (conversation != NULL) {
                conversation->starting_invocation = g_object_ref (invocation);
                conversation->starting_username = g_strdup (username);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_client_answer_query (GdmDBusUserVerifier    *user_verifier_interface,
                                        GDBusMethodInvocation  *invocation,
                                        const char             *service_name,
                                        const char             *answer,
                                        GdmSession             *self)
{
        gdm_dbus_user_verifier_complete_answer_query (user_verifier_interface,
                                                      invocation);
        gdm_session_answer_query (self, service_name, answer);
        return TRUE;
}

static gboolean
gdm_session_handle_client_cancel (GdmDBusUserVerifier    *user_verifier_interface,
                                  GDBusMethodInvocation  *invocation,
                                  GdmSession             *self)
{
        gdm_dbus_user_verifier_complete_cancel (user_verifier_interface,
                                                invocation);
        gdm_session_cancel (self);
        return TRUE;
}

static gboolean
gdm_session_handle_client_select_session (GdmDBusGreeter         *greeter_interface,
                                          GDBusMethodInvocation  *invocation,
                                          const char             *session,
                                          GdmSession             *self)
{
        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_select_session (greeter_interface,
                                                          invocation);
        }
        gdm_session_select_session (self, session);
        return TRUE;
}

static gboolean
gdm_session_handle_client_select_user (GdmDBusGreeter        *greeter_interface,
                                       GDBusMethodInvocation *invocation,
                                       const char            *username,
                                       GdmSession            *self)
{
        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_select_user (greeter_interface,
                                                       invocation);
        }
        gdm_session_select_user (self, username);
        return TRUE;
}

static gboolean
gdm_session_handle_client_start_session_when_ready (GdmDBusGreeter        *greeter_interface,
                                                    GDBusMethodInvocation *invocation,
                                                    const char            *service_name,
                                                    gboolean               client_is_ready,
                                                    GdmSession            *self)
{

        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_start_session_when_ready (greeter_interface,
                                                                    invocation);
        }
        g_signal_emit (G_OBJECT (self),
                       signals [CLIENT_READY_FOR_SESSION_TO_START],
                       0,
                       service_name,
                       client_is_ready);
        return TRUE;
}

static gboolean
gdm_session_handle_get_timed_login_details (GdmDBusGreeter        *greeter_interface,
                                            GDBusMethodInvocation *invocation,
                                            GdmSession            *self)
{

        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_get_timed_login_details (greeter_interface,
                                                                   invocation,
                                                                   self->priv->timed_login_username != NULL,
                                                                   self->priv->timed_login_username != NULL? self->priv->timed_login_username : "",
                                                                   self->priv->timed_login_delay);
                if (self->priv->timed_login_username != NULL) {
                        gdm_dbus_greeter_emit_timed_login_requested (self->priv->greeter_interface,
                                                                     self->priv->timed_login_username,
                                                                     self->priv->timed_login_delay);
                }
        }
        return TRUE;
}

static gboolean
gdm_session_handle_client_begin_auto_login (GdmDBusGreeter        *greeter_interface,
                                            GDBusMethodInvocation *invocation,
                                            const char            *username,
                                            GdmSession            *self)
{
        if (self->priv->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_begin_auto_login (greeter_interface,
                                                            invocation);
        }

        g_debug ("GdmSession: begin auto login for user '%s'", username);

        gdm_session_setup_for_user (self, "gdm-autologin", username);

        return TRUE;
}

static void
export_user_verifier_interface (GdmSession      *self,
                                GDBusConnection *connection)
{
        GdmDBusUserVerifier   *user_verifier_interface;
        user_verifier_interface = GDM_DBUS_USER_VERIFIER (gdm_dbus_user_verifier_skeleton_new ());
        g_signal_connect (user_verifier_interface,
                          "handle-begin-verification",
                          G_CALLBACK (gdm_session_handle_client_begin_verification),
                          self);
        g_signal_connect (user_verifier_interface,
                          "handle-begin-verification-for-user",
                          G_CALLBACK (gdm_session_handle_client_begin_verification_for_user),
                          self);
        g_signal_connect (user_verifier_interface,
                          "handle-answer-query",
                          G_CALLBACK (gdm_session_handle_client_answer_query),
                          self);
        g_signal_connect (user_verifier_interface,
                          "handle-cancel",
                          G_CALLBACK (gdm_session_handle_client_cancel),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (user_verifier_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        self->priv->user_verifier_interface = user_verifier_interface;
}

static void
export_greeter_interface (GdmSession      *self,
                          GDBusConnection *connection)
{
        GdmDBusGreeter *greeter_interface;

        greeter_interface = GDM_DBUS_GREETER (gdm_dbus_greeter_skeleton_new ());

        g_signal_connect (greeter_interface,
                          "handle-begin-auto-login",
                          G_CALLBACK (gdm_session_handle_client_begin_auto_login),
                          self);
        g_signal_connect (greeter_interface,
                          "handle-select-session",
                          G_CALLBACK (gdm_session_handle_client_select_session),
                          self);
        g_signal_connect (greeter_interface,
                          "handle-select-user",
                          G_CALLBACK (gdm_session_handle_client_select_user),
                          self);
        g_signal_connect (greeter_interface,
                          "handle-start-session-when-ready",
                          G_CALLBACK (gdm_session_handle_client_start_session_when_ready),
                          self);
        g_signal_connect (greeter_interface,
                          "handle-get-timed-login-details",
                          G_CALLBACK (gdm_session_handle_get_timed_login_details),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (greeter_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        self->priv->greeter_interface = greeter_interface;

}

static gboolean
gdm_session_handle_client_disconnect (GdmDBusChooser        *chooser_interface,
                                      GDBusMethodInvocation *invocation,
                                      GdmSession            *self)
{
        gdm_dbus_chooser_complete_disconnect (chooser_interface,
                                              invocation);
        g_signal_emit (self, signals[DISCONNECTED], 0);
        return TRUE;
}

static void
export_remote_greeter_interface (GdmSession      *self,
                                 GDBusConnection *connection)
{
        GdmDBusRemoteGreeter *remote_greeter_interface;

        remote_greeter_interface = GDM_DBUS_REMOTE_GREETER (gdm_dbus_remote_greeter_skeleton_new ());

        g_signal_connect (remote_greeter_interface,
                          "handle-disconnect",
                          G_CALLBACK (gdm_session_handle_client_disconnect),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (remote_greeter_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        self->priv->remote_greeter_interface = remote_greeter_interface;

}

static gboolean
gdm_session_handle_client_select_hostname (GdmDBusChooser        *chooser_interface,
                                           GDBusMethodInvocation *invocation,
                                           const char            *hostname,
                                           GdmSession            *self)
{

        gdm_dbus_chooser_complete_select_hostname (chooser_interface,
                                                   invocation);
        g_signal_emit (self, signals[HOSTNAME_SELECTED], 0, hostname);
        return TRUE;
}

static void
export_chooser_interface (GdmSession      *self,
                          GDBusConnection *connection)
{
        GdmDBusChooser *chooser_interface;

        chooser_interface = GDM_DBUS_CHOOSER (gdm_dbus_chooser_skeleton_new ());

        g_signal_connect (chooser_interface,
                          "handle-select-hostname",
                          G_CALLBACK (gdm_session_handle_client_select_hostname),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (chooser_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        self->priv->chooser_interface = chooser_interface;
}

static void
on_outside_connection_closed (GDBusConnection *connection,
                              gboolean         remote_peer_vanished,
                              GError          *error,
                              GdmSession      *self)
{
        GCredentials *credentials;
        GPid          pid_of_client;

        g_debug ("GdmSession: external connection closed");

        self->priv->outside_connections =
            g_list_remove (self->priv->outside_connections,
                            connection);

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid_of_client = g_credentials_get_unix_pid (credentials, NULL);

        g_signal_emit (G_OBJECT (self),
                       signals [CLIENT_DISCONNECTED],
                       0,
                       credentials,
                       (guint)
                       pid_of_client);

        g_object_unref (connection);
}

static gboolean
handle_connection_from_outside (GDBusServer      *server,
                                GDBusConnection  *connection,
                                GdmSession       *self)
{
        GCredentials *credentials;
        GPid          pid_of_client;

        g_debug ("GdmSession: Handling new connection from outside");

        self->priv->outside_connections =
            g_list_prepend (self->priv->outside_connections,
                            g_object_ref (connection));

        g_signal_connect_object (connection,
                                 "closed",
                                 G_CALLBACK (on_outside_connection_closed),
                                 self,
                                 0);

        export_user_verifier_interface (self, connection);

        switch (self->priv->verification_mode) {
                case GDM_SESSION_VERIFICATION_MODE_LOGIN:
                        export_greeter_interface (self, connection);
                break;

                case GDM_SESSION_VERIFICATION_MODE_CHOOSER:
                        export_chooser_interface (self, connection);
                break;

                default:
                break;
        }

        if (!self->priv->display_is_local) {
                export_remote_greeter_interface (self, connection);
        }

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid_of_client = g_credentials_get_unix_pid (credentials, NULL);

        g_signal_emit (G_OBJECT (self),
                       signals [CLIENT_CONNECTED],
                       0,
                       credentials,
                       (guint)
                       pid_of_client);

        return TRUE;
}

static void
setup_worker_server (GdmSession *self)
{
        GDBusAuthObserver *observer;
        GDBusServer *server;
        GError *error = NULL;

        g_debug ("GdmSession: Creating D-Bus server for worker for session");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect_object (observer,
                                 "authorize-authenticated-peer",
                                 G_CALLBACK (allow_worker_function),
                                 self,
                                 0);

        server = gdm_dbus_setup_private_server (observer, &error);
        g_object_unref (observer);

        if (server == NULL) {
                g_warning ("Cannot create worker D-Bus server for the session: %s",
                           error->message);
                return;
        }

        g_signal_connect_object (server,
                                 "new-connection",
                                 G_CALLBACK (handle_connection_from_worker),
                                 self,
                                 0);
        self->priv->worker_server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSession: D-Bus server for workers listening on %s",
        g_dbus_server_get_client_address (self->priv->worker_server));
}

static gboolean
allow_user_function (GDBusAuthObserver *observer,
                     GIOStream         *stream,
                     GCredentials      *credentials,
                     GdmSession        *self)
{
        uid_t client_uid;
        GPid  pid_of_client;

        client_uid = g_credentials_get_unix_user (credentials, NULL);
        if (client_uid == self->priv->allowed_user) {
                return TRUE;
        }

        g_debug ("GdmSession: User not allowed");

        pid_of_client = g_credentials_get_unix_pid (credentials, NULL);
        g_signal_emit (G_OBJECT (self),
                       signals [CLIENT_REJECTED],
                       0,
                       credentials,
                       (guint)
                       pid_of_client);


        return FALSE;
}

static void
setup_outside_server (GdmSession *self)
{
        GDBusAuthObserver *observer;
        GDBusServer *server;
        GError *error = NULL;

        g_debug ("GdmSession: Creating D-Bus server for greeters and such");

        observer = g_dbus_auth_observer_new ();
        g_signal_connect_object (observer,
                                 "authorize-authenticated-peer",
                                 G_CALLBACK (allow_user_function),
                                 self,
                                 0);

        server = gdm_dbus_setup_private_server (observer, &error);
        g_object_unref (observer);

        if (server == NULL) {
                g_warning ("Cannot create greeter D-Bus server for the session: %s",
                           error->message);
                return;
        }

        g_signal_connect_object (server,
                                 "new-connection",
                                 G_CALLBACK (handle_connection_from_outside),
                                 self,
                                 0);
        self->priv->outside_server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSession: D-Bus server for greeters listening on %s",
        g_dbus_server_get_client_address (self->priv->outside_server));
}

static void
free_conversation (GdmSessionConversation *conversation)
{
        if (conversation->job != NULL) {
                g_warning ("Freeing conversation '%s' with active job", conversation->service_name);
        }

        g_free (conversation->service_name);
        g_free (conversation->starting_username);
        g_free (conversation->session_id);
        g_clear_object (&conversation->worker_manager_interface);
        g_clear_object (&conversation->worker_proxy);
        g_clear_object (&conversation->session);
        g_free (conversation);
}

static void
gdm_session_init (GdmSession *self)
{
        self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                  GDM_TYPE_SESSION,
                                                  GdmSessionPrivate);

        self->priv->conversations = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           (GDestroyNotify) g_free,
                                                           (GDestroyNotify)
                                                           free_conversation);
        self->priv->environment = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         (GDestroyNotify) g_free,
                                                         (GDestroyNotify) g_free);

        setup_worker_server (self);
        setup_outside_server (self);
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

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_EXITED], 0, code);
                self->priv->session_conversation = NULL;
        }

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_stopped (self->priv->user_verifier_interface,
                                                                  conversation->service_name);
        }
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

        g_hash_table_steal (self->priv->conversations, conversation->service_name);

        g_object_ref (conversation->job);
        if (self->priv->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_DIED], 0, signum);
                self->priv->session_conversation = NULL;
        }

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_stopped (self->priv->user_verifier_interface,
                                                                  conversation->service_name);
        }
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
        conversation->session = g_object_ref (self);
        conversation->service_name = g_strdup (service_name);
        conversation->worker_pid = -1;
        conversation->job = gdm_session_worker_job_new ();
        gdm_session_worker_job_set_server_address (conversation->job,
                                                   g_dbus_server_get_client_address (self->priv->worker_server));
        gdm_session_worker_job_set_for_reauth (conversation->job,
                                               self->priv->verification_mode == GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE);

        if (self->priv->conversation_environment != NULL) {
                gdm_session_worker_job_set_environment (conversation->job,
                                                        (const char * const *)
                                                        self->priv->conversation_environment);

        }
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
close_conversation (GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        if (conversation->worker_manager_interface != NULL) {
                unexport_worker_manager_interface (self, conversation->worker_manager_interface);
                g_clear_object (&conversation->worker_manager_interface);
        }

        if (conversation->worker_proxy != NULL) {
                GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (conversation->worker_proxy));
                g_dbus_connection_close_sync (connection, NULL, NULL);
        }
}

static void
stop_conversation (GdmSessionConversation *conversation)
{
        close_conversation (conversation);

        conversation->is_stopping = TRUE;
        gdm_session_worker_job_stop (conversation->job);
}

static void
stop_conversation_now (GdmSessionConversation *conversation)
{
        close_conversation (conversation);

        gdm_session_worker_job_stop_now (conversation->job);
        g_clear_object (&conversation->job);
}

gboolean
gdm_session_start_conversation (GdmSession *self,
                                const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        conversation = g_hash_table_lookup (self->priv->conversations,
                                            service_name);

        if (conversation != NULL) {
                if (!conversation->is_stopping) {
                        g_warning ("GdmSession: conversation %s started more than once", service_name);
                        return FALSE;
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
        return TRUE;
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
on_setup_complete_cb (GdmDBusWorker *proxy,
                      GAsyncResult  *res,
                      gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        GVariant *ret;

        ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (ret != NULL) {
                if (conversation->starting_invocation) {
                        g_dbus_method_invocation_return_value (conversation->starting_invocation,
                                                               NULL);
                }

                g_signal_emit (G_OBJECT (self),
                               signals [SETUP_COMPLETE],
                               0,
                               service_name);

                gdm_session_authenticate (self, service_name);
                g_variant_unref (ret);

        } else {
                g_dbus_method_invocation_return_gerror (conversation->starting_invocation, error);
                report_and_stop_conversation (self, service_name, error);
                g_error_free (error);
        }

        g_clear_object (&conversation->starting_invocation);
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
                gdm_dbus_worker_call_setup (conversation->worker_proxy,
                                            service_name,
                                            display_name,
                                            display_x11_authority_file,
                                            display_device,
                                            display_seat_id,
                                            display_hostname,
                                            self->priv->display_is_local,
                                            self->priv->display_is_initial,
                                            NULL,
                                            (GAsyncReadyCallback) on_setup_complete_cb,
                                            conversation);
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
                gdm_dbus_worker_call_setup_for_user (conversation->worker_proxy,
                                                     service_name,
                                                     selected_user,
                                                     display_name,
                                                     display_x11_authority_file,
                                                     display_device,
                                                     display_seat_id,
                                                     display_hostname,
                                                     self->priv->display_is_local,
                                                     self->priv->display_is_initial,
                                                     NULL,
                                                     (GAsyncReadyCallback) on_setup_complete_cb,
                                                     conversation);
        }
}

static void
send_setup_for_program (GdmSession *self,
                        const char *service_name,
                        const char *username,
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

        g_debug ("GdmSession: Beginning setup for session for program using PAM service %s", service_name);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_setup_for_program (conversation->worker_proxy,
                                                        service_name,
                                                        username,
                                                        display_name,
                                                        display_x11_authority_file,
                                                        display_device,
                                                        display_seat_id,
                                                        display_hostname,
                                                        self->priv->display_is_local,
                                                        self->priv->display_is_initial,
                                                        log_file,
                                                        NULL,
                                                        (GAsyncReadyCallback) on_setup_complete_cb,
                                                        conversation);
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


void
gdm_session_setup_for_user (GdmSession *self,
                            const char *service_name,
                            const char *username)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (username != NULL);

        gdm_session_select_user (self, username);

        self->priv->is_program_session = FALSE;
        send_setup_for_user (self, service_name);
        gdm_session_defaults_changed (self);
}

void
gdm_session_setup_for_program (GdmSession *self,
                               const char *service_name,
                               const char *username,
                               const char *log_file)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        self->priv->is_program_session = TRUE;
        send_setup_for_program (self, service_name, username, log_file);
}

void
gdm_session_authenticate (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_authenticate (conversation->worker_proxy,
                                                   NULL,
                                                   (GAsyncReadyCallback) on_authenticate_cb,
                                                   conversation);
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
                gdm_dbus_worker_call_authorize (conversation->worker_proxy,
                                                NULL,
                                                (GAsyncReadyCallback) on_authorize_cb,
                                                conversation);
        }
}

void
gdm_session_accredit (GdmSession *self,
                      const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_establish_credentials (conversation->worker_proxy,
                                                            NULL,
                                                            (GAsyncReadyCallback) on_establish_credentials_cb,
                                                            conversation);
        }

}

static void
send_environment_variable (const char             *key,
                           const char             *value,
                           GdmSessionConversation *conversation)
{
        gdm_dbus_worker_call_set_environment_variable (conversation->worker_proxy,
                                                       key, value,
                                                       NULL, NULL, NULL);
}

static void
send_environment (GdmSession             *self,
                  GdmSessionConversation *conversation)
{

        g_hash_table_foreach (self->priv->environment,
                              (GHFunc) send_environment_variable,
                              conversation);
}

void
gdm_session_send_environment (GdmSession *self,
                              const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                send_environment (self, conversation);
        }
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

static gchar *
get_session_desktop_names (GdmSession *self)
{
        gchar *filename;
        GKeyFile *keyfile;
        gchar *desktop_names = NULL;

        filename = g_strdup_printf ("%s.desktop", get_session_name (self));
        g_debug ("GdmSession: getting desktop names for file '%s'", filename);
        keyfile = load_key_file_for_file (filename, NULL);
        if (keyfile != NULL) {
              gchar **names;

              names = g_key_file_get_string_list (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                                                  "DesktopNames", NULL, NULL);
              if (names != NULL) {
                      desktop_names = g_strjoinv (":", names);

                      g_strfreev (names);
              }
        }

        g_key_file_free (keyfile);
        g_free (filename);
        return desktop_names;
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
set_up_session_language (GdmSession *self)
{
        char **environment;
        int i;
        const char *value;

        environment = g_listenv ();
        for (i = 0; environment[i] != NULL; i++) {
                if (strcmp (environment[i], "LANG") != 0 &&
                    strcmp (environment[i], "LANGUAGE") != 0 &&
                    !g_str_has_prefix (environment[i], "LC_")) {
                    continue;
                }

                value = g_getenv (environment[i]);

                gdm_session_set_environment_variable (self,
                                                      environment[i],
                                                      value);
        }
        g_strfreev (environment);
}

static void
set_up_session_environment (GdmSession *self)
{
        GdmSessionDisplayMode display_mode;
        gchar *desktop_names;
        const char *locale;

        gdm_session_set_environment_variable (self,
                                              "GDMSESSION",
                                              get_session_name (self));
        gdm_session_set_environment_variable (self,
                                              "DESKTOP_SESSION",
                                              get_session_name (self));
        gdm_session_set_environment_variable (self,
                                              "XDG_SESSION_DESKTOP",
                                              get_session_name (self));

        desktop_names = get_session_desktop_names (self);
        if (desktop_names != NULL) {
                gdm_session_set_environment_variable (self, "XDG_CURRENT_DESKTOP", desktop_names);
        }

        set_up_session_language (self);

        locale = get_default_language_name (self);

        if (locale != NULL && locale[0] != '\0') {
                gdm_session_set_environment_variable (self,
                                                      "LANG",
                                                      locale);
                gdm_session_set_environment_variable (self,
                                                      "GDM_LANG",
                                                      locale);
        }

        display_mode = gdm_session_get_display_mode (self);
        if (display_mode == GDM_SESSION_DISPLAY_MODE_REUSE_VT) {
                gdm_session_set_environment_variable (self,
                                                      "DISPLAY",
                                                      self->priv->display_name);

                if (self->priv->user_x11_authority_file != NULL) {
                        gdm_session_set_environment_variable (self,
                                                              "XAUTHORITY",
                                                              self->priv->user_x11_authority_file);
                }
        }

        if (g_getenv ("WINDOWPATH") != NULL) {
                gdm_session_set_environment_variable (self,
                                                      "WINDOWPATH",
                                                      g_getenv ("WINDOWPATH"));
        }

        g_free (desktop_names);
}

static void
send_display_mode (GdmSession *self,
                   GdmSessionConversation *conversation)
{
        GdmSessionDisplayMode mode;

        mode = gdm_session_get_display_mode (self);
        gdm_dbus_worker_call_set_session_display_mode (conversation->worker_proxy,
                                                       gdm_session_display_mode_to_string (mode),
                                                       NULL, NULL, NULL);
}

static void
send_session_type (GdmSession *self,
                   GdmSessionConversation *conversation)
{
        const char *session_type = "x11";

        if (self->priv->session_type != NULL) {
                session_type = self->priv->session_type;
        }

        gdm_dbus_worker_call_set_environment_variable (conversation->worker_proxy,
                                                       "XDG_SESSION_TYPE",
                                                       session_type,
                                                       NULL, NULL, NULL);
}

void
gdm_session_open_session (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                send_display_mode (self, conversation);
                send_session_type (self, conversation);

                gdm_dbus_worker_call_open (conversation->worker_proxy,
                                           NULL,
                                           (GAsyncReadyCallback) on_opened, conversation);
        }
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

static void
on_start_program_cb (GdmDBusWorker *worker,
                     GAsyncResult  *res,
                     gpointer       user_data)
{
        GdmSessionConversation *conversation = user_data;
        GdmSession *self;
        char *service_name;

        GError *error = NULL;
        gboolean worked;
        GPid pid;

        worked = gdm_dbus_worker_call_start_program_finish (worker,
                                                            &pid,
                                                            res,
                                                            &error);

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
            g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;

        self = conversation->session;
        service_name = conversation->service_name;

        if (worked) {
                self->priv->session_pid = pid;
                self->priv->session_conversation = conversation;

                g_debug ("GdmSession: Emitting 'session-started' signal with pid '%d'", pid);
                g_signal_emit (self, signals[SESSION_STARTED], 0, service_name, pid);
        } else {
                gdm_session_stop_conversation (self, service_name);

                g_debug ("GdmSession: Emitting 'session-start-failed' signal");
                g_signal_emit (self, signals[SESSION_START_FAILED], 0, service_name, error->message);
        }
}

void
gdm_session_start_session (GdmSession *self,
                           const char *service_name)
{
        GdmSessionConversation *conversation;
        GdmSessionDisplayMode   display_mode;
        gboolean                is_x11 = TRUE;
        gboolean                run_launcher = FALSE;
        gboolean                allow_remote_connections = FALSE;
        char                   *command;
        char                   *program;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (self->priv->session_conversation == NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation == NULL) {
                g_warning ("GdmSession: Tried to start session of "
                           "nonexistent conversation %s", service_name);
                return;
        }

        stop_all_other_conversations (self, conversation, FALSE);

        display_mode = gdm_session_get_display_mode (self);

#ifdef ENABLE_WAYLAND_SUPPORT
        is_x11 = g_strcmp0 (self->priv->session_type, "wayland") != 0;
#endif

        if (display_mode == GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED ||
            display_mode == GDM_SESSION_DISPLAY_MODE_NEW_VT) {
                run_launcher = TRUE;
        }

        if (self->priv->selected_program == NULL) {
                gboolean run_xsession_script;

                command = get_session_command (self);

                run_xsession_script = !gdm_session_bypasses_xsession (self);

                if (self->priv->display_is_local) {
                        gboolean disallow_tcp = TRUE;
                        gdm_settings_direct_get_boolean (GDM_KEY_DISALLOW_TCP, &disallow_tcp);
                        allow_remote_connections = !disallow_tcp;
                } else {
                        allow_remote_connections = TRUE;
                }

                if (run_launcher) {
                        if (is_x11) {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-x-session %s %s\"%s\"",
                                                           run_xsession_script? "--run-script " : "",
                                                           allow_remote_connections? "--allow-remote-connections " : "",
                                                           command);
                        } else {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-wayland-session \"%s\"",
                                                           command);
                        }
                } else if (run_xsession_script) {
                        program = g_strdup_printf (GDMCONFDIR "/Xsession \"%s\"", command);
                } else {
                        program = g_strdup (command);
                }

                g_free (command);
        } else {
                if (run_launcher) {
                        if (is_x11) {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-x-session \"%s\"",
                                                           self->priv->selected_program);
                        } else {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-wayland-session \"%s\"",
                                                           self->priv->selected_program);
                        }
                } else {
                        program = g_strdup (self->priv->selected_program);
                }
        }

        set_up_session_environment (self);
        send_environment (self, conversation);

        gdm_dbus_worker_call_start_program (conversation->worker_proxy,
                                            program,
                                            NULL,
                                            (GAsyncReadyCallback) on_start_program_cb,
                                            conversation);
        g_free (program);
}

static void
stop_all_conversations (GdmSession *self)
{
        stop_all_other_conversations (self, NULL, TRUE);
}

static void
do_reset (GdmSession *self)
{
        stop_all_conversations (self);

        g_list_free_full (self->priv->pending_worker_connections, g_object_unref);
        self->priv->pending_worker_connections = NULL;

        g_free (self->priv->selected_user);
        self->priv->selected_user = NULL;

        g_free (self->priv->selected_session);
        self->priv->selected_session = NULL;

        g_free (self->priv->saved_session);
        self->priv->saved_session = NULL;

        g_free (self->priv->saved_language);
        self->priv->saved_language = NULL;

        g_free (self->priv->user_x11_authority_file);
        self->priv->user_x11_authority_file = NULL;

        g_hash_table_remove_all (self->priv->environment);

        self->priv->session_pid = -1;
        self->priv->session_conversation = NULL;
}

void
gdm_session_close (GdmSession *self)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        g_debug ("GdmSession: Closing session");
        do_reset (self);

        g_list_free_full (self->priv->outside_connections, g_object_unref);
        self->priv->outside_connections = NULL;
}

void
gdm_session_answer_query (GdmSession *self,
                          const char *service_name,
                          const char *text)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                answer_pending_query (conversation, text);
        }
}

void
gdm_session_cancel  (GdmSession *self)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_signal_emit (G_OBJECT (self), signals [CANCELLED], 0);
}

void
gdm_session_reset (GdmSession *self)
{
        if (self->priv->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_reset (self->priv->user_verifier_interface);
        }

        do_reset (self);
}

void
gdm_session_set_timed_login_details (GdmSession *self,
                                     const char *username,
                                     int         delay)
{
        g_debug ("GdmSession: timed login details %s %d", username, delay);
        self->priv->timed_login_username = g_strdup (username);
        self->priv->timed_login_delay = delay;
}

gboolean
gdm_session_is_running (GdmSession *self)
{
        return self->priv->session_pid > 0;
}

gboolean
gdm_session_client_is_connected (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        return self->priv->outside_connections != NULL;
}

uid_t
gdm_session_get_allowed_user (GdmSession *self)
{
        return self->priv->allowed_user;
}

void
gdm_session_start_reauthentication (GdmSession *session,
                                    GPid        pid_of_caller,
                                    uid_t       uid_of_caller)
{
        GdmSessionConversation *conversation = session->priv->session_conversation;

        g_return_if_fail (conversation != NULL);

        conversation->reauth_pid_of_caller = pid_of_caller;

        gdm_dbus_worker_call_start_reauthentication (conversation->worker_proxy,
                                                     (int) pid_of_caller,
                                                     (int) uid_of_caller,
                                                     NULL,
                                                     (GAsyncReadyCallback) on_reauthentication_started_cb,
                                                     conversation);
}

const char *
gdm_session_get_server_address (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_dbus_server_get_client_address (self->priv->outside_server);
}

const char *
gdm_session_get_username (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return self->priv->selected_user;
}

const char *
gdm_session_get_display_device (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return self->priv->display_device;
}

const char *
gdm_session_get_display_seat_id (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_strdup (self->priv->display_seat_id);
}

const char *
gdm_session_get_session_id (GdmSession *self)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        conversation = self->priv->session_conversation;

        if (conversation == NULL) {
                return NULL;
        }

        return conversation->session_id;
}

const char *
gdm_session_get_conversation_session_id (GdmSession *self,
                                         const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation == NULL) {
                return NULL;
        }

        return conversation->session_id;
}

static char *
get_session_filename (GdmSession *self)
{
        return g_strdup_printf ("%s.desktop", get_session_name (self));
}

#ifdef ENABLE_WAYLAND_SUPPORT
static gboolean
gdm_session_is_wayland_session (GdmSession *self)
{
        GKeyFile   *key_file;
        gboolean    is_wayland_session = FALSE;
        char       *filename;
        char       *full_path = NULL;

        g_return_val_if_fail (self != NULL, FALSE);
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        filename = get_session_filename (self);

        key_file = load_key_file_for_file (filename, &full_path);

        if (key_file == NULL) {
                goto out;
        }

        if (full_path != NULL && strstr (full_path, "/wayland-sessions/") != NULL) {
                is_wayland_session = TRUE;
        }
        g_debug ("GdmSession: checking if file '%s' is wayland session: %s", filename, is_wayland_session? "yes" : "no");

out:
        g_clear_pointer (&key_file, (GDestroyNotify) g_key_file_free);
        g_free (filename);
        return is_wayland_session;
}
#endif

gboolean
gdm_session_bypasses_xsession (GdmSession *self)
{
        GError     *error;
        GKeyFile   *key_file;
        gboolean    res;
        gboolean    bypasses_xsession = FALSE;
        char       *filename = NULL;

        g_return_val_if_fail (self != NULL, FALSE);
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

#ifdef ENABLE_WAYLAND_SUPPORT
        if (gdm_session_is_wayland_session (self)) {
                bypasses_xsession = TRUE;
                goto out;
        }
#endif

        filename = get_session_filename (self);

        key_file = load_key_file_for_file (filename, NULL);

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
        }

out:
        if (bypasses_xsession) {
                g_debug ("GdmSession: Session %s bypasses Xsession wrapper script", filename);
        }
        g_free (filename);
        return bypasses_xsession;
}

GdmSessionDisplayMode
gdm_session_get_display_mode (GdmSession *self)
{
#ifdef ENABLE_WAYLAND_SUPPORT
        /* Wayland sessions are for now assumed to run in a
         * mutter-launch-like environment, so we allocate
         * a new VT for them. */
        if (g_strcmp0 (self->priv->session_type, "wayland") == 0) {
                return GDM_SESSION_DISPLAY_MODE_NEW_VT;
        }
#endif

        /* Non-seat0 sessions share their X server with their login screen
         * for now.
         */
        if (g_strcmp0 (self->priv->display_seat_id, "seat0") != 0) {
                return GDM_SESSION_DISPLAY_MODE_REUSE_VT;
        }

        /* The X session used for the login screen now is run
         * within the login session and managed by logind
         */
        if (self->priv->is_program_session) {
                return GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED;
        }

        /* user based X sessions need us to allocate a VT for them
         * and jump to it up front, because the X servers logind support
         * currently relies on X running in the foreground VT.
         */
        return GDM_SESSION_DISPLAY_MODE_NEW_VT;
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

        g_debug ("GdmSession: selecting session type '%s'", text);

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                gdm_dbus_worker_call_set_session_type (conversation->worker_proxy,
                                                       text,
                                                       NULL, NULL, NULL);
        }
}

void
gdm_session_select_session (GdmSession *self,
                            const char *text)
{
        GHashTableIter iter;
        gpointer key, value;
        gboolean is_wayland_session = FALSE;

        g_debug ("GdmSession: selecting session '%s'", text);

        g_free (self->priv->selected_session);
        self->priv->selected_session = g_strdup (text);

#ifdef ENABLE_WAYLAND_SUPPORT
        is_wayland_session = gdm_session_is_wayland_session (self);
        if (is_wayland_session) {
                set_session_type (self, "wayland");
        } else {
                set_session_type (self, NULL);
        }
#endif

        g_hash_table_iter_init (&iter, self->priv->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                gdm_dbus_worker_call_set_session_name (conversation->worker_proxy,
                                                       get_session_name (self),
                                                       NULL, NULL, NULL);
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
set_display_is_initial (GdmSession *self,
                        gboolean    is_initial)
{
        self->priv->display_is_initial = is_initial;
}

static void
set_verification_mode (GdmSession                 *self,
                       GdmSessionVerificationMode  verification_mode)
{
        self->priv->verification_mode = verification_mode;
}

static void
set_allowed_user (GdmSession *self,
                  uid_t       allowed_user)
{
        self->priv->allowed_user = allowed_user;
}

static void
set_conversation_environment (GdmSession  *self,
                              char       **environment)
{
        g_strfreev (self->priv->conversation_environment);
        self->priv->conversation_environment = g_strdupv (environment);
}

static void
set_session_type (GdmSession *self,
                  const char *session_type)
{

        g_debug ("GdmSession: setting session to type '%s'", session_type? session_type : "");
        g_free (self->priv->session_type);
        self->priv->session_type = g_strdup (session_type);
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
        case PROP_SESSION_TYPE:
                set_session_type (self, g_value_get_string (value));
                break;
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
        case PROP_DISPLAY_IS_INITIAL:
                set_display_is_initial (self, g_value_get_boolean (value));
                break;
        case PROP_VERIFICATION_MODE:
                set_verification_mode (self, g_value_get_enum (value));
                break;
        case PROP_ALLOWED_USER:
                set_allowed_user (self, g_value_get_uint (value));
                break;
        case PROP_CONVERSATION_ENVIRONMENT:
                set_conversation_environment (self, g_value_get_pointer (value));
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
        case PROP_SESSION_TYPE:
                g_value_set_string (value, self->priv->session_type);
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
        case PROP_DISPLAY_IS_INITIAL:
                g_value_set_boolean (value, self->priv->display_is_initial);
                break;
        case PROP_VERIFICATION_MODE:
                g_value_set_enum (value, self->priv->verification_mode);
                break;
        case PROP_ALLOWED_USER:
                g_value_set_uint (value, self->priv->allowed_user);
                break;
        case PROP_CONVERSATION_ENVIRONMENT:
                g_value_set_pointer (value, self->priv->environment);
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

        g_clear_object (&self->priv->user_verifier_interface);
        g_clear_object (&self->priv->greeter_interface);
        g_clear_object (&self->priv->chooser_interface);

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

        g_strfreev (self->priv->conversation_environment);
        self->priv->conversation_environment = NULL;

        if (self->priv->worker_server != NULL) {
                g_dbus_server_stop (self->priv->worker_server);
                g_clear_object (&self->priv->worker_server);
        }

        if (self->priv->outside_server != NULL) {
                g_dbus_server_stop (self->priv->outside_server);
                g_clear_object (&self->priv->outside_server);
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

        signals [AUTHENTICATION_FAILED] =
                g_signal_new ("authentication-failed",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, authentication_failed),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_INT);
        signals [VERIFICATION_COMPLETE] =
                g_signal_new ("verification-complete",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, verification_complete),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SESSION_OPENED] =
                g_signal_new ("session-opened",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, session_opened),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_STRING);
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
                              G_TYPE_STRING,
                              G_TYPE_INT);
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

        signals [REAUTHENTICATION_STARTED] =
                g_signal_new ("reauthentication-started",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, reauthentication_started),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_INT,
                              G_TYPE_STRING);
        signals [REAUTHENTICATED] =
                g_signal_new ("reauthenticated",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, reauthenticated),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [CANCELLED] =
                g_signal_new ("cancelled",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, cancelled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [CLIENT_REJECTED] =
                g_signal_new ("client-rejected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, client_rejected),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_CREDENTIALS,
                              G_TYPE_UINT);

        signals [CLIENT_CONNECTED] =
                g_signal_new ("client-connected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, client_connected),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_CREDENTIALS,
                              G_TYPE_UINT);

        signals [CLIENT_DISCONNECTED] =
                g_signal_new ("client-disconnected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, client_disconnected),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_CREDENTIALS,
                              G_TYPE_UINT);
        signals [CLIENT_READY_FOR_SESSION_TO_START] =
                g_signal_new ("client-ready-for-session-to-start",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, client_ready_for_session_to_start),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);

        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, disconnected),
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_VERIFICATION_MODE,
                                         g_param_spec_enum ("verification-mode",
                                                            "verification mode",
                                                            "verification mode",
                                                            GDM_TYPE_SESSION_VERIFICATION_MODE,
                                                            GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_ALLOWED_USER,
                                         g_param_spec_uint ("allowed-user",
                                                            "allowed user",
                                                            "allowed user ",
                                                            0,
                                                            G_MAXUINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_CONVERSATION_ENVIRONMENT,
                                         g_param_spec_pointer ("conversation-environment",
                                                               "conversation environment",
                                                               "conversation environment",
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_TYPE,
                                         g_param_spec_string ("session-type",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
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
                                         PROP_DISPLAY_IS_INITIAL,
                                         g_param_spec_boolean ("display-is-initial",
                                                               "display is initial",
                                                               "display is initial",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
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
gdm_session_new (GdmSessionVerificationMode  verification_mode,
                 uid_t                       allowed_user,
                 const char                 *display_name,
                 const char                 *display_hostname,
                 const char                 *display_device,
                 const char                 *display_seat_id,
                 const char                 *display_x11_authority_file,
                 gboolean                    display_is_local,
                 const char * const         *environment)
{
        GdmSession *self;

        self = g_object_new (GDM_TYPE_SESSION,
                             "verification-mode", verification_mode,
                             "allowed-user", (guint) allowed_user,
                             "display-name", display_name,
                             "display-hostname", display_hostname,
                             "display-device", display_device,
                             "display-seat-id", display_seat_id,
                             "display-x11-authority-file", display_x11_authority_file,
                             "display-is-local", display_is_local,
                             "conversation-environment", environment,
                             NULL);

        return self;
}

GdmSessionDisplayMode
gdm_session_display_mode_from_string (const char *str)
{
        if (strcmp (str, "reuse-vt") == 0)
                return GDM_SESSION_DISPLAY_MODE_REUSE_VT;
        if (strcmp (str, "new-vt") == 0)
                return GDM_SESSION_DISPLAY_MODE_NEW_VT;
        if (strcmp (str, "logind-managed") == 0)
                return GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED;

        g_warning ("Unknown GdmSessionDisplayMode %s", str);
        return -1;
}

const char *
gdm_session_display_mode_to_string (GdmSessionDisplayMode mode)
{
        switch (mode) {
        case GDM_SESSION_DISPLAY_MODE_REUSE_VT:
                return "reuse-vt";
        case GDM_SESSION_DISPLAY_MODE_NEW_VT:
                return "new-vt";
        case GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED:
                return "logind-managed";
        default:
                break;
        }

        g_warning ("Unknown GdmSessionDisplayMode %d", mode);
        return "";
}

GPid
gdm_session_get_pid (GdmSession *session)
{
        return session->priv->session_pid;
}
