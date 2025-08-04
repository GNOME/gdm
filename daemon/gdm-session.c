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

#include <json-glib/json-glib.h>

#include <systemd/sd-login.h>

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
        GCancellable          *worker_cancellable;
        char                  *session_id;
        guint32                is_stopping : 1;

        GPid                   reauth_pid_of_caller;
} GdmSessionConversation;

struct _GdmSession
{
        GObject              parent;

        /* per open scope */
        char                *selected_program;
        char                *selected_session;
        char                *saved_session;
        char                *saved_session_type;
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
        GHashTable            *user_verifier_extensions;
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

        GStrv                supported_session_types;

        char                *remote_id;

        guint32              is_program_session : 1;
        guint32              display_is_initial : 1;
        guint32              is_opened : 1;
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
        PROP_SUPPORTED_SESSION_TYPES,
        PROP_REMOTE_ID,
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
        CREDENTIALS_ESTABLISHED,
        VERIFICATION_COMPLETE,
        SESSION_OPENED,
        SESSION_OPENED_FAILED,
        SESSION_STARTED,
        SESSION_START_FAILED,
        SESSION_EXITED,
        SESSION_DIED,
        REAUTHENTICATION_STARTED,
        REAUTHENTICATED,
        STOP_CONFLICTING_SESSION,
        LAST_SIGNAL
};

#ifdef ENABLE_WAYLAND_SUPPORT
static gboolean gdm_session_is_wayland_session (GdmSession *self);
#endif
static void update_session_type (GdmSession *self);
static void set_session_type (GdmSession *self,
                              const char *session_type);
static void close_conversation (GdmSessionConversation *conversation);

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GdmSession,
               gdm_session,
               G_TYPE_OBJECT);

static GdmSessionConversation *
find_conversation_by_name (GdmSession *self,
                           const char *service_name)
{
        GdmSessionConversation *conversation;

        conversation = g_hash_table_lookup (self->conversations, service_name);

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

        if (self->user_verifier_interface != NULL) {
                if (g_error_matches (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE) ||
                    g_error_matches (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_TOO_MANY_RETRIES)) {
                        gdm_dbus_user_verifier_emit_service_unavailable (self->user_verifier_interface,
                                                                         service_name,
                                                                         error->message);
                } else {
                        gdm_dbus_user_verifier_emit_problem (self->user_verifier_interface,
                                                             service_name,
                                                             error->message);
                }
                gdm_dbus_user_verifier_emit_verification_failed (self->user_verifier_interface,
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

        self = g_object_ref (conversation->session);
        service_name = g_strdup (conversation->service_name);

        if (worked) {
                g_signal_emit (self,
                               signals[CREDENTIALS_ESTABLISHED],
                               0,
                               service_name,
                               conversation->worker_pid);

                switch (self->verification_mode) {
                case GDM_SESSION_VERIFICATION_MODE_LOGIN:
                case GDM_SESSION_VERIFICATION_MODE_CHOOSER:
                        gdm_session_open_session (self, service_name);
                        break;
                case GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE:
                        if (self->user_verifier_interface != NULL) {
                                gdm_dbus_user_verifier_emit_verification_complete (self->user_verifier_interface,
                                                                                   service_name);
                                g_signal_emit (self, signals[VERIFICATION_COMPLETE], 0, service_name);
                        }
                        break;
                default:
                        break;
                }
        } else {
                report_and_stop_conversation (self, service_name, error);
        }

        g_free (service_name);
        g_object_unref (self);
}

static gboolean
supports_session_type (GdmSession *self,
                       const char *session_type)
{
        if (session_type == NULL)
                return TRUE;

        return g_strv_contains ((const char * const *) self->supported_session_types,
                                session_type);
}

static char **
get_system_session_dirs (GdmSession *self,
                         const char *type)
{
        GArray *search_array = NULL;
        char **search_dirs;
        int i, j;
        const gchar * const *system_data_dirs = g_get_system_data_dirs ();

        static const char *x_search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
        };

        static const char *wayland_search_dir = DATADIR "/wayland-sessions/";

        search_array = g_array_new (TRUE, TRUE, sizeof (char *));

        for (j = 0; self->supported_session_types[j] != NULL; j++) {
                const char *supported_type = self->supported_session_types[j];

                if (g_str_equal (supported_type, "x11") &&
                    (type == NULL || g_str_equal (type, supported_type))) {
                        for (i = 0; system_data_dirs[i]; i++) {
                                gchar *dir = g_build_filename (system_data_dirs[i], "xsessions", NULL);
                                g_array_append_val (search_array, dir);
                        }

                        g_array_append_vals (search_array, x_search_dirs, G_N_ELEMENTS (x_search_dirs));
                }


#ifdef ENABLE_WAYLAND_SUPPORT
                if (g_str_equal (supported_type, "wayland") &&
                    (type == NULL || g_str_equal (type, supported_type))) {
                        for (i = 0; system_data_dirs[i]; i++) {
                                gchar *dir = g_build_filename (system_data_dirs[i], "wayland-sessions", NULL);
                                g_array_append_val (search_array, dir);
                        }

                        g_array_append_val (search_array, wayland_search_dir);
                }
#endif
        }

        search_dirs = g_strdupv ((char **) search_array->data);

        g_array_free (search_array, TRUE);

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
load_key_file_for_file (GdmSession   *self,
                        const char   *file,
                        const char   *type,
                        char        **full_path)
{
        GKeyFile   *key_file;
        GError     *error = NULL;
        gboolean    res;
        char      **search_dirs;

        key_file = g_key_file_new ();

        search_dirs = get_system_session_dirs (self, type);

        error = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         (const char **) search_dirs,
                                         full_path,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("GdmSession: File '%s' not found in search dirs", file);
                if (error != NULL) {
                        g_debug ("GdmSession: %s", error->message);
                        g_error_free (error);
                }
                g_key_file_free (key_file);
                key_file = NULL;
        }

        g_strfreev (search_dirs);

        return key_file;
}

static gboolean
is_wayland_headless (GdmSession *self)
{
        return g_strcmp0 (self->session_type, "wayland") == 0 &&
                          !self->display_is_local;
}

static gboolean
get_session_command_for_file (GdmSession  *self,
                              const char  *file,
                              const char  *type,
                              char       **command)
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

        if (!supports_session_type (self, type)) {
                g_debug ("GdmSession: ignoring %s session command request for file '%s'",
                         type, file);
                goto out;
        }

        g_debug ("GdmSession: getting session command for file '%s'", file);
        key_file = load_key_file_for_file (self, file, type, NULL);
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

        if (is_wayland_headless (self)) {
                gboolean can_run_headless;

                can_run_headless = g_key_file_get_boolean (key_file,
                                                           G_KEY_FILE_DESKTOP_GROUP,
                                                           "X-GDM-CanRunHeadless",
                                                           NULL);
                if (!can_run_headless && is_wayland_headless (self)) {
                        g_debug ("GdmSession: Session %s is not headless capable", file);
                        goto out;
                }
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
get_session_command_for_name (GdmSession  *self,
                              const char  *name,
                              const char  *type,
                              char       **command)
{
        gboolean res;
        char    *filename;

        filename = g_strdup_printf ("%s.desktop", name);
        res = get_session_command_for_file (self, filename, type, command);
        g_free (filename);

        return res;
}

static const char *
get_default_language_name (GdmSession *self)
{
    const char *default_language;

    if (self->saved_language != NULL) {
            return self->saved_language;
    }

    default_language = g_hash_table_lookup (self->environment,
                                            "LANG");

    if (default_language != NULL) {
            return default_language;
    }

    return setlocale (LC_MESSAGES, NULL);
}

static const char *
get_fallback_session_name (GdmSession *self)
{
        char          **search_dirs;
        int             i;
        char           *name;
        GSequence      *sessions;
        GSequenceIter  *session;

        if (self->fallback_session_name != NULL) {
                /* verify that the cached version still exists */
                if (get_session_command_for_name (self, self->fallback_session_name, NULL, NULL)) {
                        goto out;
                }
        }

        name = g_strdup ("gnome");
        if (get_session_command_for_name (self, name, NULL, NULL)) {
                g_free (self->fallback_session_name);
                self->fallback_session_name = name;
                goto out;
        }
        g_free (name);

        sessions = g_sequence_new (g_free);

        search_dirs = get_system_session_dirs (self, NULL);
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

                        if (get_session_command_for_file (self, base_name, NULL, NULL)) {
                                name = g_strndup (base_name, strlen (base_name) - strlen (".desktop"));
                                g_sequence_insert_sorted (sessions, name, (GCompareDataFunc) g_strcmp0, NULL);
                        }
                } while (base_name != NULL);

                g_dir_close (dir);
        }
        g_strfreev (search_dirs);

        name = NULL;
        session = g_sequence_get_begin_iter (sessions);

        if (g_sequence_iter_is_end (session))
                g_error ("GdmSession: no session desktop files installed, aborting...");

        do {
               name = g_sequence_get (session);
               if (name) {
                       break;
               }
               session = g_sequence_iter_next (session);
        } while (!g_sequence_iter_is_end (session));

        g_free (self->fallback_session_name);
        self->fallback_session_name = g_strdup (name);

        g_sequence_free (sessions);

 out:
        return self->fallback_session_name;
}

