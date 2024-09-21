/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <sys/kd.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>

#include <security/pam_appl.h>

#ifdef HAVE_LOGINCAP
#include <login_cap.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <json-glib/json-glib.h>

#ifdef ENABLE_X11_SUPPORT
#include <X11/Xauth.h>
#endif

#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>

#ifdef ENABLE_SYSTEMD_JOURNAL
#include <systemd/sd-journal.h>
#endif

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif /* HAVE_SELINUX */

#include "gdm-common.h"
#include "gdm-log.h"

#ifdef SUPPORTS_PAM_EXTENSIONS
#include "gdm-pam-extensions.h"
#endif

#include "gdm-dbus-glue.h"
#include "gdm-session-worker.h"
#include "gdm-session-glue.h"
#include "gdm-session.h"

#if defined (HAVE_ADT)
#include "gdm-session-solaris-auditor.h"
#elif defined (HAVE_LIBAUDIT)
#include "gdm-session-linux-auditor.h"
#else
#include "gdm-session-auditor.h"
#endif

#include "gdm-session-settings.h"

#define GDM_SESSION_DBUS_PATH         "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_NAME         "org.gnome.DisplayManager.Session"
#define GDM_SESSION_DBUS_ERROR_CANCEL "org.gnome.DisplayManager.Session.Error.Cancel"

#define GDM_WORKER_DBUS_PATH "/org/gnome/DisplayManager/Worker"

#ifndef GDM_PASSWD_AUXILLARY_BUFFER_SIZE
#define GDM_PASSWD_AUXILLARY_BUFFER_SIZE 1024
#endif

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"
#endif

#ifndef GDM_SESSION_ROOT_UID
#define GDM_SESSION_ROOT_UID 0
#endif

#ifndef GDM_SESSION_LOG_FILENAME
#define GDM_SESSION_LOG_FILENAME "session.log"
#endif

#define MAX_FILE_SIZE     65536
#define MAX_LOGS          5

#define RELEASE_DISPLAY_SIGNAL (SIGRTMAX)
#define ACQUIRE_DISPLAY_SIGNAL (SIGRTMAX - 1)

typedef struct
{
        GdmSessionWorker *worker;
        GdmSession       *session;
        GPid              pid_of_caller;
        uid_t             uid_of_caller;

} ReauthenticationRequest;

struct _GdmSessionWorker
{
        GdmDBusWorkerSkeleton parent;
        GdmSessionWorkerState state;

        int               exit_code;

        pam_handle_t     *pam_handle;

        GPid              child_pid;
        guint             child_watch_id;

        /* from Setup */
        char             *service;
        char             *x11_display_name;
        char             *x11_authority_file;
        char             *display_device;
        char             *display_seat_id;
        char             *hostname;
        char             *username;
        char             *log_file;
        char             *session_id;
        uid_t             uid;
        gid_t             gid;
        gboolean          password_is_required;
        char            **extensions;

        int               cred_flags;
        int               session_vt;
        int               session_tty_fd;

        char            **arguments;
        guint32           cancelled : 1;
        guint32           timed_out : 1;
        guint32           is_program_session : 1;
        guint32           is_reauth_session : 1;
        guint32           display_is_local : 1;
        guint32           display_is_initial : 1;
        guint32           seat0_has_vts : 1;
        guint             state_change_idle_id;
        GdmSessionDisplayMode display_mode;

        char                 *server_address;
        GDBusConnection      *connection;
        GdmDBusWorkerManager *manager;

        GHashTable         *reauthentication_requests;

        GdmSessionAuditor  *auditor;
        GdmSessionSettings *user_settings;

        GDBusMethodInvocation *pending_invocation;
};

#ifdef SUPPORTS_PAM_EXTENSIONS
static char gdm_pam_extension_environment_block[_POSIX_ARG_MAX];

static const char * const
gdm_supported_pam_extensions[] = {
        GDM_PAM_EXTENSION_CHOICE_LIST,
        GDM_PAM_EXTENSION_CUSTOM_JSON,
        NULL
};
#endif

enum {
        PROP_0,
        PROP_SERVER_ADDRESS,
        PROP_IS_REAUTH_SESSION,
        PROP_STATE,
};

static void     gdm_session_worker_class_init   (GdmSessionWorkerClass *klass);
static void     gdm_session_worker_init         (GdmSessionWorker      *session_worker);
static void     gdm_session_worker_finalize     (GObject               *object);

static void     gdm_session_worker_set_environment_variable (GdmSessionWorker *worker,
                                                             const char       *key,
                                                             const char       *value);

static void     queue_state_change              (GdmSessionWorker      *worker);

static void     worker_interface_init           (GdmDBusWorkerIface *iface);


typedef int (* GdmSessionWorkerPamNewMessagesFunc) (int,
                                                    const struct pam_message **,
                                                    struct pam_response **,
                                                    gpointer);

G_DEFINE_TYPE_WITH_CODE (GdmSessionWorker,
                         gdm_session_worker,
                         GDM_DBUS_TYPE_WORKER_SKELETON,
                         G_IMPLEMENT_INTERFACE (GDM_DBUS_TYPE_WORKER,
                                                worker_interface_init))

/* adapted from glib script_execute */
static void
script_execute (const gchar *file,
                char       **argv,
                char       **envp,
                gboolean     search_path)
{
        /* Count the arguments.  */
        int argc = 0;

        while (argv[argc]) {
                ++argc;
        }

        /* Construct an argument list for the shell.  */
        {
                char **new_argv;

                new_argv = g_new0 (gchar*, argc + 2); /* /bin/sh and NULL */

                new_argv[0] = (char *) "/bin/sh";
                new_argv[1] = (char *) file;
                while (argc > 0) {
                        new_argv[argc + 1] = argv[argc];
                        --argc;
                }

                /* Execute the shell. */
                if (envp) {
                        execve (new_argv[0], new_argv, envp);
                } else {
                        execv (new_argv[0], new_argv);
                }

                g_free (new_argv);
        }
}

static char *
my_strchrnul (const char *str, char c)
{
        char *p = (char*) str;
        while (*p && (*p != c)) {
                ++p;
        }

        return p;
}

/* adapted from glib g_execute */
static gint
gdm_session_execute (const char *file,
                     char      **argv,
                     char      **envp,
                     gboolean    search_path)
{
        if (*file == '\0') {
                /* We check the simple case first. */
                errno = ENOENT;
                return -1;
        }

        if (!search_path || strchr (file, '/') != NULL) {
                /* Don't search when it contains a slash. */
                if (envp) {
                        execve (file, argv, envp);
                } else {
                        execv (file, argv);
                }

                if (errno == ENOEXEC) {
                        script_execute (file, argv, envp, FALSE);
                }
        } else {
                gboolean got_eacces = 0;
                const char *path, *p;
                char *name, *freeme;
                gsize len;
                gsize pathlen;

                path = g_getenv ("PATH");
                if (path == NULL) {
                        /* There is no `PATH' in the environment.  The default
                         * search path in libc is the current directory followed by
                         * the path `confstr' returns for `_CS_PATH'.
                         */

                        /* In GLib we put . last, for security, and don't use the
                         * unportable confstr(); UNIX98 does not actually specify
                         * what to search if PATH is unset. POSIX may, dunno.
                         */

                        path = "/bin:/usr/bin:.";
                }

                len = strlen (file) + 1;
                pathlen = strlen (path);
                freeme = name = g_malloc (pathlen + len + 1);

                /* Copy the file name at the top, including '\0'  */
                memcpy (name + pathlen + 1, file, len);
                name = name + pathlen;
                /* And add the slash before the filename  */
                *name = '/';

                p = path;
                do {
                        char *startp;

                        path = p;
                        p = my_strchrnul (path, ':');

                        if (p == path) {
                                /* Two adjacent colons, or a colon at the beginning or the end
                                 * of `PATH' means to search the current directory.
                                 */
                                startp = name + 1;
                        } else {
                                startp = memcpy (name - (p - path), path, p - path);
                        }

                        /* Try to execute this name.  If it works, execv will not return.  */
                        if (envp) {
                                execve (startp, argv, envp);
                        } else {
                                execv (startp, argv);
                        }

                        if (errno == ENOEXEC) {
                                script_execute (startp, argv, envp, search_path);
                        }

                        switch (errno) {
                        case EACCES:
                                /* Record the we got a `Permission denied' error.  If we end
                                 * up finding no executable we can use, we want to diagnose
                                 * that we did find one but were denied access.
                                 */
                                got_eacces = TRUE;

                                /* FALL THRU */

                        case ENOENT:
#ifdef ESTALE
                        case ESTALE:
#endif
#ifdef ENOTDIR
                        case ENOTDIR:
#endif
                                /* Those errors indicate the file is missing or not executable
                                 * by us, in which case we want to just try the next path
                                 * directory.
                                 */
                                break;

                        default:
                                /* Some other error means we found an executable file, but
                                 * something went wrong executing it; return the error to our
                                 * caller.
                                 */
                                g_free (freeme);
                                return -1;
                        }
                } while (*p++ != '\0');

                /* We tried every element and none of them worked.  */
                if (got_eacces) {
                        /* At least one failure was due to permissions, so report that
                         * error.
                         */
                        errno = EACCES;
                }

                g_free (freeme);
        }

        /* Return the error from the last attempt (probably ENOENT).  */
        return -1;
}

/*
 * This function is called with username set to NULL to update the
 * auditor username value.
 */
static gboolean
gdm_session_worker_get_username (GdmSessionWorker  *worker,
                                 char             **username)
{
        gconstpointer item;

        g_assert (worker->pam_handle != NULL);

        if (pam_get_item (worker->pam_handle, PAM_USER, &item) == PAM_SUCCESS) {
                if (username != NULL) {
                        *username = g_strdup ((char *) item);
                        g_debug ("GdmSessionWorker: username is '%s'",
                                 *username != NULL ? *username : "<unset>");
                }

                if (worker->auditor != NULL) {
                        gdm_session_auditor_set_username (worker->auditor, (char *)item);
                }

                return TRUE;
        }

        return FALSE;
}

static void
attempt_to_load_user_settings (GdmSessionWorker *worker,
                               const char       *username)
{
        if (worker->user_settings == NULL)
                return;

        if (gdm_session_settings_is_loaded (worker->user_settings))
                return;

        g_debug ("GdmSessionWorker: attempting to load user settings");
        gdm_session_settings_load (worker->user_settings,
                                   username);
}

static void
gdm_session_worker_update_username (GdmSessionWorker *worker)
{
        g_autofree char *username = NULL;
        gboolean res;

        res = gdm_session_worker_get_username (worker, &username);
        if (res) {
                g_debug ("GdmSessionWorker: old-username='%s' new-username='%s'",
                         worker->username != NULL ? worker->username : "<unset>",
                         username != NULL ? username : "<unset>");


                gdm_session_auditor_set_username (worker->auditor, worker->username);

                if ((worker->username == username) ||
                    ((worker->username != NULL) && (username != NULL) &&
                     (strcmp (worker->username, username) == 0)))
                        return;

                g_debug ("GdmSessionWorker: setting username to '%s'", username);

                g_free (worker->username);
                worker->username = g_steal_pointer (&username);

                gdm_dbus_worker_emit_username_changed (GDM_DBUS_WORKER (worker),
                                                       worker->username);

                /* We have a new username to try. If we haven't been able to
                 * read user settings up until now, then give it a go now
                 * (see the comment in do_setup for rationale on why it's useful
                 * to keep trying to read settings)
                 */
                if (worker->username != NULL &&
                    worker->username[0] != '\0') {
                        attempt_to_load_user_settings (worker, worker->username);
                }
        }
}

static gboolean
gdm_session_worker_ask_question (GdmSessionWorker *worker,
                                 const char       *question,
                                 char            **answerp)
{
        return gdm_dbus_worker_manager_call_info_query_sync (worker->manager,
                                                             worker->service,
                                                             question,
                                                             answerp,
                                                             NULL,
                                                             NULL);
}

static gboolean
gdm_session_worker_ask_for_secret (GdmSessionWorker *worker,
                                   const char       *question,
                                   char            **answerp)
{
        return gdm_dbus_worker_manager_call_secret_info_query_sync (worker->manager,
                                                                    worker->service,
                                                                    question,
                                                                    answerp,
                                                                    NULL,
                                                                    NULL);
}

static gboolean
gdm_session_worker_report_info (GdmSessionWorker *worker,
                                const char       *info)
{
        return gdm_dbus_worker_manager_call_info_sync (worker->manager,
                                                       worker->service,
                                                       info,
                                                       NULL,
                                                       NULL);
}

static gboolean
gdm_session_worker_report_problem (GdmSessionWorker *worker,
                                   const char       *problem)
{
        return gdm_dbus_worker_manager_call_problem_sync (worker->manager,
                                                          worker->service,
                                                          problem,
                                                          NULL,
                                                          NULL);
}

