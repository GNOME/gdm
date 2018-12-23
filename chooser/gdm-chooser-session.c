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
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-chooser-session.h"
#include "gdm-client.h"

#include "gdm-host-chooser-dialog.h"

struct _GdmChooserSession
{
        GObject                parent;

        GdmClient             *client;
        GdmRemoteGreeter      *remote_greeter;
        GdmChooser            *chooser;
        GtkWidget             *chooser_dialog;
};

enum {
        PROP_0,
};

static void     gdm_chooser_session_class_init  (GdmChooserSessionClass *klass);
static void     gdm_chooser_session_init        (GdmChooserSession      *chooser_session);
static void     gdm_chooser_session_finalize    (GObject                *object);

G_DEFINE_TYPE (GdmChooserSession, gdm_chooser_session, G_TYPE_OBJECT)

static gpointer session_object = NULL;

static gboolean
launch_compiz (GdmChooserSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmChooserSession: Launching compiz");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("gtk-window-decorator --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        error = NULL;
        g_spawn_command_line_async ("compiz --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

        /* FIXME: should try to detect if it actually works */

 out:
        return ret;
}

static gboolean
launch_metacity (GdmChooserSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmChooserSession: Launching metacity");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("metacity --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
start_window_manager (GdmChooserSession *session)
{
        if (! launch_metacity (session)) {
                launch_compiz (session);
        }
}

static gboolean
start_settings_daemon (GdmChooserSession *session)
{
        GError  *error;
        gboolean ret;

        g_debug ("GdmChooserSession: Launching settings daemon");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async (GNOME_SETTINGS_DAEMON_DIR "/gnome-settings-daemon", &error);
        if (error != NULL) {
                g_warning ("Error starting settings daemon: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
on_dialog_response (GtkDialog         *dialog,
                    int                response_id,
                    GdmChooserSession *session)
{
        GdmChooserHost *host;
        GError *error = NULL;

        host = NULL;
        switch (response_id) {
        case GTK_RESPONSE_OK:
                host = gdm_host_chooser_dialog_get_host (GDM_HOST_CHOOSER_DIALOG (dialog));
        case GTK_RESPONSE_NONE:
                /* delete event */
        default:
                break;
        }

        if (host != NULL) {
                char *hostname;

                /* only support XDMCP hosts in remote chooser */
                g_assert (gdm_chooser_host_get_kind (host) == GDM_CHOOSER_HOST_KIND_XDMCP);

                hostname = NULL;
                gdm_address_get_hostname (gdm_chooser_host_get_address (host), &hostname);
                /* FIXME: fall back to numerical address? */
                if (hostname != NULL) {
                        g_debug ("GdmChooserSession: Selected hostname '%s'", hostname);
                        gdm_chooser_call_select_hostname_sync (session->chooser,
                                                               hostname,
                                                               NULL,
                                                               &error);

                        if (error != NULL) {
                                g_debug ("GdmChooserSession: %s", error->message);
                                g_clear_error (&error);
                        }
                        g_free (hostname);
                }
        }

        gdm_remote_greeter_call_disconnect_sync (session->remote_greeter,
                                                 NULL,
                                                 &error);
        if (error != NULL) {
                g_debug ("GdmChooserSession: disconnect failed: %s", error->message);
        }
}

gboolean
gdm_chooser_session_start (GdmChooserSession *session,
                           GError           **error)
{
        g_return_val_if_fail (GDM_IS_CHOOSER_SESSION (session), FALSE);

        session->remote_greeter = gdm_client_get_remote_greeter_sync (session->client,
                                                                            NULL,
                                                                            error);
        if (session->remote_greeter == NULL) {
                return FALSE;
        }

        session->chooser = gdm_client_get_chooser_sync (session->client,
                                                              NULL,
                                                              error);
        if (session->chooser == NULL) {
                return FALSE;
        }

        start_settings_daemon (session);
        start_window_manager (session);

        /* Only support XDMCP on remote choosers */
        session->chooser_dialog = gdm_host_chooser_dialog_new (GDM_CHOOSER_HOST_KIND_XDMCP);
        g_signal_connect (session->chooser_dialog,
                          "response",
                          G_CALLBACK (on_dialog_response),
                          session);
        gtk_widget_show (session->chooser_dialog);

        return TRUE;
}

void
gdm_chooser_session_stop (GdmChooserSession *session)
{
        g_return_if_fail (GDM_IS_CHOOSER_SESSION (session));

}

static void
gdm_chooser_session_set_property (GObject        *object,
                                  guint           prop_id,
                                  const GValue   *value,
                                  GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_chooser_session_get_property (GObject        *object,
                                  guint           prop_id,
                                  GValue         *value,
                                  GParamSpec     *pspec)
{
        switch (prop_id) {
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

        chooser_session = GDM_CHOOSER_SESSION (G_OBJECT_CLASS (gdm_chooser_session_parent_class)->constructor (type,
                                                                                                               n_construct_properties,
                                                                                                               construct_properties));

        return G_OBJECT (chooser_session);
}

static void
gdm_chooser_session_dispose (GObject *object)
{
        g_debug ("GdmChooserSession: Disposing chooser_session");

        G_OBJECT_CLASS (gdm_chooser_session_parent_class)->dispose (object);
}

static void
gdm_chooser_session_class_init (GdmChooserSessionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_chooser_session_get_property;
        object_class->set_property = gdm_chooser_session_set_property;
        object_class->constructor = gdm_chooser_session_constructor;
        object_class->dispose = gdm_chooser_session_dispose;
        object_class->finalize = gdm_chooser_session_finalize;
}

static void
gdm_chooser_session_init (GdmChooserSession *session)
{
        session->client = gdm_client_new ();
}

static void
gdm_chooser_session_finalize (GObject *object)
{
        GdmChooserSession *chooser_session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_CHOOSER_SESSION (object));

        chooser_session = GDM_CHOOSER_SESSION (object);

        g_return_if_fail (chooser_session != NULL);

        g_clear_object (&chooser_session->chooser);
        g_clear_object (&chooser_session->remote_greeter);
        g_clear_object (&chooser_session->client);

        G_OBJECT_CLASS (gdm_chooser_session_parent_class)->finalize (object);
}

GdmChooserSession *
gdm_chooser_session_new (void)
{
        if (session_object != NULL) {
                g_object_ref (session_object);
        } else {
                session_object = g_object_new (GDM_TYPE_CHOOSER_SESSION, NULL);
                g_object_add_weak_pointer (session_object,
                                           (gpointer *) &session_object);
        }

        return GDM_CHOOSER_SESSION (session_object);
}
