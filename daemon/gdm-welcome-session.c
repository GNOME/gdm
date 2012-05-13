/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
#include <signal.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"

#include "gdm-session-direct.h"
#include "gdm-welcome-session.h"

#define DBUS_LAUNCH_COMMAND BINDIR "/dbus-launch"

#define MAX_LOGS 5

extern char **environ;

#define GDM_WELCOME_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_WELCOME_SESSION, GdmWelcomeSessionPrivate))

struct GdmWelcomeSessionPrivate
{
        GdmSessionDirect *session;
        char           *command;
        GPid            pid;

        char           *user_name;
        char           *group_name;
        char           *runtime_dir;

        char           *x11_display_name;
        char           *x11_display_seat_id;
        char           *x11_display_device;
        char           *x11_display_hostname;
        char           *x11_authority_file;
        gboolean        x11_display_is_local;

        GPid            dbus_pid;
        char           *dbus_bus_address;
        char           *server_dbus_path;
        char           *server_dbus_interface;
        char           *server_env_var_name;

        char           *server_address;
};

enum {
        PROP_0,
        PROP_X11_DISPLAY_NAME,
        PROP_X11_DISPLAY_SEAT_ID,
        PROP_X11_DISPLAY_DEVICE,
        PROP_X11_DISPLAY_HOSTNAME,
        PROP_X11_AUTHORITY_FILE,
        PROP_X11_DISPLAY_IS_LOCAL,
        PROP_USER_NAME,
        PROP_GROUP_NAME,
        PROP_RUNTIME_DIR,
        PROP_SERVER_ADDRESS,
        PROP_COMMAND,
        PROP_SERVER_DBUS_PATH,
        PROP_SERVER_DBUS_INTERFACE,
        PROP_SERVER_ENV_VAR_NAME
};

enum {
        STARTED,
        STOPPED,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_welcome_session_class_init    (GdmWelcomeSessionClass *klass);
static void     gdm_welcome_session_init          (GdmWelcomeSession      *welcome_session);
static void     gdm_welcome_session_finalize      (GObject                *object);

G_DEFINE_ABSTRACT_TYPE (GdmWelcomeSession, gdm_welcome_session, G_TYPE_OBJECT)

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("GdmWelcomeSession: welcome environment: %s", str);
        g_ptr_array_add (env, str);
}

