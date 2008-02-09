/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Written by: Ray Strode <rstrode@redhat.com>
 *              William Jon McCann <mccann@jhu.edu>
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

#include "gdm-option-widget.h"

#define GDM_OPTION_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_OPTION_WIDGET, GdmOptionWidgetPrivate))

#define GDM_OPTION_WIDGET_RC_STRING \
"style \"gdm-option-widget-style\"" \
"{" \
"  GtkComboBox::appears-as-list = 1" \
"}" \
"widget_class \"*<GdmOptionWidget>.*.GtkComboBox\" style \"gdm-option-widget-style\""

struct GdmOptionWidgetPrivate
{
        GtkWidget                *label;
        char                     *label_text;

        GtkWidget                *items_combo_box;
        GtkListStore             *list_store;

        GtkTreeModelSort         *model_sorter;

        GtkTreeRowReference      *active_row;
        GtkTreeRowReference      *separator_row;

        gint                     number_of_normal_rows;
        gint                     number_of_separated_rows;

        GdmOptionWidgetPosition  separator_position;
};

enum {
        PROP_0,
        PROP_LABEL_TEXT
};

enum {
        ACTIVATED = 0,
        NUMBER_OF_SIGNALS
};

static guint    signals[NUMBER_OF_SIGNALS];

static void     gdm_option_widget_class_init  (GdmOptionWidgetClass *klass);
static void     gdm_option_widget_init        (GdmOptionWidget      *option_widget);
static void     gdm_option_widget_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmOptionWidget, gdm_option_widget, GTK_TYPE_ALIGNMENT)
enum {
        OPTION_NAME_COLUMN = 0,
        OPTION_COMMENT_COLUMN,
        OPTION_ITEM_IS_SEPARATED_COLUMN,
        OPTION_ID_COLUMN,
        NUMBER_OF_OPTION_COLUMNS
};

static gboolean
find_item (GdmOptionWidget *widget,
           const char       *id,
           GtkTreeIter      *iter)
{
        GtkTreeModel *model;
        gboolean      found_item;

        g_assert (GDM_IS_OPTION_WIDGET (widget));
        g_assert (id != NULL);

        found_item = FALSE;
        model = GTK_TREE_MODEL (widget->priv->model_sorter);

        if (!gtk_tree_model_get_iter_first (model, iter)) {
                return FALSE;
        }

        do {
                char *item_id;

                gtk_tree_model_get (model, iter,
                                    OPTION_ID_COLUMN, &item_id, -1);

                g_assert (item_id != NULL);

                if (strcmp (id, item_id) == 0) {
                        found_item = TRUE;
                }
                g_free (item_id);

        } while (!found_item && gtk_tree_model_iter_next (model, iter));

        return found_item;
}

static char *
get_active_item_id (GdmOptionWidget *widget,
                    GtkTreeIter      *iter)
{
        char         *item_id;
        GtkTreeModel *model;
        GtkTreePath  *path;

        g_return_val_if_fail (GDM_IS_OPTION_WIDGET (widget), NULL);

        model = GTK_TREE_MODEL (widget->priv->list_store);
        item_id = NULL;

        if (widget->priv->active_row == NULL) {
                return NULL;
        }

        path = gtk_tree_row_reference_get_path (widget->priv->active_row);
        if (gtk_tree_model_get_iter (model, iter, path)) {
                gtk_tree_model_get (model, iter,
                                    OPTION_ID_COLUMN, &item_id, -1);
        };
        gtk_tree_path_free (path);

        return item_id;
}

char *
gdm_option_widget_get_active_item (GdmOptionWidget *widget)
{
        GtkTreeIter iter;

        return get_active_item_id (widget, &iter);
}

static void
activate_from_item_id (GdmOptionWidget *widget,
                       const char      *item_id)
{
        GtkTreeModel *model;
        GtkTreePath  *path;
        GtkTreeIter   iter;

        model = GTK_TREE_MODEL (widget->priv->list_store);
        path = NULL;

        if (!find_item (widget, item_id, &iter)) {
                g_critical ("Tried to activate non-existing item from option widget");
                return;
        }

        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget->priv->items_combo_box),
                                       &iter);
}

static void
activate_from_row (GdmOptionWidget    *widget,
                   GtkTreeRowReference *row)
{
        g_assert (row != NULL);
        g_assert (gtk_tree_row_reference_valid (row));

        if (widget->priv->active_row != NULL) {
                gtk_tree_row_reference_free (widget->priv->active_row);
                widget->priv->active_row = NULL;
        }

        widget->priv->active_row = gtk_tree_row_reference_copy (row);

        g_signal_emit (widget, signals[ACTIVATED], 0);

}