#ifdef SUPPORTS_PAM_EXTENSIONS
static gboolean
gdm_session_worker_ask_list_of_choices (GdmSessionWorker *worker,
                                        const char       *prompt_message,
                                        GdmChoiceList    *list,
                                        char            **answerp)
{
        g_autoptr(GVariant) choices_as_variant = NULL;
        g_autoptr(GError) error = NULL;
        GVariantBuilder builder;
        gboolean res;
        size_t i;

        g_debug ("GdmSessionWorker: presenting user with list of choices:");

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

        for (i = 0; i < list->number_of_items; i++) {
                if (list->items[i].key == NULL) {
                        g_warning ("choice list contains item with NULL key");
                        g_variant_builder_clear (&builder);
                        return FALSE;
                }
                g_debug ("GdmSessionWorker:        choices['%s'] = \"%s\"", list->items[i].key, list->items[i].text);
                g_variant_builder_add (&builder, "{ss}", list->items[i].key, list->items[i].text);
        }
        g_debug ("GdmSessionWorker: (and waiting for reply)");

        choices_as_variant = g_variant_builder_end (&builder);

        res = gdm_dbus_worker_manager_call_choice_list_query_sync (worker->manager,
                                                                    worker->service,
                                                                    prompt_message,
                                                                    choices_as_variant,
                                                                    answerp,
                                                                    NULL,
                                                                    &error);

        if (! res) {
                g_debug ("GdmSessionWorker: list request failed: %s", error->message);
        } else {
                g_debug ("GdmSessionWorker: user selected '%s'", *answerp);
        }

        return res;
}

static gboolean
gdm_session_worker_process_choice_list_request (GdmSessionWorker                   *worker,
                                                GdmPamExtensionChoiceListRequest  *request,
                                                GdmPamExtensionChoiceListResponse *response)
{
        return gdm_session_worker_ask_list_of_choices (worker, request->prompt_message, &request->list, &response->key);
}

static gboolean
gdm_session_worker_process_custom_json_protocol (GdmSessionWorker            *worker,
                                                 GdmPamExtensionJSONProtocol *request,
                                                 GdmPamExtensionJSONProtocol *response)
{
        g_autoptr(GError) error = NULL;
        g_autoptr(JsonParser) parser = NULL;
        g_autofree char *json_reply = NULL;

        g_debug ("GdmSessionWorker: sending custom JSON protocol request: %s v%d",
                 request->protocol_name, request->version);
        g_debug ("GdmSessionWorker: (and waiting for reply)");

        if (!request->json) {
                g_warning ("GdmSessionWorker: custom JSON request is not valid");
                return FALSE;
        }

        parser = json_parser_new_immutable ();
        if (!json_parser_load_from_data (parser, request->json, -1, &error)) {
                g_warning ("GdmSessionWorker: custom JSON request is not valid JSON: %s",
                           error->message);
                return FALSE;
        }

        if (!gdm_dbus_worker_manager_call_custom_json_request_sync (worker->manager,
                                                                    worker->service,
                                                                    request->protocol_name,
                                                                    request->version,
                                                                    request->json,
                                                                    &response->json,
                                                                    NULL,
                                                                    &error)) {
                g_warning ("GdmSessionWorker: custom JSON request failed: %s",
                           error->message);
                return FALSE;
        }

        if (!response->json) {
                g_warning ("GdmSessionWorker: custom JSON request returned invalid data");
                return FALSE;
        }

        /* No need to validate JSON reply again since that's what we got from
         * the client and validation happens at daemon level.
         */
        return TRUE;
}

static gboolean
gdm_session_worker_process_extended_pam_message (GdmSessionWorker          *worker,
                                                 const struct pam_message  *query,
                                                 char                     **response)
{
        GdmPamExtensionMessage *extended_message;
        gboolean res;

        extended_message = GDM_PAM_EXTENSION_MESSAGE_FROM_PAM_MESSAGE (query);

        if (GDM_PAM_EXTENSION_MESSAGE_TRUNCATED (extended_message)) {
                g_warning ("PAM service requested binary response for truncated query");
                return FALSE;
        }

        if (GDM_PAM_EXTENSION_MESSAGE_INVALID_TYPE (extended_message)) {
                g_warning ("PAM service requested binary response for unadvertised query type");
                return FALSE;
        }

        if (GDM_PAM_EXTENSION_MESSAGE_MATCH (extended_message, worker->extensions, GDM_PAM_EXTENSION_CHOICE_LIST)) {
                GdmPamExtensionChoiceListRequest *list_request = (GdmPamExtensionChoiceListRequest *) extended_message;
                GdmPamExtensionChoiceListResponse *list_response = malloc (GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_SIZE);

                g_debug ("GdmSessionWorker: received extended pam message '%s'", GDM_PAM_EXTENSION_CHOICE_LIST);

                GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_INIT (list_response);

                res = gdm_session_worker_process_choice_list_request (worker, list_request, list_response);

                if (! res) {
                        g_free (list_response);
                        return FALSE;
                }

                *response = GDM_PAM_EXTENSION_MESSAGE_TO_PAM_REPLY (list_response);
                return TRUE;
        } else if (GDM_PAM_EXTENSION_MESSAGE_MATCH (extended_message, worker->extensions, GDM_PAM_EXTENSION_CUSTOM_JSON)) {
                GdmPamExtensionJSONProtocol *json_request = (GdmPamExtensionJSONProtocol *) extended_message;
                g_autofree GdmPamExtensionJSONProtocol *json_response = malloc (GDM_PAM_EXTENSION_CUSTOM_JSON_SIZE);

                g_debug ("GdmSessionWorker: received extended pam message '%s'", GDM_PAM_EXTENSION_CUSTOM_JSON);

                GDM_PAM_EXTENSION_CUSTOM_JSON_RESPONSE_INIT (json_response,
                                                              json_request->protocol_name,
                                                              json_request->version);

                if (!gdm_session_worker_process_custom_json_protocol (worker, json_request, json_response)) {
                        return FALSE;
                }

                *response = GDM_PAM_EXTENSION_MESSAGE_TO_PAM_REPLY (g_steal_pointer (&json_response));
                return TRUE;
        } else {
                g_debug ("GdmSessionWorker: received extended pam message of unknown type %u", (unsigned int) extended_message->type);
                return FALSE;

        }

        return TRUE;
}
#endif

static char *
convert_to_utf8 (const char *str)
{
        g_autofree char *utf8 = NULL;
        utf8 = g_locale_to_utf8 (str,
                                 -1,
                                 NULL,
                                 NULL,
                                 NULL);

        /* if we couldn't convert text from locale then
         * assume utf-8 and hope for the best */
        if (utf8 == NULL) {
                char *p;
                char *q;

                utf8 = g_strdup (str);

                p = utf8;
                while (*p != '\0' && !g_utf8_validate ((const char *)p, -1, (const char **)&q)) {
                        *q = '?';
                        p = q + 1;
                }
        }

        return g_steal_pointer (&utf8);
}

static gboolean
gdm_session_worker_process_pam_message (GdmSessionWorker          *worker,
                                        const struct pam_message  *query,
                                        char                     **response)
{
        g_autofree char *user_answer = NULL;
        g_autofree char *utf8_msg = NULL;
        g_autofree char *msg = NULL;
        gboolean res;

        if (response != NULL) {
                *response = NULL;
        }

        gdm_session_worker_update_username (worker);

#ifdef SUPPORTS_PAM_EXTENSIONS
        if (query->msg_style == PAM_BINARY_PROMPT)
                return gdm_session_worker_process_extended_pam_message (worker, query, response);
#endif

        g_debug ("GdmSessionWorker: received pam message of type %u with payload '%s'",
                 query->msg_style, query->msg);

        utf8_msg = convert_to_utf8 (query->msg);

        worker->cancelled = FALSE;
        worker->timed_out = FALSE;

        switch (query->msg_style) {
        case PAM_PROMPT_ECHO_ON:
                res = gdm_session_worker_ask_question (worker, utf8_msg, &user_answer);
                break;
        case PAM_PROMPT_ECHO_OFF:
                res = gdm_session_worker_ask_for_secret (worker, utf8_msg, &user_answer);
                break;
        case PAM_TEXT_INFO:
                res = gdm_session_worker_report_info (worker, utf8_msg);
                break;
        case PAM_ERROR_MSG:
                res = gdm_session_worker_report_problem (worker, utf8_msg);
                break;
#ifdef PAM_RADIO_TYPE
        case PAM_RADIO_TYPE:
                msg = g_strdup_printf ("%s (yes/no)", utf8_msg);
                res = gdm_session_worker_ask_question (worker, msg, &user_answer);
                break;
#endif
        default:
                res = FALSE;
                g_warning ("Unknown and unhandled message type %d\n",
                           query->msg_style);

                break;
        }

        if (worker->timed_out) {
                gdm_dbus_worker_emit_cancel_pending_query (GDM_DBUS_WORKER (worker));
                worker->timed_out = FALSE;
        }

        if (user_answer != NULL) {
                /* we strndup and g_free to make sure we return malloc'd
                 * instead of g_malloc'd memory.  PAM_MAX_RESP_SIZE includes
                 * the '\0' terminating character, thus the "- 1".
                 */
                if (res && response != NULL) {
                        *response = strndup (user_answer, PAM_MAX_RESP_SIZE - 1);
                }

                memset (user_answer, '\0', strlen (user_answer));

                g_debug ("GdmSessionWorker: trying to get updated username");

                res = TRUE;
        }

        return res;
}

static const char *
get_max_retries_error_message (GdmSessionWorker *worker)
{
        if (g_strcmp0 (worker->service, "gdm-password") == 0)
                return _("You reached the maximum password authentication attempts, please try another method");

        if (g_strcmp0 (worker->service, "gdm-autologin") == 0)
                return _("You reached the maximum auto login attempts, please try another authentication method");

        if (g_strcmp0 (worker->service, "gdm-fingerprint") == 0)
                return _("You reached the maximum fingerprint authentication attempts, please try another method");

        if (g_strcmp0 (worker->service, "gdm-smartcard") == 0)
                return _("You reached the maximum smart card authentication attempts, please try another method");

        return _("You reached the maximum authentication attempts, please try another method");
}

static const char *
get_generic_error_message (GdmSessionWorker *worker)
{
        if (g_strcmp0 (worker->service, "gdm-password") == 0)
                return _("Sorry, password authentication didn’t work. Please try again.");

        if (g_strcmp0 (worker->service, "gdm-autologin") == 0)
                return _("Sorry, auto login didn’t work. Please try again.");

        if (g_strcmp0 (worker->service, "gdm-fingerprint") == 0)
                return _("Sorry, fingerprint authentication didn’t work. Please try again.");

        if (g_strcmp0 (worker->service, "gdm-smartcard") == 0)
                return _("Sorry, smart card authentication didn’t work. Please try again.");

        return _("Sorry, that didn’t work. Please try again.");
}

static const char *
get_friendly_error_message (GdmSessionWorker *worker,
                            int               error_code)
{
        switch (error_code) {
            case PAM_SUCCESS:
            case PAM_IGNORE:
                return "";
                break;

            case PAM_ACCT_EXPIRED:
            case PAM_AUTHTOK_EXPIRED:
                return _("Your account was given a time limit that’s now passed.");
                break;

            case PAM_MAXTRIES:
                return get_max_retries_error_message (worker);

            default:
                break;
        }

        return get_generic_error_message (worker);
}

static int
gdm_session_worker_pam_new_messages_handler (int                        number_of_messages,
                                             const struct pam_message **messages,
                                             struct pam_response      **responses,
                                             GdmSessionWorker          *worker)
{
        struct pam_response *replies;
        int                  return_value;
        int                  i;

        g_debug ("GdmSessionWorker: %d new messages received from PAM\n", number_of_messages);

        return_value = PAM_CONV_ERR;

        if (number_of_messages < 0) {
                return PAM_CONV_ERR;
        }

        if (number_of_messages == 0) {
                if (responses) {
                        *responses = NULL;
                }

                return PAM_SUCCESS;
        }

        /* we want to generate one reply for every question
         */
        replies = (struct pam_response *) calloc (number_of_messages,
                                                  sizeof (struct pam_response));
        for (i = 0; i < number_of_messages; i++) {
                gboolean got_response;
                char    *response;

                response = NULL;
                got_response = gdm_session_worker_process_pam_message (worker,
                                                                       messages[i],
                                                                       &response);
                if (!got_response) {
                        goto out;
                }

                replies[i].resp = response;
                replies[i].resp_retcode = PAM_SUCCESS;
        }

        return_value = PAM_SUCCESS;

 out:
        if (return_value != PAM_SUCCESS || responses == NULL) {
                for (i = 0; i < number_of_messages; i++) {
                        if (replies[i].resp != NULL) {
                                memset (replies[i].resp, 0, strlen (replies[i].resp));
                                free (replies[i].resp);
                        }
                        memset (&replies[i], 0, sizeof (replies[i]));
                }
                free (replies);
                replies = NULL;
        }

        if (responses) {
                *responses = replies;
        }

        g_debug ("GdmSessionWorker: PAM conversation returning %d: %s",
                 return_value,
                 pam_strerror (worker->pam_handle, return_value));

        return return_value;
}