static const char *
get_default_session_name (GdmSession *self)
{
        if (self->saved_session != NULL) {
                return self->saved_session;
        }

        return get_fallback_session_name (self);
}

static void
gdm_session_defaults_changed (GdmSession *self)
{

        update_session_type (self);

        if (self->greeter_interface != NULL) {
                gdm_dbus_greeter_emit_default_language_name_changed (self->greeter_interface,
                                                                     get_default_language_name (self));
                gdm_dbus_greeter_emit_default_session_name_changed (self->greeter_interface,
                                                                    get_default_session_name (self));
        }
}

void
gdm_session_select_user (GdmSession *self,
                         const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (text != NULL);

        g_debug ("GdmSession: selecting user '%s' for session '%s' (%p)",
                 text,
                 gdm_session_get_session_id (self),
                 self);

        g_free (self->selected_user);
        self->selected_user = g_strdup (text);

        g_free (self->saved_session);
        self->saved_session = NULL;

        g_free (self->saved_session_type);
        self->saved_session_type = NULL;

        g_free (self->saved_language);
        self->saved_language = NULL;
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
gdm_session_handle_choice_list_query (GdmDBusWorkerManager  *worker_manager_interface,
                                      GDBusMethodInvocation *invocation,
                                      const char            *service_name,
                                      const char            *prompt_message,
                                      GVariant              *query,
                                      GdmSession            *self)
{
        GdmSessionConversation *conversation;
        GdmDBusUserVerifierChoiceList *choice_list_interface = NULL;

        g_debug ("GdmSession: choice query for service '%s'", service_name);

        if (self->user_verifier_extensions != NULL)
                choice_list_interface = g_hash_table_lookup (self->user_verifier_extensions,
                                                             gdm_dbus_user_verifier_choice_list_interface_info ()->name);

        if (choice_list_interface == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                                               "ChoiceList interface not supported by client");
                return TRUE;
        }

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                g_debug ("GdmSession: emitting choice query '%s'", prompt_message);
                gdm_dbus_user_verifier_choice_list_emit_choice_query (choice_list_interface,
                                                                      service_name,
                                                                      prompt_message,
                                                                      query);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_custom_json_request (GdmDBusWorkerManager  *worker_manager_interface,
                                        GDBusMethodInvocation *invocation,
                                        const char            *service_name,
                                        const char            *protocol,
                                        unsigned int           version,
                                        const char            *request,
                                        GdmSession            *self)
{
        GdmSessionConversation *conversation;
        GdmDBusUserVerifierCustomJSON *custom_json_interface = NULL;

        g_debug ("GdmSession: custom JSON request for service '%s'", service_name);

        if (self->user_verifier_extensions != NULL) {
                custom_json_interface =
                        g_hash_table_lookup (self->user_verifier_extensions,
                                             gdm_dbus_user_verifier_custom_json_interface_info ()->name);
        }

        if (custom_json_interface == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                                               "custom JSON interface not supported by client");
                return TRUE;
        }

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                g_debug ("GdmSession: emitting custom JSON request '%s' v%u",
                         protocol, version);
                gdm_dbus_user_verifier_custom_json_emit_request (custom_json_interface,
                                                                 service_name,
                                                                 protocol,
                                                                 version,
                                                                 request);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_info_query (GdmDBusWorkerManager  *worker_manager_interface,
                               GDBusMethodInvocation *invocation,
                               const char            *service_name,
                               const char            *query,
                               GdmSession            *self)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (self->user_verifier_interface != NULL, FALSE);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                gdm_dbus_user_verifier_emit_info_query (self->user_verifier_interface,
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

        g_return_val_if_fail (self->user_verifier_interface != NULL, FALSE);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                set_pending_query (conversation, invocation);

                gdm_dbus_user_verifier_emit_secret_info_query (self->user_verifier_interface,
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

        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_info (self->user_verifier_interface,
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

        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_problem (self->user_verifier_interface,
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

                if (self->user_verifier_interface != NULL) {
                        gdm_dbus_user_verifier_emit_verification_complete (self->user_verifier_interface,
                                                                           service_name);
                        g_signal_emit (self, signals[VERIFICATION_COMPLETE], 0, service_name);
                }

                if (self->greeter_interface != NULL) {
                        gdm_dbus_greeter_emit_session_opened (self->greeter_interface,
                                                              service_name,
                                                              session_id);
                }

                g_debug ("GdmSession: Emitting 'session-opened' signal");
                g_signal_emit (self, signals[SESSION_OPENED], 0, service_name, session_id);

                self->is_opened = TRUE;
        } else {
                report_and_stop_conversation (self, service_name, error);

                g_debug ("GdmSession: Emitting 'session-start-failed' signal");
                g_signal_emit (self, signals[SESSION_OPENED_FAILED], 0, service_name, error->message);
        }
}

static void
worker_on_username_changed (GdmDBusWorker          *worker,
                            const char             *username,
                            GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        g_debug ("GdmSession: changing username from '%s' to '%s'",
                 self->selected_user != NULL ? self->selected_user : "<unset>",
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

        self->session_conversation = NULL;

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
                           int                     reauth_pid,
                           GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;
        g_debug ("GdmSession: Emitting 'reauthenticated' signal ");
        g_signal_emit (self, signals[REAUTHENTICATED], 0, service_name, reauth_pid);
}

static void
worker_on_saved_language_name_read (GdmDBusWorker          *worker,
                                    const char             *language_name,
                                    GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        if (strlen (language_name) > 0) {
                g_free (self->saved_language);
                self->saved_language = g_strdup (language_name);

                if (self->greeter_interface != NULL) {
                        gdm_dbus_greeter_emit_default_language_name_changed (self->greeter_interface,
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

        if (! get_session_command_for_name (self, session_name, self->saved_session_type, NULL)) {
                /* ignore sessions that don't exist */
                g_debug ("GdmSession: not using invalid .dmrc session: %s", session_name);
                g_free (self->saved_session);
                self->saved_session = NULL;
                update_session_type (self);
        } else {
                if (strcmp (session_name,
                            get_default_session_name (self)) != 0) {
                        g_free (self->saved_session);
                        self->saved_session = g_strdup (session_name);

                        if (self->greeter_interface != NULL) {
                                gdm_dbus_greeter_emit_default_session_name_changed (self->greeter_interface,
                                                                                    session_name);
                        }
                }
                if (self->saved_session_type != NULL)
                        set_session_type (self, self->saved_session_type);
                else
                        update_session_type (self);
        }

}

static void
worker_on_saved_session_type_read (GdmDBusWorker          *worker,
                                   const char             *session_type,
                                   GdmSessionConversation *conversation)
{
        GdmSession *self = conversation->session;

        g_free (self->saved_session_type);
        self->saved_session_type = g_strdup (session_type);
}

static GdmSessionConversation *
find_conversation_by_pid (GdmSession *self,
                          GPid        pid)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, self->conversations);
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

        if (connecting_user == self->allowed_user) {
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
        self->pending_worker_connections =
            g_list_remove (self->pending_worker_connections,
                           connection);
        g_object_set_data (G_OBJECT (connection),
                           "gdm-dbus-worker-manager-interface",
                           NULL);
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
        connection_node = g_list_find (self->pending_worker_connections, connection);

        if (connection_node == NULL) {
                g_debug ("GdmSession: Ignoring connection that we aren't tracking");
                return FALSE;
        }

        /* connection was ref'd when it was added to list, we're taking that
         * reference over and removing it from the list
         */
        self->pending_worker_connections =
                g_list_delete_link (self->pending_worker_connections,
                                    connection_node);

        g_object_set_data (G_OBJECT (connection),
                           "gdm-dbus-worker-manager-interface",
                           NULL);

        g_signal_handlers_disconnect_by_func (connection,
                                              G_CALLBACK (on_worker_connection_closed),
                                              self);

        credentials = g_dbus_connection_get_peer_credentials (connection);
        pid = g_credentials_get_unix_pid (credentials, NULL);

        conversation = find_conversation_by_pid (self, (GPid) pid);

        if (conversation == NULL) {
                g_warning ("GdmSession: New worker connection is from unknown source");

                g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
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

        conversation->worker_cancellable = g_cancellable_new ();

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
                          "saved-session-type-read",
                          G_CALLBACK (worker_on_saved_session_type_read), conversation);
        g_signal_connect (conversation->worker_proxy,
                          "cancel-pending-query",
                          G_CALLBACK (worker_on_cancel_pending_query), conversation);

        conversation->worker_manager_interface = g_object_ref (worker_manager_interface);
        g_debug ("GdmSession: worker connection is %p", connection);

        g_debug ("GdmSession: Emitting conversation-started signal");
        g_signal_emit (self, signals[CONVERSATION_STARTED], 0, conversation->service_name);

        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_started (self->user_verifier_interface,
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
        g_signal_connect_object (worker_manager_interface,
                                 "handle-hello",
                                 G_CALLBACK (register_worker),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-info-query",
                                 G_CALLBACK (gdm_session_handle_info_query),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-secret-info-query",
                                 G_CALLBACK (gdm_session_handle_secret_info_query),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-info",
                                 G_CALLBACK (gdm_session_handle_info),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-problem",
                                 G_CALLBACK (gdm_session_handle_problem),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-choice-list-query",
                                 G_CALLBACK (gdm_session_handle_choice_list_query),
                                 self,
                                 0);
        g_signal_connect_object (worker_manager_interface,
                                 "handle-custom-json-request",
                                 G_CALLBACK (gdm_session_handle_custom_json_request),
                                 self,
                                 0);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (worker_manager_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);
        g_object_set_data_full (G_OBJECT (connection),
                                "gdm-dbus-worker-manager-interface",
                                g_object_ref (worker_manager_interface),
                                g_object_unref);
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
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_choice_list_query),
                                              self);
        g_signal_handlers_disconnect_by_func (worker_manager_interface,
                                              G_CALLBACK (gdm_session_handle_custom_json_request),
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
        self->pending_worker_connections =
                g_list_prepend (self->pending_worker_connections,
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
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_SPAWN_FAILED,
                                                               _("Could not create authentication helper process"));
        }

        return conversation;
}

static gboolean
gdm_session_handle_client_select_choice (GdmDBusUserVerifierChoiceList    *choice_list_interface,
                                         GDBusMethodInvocation            *invocation,
                                         const char                       *service_name,
                                         const char                       *answer,
                                         GdmSession                       *self)
{
        g_debug ("GdmSession: user selected choice '%s'", answer);
        gdm_dbus_user_verifier_choice_list_complete_select_choice (choice_list_interface, invocation);
        gdm_session_answer_query (self, service_name, answer);
        return TRUE;
}

static void
export_user_verifier_choice_list_interface (GdmSession      *self,
                                            GDBusConnection *connection)
{
        GdmDBusUserVerifierChoiceList   *interface;

        interface = GDM_DBUS_USER_VERIFIER_CHOICE_LIST (gdm_dbus_user_verifier_choice_list_skeleton_new ());

        g_signal_connect (interface,
                          "handle-select-choice",
                          G_CALLBACK (gdm_session_handle_client_select_choice),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        g_hash_table_insert (self->user_verifier_extensions,
                             gdm_dbus_user_verifier_choice_list_interface_info ()->name,
                             interface);
}

static gboolean
gdm_session_handle_client_custom_json_reply (GdmDBusUserVerifierCustomJSON *custom_json_interface,
                                             GDBusMethodInvocation         *invocation,
                                             const char                    *service_name,
                                             const char                    *json,
                                             GdmSession                    *self)
{
        g_autoptr(GError) error = NULL;
        g_autoptr(JsonParser) parser = NULL;

        g_debug ("GdmSession: user replied with custom JSON");

        parser = json_parser_new_immutable ();
        if (!json_parser_load_from_data (parser, json, -1, &error)) {
                g_autofree char *message = NULL;

                message = g_strdup_printf ("JSON reply is not valid: %s", error->message);
                g_warning ("GdmSession: %s", message);

                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                                               message);
                gdm_session_report_error (self, service_name,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          message);
                return TRUE;
        }

        gdm_dbus_user_verifier_custom_json_complete_reply (custom_json_interface, invocation);
        gdm_session_answer_query (self, service_name, json);
        return TRUE;
}

static gboolean
gdm_session_handle_client_custom_json_report_error (GdmDBusUserVerifierCustomJSON *custom_json_interface,
                                                    GDBusMethodInvocation         *invocation,
                                                    const char                    *service_name,
                                                    const char                    *message,
                                                    GdmSession                    *self)
{
        g_debug ("GdmSession: user reported custom JSON error: %s", message);

        gdm_dbus_user_verifier_custom_json_complete_report_error (custom_json_interface, invocation);
        gdm_session_report_error (self, service_name, G_DBUS_ERROR_ACCESS_DENIED, message);
        return TRUE;
}

static void
export_user_verifier_custom_json_interface (GdmSession      *self,
                                             GDBusConnection *connection)
{
        GdmDBusUserVerifierCustomJSON *interface;

        interface = GDM_DBUS_USER_VERIFIER_CUSTOM_JSON (gdm_dbus_user_verifier_custom_json_skeleton_new ());

        g_signal_connect (interface,
                          "handle-reply",
                          G_CALLBACK (gdm_session_handle_client_custom_json_reply),
                          self);
        g_signal_connect (interface,
                          "handle-report-error",
                          G_CALLBACK (gdm_session_handle_client_custom_json_report_error),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        g_hash_table_insert (self->user_verifier_extensions,
                             gdm_dbus_user_verifier_custom_json_interface_info ()->name,
                             interface);
}

static gboolean
gdm_session_handle_client_enable_extensions (GdmDBusUserVerifier    *user_verifier_interface,
                                             GDBusMethodInvocation  *invocation,
                                             const char * const *    extensions,
                                             GDBusConnection        *connection)
{
        GdmSession *self = g_object_get_data (G_OBJECT (connection), "gdm-session");
        size_t i;

        g_hash_table_remove_all (self->user_verifier_extensions);

        for (i = 0; extensions[i] != NULL; i++) {
                if (g_hash_table_lookup (self->user_verifier_extensions, extensions[i]) != NULL)
                        continue;

                if (strcmp (extensions[i],
                            gdm_dbus_user_verifier_choice_list_interface_info ()->name) == 0)
                        export_user_verifier_choice_list_interface (self, connection);

                if (g_str_equal (extensions[i],
                                 gdm_dbus_user_verifier_custom_json_interface_info ()->name))
                        export_user_verifier_custom_json_interface (self, connection);

        }

        gdm_dbus_user_verifier_complete_enable_extensions (user_verifier_interface, invocation);

        return TRUE;
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
        if (gdm_session_is_running (self)) {
                const char *username;

                username = gdm_session_get_username (self);
                g_debug ("GdmSession: refusing to select session %s since it's already running (for user %s)",
                         session,
                         username);
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Session already running for user %s",
                                                       username);
                return TRUE;
        }

        if (self->greeter_interface != NULL) {
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
        if (gdm_session_is_running (self)) {
                const char *session_username;

                session_username = gdm_session_get_username (self);
                g_debug ("GdmSession: refusing to select user %s, since session (%p) already running (for user %s)",
                          username,
                          self,
                          session_username);
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Session already running for user %s",
                                                       session_username);
                return TRUE;
        }

        if (self->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_select_user (greeter_interface,
                                                       invocation);
        }
        g_debug ("GdmSession: client selected user '%s' on session (%p)", username, self);
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
        if (gdm_session_is_running (self)) {
                const char *username;

                username = gdm_session_get_username (self);
                g_debug ("GdmSession: refusing to start session (%p), since it's already running (for user %s)",
                         self,
                         username);
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Session already running for user %s",
                                                       username);
                return TRUE;
        }

        if (self->greeter_interface != NULL) {
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
        if (gdm_session_is_running (self)) {
                const char *username;

                username = gdm_session_get_username (self);
                g_debug ("GdmSession: refusing to give timed login details, session (%p) already running (for user %s)",
                         self,
                         username);
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Session already running for user %s",
                                                       username);
                return TRUE;
        }

        if (self->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_get_timed_login_details (greeter_interface,
                                                                   invocation,
                                                                   self->timed_login_username != NULL,
                                                                   self->timed_login_username != NULL? self->timed_login_username : "",
                                                                   self->timed_login_delay);
                if (self->timed_login_username != NULL) {
                        gdm_dbus_greeter_emit_timed_login_requested (self->greeter_interface,
                                                                     self->timed_login_username,
                                                                     self->timed_login_delay);
                }
        }
        return TRUE;
}

static gboolean
gdm_session_handle_client_stop_conflicting_session (GdmDBusGreeter        *greeter_interface,
                                                    GDBusMethodInvocation *invocation,
                                                    GdmSession            *self)
{
        if (!self->is_opened) {
                g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               "Can't stop conflicting session if this session is not opened yet");
                return TRUE;
        }

        g_signal_emit (self, signals[STOP_CONFLICTING_SESSION], 0, self->selected_user);

        if (self->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_stop_conflicting_session (self->greeter_interface,
                                                                    invocation);
        }

        return TRUE;
}

static gboolean
gdm_session_handle_client_begin_auto_login (GdmDBusGreeter        *greeter_interface,
                                            GDBusMethodInvocation *invocation,
                                            const char            *username,
                                            GdmSession            *self)
{
        const char *session_username;

        if (gdm_session_is_running (self)) {
                session_username = gdm_session_get_username (self);
                g_debug ("GdmSession: refusing auto login operation, session (%p) already running for user %s (%s requested)",
                         self,
                         session_username,
                         username);
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Session already owned by user %s",
                                                       session_username);
                return TRUE;
        }

        if (self->greeter_interface != NULL) {
                gdm_dbus_greeter_complete_begin_auto_login (greeter_interface,
                                                            invocation);
        }

        g_debug ("GdmSession: client requesting automatic login for user '%s' on session '%s' (%p)",
                 username,
                 gdm_session_get_session_id (self),
                 self);

        gdm_session_setup_for_user (self, "gdm-autologin", username);

        return TRUE;
}

static void
export_user_verifier_interface (GdmSession      *self,
                                GDBusConnection *connection)
{
        g_autoptr (GdmDBusUserVerifier) user_verifier_interface = NULL;

        user_verifier_interface = GDM_DBUS_USER_VERIFIER (gdm_dbus_user_verifier_skeleton_new ());

        g_object_set_data (G_OBJECT (connection), "gdm-session", self);

        g_signal_connect (user_verifier_interface,
                          "handle-enable-extensions",
                          G_CALLBACK (gdm_session_handle_client_enable_extensions),
                          connection);
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

        g_set_object (&self->user_verifier_interface, user_verifier_interface);
}

static void
export_greeter_interface (GdmSession      *self,
                          GDBusConnection *connection)
{
        g_autoptr (GdmDBusGreeter) greeter_interface = NULL;

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
        g_signal_connect (greeter_interface,
                          "handle-stop-conflicting-session",
                          G_CALLBACK (gdm_session_handle_client_stop_conflicting_session),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (greeter_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        g_set_object (&self->greeter_interface, greeter_interface);
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
        g_autoptr (GdmDBusRemoteGreeter) remote_greeter_interface = NULL;

        remote_greeter_interface = GDM_DBUS_REMOTE_GREETER (gdm_dbus_remote_greeter_skeleton_new ());

        g_signal_connect (remote_greeter_interface,
                          "handle-disconnect",
                          G_CALLBACK (gdm_session_handle_client_disconnect),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (remote_greeter_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        g_set_object (&self->remote_greeter_interface, remote_greeter_interface);
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
        g_autoptr (GdmDBusChooser) chooser_interface = NULL;

        chooser_interface = GDM_DBUS_CHOOSER (gdm_dbus_chooser_skeleton_new ());

        g_signal_connect (chooser_interface,
                          "handle-select-hostname",
                          G_CALLBACK (gdm_session_handle_client_select_hostname),
                          self);

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (chooser_interface),
                                          connection,
                                          GDM_SESSION_DBUS_OBJECT_PATH,
                                          NULL);

        g_set_object (&self->chooser_interface, chooser_interface);
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

        self->outside_connections = g_list_remove (self->outside_connections,
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

        self->outside_connections = g_list_prepend (self->outside_connections,
                                                    g_object_ref (connection));

        g_signal_connect_object (connection,
                                 "closed",
                                 G_CALLBACK (on_outside_connection_closed),
                                 self,
                                 0);

        export_user_verifier_interface (self, connection);

        switch (self->verification_mode) {
                case GDM_SESSION_VERIFICATION_MODE_LOGIN:
                        export_greeter_interface (self, connection);
                break;

                case GDM_SESSION_VERIFICATION_MODE_CHOOSER:
                        export_chooser_interface (self, connection);
                break;

                default:
                break;
        }

        if (!self->display_is_local) {
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
        self->worker_server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSession: D-Bus server for workers listening on %s",
        g_dbus_server_get_client_address (self->worker_server));
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
        if (client_uid == self->allowed_user) {
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

         g_debug ("GdmSession: Creating D-Bus server for greeters and such for session %s (%p)",
                  gdm_session_get_session_id (self),
                  self);

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
        self->outside_server = server;

        g_dbus_server_start (server);

        g_debug ("GdmSession: D-Bus server for greeters listening on %s",
        g_dbus_server_get_client_address (self->outside_server));
}

static void
free_conversation (GdmSessionConversation *conversation)
{
        close_conversation (conversation);

        if (conversation->job != NULL) {
                g_warning ("Freeing conversation '%s' with active job", conversation->service_name);
        }

        g_free (conversation->service_name);
        g_free (conversation->starting_username);
        g_free (conversation->session_id);
        g_clear_object (&conversation->worker_manager_interface);

        g_cancellable_cancel (conversation->worker_cancellable);
        g_clear_object (&conversation->worker_cancellable);

        if (conversation->worker_proxy != NULL) {
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_username_changed),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_session_exited),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_reauthenticated),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_saved_language_name_read),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_saved_session_name_read),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_saved_session_type_read),
                                                      conversation);
                g_signal_handlers_disconnect_by_func (conversation->worker_proxy,
                                                      G_CALLBACK (worker_on_cancel_pending_query),
                                                      conversation);
                g_clear_object (&conversation->worker_proxy);
        }
        g_clear_object (&conversation->session);
        g_free (conversation);
}