static void
activate_selected_item (GdmOptionWidget *widget)
{
        GtkTreeModel        *model;
        GtkTreeIter          sorted_iter;
        gboolean             is_already_active;

        model = GTK_TREE_MODEL (widget->priv->list_store);
        is_already_active = FALSE;

        if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget->priv->items_combo_box), &sorted_iter)) {
                GtkTreeRowReference *row;
                GtkTreePath *sorted_path;
                GtkTreePath *base_path;

                sorted_path = gtk_tree_model_get_path (GTK_TREE_MODEL (widget->priv->model_sorter),
                                                       &sorted_iter);
                base_path =
                    gtk_tree_model_sort_convert_path_to_child_path (widget->priv->model_sorter,
                                                                    sorted_path);
                gtk_tree_path_free (sorted_path);

                if (widget->priv->active_row != NULL) {
                        GtkTreePath *active_path;

                        active_path = gtk_tree_row_reference_get_path (widget->priv->active_row);

                        if (active_path != NULL) {
                                if (gtk_tree_path_compare  (base_path, active_path) == 0) {
                                        is_already_active = TRUE;
                                }
                                gtk_tree_path_free (active_path);
                        }
                }
                g_assert (base_path != NULL);
                row = gtk_tree_row_reference_new (model, base_path);
                gtk_tree_path_free (base_path);

                if (!is_already_active) {
                    activate_from_row (widget, row);
                }

                gtk_tree_row_reference_free (row);
        }
}

void
gdm_option_widget_set_active_item (GdmOptionWidget *widget,
                                   const char      *id)
{
        g_return_if_fail (GDM_IS_OPTION_WIDGET (widget));
        g_return_if_fail (id != NULL);

        activate_from_item_id (widget, id);
}

static const char *
gdm_option_widget_get_label_text (GdmOptionWidget *widget)
{
        return widget->priv->label_text;
}

static void
gdm_option_widget_set_label_text (GdmOptionWidget *widget,
                                  const char      *text)
{
        if (widget->priv->label_text == NULL ||
            strcmp (widget->priv->label_text, text) != 0) {
                g_free (widget->priv->label_text);
                widget->priv->label_text = g_strdup (text);
                gtk_label_set_markup_with_mnemonic (GTK_LABEL (widget->priv->label),
                                      text);
                g_object_notify (G_OBJECT (widget), "label-text");
        }
}

