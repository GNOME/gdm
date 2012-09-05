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

#include "gdm-common.h"

#include "gdm-session-enum-types.h"
#include "gdm-launch-environment.h"

#define DBUS_LAUNCH_COMMAND BINDIR "/dbus-launch"

#define MAX_LOGS 5

extern char **environ;

#define GDM_LAUNCH_ENVIRONMENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LAUNCH_ENVIRONMENT, GdmLaunchEnvironmentPrivate))

struct GdmLaunchEnvironmentPrivate
{
        GdmSession     *session;
        char           *command;
        GPid            pid;

        GdmSessionVerificationMode verification_mode;

        char           *user_name;
        char           *group_name;
        char           *runtime_dir;

        char           *session_id;
        char           *x11_display_name;
        char           *x11_display_seat_id;
        char           *x11_display_device;
        char           *x11_display_hostname;
        char           *x11_authority_file;
        gboolean        x11_display_is_local;

        GPid            dbus_pid;
        char           *dbus_bus_address;
};

enum {
        PROP_0,
        PROP_VERIFICATION_MODE,
        PROP_X11_DISPLAY_NAME,
        PROP_X11_DISPLAY_SEAT_ID,
        PROP_X11_DISPLAY_DEVICE,
        PROP_X11_DISPLAY_HOSTNAME,
        PROP_X11_AUTHORITY_FILE,
        PROP_X11_DISPLAY_IS_LOCAL,
        PROP_USER_NAME,
        PROP_GROUP_NAME,
        PROP_RUNTIME_DIR,
        PROP_COMMAND,
};

enum {
        OPENED,
        STARTED,
        STOPPED,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_launch_environment_class_init    (GdmLaunchEnvironmentClass *klass);
static void     gdm_launch_environment_init          (GdmLaunchEnvironment      *launch_environment);
static void     gdm_launch_environment_finalize      (GObject                   *object);

G_DEFINE_TYPE (GdmLaunchEnvironment, gdm_launch_environment, G_TYPE_OBJECT)

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("GdmLaunchEnvironment: launch environment: %s", str);
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
build_launch_environment (GdmLaunchEnvironment *launch_environment,
                          gboolean              start_session)
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

        if (launch_environment->priv->dbus_bus_address != NULL) {
                g_hash_table_insert (hash,
                                     g_strdup ("DBUS_SESSION_BUS_ADDRESS"),
                                     g_strdup (launch_environment->priv->dbus_bus_address));
        }

        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (launch_environment->priv->x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (launch_environment->priv->x11_display_name));

        g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (launch_environment->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (launch_environment->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (launch_environment->priv->user_name));

        g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
        g_hash_table_remove (hash, "MAIL");

        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        gdm_get_pwent_for_name (launch_environment->priv->user_name, &pwent);
        if (pwent != NULL) {
                if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
                        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
                }

                g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
        }

