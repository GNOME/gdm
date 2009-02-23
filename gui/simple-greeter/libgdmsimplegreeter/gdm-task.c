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

#include "gdm-task.h"

enum {
        ENABLED,
        DISABLED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };
static void gdm_task_class_init (gpointer g_iface);

GType
gdm_task_get_type (void)
{
        static GType task_type = 0;

        if (!task_type) {
                task_type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                           "GdmTask",
                                                           sizeof (GdmTaskIface),
                                                           (GClassInitFunc) gdm_task_class_init,
                                                           0, NULL, 0);

                g_type_interface_add_prerequisite (task_type, G_TYPE_OBJECT);
        }

        return task_type;
}

GIcon *
gdm_task_get_icon (GdmTask *task)
{
        return GDM_TASK_GET_IFACE (task)->get_icon (task);
}

char *
gdm_task_get_description (GdmTask *task)
{
        return GDM_TASK_GET_IFACE (task)->get_description (task);
}

char *
gdm_task_get_name (GdmTask *task)
{
        return GDM_TASK_GET_IFACE (task)->get_name (task);
}

void
gdm_task_set_enabled (GdmTask   *task,
                      gboolean   should_enable)
{
        g_object_set_data (G_OBJECT (task), "gdm-task-is-disabled", GINT_TO_POINTER (!should_enable));

        if (should_enable) {
                g_signal_emit (G_OBJECT (task), signals [ENABLED], 0);
        } else {
                g_signal_emit (G_OBJECT (task), signals [DISABLED], 0);
        }
}

gboolean
gdm_task_is_enabled (GdmTask   *task)
{
        return !g_object_get_data (G_OBJECT (task), "gdm-task-is-disabled");
}

gboolean
gdm_task_is_choosable (GdmTask *task)
{
        return GDM_TASK_GET_IFACE (task)->is_choosable (task);
}

static void
gdm_task_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [ENABLED] =
                g_signal_new ("enabled",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmTaskIface, enabled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [DISABLED] =
                g_signal_new ("disabled",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmTaskIface, disabled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
}
