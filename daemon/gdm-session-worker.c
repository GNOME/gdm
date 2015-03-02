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
#ifdef WITH_SYSTEMD
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <sys/kd.h>
#endif
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

#include <X11/Xauth.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef ENABLE_SYSTEMD_JOURNAL
#include <systemd/sd-journal.h>
#endif

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif /* HAVE_SELINUX */

#include "gdm-common.h"
#include "gdm-log.h"
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

#define GDM_SESSION_WORKER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_WORKER, GdmSessionWorkerPrivate))

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

enum {
        GDM_SESSION_WORKER_STATE_NONE = 0,
        GDM_SESSION_WORKER_STATE_SETUP_COMPLETE,
        GDM_SESSION_WORKER_STATE_AUTHENTICATED,
        GDM_SESSION_WORKER_STATE_AUTHORIZED,
        GDM_SESSION_WORKER_STATE_ACCREDITED,
        GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED,
        GDM_SESSION_WORKER_STATE_SESSION_OPENED,
        GDM_SESSION_WORKER_STATE_SESSION_STARTED
};

typedef struct
{
        GdmSessionWorker *worker;
        GdmSession       *session;
        GPid              pid_of_caller;
        uid_t             uid_of_caller;

} ReauthenticationRequest;

struct GdmSessionWorkerPrivate
{
        int               state;

        int               exit_code;

#ifdef WITH_CONSOLE_KIT
        char             *session_cookie;
#endif

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
        char             *session_type;
        char             *session_id;
        uid_t             uid;
        gid_t             gid;
        gboolean          password_is_required;

        int               cred_flags;
        int               login_vt;
        int               session_vt;
        int               session_tty_fd;

        char            **arguments;
        guint32           cancelled : 1;
        guint32           timed_out : 1;
        guint32           is_program_session : 1;
        guint32           is_reauth_session : 1;
        guint32           display_is_local : 1;
        guint32           display_is_initial : 1;
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

enum {
        PROP_0,
        PROP_SERVER_ADDRESS,
        PROP_IS_REAUTH_SESSION,
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

#ifdef WITH_CONSOLE_KIT
static gboolean
open_ck_session (GdmSessionWorker  *worker)
{
        GDBusConnection  *system_bus;
        GVariantBuilder   builder;
        GVariant         *parameters;
        GVariant         *in_args;
        struct passwd    *pwent;
        GVariant         *reply;
        GError           *error = NULL;
        const char       *display_name;
        const char       *display_device;
        const char       *display_hostname;
        const char       *session_type;
        gint32            uid;

        g_assert (worker->priv->session_cookie == NULL);

        if (worker->priv->x11_display_name != NULL) {
                display_name = worker->priv->x11_display_name;
        } else {
                display_name = "";
        }
        if (worker->priv->hostname != NULL) {
                display_hostname = worker->priv->hostname;
        } else {
                display_hostname = "";
        }
        if (worker->priv->display_device != NULL) {
                display_device = worker->priv->display_device;
        } else {
                display_device = "";
        }

        if (worker->priv->session_type != NULL) {
                session_type = worker->priv->session_type;
        } else {
                session_type = "";
        }

        g_assert (worker->priv->username != NULL);

        gdm_get_pwent_for_name (worker->priv->username, &pwent);
        if (pwent == NULL) {
                goto out;
        }

        uid = (gint32) pwent->pw_uid;

        error = NULL;
        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);

        if (system_bus == NULL) {
                g_warning ("Couldn't create connection to system bus: %s",
                           error->message);

                g_error_free (error);
                goto out;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sv)"));
        g_variant_builder_add_parsed (&builder, "('unix-user', <%i>)", uid);
        g_variant_builder_add_parsed (&builder, "('x11-display-device', <%s>)", display_device);
        g_variant_builder_add_parsed (&builder, "('x11-display', <%s>)", display_name);
        g_variant_builder_add_parsed (&builder, "('remote-host-name', <%s>)", display_hostname);
        g_variant_builder_add_parsed (&builder, "('is-local', <%b>)", worker->priv->display_is_local);
        g_variant_builder_add_parsed (&builder, "('session-type', <%s>)", session_type);

        parameters = g_variant_builder_end (&builder);
        in_args = g_variant_new_tuple (&parameters, 1);

        reply = g_dbus_connection_call_sync (system_bus,
                                             "org.freedesktop.ConsoleKit",
                                             "/org/freedesktop/ConsoleKit/Manager",
                                             "org.freedesktop.ConsoleKit.Manager",
                                             "OpenSessionWithParameters",
                                             in_args,
                                             G_VARIANT_TYPE ("(s)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (! reply) {
                g_warning ("%s\n", error->message);
                g_clear_error (&error);
                goto out;
        }

        g_variant_get (reply, "(s)", &worker->priv->session_cookie);

        g_variant_unref (reply);

out:
        return worker->priv->session_cookie != NULL;
}

static void
close_ck_session (GdmSessionWorker *worker)
{
        GDBusConnection  *system_bus;
        GVariant         *reply;
        GError           *error = NULL;
        gboolean          was_closed;

        if (worker->priv->session_cookie == NULL) {
                return;
        }

        error = NULL;
        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);

        if (system_bus == NULL) {
                g_warning ("Couldn't create connection to system bus: %s",
                           error->message);

                g_error_free (error);
                goto out;
        }

        reply = g_dbus_connection_call_sync (system_bus,
                                             "org.freedesktop.ConsoleKit",
                                             "/org/freedesktop/ConsoleKit/Manager",
                                             "org.freedesktop.ConsoleKit.Manager",
                                             "CloseSession",
                                             g_variant_new ("(s)", worker->priv->session_cookie),
                                             G_VARIANT_TYPE ("(b)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (! reply) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
                goto out;
        }

        g_variant_get (reply, "(b)", &was_closed);

        if (!was_closed) {
                g_warning ("Unable to close ConsoleKit session");
        }

        g_variant_unref (reply);

out:
        g_clear_pointer (&worker->priv->session_cookie,
                         (GDestroyNotify) g_free);
}

static char *
get_ck_session_id (GdmSessionWorker *worker)
{
        GDBusConnection  *system_bus;
        GVariant         *reply;
        GError           *error = NULL;
        char             *session_id = NULL;

        error = NULL;
        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);

        if (system_bus == NULL) {
                g_warning ("Couldn't create connection to system bus: %s",
                           error->message);

                g_error_free (error);
                goto out;
        }

        reply = g_dbus_connection_call_sync (system_bus,
                                             "org.freedesktop.ConsoleKit",
                                             "/org/freedesktop/ConsoleKit/Manager",
                                             "org.freedesktop.ConsoleKit.Manager",
                                             "GetSessionForCookie",
                                             g_variant_new ("(s)", worker->priv->session_cookie),
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (reply == NULL) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
                goto out;
        }

        g_variant_get (reply, "(o)", &session_id);

        g_variant_unref (reply);

out:
        return session_id;
}
#endif

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

        g_assert (worker->priv->pam_handle != NULL);

        if (pam_get_item (worker->priv->pam_handle, PAM_USER, &item) == PAM_SUCCESS) {
                if (username != NULL) {
                        *username = g_strdup ((char *) item);
                        g_debug ("GdmSessionWorker: username is '%s'",
                                 *username != NULL ? *username : "<unset>");
                }

                if (worker->priv->auditor != NULL) {
                        gdm_session_auditor_set_username (worker->priv->auditor, (char *)item);
                }

                return TRUE;
        }

        return FALSE;
}