        if (start_session && launch_environment->priv->x11_display_seat_id != NULL) {
                char *seat_id;

                seat_id = launch_environment->priv->x11_display_seat_id;
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
get_launch_environment (GdmLaunchEnvironment *launch_environment,
                        gboolean              start_session)
{
        GHashTable    *hash;
        GPtrArray     *env;

        hash = build_launch_environment (launch_environment, start_session);

        env = g_ptr_array_new ();
        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

static gboolean
stop_dbus_daemon (GdmLaunchEnvironment *launch_environment)
{
        int res;

        if (launch_environment->priv->dbus_pid > 0) {
                g_debug ("GdmLaunchEnvironment: Stopping D-Bus daemon");
                res = gdm_signal_pid (-1 * launch_environment->priv->dbus_pid, SIGTERM);
                if (res < 0) {
                        g_warning ("Unable to kill D-Bus daemon");
                } else {
                        launch_environment->priv->dbus_pid = 0;
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

        g_debug ("GdmLaunchEnvironment: Setting up run time dir %s", data->runtime_dir);
        g_mkdir (data->runtime_dir, 0755);
        res = chown (data->runtime_dir, pwent->pw_uid, pwent->pw_gid);
        if (res == -1) {
                g_warning ("GdmLaunchEnvironment: Error setting owner of run time directory: %s",
                           g_strerror (errno));
        }

        g_debug ("GdmLaunchEnvironment: Changing (uid:gid) for child process to (%d:%d)",
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
                g_debug ("GdmLaunchEnvironment: could not set pid '%u' as leader of new session and process group - %s",
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
start_dbus_daemon (GdmLaunchEnvironment *launch_environment)
{
        gboolean   res;
        char      *std_out;
        char      *std_err;
        int        exit_status;
        GError    *error;
        GPtrArray *env;

        g_debug ("GdmLaunchEnvironment: Starting D-Bus daemon");

        env = get_launch_environment (launch_environment, FALSE);

        std_out = NULL;
        std_err = NULL;
        error = NULL;
        res = spawn_command_line_sync_as_user (DBUS_LAUNCH_COMMAND,
                                               launch_environment->priv->user_name,
                                               launch_environment->priv->group_name,
                                               launch_environment->priv->x11_display_seat_id,
                                               launch_environment->priv->runtime_dir,
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
                                        &launch_environment->priv->dbus_bus_address,
                                        &launch_environment->priv->dbus_pid);
        if (! res) {
                g_warning ("Unable to parse D-Bus launch output");
        } else {
                g_debug ("GdmLaunchEnvironment: Started D-Bus daemon on pid %d", launch_environment->priv->dbus_pid);
        }
 out:
        g_free (std_out);
        g_free (std_err);
        return res;
}

static void
on_session_setup_complete (GdmSession        *session,
                           const char        *service_name,
                           GdmLaunchEnvironment *launch_environment)
{
        GHashTable       *hash;
        GHashTableIter    iter;
        gpointer          key, value;

        hash = build_launch_environment (launch_environment, TRUE);

        g_hash_table_iter_init (&iter, hash);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                gdm_session_set_environment_variable (launch_environment->priv->session, key, value);
        }
        g_hash_table_destroy (hash);

        gdm_session_select_session_type (launch_environment->priv->session, "LoginWindow");
}

static void
on_session_opened (GdmSession           *session,
                   const char           *service_name,
                   const char           *session_id,
                   GdmLaunchEnvironment *launch_environment)
{
        launch_environment->priv->session_id = g_strdup (session_id);

        g_signal_emit (G_OBJECT (launch_environment), signals [OPENED], 0);
        gdm_session_start_session (launch_environment->priv->session, service_name);
}

static void
on_session_started (GdmSession           *session,
                    const char           *service_name,
                    int                   pid,
                    GdmLaunchEnvironment *launch_environment)
{
        launch_environment->priv->pid = pid;
        g_signal_emit (G_OBJECT (launch_environment), signals [STARTED], 0);
}

static void
on_session_exited (GdmSession           *session,
                   int                   exit_code,
                   GdmLaunchEnvironment *launch_environment)
{
        gdm_session_stop_conversation (launch_environment->priv->session, "gdm-launch-environment");

        g_signal_emit (G_OBJECT (launch_environment), signals [EXITED], 0, exit_code);
}

static void
on_session_died (GdmSession           *session,
                 int                   signal_number,
                 GdmLaunchEnvironment *launch_environment)
{
        gdm_session_stop_conversation (launch_environment->priv->session, "gdm-launch-environment");

        g_signal_emit (G_OBJECT (launch_environment), signals [DIED], 0, signal_number);
}

static void
on_conversation_started (GdmSession           *session,
                         const char           *service_name,
                         GdmLaunchEnvironment *launch_environment)
{
        char             *log_path;
        char             *log_file;

        log_file = g_strdup_printf ("%s-greeter.log", launch_environment->priv->x11_display_name);
        log_path = g_build_filename (LOGDIR, log_file, NULL);
        g_free (log_file);

        gdm_session_setup_for_program (launch_environment->priv->session,
                                       "gdm-launch-environment",
                                       launch_environment->priv->user_name,
                                       log_path);
        g_free (log_path);
}

static void
on_conversation_stopped (GdmSession           *session,
                         const char           *service_name,
                         GdmLaunchEnvironment *launch_environment)
{
        GdmSession *conversation_session;

        conversation_session = launch_environment->priv->session;
        launch_environment->priv->session = NULL;

        g_debug ("GdmLaunchEnvironment: conversation stopped");
        stop_dbus_daemon (launch_environment);

        if (launch_environment->priv->pid > 1) {
                g_signal_emit (G_OBJECT (launch_environment), signals [STOPPED], 0);
        }

        if (conversation_session != NULL) {
                gdm_session_close (conversation_session);
                g_object_unref (conversation_session);
        }
}

/**
 * gdm_launch_environment_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X launch_environment. Handles retries and fatal errors properly.
 */
gboolean
gdm_launch_environment_start (GdmLaunchEnvironment *launch_environment)
{
        gboolean          res;
        struct passwd *passwd_entry;
        uid_t uid;

        g_debug ("GdmLaunchEnvironment: Starting...");
        res = start_dbus_daemon (launch_environment);

        if (!res) {
                return FALSE;
        }

        res = gdm_get_pwent_for_name (launch_environment->priv->user_name,
                                      &passwd_entry);

        if (!res) {
                return FALSE;
        }

        uid = passwd_entry->pw_uid;
        launch_environment->priv->session = gdm_session_new (launch_environment->priv->verification_mode,
                                                             uid,
                                                             launch_environment->priv->x11_display_name,
                                                             launch_environment->priv->x11_display_hostname,
                                                             launch_environment->priv->x11_display_device,
                                                             launch_environment->priv->x11_display_seat_id,
                                                             launch_environment->priv->x11_authority_file,
                                                             launch_environment->priv->x11_display_is_local);

        g_signal_connect (launch_environment->priv->session,
                          "conversation-started",
                          G_CALLBACK (on_conversation_started),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "conversation-stopped",
                          G_CALLBACK (on_conversation_stopped),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "setup-complete",
                          G_CALLBACK (on_session_setup_complete),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "session-opened",
                          G_CALLBACK (on_session_opened),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          launch_environment);
        g_signal_connect (launch_environment->priv->session,
                          "session-died",
                          G_CALLBACK (on_session_died),
                          launch_environment);

        gdm_session_start_conversation (launch_environment->priv->session, "gdm-launch-environment");
        gdm_session_select_program (launch_environment->priv->session, launch_environment->priv->command);
        return TRUE;
}

gboolean
gdm_launch_environment_stop (GdmLaunchEnvironment *launch_environment)
{
        if (launch_environment->priv->pid > 1) {
                gdm_signal_pid (launch_environment->priv->pid, SIGTERM);
        } else {
                if (launch_environment->priv->session != NULL) {
                        gdm_session_stop_conversation (launch_environment->priv->session, "gdm-launch-environment");
                        gdm_session_close (launch_environment->priv->session);

                        g_clear_object (&launch_environment->priv->session);
                } else {
                        stop_dbus_daemon (launch_environment);
                }

                g_signal_emit (G_OBJECT (launch_environment), signals [STOPPED], 0);
        }

        return TRUE;
}

GdmSession *
gdm_launch_environment_get_session (GdmLaunchEnvironment *launch_environment)
{
        return launch_environment->priv->session;
}

char *
gdm_launch_environment_get_session_id (GdmLaunchEnvironment *launch_environment)
{
        return g_strdup (launch_environment->priv->session_id);
}

static void
_gdm_launch_environment_set_verification_mode (GdmLaunchEnvironment           *launch_environment,
                                               GdmSessionVerificationMode      verification_mode)
{
        launch_environment->priv->verification_mode = verification_mode;
}

static void
_gdm_launch_environment_set_x11_display_name (GdmLaunchEnvironment *launch_environment,
                                              const char           *name)
{
        g_free (launch_environment->priv->x11_display_name);
        launch_environment->priv->x11_display_name = g_strdup (name);
}

static void
_gdm_launch_environment_set_x11_display_seat_id (GdmLaunchEnvironment *launch_environment,
                                                 const char           *sid)
{
        g_free (launch_environment->priv->x11_display_seat_id);
        launch_environment->priv->x11_display_seat_id = g_strdup (sid);
}

static void
_gdm_launch_environment_set_x11_display_hostname (GdmLaunchEnvironment *launch_environment,
                                                  const char           *name)
{
        g_free (launch_environment->priv->x11_display_hostname);
        launch_environment->priv->x11_display_hostname = g_strdup (name);
}

static void
_gdm_launch_environment_set_x11_display_device (GdmLaunchEnvironment *launch_environment,
                                                const char           *name)
{
        g_free (launch_environment->priv->x11_display_device);
        launch_environment->priv->x11_display_device = g_strdup (name);
}

static void
_gdm_launch_environment_set_x11_display_is_local (GdmLaunchEnvironment *launch_environment,
                                                  gboolean              is_local)
{
        launch_environment->priv->x11_display_is_local = is_local;
}

static void
_gdm_launch_environment_set_x11_authority_file (GdmLaunchEnvironment *launch_environment,
                                                const char           *file)
{
        g_free (launch_environment->priv->x11_authority_file);
        launch_environment->priv->x11_authority_file = g_strdup (file);
}

static void
_gdm_launch_environment_set_user_name (GdmLaunchEnvironment *launch_environment,
                                       const char           *name)
{
        g_free (launch_environment->priv->user_name);
        launch_environment->priv->user_name = g_strdup (name);
}

static void
_gdm_launch_environment_set_group_name (GdmLaunchEnvironment *launch_environment,
                                        const char           *name)
{
        g_free (launch_environment->priv->group_name);
        launch_environment->priv->group_name = g_strdup (name);
}

static void
_gdm_launch_environment_set_runtime_dir (GdmLaunchEnvironment *launch_environment,
                                         const char           *dir)
{
        g_free (launch_environment->priv->runtime_dir);
        launch_environment->priv->runtime_dir = g_strdup (dir);
}

static void
_gdm_launch_environment_set_command (GdmLaunchEnvironment *launch_environment,
                                     const char           *name)
{
        g_free (launch_environment->priv->command);
        launch_environment->priv->command = g_strdup (name);
}

static void
gdm_launch_environment_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
        GdmLaunchEnvironment *self;

        self = GDM_LAUNCH_ENVIRONMENT (object);

        switch (prop_id) {
        case PROP_VERIFICATION_MODE:
                _gdm_launch_environment_set_verification_mode (self, g_value_get_enum (value));
                break;
        case PROP_X11_DISPLAY_NAME:
                _gdm_launch_environment_set_x11_display_name (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_SEAT_ID:
                _gdm_launch_environment_set_x11_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_HOSTNAME:
                _gdm_launch_environment_set_x11_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_DEVICE:
                _gdm_launch_environment_set_x11_display_device (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_IS_LOCAL:
                _gdm_launch_environment_set_x11_display_is_local (self, g_value_get_boolean (value));
                break;
        case PROP_X11_AUTHORITY_FILE:
                _gdm_launch_environment_set_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_launch_environment_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_GROUP_NAME:
                _gdm_launch_environment_set_group_name (self, g_value_get_string (value));
                break;
        case PROP_RUNTIME_DIR:
                _gdm_launch_environment_set_runtime_dir (self, g_value_get_string (value));
                break;
        case PROP_COMMAND:
                _gdm_launch_environment_set_command (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_launch_environment_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
        GdmLaunchEnvironment *self;

        self = GDM_LAUNCH_ENVIRONMENT (object);

        switch (prop_id) {
        case PROP_VERIFICATION_MODE:
                g_value_set_enum (value, self->priv->verification_mode);
                break;
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
        case PROP_COMMAND:
                g_value_set_string (value, self->priv->command);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_launch_environment_class_init (GdmLaunchEnvironmentClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_launch_environment_get_property;
        object_class->set_property = gdm_launch_environment_set_property;
        object_class->finalize = gdm_launch_environment_finalize;

        g_type_class_add_private (klass, sizeof (GdmLaunchEnvironmentPrivate));

        g_object_class_install_property (object_class,
                                         PROP_VERIFICATION_MODE,
                                         g_param_spec_enum ("verification-mode",
                                                            "verification mode",
                                                            "verification mode",
                                                            GDM_TYPE_SESSION_VERIFICATION_MODE,
                                                            GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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
                                         PROP_COMMAND,
                                         g_param_spec_string ("command",
                                                              "command",
                                                              "command",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        signals [OPENED] =
                g_signal_new ("opened",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLaunchEnvironmentClass, opened),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STARTED] =
                g_signal_new ("started",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLaunchEnvironmentClass, started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLaunchEnvironmentClass, stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLaunchEnvironmentClass, exited),
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
                              G_STRUCT_OFFSET (GdmLaunchEnvironmentClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
}

static void
gdm_launch_environment_init (GdmLaunchEnvironment *launch_environment)
{

        launch_environment->priv = GDM_LAUNCH_ENVIRONMENT_GET_PRIVATE (launch_environment);

        launch_environment->priv->command = NULL;
        launch_environment->priv->session = NULL;
}

static void
gdm_launch_environment_finalize (GObject *object)
{
        GdmLaunchEnvironment *launch_environment;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (object));

        launch_environment = GDM_LAUNCH_ENVIRONMENT (object);

        g_return_if_fail (launch_environment->priv != NULL);

        gdm_launch_environment_stop (launch_environment);

        if (launch_environment->priv->session) {
                g_object_unref (launch_environment->priv->session);
        }

        g_free (launch_environment->priv->command);
        g_free (launch_environment->priv->user_name);
        g_free (launch_environment->priv->group_name);
        g_free (launch_environment->priv->runtime_dir);
        g_free (launch_environment->priv->x11_display_name);
        g_free (launch_environment->priv->x11_display_seat_id);
        g_free (launch_environment->priv->x11_display_device);
        g_free (launch_environment->priv->x11_display_hostname);
        g_free (launch_environment->priv->x11_authority_file);
        g_free (launch_environment->priv->dbus_bus_address);
        g_free (launch_environment->priv->session_id);

        G_OBJECT_CLASS (gdm_launch_environment_parent_class)->finalize (object);
}