static void
load_lang_config_file (GdmSession *self)
{
        static const char *config_file = LANG_CONFIG_FILE;
        gchar         *contents = NULL;
        gchar         *p;
        gchar         *key;
        gchar         *value;
        gsize          length;
        GError        *error;
        GString       *line;
        GRegex        *re;

        if (!g_file_test (config_file, G_FILE_TEST_EXISTS)) {
                g_debug ("Cannot access '%s'", config_file);
                return;
        }

        error = NULL;
        if (!g_file_get_contents (config_file, &contents, &length, &error)) {
                g_debug ("Failed to parse '%s': %s",
                         LANG_CONFIG_FILE,
                         (error && error->message) ? error->message : "(null)");
                g_error_free (error);
                return;
        }

        if (!g_utf8_validate (contents, length, NULL)) {
                g_warning ("Invalid UTF-8 in '%s'", config_file);
                g_free (contents);
                return;
        }

        re = g_regex_new ("(?P<key>(LANG|LANGUAGE|LC_CTYPE|LC_NUMERIC|LC_TIME|LC_COLLATE|LC_MONETARY|LC_MESSAGES|LC_PAPER|LC_NAME|LC_ADDRESS|LC_TELEPHONE|LC_MEASUREMENT|LC_IDENTIFICATION|LC_ALL))=(\")?(?P<value>[^\"]*)?(\")?", 0, 0, &error);
        if (re == NULL) {
                g_warning ("Failed to regex: %s",
                           (error && error->message) ? error->message : "(null)");
                g_error_free (error);
                g_free (contents);
                return;
        }

        line = g_string_new ("");
        for (p = contents; p && *p; p = g_utf8_find_next_char (p, NULL)) {
                gunichar ch;
                GMatchInfo *match_info = NULL;

                ch = g_utf8_get_char (p);
                if ((ch != '\n') && (ch != '\0')) {
                        g_string_append_unichar (line, ch);
                        continue;
                }

                if (line->str && g_utf8_get_char (line->str) == '#') {
                        goto next_line;
                }

                if (!g_regex_match (re, line->str, 0, &match_info)) {
                        goto next_line;
                }

                if (!g_match_info_matches (match_info)) {
                        goto next_line;
                }

                key = g_match_info_fetch_named (match_info, "key");
                value = g_match_info_fetch_named (match_info, "value");

                if (key && *key && value && *value) {
			g_setenv (key, value, TRUE);
                }

                g_free (key);
                g_free (value);
next_line:
                g_match_info_free (match_info);
                g_string_set_size (line, 0);
        }

        g_string_free (line, TRUE);
        g_regex_unref (re);
        g_free (contents);
}

