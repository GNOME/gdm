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

struct _GdmRemoteDisplay
{
        GdmDisplay            parent;

        GdmDBusRemoteDisplay *skeleton;
};

static void     gdm_remote_display_class_init   (GdmRemoteDisplayClass *klass);
static void     gdm_remote_display_init         (GdmRemoteDisplay      *remote_display);

G_DEFINE_TYPE (GdmRemoteDisplay, gdm_remote_display, GDM_TYPE_DISPLAY)

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

        g_clear_object (&display->skeleton);

        G_OBJECT_CLASS (gdm_remote_display_parent_class)->finalize (object);
}

static gboolean
gdm_remote_display_prepare (GdmDisplay *display)
{
        GdmRemoteDisplay *self = GDM_REMOTE_DISPLAY (display);
        g_autoptr (GdmLaunchEnvironment) launch_environment = NULL;
        g_autofree char *session_type = NULL;

        g_object_get (self,
                      "session-type", &session_type,
                      NULL);

        launch_environment = gdm_create_greeter_launch_environment (NULL,
                                                                    NULL,
                                                                    session_type,
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
}

static void
gdm_remote_display_init (GdmRemoteDisplay *remote_display)
{
}

GdmDisplay *
gdm_remote_display_new (const char *remote_id)
{
        GObject *object;
        GdmRemoteDisplay *self;

        const char *session_types[] = { "wayland", NULL };

        object = g_object_new (GDM_TYPE_REMOTE_DISPLAY,
                               "is-local", FALSE,
                               "session-type", session_types[0],
                               "supported-session-types", session_types,
                               NULL);

        self = GDM_REMOTE_DISPLAY (object);
        g_object_set (G_OBJECT (self->skeleton), "remote-id", remote_id, NULL);

        return GDM_DISPLAY (object);
}

