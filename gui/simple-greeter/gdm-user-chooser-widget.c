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

#include "gdm-user-chooser-widget.h"

enum {
        USER_NO_DISPLAY              = 1 << 0,
        USER_ACCOUNT_DISABLED        = 1 << 1,
};

#define GDM_USER_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_CHOOSER_WIDGET, GdmUserChooserWidgetPrivate))

typedef struct _GdmChooserUser {
        char      *name;
        char      *realname;
        GdkPixbuf *pixbuf;
        guint      flags;
} GdmChooserUser;

struct GdmUserChooserWidgetPrivate
{
        GtkWidget          *iconview;

        GHashTable         *available_users;
        char               *current_user;
};

enum {
        PROP_0,
};

enum {
        USER_ACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_user_chooser_widget_class_init  (GdmUserChooserWidgetClass *klass);
static void     gdm_user_chooser_widget_init        (GdmUserChooserWidget      *user_chooser_widget);
static void     gdm_user_chooser_widget_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmUserChooserWidget, gdm_user_chooser_widget, GTK_TYPE_VBOX)

enum {
        CHOOSER_LIST_PIXBUF_COLUMN = 0,
        CHOOSER_LIST_CAPTION_COLUMN,
        CHOOSER_LIST_TOOLTIP_COLUMN,
        CHOOSER_LIST_ID_COLUMN
};

static void
chooser_user_free (GdmChooserUser *user)
{
        if (user == NULL) {
                return;
        }

        if (user->pixbuf != NULL) {
                g_object_unref (user->pixbuf);
        }

        g_free (user->name);
        g_free (user->realname);

        g_free (user);
}

char *
gdm_user_chooser_widget_get_current_user_name (GdmUserChooserWidget *widget)
{
        char *user_name;

        g_return_val_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget), NULL);

        user_name = NULL;
        if (widget->priv->current_user != NULL) {
                user_name = g_strdup (widget->priv->current_user);
        }

        return user_name;
}

static void
select_name (GdmUserChooserWidget *widget,
             const char           *name)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkTreePath  *path;

        path = NULL;

        model = gtk_icon_view_get_model (GTK_ICON_VIEW (widget->priv->iconview));

        if (name != NULL && gtk_tree_model_get_iter_first (model, &iter)) {

                do {
                        GdmChooserUser *user;
                        char              *id;
                        gboolean          found;

                        user = NULL;
                        id = NULL;
                        gtk_tree_model_get (model,
                                            &iter,
                                            CHOOSER_LIST_ID_COLUMN, &id,
                                            -1);
                        if (id != NULL) {
                                user = g_hash_table_lookup (widget->priv->available_users, id);
                                g_free (id);
                        }

                        found = (user != NULL
                                 && user->name != NULL
                                 && strcmp (user->name, name) == 0);

                        if (found) {
                                path = gtk_tree_model_get_path (model, &iter);
                                break;
                        }

                } while (gtk_tree_model_iter_next (model, &iter));
        }

        if (path != NULL) {
                gtk_icon_view_scroll_to_path (GTK_ICON_VIEW (widget->priv->iconview),
                                              path,
                                              TRUE,
                                              0.5,
                                              0.0);
                gtk_icon_view_set_cursor (GTK_ICON_VIEW (widget->priv->iconview),
                                          path,
                                          NULL,
                                          FALSE);

                gtk_tree_path_free (path);
        }
}

void
gdm_user_chooser_widget_set_current_user_name (GdmUserChooserWidget *widget,
                                               const char           *name)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (name == NULL) {
                gtk_icon_view_unselect_all (GTK_ICON_VIEW (widget->priv->iconview));
        } else {
                select_name (widget, name);
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

        if (widget->priv->available_users != NULL) {
                g_hash_table_destroy (widget->priv->available_users);
                widget->priv->available_users = NULL;
        }

        g_free (widget->priv->current_user);
        widget->priv->current_user = NULL;

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

        signals [USER_ACTIVATED] = g_signal_new ("user-activated",
                                                 G_TYPE_FROM_CLASS (object_class),
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET (GdmUserChooserWidgetClass, user_activated),
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE,
                                                 0);

        g_type_class_add_private (klass, sizeof (GdmUserChooserWidgetPrivate));
}