static void
attempt_to_load_user_settings (GdmSessionWorker *worker,
                               const char       *username)
{
        g_debug ("GdmSessionWorker: attempting to load user settings");
        gdm_session_settings_load (worker->priv->user_settings,
                                   username);
}

static void
gdm_session_worker_update_username (GdmSessionWorker *worker)
{
        char    *username;
        gboolean res;

        username = NULL;
        res = gdm_session_worker_get_username (worker, &username);
        if (res) {
                g_debug ("GdmSessionWorker: old-username='%s' new-username='%s'",
                         worker->priv->username != NULL ? worker->priv->username : "<unset>",
                         username != NULL ? username : "<unset>");


                gdm_session_auditor_set_username (worker->priv->auditor, worker->priv->username);

                if ((worker->priv->username == username) ||
                    ((worker->priv->username != NULL) && (username != NULL) &&
                     (strcmp (worker->priv->username, username) == 0)))
                        goto out;

                g_debug ("GdmSessionWorker: setting username to '%s'", username);

                g_free (worker->priv->username);
                worker->priv->username = username;
                username = NULL;

                gdm_dbus_worker_emit_username_changed (GDM_DBUS_WORKER (worker),
                                                       worker->priv->username);

                /* We have a new username to try. If we haven't been able to
                 * read user settings up until now, then give it a go now
                 * (see the comment in do_setup for rationale on why it's useful
                 * to keep trying to read settings)
                 */
                if (worker->priv->username != NULL &&
                    worker->priv->username[0] != '\0' &&
                    !gdm_session_settings_is_loaded (worker->priv->user_settings)) {
                        attempt_to_load_user_settings (worker, worker->priv->username);
                }
        }

 out:
        g_free (username);
}

static gboolean
gdm_session_worker_ask_question (GdmSessionWorker *worker,
                                 const char       *question,
                                 char            **answerp)
{
        return gdm_dbus_worker_manager_call_info_query_sync (worker->priv->manager,
                                                             worker->priv->service,
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
        return gdm_dbus_worker_manager_call_secret_info_query_sync (worker->priv->manager,
                                                                    worker->priv->service,
                                                                    question,
                                                                    answerp,
                                                                    NULL,
                                                                    NULL);
}

static gboolean
gdm_session_worker_report_info (GdmSessionWorker *worker,
                                const char       *info)
{
        return gdm_dbus_worker_manager_call_info_sync (worker->priv->manager,
                                                       worker->priv->service,
                                                       info,
                                                       NULL,
                                                       NULL);
}

static gboolean
gdm_session_worker_report_problem (GdmSessionWorker *worker,
                                   const char       *problem)
{
        return gdm_dbus_worker_manager_call_problem_sync (worker->priv->manager,
                                                          worker->priv->service,
                                                          problem,
                                                          NULL,
                                                          NULL);
}

static char *
convert_to_utf8 (const char *str)
{
        char *utf8;
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

        return utf8;
}

static gboolean
gdm_session_worker_process_pam_message (GdmSessionWorker          *worker,
                                        const struct pam_message  *query,
                                        char                     **response_text)
{
        char    *user_answer;
        gboolean res;
        char    *utf8_msg;
        char	*msg;

        if (response_text != NULL) {
                *response_text = NULL;
        }

        gdm_session_worker_update_username (worker);

        g_debug ("GdmSessionWorker: received pam message of type %u with payload '%s'",
                 query->msg_style, query->msg);

        utf8_msg = convert_to_utf8 (query->msg);

        worker->priv->cancelled = FALSE;
        worker->priv->timed_out = FALSE;

        user_answer = NULL;
        res = FALSE;
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
                g_free (msg);
                break;
#endif
        default:
                res = FALSE;
                g_warning ("Unknown and unhandled message type %d\n",
                           query->msg_style);

                break;
        }

        if (worker->priv->timed_out) {
                gdm_dbus_worker_emit_cancel_pending_query (GDM_DBUS_WORKER (worker));
                worker->priv->timed_out = FALSE;
        }

        if (user_answer != NULL) {
                /* we strndup and g_free to make sure we return malloc'd
                 * instead of g_malloc'd memory.  PAM_MAX_RESP_SIZE includes
                 * the '\0' terminating character, thus the "- 1".
                 */
                if (res && response_text != NULL) {
                        *response_text = strndup (user_answer, PAM_MAX_RESP_SIZE - 1);
                }

                memset (user_answer, '\0', strlen (user_answer));
                g_free (user_answer);

                g_debug ("GdmSessionWorker: trying to get updated username");

                res = TRUE;
        }

        g_free (utf8_msg);

        return res;
}

static const char *
get_friendly_error_message (int error_code)
{
        switch (error_code) {
            case PAM_SUCCESS:
            case PAM_IGNORE:
                return "";
                break;

            case PAM_ACCT_EXPIRED:
            case PAM_AUTHTOK_EXPIRED:
                return _("Your account was given a time limit that's now passed.");
                break;

            default:
                break;
        }

        return _("Sorry, that didn't work. Please try again.");
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
                char    *response_text;

                response_text = NULL;
                got_response = gdm_session_worker_process_pam_message (worker,
                                                                       messages[i],
                                                                       &response_text);
                if (!got_response) {
                        goto out;
                }

                replies[i].resp = response_text;
                replies[i].resp_retcode = PAM_SUCCESS;
        }

        return_value = PAM_SUCCESS;

 out:
        if (return_value != PAM_SUCCESS) {
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
                 pam_strerror (worker->priv->pam_handle, return_value));

        return return_value;
}

static void
gdm_session_worker_start_auditor (GdmSessionWorker *worker)
{
    /* Use dummy auditor so program session doesn't pollute user audit logs
     */
    if (worker->priv->is_program_session) {
            worker->priv->auditor = gdm_session_auditor_new (worker->priv->hostname,
                                                             worker->priv->display_device);
            return;
    }

/* FIXME: it may make sense at some point to keep a list of
 * auditors, instead of assuming they are mutually exclusive
 */
#if defined (HAVE_ADT)
        worker->priv->auditor = gdm_session_solaris_auditor_new (worker->priv->hostname,
                                                                 worker->priv->display_device);
#elif defined (HAVE_LIBAUDIT)
        worker->priv->auditor = gdm_session_linux_auditor_new (worker->priv->hostname,
                                                               worker->priv->display_device);
#else
        worker->priv->auditor = gdm_session_auditor_new (worker->priv->hostname,
                                                         worker->priv->display_device);
#endif
}

static void
gdm_session_worker_stop_auditor (GdmSessionWorker *worker)
{
        g_object_unref (worker->priv->auditor);
        worker->priv->auditor = NULL;
}

#ifdef WITH_SYSTEMD
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

static void
handle_terminal_vt_switches (GdmSessionWorker *worker,
                             int               tty_fd)
{
        struct vt_mode setmode_request = { 0 };

        setmode_request.mode = VT_PROCESS;
        setmode_request.relsig = RELEASE_DISPLAY_SIGNAL;
        setmode_request.acqsig = ACQUIRE_DISPLAY_SIGNAL;

        if (ioctl (tty_fd, VT_SETMODE, &setmode_request) < 0) {
                g_debug ("GdmSessionWorker: couldn't manage VTs manually: %m");
        }

        signal (RELEASE_DISPLAY_SIGNAL, on_release_display);
        signal (ACQUIRE_DISPLAY_SIGNAL, on_acquire_display);
}

