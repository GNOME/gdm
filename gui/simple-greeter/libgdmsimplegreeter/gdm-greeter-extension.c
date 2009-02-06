/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Written By: Ray Strode <rstrode@redhat.com>
 *
 */

#include <glib.h>
#include <glib-object.h>

#include "gdm-greeter-extension.h"

enum {
        LOADED,
        LOAD_FAILED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void gdm_greeter_extension_class_init (gpointer g_iface);

GType
gdm_greeter_extension_get_type (void)
{
        static GType greeter_extension_type = 0;

        if (!greeter_extension_type) {
                greeter_extension_type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                           "GdmGreeterExtension",
                                                           sizeof (GdmGreeterExtensionIface),
                                                           (GClassInitFunc) gdm_greeter_extension_class_init,
                                                           0, NULL, 0);

                g_type_interface_add_prerequisite (greeter_extension_type, G_TYPE_OBJECT);
        }

        return greeter_extension_type;
}

static void
gdm_greeter_extension_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [LOADED] =
                g_signal_new ("loaded",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterExtensionIface, loaded),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [LOADED] =
                g_signal_new ("load_failed",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmGreeterExtensionIface, load_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
}

void
gdm_greeter_extension_loaded (GdmGreeterExtension *extension)
{
        g_signal_emit (extension, signals [LOADED], 0);
}

void
gdm_greeter_extension_load_failed (GdmGreeterExtension *extension,
                                   GError              *error)
{
        g_signal_emit (extension, signals [LOAD_FAILED], 0, error);
}
