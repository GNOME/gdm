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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-session-client.h"
#include "gdm-common.h"

#define GDM_SESSION_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_CLIENT, GdmSessionClientPrivate))

struct GdmSessionClientPrivate
{
        char    *desktop_filename;
        guint    priority;
        char    *name;
        char    *command;
        char    *try_exec;
        gboolean enabled;
        GPid     pid;
        guint    child_watch_id;
};

enum {
        PROP_0,
        PROP_DESKTOP_FILE,
        PROP_ENABLED,
};

enum {
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_session_client_class_init  (GdmSessionClientClass *klass);
static void     gdm_session_client_init        (GdmSessionClient      *session_client);
static void     gdm_session_client_finalize    (GObject               *object);

G_DEFINE_TYPE (GdmSessionClient, gdm_session_client, G_TYPE_OBJECT)

static void
client_child_watch (GPid              pid,
                    int               status,
                    GdmSessionClient *client)
{
        g_debug ("GdmSessionClient: **** child '%s' (pid:%d) done (%s:%d)",
                 client->priv->name,
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        if (WIFEXITED (status)) {
                int code = WEXITSTATUS (status);
                g_signal_emit (client, signals [EXITED], 0, code);
        } else if (WIFSIGNALED (status)) {
                int num = WTERMSIG (status);
                g_signal_emit (client, signals [DIED], 0, num);
        }

        g_spawn_close_pid (client->priv->pid);

        client->priv->pid = -1;
        client->priv->child_watch_id = 0;
}

gboolean
gdm_session_client_start (GdmSessionClient *client,
                          GError          **error)
{
        GError     *local_error;
        char      **argv;
        gboolean    res;
        gboolean    ret;
        int         flags;

        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), FALSE);

        g_debug ("GdmSessionClient: Starting client: %s", client->priv->name);

        ret = FALSE;

        argv = NULL;
        local_error = NULL;
        res = g_shell_parse_argv (client->priv->command, NULL, &argv, &local_error);
        if (! res) {
                g_warning ("GdmSessionClient: Unable to parse command: %s", local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        flags = G_SPAWN_SEARCH_PATH
                | G_SPAWN_DO_NOT_REAP_CHILD;

        if (! gdm_is_version_unstable ()) {
                flags |= G_SPAWN_STDOUT_TO_DEV_NULL
                        | G_SPAWN_STDERR_TO_DEV_NULL;
        }

        local_error = NULL;
        res = g_spawn_async (NULL,
                             argv,
                             NULL,
                             flags,
                             NULL,
                             NULL,
                             &client->priv->pid,
                             &local_error);
        g_strfreev (argv);

        if (! res) {
                g_warning ("GdmSessionClient: Unable to run command %s: %s",
                           client->priv->command,
                           local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        g_debug ("GdmSessionClient: Started: pid=%d command='%s'", client->priv->pid, client->priv->command);

        client->priv->child_watch_id = g_child_watch_add (client->priv->pid,
                                                          (GChildWatchFunc)client_child_watch,
                                                          client);

        ret = TRUE;

 out:
        return ret;
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
                        g_debug ("GdmSessionClient: waitpid () should not fail");
                }
        }

        return status;
}

static void
client_died (GdmSessionClient *client)
{
        int exit_status;

        g_debug ("GdmSessionClient: Waiting on process %d", client->priv->pid);
        exit_status = wait_on_child (client->priv->pid);

        if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                g_debug ("GdmSessionClient: Wait on child process failed");
        } else {
                /* exited normally */
        }

        g_spawn_close_pid (client->priv->pid);
        client->priv->pid = -1;

        g_debug ("GdmSessionClient: SessionClient died");
}

void
gdm_session_client_stop (GdmSessionClient *client)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        /* remove watch before killing so we don't restart */
        if (client->priv->child_watch_id > 0) {
                g_source_remove (client->priv->child_watch_id);
                client->priv->child_watch_id = 0;
        }

        g_debug ("GdmSessionClient: Stopping client: %s", client->priv->name);
        if (client->priv->pid > 0) {
                gdm_signal_pid (client->priv->pid, SIGTERM);
                client_died (client);
        }
}

static void
_gdm_session_client_set_desktop_file (GdmSessionClient *client,
                                      const char       *file)
{
        g_free (client->priv->desktop_filename);
        client->priv->desktop_filename = g_strdup (file);
}

gboolean
gdm_session_client_get_enabled (GdmSessionClient *client)
{
        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), FALSE);

        return client->priv->enabled;
}

void
gdm_session_client_set_enabled (GdmSessionClient *client,
                                gboolean          enabled)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        if (enabled != client->priv->enabled) {
                client->priv->enabled = enabled;
                g_object_notify (G_OBJECT (client), "enabled");
        }
}

const char *
gdm_session_client_get_name (GdmSessionClient *client)
{
        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), NULL);

        return client->priv->name;
}