static void
gdm_option_widget_set_property (GObject        *object,
                                 guint           prop_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
        GdmOptionWidget *self;

        self = GDM_OPTION_WIDGET (object);

        switch (prop_id) {
        case PROP_LABEL_TEXT:
                gdm_option_widget_set_label_text (self, g_value_get_string (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_option_widget_get_property (GObject        *object,
                                guint           prop_id,
                                GValue         *value,
                                GParamSpec     *pspec)
{
        GdmOptionWidget *self;

        self = GDM_OPTION_WIDGET (object);

        switch (prop_id) {
        case PROP_LABEL_TEXT:
                g_value_set_string (value,
                                    gdm_option_widget_get_label_text (self));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_option_widget_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmOptionWidget      *option_widget;
        GdmOptionWidgetClass *klass;

        klass = GDM_OPTION_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_OPTION_WIDGET));

        option_widget = GDM_OPTION_WIDGET (G_OBJECT_CLASS (gdm_option_widget_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (option_widget);
}

static void
gdm_option_widget_dispose (GObject *object)
{
        GdmOptionWidget *widget;

        widget = GDM_OPTION_WIDGET (object);

        if (widget->priv->separator_row != NULL) {
                gtk_tree_row_reference_free (widget->priv->separator_row);
                widget->priv->separator_row = NULL;
        }

        if (widget->priv->active_row != NULL) {
                gtk_tree_row_reference_free (widget->priv->active_row);
                widget->priv->active_row = NULL;
        }

        G_OBJECT_CLASS (gdm_option_widget_parent_class)->dispose (object);
}

static void
gdm_option_widget_class_init (GdmOptionWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_option_widget_get_property;
        object_class->set_property = gdm_option_widget_set_property;
        object_class->constructor = gdm_option_widget_constructor;
        object_class->dispose = gdm_option_widget_dispose;
        object_class->finalize = gdm_option_widget_finalize;

        gtk_rc_parse_string (GDM_OPTION_WIDGET_RC_STRING);

        signals [ACTIVATED] = g_signal_new ("activated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmOptionWidgetClass, activated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__VOID,
                                            G_TYPE_NONE,
                                            0);

        g_object_class_install_property (object_class,
                                         PROP_LABEL_TEXT,
                                         g_param_spec_string ("label-text",
                                                              _("Label Text"),
                                                              _("The text to use as a label"),
                                                              NULL,
                                                              (G_PARAM_READWRITE |
                                                               G_PARAM_CONSTRUCT)));


        g_type_class_add_private (klass, sizeof (GdmOptionWidgetPrivate));
}

static void
on_changed (GtkComboBox     *combo_box,
            GdmOptionWidget *widget)
{
        activate_selected_item (widget);
}

static gboolean
path_is_separator (GdmOptionWidget *widget,
                   GtkTreeModel     *model,
                   GtkTreePath      *path)
{
        GtkTreePath      *base_path;
        GtkTreePath      *sorted_path;
        GtkTreePath      *separator_path;
        gboolean          is_separator;

        if (widget->priv->separator_row == NULL) {
                return FALSE;
        }

        base_path = gtk_tree_row_reference_get_path (widget->priv->separator_row);
        separator_path = base_path;
        sorted_path = NULL;

        if (base_path == NULL) {
                return FALSE;
        }

        if (model != GTK_TREE_MODEL (widget->priv->list_store)) {
                sorted_path = gtk_tree_model_sort_convert_child_path_to_path (widget->priv->model_sorter, base_path);
                separator_path = sorted_path;

                gtk_tree_path_free (base_path);
                base_path = NULL;
        }

        if ((separator_path != NULL) &&
            gtk_tree_path_compare (path, separator_path) == 0) {
                is_separator = TRUE;
        } else {
                is_separator = FALSE;
        }
        gtk_tree_path_free (separator_path);

        return is_separator;
}

static int
compare_item  (GtkTreeModel *model,
               GtkTreeIter  *a,
               GtkTreeIter  *b,
               gpointer      data)
{
        GdmOptionWidget *widget;
        char             *name_a;
        char             *name_b;
        gboolean          is_separate_a;
        gboolean          is_separate_b;
        int               result;
        int               direction;
        GtkTreeIter      *separator_iter;

        g_assert (GDM_IS_OPTION_WIDGET (data));

        widget = GDM_OPTION_WIDGET (data);

        separator_iter = NULL;
        if (widget->priv->separator_row != NULL) {

                GtkTreePath      *path_a;
                GtkTreePath      *path_b;

                path_a = gtk_tree_model_get_path (model, a);
                path_b = gtk_tree_model_get_path (model, b);

                if (path_is_separator (widget, model, path_a)) {
                        separator_iter = a;
                } else if (path_is_separator (widget, model, path_b)) {
                        separator_iter = b;
                }

                gtk_tree_path_free (path_a);
                gtk_tree_path_free (path_b);
        }

        name_a = NULL;
        is_separate_a = FALSE;
        if (separator_iter != a) {
                gtk_tree_model_get (model, a,
                                    OPTION_NAME_COLUMN, &name_a,
                                    OPTION_ITEM_IS_SEPARATED_COLUMN, &is_separate_a,
                                    -1);
        }

        char *id;
        name_b = NULL;
        is_separate_b = FALSE;
        if (separator_iter != b) {
                gtk_tree_model_get (model, b,
                                    OPTION_NAME_COLUMN, &name_b,
                                    OPTION_ID_COLUMN, &id,
                                    OPTION_ITEM_IS_SEPARATED_COLUMN, &is_separate_b,
                                    -1);
        }

        if (widget->priv->separator_position == GDM_OPTION_WIDGET_POSITION_TOP) {
                direction = -1;
        } else {
                direction = 1;
        }

        if (separator_iter == b) {
                result = is_separate_a? 1 : -1;
                result *= direction;
        } else if (separator_iter == a) {
                result = is_separate_b? -1 : 1;
                result *= direction;
        } else if (is_separate_b == is_separate_a) {
                result = g_utf8_collate (name_a, name_b);
        } else {
                result = is_separate_a - is_separate_b;
                result *= direction;
        }

        g_free (name_a);
        g_free (name_b);

        return result;
}

static void
name_cell_data_func (GtkTreeViewColumn  *tree_column,
                     GtkCellRenderer    *cell,
                     GtkTreeModel       *model,
                     GtkTreeIter        *iter,
                     GdmOptionWidget   *widget)
{
        char    *name;
        char    *markup;

        name = NULL;
        gtk_tree_model_get (model,
                            iter,
                            OPTION_NAME_COLUMN, &name,
                            -1);

        markup = g_strdup_printf ("<span size='small'>%s</span>",
                name ? name : "(null)");
        g_free (name);

        g_object_set (cell, "markup", markup, NULL);
        g_free (markup);
}

static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter  *iter,
                gpointer      data)
{
        GdmOptionWidget *widget;
        GtkTreePath      *path;
        gboolean          is_separator;

        g_assert (GDM_IS_OPTION_WIDGET (data));

        widget = GDM_OPTION_WIDGET (data);

        if (widget->priv->separator_row == NULL) {
                return FALSE;
        }

        path = gtk_tree_model_get_path (model, iter);

        is_separator = path_is_separator (widget, model, path);

        gtk_tree_path_free (path);

        return is_separator;
}

static void
add_separator (GdmOptionWidget *widget)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        GtkTreePath  *path;

        g_assert (widget->priv->separator_row == NULL);

        model = GTK_TREE_MODEL (widget->priv->list_store);

        gtk_list_store_insert_with_values (widget->priv->list_store,
                                           &iter, 0,
                                           OPTION_ID_COLUMN, "-",
                                           OPTION_ITEM_IS_SEPARATED_COLUMN, TRUE,
                                           -1);
        path = gtk_tree_model_get_path (model, &iter);
        widget->priv->separator_row =
            gtk_tree_row_reference_new (model, path);
        gtk_tree_path_free (path);
}

static void
gdm_option_widget_init (GdmOptionWidget *widget)
{
        GtkWidget         *box;
        GtkCellRenderer   *renderer;

        widget->priv = GDM_OPTION_WIDGET_GET_PRIVATE (widget);

        gtk_alignment_set_padding (GTK_ALIGNMENT (widget), 0, 0, 0, 0);
        gtk_alignment_set (GTK_ALIGNMENT (widget), 0.5, 0.5, 0, 0);

        box = gtk_hbox_new (FALSE, 6);
        gtk_widget_show (box);
        gtk_container_add (GTK_CONTAINER (widget),
                           box);

        widget->priv->label = gtk_label_new ("");
        gtk_label_set_use_underline (GTK_LABEL (widget->priv->label), TRUE);
        gtk_label_set_use_markup (GTK_LABEL (widget->priv->label), TRUE);
        gtk_widget_show (widget->priv->label);
        gtk_container_add (GTK_CONTAINER (box), widget->priv->label);

        widget->priv->items_combo_box = gtk_combo_box_new ();
        g_signal_connect (widget->priv->items_combo_box,
                          "changed",
                          G_CALLBACK (on_changed),
                          widget);

        gtk_widget_show (widget->priv->items_combo_box);
        gtk_container_add (GTK_CONTAINER (box),
                           widget->priv->items_combo_box);
        gtk_label_set_mnemonic_widget (GTK_LABEL (widget->priv->label),
                                       widget->priv->items_combo_box);

        g_assert (NUMBER_OF_OPTION_COLUMNS == 4);
        widget->priv->list_store = gtk_list_store_new (NUMBER_OF_OPTION_COLUMNS,
                                                       G_TYPE_STRING,
                                                       G_TYPE_STRING,
                                                       G_TYPE_BOOLEAN,
                                                       G_TYPE_STRING);

        widget->priv->model_sorter = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (widget->priv->list_store)));

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (widget->priv->model_sorter),
                                         OPTION_ID_COLUMN,
                                         compare_item,
                                         widget, NULL);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (widget->priv->model_sorter),
                                              OPTION_ID_COLUMN,
                                              GTK_SORT_ASCENDING);
        gtk_combo_box_set_model (GTK_COMBO_BOX (widget->priv->items_combo_box),
                                 GTK_TREE_MODEL (widget->priv->model_sorter));

        add_separator (widget);
        gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (widget->priv->items_combo_box),
                                              separator_func, widget, NULL);

        /* NAME COLUMN */
        renderer = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget->priv->items_combo_box), renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (widget->priv->items_combo_box),
                                            renderer,
                                            (GtkCellLayoutDataFunc) name_cell_data_func,
                                            widget,
                                            NULL);
}

