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
 *  Written by: Ray Strode <rstrode@redhat.com>
 */

#ifndef __GDM_TASK_LIST_H
#define __GDM_TASK_LIST_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtkalignment.h>

#include "gdm-task.h"

G_BEGIN_DECLS

#define GDM_TYPE_TASK_LIST         (gdm_task_list_get_type ())
#define GDM_TASK_LIST(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_TASK_LIST, GdmTaskList))
#define GDM_TASK_LIST_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_TASK_LIST, GdmTaskListClass))
#define GDM_IS_TASK_LIST(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_TASK_LIST))
#define GDM_IS_TASK_LIST_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_TASK_LIST))
#define GDM_TASK_LIST_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_TASK_LIST, GdmTaskListClass))

typedef struct GdmTaskListPrivate GdmTaskListPrivate;
typedef struct _GdmTaskList GdmTaskList;

typedef gboolean (* GdmTaskListForeachFunc) (GdmTaskList *task_list,
                                            GdmTask     *task,
                                            gpointer     data);

struct _GdmTaskList
{
        GtkAlignment             parent;
        GdmTaskListPrivate *priv;
};

typedef struct
{
        GtkAlignmentClass       parent_class;

        void (* deactivated)      (GdmTaskList *widget,
                                   GdmTask     *task);
        void (* activated)      (GdmTaskList *widget,
                                 GdmTask     *task);
} GdmTaskListClass;

GType       gdm_task_list_get_type               (void);
GtkWidget * gdm_task_list_new                    (void);


gboolean    gdm_task_list_task_is_active (GdmTaskList *task_list,
                                          GdmTask     *task);
GdmTask *   gdm_task_list_get_active_task (GdmTaskList *widget);
gboolean    gdm_task_list_set_active_task (GdmTaskList *widget,
                                           GdmTask     *task);
GdmTask *   gdm_task_list_foreach_task (GdmTaskList           *widget,
                                     GdmTaskListForeachFunc  foreach_func,
                                     gpointer               data);
void        gdm_task_list_add_task        (GdmTaskList *widget,
                                           GdmTask     *task);

int         gdm_task_list_get_number_of_tasks (GdmTaskList *widget);
G_END_DECLS

#endif /* __GDM_TASK_LIST_H */