static void
load_lang_config_file (const char  *config_file,
                       const char **str_array)
{
        gchar         *contents = NULL;
        gchar         *p;
        gchar         *str_joinv;
        gchar         *pattern;
        gchar         *key;
        gchar         *value;
        gsize          length;
        GError        *error;
        GString       *line;
        GRegex        *re;

        g_return_if_fail (config_file != NULL);
        g_return_if_fail (str_array != NULL);

        if (!g_file_test (config_file, G_FILE_TEST_EXISTS)) {
                g_debug ("Cannot access '%s'", config_file);
                return;
        }

        error = NULL;
        if (!g_file_get_contents (config_file, &contents, &length, &error)) {
                g_debug ("Failed to parse '%s': %s",
                         config_file,
                         (error && error->message) ? error->message : "(null)");
                g_error_free (error);
                return;
        }

        if (!g_utf8_validate (contents, length, NULL)) {
                g_warning ("Invalid UTF-8 in '%s'", config_file);
                g_free (contents);
                return;
        }

        str_joinv = g_strjoinv ("|", (char **) str_array);
        if (str_joinv == NULL) {
                g_warning ("Error in joined");
                g_free (contents);
                return;
        }

        pattern = g_strdup_printf ("(?P<key>(%s))=(\")?(?P<value>[^\"]*)?(\")?",
                                   str_joinv);
        error = NULL;
        re = g_regex_new (pattern, 0, 0, &error);
        g_free (pattern);
        g_free (str_joinv);
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
                } else if (key && *key) {
                        g_unsetenv (key);
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

static GHashTable *
build_welcome_environment (GdmWelcomeSession *welcome_session,
                           gboolean           start_session)
{
        GHashTable    *hash;
        struct passwd *pwent;
        static const char * const optional_environment[] = {
                "LANG", "LANGUAGE", "LC_CTYPE", "LC_NUMERIC", "LC_TIME",
                "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER",
                "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT",
                "LC_IDENTIFICATION", "LC_ALL", "WINDOWPATH",
                NULL
        };
        char *system_data_dirs;
        int i;

        load_lang_config_file (LANG_CONFIG_FILE,
                               (const char **) optional_environment);

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        for (i = 0; optional_environment[i] != NULL; i++) {
                if (g_getenv (optional_environment[i]) == NULL) {
                        continue;
                }

                g_hash_table_insert (hash,
                                     g_strdup (optional_environment[i]),
                                     g_strdup (g_getenv (optional_environment[i])));
        }

        system_data_dirs = g_strjoinv (":", (char **) g_get_system_data_dirs ());

        g_hash_table_insert (hash,
                             g_strdup ("XDG_DATA_DIRS"),
                             g_strdup_printf ("%s:%s",
                                              DATADIR "/gdm/greeter",
                                              system_data_dirs));
        g_free (system_data_dirs);

        if (welcome_session->priv->dbus_bus_address != NULL) {
                g_hash_table_insert (hash,
                                     g_strdup ("DBUS_SESSION_BUS_ADDRESS"),
                                     g_strdup (welcome_session->priv->dbus_bus_address));
        }
        if (welcome_session->priv->server_address != NULL) {
                g_assert (welcome_session->priv->server_env_var_name != NULL);
                g_hash_table_insert (hash, g_strdup (welcome_session->priv->server_env_var_name),
                                     g_strdup (welcome_session->priv->server_address));
        }

        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (welcome_session->priv->x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (welcome_session->priv->x11_display_name));

        g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (welcome_session->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (welcome_session->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (welcome_session->priv->user_name));

        g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
        g_hash_table_remove (hash, "MAIL");

        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        gdm_get_pwent_for_name (welcome_session->priv->user_name, &pwent);
        if (pwent != NULL) {
                if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
                        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
                }

                g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
        }

        if (start_session && welcome_session->priv->x11_display_seat_id != NULL) {
                char *seat_id;

                seat_id = welcome_session->priv->x11_display_seat_id;
                if (g_str_has_prefix (seat_id, "/org/freedesktop/ConsoleKit/")) {
                        seat_id += strlen ("/org/freedesktop/ConsoleKit/");
                }

                g_hash_table_insert (hash, g_strdup ("GDM_SEAT_ID"), g_strdup (seat_id));
        }

        g_hash_table_insert (hash, g_strdup ("PATH"), g_strdup (g_getenv ("PATH")));

        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));
        g_hash_table_insert (hash, g_strdup ("GVFS_DISABLE_FUSE"), g_strdup ("1"));
        g_hash_table_insert (hash, g_strdup ("DCONF_PROFILE"), g_strdup ("gdm"));

        return hash;
}

static GPtrArray *
get_welcome_environment (GdmWelcomeSession *welcome_session,
                         gboolean           start_session)
{
        GHashTable    *hash;
        GPtrArray     *env;

        hash = build_welcome_environment (welcome_session, start_session);

        env = g_ptr_array_new ();
        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

static gboolean
stop_dbus_daemon (GdmWelcomeSession *welcome_session)
{
        int res;

        if (welcome_session->priv->dbus_pid > 0) {
                g_debug ("GdmWelcomeSession: Stopping D-Bus daemon");
                res = gdm_signal_pid (-1 * welcome_session->priv->dbus_pid, SIGTERM);
                if (res < 0) {
                        g_warning ("Unable to kill D-Bus daemon");
                } else {
                        welcome_session->priv->dbus_pid = 0;
                }
        }
        return TRUE;
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

                VE_IGNORE_EINTR (g_unlink (name_n));
                VE_IGNORE_EINTR (g_rename (name_n1, name_n));

                g_free (name_n1);
                g_free (name_n);
        }

        VE_IGNORE_EINTR (g_unlink (path));
}