static void
unexport_and_free_user_verifier_extension (GDBusInterfaceSkeleton *interface)
{
        g_dbus_interface_skeleton_unexport (interface);

        g_object_run_dispose (G_OBJECT (interface));
        g_object_unref (interface);
}

static void
gdm_session_init (GdmSession *self)
{
        self->conversations = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           (GDestroyNotify) g_free,
                                                           (GDestroyNotify)
                                                           free_conversation);
        self->environment = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         (GDestroyNotify) g_free,
                                                         (GDestroyNotify) g_free);
        self->user_verifier_extensions = g_hash_table_new_full (g_str_hash,
                                                                      g_str_equal,
                                                                      NULL,
                                                                      (GDestroyNotify)
                                                                      unexport_and_free_user_verifier_extension);

        load_lang_config_file (self);
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

        g_hash_table_steal (self->conversations, conversation->service_name);

        g_object_ref (conversation->job);
        if (self->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_EXITED], 0, code);
                self->session_conversation = NULL;
        }

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_stopped (self->user_verifier_interface,
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

        g_hash_table_steal (self->conversations, conversation->service_name);

        g_object_ref (conversation->job);
        if (self->session_conversation == conversation) {
                g_signal_emit (self, signals[SESSION_DIED], 0, signum);
                self->session_conversation = NULL;
        }

        g_debug ("GdmSession: Emitting conversation-stopped signal");
        g_signal_emit (self, signals[CONVERSATION_STOPPED], 0, conversation->service_name);
        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_conversation_stopped (self->user_verifier_interface,
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
                                                   g_dbus_server_get_client_address (self->worker_server));
        gdm_session_worker_job_set_for_reauth (conversation->job,
                                               self->verification_mode == GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE);

        if (self->conversation_environment != NULL) {
                gdm_session_worker_job_set_environment (conversation->job,
                                                        (const char * const *)
                                                        self->conversation_environment);

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

void
gdm_session_set_supported_session_types (GdmSession         *self,
                                         const char * const *supported_session_types)
{
        const char * const session_types[] = { "wayland", "x11", NULL };
        g_strfreev (self->supported_session_types);

        if (supported_session_types == NULL)
                self->supported_session_types = g_strdupv ((GStrv) session_types);
        else
                self->supported_session_types = g_strdupv ((GStrv) supported_session_types);
}

gboolean
gdm_session_start_conversation (GdmSession *self,
                                const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);
        g_return_val_if_fail (service_name != NULL, FALSE);

        conversation = g_hash_table_lookup (self->conversations,
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

        g_debug ("GdmSession: starting conversation %s for session (%p)", service_name, self);

        conversation = start_conversation (self, service_name);

        g_hash_table_insert (self->conversations,
                             g_strdup (service_name), conversation);
        return TRUE;
}

void
gdm_session_stop_conversation (GdmSession *self,
                               const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        g_debug ("GdmSession: stopping conversation %s", service_name);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                stop_conversation (conversation);
        }
}

static void
on_initialization_complete_cb (GdmDBusWorker *proxy,
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
initialize (GdmSession *self,
            const char *service_name,
            const char *username,
            const char *log_file)
{
        GVariantBuilder          details;
        const char             **extensions;
        GdmSessionConversation  *conversation;

        g_assert (service_name != NULL);

        g_variant_builder_init (&details, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add_parsed (&details, "{'service', <%s>}", service_name);
        extensions = (const char **) g_hash_table_get_keys_as_array (self->user_verifier_extensions, NULL);

        g_variant_builder_add_parsed (&details, "{'extensions', <%^as>}", extensions);

        if (username != NULL)
                g_variant_builder_add_parsed (&details, "{'username', <%s>}", username);

        if (log_file != NULL)
                g_variant_builder_add_parsed (&details, "{'log-file', <%s>}", log_file);

        if (self->is_program_session)
                g_variant_builder_add_parsed (&details, "{'is-program-session', <%b>}", self->is_program_session);

        if (self->display_name != NULL)
                g_variant_builder_add_parsed (&details, "{'x11-display-name', <%s>}", self->display_name);

        if (self->display_hostname != NULL)
                g_variant_builder_add_parsed (&details, "{'hostname', <%s>}", self->display_hostname);

        if (self->display_is_local)
                g_variant_builder_add_parsed (&details, "{'display-is-local', <%b>}", self->display_is_local);

        if (self->display_is_initial)
                g_variant_builder_add_parsed (&details, "{'display-is-initial', <%b>}", self->display_is_initial);

        if (self->display_device != NULL)
                g_variant_builder_add_parsed (&details, "{'console', <%s>}", self->display_device);

        if (self->display_seat_id != NULL)
                g_variant_builder_add_parsed (&details, "{'seat-id', <%s>}", self->display_seat_id);

        if (self->display_x11_authority_file != NULL)
                g_variant_builder_add_parsed (&details, "{'x11-authority-file', <%s>}", self->display_x11_authority_file);

        g_debug ("GdmSession: Beginning initialization");

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_initialize (conversation->worker_proxy,
                                                 g_variant_builder_end (&details),

                                                 conversation->worker_cancellable,
                                                 (GAsyncReadyCallback) on_initialization_complete_cb,
                                                 conversation);
        }

        g_free (extensions);
}

void
gdm_session_setup (GdmSession *self,
                   const char *service_name)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        update_session_type (self);

        initialize (self, service_name, NULL, NULL);
        gdm_session_defaults_changed (self);
}


void
gdm_session_setup_for_user (GdmSession *self,
                            const char *service_name,
                            const char *username)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);
        g_return_if_fail (username != NULL);

        update_session_type (self);

        g_debug ("GdmSession: Set up service %s for username %s on session (%p)",
                 service_name,
                 username,
                 self);
        gdm_session_select_user (self, username);

        self->is_program_session = FALSE;
        initialize (self, service_name, self->selected_user, NULL);
        gdm_session_defaults_changed (self);
}