static void
jump_to_vt (GdmSessionWorker  *worker,
            int                vt_number)
{
        int fd;
        gboolean just_opened_tty = FALSE;

        g_debug ("GdmSessionWorker: jumping to VT %d", vt_number);
        if (worker->priv->session_tty_fd != -1) {

                fd = worker->priv->session_tty_fd;

                g_debug ("GdmSessionWorker: first setting graphics mode to prevent flicker");
                if (ioctl (fd, KDSETMODE, KD_GRAPHICS) < 0) {
                        g_debug ("GdmSessionWorker: couldn't set graphics mode: %m");
                }

                handle_terminal_vt_switches (worker, fd);

        } else {
                fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);
                just_opened_tty = TRUE;
        }

        if (ioctl (fd, VT_ACTIVATE, vt_number) < 0) {
                g_debug ("GdmSessionWorker: couldn't initiate jump to VT %d: %m",
                         vt_number);
        } else if (ioctl (fd, VT_WAITACTIVE, vt_number) < 0) {
                g_debug ("GdmSessionWorker: couldn't finalize jump to VT %d: %m",
                         vt_number);
        }

        if (just_opened_tty) {
                close(fd);
        }
}
#endif

static void
gdm_session_worker_uninitialize_pam (GdmSessionWorker *worker,
                                     int               status)
{
        g_debug ("GdmSessionWorker: uninitializing PAM");

        if (worker->priv->pam_handle == NULL)
                return;

        gdm_session_worker_get_username (worker, NULL);

        if (worker->priv->state >= GDM_SESSION_WORKER_STATE_SESSION_OPENED) {
                pam_close_session (worker->priv->pam_handle, 0);
                gdm_session_auditor_report_logout (worker->priv->auditor);
        } else {
                gdm_session_auditor_report_login_failure (worker->priv->auditor,
                                                          status,
                                                          pam_strerror (worker->priv->pam_handle, status));
        }

        if (worker->priv->state >= GDM_SESSION_WORKER_STATE_ACCREDITED) {
                pam_setcred (worker->priv->pam_handle, PAM_DELETE_CRED);
        }

        pam_end (worker->priv->pam_handle, status);
        worker->priv->pam_handle = NULL;

        gdm_session_worker_stop_auditor (worker);

#ifdef WITH_SYSTEMD
        if (worker->priv->login_vt != worker->priv->session_vt) {
                jump_to_vt (worker, worker->priv->login_vt);
        }
#endif

        worker->priv->login_vt = 0;
        worker->priv->session_vt = 0;

        g_debug ("GdmSessionWorker: state NONE");
        worker->priv->state = GDM_SESSION_WORKER_STATE_NONE;
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

#ifdef PAM_XAUTHDATA
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
gdm_session_worker_initialize_pam (GdmSessionWorker *worker,
                                   const char       *service,
                                   const char       *username,
                                   const char       *hostname,
                                   gboolean          display_is_local,
                                   const char       *x11_display_name,
                                   const char       *x11_authority_file,
                                   const char       *display_device,
                                   const char       *seat_id,
                                   GError          **error)
{
        struct pam_conv        pam_conversation;
        int                    error_code;

        g_assert (worker->priv->pam_handle == NULL);

        g_debug ("GdmSessionWorker: initializing PAM; service=%s username=%s seat=%s",
                 service ? service : "(null)",
                 username ? username : "(null)",
                 seat_id ? seat_id : "(null)");

        pam_conversation.conv = (GdmSessionWorkerPamNewMessagesFunc) gdm_session_worker_pam_new_messages_handler;
        pam_conversation.appdata_ptr = worker;

        gdm_session_worker_start_auditor (worker);
        error_code = pam_start (service,
                                username,
                                &pam_conversation,
                                &worker->priv->pam_handle);
        if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: could not initialize PAM: (error code %d)", error_code);
                /* we don't use pam_strerror here because it requires a valid
                 * pam handle, and if pam_start fails pam_handle is undefined
                 */
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
                             "%s", "");

                goto out;
        }

        /* set USER PROMPT */
        if (username == NULL) {
                error_code = pam_set_item (worker->priv->pam_handle, PAM_USER_PROMPT, _("Username:"));

                if (error_code != PAM_SUCCESS) {
                        g_debug ("GdmSessionWorker: error informing authentication system of preferred username prompt: %s",
                                pam_strerror (worker->priv->pam_handle, error_code));
                        g_set_error (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     "%s", "");
                        goto out;
                }
        }

        /* set RHOST */
        if (hostname != NULL && hostname[0] != '\0') {
                error_code = pam_set_item (worker->priv->pam_handle, PAM_RHOST, hostname);
                g_debug ("error informing authentication system of user's hostname %s: %s",
                         hostname,
                         pam_strerror (worker->priv->pam_handle, error_code));

                if (error_code != PAM_SUCCESS) {
                        g_set_error (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     "%s", "");
                        goto out;
                }
        }

#ifdef WITH_SYSTEMD
        /* set seat ID */
        if (seat_id != NULL && seat_id[0] != '\0' && LOGIND_RUNNING()) {
                gdm_session_worker_set_environment_variable (worker, "XDG_SEAT", seat_id);
        }
