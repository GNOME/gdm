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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
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

#include "gdm-user-manager.h"
#include "gdm-user-chooser-widget.h"

enum {
        USER_NO_DISPLAY              = 1 << 0,
        USER_ACCOUNT_DISABLED        = 1 << 1,
};

#define GDM_USER_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_CHOOSER_WIDGET, GdmUserChooserWidgetPrivate))

#define ICON_SIZE 64

struct GdmUserChooserWidgetPrivate
{
        GtkWidget          *treeview;

        GtkTreeModel       *real_model;
        GtkTreeModel       *filter_model;
        GtkTreeModel       *sort_model;

        GdmUserManager     *manager;

        char               *chosen_user;
        gboolean            show_only_chosen;
        gboolean            show_other_user;
        gboolean            show_guest_user;

        guint               populate_id;
};

enum {
        PROP_0,
};

enum {
        USER_CHOSEN,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_user_chooser_widget_class_init  (GdmUserChooserWidgetClass *klass);
static void     gdm_user_chooser_widget_init        (GdmUserChooserWidget      *user_chooser_widget);
static void     gdm_user_chooser_widget_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmUserChooserWidget, gdm_user_chooser_widget, GTK_TYPE_VBOX)

enum {
        CHOOSER_LIST_PIXBUF_COLUMN = 0,
        CHOOSER_LIST_NAME_COLUMN,
        CHOOSER_LIST_TOOLTIP_COLUMN,
        CHOOSER_LIST_IS_LOGGED_IN_COLUMN,
        CHOOSER_LIST_ID_COLUMN
};

void
gdm_user_chooser_widget_set_show_only_chosen (GdmUserChooserWidget *widget,
                                              gboolean              show_only)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_only_chosen != show_only) {
                widget->priv->show_only_chosen = show_only;
                if (widget->priv->filter_model != NULL) {
                        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (widget->priv->filter_model));
                }
        }
}

void
gdm_user_chooser_widget_set_show_other_user (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_other_user != show_user) {
                widget->priv->show_other_user = show_user;
                if (widget->priv->filter_model != NULL) {
                        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (widget->priv->filter_model));
                }
        }
}

void
gdm_user_chooser_widget_set_show_guest_user (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_guest_user != show_user) {
                widget->priv->show_guest_user = show_user;
                if (widget->priv->filter_model != NULL) {
                        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (widget->priv->filter_model));
                }
        }
}

char *
gdm_user_chooser_widget_get_chosen_user_name (GdmUserChooserWidget *widget)
{
        char *user_name;

        g_return_val_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget), NULL);

        user_name = NULL;
        if (widget->priv->chosen_user != NULL) {
                user_name = g_strdup (widget->priv->chosen_user);
        }

        return user_name;
}

static void
activate_name (GdmUserChooserWidget *widget,
               const char           *name)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkTreePath  *path;

        path = NULL;

        model = widget->priv->real_model;

        if (name != NULL && gtk_tree_model_get_iter_first (model, &iter)) {

                do {
                        char           *id;
                        gboolean        found;

                        id = NULL;
                        gtk_tree_model_get (model,
                                            &iter,
                                            CHOOSER_LIST_ID_COLUMN, &id,
                                            -1);
                        if (id == NULL) {
                                continue;
                        }

                        found = (strcmp (id, name) == 0);

                        if (found) {
                                path = gtk_tree_model_get_path (model, &iter);
                                break;
                        }

                } while (gtk_tree_model_iter_next (model, &iter));
        }

        if (path != NULL) {
                gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (widget->priv->treeview),
                                              path,
                                              NULL,
                                              TRUE,
                                              0.5,
                                              0.0);
                gtk_tree_view_set_cursor (GTK_TREE_VIEW (widget->priv->treeview),
                                          path,
                                          NULL,
                                          FALSE);

                gtk_tree_view_row_activated (GTK_TREE_VIEW (widget->priv->treeview),
                                             path,
                                             NULL);
                gtk_tree_path_free (path);
        }
}

static void
choose_user_id (GdmUserChooserWidget *widget,
                const char           *id)
{

        g_debug ("Selection changed from:'%s' to:'%s'",
                 widget->priv->chosen_user ? widget->priv->chosen_user : "",
                 id ? id : "");

        g_free (widget->priv->chosen_user);
        widget->priv->chosen_user = g_strdup (id);

        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (widget->priv->filter_model));
}

