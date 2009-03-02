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
static void     gdm_task_list_finalize    (GObject          *object);

G_DEFINE_TYPE (GdmTaskList, gdm_task_list, GTK_TYPE_ALIGNMENT);

static void
on_task_toggled (GdmTaskList    *widget,
                 GtkRadioButton *button)
{
        GdmTask *task;

        task = g_object_get_data (G_OBJECT (button), "gdm-task");

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
                g_signal_emit (widget, signals[ACTIVATED], 0, task);
        } else {
                g_signal_emit (widget, signals[DEACTIVATED], 0, task);
        }
}

GdmTask *
gdm_task_list_foreach_task (GdmTaskList           *task_list,
                            GdmTaskListForeachFunc  search_func,
                            gpointer               data)
{
        GList *node;

        for (node = task_list->priv->tasks; node != NULL; node = node->next) {
                GdmTask *task;

                task = node->data;

                if (search_func (task_list, task, data)) {
                        return g_object_ref (task);
                }
        }

        return NULL;
}

static void
on_task_enabled (GdmTaskList *task_list,
                 GdmTask     *task)
{
        GtkWidget *button;
        GList     *task_node;

        button = g_object_get_data (G_OBJECT (task), "gdm-task-list-button");

        gtk_widget_set_sensitive (button, TRUE);

        /* Sort the list such that the tasks the user clicks last end
         * up first.  This doesn't change the order in which the tasks
         * appear in the UI, but will affect which tasks we implicitly
         * activate if the currently active task gets disabled.
         */
        task_node = g_list_find (task_list->priv->tasks, task);
        if (task_node != NULL) {
                task_list->priv->tasks = g_list_delete_link (task_list->priv->tasks, task_node);
                task_list->priv->tasks = g_list_prepend (task_list->priv->tasks,
                                                         task);
        }
}

static void
activate_first_available_task (GdmTaskList *task_list)
{
        GList *node;

        node = task_list->priv->tasks;
        while (node != NULL) {
                GdmTask   *task;

                task = GDM_TASK (node->data);

                if (gdm_task_list_set_active_task (task_list, task)) {
                        break;
                }

                node = node->next;
        }

}

static void
on_task_disabled (GdmTaskList *task_list,
                  GdmTask     *task)
{
        GtkWidget *button;
        gboolean   was_active;

        button = g_object_get_data (G_OBJECT (task), "gdm-task-list-button");
        was_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

        gtk_widget_set_sensitive (button, FALSE);

        if (was_active) {
                activate_first_available_task (task_list);
        }
}

void
gdm_task_list_add_task (GdmTaskList *task_list,
                        GdmTask     *task)
{
        GtkWidget *image;
        GtkWidget *button;
        GIcon     *icon;
        char      *description;

        if (task_list->priv->tasks == NULL) {
                button = gtk_radio_button_new (NULL);
        } else {
                GdmTask *previous_task;
                GtkRadioButton *previous_button;

                previous_task = GDM_TASK (task_list->priv->tasks->data);
                previous_button = GTK_RADIO_BUTTON (g_object_get_data (G_OBJECT (previous_task), "gdm-task-list-button"));
                button = gtk_radio_button_new_from_widget (previous_button);
        }
        g_object_set_data (G_OBJECT (task), "gdm-task-list-button", button);

        g_object_set (G_OBJECT (button), "draw-indicator", FALSE, NULL);
        g_object_set_data (G_OBJECT (button), "gdm-task", task);
        g_signal_connect_swapped (button, "toggled",
                                  G_CALLBACK (on_task_toggled),
                                  task_list);

        gtk_button_set_focus_on_click (GTK_BUTTON (button), FALSE);
        gtk_widget_set_sensitive (button, gdm_task_is_enabled (task));

        g_signal_connect_swapped (G_OBJECT (task), "enabled",
                                  G_CALLBACK (on_task_enabled),
                                  task_list);

        g_signal_connect_swapped (G_OBJECT (task), "disabled",
                                  G_CALLBACK (on_task_disabled),
                                  task_list);

        icon = gdm_task_get_icon (task);
        image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_SMALL_TOOLBAR);
        g_object_unref (icon);

        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (button), image);
        description = gdm_task_get_description (task);
        gtk_widget_set_tooltip_text (button, description);
        g_free (description);
        gtk_widget_show (button);

        gtk_container_add (GTK_CONTAINER (task_list->priv->box), button);
        task_list->priv->tasks = g_list_append (task_list->priv->tasks, task);

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
                g_signal_emit (task_list, signals[ACTIVATED], 0, task);
        }
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
                                            g_cclosure_marshal_VOID__OBJECT,
                                            G_TYPE_NONE,
                                            1, G_TYPE_OBJECT);

        signals [DEACTIVATED] = g_signal_new ("deactivated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmTaskListClass, deactivated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__OBJECT,
                                            G_TYPE_NONE,
                                            1, G_TYPE_OBJECT);

        g_type_class_add_private (klass, sizeof (GdmTaskListPrivate));
} static void gdm_task_list_init (GdmTaskList *widget)
{
        widget->priv = GDM_TASK_LIST_GET_PRIVATE (widget);

        gtk_alignment_set_padding (GTK_ALIGNMENT (widget), 0, 0, 0, 0);
        gtk_alignment_set (GTK_ALIGNMENT (widget), 0.0, 0.0, 0, 0);

        widget->priv->box = gtk_hbox_new (TRUE, 2);
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

gboolean
gdm_task_list_task_is_active (GdmTaskList *task_list,
                              GdmTask     *task)
{
        GtkWidget *button;

        button = g_object_get_data (G_OBJECT (task), "gdm-task-list-button");

        return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
}

GdmTask *
gdm_task_list_get_active_task (GdmTaskList *widget)
{
        return gdm_task_list_foreach_task (widget,
                                        (GdmTaskListForeachFunc)
                                        gdm_task_list_task_is_active,
                                        NULL);
}

gboolean
gdm_task_list_set_active_task (GdmTaskList *widget,
                               GdmTask     *task)
{
        GtkWidget *button;
        gboolean   was_sensitive;
        gboolean   was_activated;

        was_sensitive = GTK_WIDGET_SENSITIVE (widget);
        gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);

        button = GTK_WIDGET (g_object_get_data (G_OBJECT (task),
                             "gdm-task-list-button"));

        was_activated = FALSE;
        if (GTK_WIDGET_IS_SENSITIVE (button)) {
                if (gtk_widget_activate (button)) {
                        was_activated = TRUE;
                }
        }

        gtk_widget_set_sensitive (GTK_WIDGET (widget), was_sensitive);
        return was_activated;
}

int
gdm_task_list_get_number_of_tasks (GdmTaskList *widget)
{
        return g_list_length (widget->priv->tasks);
}