static void
gdm_option_widget_finalize (GObject *object)
{
        GdmOptionWidget *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_OPTION_WIDGET (object));

        widget = GDM_OPTION_WIDGET (object);

        g_return_if_fail (widget->priv != NULL);

        G_OBJECT_CLASS (gdm_option_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_option_widget_new (const char *label_text)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_OPTION_WIDGET,
                               "label-text", label_text, NULL);

        return GTK_WIDGET (object);
}

void
gdm_option_widget_add_item (GdmOptionWidget *widget,
                             const char     *id,
                             const char     *name,
                             const char     *comment,
                             gboolean        keep_separate)
{
        GtkTreeIter iter;

        g_return_if_fail (GDM_IS_OPTION_WIDGET (widget));

        if (keep_separate) {
                widget->priv->number_of_separated_rows++;
        } else {
                widget->priv->number_of_normal_rows++;
        }

        gtk_list_store_insert_with_values (widget->priv->list_store,
                                           &iter, 0,
                                           OPTION_NAME_COLUMN, name,
                                           OPTION_COMMENT_COLUMN, comment,
                                           OPTION_ITEM_IS_SEPARATED_COLUMN, keep_separate,
                                           OPTION_ID_COLUMN, id,
                                           -1);
}

void
gdm_option_widget_remove_item (GdmOptionWidget *widget,
                               const char      *id)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        gboolean      is_separate;

        g_return_if_fail (GDM_IS_OPTION_WIDGET (widget));

        model = GTK_TREE_MODEL (widget->priv->list_store);

        if (!find_item (widget, id, &iter)) {
                g_critical ("Tried to remove non-existing item from option widget");
                return;
        }

        is_separate = FALSE;
        gtk_tree_model_get (model, &iter,
                            OPTION_ITEM_IS_SEPARATED_COLUMN, &is_separate,
                            -1);

        if (is_separate) {
                widget->priv->number_of_separated_rows--;
        } else {
                widget->priv->number_of_normal_rows--;
        }

        gtk_list_store_remove (widget->priv->list_store, &iter);
}