static void
gdm_session_worker_start_auditor (GdmSessionWorker *worker)
{
    /* Use dummy auditor so program session doesn't pollute user audit logs
     */
    if (worker->is_program_session) {
            worker->auditor = gdm_session_auditor_new (worker->hostname,
                                                       worker->display_device);
            return;
    }

/* FIXME: it may make sense at some point to keep a list of
 * auditors, instead of assuming they are mutually exclusive
 */
#if defined (HAVE_ADT)
        worker->auditor = gdm_session_solaris_auditor_new (worker->hostname,
                                                           worker->display_device);
#elif defined (HAVE_LIBAUDIT)
        worker->auditor = gdm_session_linux_auditor_new (worker->hostname,
                                                         worker->display_device);
#else
        worker->auditor = gdm_session_auditor_new (worker->hostname,
                                                   worker->display_device);
#endif
}

static void
gdm_session_worker_stop_auditor (GdmSessionWorker *worker)
{
        g_object_unref (worker->auditor);
        worker->auditor = NULL;
}

static void
on_release_display (int signal)
{
        int fd;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);
        ioctl(fd, VT_RELDISP, 1);
        close(fd);
}

static void
on_acquire_display (int signal)
{
        int fd;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);
        ioctl(fd, VT_RELDISP, VT_ACKACQ);
        close(fd);
}

static gboolean
handle_terminal_vt_switches (GdmSessionWorker *worker,
                             int               tty_fd)
{
        struct vt_mode setmode_request = { 0 };
        gboolean succeeded = TRUE;

        setmode_request.mode = VT_PROCESS;
        setmode_request.relsig = RELEASE_DISPLAY_SIGNAL;
        setmode_request.acqsig = ACQUIRE_DISPLAY_SIGNAL;

        if (ioctl (tty_fd, VT_SETMODE, &setmode_request) < 0) {
                g_debug ("GdmSessionWorker: couldn't manage VTs manually: %m");
                succeeded = FALSE;
        }

        signal (RELEASE_DISPLAY_SIGNAL, on_release_display);
        signal (ACQUIRE_DISPLAY_SIGNAL, on_acquire_display);

        return succeeded;
}

static void
fix_terminal_vt_mode (GdmSessionWorker  *worker,
                      int                tty_fd)
{
        struct vt_mode getmode_reply = { 0 };
        int kernel_display_mode = 0;
        gboolean mode_fixed = FALSE;
        gboolean succeeded = TRUE;

        if (ioctl (tty_fd, VT_GETMODE, &getmode_reply) < 0) {
                g_debug ("GdmSessionWorker: couldn't query VT mode: %m");
                succeeded = FALSE;
        }

        if (getmode_reply.mode != VT_AUTO) {
                goto out;
        }

        if (ioctl (tty_fd, KDGETMODE, &kernel_display_mode) < 0) {
                g_debug ("GdmSessionWorker: couldn't query kernel display mode: %m");
                succeeded = FALSE;
        }

        if (kernel_display_mode == KD_TEXT) {
                goto out;
        }

        /* VT is in the anti-social state of VT_AUTO + KD_GRAPHICS,
         * fix it.
         */
        succeeded = handle_terminal_vt_switches (worker, tty_fd);
        mode_fixed = TRUE;
out:
        if (!succeeded) {
                g_error ("GdmSessionWorker: couldn't set up terminal, aborting...");
                return;
        }

        g_debug ("GdmSessionWorker: VT mode did %sneed to be fixed",
                 mode_fixed? "" : "not ");
}

static void
jump_to_vt (GdmSessionWorker  *worker,
            int                vt_number)
{
        int fd;
        int active_vt_tty_fd;
        int active_vt = -1;
        struct vt_stat vt_state = { 0 };

        g_debug ("GdmSessionWorker: jumping to VT %d", vt_number);
        active_vt_tty_fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

        if (worker->session_tty_fd != -1) {
                static const char *clear_screen_escape_sequence = "\33[H\33[2J";

                /* let's make sure the new VT is clear */
                write (worker->session_tty_fd,
                       clear_screen_escape_sequence,
                       sizeof (clear_screen_escape_sequence));

                fd = worker->session_tty_fd;

                handle_terminal_vt_switches (worker, fd);

                g_debug ("GdmSessionWorker: first setting graphics mode to prevent flicker");
                if (ioctl (fd, KDSETMODE, KD_GRAPHICS) < 0) {
                        g_debug ("GdmSessionWorker: couldn't set graphics mode: %m");
                }
        } else {
                fd = active_vt_tty_fd;
        }

        /* It's possible that the current VT was left in a broken
         * combination of states (KD_GRAPHICS with VT_AUTO), that
         * can't be switched away from.  This call makes sure things
         * are set in a way that VT_ACTIVATE should work and
         * VT_WAITACTIVE shouldn't hang.
         */
        fix_terminal_vt_mode (worker, active_vt_tty_fd);

        if (ioctl (fd, VT_GETSTATE, &vt_state) < 0) {
                g_debug ("GdmSessionWorker: couldn't get current VT: %m");
        } else {
                active_vt = vt_state.v_active;
        }

        if (active_vt != vt_number) {
                if (ioctl (fd, VT_ACTIVATE, vt_number) < 0) {
                        g_debug ("GdmSessionWorker: couldn't initiate jump to VT %d: %m",
                                 vt_number);
                } else if (ioctl (fd, VT_WAITACTIVE, vt_number) < 0) {
                        g_debug ("GdmSessionWorker: couldn't finalize jump to VT %d: %m",
                                 vt_number);
                }
        }

        close (active_vt_tty_fd);
}

static void
gdm_session_worker_set_state (GdmSessionWorker      *worker,
                              GdmSessionWorkerState  state)
{
        if (worker->state == state)
                return;

        worker->state = state;
        g_object_notify (G_OBJECT (worker), "state");
}

static void
gdm_session_worker_uninitialize_pam (GdmSessionWorker *worker,
                                     int               status)
{
        g_debug ("GdmSessionWorker: uninitializing PAM");

        if (worker->pam_handle == NULL)
                return;

        gdm_session_worker_get_username (worker, NULL);

        if (worker->state >= GDM_SESSION_WORKER_STATE_SESSION_OPENED) {
                pam_close_session (worker->pam_handle, 0);
                gdm_session_auditor_report_logout (worker->auditor);
        } else {
                gdm_session_auditor_report_login_failure (worker->auditor,
                                                          status,
                                                          pam_strerror (worker->pam_handle, status));
        }

        if (worker->state >= GDM_SESSION_WORKER_STATE_ACCREDITED) {
                pam_setcred (worker->pam_handle, PAM_DELETE_CRED);
        }

        pam_end (worker->pam_handle, status);
        worker->pam_handle = NULL;

        gdm_session_worker_stop_auditor (worker);

        g_debug ("GdmSessionWorker: state NONE");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_NONE);
}

static char *
_get_tty_for_pam (const char *x11_display_name,
                  const char *display_device)
{
#ifdef __sun
        return g_strdup (display_device);
#else
        return g_strdup (x11_display_name);
#endif
}

#if defined(PAM_XAUTHDATA) && defined(ENABLE_X11_SUPPORT)
static struct pam_xauth_data *
_get_xauth_for_pam (const char *x11_authority_file)
{
        FILE                  *fh;
        Xauth                 *auth = NULL;
        struct pam_xauth_data *retval = NULL;
        gsize                  len = sizeof (*retval) + 1;

        fh = fopen (x11_authority_file, "r");
        if (fh) {
                auth = XauReadAuth (fh);
                fclose (fh);
        }
        if (auth) {
                len += auth->name_length + auth->data_length;
                retval = g_malloc0 (len);
        }
        if (retval) {
                retval->namelen = auth->name_length;
                retval->name = (char *) (retval + 1);
                memcpy (retval->name, auth->name, auth->name_length);
                retval->datalen = auth->data_length;
                retval->data = retval->name + auth->name_length + 1;
                memcpy (retval->data, auth->data, auth->data_length);
        }
        XauDisposeAuth (auth);
        return retval;
}
#endif

static gboolean
gdm_session_worker_initialize_pam (GdmSessionWorker   *worker,
                                   const char         *service,
                                   const char * const *extensions,
                                   const char         *username,
                                   const char         *hostname,
                                   gboolean            display_is_local,
                                   const char         *x11_display_name,
                                   const char         *x11_authority_file,
                                   const char         *display_device,
                                   const char         *seat_id,
                                   GError            **error)
{
        struct pam_conv        pam_conversation;
        int                    error_code;
        char tty_string[256];

        g_assert (service != NULL);
        g_assert (worker->pam_handle == NULL);

        g_debug ("GdmSessionWorker: initializing PAM; service=%s username=%s seat=%s",
                 service ? service : "(null)",
                 username ? username : "(null)",
                 seat_id ? seat_id : "(null)");

#ifdef SUPPORTS_PAM_EXTENSIONS
        if (extensions != NULL) {
                GDM_PAM_EXTENSION_ADVERTISE_SUPPORTED_EXTENSIONS (gdm_pam_extension_environment_block, extensions);
        }
#endif

        pam_conversation.conv = (GdmSessionWorkerPamNewMessagesFunc) gdm_session_worker_pam_new_messages_handler;
        pam_conversation.appdata_ptr = worker;

        gdm_session_worker_start_auditor (worker);
        error_code = pam_start (service,
                                username,
                                &pam_conversation,
                                &worker->pam_handle);
        if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: could not initialize PAM: (error code %d)", error_code);
                /* we don't use pam_strerror here because it requires a valid
                 * pam handle, and if pam_start fails pam_handle is undefined
                 */
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
                                     "");

                goto out;
        }

        /* set USER PROMPT */
        if (username == NULL) {
                error_code = pam_set_item (worker->pam_handle, PAM_USER_PROMPT, _("Username:"));

                if (error_code != PAM_SUCCESS) {
                        g_debug ("GdmSessionWorker: error informing authentication system of preferred username prompt: %s",
                                pam_strerror (worker->pam_handle, error_code));
                        g_set_error_literal (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                             "");
                        goto out;
                }
        }

        /* set RHOST */
        if (!display_is_local) {
                if (hostname != NULL && hostname[0] != '\0') {
                        error_code = pam_set_item (worker->pam_handle, PAM_RHOST, hostname);

                        g_debug ("error informing authentication system of user's hostname %s: %s",
                                 hostname,
                                 pam_strerror (worker->pam_handle, error_code));
                } else {
                        error_code = pam_set_item (worker->pam_handle, PAM_RHOST, "0.0.0.0");

                        g_debug ("error informing authentication system user is remote but has indeterminate hostname: %s",
                                 pam_strerror (worker->pam_handle, error_code));
                }

                if (error_code != PAM_SUCCESS) {
                        g_set_error_literal (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                             "");
                        goto out;
                }
        }

        /* set seat ID */
        if (seat_id != NULL && seat_id[0] != '\0') {
                gdm_session_worker_set_environment_variable (worker, "XDG_SEAT", seat_id);
        }

        if (strcmp (service, "gdm-launch-environment") == 0) {
                gdm_session_worker_set_environment_variable (worker, "XDG_SESSION_CLASS", "greeter");
        }

        g_debug ("GdmSessionWorker: state SETUP_COMPLETE");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_SETUP_COMPLETE);

        if (g_strcmp0 (seat_id, "seat0") == 0 && worker->seat0_has_vts) {
                /* Temporarily set PAM_TTY with the login VT,
                   PAM_TTY will be reset with the users VT right before the user session is opened */
                g_snprintf (tty_string, 256, "/dev/tty%d", GDM_INITIAL_VT);
                pam_set_item (worker->pam_handle, PAM_TTY, tty_string);
        }

        if (!display_is_local)
                worker->password_is_required = TRUE;

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        return TRUE;
}

static gboolean
gdm_session_worker_authenticate_user (GdmSessionWorker *worker,
                                      gboolean          password_is_required,
                                      GError          **error)
{
        int error_code;
        int authentication_flags;

        g_debug ("GdmSessionWorker: authenticating user %s", worker->username);

        authentication_flags = 0;

        if (password_is_required) {
                authentication_flags |= PAM_DISALLOW_NULL_AUTHTOK;
        }

        /* blocking call, does the actual conversation */
        error_code = pam_authenticate (worker->pam_handle, authentication_flags);

        if (error_code == PAM_AUTHINFO_UNAVAIL) {
                g_debug ("GdmSessionWorker: authentication service unavailable");

                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
                                     "");
                goto out;
#ifdef PAM_MODULE_UNKNOWN
        } else if (error_code == PAM_MODULE_UNKNOWN) {
                g_debug ("GdmSessionWorker: authentication module unavailable");

                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
                                     "");
                goto out;