void
gdm_session_client_set_name (GdmSessionClient *client,
                             const char       *name)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        g_free (client->priv->name);
        client->priv->name = g_strdup (name);
}

const char *
gdm_session_client_get_command (GdmSessionClient *client)
{
        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), NULL);

        return client->priv->command;
}

void
gdm_session_client_set_command (GdmSessionClient *client,
                                const char       *name)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        g_free (client->priv->command);
        client->priv->command = g_strdup (name);
}

const char *
gdm_session_client_get_try_exec (GdmSessionClient *client)
{
        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), NULL);

        return client->priv->try_exec;
}

void
gdm_session_client_set_try_exec (GdmSessionClient *client,
                                 const char       *name)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        g_free (client->priv->try_exec);
        client->priv->try_exec = g_strdup (name);
}

guint
gdm_session_client_get_priority (GdmSessionClient *client)
{
        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), 0);

        return client->priv->priority;
}

void
gdm_session_client_set_priority (GdmSessionClient *client,
                                 guint             priority)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        client->priv->priority = priority;
}

static void
gdm_session_client_set_property (GObject        *object,
                                 guint           prop_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
        GdmSessionClient *self;

        self = GDM_SESSION_CLIENT (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILE:
                _gdm_session_client_set_desktop_file (self, g_value_get_string (value));
                break;
        case PROP_ENABLED:
                gdm_session_client_set_enabled (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_client_get_property (GObject        *object,
                                 guint           prop_id,
                                 GValue         *value,
                                 GParamSpec     *pspec)
{
        GdmSessionClient *self;

        self = GDM_SESSION_CLIENT (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILE:
                g_value_set_string (value, self->priv->desktop_filename);
                break;
        case PROP_ENABLED:
                g_value_set_boolean (value, self->priv->enabled);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
load_from_desktop_file (GdmSessionClient *client)
{
        GKeyFile *keyfile;
        GError   *error;
        gboolean  res;

        keyfile = g_key_file_new ();

        error = NULL;
        res = g_key_file_load_from_file (keyfile, client->priv->desktop_filename, G_KEY_FILE_NONE, &error);
        if (! res) {
                g_warning ("Unable to load file %s: %s", client->priv->desktop_filename, error->message);
                g_error_free (error);
                goto out;
        }

        client->priv->name = g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, NULL);
        client->priv->command = g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        client->priv->try_exec = g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL);

        res = g_key_file_get_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL);
        if (res) {
                client->priv->enabled = FALSE;
                goto out;
        }

        client->priv->enabled = TRUE;

 out:
        g_key_file_free (keyfile);
}

static GObject *
gdm_session_client_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmSessionClient      *client;

        client = GDM_SESSION_CLIENT (G_OBJECT_CLASS (gdm_session_client_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

        if (client->priv->desktop_filename != NULL) {
                load_from_desktop_file (client);
        }

        return G_OBJECT (client);
}

static void
gdm_session_client_dispose (GObject *object)
{
        gdm_session_client_stop (GDM_SESSION_CLIENT (object));

        G_OBJECT_CLASS (gdm_session_client_parent_class)->dispose (object);
}

static void
gdm_session_client_class_init (GdmSessionClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_client_get_property;
        object_class->set_property = gdm_session_client_set_property;
        object_class->constructor = gdm_session_client_constructor;
        object_class->dispose = gdm_session_client_dispose;
        object_class->finalize = gdm_session_client_finalize;

        g_object_class_install_property (object_class,
                                         PROP_ENABLED,
                                         g_param_spec_boolean ("enabled",
                                                               "enabled",
                                                               "enabled",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_DESKTOP_FILE,
                                         g_param_spec_string ("desktop-file",
                                                              "desktop file",
                                                              "desktop file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionClientClass, exited),
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
                              G_STRUCT_OFFSET (GdmSessionClientClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);

        g_type_class_add_private (klass, sizeof (GdmSessionClientPrivate));
}

static void
gdm_session_client_init (GdmSessionClient *session)
{

        session->priv = GDM_SESSION_CLIENT_GET_PRIVATE (session);

}

static void
gdm_session_client_finalize (GObject *object)
{
        GdmSessionClient *session_client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_CLIENT (object));

        session_client = GDM_SESSION_CLIENT (object);

        g_return_if_fail (session_client->priv != NULL);

        G_OBJECT_CLASS (gdm_session_client_parent_class)->finalize (object);
}

GdmSessionClient *
gdm_session_client_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_CLIENT, NULL);

        return GDM_SESSION_CLIENT (object);
}

GdmSessionClient *
gdm_session_client_new_from_desktop_file (const char *filename)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_CLIENT,
                               "desktop-file", filename,
                               NULL);

        return GDM_SESSION_CLIENT (object);
}