typedef struct {
        const char *user_name;
        const char *group_name;
        const char *runtime_dir;
        const char *log_file;
        const char *seat_id;
} SpawnChildData;

static void
spawn_child_setup (SpawnChildData *data)
{
        struct passwd *pwent;
        struct group  *grent;
        int            res;

        if (data->user_name == NULL) {
                return;
        }

        gdm_get_pwent_for_name (data->user_name, &pwent);
        if (pwent == NULL) {
                g_warning (_("User %s doesn't exist"),
                           data->user_name);
                _exit (1);
        }

        grent = getgrnam (data->group_name);
        if (grent == NULL) {
                g_warning (_("Group %s doesn't exist"),
                           data->group_name);
                _exit (1);
        }

        g_debug ("GdmWelcomeSession: Setting up run time dir %s", data->runtime_dir);
        g_mkdir (data->runtime_dir, 0755);
        res = chown (data->runtime_dir, pwent->pw_uid, pwent->pw_gid);
        if (res == -1) {
                g_warning ("GdmWelcomeSession: Error setting owner of run time directory: %s",
                           g_strerror (errno));
        }

        g_debug ("GdmWelcomeSession: Changing (uid:gid) for child process to (%d:%d)",
                 pwent->pw_uid,
                 grent->gr_gid);

        if (pwent->pw_uid != 0) {
                if (setgid (grent->gr_gid) < 0)  {
                        g_warning (_("Couldn't set groupid to %d"),
                                   grent->gr_gid);
                        _exit (1);
                }

                if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
                        g_warning (_("initgroups () failed for %s"),
                                   pwent->pw_name);
                        _exit (1);
                }

                if (setuid (pwent->pw_uid) < 0)  {
                        g_warning (_("Couldn't set userid to %d"),
                                   (int)pwent->pw_uid);
                        _exit (1);
                }
        } else {
                gid_t groups[1] = { 0 };

                if (setgid (0) < 0)  {
                        g_warning (_("Couldn't set groupid to %d"), 0);
                        /* Don't error out, it's not fatal, if it fails we'll
                         * just still be */
                }

                /* this will get rid of any suplementary groups etc... */
                setgroups (1, groups);
        }

        if (setsid () < 0) {
                g_debug ("GdmWelcomeSession: could not set pid '%u' as leader of new session and process group - %s",
                         (guint) getpid (), g_strerror (errno));
                _exit (2);
        }

        /* Terminate the process when the parent dies */
#ifdef HAVE_SYS_PRCTL_H
        prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif

        if (data->log_file != NULL) {
                int logfd;

                rotate_logs (data->log_file, MAX_LOGS);

                VE_IGNORE_EINTR (g_unlink (data->log_file));
                VE_IGNORE_EINTR (logfd = open (data->log_file, O_CREAT|O_APPEND|O_TRUNC|O_WRONLY|O_EXCL, 0644));

                if (logfd != -1) {
                        VE_IGNORE_EINTR (dup2 (logfd, 1));
                        VE_IGNORE_EINTR (dup2 (logfd, 2));
                        close (logfd);
                }
        }
}

