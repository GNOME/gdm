/*
 * Copyright (C) Red Hat, Inc.
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
 * Written by: Ray Strode <rstrode@redhat.com>
 */


#ifndef __GDM_TASK_H
#define __GDM_TASK_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GDM_TYPE_TASK         (gdm_task_get_type ())
#define GDM_TASK(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_TASK, GdmTask))
#define GDM_TASK_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_TASK, GdmTaskIface))
#define GDM_IS_TASK(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_TASK))
#define GDM_TASK_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GDM_TYPE_TASK, GdmTaskIface))

typedef struct _GdmTask      GdmTask;
typedef struct _GdmTaskIface GdmTaskIface;

struct _GdmTaskIface
{
        GTypeInterface base_iface;

        /* methods */
        GIcon * (* get_icon)        (GdmTask   *task);
        char *  (* get_description) (GdmTask   *task);
        char *  (* get_name)        (GdmTask   *task);
        /* signals */
        void (* enabled) (GdmTask *task);
        void (* disabled) (GdmTask *task);
};

GType  gdm_task_get_type        (void) G_GNUC_CONST;

GIcon *gdm_task_get_icon        (GdmTask   *task);
char  *gdm_task_get_description (GdmTask   *task);
char  *gdm_task_get_name        (GdmTask   *task);
void   gdm_task_set_enabled     (GdmTask   *task,
                                 gboolean   should_enable);
gboolean   gdm_task_is_enabled     (GdmTask   *task);
G_END_DECLS

#endif /* __GDM_TASK_H */