#endif

        if (strcmp (service, "gdm-launch-environment") == 0) {
                gdm_session_worker_set_environment_variable (worker, "XDG_SESSION_CLASS", "greeter");
        }

        g_debug ("GdmSessionWorker: state SETUP_COMPLETE");
        worker->priv->state = GDM_SESSION_WORKER_STATE_SETUP_COMPLETE;

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

        g_debug ("GdmSessionWorker: authenticating user %s", worker->priv->username);

        authentication_flags = 0;

        if (password_is_required) {
                authentication_flags |= PAM_DISALLOW_NULL_AUTHTOK;
        }

        /* blocking call, does the actual conversation */
        error_code = pam_authenticate (worker->priv->pam_handle, authentication_flags);

        if (error_code == PAM_AUTHINFO_UNAVAIL) {
                g_debug ("GdmSessionWorker: authentication service unavailable");

                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
                             "%s", "");
                goto out;
        } else if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: authentication returned %d: %s", error_code, pam_strerror (worker->priv->pam_handle, error_code));

                /*
                 * Do not display a different message for user unknown versus
                 * a failed password for a valid user.
                 */
                if (error_code == PAM_USER_UNKNOWN) {
                        error_code = PAM_AUTH_ERR;
                }

                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                             "%s", get_friendly_error_message (error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state AUTHENTICATED");
        worker->priv->state = GDM_SESSION_WORKER_STATE_AUTHENTICATED;

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
        error_code = pam_acct_mgmt (worker->priv->pam_handle, authentication_flags);

        /* it's possible that the user needs to change their password or pin code
         */
        if (error_code == PAM_NEW_AUTHTOK_REQD && !worker->priv->is_program_session) {
                g_debug ("GdmSessionWorker: authenticated user requires new auth token");
                gdm_session_worker_report_problem (worker, _("Your password has expired, please change it now."));
                error_code = pam_chauthtok (worker->priv->pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);

                gdm_session_worker_get_username (worker, NULL);

                if (error_code != PAM_SUCCESS) {
                        gdm_session_auditor_report_password_change_failure (worker->priv->auditor);
                } else {
                        gdm_session_auditor_report_password_changed (worker->priv->auditor);
                }
        }

        /* If the user is reauthenticating, then authorization isn't required to
         * proceed, the user is already logged in after all.
         */
        if (worker->priv->is_reauth_session) {
                error_code = PAM_SUCCESS;
        }

        if (error_code != PAM_SUCCESS) {
                g_debug ("GdmSessionWorker: user is not authorized to log in: %s",
                         pam_strerror (worker->priv->pam_handle, error_code));
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_AUTHORIZING,
                             "%s", get_friendly_error_message (error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state AUTHORIZED");
        worker->priv->state = GDM_SESSION_WORKER_STATE_AUTHORIZED;

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
        int error_code;
        char *environment_entry;

        if (value != NULL) {
                environment_entry = g_strdup_printf ("%s=%s", key, value);
        } else {
                /* empty value means "remove from environment" */
                environment_entry = g_strdup (key);
        }

        error_code = pam_putenv (worker->priv->pam_handle,
                                 environment_entry);

        if (error_code != PAM_SUCCESS) {
                g_warning ("cannot put %s in pam environment: %s\n",
                           environment_entry,
                           pam_strerror (worker->priv->pam_handle, error_code));
        }
        g_debug ("GdmSessionWorker: Set PAM environment variable: '%s'", environment_entry);
        g_free (environment_entry);
}

static char *
gdm_session_worker_get_environment_variable (GdmSessionWorker *worker,
                                             const char       *key)
{
        return g_strdup (pam_getenv (worker->priv->pam_handle, key));
}

static void
gdm_session_worker_update_environment_from_passwd_info (GdmSessionWorker *worker,
                                                        uid_t             uid,
                                                        gid_t             gid,
                                                        const char       *home,
                                                        const char       *shell)
{
        gdm_session_worker_set_environment_variable (worker, "LOGNAME", worker->priv->username);
        gdm_session_worker_set_environment_variable (worker, "USER", worker->priv->username);
        gdm_session_worker_set_environment_variable (worker, "USERNAME", worker->priv->username);
        gdm_session_worker_set_environment_variable (worker, "HOME", home);
        gdm_session_worker_set_environment_variable (worker, "SHELL", shell);
}

static gboolean
gdm_session_worker_environment_variable_is_set (GdmSessionWorker *worker,
                                                const char       *key)
{
        return pam_getenv (worker->priv->pam_handle, key) != NULL;
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
        worker->priv->uid = uid;
        worker->priv->gid = gid;

        if (setgid (gid) < 0) {
                return FALSE;
        }

        if (initgroups (worker->priv->username, gid) < 0) {
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
        aux_buffer_size = 0;

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

static gboolean
gdm_session_worker_accredit_user (GdmSessionWorker  *worker,
                                  GError           **error)
{
        gboolean ret;
        gboolean res;
        uid_t    uid;
        gid_t    gid;
        char    *shell;
        char    *home;
        int      error_code;

        ret = FALSE;

        home = NULL;
        shell = NULL;

        if (worker->priv->username == NULL) {
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
        res = _lookup_passwd_info (worker->priv->username,
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
                g_set_error (error, GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                             "%s", _("Unable to change to user"));
                goto out;
        }

        error_code = pam_setcred (worker->priv->pam_handle, worker->priv->cred_flags);

        /* If the user is reauthenticating and they've made it this far, then there
         * is no reason we should lock them out of their session.  They've already
         * proved they are they same person who logged in, and that's all we care
         * about.
         */
        if (worker->priv->is_reauth_session) {
                error_code = PAM_SUCCESS;
        }

        if (error_code != PAM_SUCCESS) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
                             "%s",
                             pam_strerror (worker->priv->pam_handle, error_code));
                goto out;
        }

        ret = TRUE;

 out:
        g_free (home);
        g_free (shell);
        if (ret) {
                g_debug ("GdmSessionWorker: state ACCREDITED");
                ret = TRUE;

                gdm_session_worker_get_username (worker, NULL);
                gdm_session_auditor_report_user_accredited (worker->priv->auditor);
                worker->priv->state = GDM_SESSION_WORKER_STATE_ACCREDITED;
        } else {
                gdm_session_worker_uninitialize_pam (worker, error_code);
        }

        return ret;
}

static const char * const *
gdm_session_worker_get_environment (GdmSessionWorker *worker)
{
        return (const char * const *) pam_getenvlist (worker->priv->pam_handle);
}

#ifdef WITH_CONSOLE_KIT
static void
register_ck_session (GdmSessionWorker *worker)
{
#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                return;
        }
#endif

        open_ck_session (worker);

        if (worker->priv->session_cookie != NULL) {
                gdm_session_worker_set_environment_variable (worker,
                                                             "XDG_SESSION_COOKIE",
                                                             worker->priv->session_cookie);
        }
}
#endif

static gboolean
run_script (GdmSessionWorker *worker,
            const char       *dir)
{
        /* scripts are for non-program sessions only */
        if (worker->priv->is_program_session) {
                return TRUE;
        }

        return gdm_run_script (dir,
                               worker->priv->username,
                               worker->priv->x11_display_name,
                               worker->priv->display_is_local? NULL : worker->priv->hostname,
                               worker->priv->x11_authority_file);
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

#ifdef WITH_CONSOLE_KIT
        close_ck_session (worker);
#endif

        gdm_session_worker_uninitialize_pam (worker, PAM_SUCCESS);

        gdm_dbus_worker_emit_session_exited (GDM_DBUS_WORKER (worker),
                                             worker->priv->service,
                                             status);

        worker->priv->child_pid = -1;
        worker->priv->child_watch_id = 0;
        run_script (worker, GDMCONFDIR "/PostSession");
}

static void
gdm_session_worker_watch_child (GdmSessionWorker *worker)
{
        g_debug ("GdmSession worker: watching pid %d", worker->priv->child_pid);
        worker->priv->child_watch_id = g_child_watch_add (worker->priv->child_pid,
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
                char *name_n;
                char *name_n1;

                name_n = g_strdup_printf ("%s.%d", path, i);
                if (i > 1) {
                        name_n1 = g_strdup_printf ("%s.%d", path, i - 1);
                } else {
                        name_n1 = g_strdup (path);
                }

                g_unlink (name_n);
                g_rename (name_n1, name_n);

                g_free (name_n1);
                g_free (name_n);
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
                char *temp_name;

                temp_name = g_strdup_printf ("%s.XXXXXXXX", filename);

                fd = g_mkstemp (temp_name);

                if (fd < 0) {
                        g_free (temp_name);
                        goto out;
                }

                g_warning ("session log '%s' is not appendable, logging session to '%s' instead.\n", filename,
                           temp_name);
                g_free (temp_name);
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
        char *filename;

        filename = g_build_filename (dir, GDM_SESSION_LOG_FILENAME, NULL);

        if (g_access (dir, R_OK | W_OK | X_OK) == 0 && _is_loggable_file (filename)) {
                char *filename_old;

                filename_old = g_strdup_printf ("%s.old", filename);
                g_rename (filename, filename_old);
                g_free (filename_old);
        }

        fd = g_open (filename, O_RDWR | O_APPEND | O_CREAT, 0600);

        if (fd < 0) {
                char *temp_name;

                temp_name = g_strdup_printf ("%s.XXXXXXXX", filename);

                fd = g_mkstemp (temp_name);

                if (fd < 0) {
                        g_free (temp_name);
                        goto out;
                }

                g_warning ("session log '%s' is not appendable, logging session to '%s' instead.\n", filename,
                           temp_name);
                g_free (filename);
                filename = temp_name;
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
        g_free (filename);

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

        gdm_get_pwent_for_name (worker->priv->username, &passwd_entry);
        if (worker->priv->is_program_session) {
                g_debug ("GdmSessionWorker: opening session for program '%s'",
                         worker->priv->arguments[0]);
        } else {
                g_debug ("GdmSessionWorker: opening user session with program '%s'",
                         worker->priv->arguments[0]);
        }

        error_code = PAM_SUCCESS;

#ifdef WITH_SYSTEMD
        /* If we're in new vt mode, jump to the new vt now. There's no need to jump for
         * the other two modes: in the logind case, the session will activate itself when
         * ready, and in the reuse server case, we're already on the correct VT. */
        if (worker->priv->display_mode == GDM_SESSION_DISPLAY_MODE_NEW_VT) {
                jump_to_vt (worker, worker->priv->session_vt);
        }
#endif

        session_pid = fork ();

        if (session_pid < 0) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                             "%s", g_strerror (errno));
                error_code = PAM_ABORT;
                goto out;
        }

        if (session_pid == 0) {
                const char * const * environment;
                char  *home_dir;
                int    stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
                gboolean has_journald = FALSE, needs_controlling_terminal = FALSE;
                sigset_t mask;

                /* Leak the TTY into the session as stdin so that it stays open
                 * without any races. */
                if (worker->priv->session_tty_fd > 0) {
                        dup2 (worker->priv->session_tty_fd, STDIN_FILENO);
                        close (worker->priv->session_tty_fd);
                        worker->priv->session_tty_fd = -1;
                        needs_controlling_terminal = TRUE;
                } else {
                        stdin_fd = open ("/dev/null", O_RDWR);
                        dup2 (stdin_fd, STDIN_FILENO);
                        close (stdin_fd);
                }

#ifdef ENABLE_SYSTEMD_JOURNAL
                has_journald = sd_booted() > 0;
#endif
                if (!has_journald && worker->priv->is_program_session) {
                        stdout_fd = _open_program_session_log (worker->priv->log_file);
                        stderr_fd = dup (stdout_fd);
                }

                if (setsid () < 0) {
                        g_debug ("GdmSessionWorker: could not set pid '%u' as leader of new session and process group: %s",
                                 (guint) getpid (), g_strerror (errno));
                        _exit (2);
                }

#ifdef WITH_SYSTEMD
                /* Take control of the tty
                 */
                if (needs_controlling_terminal) {
                        if (ioctl (STDIN_FILENO, TIOCSCTTY, 0) < 0) {
                                g_debug ("GdmSessionWorker: could not take control of tty: %m");
                        }
                }
#endif

#ifdef HAVE_LOGINCAP
                if (setusercontext (NULL, passwd_entry, passwd_entry->pw_uid, LOGIN_SETALL) < 0) {
                        g_debug ("GdmSessionWorker: setusercontext() failed for user %s: %s",
                                 passwd_entry->pw_name, g_strerror (errno));
                        _exit (1);
                }
#else
                if (setuid (worker->priv->uid) < 0) {
                        g_debug ("GdmSessionWorker: could not reset uid: %s", g_strerror (errno));
                        _exit (1);
                }
#endif

                environment = gdm_session_worker_get_environment (worker);

                g_assert (geteuid () == getuid ());

                home_dir = gdm_session_worker_get_environment_variable (worker, "HOME");
                if ((home_dir == NULL) || g_chdir (home_dir) < 0) {
                        g_chdir ("/");
                }

#ifdef ENABLE_SYSTEMD_JOURNAL
                if (has_journald) {
                        stdout_fd = sd_journal_stream_fd (worker->priv->arguments[0], LOG_INFO, FALSE);
                        stderr_fd = sd_journal_stream_fd (worker->priv->arguments[0], LOG_WARNING, FALSE);

                        /* Unset the CLOEXEC flags, because sd_journal_stream_fd
                         * gives it to us by default.
                         */
                        gdm_clear_close_on_exec_flag (stdout_fd);
                        gdm_clear_close_on_exec_flag (stderr_fd);
                }
#endif
                if (!has_journald && !worker->priv->is_program_session) {
                        if (home_dir != NULL && home_dir[0] != '\0') {
                                char *cache_dir;
                                char *log_dir;

                                cache_dir = gdm_session_worker_get_environment_variable (worker, "XDG_CACHE_HOME");
                                if (cache_dir == NULL || cache_dir[0] == '\0') {
                                        cache_dir = g_build_filename (home_dir, ".cache", NULL);
                                }

                                log_dir = g_build_filename (cache_dir, "gdm", NULL);
                                g_free (cache_dir);

                                if (g_mkdir_with_parents (log_dir, S_IRWXU) == 0) {
                                        stdout_fd = _open_user_session_log (log_dir);
                                        stderr_fd = dup (stdout_fd);
                                } else {
                                        stdout_fd = open ("/dev/null", O_RDWR);
                                        stderr_fd = dup (stdout_fd);
                                }
                                g_free (log_dir);
                        } else {
                                stdout_fd = open ("/dev/null", O_RDWR);
                                stderr_fd = dup (stdout_fd);
                        }
                }
                g_free (home_dir);

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

                /*
                 * Reset SIGUSR1 to default since it was blocked by the manager
                 * process for the X server startup handshake
                 */
                signal (SIGUSR1, SIG_DFL);

                /*
                 * Reset signal mask to default since it was altered by the
                 * manager process
                 */
                sigemptyset (&mask);
                sigprocmask (SIG_SETMASK, &mask, NULL);

                gdm_session_execute (worker->priv->arguments[0],
                                     worker->priv->arguments,
                                     (char **)
                                     environment,
                                     TRUE);

                gdm_log_init ();
                g_debug ("GdmSessionWorker: child '%s' could not be started: %s",
                         worker->priv->arguments[0],
                         g_strerror (errno));

                _exit (127);
        }

        if (worker->priv->session_tty_fd > 0) {
                close (worker->priv->session_tty_fd);
                worker->priv->session_tty_fd = -1;
        }

        /* If we end up execing again, make sure we don't use the executable context set up
         * by pam_selinux durin pam_open_session
         */
#ifdef HAVE_SELINUX
        setexeccon (NULL);
#endif

        worker->priv->child_pid = session_pid;

        g_debug ("GdmSessionWorker: session opened creating reply...");
        g_assert (sizeof (GPid) <= sizeof (int));

        g_debug ("GdmSessionWorker: state SESSION_STARTED");
        worker->priv->state = GDM_SESSION_WORKER_STATE_SESSION_STARTED;

        gdm_session_worker_watch_child (worker);

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        return TRUE;
}

#ifdef WITH_SYSTEMD
static gboolean
set_up_for_new_vt (GdmSessionWorker *worker)
{
        int fd;
        char vt_string[256], tty_string[256];
        struct vt_stat vt_state = { 0 };
        int session_vt = 0;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

        if (fd < 0) {
                g_debug ("GdmSessionWorker: couldn't open VT master: %m");
                return FALSE;
        }

        if (ioctl (fd, VT_GETSTATE, &vt_state) < 0) {
                g_debug ("GdmSessionWorker: couldn't get current VT: %m");
                goto fail;
        }

        if (worker->priv->display_is_initial) {
                session_vt = atoi (GDM_INITIAL_VT);
        } else {
                if (ioctl(fd, VT_OPENQRY, &session_vt) < 0) {
                        g_debug ("GdmSessionWorker: couldn't open new VT: %m");
                        goto fail;
                }
        }

        worker->priv->login_vt = vt_state.v_active;
        worker->priv->session_vt = session_vt;

        close (fd);
        fd = -1;

        g_assert (session_vt > 0);

        g_snprintf (vt_string, sizeof (vt_string), "%d", session_vt);

        /* Set the VTNR. This is used by logind to configure a session in
         * the logind-managed case, but it doesn't hurt to set it always.
         * When logind gains support for XDG_VTNR=auto, we can make the
         * OPENQRY and this whole path only used by the new VT code. */
        gdm_session_worker_set_environment_variable (worker,
                                                     "XDG_VTNR",
                                                     vt_string);

        g_snprintf (tty_string, 256, "/dev/tty%d", session_vt);
        worker->priv->session_tty_fd = open (tty_string, O_RDWR | O_NOCTTY);
        pam_set_item (worker->priv->pam_handle, PAM_TTY, tty_string);

        return TRUE;

fail:
        close (fd);
        return FALSE;
}
#endif

static gboolean
set_up_for_current_vt (GdmSessionWorker  *worker,
                       GError           **error)
{
#ifdef PAM_XAUTHDATA
        struct pam_xauth_data *pam_xauth;
#endif
        int                    error_code = PAM_SUCCESS;
        char                  *pam_tty;

        /* set TTY */
        pam_tty = _get_tty_for_pam (worker->priv->x11_display_name, worker->priv->display_device);
        if (pam_tty != NULL && pam_tty[0] != '\0') {
                error_code = pam_set_item (worker->priv->pam_handle, PAM_TTY, pam_tty);

                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of user's console %s: %s",
                                 pam_tty,
                                 pam_strerror (worker->priv->pam_handle, error_code));
                        g_free (pam_tty);
                        g_set_error (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     "%s", "");
                        goto out;
                }
        }
        g_free (pam_tty);

#ifdef PAM_XDISPLAY
        /* set XDISPLAY */
        if (worker->priv->x11_display_name != NULL && worker->priv->x11_display_name[0] != '\0') {
                error_code = pam_set_item (worker->priv->pam_handle, PAM_XDISPLAY, worker->priv->x11_display_name);
                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of display string %s: %s",
                                 worker->priv->x11_display_name,
                                 pam_strerror (worker->priv->pam_handle, error_code));
                        g_set_error (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     "%s", "");
                        goto out;
                }
        }
