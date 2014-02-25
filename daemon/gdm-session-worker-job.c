/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef ENABLE_SYSTEMD_JOURNAL
#include <systemd/sd-journal.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-common.h"

#include "gdm-session-worker-job.h"

extern char **environ;

#define GDM_SESSION_WORKER_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_WORKER_JOB, GdmSessionWorkerJobPrivate))

struct GdmSessionWorkerJobPrivate
{
        char           *command;
        GPid            pid;
        gboolean        for_reauth;

        guint           child_watch_id;

        char           *server_address;
        char          **environment;
};

enum {
        PROP_0,
        PROP_SERVER_ADDRESS,
        PROP_ENVIRONMENT,
        PROP_FOR_REAUTH,
};

enum {
        STARTED,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_session_worker_job_class_init       (GdmSessionWorkerJobClass *klass);
static void     gdm_session_worker_job_init     (GdmSessionWorkerJob      *session_worker_job);
static void     gdm_session_worker_job_finalize (GObject         *object);

G_DEFINE_TYPE (GdmSessionWorkerJob, gdm_session_worker_job, G_TYPE_OBJECT)

static void
session_worker_job_setup_journal_fds (void)
{
#ifdef ENABLE_SYSTEMD_JOURNAL
        if (sd_booted () > 0) {
                const char *identifier = "gdm-session-worker";
                int out, err;

                out = sd_journal_stream_fd (identifier, LOG_INFO, FALSE);
                if (out < 0)
                        return;

                err = sd_journal_stream_fd (identifier, LOG_WARNING, FALSE);
                if (err < 0) {
                        close (out);
                        return;
                }

                VE_IGNORE_EINTR (dup2 (out, 1));
                VE_IGNORE_EINTR (dup2 (err, 2));
                return;
        }
#endif
        return;
}

static void
session_worker_job_child_setup (GdmSessionWorkerJob *session_worker_job)
{
        session_worker_job_setup_journal_fds ();

        /* Terminate the process when the parent dies */
#ifdef HAVE_SYS_PRCTL_H
        prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif
}

static void
session_worker_job_child_watch (GPid                 pid,
                                int                  status,
                                GdmSessionWorkerJob *job)
{
        g_debug ("GdmSessionWorkerJob: child (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_spawn_close_pid (job->priv->pid);
        job->priv->pid = -1;

        if (WIFEXITED (status)) {
                int code = WEXITSTATUS (status);
                g_signal_emit (job, signals [EXITED], 0, code);
        } else if (WIFSIGNALED (status)) {
                int num = WTERMSIG (status);
                g_signal_emit (job, signals [DIED], 0, num);
        }
}

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;

        if (value == NULL)
                value = "";

        str = g_strdup_printf ("%s=%s", key, value);
        g_ptr_array_add (env, str);
}

static void
copy_environment_to_hash (GdmSessionWorkerJob *job,
                          GHashTable          *hash)
{
        char **environment;
        gint   i;

        if (job->priv->environment != NULL) {
                environment = g_strdupv (job->priv->environment);
        } else {
                environment = g_get_environ ();
        }
        for (i = 0; environment[i]; i++) {
                char **parts;

                parts = g_strsplit (environment[i], "=", 2);

                if (parts[0] != NULL && parts[1] != NULL) {
                        g_hash_table_insert (hash, g_strdup (parts[0]), g_strdup (parts[1]));
                }

                g_strfreev (parts);
        }

        g_strfreev (environment);
}

static GPtrArray *
get_job_arguments (GdmSessionWorkerJob *job,
                   const char          *name)
{
        GPtrArray  *args;
        GError     *error;
        char      **argv;
        int         i;

        args = NULL;
        argv = NULL;
        error = NULL;
        if (! g_shell_parse_argv (job->priv->command, NULL, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                goto out;
        }

        args = g_ptr_array_new ();
        g_ptr_array_add (args, g_strdup (argv[0]));
        g_ptr_array_add (args, g_strdup (name));
        for (i = 1; argv[i] != NULL; i++) {
                g_ptr_array_add (args, g_strdup (argv[i]));
        }
        g_strfreev (argv);

        g_ptr_array_add (args, NULL);
out:
        return args;
}

static GPtrArray *
get_job_environment (GdmSessionWorkerJob *job)
{
        GPtrArray     *env;
        GHashTable    *hash;

        env = g_ptr_array_new ();

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        copy_environment_to_hash (job, hash);

        g_hash_table_insert (hash, g_strdup ("GDM_SESSION_DBUS_ADDRESS"), g_strdup (job->priv->server_address));

        if (job->priv->for_reauth) {
                g_hash_table_insert (hash, g_strdup ("GDM_SESSION_FOR_REAUTH"), g_strdup ("1"));
        }

        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

static gboolean
gdm_session_worker_job_spawn (GdmSessionWorkerJob *session_worker_job,
                              const char          *name)
{
        GError          *error;
        gboolean         ret;
        GPtrArray       *args;
        GPtrArray       *env;

        ret = FALSE;

        g_debug ("GdmSessionWorkerJob: Running session_worker_job process: %s %s",
                 name != NULL? name : "", session_worker_job->priv->command);

        args = get_job_arguments (session_worker_job, name);

        if (args == NULL) {
                return FALSE;
        }
        env = get_job_environment (session_worker_job);

        error = NULL;
        ret = g_spawn_async_with_pipes (NULL,
                                        (char **) args->pdata,
                                        (char **)env->pdata,
                                        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_FILE_AND_ARGV_ZERO,
                                        (GSpawnChildSetupFunc)session_worker_job_child_setup,
                                        session_worker_job,
                                        &session_worker_job->priv->pid,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &error);

        g_ptr_array_foreach (args, (GFunc)g_free, NULL);
        g_ptr_array_free (args, TRUE);

        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

        if (! ret) {
                g_warning ("Could not start command '%s': %s",
                           session_worker_job->priv->command,
                           error->message);
                g_error_free (error);
        } else {
                g_debug ("GdmSessionWorkerJob: : SessionWorkerJob on pid %d", (int)session_worker_job->priv->pid);
        }

        session_worker_job->priv->child_watch_id = g_child_watch_add (session_worker_job->priv->pid,
                                                                      (GChildWatchFunc)session_worker_job_child_watch,
                                                                      session_worker_job);

        return ret;
}

/**
 * gdm_session_worker_job_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X session_worker_job. Handles retries and fatal errors properly.
 */
gboolean
gdm_session_worker_job_start (GdmSessionWorkerJob *session_worker_job,
                              const char          *name)
{
        gboolean    res;

        g_debug ("GdmSessionWorkerJob: Starting worker...");

        res = gdm_session_worker_job_spawn (session_worker_job, name);

        return res;
}

static void
handle_session_worker_job_death (GdmSessionWorkerJob *session_worker_job)
{
        int exit_status;

        g_debug ("GdmSessionWorkerJob: Waiting on process %d", session_worker_job->priv->pid);
        exit_status = gdm_wait_on_and_disown_pid (session_worker_job->priv->pid, 5);

        if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                g_debug ("GdmSessionWorkerJob: Wait on child process failed");
        } else {
                /* exited normally */
        }

        g_spawn_close_pid (session_worker_job->priv->pid);
        session_worker_job->priv->pid = -1;

        g_debug ("GdmSessionWorkerJob: SessionWorkerJob died");
}

void
gdm_session_worker_job_stop_now (GdmSessionWorkerJob *session_worker_job)
{
        if (session_worker_job->priv->pid <= 1) {
                return;
        }

        /* remove watch source before we can wait on child */
        if (session_worker_job->priv->child_watch_id > 0) {
                g_source_remove (session_worker_job->priv->child_watch_id);
                session_worker_job->priv->child_watch_id = 0;
        }

        gdm_session_worker_job_stop (session_worker_job);
        handle_session_worker_job_death (session_worker_job);
}

void
gdm_session_worker_job_stop (GdmSessionWorkerJob *session_worker_job)
{
        int res;

        if (session_worker_job->priv->pid <= 1) {
                return;
        }

        g_debug ("GdmSessionWorkerJob: Stopping job pid:%d", session_worker_job->priv->pid);

        res = gdm_signal_pid (session_worker_job->priv->pid, SIGTERM);

        if (res < 0) {
                g_warning ("Unable to kill session worker process");
        }
}

GPid
gdm_session_worker_job_get_pid (GdmSessionWorkerJob *session_worker_job)
{
        g_return_val_if_fail (GDM_IS_SESSION_WORKER_JOB (session_worker_job), 0);
        return session_worker_job->priv->pid;
}

void
gdm_session_worker_job_set_server_address (GdmSessionWorkerJob *session_worker_job,
                                           const char      *address)
{
        g_return_if_fail (GDM_IS_SESSION_WORKER_JOB (session_worker_job));

        g_free (session_worker_job->priv->server_address);
        session_worker_job->priv->server_address = g_strdup (address);
}

void
gdm_session_worker_job_set_for_reauth (GdmSessionWorkerJob *session_worker_job,
                                       gboolean             for_reauth)
{
        g_return_if_fail (GDM_IS_SESSION_WORKER_JOB (session_worker_job));

        session_worker_job->priv->for_reauth = for_reauth;
}

void
gdm_session_worker_job_set_environment (GdmSessionWorkerJob *session_worker_job,
                                        const char * const  *environment)
{
        g_return_if_fail (GDM_IS_SESSION_WORKER_JOB (session_worker_job));

        session_worker_job->priv->environment = g_strdupv ((char **) environment);
}

static void
gdm_session_worker_job_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
        GdmSessionWorkerJob *self;

        self = GDM_SESSION_WORKER_JOB (object);

        switch (prop_id) {
        case PROP_SERVER_ADDRESS:
                gdm_session_worker_job_set_server_address (self, g_value_get_string (value));
                break;
        case PROP_FOR_REAUTH:
                gdm_session_worker_job_set_for_reauth (self, g_value_get_boolean (value));
                break;
        case PROP_ENVIRONMENT:
                gdm_session_worker_job_set_environment (self, g_value_get_pointer (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_worker_job_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
        GdmSessionWorkerJob *self;

        self = GDM_SESSION_WORKER_JOB (object);

        switch (prop_id) {
        case PROP_SERVER_ADDRESS:
                g_value_set_string (value, self->priv->server_address);
                break;
        case PROP_FOR_REAUTH:
                g_value_set_boolean (value, self->priv->for_reauth);
                break;
        case PROP_ENVIRONMENT:
                g_value_set_pointer (value, self->priv->environment);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_session_worker_job_constructor (GType                  type,
                                    guint                  n_construct_properties,
                                    GObjectConstructParam *construct_properties)
{
        GdmSessionWorkerJob      *session_worker_job;

        session_worker_job = GDM_SESSION_WORKER_JOB (G_OBJECT_CLASS (gdm_session_worker_job_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));

        return G_OBJECT (session_worker_job);
}

static void
gdm_session_worker_job_class_init (GdmSessionWorkerJobClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_worker_job_get_property;
        object_class->set_property = gdm_session_worker_job_set_property;
        object_class->constructor = gdm_session_worker_job_constructor;
        object_class->finalize = gdm_session_worker_job_finalize;

        g_type_class_add_private (klass, sizeof (GdmSessionWorkerJobPrivate));

        g_object_class_install_property (object_class,
                                         PROP_SERVER_ADDRESS,
                                         g_param_spec_string ("server-address",
                                                              "server address",
                                                              "server address",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_FOR_REAUTH,
                                         g_param_spec_boolean ("for-reauth",
                                                               "for reauth",
                                                               "for reauth",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_ENVIRONMENT,
                                         g_param_spec_pointer ("environment",
                                                               "environment",
                                                               "environment",
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals [STARTED] =
                g_signal_new ("started",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionWorkerJobClass, started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionWorkerJobClass, exited),
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
                              G_STRUCT_OFFSET (GdmSessionWorkerJobClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
}

static void
gdm_session_worker_job_init (GdmSessionWorkerJob *session_worker_job)
{

        session_worker_job->priv = GDM_SESSION_WORKER_JOB_GET_PRIVATE (session_worker_job);

        session_worker_job->priv->pid = -1;

        session_worker_job->priv->command = g_strdup (LIBEXECDIR "/gdm-session-worker");
}

static void
gdm_session_worker_job_finalize (GObject *object)
{
        GdmSessionWorkerJob *session_worker_job;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_WORKER_JOB (object));

        session_worker_job = GDM_SESSION_WORKER_JOB (object);

        g_return_if_fail (session_worker_job->priv != NULL);

        gdm_session_worker_job_stop (session_worker_job);

        g_free (session_worker_job->priv->command);
        g_free (session_worker_job->priv->server_address);

        G_OBJECT_CLASS (gdm_session_worker_job_parent_class)->finalize (object);
}

GdmSessionWorkerJob *
gdm_session_worker_job_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_WORKER_JOB,
                               NULL);

        return GDM_SESSION_WORKER_JOB (object);
}
