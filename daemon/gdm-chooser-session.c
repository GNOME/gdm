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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"

#include "gdm-chooser-session.h"

#define DBUS_LAUNCH_COMMAND BINDIR "/dbus-launch --exit-with-session"

#define GDM_CHOOSER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/ChooserServer"
#define GDM_CHOOSER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.ChooserServer"

extern char **environ;

#define GDM_CHOOSER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_CHOOSER_SESSION, GdmChooserSessionPrivate))

struct GdmChooserSessionPrivate
{
        char           *command;
        GPid            pid;

        char           *user_name;
        char           *group_name;

        char           *x11_display_name;
        char           *x11_display_device;
        char           *x11_display_hostname;
        char           *x11_authority_file;

        guint           child_watch_id;

        GPid            dbus_pid;
        char           *dbus_bus_address;

        char           *server_address;
};

enum {
        PROP_0,
        PROP_X11_DISPLAY_NAME,
        PROP_X11_DISPLAY_DEVICE,
        PROP_X11_DISPLAY_HOSTNAME,
        PROP_X11_AUTHORITY_FILE,
        PROP_USER_NAME,
        PROP_GROUP_NAME,
        PROP_SERVER_ADDRESS,
};

enum {
        STARTED,
        STOPPED,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_chooser_session_class_init    (GdmChooserSessionClass *klass);
static void     gdm_chooser_session_init          (GdmChooserSession      *chooser_session);
static void     gdm_chooser_session_finalize      (GObject                *object);

G_DEFINE_TYPE (GdmChooserSession, gdm_chooser_session, G_TYPE_OBJECT)

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("GdmChooserSession: chooser environment: %s", str);
        g_ptr_array_add (env, str);
}

static GPtrArray *
get_chooser_environment (GdmChooserSession *chooser_session)
{
        GPtrArray     *env;
        GHashTable    *hash;
        struct passwd *pwent;

        env = g_ptr_array_new ();

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        if (chooser_session->priv->dbus_bus_address != NULL) {
                g_hash_table_insert (hash, g_strdup ("DBUS_SESSION_BUS_ADDRESS"), g_strdup (chooser_session->priv->dbus_bus_address));
        }
        if (chooser_session->priv->server_address != NULL) {
                g_hash_table_insert (hash, g_strdup ("GDM_CHOOSER_DBUS_ADDRESS"), g_strdup (chooser_session->priv->server_address));
        }

        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (chooser_session->priv->x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (chooser_session->priv->x11_display_name));

#if 0
        /* hackish ain't it */
        set_xnest_parent_stuff ();
#endif

        g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (chooser_session->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (chooser_session->priv->user_name));
        g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (chooser_session->priv->user_name));

        g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
        g_hash_table_remove (hash, "MAIL");

        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        pwent = getpwnam (chooser_session->priv->user_name);
        if (pwent != NULL) {
                if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
                        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
                }

                g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
        }


        g_hash_table_insert (hash, g_strdup ("PATH"), g_strdup (g_getenv ("PATH")));

        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

static void
chooser_session_child_watch (GPid               pid,
                             int                status,
                             GdmChooserSession *session)
{
        g_debug ("GdmChooserSession: child (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        if (WIFEXITED (status)) {
                int code = WEXITSTATUS (status);
                g_signal_emit (session, signals [EXITED], 0, code);
        } else if (WIFSIGNALED (status)) {
                int num = WTERMSIG (status);
                g_signal_emit (session, signals [DIED], 0, num);
        }

        g_spawn_close_pid (session->priv->pid);
        session->priv->pid = -1;
}

typedef struct {
        const char *user_name;
        const char *group_name;
} SpawnChildData;