#endif
#ifdef PAM_XAUTHDATA
        /* set XAUTHDATA */
        pam_xauth = _get_xauth_for_pam (worker->priv->x11_authority_file);
        if (pam_xauth != NULL) {
                error_code = pam_set_item (worker->priv->pam_handle, PAM_XAUTHDATA, pam_xauth);
                if (error_code != PAM_SUCCESS) {
                        g_debug ("error informing authentication system of display string %s: %s",
                                 worker->priv->x11_display_name,
                                 pam_strerror (worker->priv->pam_handle, error_code));
                        g_free (pam_xauth);

                        g_set_error (error,
                                     GDM_SESSION_WORKER_ERROR,
                                     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
                                     "%s", "");
                        goto out;
                }
                g_free (pam_xauth);
         }
#endif
        return TRUE;
out:
        return FALSE;
}

static gboolean
gdm_session_worker_open_session (GdmSessionWorker  *worker,
                                 GError           **error)
{
        int error_code;
        int flags;
        char *session_id = NULL;

        g_assert (worker->priv->state == GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED);
        g_assert (geteuid () == 0);

        switch (worker->priv->display_mode) {
        case GDM_SESSION_DISPLAY_MODE_REUSE_VT:
                if (!set_up_for_current_vt (worker, error)) {
                        return FALSE;
                }
                break;
#ifdef WITH_SYSTEMD
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
#endif
        }

        flags = 0;

        if (worker->priv->is_program_session) {
                flags |= PAM_SILENT;
        }

        if (!run_script (worker, GDMCONFDIR "/PostLogin")) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                             "Failed to execute PostLogin script");
                return FALSE;
        }

        error_code = pam_open_session (worker->priv->pam_handle, flags);

        if (error_code != PAM_SUCCESS) {
                g_set_error (error,
                             GDM_SESSION_WORKER_ERROR,
                             GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
                             "%s", pam_strerror (worker->priv->pam_handle, error_code));
                goto out;
        }

        g_debug ("GdmSessionWorker: state SESSION_OPENED");
        worker->priv->state = GDM_SESSION_WORKER_STATE_SESSION_OPENED;

