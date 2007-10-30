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
};

enum {
        PROP_0,
        PROP_DESKTOP_FILE,
};

static void     gdm_session_client_class_init  (GdmSessionClientClass *klass);
static void     gdm_session_client_init        (GdmSessionClient      *session_client);
static void     gdm_session_client_finalize    (GObject               *object);

G_DEFINE_TYPE (GdmSessionClient, gdm_session_client, G_TYPE_OBJECT)

gboolean
gdm_session_client_start (GdmSessionClient *client,
                          GError          **error)
{
        GError     *local_error;
        char      **argv;
        gboolean    res;
        gboolean    ret;

        g_return_val_if_fail (GDM_IS_SESSION_CLIENT (client), FALSE);

        g_debug ("GdmSessionClient: Starting client: %s", client->priv->name);

        ret = FALSE;

        argv = NULL;
        local_error = NULL;
        res = g_shell_parse_argv (client->priv->command, NULL, &argv, &local_error);
        if (! res) {
                g_warning ("Unable to parse command: %s", local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        local_error = NULL;
        res = g_spawn_async (NULL,
                             argv,
                             NULL,
                             G_SPAWN_SEARCH_PATH
                             | G_SPAWN_STDOUT_TO_DEV_NULL
                             | G_SPAWN_STDERR_TO_DEV_NULL,
                             NULL,
                             NULL,
                             &client->priv->pid,
                             &local_error);
        g_strfreev (argv);

        if (! res) {
                g_warning ("Unable to run command %s: %s",
                           client->priv->command,
                           local_error->message);
                g_propagate_error (error, local_error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

void
gdm_session_client_stop (GdmSessionClient *client)
{
        g_return_if_fail (GDM_IS_SESSION_CLIENT (client));

        g_debug ("GdmSessionClient: Stopping client: %s", client->priv->name);
        if (client->priv->pid > 0) {
                gdm_signal_pid (client->priv->pid, SIGTERM);
                client->priv->pid = 0;
        }
}

static void
_gdm_session_client_set_desktop_file (GdmSessionClient *client,
                                      const char       *file)
{
        g_free (client->priv->desktop_filename);
        client->priv->desktop_filename = g_strdup (file);
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
        gboolean res;

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


 out:
        g_key_file_free (keyfile);
}

static GObject *
gdm_session_client_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmSessionClient      *client;
        GdmSessionClientClass *klass;

        klass = GDM_SESSION_CLIENT_CLASS (g_type_class_peek (GDM_TYPE_SESSION_CLIENT));

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
        GdmSessionClient *session_client;

        session_client = GDM_SESSION_CLIENT (object);

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
                                         PROP_DESKTOP_FILE,
                                         g_param_spec_string ("desktop-file",
                                                              "desktop file",
                                                              "desktop file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