static void
spawn_child_setup (SpawnChildData *data)
{
        struct passwd *pwent;
        struct group  *grent;

        if (data->user_name == NULL) {
                return;
        }

        pwent = getpwnam (data->user_name);
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

        g_debug ("GdmChooserSession: Changing (uid:gid) for child process to (%d:%d)",
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
                        g_warning (_("Couldn't set groupid to 0"));
                        /* Don't error out, it's not fatal, if it fails we'll
                         * just still be */
                }

                /* this will get rid of any suplementary groups etc... */
                setgroups (1, groups);
        }

        if (setsid () < 0) {
                g_debug ("GdmChooserSession: could not set pid '%u' as leader of new session and process group - %s",
                         (guint) getpid (), g_strerror (errno));
                _exit (2);
        }
}

static gboolean
spawn_command_line_sync_as_user (const char *command_line,
                                 const char *user_name,
                                 const char *group_name,
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
spawn_command_line_async_as_user (const char *command_line,
                                  const char *user_name,
                                  const char *group_name,
                                  char      **env,
                                  GPid       *child_pid,
                                  GError    **error)
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

        local_error = NULL;
        res = g_spawn_async (NULL,
                             argv,
                             env,
                             G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                             (GSpawnChildSetupFunc)spawn_child_setup,
                             &data,
                             child_pid,
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
                g_critical (error->message);
        }

        g_regex_match (re, output, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse output: %s", output);
                goto out;
        }

        if (addressp != NULL) {
                *addressp = g_strdup (g_match_info_fetch (match_info, 1));
        }

        if (pidp != NULL) {
                int      pid;
                gboolean res;
                res = parse_value_as_integer (g_match_info_fetch (match_info, 2), &pid);
                if (res) {
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
start_dbus_daemon (GdmChooserSession *chooser_session)
{
        gboolean   res;
        char      *std_out;
        char      *std_err;
        int        exit_status;
        GError    *error;
        GPtrArray *env;

        env = get_chooser_environment (chooser_session);

        error = NULL;
        res = spawn_command_line_sync_as_user (DBUS_LAUNCH_COMMAND,
                                               chooser_session->priv->user_name,
                                               chooser_session->priv->group_name,
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
                                        &chooser_session->priv->dbus_bus_address,
                                        &chooser_session->priv->dbus_pid);
        if (! res) {
                g_warning ("Unable to parse D-Bus launch output");
        } else {
                g_debug ("GdmChooserSession: Started D-Bus daemon on pid %d", chooser_session->priv->dbus_pid);
        }
 out:
        return res;
}

static gboolean
stop_dbus_daemon (GdmChooserSession *chooser_session)
{
        if (chooser_session->priv->dbus_pid > 0) {
                gdm_signal_pid (-1 * chooser_session->priv->dbus_pid, SIGTERM);
                chooser_session->priv->dbus_pid = 0;
        }
        return TRUE;
}

static gboolean
gdm_chooser_session_spawn (GdmChooserSession *chooser_session)
{
        GError          *error;
        GPtrArray       *env;
        gboolean         ret;
        gboolean         res;

        ret = FALSE;

        g_debug ("GdmChooserSession: Running chooser_session process: %s", chooser_session->priv->command);

        res = start_dbus_daemon (chooser_session);
        if (! res) {
                /* FIXME: */
        }

        env = get_chooser_environment (chooser_session);

        error = NULL;

        ret = spawn_command_line_async_as_user (chooser_session->priv->command,
                                                chooser_session->priv->user_name,
                                                chooser_session->priv->group_name,
                                                (char **)env->pdata,
                                                &chooser_session->priv->pid,
                                                &error);

        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

        if (! ret) {
                g_warning ("Could not start command '%s': %s",
                           chooser_session->priv->command,
                           error->message);
                g_error_free (error);
                goto out;
        } else {
                g_debug ("GdmChooserSession: ChooserSession on pid %d", (int)chooser_session->priv->pid);
        }

        chooser_session->priv->child_watch_id = g_child_watch_add (chooser_session->priv->pid,
                                                                   (GChildWatchFunc)chooser_session_child_watch,
                                                                   chooser_session);

 out:

        return ret;
}

/**
 * gdm_chooser_session_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X chooser_session. Handles retries and fatal errors properly.
 */
gboolean
gdm_chooser_session_start (GdmChooserSession *chooser_session)
{
        gboolean    res;

        g_debug ("GdmChooserSession: Starting chooser...");

        res = gdm_chooser_session_spawn (chooser_session);

        if (res) {

        }


        return res;
}

static int
wait_on_child (int pid)
{
        int status;

 wait_again:
        if (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR) {
                        goto wait_again;
                } else if (errno == ECHILD) {
                        ; /* do nothing, child already reaped */
                } else {
                        g_debug ("GdmChooserSession: waitpid () should not fail");
                }
        }

        return status;
}

static void
chooser_session_died (GdmChooserSession *chooser_session)
{
        int exit_status;

        g_debug ("GdmChooserSession: Waiting on process %d", chooser_session->priv->pid);
        exit_status = wait_on_child (chooser_session->priv->pid);

        if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                g_debug ("GdmChooserSession: Wait on child process failed");
        } else {
                /* exited normally */
        }

        g_spawn_close_pid (chooser_session->priv->pid);
        chooser_session->priv->pid = -1;

        g_debug ("GdmChooserSession: ChooserSession died");
}

gboolean
gdm_chooser_session_stop (GdmChooserSession *chooser_session)
{

        if (chooser_session->priv->pid <= 1) {
                return TRUE;
        }

        /* remove watch source before we can wait on child */
        if (chooser_session->priv->child_watch_id > 0) {
                g_source_remove (chooser_session->priv->child_watch_id);
                chooser_session->priv->child_watch_id = 0;
        }

        g_debug ("GdmChooserSession: Stopping chooser_session");

        gdm_signal_pid (-1 * chooser_session->priv->pid, SIGTERM);
        chooser_session_died (chooser_session);

        stop_dbus_daemon (chooser_session);

        return TRUE;
}

void
gdm_chooser_session_set_server_address (GdmChooserSession *chooser_session,
                                        const char        *address)
{
        g_return_if_fail (GDM_IS_CHOOSER_SESSION (chooser_session));

        g_free (chooser_session->priv->server_address);
        chooser_session->priv->server_address = g_strdup (address);
}

static void
_gdm_chooser_session_set_x11_display_name (GdmChooserSession *chooser_session,
                                           const char        *name)
{
        g_free (chooser_session->priv->x11_display_name);
        chooser_session->priv->x11_display_name = g_strdup (name);
}

static void
_gdm_chooser_session_set_x11_display_hostname (GdmChooserSession *chooser_session,
                                               const char        *name)
{
        g_free (chooser_session->priv->x11_display_hostname);
        chooser_session->priv->x11_display_hostname = g_strdup (name);
}

static void
_gdm_chooser_session_set_x11_display_device (GdmChooserSession *chooser_session,
                                             const char        *name)
{
        g_free (chooser_session->priv->x11_display_device);
        chooser_session->priv->x11_display_device = g_strdup (name);
}

static void
_gdm_chooser_session_set_x11_authority_file (GdmChooserSession *chooser_session,
                                             const char        *file)
{
        g_free (chooser_session->priv->x11_authority_file);
        chooser_session->priv->x11_authority_file = g_strdup (file);
}

static void
_gdm_chooser_session_set_user_name (GdmChooserSession *chooser_session,
                                    const char        *name)
{
        g_free (chooser_session->priv->user_name);
        chooser_session->priv->user_name = g_strdup (name);
}

static void
_gdm_chooser_session_set_group_name (GdmChooserSession *chooser_session,
                                     const char        *name)
{
        g_free (chooser_session->priv->group_name);
        chooser_session->priv->group_name = g_strdup (name);
}

static void
gdm_chooser_session_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmChooserSession *self;

        self = GDM_CHOOSER_SESSION (object);

        switch (prop_id) {
        case PROP_X11_DISPLAY_NAME:
                _gdm_chooser_session_set_x11_display_name (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_HOSTNAME:
                _gdm_chooser_session_set_x11_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_DEVICE:
                _gdm_chooser_session_set_x11_display_device (self, g_value_get_string (value));
                break;
        case PROP_X11_AUTHORITY_FILE:
                _gdm_chooser_session_set_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_chooser_session_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_GROUP_NAME:
                _gdm_chooser_session_set_group_name (self, g_value_get_string (value));
                break;
        case PROP_SERVER_ADDRESS:
                gdm_chooser_session_set_server_address (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_chooser_session_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmChooserSession *self;

        self = GDM_CHOOSER_SESSION (object);

        switch (prop_id) {
        case PROP_X11_DISPLAY_NAME:
                g_value_set_string (value, self->priv->x11_display_name);
                break;
        case PROP_X11_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->priv->x11_display_hostname);
                break;
        case PROP_X11_DISPLAY_DEVICE:
                g_value_set_string (value, self->priv->x11_display_device);
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
        case PROP_SERVER_ADDRESS:
                g_value_set_string (value, self->priv->server_address);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_chooser_session_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        GdmChooserSession      *chooser_session;
        GdmChooserSessionClass *klass;

        klass = GDM_CHOOSER_SESSION_CLASS (g_type_class_peek (GDM_TYPE_CHOOSER_SESSION));

        chooser_session = GDM_CHOOSER_SESSION (G_OBJECT_CLASS (gdm_chooser_session_parent_class)->constructor (type,
                                                                                                               n_construct_properties,
                                                                                                               construct_properties));

        return G_OBJECT (chooser_session);
}

static void
gdm_chooser_session_class_init (GdmChooserSessionClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_chooser_session_get_property;
        object_class->set_property = gdm_chooser_session_set_property;
        object_class->constructor = gdm_chooser_session_constructor;
        object_class->finalize = gdm_chooser_session_finalize;

        g_type_class_add_private (klass, sizeof (GdmChooserSessionPrivate));

        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NAME,
                                         g_param_spec_string ("x11-display-name",
                                                              "name",
                                                              "name",
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
                                                              "gdm",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_GROUP_NAME,
                                         g_param_spec_string ("group-name",
                                                              "group name",
                                                              "group name",
                                                              "gdm",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SERVER_ADDRESS,
                                         g_param_spec_string ("server-address",
                                                              "server address",
                                                              "server address",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals [STARTED] =
                g_signal_new ("started",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserSessionClass, started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserSessionClass, stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmChooserSessionClass, exited),
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
                              G_STRUCT_OFFSET (GdmChooserSessionClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
}

static void
gdm_chooser_session_init (GdmChooserSession *chooser_session)
{

        chooser_session->priv = GDM_CHOOSER_SESSION_GET_PRIVATE (chooser_session);

        chooser_session->priv->pid = -1;

        chooser_session->priv->command = g_strdup (LIBEXECDIR "/gdm-simple-chooser");
}

static void
gdm_chooser_session_finalize (GObject *object)
{
        GdmChooserSession *chooser_session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_CHOOSER_SESSION (object));

        chooser_session = GDM_CHOOSER_SESSION (object);

        g_return_if_fail (chooser_session->priv != NULL);

        gdm_chooser_session_stop (chooser_session);

        g_free (chooser_session->priv->command);
        g_free (chooser_session->priv->user_name);
        g_free (chooser_session->priv->group_name);
        g_free (chooser_session->priv->x11_display_name);
        g_free (chooser_session->priv->x11_display_device);
        g_free (chooser_session->priv->x11_display_hostname);
        g_free (chooser_session->priv->x11_authority_file);
        g_free (chooser_session->priv->server_address);

        G_OBJECT_CLASS (gdm_chooser_session_parent_class)->finalize (object);
}

GdmChooserSession *
gdm_chooser_session_new (const char *display_name,
                         const char *display_device,
                         const char *display_hostname)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_CHOOSER_SESSION,
                               "x11-display-name", display_name,
                               "x11-display-device", display_device,
                               "x11-display-hostname", display_hostname,
                               NULL);

        return GDM_CHOOSER_SESSION (object);
}