#ifdef WITH_SYSTEMD
        session_id = gdm_session_worker_get_environment_variable (worker, "XDG_SESSION_ID");
#endif

        /* FIXME: should we do something here?
         * Note that error return status from PreSession script should
         * be ignored in the case of a X-GDM-BypassXsession session, which can
         * be checked by calling:
         * gdm_session_bypasses_xsession (session)
         */
        run_script (worker, GDMCONFDIR "/PreSession");

#ifdef WITH_CONSOLE_KIT
        register_ck_session (worker);

        if (session_id == NULL) {
                session_id = get_ck_session_id (worker);
        }
#endif

        if (session_id != NULL) {
                g_free (worker->priv->session_id);
                worker->priv->session_id = session_id;
        }

 out:
        if (error_code != PAM_SUCCESS) {
                gdm_session_worker_uninitialize_pam (worker, error_code);
                return FALSE;
        }

        gdm_session_worker_get_username (worker, NULL);
        gdm_session_auditor_report_login (worker->priv->auditor);

        return TRUE;
}

static void
gdm_session_worker_set_server_address (GdmSessionWorker *worker,
                                       const char       *address)
{
        g_free (worker->priv->server_address);
        worker->priv->server_address = g_strdup (address);
}

static void
gdm_session_worker_set_is_reauth_session (GdmSessionWorker *worker,
                                          gboolean          is_reauth_session)
{
        worker->priv->is_reauth_session = is_reauth_session;
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
                g_value_set_string (value, self->priv->server_address);
                break;
        case PROP_IS_REAUTH_SESSION:
                g_value_set_boolean (value, self->priv->is_reauth_session);
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
        gdm_session_settings_set_session_name (worker->priv->user_settings,
                                               session_name);
        gdm_dbus_worker_complete_set_session_name (object, invocation);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_set_session_type (GdmDBusWorker         *object,
                                            GDBusMethodInvocation *invocation,
                                            const char            *session_type)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_debug ("GdmSessionWorker: session type set to %s", session_type);
        g_free (worker->priv->session_type);
        worker->priv->session_type = g_strdup (session_type);
        gdm_dbus_worker_complete_set_session_type (object, invocation);
        return TRUE;
}

static gboolean
gdm_session_worker_handle_set_session_display_mode (GdmDBusWorker         *object,
                                                    GDBusMethodInvocation *invocation,
                                                    const char            *str)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        g_debug ("GdmSessionWorker: session display mode set to %s", str);
        worker->priv->display_mode = gdm_session_display_mode_from_string (str);
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
        gdm_session_settings_set_language_name (worker->priv->user_settings,
                                                language_name);
        gdm_dbus_worker_complete_set_language_name (object, invocation);
        return TRUE;
}

static void
on_saved_language_name_read (GdmSessionWorker *worker)
{
        char *language_name;

        language_name = gdm_session_settings_get_language_name (worker->priv->user_settings);

        g_debug ("GdmSessionWorker: Saved language is %s", language_name);
        gdm_dbus_worker_emit_saved_language_name_read (GDM_DBUS_WORKER (worker),
                                                       language_name);
        g_free (language_name);
}

static void
on_saved_session_name_read (GdmSessionWorker *worker)
{
        char *session_name;

        session_name = gdm_session_settings_get_session_name (worker->priv->user_settings);

        g_debug ("GdmSessionWorker: Saved session is %s", session_name);
        gdm_dbus_worker_emit_saved_session_name_read (GDM_DBUS_WORKER (worker),
                                                      session_name);
        g_free (session_name);
}