static void
on_selection_changed (GtkIconView          *icon_view,
                      GdmUserChooserWidget *widget)
{
        GList        *items;
        char         *id;

        id = NULL;

        items = gtk_icon_view_get_selected_items (icon_view);
        if (items != NULL) {
                GtkTreeModel *model;
                GtkTreeIter   iter;
                GtkTreePath  *path;

                path = items->data;
                model = gtk_icon_view_get_model (icon_view);
                gtk_tree_model_get_iter (model, &iter, path);
                gtk_tree_model_get (model, &iter, CHOOSER_LIST_ID_COLUMN, &id, -1);
        }

        g_free (widget->priv->current_user);
        widget->priv->current_user = g_strdup (id);

        g_list_foreach (items, (GFunc)gtk_tree_path_free, NULL);
        g_list_free (items);
}

static void
collect_users (GdmUserChooserWidget *widget)
{

}

static void
on_item_activated (GtkIconView          *icon_view,
                   GtkTreePath          *tree_path,
                   GdmUserChooserWidget *widget)
{
        g_signal_emit (widget, signals[USER_ACTIVATED], 0);
}

static void
add_user_to_model (const char           *name,
                   GdmChooserUser       *user,
                   GdmUserChooserWidget *widget)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        char         *caption;
        char         *tooltip;

        if (user->flags & USER_NO_DISPLAY
            || user->flags & USER_ACCOUNT_DISABLED) {
                /* skip */
                g_debug ("Not adding user to list: %s", user->name);
        }

        caption = g_strdup_printf ("<span size=\"x-large\">%s</span>",
                                   user->realname);
        tooltip = g_strdup_printf ("%s: %s",
                                   _("Short Name"),
                                   user->name);

        model = gtk_icon_view_get_model (GTK_ICON_VIEW (widget->priv->iconview));

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model),
                            &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, user->pixbuf,
                            CHOOSER_LIST_CAPTION_COLUMN, caption,
                            CHOOSER_LIST_TOOLTIP_COLUMN, tooltip,
                            CHOOSER_LIST_ID_COLUMN, name,
                            -1);
        g_free (caption);
}

static GdkPixbuf *
get_pixbuf_for_user (GdmUserChooserWidget *widget,
                     const char           *username)
{
        GtkIconTheme *theme;
        GdkPixbuf    *pixbuf;
        int           size;

        theme = gtk_icon_theme_get_default ();
        gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &size, NULL);
        pixbuf = gtk_icon_theme_load_icon (theme, "stock_person", size, 0, NULL);

        return pixbuf;
}