#endif
        } else if (error_code == PAM_MAXTRIES) {
                g_debug ("GdmSessionWorker: authentication service had too many retries");
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_TOO_MANY_RETRIES,
                                     get_friendly_error_message (worker, error_code));
                goto out;
        } else if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: authentication returned %d: %s", error_code, pam_strerror (worker->pam_handle, error_code));

                /*
                 * Do not display a different message for user unknown versus
                 * a failed password for a valid user.
                 */
                if (error_code == PAM_USER_UNKNOWN) {
                        error_code = PAM_AUTH_ERR;
                }

                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     get_friendly_error_message (worker, error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state AUTHENTICATED");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_AUTHENTICATED);

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        return TRUE;
}

static gboolean
gdm_session_worker_authorize_user (GdmSessionWorker *worker,
                                   gboolean          password_is_required,
                                   GError          **error)
{
        int error_code;
        int authentication_flags;

        g_debug ("GdmSessionWorker: determining if authenticated user (password required:%d) is authorized to session",
                 password_is_required);

        authentication_flags = 0;

        if (password_is_required) {
                authentication_flags |= PAM_DISALLOW_NULL_AUTHTOK;
        }

        /* check that the account isn't disabled or expired
         */
        error_code = pam_acct_mgmt (worker->pam_handle, authentication_flags);

        /* it's possible that the user needs to change their password or pin code
         */
        if (error_code == PAM_NEW_AUTHTOK_REQD && !worker->is_program_session) {
                g_debug ("GdmSessionWorker: authenticated user requires new auth token");
                error_code = pam_chauthtok (worker->pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);

                gdm_session_worker_get_username (worker, NULL);

                if (error_code != PAM_SUCCESS) {
                        gdm_session_auditor_report_password_change_failure (worker->auditor);
                } else {
                        gdm_session_auditor_report_password_changed (worker->auditor);
                }
        }

        /* If the user is reauthenticating, then authorization isn't required to
         * proceed, the user is already logged in after all.
         */
        if (worker->is_reauth_session) {
                error_code = PAM_SUCCESS;
        }

        if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: user is not authorized to log in: %s",
                         pam_strerror (worker->pam_handle, error_code));
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHORIZING,
                                     get_friendly_error_message (worker, error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state AUTHORIZED");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_AUTHORIZED);

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        return TRUE;
}

static void
gdm_session_worker_set_environment_variable (GdmSessionWorker *worker,
                                             const char       *key,
                                             const char       *value)
{
        g_autofree char *environment_entry = NULL;
        int error_code;

        if (value != NULL) {
                environment_entry = g_strdup_printf ("%s=%s", key, value);
        } else {
                /* empty value means "remove from environment" */
                environment_entry = g_strdup (key);
        }

        error_code = pam_putenv (worker->pam_handle,
                                 environment_entry);

        if (error_code != PAM_SUCCESS) {
                g_warning ("cannot put %s in pam environment: %s\n",
                           environment_entry,
                           pam_strerror (worker->pam_handle, error_code));
        }
        g_debug ("GdmSessionWorker: Set PAM environment variable: '%s'", environment_entry);
}

static char *
gdm_session_worker_get_environment_variable (GdmSessionWorker *worker,
                                             const char       *key)
{
        return g_strdup (pam_getenv (worker->pam_handle, key));
}

static void
gdm_session_worker_update_environment_from_passwd_info (GdmSessionWorker *worker,
                                                        uid_t             uid,
                                                        gid_t             gid,
                                                        const char       *home,
                                                        const char       *shell)
{
        gdm_session_worker_set_environment_variable (worker, "LOGNAME", worker->username);
        gdm_session_worker_set_environment_variable (worker, "USER", worker->username);
        gdm_session_worker_set_environment_variable (worker, "USERNAME", worker->username);
        gdm_session_worker_set_environment_variable (worker, "HOME", home);
        gdm_session_worker_set_environment_variable (worker, "PWD", home);
        gdm_session_worker_set_environment_variable (worker, "SHELL", shell);
}

static gboolean
gdm_session_worker_environment_variable_is_set (GdmSessionWorker *worker,
                                                const char       *key)
{
        return pam_getenv (worker->pam_handle, key) != NULL;
}

static gboolean
_change_user (GdmSessionWorker  *worker,
              uid_t              uid,
              gid_t              gid)
{
#ifdef THE_MAN_PAGE_ISNT_LYING
        /* pam_setcred wants to be called as the authenticated user
         * but pam_open_session needs to be called as super-user.
         *
         * Set the real uid and gid to the user and give the user a
         * temporary super-user effective id.
         */
        if (setreuid (uid, GDM_SESSION_ROOT_UID) < 0) {
                return FALSE;
        }
#endif
        worker->uid = uid;
        worker->gid = gid;

        if (setgid (gid) < 0) {
                return FALSE;
        }

        if (initgroups (worker->username, gid) < 0) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
_lookup_passwd_info (const char *username,
                     uid_t      *uidp,
                     gid_t      *gidp,
                     char      **homep,
                     char      **shellp)
{
        gboolean       ret;
        struct passwd *passwd_entry;
        struct passwd  passwd_buffer;
        char          *aux_buffer;
        long           required_aux_buffer_size;
        gsize          aux_buffer_size;

        ret = FALSE;
        aux_buffer = NULL;

        required_aux_buffer_size = sysconf (_SC_GETPW_R_SIZE_MAX);

        if (required_aux_buffer_size < 0) {
                aux_buffer_size = GDM_PASSWD_AUXILLARY_BUFFER_SIZE;
        } else {
                aux_buffer_size = (gsize) required_aux_buffer_size;
        }

        aux_buffer = g_slice_alloc0 (aux_buffer_size);

        /* we use the _r variant of getpwnam()
         * (with its weird semantics) so that the
         * passwd_entry doesn't potentially get stomped on
         * by a PAM module
         */
 again:
        passwd_entry = NULL;
#ifdef HAVE_POSIX_GETPWNAM_R
        errno = getpwnam_r (username,
                            &passwd_buffer,
                            aux_buffer,
                            (size_t) aux_buffer_size,
                            &passwd_entry);
#else
        passwd_entry = getpwnam_r (username,
                                   &passwd_buffer,
                                   aux_buffer,
                                   (size_t) aux_buffer_size);
        errno = 0;
#endif /* !HAVE_POSIX_GETPWNAM_R */
        if (errno == EINTR) {
                g_debug ("%s", g_strerror (errno));
                goto again;
        } else if (errno != 0) {
                g_warning ("%s", g_strerror (errno));
                goto out;
        }

        if (passwd_entry == NULL) {
                goto out;
        }

        if (uidp != NULL) {
                *uidp = passwd_entry->pw_uid;
        }
        if (gidp != NULL) {
                *gidp = passwd_entry->pw_gid;
        }
        if (homep != NULL) {
                if (passwd_entry->pw_dir != NULL && passwd_entry->pw_dir[0] != '\0') {
                        *homep = g_strdup (passwd_entry->pw_dir);
                } else {
                        *homep = g_strdup ("/");
                }
        }
        if (shellp != NULL) {
                if (passwd_entry->pw_shell != NULL && passwd_entry->pw_shell[0] != '\0') {
                        *shellp = g_strdup (passwd_entry->pw_shell);
                } else {
                        *shellp = g_strdup ("/bin/bash");
                }
        }
        ret = TRUE;
 out:
        if (aux_buffer != NULL) {
                g_assert (aux_buffer_size > 0);
                g_slice_free1 (aux_buffer_size, aux_buffer);
        }

        return ret;
}

static char *
get_var_cb (const char *key,
            gpointer user_data)
{
        return gdm_session_worker_get_environment_variable (user_data, key);
}

static void
load_env_func (const char *var,
               const char *value,
               gpointer user_data)
{
        GdmSessionWorker *worker = user_data;
        gdm_session_worker_set_environment_variable (worker, var, value);
}

static gboolean
gdm_session_worker_accredit_user (GdmSessionWorker  *worker,
                                  GError           **error)
{
        g_autofree char *shell = NULL;
        g_autofree char *home = NULL;
        gboolean ret;
        gboolean res;
        uid_t    uid;
        gid_t    gid;
        int      error_code;

        ret = FALSE;

        if (worker->username == NULL) {
                g_debug ("GdmSessionWorker: Username not set");
                error_code = PAM_USER_UNKNOWN;
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                             _("no user account available"));
                goto out;
        }

        uid = 0;
        gid = 0;
        res = _lookup_passwd_info (worker->username,
                                   &uid,
                                   &gid,
                                   &home,
                                   &shell);
        if (! res) {
                g_debug ("GdmSessionWorker: Unable to lookup account info");
                error_code = PAM_AUTHINFO_UNAVAIL;
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                             _("no user account available"));
                goto out;
        }

        gdm_session_worker_update_environment_from_passwd_info (worker,
                                                                uid,
                                                                gid,
                                                                home,
                                                                shell);

        /* Let's give the user a default PATH if he doesn't already have one
         */
        if (!gdm_session_worker_environment_variable_is_set (worker, "PATH")) {
                if (strcmp (BINDIR, "/usr/bin") == 0) {
                        gdm_session_worker_set_environment_variable (worker, "PATH",
                                                                     GDM_SESSION_DEFAULT_PATH);
                } else {
                        gdm_session_worker_set_environment_variable (worker, "PATH",
                                                                     BINDIR ":" GDM_SESSION_DEFAULT_PATH);
                }
        }

        if (! _change_user (worker, uid, gid)) {
                g_debug ("GdmSessionWorker: Unable to change to user");
                error_code = PAM_SYSTEM_ERR;
                g_set_error_literal (error, GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                                     _("Unable to change to user"));
                goto out;
        }

        error_code = pam_setcred (worker->pam_handle, worker->cred_flags);

        /* If the user is reauthenticating and they've made it this far, then there
         * is no reason we should lock them out of their session.  They've already
         * proved they are they same person who logged in, and that's all we care
         * about.
         */
        if (worker->is_reauth_session) {
                error_code = PAM_SUCCESS;
        }

        if (error_code != PAM_SUCCESS) {
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                                     pam_strerror (worker->pam_handle, error_code));
                goto out;
        }

        ret = TRUE;

 out:
        if (ret) {
                g_debug ("GdmSessionWorker: state ACCREDITED");
                ret = TRUE;

                gdm_session_worker_get_username (worker, NULL);
                gdm_session_auditor_report_user_accredited (worker->auditor);
                gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_ACCREDITED);
        } else {
                gdm_session_worker_uninitialize_pam (worker, error_code);
        }

        return ret;
}

static const char * const *
gdm_session_worker_get_environment (GdmSessionWorker *worker)
{
        return (const char * const *) pam_getenvlist (worker->pam_handle);
}

static gboolean
run_script (GdmSessionWorker *worker,
            const char       *dir)
{
        /* scripts are for non-program sessions only */
        if (worker->is_program_session) {
                return TRUE;
        }

        return gdm_run_script (dir,
                               worker->username,
                               worker->x11_display_name,
                               worker->display_is_local? NULL : worker->hostname,
                               worker->x11_authority_file);
}

static void
wait_until_dbus_signal_emission_to_manager_finishes (GdmSessionWorker *worker)
{
        g_autoptr (GdmDBusPeer) peer_proxy = NULL;
        g_autoptr (GError) error = NULL;
        gboolean pinged;

        peer_proxy = gdm_dbus_peer_proxy_new_sync (worker->connection,
                                                   G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                   NULL,
                                                   "/org/freedesktop/DBus",
                                                   NULL,
                                                   &error);

        if (peer_proxy == NULL) {
                g_debug ("GdmSessionWorker: could not create peer proxy to daemon: %s",
                         error->message);
                return;
        }

        pinged = gdm_dbus_peer_call_ping_sync (peer_proxy, NULL, &error);

        if (!pinged) {
                g_debug ("GdmSessionWorker: could not ping daemon: %s",
                         error->message);
                return;
        }
}

static void
jump_back_to_initial_vt (GdmSessionWorker *worker)
{
        if (worker->session_vt == 0)
                return;

        if (worker->session_vt == GDM_INITIAL_VT)
                return;

        if (g_strcmp0 (worker->display_seat_id, "seat0") != 0 || !worker->seat0_has_vts)
                return;

#ifdef ENABLE_USER_DISPLAY_SERVER
        jump_to_vt (worker, GDM_INITIAL_VT);
        worker->session_vt = 0;
#endif
}

