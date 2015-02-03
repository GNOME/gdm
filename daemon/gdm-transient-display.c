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
#include <gio/gio.h>

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-transient-display.h"
#include "gdm-transient-display-glue.h"

#define GDM_TRANSIENT_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_TRANSIENT_DISPLAY, GdmTransientDisplayPrivate))

struct GdmTransientDisplayPrivate
{
        GdmDBusTransientDisplay *skeleton;
};

static void     gdm_transient_display_class_init   (GdmTransientDisplayClass *klass);
static void     gdm_transient_display_init         (GdmTransientDisplay      *display);

G_DEFINE_TYPE (GdmTransientDisplay, gdm_transient_display, GDM_TYPE_DISPLAY)

static GObject *
gdm_transient_display_constructor (GType                  type,
                                   guint                  n_construct_properties,
                                   GObjectConstructParam *construct_properties)
{
        GdmTransientDisplay      *display;

        display = GDM_TRANSIENT_DISPLAY (G_OBJECT_CLASS (gdm_transient_display_parent_class)->constructor (type,
                                                                                                           n_construct_properties,
                                                                                                           construct_properties));

        display->priv->skeleton = GDM_DBUS_TRANSIENT_DISPLAY (gdm_dbus_transient_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->priv->skeleton));

        return G_OBJECT (display);
}

static void
gdm_transient_display_finalize (GObject *object)
{
        GdmTransientDisplay *display = GDM_TRANSIENT_DISPLAY (object);

        g_clear_object (&display->priv->skeleton);

        G_OBJECT_CLASS (gdm_transient_display_parent_class)->finalize (object);
}

static void
gdm_transient_display_class_init (GdmTransientDisplayClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gdm_transient_display_constructor;
        object_class->finalize = gdm_transient_display_finalize;

        g_type_class_add_private (klass, sizeof (GdmTransientDisplayPrivate));
}

static void
gdm_transient_display_init (GdmTransientDisplay *display)
{

        display->priv = GDM_TRANSIENT_DISPLAY_GET_PRIVATE (display);
}

GdmDisplay *
gdm_transient_display_new (int display_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf (":%d", display_number);
        object = g_object_new (GDM_TYPE_TRANSIENT_DISPLAY,
                               "x11-display-number", display_number,
                               "x11-display-name", x11_display,
                               "allow-timed-login", FALSE,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