static void
do_setup (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = gdm_session_worker_initialize_pam (worker,
                                                 worker->priv->service,
                                                 worker->priv->username,
                                                 worker->priv->hostname,
                                                 worker->priv->display_is_local,
                                                 worker->priv->x11_display_name,
                                                 worker->priv->x11_authority_file,
                                                 worker->priv->display_device,
                                                 worker->priv->display_seat_id,
                                                 &error);

        if (res) {
                g_dbus_method_invocation_return_value (worker->priv->pending_invocation, NULL);
        } else {
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
}

static void
do_authenticate (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        /* find out who the user is and ensure they are who they say they are
         */
        error = NULL;
        res = gdm_session_worker_authenticate_user (worker,
                                                    worker->priv->password_is_required,
                                                    &error);
        if (res) {
                /* we're authenticated.  Let's make sure we've been given
                 * a valid username for the system
                 */
                if (!worker->priv->is_program_session) {
                        g_debug ("GdmSessionWorker: trying to get updated username");
                        gdm_session_worker_update_username (worker);
                }

                gdm_dbus_worker_complete_authenticate (GDM_DBUS_WORKER (worker), worker->priv->pending_invocation);
        } else {
                g_debug ("GdmSessionWorker: Unable to verify user");
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
}

static void
do_authorize (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        /* make sure the user is allowed to log in to this system
         */
        error = NULL;
        res = gdm_session_worker_authorize_user (worker,
                                                 worker->priv->password_is_required,
                                                 &error);
        if (res) {
                gdm_dbus_worker_complete_authorize (GDM_DBUS_WORKER (worker), worker->priv->pending_invocation);
        } else {
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
}

static void
do_accredit (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        /* get kerberos tickets, setup group lists, etc
         */
        error = NULL;
        res = gdm_session_worker_accredit_user (worker, &error);

        if (res) {
                gdm_dbus_worker_complete_establish_credentials (GDM_DBUS_WORKER (worker), worker->priv->pending_invocation);
        } else {
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
}

static void
save_account_details_now (GdmSessionWorker *worker)
{
        g_assert (worker->priv->state == GDM_SESSION_WORKER_STATE_ACCREDITED);

        g_debug ("GdmSessionWorker: saving account details for user %s", worker->priv->username);
        worker->priv->state = GDM_SESSION_WORKER_STATE_ACCOUNT_DETAILS_SAVED;
        if (!gdm_session_settings_save (worker->priv->user_settings,
                                        worker->priv->username)) {
                g_warning ("could not save session and language settings");
        }
        queue_state_change (worker);
}

static void
on_settings_is_loaded_changed (GdmSessionSettings *user_settings,
                               GParamSpec         *pspec,
                               GdmSessionWorker   *worker)
{
        if (!gdm_session_settings_is_loaded (worker->priv->user_settings)) {
                return;
        }

        /* These signal handlers should be disconnected after the loading,
         * so that gdm_session_settings_set_* APIs don't cause the emitting
         * of Saved*NameRead D-Bus signals any more.
         */
        g_signal_handlers_disconnect_by_func (worker->priv->user_settings,
                                              G_CALLBACK (on_saved_session_name_read),
                                              worker);

        g_signal_handlers_disconnect_by_func (worker->priv->user_settings,
                                              G_CALLBACK (on_saved_language_name_read),
                                              worker);

        if (worker->priv->state == GDM_SESSION_WORKER_STATE_NONE) {
                g_debug ("GdmSessionWorker: queuing setup for user: %s %s",
                         worker->priv->username, worker->priv->display_device);
                queue_state_change (worker);
        } else if (worker->priv->state == GDM_SESSION_WORKER_STATE_ACCREDITED) {
                save_account_details_now (worker);
        } else {
                return;
        }

        g_signal_handlers_disconnect_by_func (G_OBJECT (worker->priv->user_settings),
                                              G_CALLBACK (on_settings_is_loaded_changed),
                                              worker);
}

static void
do_save_account_details_when_ready (GdmSessionWorker *worker)
{
        g_assert (worker->priv->state == GDM_SESSION_WORKER_STATE_ACCREDITED);

        if (!gdm_session_settings_is_loaded (worker->priv->user_settings)) {
                g_signal_connect (G_OBJECT (worker->priv->user_settings),
                                  "notify::is-loaded",
                                  G_CALLBACK (on_settings_is_loaded_changed),
                                  worker);
                g_debug ("GdmSessionWorker: user %s, not fully loaded yet, will save account details later",
                         worker->priv->username);
                gdm_session_settings_load (worker->priv->user_settings,
                                           worker->priv->username);
                return;
        }

        save_account_details_now (worker);
}

static void
do_open_session (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = gdm_session_worker_open_session (worker, &error);

        if (res) {
                char *session_id = worker->priv->session_id;
                if (session_id == NULL) {
                        session_id = "";
                }

                gdm_dbus_worker_complete_open (GDM_DBUS_WORKER (worker), worker->priv->pending_invocation, session_id);
        } else {
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
}

static void
do_start_session (GdmSessionWorker *worker)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = gdm_session_worker_start_session (worker, &error);
        if (res) {
                gdm_dbus_worker_complete_start_program (GDM_DBUS_WORKER (worker),
                                                        worker->priv->pending_invocation,
                                                        worker->priv->child_pid);
        } else {
                g_dbus_method_invocation_take_error (worker->priv->pending_invocation, error);
        }
        worker->priv->pending_invocation = NULL;
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

        new_state = worker->priv->state + 1;
        g_debug ("GdmSessionWorker: attempting to change state to %s",
                 get_state_name (new_state));

        worker->priv->state_change_idle_id = 0;

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
        if (worker->priv->state_change_idle_id > 0) {
                return;
        }

        worker->priv->state_change_idle_id = g_idle_add ((GSourceFunc)state_change_idle, worker);
}

static gboolean
validate_state_change (GdmSessionWorker      *worker,
                       GDBusMethodInvocation *invocation,
                       int                    new_state)
{
        if (worker->priv->pending_invocation != NULL) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_OUTSTANDING_REQUEST,
                                                       "Cannot process state change to %s, as there is already an outstanding request to move to state %s",
                                                       get_state_name (new_state),
                                                       get_state_name (worker->priv->state + 1));
                return FALSE;
        } else if (worker->priv->state != new_state - 1) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_WRONG_STATE,
                                                       "Cannot move to state %s, in state %s, not %s",
                                                       get_state_name (new_state),
                                                       get_state_name (worker->priv->state),
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
                worker->priv->pending_invocation = invocation;
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

        if (!worker->priv->is_reauth_session) {
                worker->priv->cred_flags = PAM_ESTABLISH_CRED;
        } else {
                worker->priv->cred_flags = PAM_REINITIALIZE_CRED;
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

        worker->priv->service = g_strdup (service);
        worker->priv->x11_display_name = g_strdup (x11_display_name);
        worker->priv->x11_authority_file = g_strdup (x11_authority_file);
        worker->priv->display_device = g_strdup (console);
        worker->priv->display_seat_id = g_strdup (seat_id);
        worker->priv->hostname = g_strdup (hostname);
        worker->priv->display_is_local = display_is_local;
        worker->priv->display_is_initial = display_is_initial;
        worker->priv->username = NULL;

        g_signal_connect_swapped (worker->priv->user_settings,
                                  "notify::language-name",
                                  G_CALLBACK (on_saved_language_name_read),
                                  worker);

        g_signal_connect_swapped (worker->priv->user_settings,
                                  "notify::session-name",
                                  G_CALLBACK (on_saved_session_name_read),
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

        worker->priv->service = g_strdup (service);
        worker->priv->x11_display_name = g_strdup (x11_display_name);
        worker->priv->x11_authority_file = g_strdup (x11_authority_file);
        worker->priv->display_device = g_strdup (console);
        worker->priv->display_seat_id = g_strdup (seat_id);
        worker->priv->hostname = g_strdup (hostname);
        worker->priv->display_is_local = display_is_local;
        worker->priv->display_is_initial = display_is_initial;
        worker->priv->username = g_strdup (username);

        g_signal_connect_swapped (worker->priv->user_settings,
                                  "notify::language-name",
                                  G_CALLBACK (on_saved_language_name_read),
                                  worker);

        g_signal_connect_swapped (worker->priv->user_settings,
                                  "notify::session-name",
                                  G_CALLBACK (on_saved_session_name_read),
                                  worker);

        /* Load settings from accounts daemon before continuing
         * FIXME: need to handle username not loading
         */
        worker->priv->pending_invocation = invocation;
        if (gdm_session_settings_load (worker->priv->user_settings, username)) {
                queue_state_change (worker);
        } else {
                g_signal_connect (G_OBJECT (worker->priv->user_settings),
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

        worker->priv->service = g_strdup (service);
        worker->priv->x11_display_name = g_strdup (x11_display_name);
        worker->priv->x11_authority_file = g_strdup (x11_authority_file);
        worker->priv->display_device = g_strdup (console);
        worker->priv->display_seat_id = g_strdup (seat_id);
        worker->priv->hostname = g_strdup (hostname);
        worker->priv->display_is_local = display_is_local;
        worker->priv->display_is_initial = display_is_initial;
        worker->priv->username = g_strdup (username);
        worker->priv->log_file = g_strdup (log_file);
        worker->priv->is_program_session = TRUE;

        return TRUE;
}

static gboolean
gdm_session_worker_handle_start_program (GdmDBusWorker         *object,
                                         GDBusMethodInvocation *invocation,
                                         const char            *text)
{
        GdmSessionWorker *worker = GDM_SESSION_WORKER (object);
        GError *parse_error = NULL;
        validate_state_change (worker, invocation, GDM_SESSION_WORKER_STATE_SESSION_STARTED);

        if (worker->priv->is_reauth_session) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_IN_REAUTH_SESSION,
                                                       "Cannot start a program while in a reauth session");
                return TRUE;
        }

        g_debug ("GdmSessionWorker: start program: %s", text);

        g_clear_pointer (&worker->priv->arguments, (GDestroyNotify) g_strfreev);
        if (! g_shell_parse_argv (text, NULL, &worker->priv->arguments, &parse_error)) {
                g_dbus_method_invocation_take_error (invocation, parse_error);
                return TRUE;
        }

        worker->priv->pending_invocation = invocation;
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
        g_hash_table_remove (worker->priv->reauthentication_requests,
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

        gdm_dbus_worker_emit_reauthenticated (GDM_DBUS_WORKER (worker), service_name);
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
                                            worker->priv->x11_display_name,
                                            worker->priv->hostname,
                                            worker->priv->display_device,
                                            worker->priv->display_seat_id,
                                            worker->priv->x11_authority_file,
                                            worker->priv->display_is_local,
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

        if (worker->priv->state != GDM_SESSION_WORKER_STATE_SESSION_STARTED) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GDM_SESSION_WORKER_ERROR,
                                                       GDM_SESSION_WORKER_ERROR_WRONG_STATE,
                                                       "Cannot reauthenticate while in state %s",
                                                       get_state_name (worker->priv->state));
                return TRUE;
        }

        g_debug ("GdmSessionWorker: start reauthentication");

        request = reauthentication_request_new (worker, pid_of_caller, uid_of_caller, invocation);
        g_hash_table_replace (worker->priv->reauthentication_requests,
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
        GError            *error;

        worker = GDM_SESSION_WORKER (G_OBJECT_CLASS (gdm_session_worker_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

        g_debug ("GdmSessionWorker: connecting to address: %s", worker->priv->server_address);

        error = NULL;
        worker->priv->connection = g_dbus_connection_new_for_address_sync (worker->priv->server_address,
                                                                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                           NULL,
                                                                           NULL,
                                                                           &error);
        if (worker->priv->connection == NULL) {
                g_warning ("error opening connection: %s", error->message);
                g_clear_error (&error);

                exit (1);
        }

        worker->priv->manager = GDM_DBUS_WORKER_MANAGER (gdm_dbus_worker_manager_proxy_new_sync (worker->priv->connection,
                                                                                                 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                                                                 NULL, /* dbus name */
                                                                                                 GDM_SESSION_DBUS_PATH,
                                                                                                 NULL,
                                                                                                 &error));
        if (worker->priv->manager == NULL) {
                g_warning ("error creating session proxy: %s", error->message);
                g_clear_error (&error);

                exit (1);
        }

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (worker),
                                               worker->priv->connection,
                                               GDM_WORKER_DBUS_PATH,
                                               &error)) {
                g_warning ("Error while exporting object: %s", error->message);
                exit (1);
        }

        g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (worker->priv->manager), G_MAXINT);

        /* Send an initial Hello message so that the session can associate
         * the conversation we manage with our pid.
         */
        gdm_dbus_worker_manager_call_hello_sync (worker->priv->manager,
                                                 NULL,
                                                 NULL);

        return G_OBJECT (worker);
}

static void
worker_interface_init (GdmDBusWorkerIface *interface)
{
        interface->handle_setup = gdm_session_worker_handle_setup;
        interface->handle_setup_for_user = gdm_session_worker_handle_setup_for_user;
        interface->handle_setup_for_program = gdm_session_worker_handle_setup_for_program;
        interface->handle_authenticate = gdm_session_worker_handle_authenticate;
        interface->handle_authorize = gdm_session_worker_handle_authorize;
        interface->handle_establish_credentials = gdm_session_worker_handle_establish_credentials;
        interface->handle_open = gdm_session_worker_handle_open;
        interface->handle_set_language_name = gdm_session_worker_handle_set_language_name;
        interface->handle_set_session_name = gdm_session_worker_handle_set_session_name;
        interface->handle_set_session_type = gdm_session_worker_handle_set_session_type;
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

        g_type_class_add_private (klass, sizeof (GdmSessionWorkerPrivate));

        g_object_class_install_property (object_class,
                                         PROP_SERVER_ADDRESS,
                                         g_param_spec_string ("server-address",
                                                              "server address",
                                                              "server address",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_IS_REAUTH_SESSION,
                                         g_param_spec_boolean ("is-reauth-session",
                                                               "is reauth session",
                                                               "is reauth session",
                                                              FALSE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
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
        worker->priv = GDM_SESSION_WORKER_GET_PRIVATE (worker);

        worker->priv->user_settings = gdm_session_settings_new ();
        worker->priv->reauthentication_requests = g_hash_table_new_full (NULL,
                                                                         NULL,
                                                                         NULL,
                                                                         (GDestroyNotify)
                                                                         reauthentication_request_free);
}

static void
gdm_session_worker_unwatch_child (GdmSessionWorker *worker)
{
        if (worker->priv->child_watch_id == 0)
                return;

        g_source_remove (worker->priv->child_watch_id);
        worker->priv->child_watch_id = 0;
}


static void
gdm_session_worker_finalize (GObject *object)
{
        GdmSessionWorker *worker;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_WORKER (object));

        worker = GDM_SESSION_WORKER (object);

        g_return_if_fail (worker->priv != NULL);

        gdm_session_worker_unwatch_child (worker);

        g_object_unref (worker->priv->user_settings);
        g_free (worker->priv->service);
        g_free (worker->priv->x11_display_name);
        g_free (worker->priv->x11_authority_file);
        g_free (worker->priv->display_device);
        g_free (worker->priv->display_seat_id);
        g_free (worker->priv->hostname);
        g_free (worker->priv->username);
        g_free (worker->priv->server_address);
        g_strfreev (worker->priv->arguments);

        g_hash_table_unref (worker->priv->reauthentication_requests);

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