void
gdm_session_setup_for_program (GdmSession *self,
                               const char *service_name,
                               const char *username,
                               const char *log_file)
{

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        self->is_program_session = TRUE;
        initialize (self, service_name, username, log_file);
}

void
gdm_session_authenticate (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_authenticate (conversation->worker_proxy,
                                                   conversation->worker_cancellable,
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
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_authorize (conversation->worker_proxy,
                                                conversation->worker_cancellable,
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
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                gdm_dbus_worker_call_establish_credentials (conversation->worker_proxy,
                                                            conversation->worker_cancellable,
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
                                                       conversation->worker_cancellable,
                                                       NULL, NULL);
}

static void
send_environment (GdmSession             *self,
                  GdmSessionConversation *conversation)
{

        g_hash_table_foreach (self->environment,
                              (GHFunc) send_environment_variable,
                              conversation);
}

void
gdm_session_send_environment (GdmSession *self,
                              const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation != NULL) {
                send_environment (self, conversation);
        }
}

static const char *
get_session_name (GdmSession *self)
{
        /* FIXME: test the session names before we use them? */

        if (self->selected_session != NULL) {
                return self->selected_session;
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
        res = get_session_command_for_name (self, session_name, NULL, &command);
        if (! res) {
                g_critical ("Cannot find a command for specified session: %s", session_name);
                exit (EXIT_FAILURE);
        }

        return command;
}

static gchar *
get_session_desktop_names (GdmSession *self)
{
        gchar *filename;
        GKeyFile *keyfile;
        gchar *desktop_names = NULL;

        if (self->selected_program != NULL) {
                return g_strdup ("GNOME-Greeter:GNOME");
        }

        filename = g_strdup_printf ("%s.desktop", get_session_name (self));
        g_debug ("GdmSession: getting desktop names for file '%s'", filename);
        keyfile = load_key_file_for_file (self, filename, NULL, NULL);
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
        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (key != NULL);
        g_return_if_fail (value != NULL);

        g_hash_table_replace (self->environment,
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
        char *locale;

        if (self->selected_program == NULL) {
                gdm_session_set_environment_variable (self,
                                                      "GDMSESSION",
                                                      get_session_name (self));
                gdm_session_set_environment_variable (self,
                                                      "DESKTOP_SESSION",
                                                      get_session_name (self));
                gdm_session_set_environment_variable (self,
                                                      "XDG_SESSION_DESKTOP",
                                                      get_session_name (self));
        }

        desktop_names = get_session_desktop_names (self);
        if (desktop_names != NULL) {
                gdm_session_set_environment_variable (self, "XDG_CURRENT_DESKTOP", desktop_names);
        }

        set_up_session_language (self);

        locale = g_strdup (get_default_language_name (self));

        if (locale != NULL && locale[0] != '\0') {
                gdm_session_set_environment_variable (self,
                                                      "LANG",
                                                      locale);
                gdm_session_set_environment_variable (self,
                                                      "GDM_LANG",
                                                      locale);
        }

        g_free (locale);

        display_mode = gdm_session_get_display_mode (self);
        if (display_mode == GDM_SESSION_DISPLAY_MODE_REUSE_VT) {
                gdm_session_set_environment_variable (self,
                                                      "DISPLAY",
                                                      self->display_name);

                if (self->user_x11_authority_file != NULL) {
                        gdm_session_set_environment_variable (self,
                                                              "XAUTHORITY",
                                                              self->user_x11_authority_file);
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
                                                       conversation->worker_cancellable,
                                                       NULL, NULL);
}

static void
send_session_type (GdmSession *self,
                   GdmSessionConversation *conversation)
{
        const char *session_type = "x11";

        if (self->session_type != NULL) {
                session_type = self->session_type;
        }

        gdm_dbus_worker_call_set_environment_variable (conversation->worker_proxy,
                                                       "XDG_SESSION_TYPE",
                                                       session_type,
                                                       conversation->worker_cancellable,
                                                       NULL, NULL);
}

void
gdm_session_open_session (GdmSession *self,
                          const char *service_name)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                send_display_mode (self, conversation);
                send_session_type (self, conversation);

                gdm_dbus_worker_call_open (conversation->worker_proxy,
                                           conversation->worker_cancellable,
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

        if (self->conversations == NULL) {
                return;
        }

        if (conversation_to_keep == NULL) {
                g_debug ("GdmSession: Stopping all conversations");
        } else {
                g_debug ("GdmSession: Stopping all conversations except for %s",
                         conversation_to_keep->service_name);
        }

        g_hash_table_iter_init (&iter, self->conversations);
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
                g_hash_table_remove_all (self->conversations);

                if (conversation_to_keep != NULL) {
                        g_hash_table_insert (self->conversations,
                                             g_strdup (conversation_to_keep->service_name),
                                             conversation_to_keep);
                }

                if (self->session_conversation != conversation_to_keep) {
                        self->session_conversation = NULL;
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
                self->session_pid = pid;
                self->session_conversation = conversation;

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
        gboolean               register_session;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);
        g_return_if_fail (self->session_conversation == NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation == NULL) {
                g_warning ("GdmSession: Tried to start session of "
                           "nonexistent conversation %s", service_name);
                return;
        }

        stop_all_other_conversations (self, conversation, FALSE);

        display_mode = gdm_session_get_display_mode (self);

#ifdef ENABLE_WAYLAND_SUPPORT
        is_x11 = g_strcmp0 (self->session_type, "wayland") != 0;
#endif

        if (display_mode == GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED ||
            display_mode == GDM_SESSION_DISPLAY_MODE_NEW_VT) {
                run_launcher = TRUE;
        }

        register_session = !gdm_session_session_registers (self);

        if (self->selected_program == NULL) {
                gboolean run_xsession_script;

                command = get_session_command (self);

                run_xsession_script = !gdm_session_bypasses_xsession (self);

                if (self->display_is_local) {
                        gboolean disallow_tcp = TRUE;
                        gdm_settings_direct_get_boolean (GDM_KEY_DISALLOW_TCP, &disallow_tcp);
                        allow_remote_connections = !disallow_tcp;
                } else {
                        allow_remote_connections = TRUE;
                }

                if (run_launcher) {
                        if (is_x11) {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-x-session %s%s %s\"%s\"",
                                                           register_session ? "--register-session " : "",
                                                           run_xsession_script? "--run-script " : "",
                                                           allow_remote_connections? "--allow-remote-connections " : "",
                                                           command);
                        } else {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-wayland-session %s\"%s\"",
                                                           register_session ? "--register-session " : "",
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
                                program = g_strdup_printf (LIBEXECDIR "/gdm-x-session %s\"%s\"",
                                                           register_session ? "--register-session " : "",
                                                           self->selected_program);
                        } else {
                                program = g_strdup_printf (LIBEXECDIR "/gdm-wayland-session %s\"%s\"",
                                                           register_session ? "--register-session " : "",
                                                           self->selected_program);
                        }
                } else {
                        program = g_strdup (self->selected_program);
                }
        }

        set_up_session_environment (self);
        send_environment (self, conversation);

        gdm_dbus_worker_call_start_program (conversation->worker_proxy,
                                            program,
                                            conversation->worker_cancellable,
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
free_pending_worker_connection (GdmSession      *self,
                                GDBusConnection *connection)
{
        GdmDBusWorkerManager *worker_manager_interface;

        worker_manager_interface = g_object_get_data (G_OBJECT (connection),
                                                      "gdm-dbus-worker-manager-interface");
        if (worker_manager_interface != NULL) {
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (worker_manager_interface));
                g_object_set_data (G_OBJECT (connection),
                                   "gdm-dbus-worker-manager-interface",
                                   NULL);
        }

        g_object_unref (connection);
}

static void
free_pending_worker_connections (GdmSession *self)
{
        GList *node;

        for (node = self->pending_worker_connections; node != NULL; node = node->next) {
                GDBusConnection *connection = node->data;

                free_pending_worker_connection (self, connection);
        }
        g_list_free (self->pending_worker_connections);
        self->pending_worker_connections = NULL;
}

static void
do_reset (GdmSession *self)
{
        stop_all_conversations (self);

        free_pending_worker_connections (self);

        g_free (self->selected_user);
        self->selected_user = NULL;

        g_free (self->selected_session);
        self->selected_session = NULL;

        g_free (self->saved_session);
        self->saved_session = NULL;

        g_free (self->saved_language);
        self->saved_language = NULL;

        g_free (self->user_x11_authority_file);
        self->user_x11_authority_file = NULL;

        g_hash_table_remove_all (self->environment);

        self->session_pid = -1;
        self->session_conversation = NULL;
}

void
gdm_session_close (GdmSession *self)
{

        g_return_if_fail (GDM_IS_SESSION (self));

        g_debug ("GdmSession: Closing session");
        do_reset (self);

        g_list_free_full (self->outside_connections, g_object_unref);
        self->outside_connections = NULL;
}

void
gdm_session_answer_query (GdmSession *self,
                          const char *service_name,
                          const char *text)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);
        g_return_if_fail (text != NULL);

        conversation = find_conversation_by_name (self, service_name);

        if (conversation != NULL) {
                answer_pending_query (conversation, text);
        }
}

void
gdm_session_report_error (GdmSession *self,
                          const char *service_name,
                          GDBusError  code,
                          const char *message)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (service_name != NULL);

        conversation = find_conversation_by_name (self, service_name);
        if (conversation == NULL)
                return;

        g_dbus_method_invocation_return_error_literal (g_steal_pointer (&conversation->pending_invocation),
                                                       G_DBUS_ERROR, code, message);
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
        g_return_if_fail (GDM_IS_SESSION (self));

        if (self->user_verifier_interface != NULL) {
                gdm_dbus_user_verifier_emit_reset (self->user_verifier_interface);
        }

        do_reset (self);
}

void
gdm_session_set_timed_login_details (GdmSession *self,
                                     const char *username,
                                     int         delay)
{
        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (username != NULL);

        g_debug ("GdmSession: timed login details %s %d", username, delay);
        self->timed_login_username = g_strdup (username);
        self->timed_login_delay = delay;
}

gboolean
gdm_session_is_running (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        return self->session_pid > 0;
}

gboolean
gdm_session_is_frozen (GdmSession *self)
{
        g_autofree char *cgroup = NULL, *path = NULL, *data = NULL;
        g_auto (GStrv) arr = NULL;

        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        if (self->session_pid <= 0)
                return FALSE;

        if (sd_pid_get_cgroup (self->session_pid, &cgroup) < 0)
                return FALSE;

        path = g_build_filename ("/sys/fs/cgroup", cgroup, "cgroup.events", NULL);

        if (!g_file_get_contents (path, &data, NULL, NULL))
                return FALSE;

        arr = g_strsplit_set (data, " \n", -1);

        for (gsize i = 0; arr[i] != NULL; i++) {
                if (g_str_equal (arr[i], "frozen"))
                        return g_str_equal (arr[i + 1], "1");
        }
        return FALSE;
}

gboolean
gdm_session_client_is_connected (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        return self->outside_connections != NULL;
}

uid_t
gdm_session_get_allowed_user (GdmSession *self)
{
        return self->allowed_user;
}

void
gdm_session_start_reauthentication (GdmSession *self,
                                    GPid        pid_of_caller,
                                    uid_t       uid_of_caller)
{
        GdmSessionConversation *conversation;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (self->session_conversation != NULL);

        conversation = self->session_conversation;

        g_debug ("GdmSession: starting reauthentication for session %s for client with pid %d",
                 conversation->session_id,
                 (int) uid_of_caller);

        conversation->reauth_pid_of_caller = pid_of_caller;

        gdm_dbus_worker_call_start_reauthentication (conversation->worker_proxy,
                                                     (int) pid_of_caller,
                                                     (int) uid_of_caller,
                                                     conversation->worker_cancellable,
                                                     (GAsyncReadyCallback) on_reauthentication_started_cb,
                                                     conversation);
}

const char *
gdm_session_get_server_address (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return g_dbus_server_get_client_address (self->outside_server);
}

const char *
gdm_session_get_username (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return self->selected_user;
}

const char *
gdm_session_get_display_device (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return self->display_device;
}

const char *
gdm_session_get_display_seat_id (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        return self->display_seat_id;
}

const char *
gdm_session_get_session_id (GdmSession *self)
{
        GdmSessionConversation *conversation;

        g_return_val_if_fail (GDM_IS_SESSION (self), NULL);

        conversation = self->session_conversation;

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
        g_return_val_if_fail (service_name != NULL, NULL);

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
        char            *filename;
        g_autofree char *full_path = NULL;

        g_return_val_if_fail (self != NULL, FALSE);
        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        filename = get_session_filename (self);

        key_file = load_key_file_for_file (self, filename, NULL, &full_path);

        if (key_file == NULL) {
                goto out;
        }

        if (full_path != NULL && strstr (full_path, "/wayland-sessions/") != NULL) {
                is_wayland_session = TRUE;
        }
        g_debug ("GdmSession: checking if file '%s' is wayland session: %s", filename, is_wayland_session? "yes" : "no");

out:
        g_clear_pointer (&key_file, g_key_file_free);
        g_free (filename);
        return is_wayland_session;
}
#endif

static void
update_session_type (GdmSession *self)
{
#ifdef ENABLE_WAYLAND_SUPPORT
        gboolean is_wayland_session = FALSE;

        if (supports_session_type (self, "wayland"))
                is_wayland_session = gdm_session_is_wayland_session (self);

        if (is_wayland_session) {
                set_session_type (self, "wayland");
        } else {
                set_session_type (self, NULL);
        }
#endif
}

gboolean
gdm_session_session_registers (GdmSession *self)
{
        g_autoptr(GError) error = NULL;
        g_autoptr(GKeyFile) key_file = NULL;
        gboolean session_registers = FALSE;
        g_autofree char *filename = NULL;

        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

        filename = get_session_filename (self);

        key_file = load_key_file_for_file (self, filename, NULL, NULL);

        session_registers = g_key_file_get_boolean (key_file,
                                                    G_KEY_FILE_DESKTOP_GROUP,
                                                    "X-GDM-SessionRegisters",
                                                    &error);
        if (!session_registers &&
            error != NULL &&
            !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
                g_warning ("GdmSession: Couldn't read session file '%s'", filename);
                return FALSE;
        }

        g_debug ("GdmSession: '%s' %s self", filename,
                 session_registers ? "registers" : "does not register");

        return session_registers;
}

gboolean
gdm_session_bypasses_xsession (GdmSession *self)
{
        GError     *error;
        GKeyFile   *key_file;
        gboolean    res;
        gboolean    bypasses_xsession = FALSE;
        char       *filename = NULL;

        g_return_val_if_fail (GDM_IS_SESSION (self), FALSE);

#ifdef ENABLE_WAYLAND_SUPPORT
        if (gdm_session_is_wayland_session (self)) {
                bypasses_xsession = TRUE;
                goto out;
        }
#endif

        filename = get_session_filename (self);

        key_file = load_key_file_for_file (self, filename, "x11",  NULL);

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
        g_return_val_if_fail (GDM_IS_SESSION (self), GDM_SESSION_DISPLAY_MODE_NEW_VT);

        g_debug ("GdmSession: type %s, program? %s, seat %s",
                 self->session_type,
                 self->is_program_session? "yes" : "no",
                 self->display_seat_id);

        if (self->display_seat_id == NULL &&
            g_strcmp0 (self->session_type, "wayland") != 0) {
                return GDM_SESSION_DISPLAY_MODE_REUSE_VT;
        }

        if (g_strcmp0 (self->display_seat_id, "seat0") != 0) {
                return GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED;
        }

#ifdef ENABLE_USER_DISPLAY_SERVER
        /* All other cases (wayland login screen, X login screen,
         * wayland user session, X user session) use the NEW_VT
         * display mode.  That display mode means that GDM allocates
         * a new VT and jumps to it before starting the session. The
         * session is expected to use logind to gain access to the
         * display and input devices.
         *
         * GDM also has a LOGIND_MANAGED display mode which we can't
         * use yet. The difference between it and NEW_VT, is with it,
         * GDM doesn't do any VT handling at all, expecting the session
         * and logind to do everything.  The problem is, for wayland
         * sessions it will cause flicker until * this bug is fixed:
         *
         * https://bugzilla.gnome.org/show_bug.cgi?id=745141
         *
         * Likewise, for X sessions it's problematic because
         *   1) X doesn't call TakeControl before switching VTs
         *   2) X doesn't support getting started "in the background"
         *   right now.  It will die with an error if logind devices
         *   are paused when handed out.
         */
        return GDM_SESSION_DISPLAY_MODE_NEW_VT;
#else

#ifdef ENABLE_WAYLAND_SUPPORT
        /* Wayland sessions are for now assumed to run in a
         * mutter-launch-like environment, so we allocate
         * a new VT for them. */
        if (g_strcmp0 (self->session_type, "wayland") == 0) {
                return GDM_SESSION_DISPLAY_MODE_NEW_VT;
        }
#endif
        return GDM_SESSION_DISPLAY_MODE_REUSE_VT;
#endif
}

void
gdm_session_select_program (GdmSession *self,
                            const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (self));

        g_free (self->selected_program);

        self->selected_program = g_strdup (text);
}