static void
session_worker_child_watch (GPid              pid,
                            int               status,
                            GdmSessionWorker *worker)
{
        g_debug ("GdmSessionWorker: child (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        gdm_session_worker_uninitialize_pam (worker, PAM_SUCCESS);

        worker->child_pid = -1;
        worker->child_watch_id = 0;
        run_script (worker, GDMCONFDIR "/PostSession");

        gdm_dbus_worker_emit_session_exited (GDM_DBUS_WORKER (worker),
                                             worker->service,
                                             status);

        killpg (pid, SIGHUP);

        /* FIXME: It's important to give the manager an opportunity to process the
         * session-exited emission above before switching VTs.
         *
         * This is because switching VTs makes the manager try to put a login screen
         * up on VT 1, but it may actually want to try to auto login again in response
         * to session-exited.
         *
         * This function just does a manager roundtrip over the bus to make sure the
         * signal has been dispatched before jumping.
         *
         * Ultimately, we may want to improve the manager<->worker interface.
         *
         * See:
         *
         * https://gitlab.gnome.org/GNOME/gdm/-/merge_requests/123
         *
         * for some ideas and more discussion.
         *
         */
        wait_until_dbus_signal_emission_to_manager_finishes (worker);

        jump_back_to_initial_vt (worker);
}

static void
gdm_session_worker_watch_child (GdmSessionWorker *worker)
{
        g_debug ("GdmSession worker: watching pid %d", worker->child_pid);
        worker->child_watch_id = g_child_watch_add (worker->child_pid,
                                                    (GChildWatchFunc)session_worker_child_watch,
                                                    worker);

}

static gboolean
_is_loggable_file (const char* filename)
{
        struct stat file_info;

        if (g_lstat (filename, &file_info) < 0) {
                return FALSE;
        }

        return S_ISREG (file_info.st_mode) && g_access (filename, R_OK | W_OK) == 0;
}

static void
rotate_logs (const char *path,
             guint       n_copies)
{
        int i;

        for (i = n_copies - 1; i > 0; i--) {
                g_autofree char *name_n = NULL;
                g_autofree char *name_n1 = NULL;

                name_n = g_strdup_printf ("%s.%d", path, i);
                if (i > 1) {
                        name_n1 = g_strdup_printf ("%s.%d", path, i - 1);
                } else {
                        name_n1 = g_strdup (path);
                }

                g_unlink (name_n);
                g_rename (name_n1, name_n);
        }

        g_unlink (path);
}

static int
_open_program_session_log (const char *filename)
{
        int   fd;

        rotate_logs (filename, MAX_LOGS);

        fd = g_open (filename, O_WRONLY | O_APPEND | O_CREAT, 0600);

        if (fd < 0) {
                g_autofree char *temp_name = NULL;

                temp_name = g_strdup_printf ("%s.XXXXXXXX", filename);

                fd = g_mkstemp (temp_name);

                if (fd < 0) {
                        goto out;
                }

                g_warning ("session log '%s' is not appendable, logging session to '%s' instead.\n", filename,
                           temp_name);
        } else {
                if (ftruncate (fd, 0) < 0) {
                        close (fd);
                        fd = -1;
                        goto out;
                }
        }

        if (fchmod (fd, 0644) < 0) {
                close (fd);
                fd = -1;
                goto out;
        }


out:
        if (fd < 0) {
                g_warning ("unable to log program session");
                fd = g_open ("/dev/null", O_RDWR);
        }

        return fd;
}

static int
_open_user_session_log (const char *dir)
{
        int   fd;
        g_autofree char *filename = NULL;

        filename = g_build_filename (dir, GDM_SESSION_LOG_FILENAME, NULL);

        if (g_access (dir, R_OK | W_OK | X_OK) == 0 && _is_loggable_file (filename)) {
                g_autofree char *filename_old = NULL;

                filename_old = g_strdup_printf ("%s.old", filename);
                g_rename (filename, filename_old);
        }

        fd = g_open (filename, O_RDWR | O_APPEND | O_CREAT, 0600);

        if (fd < 0) {
                g_autofree char *temp_name = NULL;

                temp_name = g_strdup_printf ("%s.XXXXXXXX", filename);

                fd = g_mkstemp (temp_name);

                if (fd < 0) {
                        goto out;
                }

                g_warning ("session log '%s' is not appendable, logging session to '%s' instead.\n", filename,
                           temp_name);
        } else {
                if (ftruncate (fd, 0) < 0) {
                        close (fd);
                        fd = -1;
                        goto out;
                }
        }

        if (fchmod (fd, 0600) < 0) {
                close (fd);
                fd = -1;
                goto out;
        }

out:
        if (fd < 0) {
                g_warning ("unable to log session");
                fd = g_open ("/dev/null", O_RDWR);
        }

        return fd;
}

static gboolean
gdm_session_worker_start_session (GdmSessionWorker  *worker,
                                  GError           **error)
{
        struct passwd *passwd_entry;
        pid_t session_pid;
        int   error_code;

        gdm_get_pwent_for_name (worker->username, &passwd_entry);
        if (worker->is_program_session) {
                g_debug ("GdmSessionWorker: opening session for program '%s'",
                         worker->arguments[0]);
        } else {
                g_debug ("GdmSessionWorker: opening user session with program '%s'",
                         worker->arguments[0]);
        }

        error_code = PAM_SUCCESS;

        /* If we're in new vt mode, jump to the new vt now. There's no need to jump for
         * the other two modes: in the logind case, the session will activate itself when
         * ready, and in the reuse server case, we're already on the correct VT. */
        if (g_strcmp0 (worker->display_seat_id, "seat0") == 0 && worker->seat0_has_vts) {
                if (worker->display_mode == GDM_SESSION_DISPLAY_MODE_NEW_VT) {
                        jump_to_vt (worker, worker->session_vt);
                }
        }

        if (!worker->is_program_session && !run_script (worker, GDMCONFDIR "/PostLogin")) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                             "Failed to execute PostLogin script");
                error_code = PAM_ABORT;
                goto out;
        }

        if (!worker->is_program_session && !run_script (worker, GDMCONFDIR "/PreSession")) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                             "Failed to execute PreSession script");
                error_code = PAM_ABORT;
                goto out;
        }

        session_pid = fork ();

        if (session_pid < 0) {
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                                     g_strerror (errno));
                error_code = PAM_ABORT;
                goto out;
        }

        if (session_pid == 0) {
                g_autofree char  *home_dir = NULL;
                const char * const * environment;
                int    stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
                gboolean has_journald = FALSE, needs_controlling_terminal = FALSE;
                /* Leak the TTY into the session as stdin so that it stays open
                 * without any races. */
                if (worker->session_tty_fd > 0) {
                        dup2 (worker->session_tty_fd, STDIN_FILENO);
                        close (worker->session_tty_fd);
                        worker->session_tty_fd = -1;
                        needs_controlling_terminal = TRUE;
                } else {
                        stdin_fd = open ("/dev/null", O_RDWR);
                        dup2 (stdin_fd, STDIN_FILENO);
                        close (stdin_fd);
                }

#ifdef ENABLE_SYSTEMD_JOURNAL
                has_journald = sd_booted() > 0;
#endif
                if (!has_journald && worker->is_program_session) {
                        stdout_fd = _open_program_session_log (worker->log_file);
                        stderr_fd = dup (stdout_fd);
                }

                if (setsid () < 0) {
                        g_debug ("GdmSessionWorker: could not set pid '%u' as leader of new session and process group: %s",
                                 (guint) getpid (), g_strerror (errno));
                        _exit (EXIT_FAILURE);
                }

                /* Take control of the tty
                 */
                if (needs_controlling_terminal) {
                        if (ioctl (STDIN_FILENO, TIOCSCTTY, 0) < 0) {
                                g_debug ("GdmSessionWorker: could not take control of tty: %m");
                        }
                }

#ifdef HAVE_LOGINCAP
                if (setusercontext (NULL, passwd_entry, passwd_entry->pw_uid, LOGIN_SETALL) < 0) {
                        g_debug ("GdmSessionWorker: setusercontext() failed for user %s: %s",
                                 passwd_entry->pw_name, g_strerror (errno));
                        _exit (EXIT_FAILURE);
                }
#else
                if (setuid (worker->uid) < 0) {
                        g_debug ("GdmSessionWorker: could not reset uid: %s", g_strerror (errno));
                        _exit (EXIT_FAILURE);
                }
#endif

                if (!worker->is_program_session) {
                        gdm_load_env_d (load_env_func, get_var_cb, worker);
                }

                environment = gdm_session_worker_get_environment (worker);

                g_assert (geteuid () == getuid ());

                home_dir = gdm_session_worker_get_environment_variable (worker, "HOME");
                if ((home_dir == NULL) || g_chdir (home_dir) < 0) {
                        g_chdir ("/");
                }

#ifdef ENABLE_SYSTEMD_JOURNAL
                if (has_journald) {
                        stdout_fd = sd_journal_stream_fd (worker->arguments[0], LOG_INFO, FALSE);
                        stderr_fd = sd_journal_stream_fd (worker->arguments[0], LOG_WARNING, FALSE);

                        /* Unset the CLOEXEC flags, because sd_journal_stream_fd
                         * gives it to us by default.
                         */
                        gdm_clear_close_on_exec_flag (stdout_fd);
                        gdm_clear_close_on_exec_flag (stderr_fd);
                }
#endif
                if (!has_journald && !worker->is_program_session) {
                        if (home_dir != NULL && home_dir[0] != '\0') {
                                g_autofree char *cache_dir = NULL;
                                g_autofree char *log_dir = NULL;

                                cache_dir = gdm_session_worker_get_environment_variable (worker, "XDG_CACHE_HOME");
                                if (cache_dir == NULL || cache_dir[0] == '\0') {
                                        cache_dir = g_build_filename (home_dir, ".cache", NULL);
                                }

                                log_dir = g_build_filename (cache_dir, "gdm", NULL);

                                if (g_mkdir_with_parents (log_dir, S_IRWXU) == 0) {
                                        stdout_fd = _open_user_session_log (log_dir);
                                        stderr_fd = dup (stdout_fd);
                                } else {
                                        stdout_fd = open ("/dev/null", O_RDWR);
                                        stderr_fd = dup (stdout_fd);
                                }
                        } else {
                                stdout_fd = open ("/dev/null", O_RDWR);
                                stderr_fd = dup (stdout_fd);
                        }
                }

                if (stdout_fd != -1) {
                        dup2 (stdout_fd, STDOUT_FILENO);
                        close (stdout_fd);
                }

                if (stderr_fd != -1) {
                        dup2 (stderr_fd, STDERR_FILENO);
                        close (stderr_fd);
                }

                gdm_log_shutdown ();

                /*
                 * Reset SIGPIPE to default so that any process in the user
                 * session get the default SIGPIPE behavior instead of ignoring
                 * SIGPIPE.
                 */
                signal (SIGPIPE, SIG_DFL);

                gdm_session_execute (worker->arguments[0],
                                     worker->arguments,
                                     (char **)
                                     environment,
                                     TRUE);

                gdm_log_init ();
                g_debug ("GdmSessionWorker: child '%s' could not be started: %s",
                         worker->arguments[0],
                         g_strerror (errno));

                _exit (EXIT_FAILURE);
        }

        if (worker->session_tty_fd > 0) {
                close (worker->session_tty_fd);
                worker->session_tty_fd = -1;
        }

        /* If we end up execing again, make sure we don't use the executable context set up
         * by pam_selinux durin pam_open_session
         */
#ifdef HAVE_SELINUX
        setexeccon (NULL);
#endif

        worker->child_pid = session_pid;

        g_debug ("GdmSessionWorker: session opened creating reply...");
        g_assert (sizeof (GPid) <= sizeof (int));

        g_debug ("GdmSessionWorker: state SESSION_STARTED");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_SESSION_STARTED);

        gdm_session_worker_watch_child (worker);

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        return TRUE;
}

