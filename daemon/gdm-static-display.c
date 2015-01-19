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
#include "gdm-static-display.h"
#include "gdm-static-display-glue.h"

#define GDM_STATIC_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_STATIC_DISPLAY, GdmStaticDisplayPrivate))

struct GdmStaticDisplayPrivate
{
        GdmDBusStaticDisplay *skeleton;
};

static void     gdm_static_display_class_init   (GdmStaticDisplayClass *klass);
static void     gdm_static_display_init         (GdmStaticDisplay      *static_display);

G_DEFINE_TYPE (GdmStaticDisplay, gdm_static_display, GDM_TYPE_DISPLAY)

static gboolean
gdm_static_display_finish (GdmDisplay *display)
{
        int status;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        /* Don't call parent's finish since we don't ever
           want to be put in the FINISHED state */

        /* restart static displays */
        gdm_display_unmanage (display);

        status = gdm_display_get_status (display);
        if (status != GDM_DISPLAY_FAILED) {
                gdm_display_manage (display);
        }

        return TRUE;
}

static GObject *
gdm_static_display_constructor (GType                  type,
                                   guint                  n_construct_properties,
                                   GObjectConstructParam *construct_properties)
{
        GdmStaticDisplay      *display;

        display = GDM_STATIC_DISPLAY (G_OBJECT_CLASS (gdm_static_display_parent_class)->constructor (type,
                                                                                                           n_construct_properties,
                                                                                                           construct_properties));

        display->priv->skeleton = GDM_DBUS_STATIC_DISPLAY (gdm_dbus_static_display_skeleton_new ());

        g_dbus_object_skeleton_add_interface (gdm_display_get_object_skeleton (GDM_DISPLAY (display)),
                                              G_DBUS_INTERFACE_SKELETON (display->priv->skeleton));

        return G_OBJECT (display);
}

static void
gdm_static_display_finalize (GObject *object)
{
        GdmStaticDisplay *display = GDM_STATIC_DISPLAY (object);

        g_clear_object (&display->priv->skeleton);

        G_OBJECT_CLASS (gdm_static_display_parent_class)->finalize (object);
}

static void
gdm_static_display_class_init (GdmStaticDisplayClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->constructor = gdm_static_display_constructor;
        object_class->finalize = gdm_static_display_finalize;

        display_class->finish = gdm_static_display_finish;

        g_type_class_add_private (klass, sizeof (GdmStaticDisplayPrivate));
}

static void
gdm_static_display_init (GdmStaticDisplay *static_display)
{

        static_display->priv = GDM_STATIC_DISPLAY_GET_PRIVATE (static_display);
}

GdmDisplay *
gdm_static_display_new (int display_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf (":%d", display_number);
        object = g_object_new (GDM_TYPE_STATIC_DISPLAY,
                               "x11-display-number", display_number,
                               "x11-display-name", x11_display,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