void
gdm_session_select_session (GdmSession *self,
                            const char *text)
{
        GHashTableIter iter;
        gpointer key, value;

        g_return_if_fail (GDM_IS_SESSION (self));
        g_return_if_fail (text != NULL);

        g_debug ("GdmSession: selecting session '%s'", text);

        g_free (self->selected_session);
        self->selected_session = g_strdup (text);

        update_session_type (self);

        g_hash_table_iter_init (&iter, self->conversations);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionConversation *conversation;

                conversation = (GdmSessionConversation *) value;

                gdm_dbus_worker_call_set_session_name (conversation->worker_proxy,
                                                       get_session_name (self),
                                                       conversation->worker_cancellable,
                                                       NULL, NULL);
        }
}

static void
set_display_name (GdmSession *self,
                  const char *name)
{
        g_free (self->display_name);
        self->display_name = g_strdup (name);
}

static void
set_display_hostname (GdmSession *self,
                      const char *name)
{
        g_free (self->display_hostname);
        self->display_hostname = g_strdup (name);
}

static void
set_display_device (GdmSession *self,
                    const char *name)
{
        g_debug ("GdmSession: Setting display device: %s", name);
        g_free (self->display_device);
        self->display_device = g_strdup (name);
}

static void
set_display_seat_id (GdmSession *self,
                     const char *name)
{
        g_free (self->display_seat_id);
        self->display_seat_id = g_strdup (name);
}