static gboolean
set_up_for_new_vt (GdmSessionWorker *worker)
{
        int initial_vt_fd;
        char vt_string[256], tty_string[256];
        int session_vt = 0;

        /* open the initial vt.  We need it for two scenarios:
         *
         * 1) display_is_initial is TRUE.  We need it directly.
         * 2) display_is_initial is FALSE. We need it to mark
         * the initial VT as "in use" so it doesn't get returned
         * by VT_OPENQRY
         * */
        g_snprintf (tty_string, sizeof (tty_string), "/dev/tty%d", GDM_INITIAL_VT);
        initial_vt_fd = open (tty_string, O_RDWR | O_NOCTTY);

        if (initial_vt_fd < 0) {
                g_debug ("GdmSessionWorker: couldn't open console of initial fd: %m");
                return FALSE;
        }

        if (worker->display_is_initial) {
                session_vt = GDM_INITIAL_VT;
        } else {

                /* Typically VT_OPENQRY is called on /dev/tty0, but we already
                 * have /dev/tty1 open above, so might as well use it.
                 */
                if (ioctl (initial_vt_fd, VT_OPENQRY, &session_vt) < 0) {
                        g_debug ("GdmSessionWorker: couldn't open new VT: %m");
                        goto fail;
                }
        }

        worker->session_vt = session_vt;

        g_assert (session_vt > 0);

        g_snprintf (vt_string, sizeof (vt_string), "%d", session_vt);

        /* Set the VTNR. This is used by logind to configure a session in
         * the logind-managed case, but it doesn't hurt to set it always.
         * When logind gains support for XDG_VTNR=auto, we can make the
         * OPENQRY and this whole path only used by the new VT code. */
        gdm_session_worker_set_environment_variable (worker,
                                                     "XDG_VTNR",
                                                     vt_string);

        if (worker->display_is_initial) {
             worker->session_tty_fd = initial_vt_fd;
        } else {
             g_snprintf (tty_string, sizeof (tty_string), "/dev/tty%d", session_vt);
             worker->session_tty_fd = open (tty_string, O_RDWR | O_NOCTTY);
             close (initial_vt_fd);
        }

        pam_set_item (worker->pam_handle, PAM_TTY, tty_string);

        return TRUE;

fail:
        close (initial_vt_fd);
        return FALSE;
}

static gboolean
set_xdg_vtnr_to_current_vt (GdmSessionWorker *worker)
{
        int fd;
        char vt_string[256];
        struct vt_stat vt_state = { 0 };

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

        if (fd < 0) {
                g_debug ("GdmSessionWorker: couldn't open VT master: %m");
                return FALSE;
        }

        if (ioctl (fd, VT_GETSTATE, &vt_state) < 0) {
                g_debug ("GdmSessionWorker: couldn't get current VT: %m");
                goto fail;
        }

        close (fd);

        g_snprintf (vt_string, sizeof (vt_string), "%d", vt_state.v_active);

        gdm_session_worker_set_environment_variable (worker,
                                                     "XDG_VTNR",
                                                     vt_string);

        return TRUE;

fail:
        close (fd);
        return FALSE;
}

static gboolean
set_up_for_current_vt (GdmSessionWorker  *worker,
                       GError           **error)
{
#ifdef PAM_XAUTHDATA
        struct pam_xauth_data *pam_xauth;
#endif
        g_autofree char *pam_tty = NULL;

        /* set TTY */
        pam_tty = _get_tty_for_pam (worker->x11_display_name, worker->display_device);
        if (pam_tty != NULL && pam_tty[0] != '\0') {
                int error_code;

                error_code = pam_set_item (worker->pam_handle, PAM_TTY, pam_tty);
                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of user's console %s: %s",
                                 pam_tty,
                                 pam_strerror (worker->pam_handle, error_code));
                        g_set_error_literal (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                             "");
                        return FALSE;
                }
        }

#ifdef PAM_XDISPLAY
        /* set XDISPLAY */
        if (worker->x11_display_name != NULL && worker->x11_display_name[0] != '\0') {
                int error_code;

                error_code = pam_set_item (worker->pam_handle, PAM_XDISPLAY, worker->x11_display_name);
                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of display string %s: %s",
                                 worker->x11_display_name,
                                 pam_strerror (worker->pam_handle, error_code));
                        g_set_error_literal (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                             "");
                        return FALSE;
                }
        }
#endif
#if defined(PAM_XAUTHDATA) && defined(ENABLE_X11_SUPPORT)
        /* set XAUTHDATA */
        pam_xauth = _get_xauth_for_pam (worker->x11_authority_file);
        if (pam_xauth != NULL) {
                int error_code;

                error_code = pam_set_item (worker->pam_handle, PAM_XAUTHDATA, pam_xauth);
                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of display string %s: %s",
                                 worker->x11_display_name,
                                 pam_strerror (worker->pam_handle, error_code));
                        g_free (pam_xauth);

                        g_set_error_literal (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                             "");
                        return FALSE;
                }
                g_free (pam_xauth);
         }
#endif

        if (g_strcmp0 (worker->display_seat_id, "seat0") == 0 && worker->seat0_has_vts) {
                g_debug ("GdmSessionWorker: setting XDG_VTNR to current vt");
                set_xdg_vtnr_to_current_vt (worker);
        } else {
                g_debug ("GdmSessionWorker: not setting XDG_VTNR since no VTs on seat");
        }

        return TRUE;
}