void
gdm_option_widget_remove_all_items (GdmOptionWidget *widget)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        gboolean      is_separated;
        gboolean      is_valid;

        g_assert (GDM_IS_OPTION_WIDGET (widget));

        model = GTK_TREE_MODEL (widget->priv->list_store);

        if (!gtk_tree_model_get_iter_first (model, &iter)) {
                return;
        }

        do {
                gtk_tree_model_get (model, &iter,
                                    OPTION_ITEM_IS_SEPARATED_COLUMN, &is_separated,
                                    -1);

                if (!is_separated) {
                        is_valid = gtk_list_store_remove (widget->priv->list_store,
                                                          &iter);
                } else {
                        is_valid = gtk_tree_model_iter_next (model, &iter);
                }


        } while (is_valid);
}

gboolean
gdm_option_widget_lookup_item (GdmOptionWidget *widget,
                                const char       *id,
                                char            **name,
                                char            **comment,
                                gboolean         *is_separate)
{
        GtkTreeIter   iter;
        char         *active_item_id;

        g_return_val_if_fail (GDM_IS_OPTION_WIDGET (widget), FALSE);

        if (id == NULL) {
                sleep (30);
        }
        g_return_val_if_fail (id != NULL, FALSE);

        active_item_id = get_active_item_id (widget, &iter);

        if (active_item_id == NULL || strcmp (active_item_id, id) != 0) {
                g_free (active_item_id);

                if (!find_item (widget, id, &iter)) {
                        return FALSE;
                }
        } else {
                g_free (active_item_id);
        }

        if (name != NULL) {
                gtk_tree_model_get (GTK_TREE_MODEL (widget->priv->list_store), &iter,
                                    OPTION_NAME_COLUMN, name, -1);
        }

        if (comment != NULL) {
                gtk_tree_model_get (GTK_TREE_MODEL (widget->priv->list_store), &iter,
                                    OPTION_COMMENT_COLUMN, comment, -1);
        }

        if (is_separate != NULL) {
                gtk_tree_model_get (GTK_TREE_MODEL (widget->priv->list_store), &iter,
                                    OPTION_ITEM_IS_SEPARATED_COLUMN, is_separate, -1);
        }

        return TRUE;
}

void
gdm_option_widget_set_separator_position (GdmOptionWidget         *widget,
                                          GdmOptionWidgetPosition  position)
{
        g_return_if_fail (GDM_IS_OPTION_WIDGET (widget));

        if (widget->priv->separator_position != position) {
                widget->priv->separator_position = position;
        }
}
