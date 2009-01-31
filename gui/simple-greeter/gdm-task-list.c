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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gdm-task-list.h"

#define GDM_TASK_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_TASK_LIST, GdmTaskListPrivate))

typedef struct
{
        GtkWidget *radio_button;
        char      *name;
} GdmTask;

struct GdmTaskListPrivate
{
        GtkWidget *box;
        GList     *tasks;
};

enum {
        ACTIVATED = 0,
        DEACTIVATED,
        NUMBER_OF_SIGNALS
};

static guint    signals[NUMBER_OF_SIGNALS];

static void     gdm_task_list_class_init  (GdmTaskListClass *klass);
static void     gdm_task_list_init        (GdmTaskList      *task_list);
static void     gdm_task_list_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmTaskList, gdm_task_list, GTK_TYPE_ALIGNMENT);

static void
on_task_toggled (GdmTaskList    *widget,
                 GtkRadioButton *radio_button)
{
        GdmTask *task;

        task = g_object_get_data (G_OBJECT (radio_button), "gdm-task");

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_button))) {
                g_signal_emit (widget, signals[ACTIVATED], 0, task->name);
        } else {
                g_signal_emit (widget, signals[DEACTIVATED], 0, task->name);
        }
}

void
gdm_task_list_add_task (GdmTaskList *task_list,
                        const char  *name,
                        const char  *icon_name)
{
        GdmTask   *task;
        GtkWidget *image;

        task = g_new0 (GdmTask, 1);

        task->name = g_strdup (name);
        if (task_list->priv->tasks == NULL) {
                task->radio_button = gtk_radio_button_new (NULL);
        } else {
                task->radio_button = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON (((GdmTask *) task_list->priv->tasks->data)->radio_button));
        }

        g_object_set (task->radio_button, "draw-indicator", FALSE, NULL);
        g_object_set_data (G_OBJECT (task->radio_button), "gdm-task", task);
        g_signal_connect_swapped (task->radio_button,
                                  "toggled", G_CALLBACK (on_task_toggled), task_list);
        image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_DND);
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (task->radio_button), image);
        gtk_widget_show (task->radio_button);
        gtk_container_add (GTK_CONTAINER (task->radio_button), task_list->priv->box);

        gtk_container_add (GTK_CONTAINER (task_list->priv->box), task->radio_button);
        task_list->priv->tasks = g_list_append (task_list->priv->tasks, task);
}

static void
gdm_task_list_class_init (GdmTaskListClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_task_list_finalize;

        signals [ACTIVATED] = g_signal_new ("activated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmTaskListClass, activated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__STRING,
                                            G_TYPE_NONE,
                                            1, G_TYPE_STRING);

        signals [DEACTIVATED] = g_signal_new ("deactivated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmTaskListClass, deactivated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__STRING,
                                            G_TYPE_NONE,
                                            1, G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (GdmTaskListPrivate));
}

static void
gdm_task_list_init (GdmTaskList *widget)
{
        widget->priv = GDM_TASK_LIST_GET_PRIVATE (widget);

        gtk_alignment_set_padding (GTK_ALIGNMENT (widget), 0, 0, 0, 0);
        gtk_alignment_set (GTK_ALIGNMENT (widget), 0.0, 0.0, 0, 0);

        widget->priv->box = gtk_hbox_new (FALSE, 2);
        gtk_widget_show (widget->priv->box);
        gtk_container_add (GTK_CONTAINER (widget),
                           widget->priv->box);
}

static void
gdm_task_list_finalize (GObject *object)
{
        GdmTaskList *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_TASK_LIST (object));

        widget = GDM_TASK_LIST (object);

        g_list_foreach (widget->priv->tasks, (GFunc) g_free, NULL);
        g_list_free (widget->priv->tasks);

        G_OBJECT_CLASS (gdm_task_list_parent_class)->finalize (object);
}

GtkWidget *
gdm_task_list_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_TASK_LIST, NULL);

        return GTK_WIDGET (object);
}

const char *
gdm_task_list_get_active_task (GdmTaskList *widget)
{
        GList *node;

        for (node = widget->priv->tasks; node != NULL; node = node->next) {
                GdmTask *task;

                task = node->data;

                if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (task->radio_button)) ) {
                        return task->name;
                }
        }

        return NULL;
}