static gboolean
spawn_command_line_sync_as_user (const char *command_line,
                                 const char *user_name,
                                 const char *group_name,
                                 const char *seat_id,
                                 const char *runtime_dir,
                                 const char *log_file,
                                 char       **env,
                                 char       **std_output,
                                 char       **std_error,
                                 int         *exit_status,
                                 GError     **error)
{
        char           **argv;
        GError          *local_error;
        gboolean         ret;
        gboolean         res;
        SpawnChildData   data;

        ret = FALSE;

        argv = NULL;
        local_error = NULL;
        if (! g_shell_parse_argv (command_line, NULL, &argv, &local_error)) {
                g_warning ("Could not parse command: %s", local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        data.user_name = user_name;
        data.group_name = group_name;
        data.runtime_dir = runtime_dir;
        data.log_file = log_file;
        data.seat_id = seat_id;

        local_error = NULL;
        res = g_spawn_sync (NULL,
                            argv,
                            env,
                            G_SPAWN_SEARCH_PATH,
                            (GSpawnChildSetupFunc)spawn_child_setup,
                            &data,
                            std_output,
                            std_error,
                            exit_status,
                            &local_error);

        if (! res) {
                g_warning ("Could not spawn command: %s", local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        ret = TRUE;
 out:
        g_strfreev (argv);

        return ret;
}

static gboolean
parse_value_as_integer (const char *value,
                        int        *intval)
{
        char *end_of_valid_int;
        glong long_value;
        gint  int_value;

        errno = 0;
        long_value = strtol (value, &end_of_valid_int, 10);

        if (*value == '\0' || *end_of_valid_int != '\0') {
                return FALSE;
        }

        int_value = long_value;
        if (int_value != long_value || errno == ERANGE) {
                return FALSE;
        }

        *intval = int_value;

        return TRUE;
}

static gboolean
parse_dbus_launch_output (const char *output,
                          char      **addressp,
                          GPid       *pidp)
{
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    ret;
        gboolean    res;
        GError     *error;

        ret = FALSE;

        error = NULL;
        re = g_regex_new ("DBUS_SESSION_BUS_ADDRESS=(.+)\nDBUS_SESSION_BUS_PID=([0-9]+)", 0, 0, &error);
        if (re == NULL) {
                g_critical ("%s", error->message);
        }

        g_regex_match (re, output, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse output: %s", output);
                goto out;
        }

        if (addressp != NULL) {
                *addressp = g_match_info_fetch (match_info, 1);
        }

        if (pidp != NULL) {
                int      pid;
                gboolean result;
                result = parse_value_as_integer (g_match_info_fetch (match_info, 2), &pid);
                if (result) {
                        *pidp = pid;
                } else {
                        *pidp = 0;
                }
        }

        ret = TRUE;

 out:
        g_match_info_free (match_info);
        g_regex_unref (re);

        return ret;
}

static gboolean
start_dbus_daemon (GdmWelcomeSession *welcome_session)
{
        gboolean   res;
        char      *std_out;
        char      *std_err;
        int        exit_status;
        GError    *error;
        GPtrArray *env;

        g_debug ("GdmWelcomeSession: Starting D-Bus daemon");

        env = get_welcome_environment (welcome_session, FALSE);

        std_out = NULL;
        std_err = NULL;
        error = NULL;
        res = spawn_command_line_sync_as_user (DBUS_LAUNCH_COMMAND,
                                               welcome_session->priv->user_name,
                                               welcome_session->priv->group_name,
                                               welcome_session->priv->x11_display_seat_id,
                                               welcome_session->priv->runtime_dir,
                                               NULL, /* log file */
                                               (char **)env->pdata,
                                               &std_out,
                                               &std_err,
                                               &exit_status,
                                               &error);
        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

        if (! res) {
                g_warning ("Unable to launch D-Bus daemon: %s", error->message);
                g_error_free (error);
                goto out;
        }

        /* pull the address and pid from the output */
        res = parse_dbus_launch_output (std_out,
                                        &welcome_session->priv->dbus_bus_address,
                                        &welcome_session->priv->dbus_pid);
        if (! res) {
                g_warning ("Unable to parse D-Bus launch output");
        } else {
                g_debug ("GdmWelcomeSession: Started D-Bus daemon on pid %d", welcome_session->priv->dbus_pid);
        }
 out:
        g_free (std_out);
        g_free (std_err);
        return res;
}

static void
on_session_setup_complete (GdmSessionDirect  *session,
                           const char        *service_name,
                           GdmWelcomeSession *welcome_session)
{
        GHashTable       *hash;
        GHashTableIter    iter;
        gpointer          key, value;

        hash = build_welcome_environment (welcome_session, TRUE);

        g_hash_table_iter_init (&iter, hash);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                gdm_session_direct_set_environment_variable (welcome_session->priv->session, key, value);
        }
        g_hash_table_destroy (hash);

        gdm_session_direct_select_session_type (welcome_session->priv->session, "LoginWindow");

        gdm_session_direct_authenticate (welcome_session->priv->session, service_name);
}

static void
on_session_authenticated (GdmSessionDirect  *session,
                          const char        *service_name,
                          GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_authorize (welcome_session->priv->session, service_name);
}

static void
on_session_authorized (GdmSessionDirect  *session,
                       const char        *service_name,
                       GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_accredit (welcome_session->priv->session, service_name, FALSE);
}

static void
on_session_accredited (GdmSessionDirect  *session,
                       const char        *service_name,
                       GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_open_session (welcome_session->priv->session, service_name);
}

static void
on_session_opened (GdmSessionDirect  *session,
                   const char        *service_name,
                   GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_start_session (welcome_session->priv->session, service_name);
}

static void
on_session_started (GdmSessionDirect  *session,
                    const char        *service_name,
                    int                pid,
                    GdmWelcomeSession *welcome_session)
{
        welcome_session->priv->pid = pid;
        g_signal_emit (G_OBJECT (welcome_session), signals [STARTED], 0);
}

static void
on_session_exited (GdmSessionDirect  *session,
                   int                exit_code,
                   GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_stop_conversation (welcome_session->priv->session, "gdm-welcome");

        g_signal_emit (G_OBJECT (welcome_session), signals [EXITED], 0, exit_code);
}

static void
on_session_died (GdmSessionDirect  *session,
                 int                signal_number,
                 GdmWelcomeSession *welcome_session)
{
        gdm_session_direct_stop_conversation (welcome_session->priv->session, "gdm-welcome");

        g_signal_emit (G_OBJECT (welcome_session), signals [DIED], 0, signal_number);
}

static void
on_conversation_started (GdmSessionDirect  *session,
                         const char        *service_name,
                         GdmWelcomeSession *welcome_session)
{
        char             *log_path;
        char             *log_file;

        log_file = g_strdup_printf ("%s-greeter.log", welcome_session->priv->x11_display_name);
        log_path = g_build_filename (LOGDIR, log_file, NULL);
        g_free (log_file);

        gdm_session_direct_setup_for_program (welcome_session->priv->session, "gdm-welcome", log_path);
        g_free (log_path);
}

static void
on_conversation_stopped (GdmSessionDirect  *session,
                         const char        *service_name,
                         GdmWelcomeSession *welcome_session)
{
        GdmSessionDirect *conversation_session;

        conversation_session = welcome_session->priv->session;
        welcome_session->priv->session = NULL;

        g_debug ("GdmWelcomeSession: conversation stopped");
        stop_dbus_daemon (welcome_session);

        if (welcome_session->priv->pid > 1) {
                g_signal_emit (G_OBJECT (welcome_session), signals [STOPPED], 0);
        }

        if (conversation_session != NULL) {
                gdm_session_direct_close (conversation_session);
                g_object_unref (conversation_session);
        }
}

/**
 * gdm_welcome_session_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X welcome_session. Handles retries and fatal errors properly.
 */
gboolean
gdm_welcome_session_start (GdmWelcomeSession *welcome_session)
{
        gboolean          res;

        g_debug ("GdmWelcomeSession: Starting welcome...");
        res = start_dbus_daemon (welcome_session);

        if (!res) {
                return FALSE;
        }

        welcome_session->priv->session = gdm_session_direct_new (NULL,
                                                                 welcome_session->priv->x11_display_name,
                                                                 welcome_session->priv->x11_display_hostname,
                                                                 welcome_session->priv->x11_display_device,
                                                                 welcome_session->priv->x11_display_seat_id,
                                                                 welcome_session->priv->x11_authority_file,
                                                                 welcome_session->priv->x11_display_is_local);

        g_signal_connect (welcome_session->priv->session,
                          "conversation-started",
                          G_CALLBACK (on_conversation_started),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "conversation-stopped",
                          G_CALLBACK (on_conversation_stopped),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "setup-complete",
                          G_CALLBACK (on_session_setup_complete),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "authenticated",
                          G_CALLBACK (on_session_authenticated),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "authorized",
                          G_CALLBACK (on_session_authorized),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "accredited",
                          G_CALLBACK (on_session_accredited),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "session-opened",
                          G_CALLBACK (on_session_opened),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          welcome_session);
        g_signal_connect (welcome_session->priv->session,
                          "session-died",
                          G_CALLBACK (on_session_died),
                          welcome_session);

        gdm_session_direct_start_conversation (welcome_session->priv->session, "gdm-welcome");
        gdm_session_direct_select_program (welcome_session->priv->session, welcome_session->priv->command);
        return TRUE;
}

gboolean
gdm_welcome_session_stop (GdmWelcomeSession *welcome_session)
{
        if (welcome_session->priv->pid > 1) {
                gdm_signal_pid (welcome_session->priv->pid, SIGTERM);
        } else {
                if (welcome_session->priv->session != NULL) {
                        gdm_session_direct_stop_conversation (welcome_session->priv->session, "gdm-welcome");
                        gdm_session_direct_close (welcome_session->priv->session);

                        g_clear_object (&welcome_session->priv->session);
                } else {
                        stop_dbus_daemon (welcome_session);
                }

                g_signal_emit (G_OBJECT (welcome_session), signals [STOPPED], 0);
        }

        return TRUE;
}

void
gdm_welcome_session_set_server_address (GdmWelcomeSession *welcome_session,
                                        const char        *address)
{
        g_return_if_fail (GDM_IS_WELCOME_SESSION (welcome_session));

        g_free (welcome_session->priv->server_address);
        welcome_session->priv->server_address = g_strdup (address);
}

static void
_gdm_welcome_session_set_x11_display_name (GdmWelcomeSession *welcome_session,
                                           const char        *name)
{
        g_free (welcome_session->priv->x11_display_name);
        welcome_session->priv->x11_display_name = g_strdup (name);
}

static void
_gdm_welcome_session_set_x11_display_seat_id (GdmWelcomeSession *welcome_session,
                                              const char        *sid)
{
        g_free (welcome_session->priv->x11_display_seat_id);
        welcome_session->priv->x11_display_seat_id = g_strdup (sid);
}

static void
_gdm_welcome_session_set_x11_display_hostname (GdmWelcomeSession *welcome_session,
                                               const char        *name)
{
        g_free (welcome_session->priv->x11_display_hostname);
        welcome_session->priv->x11_display_hostname = g_strdup (name);
}

static void
_gdm_welcome_session_set_x11_display_device (GdmWelcomeSession *welcome_session,
                                             const char        *name)
{
        g_free (welcome_session->priv->x11_display_device);
        welcome_session->priv->x11_display_device = g_strdup (name);
}

static void
_gdm_welcome_session_set_x11_display_is_local (GdmWelcomeSession *welcome_session,
                                               gboolean           is_local)
{
        welcome_session->priv->x11_display_is_local = is_local;
}

static void
_gdm_welcome_session_set_x11_authority_file (GdmWelcomeSession *welcome_session,
                                             const char        *file)
{
        g_free (welcome_session->priv->x11_authority_file);
        welcome_session->priv->x11_authority_file = g_strdup (file);
}

static void
_gdm_welcome_session_set_user_name (GdmWelcomeSession *welcome_session,
                                    const char        *name)
{
        g_free (welcome_session->priv->user_name);
        welcome_session->priv->user_name = g_strdup (name);
}

static void
_gdm_welcome_session_set_group_name (GdmWelcomeSession *welcome_session,
                                     const char        *name)
{
        g_free (welcome_session->priv->group_name);
        welcome_session->priv->group_name = g_strdup (name);
}

static void
_gdm_welcome_session_set_runtime_dir (GdmWelcomeSession *welcome_session,
                                      const char        *dir)
{
        g_free (welcome_session->priv->runtime_dir);
        welcome_session->priv->runtime_dir = g_strdup (dir);
}

static void
_gdm_welcome_session_set_server_dbus_path (GdmWelcomeSession *welcome_session,
                                           const char        *name)
{
        g_free (welcome_session->priv->server_dbus_path);
        welcome_session->priv->server_dbus_path = g_strdup (name);
}

static void
_gdm_welcome_session_set_server_dbus_interface (GdmWelcomeSession *welcome_session,
                                                const char        *name)
{
        g_free (welcome_session->priv->server_dbus_interface);
        welcome_session->priv->server_dbus_interface = g_strdup (name);
}

static void
_gdm_welcome_session_set_command (GdmWelcomeSession *welcome_session,
                                  const char        *name)
{
        g_free (welcome_session->priv->command);
        welcome_session->priv->command = g_strdup (name);
}

static void
_gdm_welcome_session_set_server_env_var_name (GdmWelcomeSession *welcome_session,
                                              const char        *name)
{
        g_free (welcome_session->priv->server_env_var_name);
        welcome_session->priv->server_env_var_name = g_strdup (name);
}

static void
gdm_welcome_session_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmWelcomeSession *self;

        self = GDM_WELCOME_SESSION (object);

        switch (prop_id) {
        case PROP_X11_DISPLAY_NAME:
                _gdm_welcome_session_set_x11_display_name (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_SEAT_ID:
                _gdm_welcome_session_set_x11_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_HOSTNAME:
                _gdm_welcome_session_set_x11_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_DEVICE:
                _gdm_welcome_session_set_x11_display_device (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_IS_LOCAL:
                _gdm_welcome_session_set_x11_display_is_local (self, g_value_get_boolean (value));
                break;
        case PROP_X11_AUTHORITY_FILE:
                _gdm_welcome_session_set_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_welcome_session_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_GROUP_NAME:
                _gdm_welcome_session_set_group_name (self, g_value_get_string (value));
                break;
        case PROP_RUNTIME_DIR:
                _gdm_welcome_session_set_runtime_dir (self, g_value_get_string (value));
                break;
        case PROP_SERVER_ADDRESS:
                gdm_welcome_session_set_server_address (self, g_value_get_string (value));
                break;
        case PROP_SERVER_DBUS_PATH:
                _gdm_welcome_session_set_server_dbus_path (self, g_value_get_string (value));
                break;
        case PROP_SERVER_DBUS_INTERFACE:
                _gdm_welcome_session_set_server_dbus_interface (self, g_value_get_string (value));
                break;
        case PROP_SERVER_ENV_VAR_NAME:
                _gdm_welcome_session_set_server_env_var_name (self, g_value_get_string (value));
                break;
        case PROP_COMMAND:
                _gdm_welcome_session_set_command (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_welcome_session_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmWelcomeSession *self;

        self = GDM_WELCOME_SESSION (object);

        switch (prop_id) {
        case PROP_X11_DISPLAY_NAME:
                g_value_set_string (value, self->priv->x11_display_name);
                break;
        case PROP_X11_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->priv->x11_display_seat_id);
                break;
        case PROP_X11_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->priv->x11_display_hostname);
                break;
        case PROP_X11_DISPLAY_DEVICE:
                g_value_set_string (value, self->priv->x11_display_device);
                break;
        case PROP_X11_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->x11_display_is_local);
                break;
        case PROP_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->x11_authority_file);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, self->priv->user_name);
                break;
        case PROP_GROUP_NAME:
                g_value_set_string (value, self->priv->group_name);
                break;
        case PROP_RUNTIME_DIR:
                g_value_set_string (value, self->priv->runtime_dir);
                break;
        case PROP_SERVER_ADDRESS:
                g_value_set_string (value, self->priv->server_address);
                break;
        case PROP_SERVER_DBUS_PATH:
                g_value_set_string (value, self->priv->server_dbus_path);
                break;
        case PROP_SERVER_DBUS_INTERFACE:
                g_value_set_string (value, self->priv->server_dbus_interface);
                break;
        case PROP_SERVER_ENV_VAR_NAME:
                g_value_set_string (value, self->priv->server_env_var_name);
                break;
        case PROP_COMMAND:
                g_value_set_string (value, self->priv->command);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_welcome_session_class_init (GdmWelcomeSessionClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_welcome_session_get_property;
        object_class->set_property = gdm_welcome_session_set_property;
        object_class->finalize = gdm_welcome_session_finalize;

        g_type_class_add_private (klass, sizeof (GdmWelcomeSessionPrivate));

        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NAME,
                                         g_param_spec_string ("x11-display-name",
                                                              "name",
                                                              "name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
         g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("x11-display-seat-id",
                                                              "seat id",
                                                              "seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("x11-display-hostname",
                                                              "hostname",
                                                              "hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_DEVICE,
                                         g_param_spec_string ("x11-display-device",
                                                              "device",
                                                              "device",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("x11-display-is-local",
                                                               "is local",
                                                               "is local",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("x11-authority-file",
                                                              "authority file",
                                                              "authority file",
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
        g_object_class_install_property (object_class,
                                         PROP_RUNTIME_DIR,
                                         g_param_spec_string ("runtime-dir",
                                                              "runtime dir",
                                                              "runtime dir",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SERVER_ADDRESS,
                                         g_param_spec_string ("server-address",
                                                              "server address",
                                                              "server address",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SERVER_DBUS_PATH,
                                         g_param_spec_string ("server-dbus-path",
                                                              "server dbus path",
                                                              "server dbus path",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_SERVER_DBUS_INTERFACE,
                                         g_param_spec_string ("server-dbus-interface",
                                                              "server dbus interface",
                                                              "server dbus interface",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_SERVER_ENV_VAR_NAME,
                                         g_param_spec_string ("server-env-var-name",
                                                              "server env var name",
                                                              "server env var name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_COMMAND,
                                         g_param_spec_string ("command",
                                                              "command",
                                                              "command",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        signals [STARTED] =
                g_signal_new ("started",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmWelcomeSessionClass, started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmWelcomeSessionClass, stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmWelcomeSessionClass, exited),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [DIED] =
                g_signal_new ("died",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmWelcomeSessionClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
}

static void
gdm_welcome_session_init (GdmWelcomeSession *welcome_session)
{

        welcome_session->priv = GDM_WELCOME_SESSION_GET_PRIVATE (welcome_session);

        welcome_session->priv->command = NULL;
        welcome_session->priv->session = NULL;
}

static void
gdm_welcome_session_finalize (GObject *object)
{
        GdmWelcomeSession *welcome_session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_WELCOME_SESSION (object));

        welcome_session = GDM_WELCOME_SESSION (object);

        g_return_if_fail (welcome_session->priv != NULL);

        gdm_welcome_session_stop (welcome_session);

        if (welcome_session->priv->session) {
                g_object_unref (welcome_session->priv->session);
        }

        g_free (welcome_session->priv->command);
        g_free (welcome_session->priv->user_name);
        g_free (welcome_session->priv->group_name);
        g_free (welcome_session->priv->runtime_dir);
        g_free (welcome_session->priv->x11_display_name);
        g_free (welcome_session->priv->x11_display_seat_id);
        g_free (welcome_session->priv->x11_display_device);
        g_free (welcome_session->priv->x11_display_hostname);
        g_free (welcome_session->priv->x11_authority_file);
        g_free (welcome_session->priv->server_address);
        g_free (welcome_session->priv->server_dbus_path);
        g_free (welcome_session->priv->server_dbus_interface);
        g_free (welcome_session->priv->server_env_var_name);
        g_free (welcome_session->priv->dbus_bus_address);

        G_OBJECT_CLASS (gdm_welcome_session_parent_class)->finalize (object);
}