static gboolean
gdm_session_worker_open_session (GdmSessionWorker  *worker,
                                 GError           **error)
{
        g_autofree char *session_id = NULL;
        int error_code;
        int flags;

        g_assert (worker->state == GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED);
        g_assert (geteuid () == 0);

        if (g_strcmp0 (worker->display_seat_id, "seat0") == 0 && worker->seat0_has_vts) {
                switch (worker->display_mode) {
                case GDM_SESSION_DISPLAY_MODE_REUSE_VT:
                        if (!set_up_for_current_vt (worker, error)) {
                                return FALSE;
                        }
                        break;
                case GDM_SESSION_DISPLAY_MODE_NEW_VT:
                case GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED:
                        if (!set_up_for_new_vt (worker)) {
                                g_set_error (error,
                                             GDM_SESSION_WORKER_ERROR,
                                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                                             "Unable to open VT");
                                return FALSE;
                        }
                        break;
                }
        }

        flags = 0;

        if (worker->is_program_session) {
                flags |= PAM_SILENT;
        }

        error_code = pam_open_session (worker->pam_handle, flags);

        if (error_code != PAM_SUCCESS) {
                g_set_error_literal (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                                     pam_strerror (worker->pam_handle, error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state SESSION_OPENED");
        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_SESSION_OPENED);

        session_id = gdm_session_worker_get_environment_variable (worker, "XDG_SESSION_ID");

        if (session_id != NULL) {
                g_free (worker->session_id);
                worker->session_id = g_steal_pointer (&session_id);
        }

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                worker->session_vt = 0;
                return FALSE;
        }

        gdm_session_worker_get_username (worker, NULL);
        gdm_session_auditor_report_login (worker->auditor);

        return TRUE;
}

static void
gdm_session_worker_set_server_address (GdmSessionWorker *worker,
                                       const char       *address)
{
        g_free (worker->server_address);
        worker->server_address = g_strdup (address);
}

static void
gdm_session_worker_set_is_reauth_session (GdmSessionWorker *worker,
                                          gboolean          is_reauth_session)
{
        worker->is_reauth_session = is_reauth_session;
}

static void
gdm_session_worker_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GdmSessionWorker *self;

        self = GDM_SESSION_WORKER (object);

        switch (prop_id) {
        case PROP_SERVER_ADDRESS:
                gdm_session_worker_set_server_address (self, g_value_get_string (value));
                break;
        case PROP_IS_REAUTH_SESSION:
                gdm_session_worker_set_is_reauth_session (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_worker_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GdmSessionWorker *self;

        self = GDM_SESSION_WORKER (object);

        switch (prop_id) {
        case PROP_SERVER_ADDRESS:
                g_value_set_string (value, self->server_address);
                break;
        case PROP_IS_REAUTH_SESSION:
                g_value_set_boolean (value, self->is_reauth_session);
                break;
        case PROP_STATE:
                g_value_set_enum (value, self->state);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
gdm_session_worker_handle_set_environment_variable (GdmDBusWorker         *object,
                                                    GDBusMethodInvocation *invocation,
                                                    const char            *key,
                                                    const char            *value)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        gdm_session_worker_set_environment_variable (worker, key, value);
        gdm_dbus_worker_complete_set_environment_variable (object, invocation);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_set_session_name (GdmDBusWorker         *object,
                                            GDBusMethodInvocation *invocation,
                                            const char            *session_name)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_debug ("GdmSessionWorker: session name set to %s", session_name);
        if (worker->user_settings != NULL)
                gdm_session_settings_set_session_name (worker->user_settings,
                                                       session_name);
        gdm_dbus_worker_complete_set_session_name (object, invocation);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_set_session_display_mode (GdmDBusWorker         *object,
                                                    GDBusMethodInvocation *invocation,
                                                    const char            *str)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_debug ("GdmSessionWorker: session display mode set to %s", str);
        worker->display_mode = gdm_session_display_mode_from_string (str);
        gdm_dbus_worker_complete_set_session_display_mode (object, invocation);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_set_language_name (GdmDBusWorker         *object,
                                             GDBusMethodInvocation *invocation,
                                             const char            *language_name)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_debug ("GdmSessionWorker: language name set to %s", language_name);
        if (worker->user_settings != NULL)
                gdm_session_settings_set_language_name (worker->user_settings,
                                                        language_name);
        gdm_dbus_worker_complete_set_language_name (object, invocation);
        return TRUE;
}

static void
on_saved_language_name_read (GdmSessionWorker *worker)
{
        g_autofree char *language_name = NULL;

        language_name = gdm_session_settings_get_language_name (worker->user_settings);

        g_debug ("GdmSessionWorker: Saved language is %s", language_name);
        gdm_dbus_worker_emit_saved_language_name_read (GDM_DBUS_WORKER (worker),
                                                       language_name);
}

static void
on_saved_session_name_read (GdmSessionWorker *worker)
{
        g_autofree char *session_name = NULL;

        session_name = gdm_session_settings_get_session_name (worker->user_settings);

        g_debug ("GdmSessionWorker: Saved session is %s", session_name);
        gdm_dbus_worker_emit_saved_session_name_read (GDM_DBUS_WORKER (worker),
                                                      session_name);
}

static void
on_saved_session_type_read (GdmSessionWorker *worker)
{
        g_autofree char *session_type = NULL;

        session_type = gdm_session_settings_get_session_type (worker->user_settings);

        g_debug ("GdmSessionWorker: Saved session type is %s", session_type);
        gdm_dbus_worker_emit_saved_session_type_read (GDM_DBUS_WORKER (worker),
                                                      session_type);
}


static void
do_setup (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        res = gdm_session_worker_initialize_pam (worker,
                                                 worker->service,
                                                 (const char **) worker->extensions,
                                                 worker->username,
                                                 worker->hostname,
                                                 worker->display_is_local,
                                                 worker->x11_display_name,
                                                 worker->x11_authority_file,
                                                 worker->display_device,
                                                 worker->display_seat_id,
                                                 &error);

        if (res) {
                g_dbus_method_invocation_return_value (worker->pending_invocation, NULL);
        } else {
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static void
do_authenticate (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        /* find out who the user is and ensure they are who they say they are
         */
        res = gdm_session_worker_authenticate_user (worker,
                                                    worker->password_is_required,
                                                    &error);
        if (res) {
                /* we're authenticated.  Let's make sure we've been given
                 * a valid username for the system
                 */
                if (!worker->is_program_session) {
                        g_debug ("GdmSessionWorker: trying to get updated username");
                        gdm_session_worker_update_username (worker);
                }

                gdm_dbus_worker_complete_authenticate (GDM_DBUS_WORKER (worker), worker->pending_invocation);
        } else {
                g_debug ("GdmSessionWorker: Unable to verify user");
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static void
do_authorize (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        /* make sure the user is allowed to log in to this system
         */
        res = gdm_session_worker_authorize_user (worker,
                                                 worker->password_is_required,
                                                 &error);
        if (res) {
                gdm_dbus_worker_complete_authorize (GDM_DBUS_WORKER (worker), worker->pending_invocation);
        } else {
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static void
do_accredit (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        /* get kerberos tickets, setup group lists, etc
         */
        res = gdm_session_worker_accredit_user (worker, &error);

        if (res) {
                gdm_dbus_worker_complete_establish_credentials (GDM_DBUS_WORKER (worker), worker->pending_invocation);
        } else {
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static void
save_account_details_now (GdmSessionWorker *worker)
{
        g_assert (worker->state == GDM_SESSION_WORKER_STATE_ACCREDITED);

        g_debug ("GdmSessionWorker: saving account details for user %s", worker->username);

        gdm_session_worker_set_state (worker, GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED);
        if (worker->user_settings != NULL) {
                if (!gdm_session_settings_save (worker->user_settings,
                                                worker->username)) {
                        g_warning ("could not save session and language settings");
                }
        }
        queue_state_change (worker);
}

static void
on_settings_is_loaded_changed (GdmSessionSettings *user_settings,
                               GParamSpec         *pspec,
                               GdmSessionWorker   *worker)
{
        if (!gdm_session_settings_is_loaded (worker->user_settings)) {
                return;
        }

        /* These signal handlers should be disconnected after the loading,
         * so that gdm_session_settings_set_* APIs don't cause the emitting
         * of Saved*NameRead D-Bus signals any more.
         */
        g_signal_handlers_disconnect_by_func (worker->user_settings,
                                              G_CALLBACK (on_saved_session_name_read),
                                              worker);

        g_signal_handlers_disconnect_by_func (worker->user_settings,
                                              G_CALLBACK (on_saved_language_name_read),
                                              worker);

        if (worker->state == GDM_SESSION_WORKER_STATE_NONE) {
                g_debug ("GdmSessionWorker: queuing setup for user: %s %s",
                         worker->username, worker->display_device);
                queue_state_change (worker);
        } else if (worker->state == GDM_SESSION_WORKER_STATE_ACCREDITED) {
                save_account_details_now (worker);
        } else {
                return;
        }

        g_signal_handlers_disconnect_by_func (G_OBJECT (worker->user_settings),
                                              G_CALLBACK (on_settings_is_loaded_changed),
                                              worker);
}

static void
do_save_account_details_when_ready (GdmSessionWorker *worker)
{
        g_assert (worker->state == GDM_SESSION_WORKER_STATE_ACCREDITED);

        if (worker->user_settings != NULL && !gdm_session_settings_is_loaded (worker->user_settings)) {
                g_signal_connect (G_OBJECT (worker->user_settings),
                                  "notify::is-loaded",
                                  G_CALLBACK (on_settings_is_loaded_changed),
                                  worker);
                g_debug ("GdmSessionWorker: user %s, not fully loaded yet, will save account details later",
                         worker->username);
                gdm_session_settings_load (worker->user_settings,
                                           worker->username);
                return;
        }

        save_account_details_now (worker);
}

static void
do_open_session (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        res = gdm_session_worker_open_session (worker, &error);

        if (res) {
                char *session_id = worker->session_id;
                if (session_id == NULL) {
                        session_id = "";
                }

                gdm_dbus_worker_complete_open (GDM_DBUS_WORKER (worker), worker->pending_invocation, session_id);
        } else {
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static void
do_start_session (GdmSessionWorker *worker)
{
        g_autoptr(GError) error = NULL;
        gboolean res;

        res = gdm_session_worker_start_session (worker, &error);
        if (res) {
                gdm_dbus_worker_complete_start_program (GDM_DBUS_WORKER (worker),
                                                        worker->pending_invocation,
                                                        worker->child_pid);
        } else {
                g_dbus_method_invocation_take_error (worker->pending_invocation,
                                                     g_steal_pointer (&error));
        }
        worker->pending_invocation = NULL;
}

static const char *
get_state_name (int state)
{
        const char *name;

        name = NULL;

        switch (state) {
        case GDM_SESSION_WORKER_STATE_NONE:
                name = "NONE";
                break;
        case GDM_SESSION_WORKER_STATE_SETUP_COMPLETE:
                name = "SETUP_COMPLETE";
                break;
        case GDM_SESSION_WORKER_STATE_AUTHENTICATED:
                name = "AUTHENTICATED";
                break;
        case GDM_SESSION_WORKER_STATE_AUTHORIZED:
                name = "AUTHORIZED";
                break;
        case GDM_SESSION_WORKER_STATE_ACCREDITED:
                name = "ACCREDITED";
                break;
        case GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED:
                name = "ACCOUNT_DETAILS_SAVED";
                break;
        case GDM_SESSION_WORKER_STATE_SESSION_OPENED:
                name = "SESSION_OPENED";
                break;
        case GDM_SESSION_WORKER_STATE_SESSION_STARTED:
                name = "SESSION_STARTED";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return name;
}

static gboolean
state_change_idle (GdmSessionWorker *worker)
{
        int new_state;

        new_state = worker->state + 1;
        g_debug ("GdmSessionWorker: attempting to change state to %s",
                 get_state_name (new_state));

        worker->state_change_idle_id = 0;

        switch (new_state) {
        case GDM_SESSION_WORKER_STATE_SETUP_COMPLETE:
                do_setup (worker);
                break;
        case GDM_SESSION_WORKER_STATE_AUTHENTICATED:
                do_authenticate (worker);
                break;
        case GDM_SESSION_WORKER_STATE_AUTHORIZED:
                do_authorize (worker);
                break;
        case GDM_SESSION_WORKER_STATE_ACCREDITED:
                do_accredit (worker);
                break;
        case GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED:
                do_save_account_details_when_ready (worker);
                break;
        case GDM_SESSION_WORKER_STATE_SESSION_OPENED:
                do_open_session (worker);
                break;
        case GDM_SESSION_WORKER_STATE_SESSION_STARTED:
                do_start_session (worker);
                break;
        case GDM_SESSION_WORKER_STATE_NONE:
        default:
                g_assert_not_reached ();
        }
        return FALSE;
}

static void
queue_state_change (GdmSessionWorker *worker)
{
        if (worker->state_change_idle_id > 0) {
                return;
        }

        worker->state_change_idle_id = g_idle_add ((GSourceFunc)state_change_idle, worker);
}

static gboolean
validate_state_change (GdmSessionWorker      *worker,
                       GDBusMethodInvocation *invocation,
                       int                    new_state)
{
        if (worker->pending_invocation != NULL) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_OUTSTANDING_REQUEST,
                                                       "Cannot process state change to %s, as there is already an outstanding request to move to state %s",
                                                       get_state_name (new_state),
                                                       get_state_name (worker->state + 1));
                return FALSE;
        } else if (worker->state != new_state - 1) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_WRONG_STATE,
                                                       "Cannot move to state %s, in state %s, not %s",
                                                       get_state_name (new_state),
                                                       get_state_name (worker->state),
                                                       get_state_name (new_state - 1));
                return FALSE;
        }

        return TRUE;
}

static void
validate_and_queue_state_change (GdmSessionWorker      *worker,
                                 GDBusMethodInvocation *invocation,
                                 int                    new_state)
{
        if (validate_state_change (worker, invocation, new_state)) {
                worker->pending_invocation = invocation;
                queue_state_change (worker);
        }
}

static gboolean
gdm_session_worker_handle_authenticate (GdmDBusWorker         *object,
                                        GDBusMethodInvocation *invocation)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_AUTHENTICATED);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_authorize (GdmDBusWorker         *object,
                                     GDBusMethodInvocation *invocation)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_AUTHORIZED);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_establish_credentials (GdmDBusWorker         *object,
                                                 GDBusMethodInvocation *invocation)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_ACCREDITED);

        if (!worker->is_reauth_session) {
                worker->cred_flags = PAM_ESTABLISH_CRED;
        } else {
                worker->cred_flags = PAM_REINITIALIZE_CRED;
        }

        return TRUE;
}

static gboolean
gdm_session_worker_handle_open (GdmDBusWorker         *object,
                                GDBusMethodInvocation *invocation)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED);
        return TRUE;
}

static char **
filter_extensions (const char * const *extensions)
{
        g_autoptr(GPtrArray) array = NULL;
        g_auto(GStrv) filtered_extensions = NULL;
        size_t i, j;

        array = g_ptr_array_new_with_free_func (g_free);

        for (i = 0; extensions[i] != NULL; i++) {
                for (j = 0; gdm_supported_pam_extensions[j] != NULL; j++) {
                        if (g_strcmp0 (extensions[i], gdm_supported_pam_extensions[j]) == 0) {
                                g_ptr_array_add (array, g_strdup (gdm_supported_pam_extensions[j]));
                                break;
                        }
                }
        }
        g_ptr_array_add (array, NULL);

        filtered_extensions = g_strdupv ((char **) array->pdata);

        return g_steal_pointer (&filtered_extensions);
}

static gboolean
gdm_session_worker_handle_initialize (GdmDBusWorker         *object,
                                      GDBusMethodInvocation *invocation,
                                      GVariant              *details)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        GVariantIter      iter;
        char             *key;
        GVariant         *value;
        gboolean          wait_for_settings = FALSE;

        if (!validate_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SETUP_COMPLETE))
                return TRUE;

        g_variant_iter_init (&iter, details);
        while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
                if (g_strcmp0 (key, "service") == 0) {
                        worker->service = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "extensions") == 0) {
                        worker->extensions = filter_extensions (g_variant_get_strv (value, NULL));
                } else if (g_strcmp0 (key, "username") == 0) {
                        worker->username = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "is-program-session") == 0) {
                        worker->is_program_session = g_variant_get_boolean (value);
                } else if (g_strcmp0 (key, "log-file") == 0) {
                        worker->log_file = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "x11-display-name") == 0) {
                        worker->x11_display_name = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "x11-authority-file") == 0) {
                        worker->x11_authority_file = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "console") == 0) {
                        worker->display_device = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "seat-id") == 0) {
                        worker->display_seat_id = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "hostname") == 0) {
                        worker->hostname = g_variant_dup_string (value, NULL);
                } else if (g_strcmp0 (key, "display-is-local") == 0) {
                        worker->display_is_local = g_variant_get_boolean (value);
                } else if (g_strcmp0 (key, "display-is-initial") == 0) {
                        worker->display_is_initial = g_variant_get_boolean (value);
                }
        }

        worker->seat0_has_vts = sd_seat_can_tty ("seat0");

        worker->pending_invocation = invocation;

        if (!worker->is_program_session) {
                worker->user_settings = gdm_session_settings_new ();

                g_signal_connect_swapped (worker->user_settings,
                                          "notify::language-name",
                                          G_CALLBACK (on_saved_language_name_read),
                                          worker);

                g_signal_connect_swapped (worker->user_settings,
                                          "notify::session-name",
                                          G_CALLBACK (on_saved_session_name_read),
                                          worker);

                g_signal_connect_swapped (worker->user_settings,
                                          "notify::session-type",
                                          G_CALLBACK (on_saved_session_type_read),
                                          worker);

                if (worker->username) {
                        wait_for_settings = !gdm_session_settings_load (worker->user_settings,
                                                                        worker->username);
                }
        }

        if (wait_for_settings) {
                /* Load settings from accounts daemon before continuing
                 */
                g_signal_connect (G_OBJECT (worker->user_settings),
                                  "notify::is-loaded",
                                  G_CALLBACK (on_settings_is_loaded_changed),
                                  worker);
        } else {
                queue_state_change (worker);
        }

        return TRUE;
}

static gboolean
gdm_session_worker_handle_setup (GdmDBusWorker         *object,
                                 GDBusMethodInvocation *invocation,
                                 const char            *service,
                                 const char            *x11_display_name,
                                 const char            *x11_authority_file,
                                 const char            *console,
                                 const char            *seat_id,
                                 const char            *hostname,
                                 gboolean               display_is_local,
                                 gboolean               display_is_initial)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SETUP_COMPLETE);

        worker->service = g_strdup (service);
        worker->x11_display_name = g_strdup (x11_display_name);
        worker->x11_authority_file = g_strdup (x11_authority_file);
        worker->display_device = g_strdup (console);
        worker->display_seat_id = g_strdup (seat_id);
        worker->hostname = g_strdup (hostname);
        worker->display_is_local = display_is_local;
        worker->display_is_initial = display_is_initial;
        worker->username = NULL;

        worker->user_settings = gdm_session_settings_new ();

        g_signal_connect_swapped (worker->user_settings,
                                  "notify::language-name",
                                  G_CALLBACK (on_saved_language_name_read),
                                  worker);

        g_signal_connect_swapped (worker->user_settings,
                                  "notify::session-name",
                                  G_CALLBACK (on_saved_session_name_read),
                                  worker);
        g_signal_connect_swapped (worker->user_settings,
                                  "notify::session-type",
                                  G_CALLBACK (on_saved_session_type_read),
                                  worker);

        return TRUE;
}

static gboolean
gdm_session_worker_handle_setup_for_user (GdmDBusWorker         *object,
                                          GDBusMethodInvocation *invocation,
                                          const char            *service,
                                          const char            *username,
                                          const char            *x11_display_name,
                                          const char            *x11_authority_file,
                                          const char            *console,
                                          const char            *seat_id,
                                          const char            *hostname,
                                          gboolean               display_is_local,
                                          gboolean               display_is_initial)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);

        if (!validate_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SETUP_COMPLETE))
                return TRUE;

        worker->service = g_strdup (service);
        worker->x11_display_name = g_strdup (x11_display_name);
        worker->x11_authority_file = g_strdup (x11_authority_file);
        worker->display_device = g_strdup (console);
        worker->display_seat_id = g_strdup (seat_id);
        worker->hostname = g_strdup (hostname);
        worker->display_is_local = display_is_local;
        worker->display_is_initial = display_is_initial;
        worker->username = g_strdup (username);

        worker->user_settings = gdm_session_settings_new ();

        g_signal_connect_swapped (worker->user_settings,
                                  "notify::language-name",
                                  G_CALLBACK (on_saved_language_name_read),
                                  worker);

        g_signal_connect_swapped (worker->user_settings,
                                  "notify::session-name",
                                  G_CALLBACK (on_saved_session_name_read),
                                  worker);
        g_signal_connect_swapped (worker->user_settings,
                                  "notify::session-type",
                                  G_CALLBACK (on_saved_session_type_read),
                                  worker);

        /* Load settings from accounts daemon before continuing
         */
        worker->pending_invocation = invocation;
        if (gdm_session_settings_load (worker->user_settings, username)) {
                queue_state_change (worker);
        } else {
                g_signal_connect (G_OBJECT (worker->user_settings),
                                  "notify::is-loaded",
                                  G_CALLBACK (on_settings_is_loaded_changed),
                                  worker);
        }

        return TRUE;
}