static void
choose_selected_user (GdmUserChooserWidget *widget)
{
        char             *id;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        id = NULL;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));
        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                gtk_tree_model_get (model, &iter, CHOOSER_LIST_ID_COLUMN, &id, -1);
        }

        choose_user_id (widget, id);
}

static void
clear_selection (GdmUserChooserWidget *widget)
{
        GtkTreeSelection *selection;
        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));
        gtk_tree_selection_unselect_all (selection);
}

void
gdm_user_chooser_widget_set_chosen_user_name (GdmUserChooserWidget *widget,
                                              const char           *name)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (name == NULL) {
                clear_selection (widget);
                choose_user_id (widget, NULL);
        } else {
                activate_name (widget, name);
        }
}

static void
gdm_user_chooser_widget_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GdmUserChooserWidget *self;

        self = GDM_USER_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_user_chooser_widget_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmUserChooserWidget *self;

        self = GDM_USER_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_user_chooser_widget_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmUserChooserWidget      *user_chooser_widget;
        GdmUserChooserWidgetClass *klass;

        klass = GDM_USER_CHOOSER_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_USER_CHOOSER_WIDGET));

        user_chooser_widget = GDM_USER_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (user_chooser_widget);
}

static void
gdm_user_chooser_widget_dispose (GObject *object)
{
        GdmUserChooserWidget *widget;

        widget = GDM_USER_CHOOSER_WIDGET (object);

        g_free (widget->priv->chosen_user);
        widget->priv->chosen_user = NULL;

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_user_chooser_widget_class_init (GdmUserChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_user_chooser_widget_get_property;
        object_class->set_property = gdm_user_chooser_widget_set_property;
        object_class->constructor = gdm_user_chooser_widget_constructor;
        object_class->dispose = gdm_user_chooser_widget_dispose;
        object_class->finalize = gdm_user_chooser_widget_finalize;

        signals [USER_CHOSEN] = g_signal_new ("user-chosen",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET (GdmUserChooserWidgetClass, user_chosen),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__VOID,
                                              G_TYPE_NONE,
                                              0);

        g_type_class_add_private (klass, sizeof (GdmUserChooserWidgetPrivate));
}

static void
on_selection_changed (GtkTreeSelection     *selection,
                      GdmUserChooserWidget *widget)
{
}

static void
on_row_activated (GtkTreeView          *tree_view,
                  GtkTreePath          *tree_path,
                  GtkTreeViewColumn    *tree_column,
                  GdmUserChooserWidget *widget)
{
        choose_selected_user (widget);

        g_signal_emit (widget, signals[USER_CHOSEN], 0);

        clear_selection (widget);
}

static GdkPixbuf *
get_pixbuf_for_user (GdmUserChooserWidget *widget,
                     const char           *username)
{
        GtkIconTheme *theme;
        GdkPixbuf    *pixbuf;

        theme = gtk_icon_theme_get_default ();
        pixbuf = gtk_icon_theme_load_icon (theme, "stock_person", ICON_SIZE, 0, NULL);

        return pixbuf;
}

static gboolean
populate_model (GdmUserChooserWidget *widget)
{
        GtkTreeIter iter;
        GdkPixbuf  *pixbuf;

        pixbuf = get_pixbuf_for_user (widget, NULL);

        /* Add some fake entries */
        gtk_list_store_append (GTK_LIST_STORE (widget->priv->real_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (widget->priv->real_model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_NAME_COLUMN, _("Other..."),
                            CHOOSER_LIST_TOOLTIP_COLUMN, _("Choose a different account"),
                            CHOOSER_LIST_ID_COLUMN, GDM_USER_CHOOSER_USER_OTHER,
                            -1);

        gtk_list_store_append (GTK_LIST_STORE (widget->priv->real_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (widget->priv->real_model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_NAME_COLUMN, _("Guest"),
                            CHOOSER_LIST_TOOLTIP_COLUMN, _("Login as a temporary guest"),
                            CHOOSER_LIST_ID_COLUMN, GDM_USER_CHOOSER_USER_GUEST,
                            -1);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }

        widget->priv->populate_id = 0;
        return FALSE;
}

#if 0
static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter  *iter,
                gpointer      data)
{
        int   column = GPOINTER_TO_INT (data);
        char *text;

        gtk_tree_model_get (model, iter, column, &text, -1);

        if (text != NULL && strcmp (text, "__separator") == 0) {
                return TRUE;
        }

        g_free (text);

        return FALSE;
}
#endif

static int
compare_user_names (char *name_a,
                    char *name_b,
                    char *id_a,
                    char *id_b)
{

        if (id_a == NULL) {
                return 1;
        } else if (id_b == NULL) {
                return -1;
        }

        if (strcmp (id_a, "__other") == 0) {
                return 1;
        } else if (strcmp (id_b, "__other") == 0) {
                return -1;
        } else if (strcmp (id_a, "__guest") == 0) {
                return 1;
        } else if (strcmp (id_b, "__guest") == 0) {
                return -1;
        }

        if (name_a == NULL) {
                return 1;
        } else if (name_b == NULL) {
                return -1;
        }

        return g_utf8_collate (name_a, name_b);
}

static int
compare_user  (GtkTreeModel *model,
                  GtkTreeIter  *a,
                  GtkTreeIter  *b,
                  gpointer      user_data)
{
        char *name_a;
        char *name_b;
        char *id_a;
        char *id_b;
        int   result;

        gtk_tree_model_get (model, a, CHOOSER_LIST_NAME_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, CHOOSER_LIST_NAME_COLUMN, &name_b, -1);
        gtk_tree_model_get (model, a, CHOOSER_LIST_ID_COLUMN, &id_a, -1);
        gtk_tree_model_get (model, b, CHOOSER_LIST_ID_COLUMN, &id_b, -1);

        result = compare_user_names (name_a, name_b, id_a, id_b);

        g_free (name_a);
        g_free (name_b);
        g_free (id_a);
        g_free (id_b);

        return result;
}

static void
on_user_added (GdmUserManager       *manager,
               GdmUser              *user,
               GdmUserChooserWidget *widget)
{
        GtkTreeIter   iter;
        GdkPixbuf    *pixbuf;
        char         *tooltip;

        g_debug ("User added: %s", gdm_user_get_user_name (user));

        pixbuf = gdm_user_render_icon (user, GTK_WIDGET (widget), ICON_SIZE);

        tooltip = g_strdup_printf ("%s: %s",
                                   _("Short Name"),
                                   gdm_user_get_user_name (user));

        gtk_list_store_append (GTK_LIST_STORE (widget->priv->real_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (widget->priv->real_model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_NAME_COLUMN, gdm_user_get_real_name (user),
                            CHOOSER_LIST_TOOLTIP_COLUMN, tooltip,
                            CHOOSER_LIST_ID_COLUMN, gdm_user_get_user_name (user),
                            -1);
        g_free (tooltip);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }
}

static void
on_user_removed (GdmUserManager       *manager,
                 GdmUser              *user,
                 GdmUserChooserWidget *widget)
{
        GtkTreeIter iter;
        gboolean    found;
        const char *user_name;

        g_debug ("User removed: %s", gdm_user_get_user_name (user));

        found = FALSE;

        user_name = gdm_user_get_user_name (user);

        if (gtk_tree_model_get_iter_first (widget->priv->real_model, &iter)) {

                do {
                        char *id;

                        id = NULL;
                        gtk_tree_model_get (widget->priv->real_model,
                                            &iter,
                                            CHOOSER_LIST_ID_COLUMN, &id,
                                            -1);
                        if (id == NULL) {
                                continue;
                        }

                        found = (strcmp (id, user_name) == 0);

                        if (found) {
                                break;
                        }

                } while (gtk_tree_model_iter_next (widget->priv->real_model, &iter));
        }
        if (found) {
                gtk_list_store_remove (GTK_LIST_STORE (widget->priv->real_model), &iter);
        }
}

static gboolean
user_visible_cb (GtkTreeModel         *model,
                 GtkTreeIter          *iter,
                 GdmUserChooserWidget *widget)
{
        char    *id;
        gboolean ret;

        ret = FALSE;

        id = NULL;
        gtk_tree_model_get (model, iter, CHOOSER_LIST_ID_COLUMN, &id, -1);
        if (id == NULL) {
                goto out;
        }

        /* if a user is chosen */
        if (widget->priv->chosen_user != NULL
            && widget->priv->show_only_chosen) {

                ret = (strcmp (id, widget->priv->chosen_user) == 0);
                goto out;
        }

        if (! widget->priv->show_other_user
            && strcmp (id, GDM_USER_CHOOSER_USER_OTHER) == 0) {
                ret = FALSE;
                goto out;
        }
        if (! widget->priv->show_guest_user
            && strcmp (id, GDM_USER_CHOOSER_USER_GUEST) == 0) {
                ret = FALSE;
                goto out;
        }

        ret = TRUE;

 out:
        g_free (id);

        return ret;
}

static void
name_cell_data_func (GtkTreeViewColumn    *tree_column,
                     GtkCellRenderer      *cell,
                     GtkTreeModel         *model,
                     GtkTreeIter          *iter,
                     GdmUserChooserWidget *widget)
{
        gboolean logged_in;
        char    *name;
        char    *markup;

        name = NULL;
        gtk_tree_model_get (model,
                            iter,
                            CHOOSER_LIST_IS_LOGGED_IN_COLUMN, &logged_in,
                            CHOOSER_LIST_NAME_COLUMN, &name,
                            -1);

        if (logged_in) {
                markup = g_strdup_printf ("<b>%s</b>\n<i><small>%s</small></i>",
                                          name,
                                          _("Currently logged in"));
        } else {
                markup = g_strdup_printf ("<b>%s</b>", name);
        }

        g_object_set (cell,
                      "markup", markup,
                      NULL);

        g_free (markup);
        g_free (name);
}

static void
gdm_user_chooser_widget_init (GdmUserChooserWidget *widget)
{
        GtkWidget         *scrolled;
        GtkTreeViewColumn *column;
        GtkTreeSelection  *selection;
        GtkCellRenderer   *renderer;

        widget->priv = GDM_USER_CHOOSER_WIDGET_GET_PRIVATE (widget);

        widget->priv->manager = gdm_user_manager_ref_default ();
        g_signal_connect (widget->priv->manager,
                          "user-added",
                          G_CALLBACK (on_user_added),
                          widget);
        g_signal_connect (widget->priv->manager,
                          "user-removed",
                          G_CALLBACK (on_user_removed),
                          widget);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget->priv->treeview), FALSE);

        g_signal_connect (widget->priv->treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->treeview);

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
        g_signal_connect (selection, "changed",
                          G_CALLBACK (on_selection_changed),
                          widget);

        widget->priv->real_model = (GtkTreeModel *)gtk_list_store_new (5,
                                                                       GDK_TYPE_PIXBUF,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_BOOLEAN,
                                                                       G_TYPE_STRING);

        widget->priv->filter_model = gtk_tree_model_filter_new (widget->priv->real_model, NULL);

        gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (widget->priv->filter_model),
                                                (GtkTreeModelFilterVisibleFunc) user_visible_cb,
                                                widget,
                                                NULL);

        widget->priv->sort_model = gtk_tree_model_sort_new_with_model (widget->priv->filter_model);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (widget->priv->sort_model),
                                         CHOOSER_LIST_NAME_COLUMN,
                                         compare_user,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (widget->priv->sort_model),
                                              CHOOSER_LIST_NAME_COLUMN,
                                              GTK_SORT_ASCENDING);

        gtk_tree_view_set_model (GTK_TREE_VIEW (widget->priv->treeview), widget->priv->sort_model);

        renderer = gtk_cell_renderer_pixbuf_new ();
        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);

        g_object_set (renderer,
                      "width", 128,
                      "yalign", 0.5,
                      "xalign", 1.0,
                      NULL);
        gtk_tree_view_column_set_attributes (column,
                                             renderer,
                                             "pixbuf", CHOOSER_LIST_PIXBUF_COLUMN,
                                             NULL);

        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);
        gtk_tree_view_column_set_cell_data_func (column,
                                                 renderer,
                                                 (GtkTreeCellDataFunc) name_cell_data_func,
                                                 widget,
                                                 NULL);

        gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (widget->priv->treeview), CHOOSER_LIST_TOOLTIP_COLUMN);

#if 0
        gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget->priv->treeview),
                                              separator_func,
                                              GINT_TO_POINTER (CHOOSER_LIST_ID_COLUMN),
                                              NULL);
#endif

        widget->priv->populate_id = g_idle_add ((GSourceFunc)populate_model, widget);
}

static void
gdm_user_chooser_widget_finalize (GObject *object)
{
        GdmUserChooserWidget *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (object));

        widget = GDM_USER_CHOOSER_WIDGET (object);

        g_return_if_fail (widget->priv != NULL);

        if (widget->priv->populate_id > 0) {
                g_source_remove (widget->priv->populate_id);
        }

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_user_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_USER_CHOOSER_WIDGET,
                               NULL);

        return GTK_WIDGET (object);
}