static void
populate_model (GdmUserChooserWidget *widget,
                GtkTreeModel         *model)
{
        GtkTreeIter   iter;
        GdkPixbuf    *pixbuf;
        char         *caption;
        char         *tooltip;

        /* Add some fake entries */

        caption = g_strdup_printf ("<span size=\"x-large\">%s</span>\n<i>%s</i>",
                                   _("Guest User"),
                                   _("Already logged in"));
        tooltip = g_strdup_printf ("%s: %s",
                                   _("Short Name"),
                                   "guest");
        pixbuf = get_pixbuf_for_user (widget, "guest");
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_CAPTION_COLUMN, caption,
                            CHOOSER_LIST_TOOLTIP_COLUMN, tooltip,
                            CHOOSER_LIST_ID_COLUMN, "guest",
                            -1);
        g_free (caption);
        g_free (tooltip);

        caption = g_strdup_printf ("<span size=\"x-large\">%s</span>\n<i>%s</i>",
                                   _("GNOME Test"),
                                   _("Already logged in"));
        tooltip = g_strdup_printf ("%s: %s",
                                   _("Short Name"),
                                   "gtest");
        pixbuf = get_pixbuf_for_user (widget, "gtest");
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_CAPTION_COLUMN, caption,
                            CHOOSER_LIST_TOOLTIP_COLUMN, tooltip,
                            CHOOSER_LIST_ID_COLUMN, "gtest",
                            -1);
        g_free (caption);
        g_free (tooltip);

        caption = g_strdup_printf ("<span size=\"x-large\">%s</span>",
                                   _("Administrator"));
        tooltip = g_strdup_printf ("%s: %s",
                                   _("Short Name"),
                                   "administrator");

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_PIXBUF_COLUMN, pixbuf,
                            CHOOSER_LIST_CAPTION_COLUMN, caption,
                            CHOOSER_LIST_TOOLTIP_COLUMN, tooltip,
                            CHOOSER_LIST_ID_COLUMN, "administrator",
                            -1);
        g_free (caption);
        g_free (tooltip);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }

        g_hash_table_foreach (widget->priv->available_users,
                              (GHFunc)add_user_to_model,
                              widget);
}

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

        if (strcmp (id_a, "__previous-user") == 0) {
                return -1;
        } else if (strcmp (id_b, "__previous-user") == 0) {
                return 1;
        } else if (strcmp (id_a, "__default-user") == 0) {
                return -1;
        } else if (strcmp (id_b, "__default-user") == 0) {
                return 1;
        } else if (strcmp (id_a, "__separator") == 0) {
                return -1;
        } else if (strcmp (id_b, "__separator") == 0) {
                return 1;
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

        gtk_tree_model_get (model, a, CHOOSER_LIST_CAPTION_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, CHOOSER_LIST_CAPTION_COLUMN, &name_b, -1);
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
gdm_user_chooser_widget_init (GdmUserChooserWidget *widget)
{
        GtkTreeModel      *model;
        GtkWidget         *scrolled;

        widget->priv = GDM_USER_CHOOSER_WIDGET_GET_PRIVATE (widget);

        widget->priv->available_users = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)chooser_user_free);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->iconview = gtk_icon_view_new ();
        gtk_icon_view_set_selection_mode (GTK_ICON_VIEW (widget->priv->iconview), GTK_SELECTION_SINGLE);
        gtk_icon_view_set_orientation (GTK_ICON_VIEW (widget->priv->iconview), GTK_ORIENTATION_HORIZONTAL);
        g_signal_connect (widget->priv->iconview,
                          "item-activated",
                          G_CALLBACK (on_item_activated),
                          widget);
        g_signal_connect (widget->priv->iconview,
                          "selection-changed",
                          G_CALLBACK (on_selection_changed),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->iconview);

        model = (GtkTreeModel *)gtk_list_store_new (4,
                                                    GDK_TYPE_PIXBUF,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING);
        gtk_icon_view_set_model (GTK_ICON_VIEW (widget->priv->iconview), model);

        gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (widget->priv->iconview), CHOOSER_LIST_PIXBUF_COLUMN);
        gtk_icon_view_set_markup_column (GTK_ICON_VIEW (widget->priv->iconview), CHOOSER_LIST_CAPTION_COLUMN);
        gtk_icon_view_set_tooltip_column (GTK_ICON_VIEW (widget->priv->iconview), CHOOSER_LIST_TOOLTIP_COLUMN);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
                                         CHOOSER_LIST_CAPTION_COLUMN,
                                         compare_user,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                              CHOOSER_LIST_CAPTION_COLUMN,
                                              GTK_SORT_ASCENDING);

        collect_users (widget);

        populate_model (widget, model);
}

static void
gdm_user_chooser_widget_finalize (GObject *object)
{
        GdmUserChooserWidget *user_chooser_widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (object));

        user_chooser_widget = GDM_USER_CHOOSER_WIDGET (object);

        g_return_if_fail (user_chooser_widget->priv != NULL);

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
