/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2022 Joan Torres <joan.torres@suse.com>
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

#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-launch-environment.h"
#include "gdm-remote-display.h"
#include "gdm-remote-display-glue.h"

#define REMOTE_DISPLAY_EXPORT_TIMEOUT_SECONDS 3

typedef struct
{
        GdmRemoteDisplay         *display;
        GDBusObjectManagerServer *object_manager;
        guint                     timeout_id;
} PendingExport;

struct _GdmRemoteDisplay
{
        GdmDisplay            parent;

        GdmDBusRemoteDisplay *skeleton;
        PendingExport        *pending_export;
};

static void     gdm_remote_display_class_init   (GdmRemoteDisplayClass *klass);
static void     gdm_remote_display_init         (GdmRemoteDisplay      *remote_display);
static void     on_pending_export_session_id_set (GdmDisplay *display,
                                                  GParamSpec *pspec,
                                                  gpointer    user_data);

G_DEFINE_TYPE (GdmRemoteDisplay, gdm_remote_display, GDM_TYPE_DISPLAY)

static void
pending_export_free (PendingExport *pending_export)
{
        g_signal_handlers_disconnect_by_func (pending_export->display,
                                              on_pending_export_session_id_set,
                                              NULL);

        g_clear_handle_id (&pending_export->timeout_id, g_source_remove);
        g_clear_object (&pending_export->object_manager);
        g_free (pending_export);
}

static void
finish_pending_export (GdmRemoteDisplay *self)
{
        GdmDisplay *display = GDM_DISPLAY (self);
        PendingExport *pending_export = g_steal_pointer (&self->pending_export);
        GDBusObjectManagerServer *object_manager = pending_export->object_manager;

        GDM_DISPLAY_CLASS (gdm_remote_display_parent_class)->export (display, object_manager);

        pending_export_free (pending_export);
}

static void
on_pending_export_session_id_set (GdmDisplay *display,
                                  GParamSpec *pspec,
                                  gpointer    user_data)
{
        if (gdm_display_get_session_id (display) == NULL)
                return;

        finish_pending_export (GDM_REMOTE_DISPLAY (display));
}

static void
on_pending_export_timeout (gpointer user_data)
{
        GdmRemoteDisplay *self = GDM_REMOTE_DISPLAY (user_data);
        g_autofree char *id;

        gdm_display_get_id (GDM_DISPLAY (self), &id, NULL);

        g_debug ("Timeout waiting for session-id to be set on remote display %p, "
                 "exporting anyway", id);

        finish_pending_export (self);
}

static void
gdm_remote_display_export (GdmDisplay               *display,
                           GDBusObjectManagerServer *object_manager)
{
        GdmRemoteDisplay *self = GDM_REMOTE_DISPLAY (display);
        PendingExport *pending_export;

        if (gdm_display_get_session_id (display) != NULL) {
                GDM_DISPLAY_CLASS (gdm_remote_display_parent_class)->export (display, object_manager);
                return;
        }

        pending_export = g_new0 (PendingExport, 1);
        pending_export->display = self;
        pending_export->object_manager = g_object_ref (object_manager);
        g_signal_connect (self, "notify::session-id",
                          G_CALLBACK (on_pending_export_session_id_set),
                          NULL);
        pending_export->timeout_id =
                g_timeout_add_seconds_once (REMOTE_DISPLAY_EXPORT_TIMEOUT_SECONDS,
                                            on_pending_export_timeout,
                                            self);

        self->pending_export = pending_export;
}

char *
gdm_remote_display_get_remote_id (GdmRemoteDisplay *display)
{
        g_autofree char *remote_id = NULL;

        g_return_val_if_fail (GDM_IS_REMOTE_DISPLAY (display), NULL);

        g_object_get (G_OBJECT (display->skeleton),
                      "remote-id", &remote_id,
                      NULL);

        return g_steal_pointer (&remote_id);
}

void
gdm_remote_display_set_remote_id (GdmRemoteDisplay *display,
                                  const char       *remote_id)
{
        g_object_set (G_OBJECT (display->skeleton), "remote-id", remote_id, NULL);
}

static GObject *
gdm_remote_display_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmRemoteDisplay *display;

        display = GDM_REMOTE_DISPLAY (G_OBJECT_CLASS (gdm_remote_display_parent_class)->constructor (type,
                                                                                                     n_construct_properties,
                                                                                                     construct_properties));

        display->skeleton = GDM_DBUS_REMOTE_DISPLAY (gdm_dbus_remote_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->skeleton));

        g_object_bind_property (display, "session-id", display->skeleton, "session-id", G_BINDING_SYNC_CREATE);

        return G_OBJECT (display);
}

static void
gdm_remote_display_finalize (GObject *object)
{
        GdmRemoteDisplay *display = GDM_REMOTE_DISPLAY (object);

        g_clear_pointer (&display->pending_export, pending_export_free);
        g_clear_object (&display->skeleton);

        G_OBJECT_CLASS (gdm_remote_display_parent_class)->finalize (object);
}

static gboolean
gdm_remote_display_prepare (GdmDisplay *display)
{
        GdmRemoteDisplay *self = GDM_REMOTE_DISPLAY (display);
        g_autoptr (GdmLaunchEnvironment) launch_environment = NULL;

        launch_environment = gdm_create_greeter_launch_environment (NULL,
                                                                    NULL,
                                                                    FALSE);

        g_object_set (self, "launch-environment", launch_environment, NULL);

        return GDM_DISPLAY_CLASS (gdm_remote_display_parent_class)->prepare (display);
}

static void
gdm_remote_display_class_init (GdmRemoteDisplayClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->constructor = gdm_remote_display_constructor;
        object_class->finalize = gdm_remote_display_finalize;

        display_class->prepare = gdm_remote_display_prepare;
        display_class->export = gdm_remote_display_export;
}

static void
gdm_remote_display_init (GdmRemoteDisplay *remote_display)
{
}

GdmDisplay *
gdm_remote_display_new (const char *remote_id,
                        const char *remote_hostname)
{
        GObject *object;
        GdmRemoteDisplay *self;

        const char *session_types[] = { "wayland", NULL };

        object = g_object_new (GDM_TYPE_REMOTE_DISPLAY,
                               "is-local", FALSE,
                               "supported-session-types", session_types,
                               "remote-hostname", remote_hostname,
                               NULL);

        self = GDM_REMOTE_DISPLAY (object);
        g_object_set (G_OBJECT (self->skeleton), "remote-id", remote_id, NULL);

        return GDM_DISPLAY (object);
}

