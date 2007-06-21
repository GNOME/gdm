/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"

#include "gdm-session-worker-job.h"

#define GDM_SESSION_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/SessionServer"
#define GDM_SESSION_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.SessionServer"

extern char **environ;

#define GDM_SESSION_WORKER_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_WORKER_JOB, GdmSessionWorkerJobPrivate))

struct GdmSessionWorkerJobPrivate
{
	char           *command;
	GPid            pid;

	guint           child_watch_id;

	char           *server_address;
};

enum {
	PROP_0,
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

static void	gdm_session_worker_job_class_init	(GdmSessionWorkerJobClass *klass);
static void	gdm_session_worker_job_init	(GdmSessionWorkerJob      *session_worker_job);
static void	gdm_session_worker_job_finalize	(GObject         *object);

G_DEFINE_TYPE (GdmSessionWorkerJob, gdm_session_worker_job, G_TYPE_OBJECT)

static void
session_worker_job_child_setup (GdmSessionWorkerJob *session_worker_job)
{
}

static void
session_worker_job_child_watch (GPid                 pid,
				int                  status,
				GdmSessionWorkerJob *job)
{
	g_debug ("child (pid:%d) done (%s:%d)",
		 (int) pid,
		 WIFEXITED (status) ? "status"
		 : WIFSIGNALED (status) ? "signal"
		 : "unknown",
		 WIFEXITED (status) ? WEXITSTATUS (status)
		 : WIFSIGNALED (status) ? WTERMSIG (status)
		 : -1);
	if (WIFEXITED (status)) {
		int code = WEXITSTATUS (status);
		g_signal_emit (job, signals [EXITED], 0, code);
	} else if (WIFSIGNALED (status)) {
		int num = WTERMSIG (status);
		g_signal_emit (job, signals [DIED], 0, num);
	}

	g_spawn_close_pid (job->priv->pid);
	job->priv->pid = -1;
}

static void
listify_hash (const char *key,
	      const char *value,
	      GPtrArray  *env)
{
	char *str;
	str = g_strdup_printf ("%s=%s", key, value);
	g_ptr_array_add (env, str);
}

static GPtrArray *
get_job_environment (GdmSessionWorkerJob *job)
{
	GPtrArray     *env;
	GHashTable    *hash;

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	g_hash_table_insert (hash, g_strdup ("GDM_SESSION_DBUS_ADDRESS"), g_strdup (job->priv->server_address));

	g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
	g_hash_table_destroy (hash);

	g_ptr_array_add (env, NULL);

	return env;
}

static gboolean
gdm_session_worker_job_spawn (GdmSessionWorkerJob *session_worker_job)
{
	gchar          **argv;
	GError          *error;
	gboolean         ret;
	GPtrArray       *env;

	ret = FALSE;

	g_debug ("Running session_worker_job process: %s", session_worker_job->priv->command);

	argv = NULL;
	if (! g_shell_parse_argv (session_worker_job->priv->command, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

	env = get_job_environment (session_worker_job);

	error = NULL;
	ret = g_spawn_async_with_pipes (NULL,
					argv,
					(char **)env->pdata,
					G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
					(GSpawnChildSetupFunc)session_worker_job_child_setup,
					session_worker_job,
					&session_worker_job->priv->pid,
					NULL,
					NULL,
					NULL,
					&error);

	g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

	if (! ret) {
		g_warning ("Could not start command '%s': %s",
			   session_worker_job->priv->command,
			   error->message);
		g_error_free (error);
	} else {
		g_debug ("gdm_slave_session_worker_job: SessionWorkerJob on pid %d", (int)session_worker_job->priv->pid);
	}

	session_worker_job->priv->child_watch_id = g_child_watch_add (session_worker_job->priv->pid,
								      (GChildWatchFunc)session_worker_job_child_watch,
								      session_worker_job);

	g_strfreev (argv);
 out:

	return ret;
}

/**
 * gdm_session_worker_job_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X session_worker_job. Handles retries and fatal errors properly.
 */
gboolean
gdm_session_worker_job_start (GdmSessionWorkerJob *session_worker_job)
{
	gboolean    res;

	g_debug ("Starting worker...");

	res = gdm_session_worker_job_spawn (session_worker_job);

	if (res) {

	}


	return res;
}

static int
signal_pid (int pid,
	    int signal)
{
	int status = -1;

	/* perhaps block sigchld */

	status = kill (pid, signal);

	if (status < 0) {
		if (errno == ESRCH) {
			g_warning ("Child process %lu was already dead.",
				   (unsigned long) pid);
		} else {
			g_warning ("Couldn't kill child process %lu: %s",
				   (unsigned long) pid,
				   g_strerror (errno));
		}
	}

	/* perhaps unblock sigchld */

	return status;
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
			g_debug ("waitpid () should not fail");
		}
	}

	return status;
}

static void
session_worker_job_died (GdmSessionWorkerJob *session_worker_job)
{
	int exit_status;

	g_debug ("Waiting on process %d", session_worker_job->priv->pid);
	exit_status = wait_on_child (session_worker_job->priv->pid);

	if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
		g_debug ("Wait on child process failed");
	} else {
		/* exited normally */
	}

	g_spawn_close_pid (session_worker_job->priv->pid);
	session_worker_job->priv->pid = -1;

	g_debug ("SessionWorkerJob died");
}

gboolean
gdm_session_worker_job_stop (GdmSessionWorkerJob *session_worker_job)
{

	if (session_worker_job->priv->pid <= 1) {
		return TRUE;
	}

	/* remove watch source before we can wait on child */
	if (session_worker_job->priv->child_watch_id > 0) {
		g_source_remove (session_worker_job->priv->child_watch_id);
		session_worker_job->priv->child_watch_id = 0;
	}

	g_debug ("Stopping session_worker_job pid:%d", session_worker_job->priv->pid);

	signal_pid (session_worker_job->priv->pid, SIGTERM);
	session_worker_job_died (session_worker_job);

	return TRUE;
}

void
gdm_session_worker_job_set_server_address (GdmSessionWorkerJob *session_worker_job,
					   const char      *address)
{
	g_return_if_fail (GDM_IS_SESSION_WORKER_JOB (session_worker_job));

	g_free (session_worker_job->priv->server_address);
	session_worker_job->priv->server_address = g_strdup (address);
}

static void
gdm_session_worker_job_set_property (GObject      *object,
				     guint	   prop_id,
				     const GValue *value,
				     GParamSpec   *pspec)
{
	GdmSessionWorkerJob *self;

	self = GDM_SESSION_WORKER_JOB (object);

	switch (prop_id) {
	case PROP_SERVER_ADDRESS:
		gdm_session_worker_job_set_server_address (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_session_worker_job_get_property (GObject    *object,
				     guint       prop_id,
				     GValue	*value,
				     GParamSpec *pspec)
{
	GdmSessionWorkerJob *self;

	self = GDM_SESSION_WORKER_JOB (object);

	switch (prop_id) {
	case PROP_SERVER_ADDRESS:
		g_value_set_string (value, self->priv->server_address);
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
        GdmSessionWorkerJobClass *klass;

        klass = GDM_SESSION_WORKER_JOB_CLASS (g_type_class_peek (GDM_TYPE_SESSION_WORKER_JOB));

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
	signals [STOPPED] =
		g_signal_new ("stopped",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionWorkerJobClass, stopped),
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
			      G_TYPE_INT);}

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