static void
set_user_x11_authority_file (GdmSession *self,
                             const char *name)
{
        g_free (self->user_x11_authority_file);
        self->user_x11_authority_file = g_strdup (name);
}

static void
set_display_x11_authority_file (GdmSession *self,
                                const char *name)
{
        g_free (self->display_x11_authority_file);
        self->display_x11_authority_file = g_strdup (name);
}

static void
set_display_is_local (GdmSession *self,
                      gboolean    is_local)
{
        self->display_is_local = is_local;
}

static void
set_display_is_initial (GdmSession *self,
                        gboolean    is_initial)
{
        self->display_is_initial = is_initial;
}

static void
set_verification_mode (GdmSession                 *self,
                       GdmSessionVerificationMode  verification_mode)
{
        self->verification_mode = verification_mode;
}

static void
set_allowed_user (GdmSession *self,
                  uid_t       allowed_user)
{
        self->allowed_user = allowed_user;
}

static void
set_conversation_environment (GdmSession  *self,
                              char       **environment)
{
        g_strfreev (self->conversation_environment);
        self->conversation_environment = g_strdupv (environment);
}

static void
set_session_type (GdmSession *self,
                  const char *session_type)
{

        if (g_strcmp0 (self->session_type, session_type) != 0) {
                g_debug ("GdmSession: setting session to type '%s'", session_type? session_type : "");
                g_free (self->session_type);
                self->session_type = g_strdup (session_type);
        }
}