static gboolean
gdm_session_worker_handle_setup_for_program (GdmDBusWorker         *object,
                                             GDBusMethodInvocation *invocation,
                                             const char            *service,
                                             const char            *username,
                                             const char            *x11_display_name,
                                             const char            *x11_authority_file,
                                             const char            *console,
                                             const char            *seat_id,
                                             const char            *hostname,
                                             gboolean               display_is_local,
                                             gboolean               display_is_initial,
                                             const char            *log_file)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        validate_and_queue_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SETUP_COMPLETE);

        worker->service = g_strdup (service);
        worker->x11_display_name = g_strdup (x11_display_name);
        worker->x11_authority_file = g_strdup (x11_authority_file);
        worker->display_device = g_strdup (console);
        worker->display_seat_id = g_strdup (seat_id);
        worker->hostname = g_strdup (hostname);
        worker->display_is_local = display_is_local;
        worker->display_is_initial = display_is_initial;
        worker->username = g_strdup (username);
        worker->log_file = g_strdup (log_file);
        worker->is_program_session = TRUE;

        return TRUE;
}

static gboolean
gdm_session_worker_handle_start_program (GdmDBusWorker         *object,
                                         GDBusMethodInvocation *invocation,
                                         const char            *text)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_autoptr(GError) parse_error = NULL;
        validate_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SESSION_STARTED);

        if (worker->is_reauth_session) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_IN_REAUTH_SESSION,
                                                       "Cannot start a program while in a reauth session");
                return TRUE;
        }

        g_debug ("GdmSessionWorker: start program: %s", text);

        g_clear_pointer (&worker->arguments, g_strfreev);
        if (! g_shell_parse_argv (text, NULL, &worker->arguments, &parse_error)) {
                g_dbus_method_invocation_take_error (invocation,
                                                     g_steal_pointer (&parse_error));
                return TRUE;
        }

        worker->pending_invocation = invocation;
        queue_state_change (worker);

        return TRUE;
}

static void
on_reauthentication_client_connected (GdmSession              *session,
                                      GCredentials            *credentials,
                                      GPid                     pid_of_client,
                                      ReauthenticationRequest *request)
{
        g_debug ("GdmSessionWorker: client connected to reauthentication server");
}

static void
on_reauthentication_client_disconnected (GdmSession              *session,
                                         GCredentials            *credentials,
                                         GPid                     pid_of_client,
                                         ReauthenticationRequest *request)
{
        GdmSessionWorker *worker;

        g_debug ("GdmSessionWorker: client disconnected from reauthentication server");

        worker = request->worker;
        g_hash_table_remove (worker->reauthentication_requests,
                             GINT_TO_POINTER (pid_of_client));
}

static void
on_reauthentication_cancelled (GdmSession              *session,
                               ReauthenticationRequest *request)
{
        g_debug ("GdmSessionWorker: client cancelled reauthentication request");
        gdm_session_reset (session);
}

static void
on_reauthentication_conversation_started (GdmSession              *session,
                                          const char              *service_name,
                                          ReauthenticationRequest *request)
{
        g_debug ("GdmSessionWorker: reauthentication service '%s' started",
                 service_name);
}

static void
on_reauthentication_conversation_stopped (GdmSession              *session,
                                          const char              *service_name,
                                          ReauthenticationRequest *request)
{
        g_debug ("GdmSessionWorker: reauthentication service '%s' stopped",
                 service_name);
}

static void
on_reauthentication_verification_complete (GdmSession              *session,
                                           const char              *service_name,
                                           ReauthenticationRequest *request)
{
        GdmSessionWorker *worker;

        worker = request->worker;

        g_debug ("GdmSessionWorker: pid %d reauthenticated user %d with service '%s'",
                 (int) request->pid_of_caller,
                 (int) request->uid_of_caller,
                 service_name);
        gdm_session_reset (session);

        gdm_dbus_worker_emit_reauthenticated (GDM_DBUS_WORKER (worker), service_name, request->pid_of_caller);
}

static ReauthenticationRequest *
reauthentication_request_new (GdmSessionWorker      *worker,
                              GPid                   pid_of_caller,
                              uid_t                  uid_of_caller,
                              GDBusMethodInvocation *invocation)
{
        ReauthenticationRequest *request;
        const char * const * environment;
        const char *address;

        environment = gdm_session_worker_get_environment (worker);

        request = g_slice_new (ReauthenticationRequest);

        request->worker = worker;
        request->pid_of_caller = pid_of_caller;
        request->uid_of_caller = uid_of_caller;
        request->session = gdm_session_new (GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE,
                                            uid_of_caller,
                                            worker->x11_display_name,
                                            worker->hostname,
                                            worker->display_device,
                                            worker->display_seat_id,
                                            worker->x11_authority_file,
                                            worker->display_is_local,
                                            environment);

        g_signal_connect (request->session,
                          "client-connected",
                          G_CALLBACK (on_reauthentication_client_connected),
                          request);
        g_signal_connect (request->session,
                          "client-disconnected",
                          G_CALLBACK (on_reauthentication_client_disconnected),
                          request);
        g_signal_connect (request->session,
                          "cancelled",
                          G_CALLBACK (on_reauthentication_cancelled),
                          request);
        g_signal_connect (request->session,
                          "conversation-started",
                          G_CALLBACK (on_reauthentication_conversation_started),
                          request);
        g_signal_connect (request->session,
                          "conversation-stopped",
                          G_CALLBACK (on_reauthentication_conversation_stopped),
                          request);
        g_signal_connect (request->session,
                          "verification-complete",
                          G_CALLBACK (on_reauthentication_verification_complete),
                          request);

        address = gdm_session_get_server_address (request->session);

        gdm_dbus_worker_complete_start_reauthentication (GDM_DBUS_WORKER (worker),
                                                         invocation,
                                                         address);

        return request;
}

static gboolean
gdm_session_worker_handle_start_reauthentication (GdmDBusWorker         *object,
                                                  GDBusMethodInvocation *invocation,
                                                  int                    pid_of_caller,
                                                  int                    uid_of_caller)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        ReauthenticationRequest *request;

        if (worker->state != GDM_SESSION_WORKER_STATE_SESSION_STARTED) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_WRONG_STATE,
                                                       "Cannot reauthenticate while in state %s",
                                                       get_state_name (worker->state));
                return TRUE;
        }

        g_debug ("GdmSessionWorker: start reauthentication");

        request = reauthentication_request_new (worker, pid_of_caller, uid_of_caller, invocation);
        g_hash_table_replace (worker->reauthentication_requests,
                              GINT_TO_POINTER (pid_of_caller),
                              request);
        return TRUE;
}

static GObject *
gdm_session_worker_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmSessionWorker  *worker;
        g_autoptr(GError) error = NULL;

        worker = GDM_SESSION_WORKER (G_OBJECT_CLASS (gdm_session_worker_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

        g_debug ("GdmSessionWorker: connecting to address: %s", worker->server_address);

        worker->connection = g_dbus_connection_new_for_address_sync (worker->server_address,
                                                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                     NULL,
                                                                     NULL,
                                                                     &error);
        if (worker->connection == NULL) {
                g_warning ("error opening connection: %s", error->message);
                exit (EXIT_FAILURE);
        }

        worker->manager = GDM_DBUS_WORKER_MANAGER (gdm_dbus_worker_manager_proxy_new_sync (worker->connection,
                                                                                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                                                           NULL, /* dbus name */
                                                                                           GDM_SESSION_DBUS_PATH,
                                                                                           NULL,
                                                                                           &error));
        if (worker->manager == NULL) {
                g_warning ("error creating session proxy: %s", error->message);
                exit (EXIT_FAILURE);
        }

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (worker),
                                               worker->connection,
                                               GDM_WORKER_DBUS_PATH,
                                               &error)) {
                g_warning ("Error while exporting object: %s", error->message);
                exit (EXIT_FAILURE);
        }

        g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (worker->manager), G_MAXINT);

        /* Send an initial Hello message so that the session can associate
         * the conversation we manage with our pid.
         */
        gdm_dbus_worker_manager_call_hello_sync (worker->manager,
                                                 NULL,
                                                 NULL);

        return G_OBJECT (worker);
}

static void
worker_interface_init (GdmDBusWorkerIface *interface)
{
        interface->handle_initialize = gdm_session_worker_handle_initialize;
        /* The next three are for backward compat only */
        interface->handle_setup = gdm_session_worker_handle_setup;
        interface->handle_setup_for_user = gdm_session_worker_handle_setup_for_user;
        interface->handle_setup_for_program = gdm_session_worker_handle_setup_for_program;
        interface->handle_authenticate = gdm_session_worker_handle_authenticate;
        interface->handle_authorize = gdm_session_worker_handle_authorize;
        interface->handle_establish_credentials = gdm_session_worker_handle_establish_credentials;
        interface->handle_open = gdm_session_worker_handle_open;
        interface->handle_set_language_name = gdm_session_worker_handle_set_language_name;
        interface->handle_set_session_name = gdm_session_worker_handle_set_session_name;
        interface->handle_set_session_display_mode = gdm_session_worker_handle_set_session_display_mode;
        interface->handle_set_environment_variable = gdm_session_worker_handle_set_environment_variable;
        interface->handle_start_program = gdm_session_worker_handle_start_program;
        interface->handle_start_reauthentication = gdm_session_worker_handle_start_reauthentication;
}

static void
gdm_session_worker_class_init (GdmSessionWorkerClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_worker_get_property;
        object_class->set_property = gdm_session_worker_set_property;
        object_class->constructor = gdm_session_worker_constructor;
        object_class->finalize = gdm_session_worker_finalize;

        g_object_class_install_property (object_class,
                                         PROP_SERVER_ADDRESS,
                                         g_param_spec_string ("server-address",
                                                              "server address",
                                                              "server address",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_IS_REAUTH_SESSION,
                                         g_param_spec_boolean ("is-reauth-session",
                                                               "is reauth session",
                                                               "is reauth session",
                                                              FALSE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_STATE,
                                         g_param_spec_enum ("state",
                                                            "state",
                                                            "state",
                                                            GDM_TYPE_SESSION_WORKER_STATE,
                                                            GDM_SESSION_WORKER_STATE_NONE,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
reauthentication_request_free (ReauthenticationRequest *request)
{

        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_client_connected),
                                              request);
        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_client_disconnected),
                                              request);
        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_cancelled),
                                              request);
        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_conversation_started),
                                              request);
        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_conversation_stopped),
                                              request);
        g_signal_handlers_disconnect_by_func (request->session,
                                              G_CALLBACK (on_reauthentication_verification_complete),
                                              request);
        g_clear_object (&request->session);
        g_slice_free (ReauthenticationRequest, request);
}

static void
gdm_session_worker_init (GdmSessionWorker *worker)
{
        worker->reauthentication_requests = g_hash_table_new_full (NULL,
                                                                   NULL,
                                                                   NULL,
                                                                   (GDestroyNotify)
                                                                   reauthentication_request_free);
}

static void
gdm_session_worker_finalize (GObject *object)
{
        GdmSessionWorker *worker;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_WORKER (object));

        worker = GDM_SESSION_WORKER (object);

        g_return_if_fail (worker != NULL);

        g_clear_handle_id (&worker->child_watch_id, g_source_remove);

        if (worker->child_pid > 0) {
                gdm_signal_pid (worker->child_pid, SIGTERM);
                gdm_wait_on_pid (worker->child_pid);
        }

        if (worker->pam_handle != NULL) {
                gdm_session_worker_uninitialize_pam (worker, PAM_SUCCESS);
        }

        jump_back_to_initial_vt (worker);

        g_clear_object (&worker->user_settings);
        g_free (worker->service);
        g_free (worker->x11_display_name);
        g_free (worker->x11_authority_file);
        g_free (worker->display_device);
        g_free (worker->display_seat_id);
        g_free (worker->hostname);
        g_free (worker->username);
        g_free (worker->server_address);
        g_strfreev (worker->arguments);
        g_strfreev (worker->extensions);

        g_hash_table_unref (worker->reauthentication_requests);

        G_OBJECT_CLASS (gdm_session_worker_parent_class)->finalize (object);
}

GdmSessionWorker *
gdm_session_worker_new (const char *address,
                        gboolean    is_reauth_session)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_WORKER,
                               "server-address", address,
                               "is-reauth-session", is_reauth_session,
                               NULL);

        return GDM_SESSION_WORKER (object);
}
