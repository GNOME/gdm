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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-launch-environment.h"
#include "gdm-local-display.h"
#include "gdm-local-display-glue.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_LOCAL_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LOCAL_DISPLAY, GdmLocalDisplayPrivate))

struct GdmLocalDisplayPrivate
{
        GdmDBusLocalDisplay *skeleton;
};

static void     gdm_local_display_class_init   (GdmLocalDisplayClass *klass);
static void     gdm_local_display_init         (GdmLocalDisplay      *local_display);

G_DEFINE_TYPE (GdmLocalDisplay, gdm_local_display, GDM_TYPE_DISPLAY)

static GObject *
gdm_local_display_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmLocalDisplay      *display;

        display = GDM_LOCAL_DISPLAY (G_OBJECT_CLASS (gdm_local_display_parent_class)->constructor (type,
                                                                                                   n_construct_properties,
                                                                                                   construct_properties));

        display->priv->skeleton = GDM_DBUS_LOCAL_DISPLAY (gdm_dbus_local_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->priv->skeleton));

        return G_OBJECT (display);
}

static void
gdm_local_display_finalize (GObject *object)
{
        GdmLocalDisplay *display = GDM_LOCAL_DISPLAY (object);

        g_clear_object (&display->priv->skeleton);

        G_OBJECT_CLASS (gdm_local_display_parent_class)->finalize (object);
}

static gboolean
gdm_local_display_prepare (GdmDisplay *display)
{
        GdmLocalDisplay *self = GDM_LOCAL_DISPLAY (display);
        GdmLaunchEnvironment *launch_environment;
        char          *seat_id;
        char          *session_class;
        char          *session_type;
        gboolean       doing_initial_setup = FALSE;
        gboolean       failed = FALSE;

        seat_id = NULL;

        g_object_get (self,
                      "seat-id", &seat_id,
                      "doing-initial-setup", &doing_initial_setup,
                      "session-class", &session_class,
                      "session-type", &session_type,
                      NULL);

        if (g_strcmp0 (session_class, "greeter") != 0) {
                goto out;
        }

        g_debug ("doing initial setup? %s", doing_initial_setup? "yes" : "no");
        if (doing_initial_setup && g_strcmp0 (session_type, "wayland") == 0) {
                g_debug ("initial setup doesn't have a wayland session, failing back to X11");
                failed = TRUE;
                goto out;
        }

        if (!doing_initial_setup) {
                launch_environment = gdm_create_greeter_launch_environment (NULL,
                                                                            seat_id,
                                                                            session_type,
                                                                            NULL,
                                                                            TRUE);
        } else {
                launch_environment = gdm_create_initial_setup_launch_environment (NULL,
                                                                                seat_id,
                                                                                NULL,
                                                                                TRUE);
        }

        g_object_set (self, "launch-environment", launch_environment, NULL);
        g_object_unref (launch_environment);

out:
        g_free (seat_id);
        g_free (session_class);
        g_free (session_type);

        if (failed) {
                return FALSE;
        }
        return GDM_DISPLAY_CLASS (gdm_local_display_parent_class)->prepare (display);
}

static void
gdm_local_display_class_init (GdmLocalDisplayClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->constructor = gdm_local_display_constructor;
        object_class->finalize = gdm_local_display_finalize;

        display_class->prepare = gdm_local_display_prepare;

        g_type_class_add_private (klass, sizeof (GdmLocalDisplayPrivate));
}

static void
gdm_local_display_init (GdmLocalDisplay *local_display)
{

        local_display->priv = GDM_LOCAL_DISPLAY_GET_PRIVATE (local_display);
}

GdmDisplay *
gdm_local_display_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_LOCAL_DISPLAY, NULL);

        return GDM_DISPLAY (object);
}