static void
set_remote_id (GdmSession *self,
               const char *remote_id)
{
        g_free (self->remote_id);
        self->remote_id = g_strdup (remote_id);
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
        case PROP_SUPPORTED_SESSION_TYPES:
                gdm_session_set_supported_session_types (self, g_value_get_boxed (value));
                break;
        case PROP_REMOTE_ID:
                set_remote_id (self, g_value_get_string (value));
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
                g_value_set_string (value, self->session_type);
                break;
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->display_name);
                break;
        case PROP_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->display_hostname);
                break;
        case PROP_DISPLAY_DEVICE:
                g_value_set_string (value, self->display_device);
                break;
        case PROP_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->display_seat_id);
                break;
        case PROP_USER_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->user_x11_authority_file);
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->display_x11_authority_file);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->display_is_local);
                break;
        case PROP_DISPLAY_IS_INITIAL:
                g_value_set_boolean (value, self->display_is_initial);
                break;
        case PROP_VERIFICATION_MODE:
                g_value_set_enum (value, self->verification_mode);
                break;
        case PROP_ALLOWED_USER:
                g_value_set_uint (value, self->allowed_user);
                break;
        case PROP_CONVERSATION_ENVIRONMENT:
                g_value_set_pointer (value, self->environment);
                break;
        case PROP_SUPPORTED_SESSION_TYPES:
                g_value_set_boxed (value, self->supported_session_types);
                break;
        case PROP_REMOTE_ID:
                g_value_set_string (value, self->remote_id);
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

        g_clear_pointer (&self->supported_session_types,
                         g_strfreev);
        g_clear_pointer (&self->conversations,
                         g_hash_table_unref);

        g_clear_object (&self->user_verifier_interface);
        g_clear_pointer (&self->user_verifier_extensions,
                         g_hash_table_unref);
        g_clear_object (&self->greeter_interface);
        g_clear_object (&self->remote_greeter_interface);
        g_clear_object (&self->chooser_interface);

        g_free (self->display_name);
        self->display_name = NULL;

        g_free (self->display_hostname);
        self->display_hostname = NULL;

        g_free (self->display_device);
        self->display_device = NULL;

        g_free (self->display_seat_id);
        self->display_seat_id = NULL;

        g_free (self->display_x11_authority_file);
        self->display_x11_authority_file = NULL;

        g_strfreev (self->conversation_environment);
        self->conversation_environment = NULL;

        if (self->worker_server != NULL) {
                g_dbus_server_stop (self->worker_server);
                g_clear_object (&self->worker_server);
        }

        if (self->outside_server != NULL) {
                g_dbus_server_stop (self->outside_server);
                g_clear_object (&self->outside_server);
        }

        if (self->environment != NULL) {
                g_hash_table_destroy (self->environment);
                self->environment = NULL;
        }

        G_OBJECT_CLASS (gdm_session_parent_class)->dispose (object);
}

static void
gdm_session_finalize (GObject *object)
{
        GdmSession   *self;
        GObjectClass *parent_class;

        self = GDM_SESSION (object);

        g_free (self->selected_user);
        g_free (self->selected_session);
        g_free (self->saved_session);
        g_free (self->saved_language);

        g_free (self->fallback_session_name);

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

        signals [CONVERSATION_STARTED] =
                g_signal_new ("conversation-started",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CONVERSATION_STOPPED] =
                g_signal_new ("conversation-stopped",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SETUP_COMPLETE] =
                g_signal_new ("setup-complete",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
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
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_INT);
        signals [CREDENTIALS_ESTABLISHED] =
                g_signal_new ("credentials-established",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [VERIFICATION_COMPLETE] =
                g_signal_new ("verification-complete",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
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
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_STRING);
        signals [SESSION_OPENED_FAILED] =
                g_signal_new ("session-opened-failed",
                              GDM_TYPE_SESSION,
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
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
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
                              0,
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
                              0,
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
                              0,
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
                              0,
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
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              G_TYPE_INT);
        signals [CANCELLED] =
                g_signal_new ("cancelled",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [CLIENT_REJECTED] =
                g_signal_new ("client-rejected",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
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
                              0,
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
                              0,
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
                              0,
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
                              0,
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
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STOP_CONFLICTING_SESSION] =
                g_signal_new ("stop-conflicting-session",
                              GDM_TYPE_SESSION,
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_VERIFICATION_MODE,
                                         g_param_spec_enum ("verification-mode",
                                                            "verification mode",
                                                            "verification mode",
                                                            GDM_TYPE_SESSION_VERIFICATION_MODE,
                                                            GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_ALLOWED_USER,
                                         g_param_spec_uint ("allowed-user",
                                                            "allowed user",
                                                            "allowed user ",
                                                            0,
                                                            G_MAXUINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_CONVERSATION_ENVIRONMENT,
                                         g_param_spec_pointer ("conversation-environment",
                                                               "conversation environment",
                                                               "conversation environment",
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_TYPE,
                                         g_param_spec_string ("session-type",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "display hostname",
                                                              "display hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_INITIAL,
                                         g_param_spec_boolean ("display-is-initial",
                                                               "display is initial",
                                                               "display is initial",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("display-x11-authority-file",
                                                              "display x11 authority file",
                                                              "display x11 authority file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        /* not construct only */
        g_object_class_install_property (object_class,
                                         PROP_USER_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("user-x11-authority-file",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_DEVICE,
                                         g_param_spec_string ("display-device",
                                                              "display device",
                                                              "display device",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "display seat id",
                                                              "display seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_SUPPORTED_SESSION_TYPES,
                                         g_param_spec_boxed ("supported-session-types",
                                                             "supported session types",
                                                             "supported session types",
                                                             G_TYPE_STRV,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_REMOTE_ID,
                                         g_param_spec_string ("remote-id",
                                                              "remote id",
                                                              "remote id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        /* Ensure we can resolve errors */
        gdm_dbus_error_ensure (GDM_SESSION_WORKER_ERROR);
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
gdm_session_get_pid (GdmSession *self)
{
        g_return_val_if_fail (GDM_IS_SESSION (self), 0);

        return self->session_pid;
}
