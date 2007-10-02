/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 The Free Software Foundation
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
 */

/* Adapted from eog eog-thumb-nav.c */

#include "config.h"

#include "gdm-icon-nav.h"
/*#include "gdm-icon-view.h"*/

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#define GDM_ICON_NAV_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GDM_TYPE_ICON_NAV, GdmIconNavPrivate))

G_DEFINE_TYPE (GdmIconNav, gdm_icon_nav, GTK_TYPE_HBOX);

#define GDM_ICON_NAV_SCROLL_INC      20
#define GDM_ICON_NAV_SCROLL_MOVE     20
#define GDM_ICON_NAV_SCROLL_TIMEOUT  20

enum {
        PROP_SHOW_BUTTONS = 1,
        PROP_ICON_VIEW,
        PROP_MODE
};

struct _GdmIconNavPrivate {
        GdmIconNavMode   mode;

        gboolean          show_buttons;
        gboolean          scroll_dir;
        gint              scroll_pos;
        gint              scroll_id;

        GtkWidget        *button_left;
        GtkWidget        *button_right;
        GtkWidget        *sw;
        GtkWidget        *scale;
        GtkWidget        *iconview;
};

static gboolean
gdm_icon_nav_scroll_event (GtkWidget      *widget,
                           GdkEventScroll *event,
                           gpointer        user_data)
{
        GdmIconNav *nav = GDM_ICON_NAV (user_data);
        GtkAdjustment *adj;
        gint inc = GDM_ICON_NAV_SCROLL_INC * 3;

        if (nav->priv->mode != GDM_ICON_NAV_MODE_ONE_ROW)
                return FALSE;

        adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (nav->priv->sw));

        switch (event->direction) {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
                inc *= -1;
                break;

        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
                break;

        default:
                g_assert_not_reached ();
                return FALSE;
        }

        if (inc < 0)
                adj->value = MAX (0, adj->value + inc);
        else
                adj->value = MIN (adj->upper - adj->page_size, adj->value + inc);

        gtk_adjustment_value_changed (adj);

        return TRUE;
}

static void
gdm_icon_nav_adj_changed (GtkAdjustment *adj,
                          gpointer       user_data)
{
        GdmIconNav *nav;
        GdmIconNavPrivate *priv;

        nav = GDM_ICON_NAV (user_data);
        priv = GDM_ICON_NAV_GET_PRIVATE (nav);

        gtk_widget_set_sensitive (priv->button_right, adj->upper > adj->page_size);
}

static void
gdm_icon_nav_adj_value_changed (GtkAdjustment *adj,
                                gpointer       user_data)
{
        GdmIconNav *nav;
        GdmIconNavPrivate *priv;

        nav = GDM_ICON_NAV (user_data);
        priv = GDM_ICON_NAV_GET_PRIVATE (nav);

        gtk_widget_set_sensitive (priv->button_left, adj->value > 0);

        gtk_widget_set_sensitive (priv->button_right,
                                  adj->value < adj->upper - adj->page_size);
}

static gboolean
gdm_icon_nav_scroll_step (gpointer user_data)
{
        static GtkAdjustment *adj = NULL;
        GdmIconNav *nav = GDM_ICON_NAV (user_data);
        gint delta;

        if (nav->priv->scroll_pos < 10)
                delta = GDM_ICON_NAV_SCROLL_INC;
        else if (nav->priv->scroll_pos < 20)
                delta = GDM_ICON_NAV_SCROLL_INC * 2;
        else if (nav->priv->scroll_pos < 30)
                delta = GDM_ICON_NAV_SCROLL_INC * 2 + 5;
        else
                delta = GDM_ICON_NAV_SCROLL_INC * 2 + 12;

        if (adj == NULL)
                adj = gtk_scrolled_window_get_hadjustment (
                                                           GTK_SCROLLED_WINDOW (nav->priv->sw));

        if (!nav->priv->scroll_dir)
                delta *= -1;

        if ((gint) (adj->value + (gdouble) delta) >= 0 &&
            (gint) (adj->value + (gdouble) delta) <= adj->upper - adj->page_size) {
                adj->value += (gdouble) delta;
                nav->priv->scroll_pos++;
                gtk_adjustment_value_changed (adj);
        } else {
                if (delta > 0)
                        adj->value = adj->upper - adj->page_size;
                else
                        adj->value = 0;

                nav->priv->scroll_pos = 0;

                gtk_adjustment_value_changed (adj);

                return FALSE;
        }

        return TRUE;
}

static void
gdm_icon_nav_button_clicked (GtkButton  *button,
                             GdmIconNav *nav)
{
        nav->priv->scroll_pos = 0;

        nav->priv->scroll_dir = (GTK_WIDGET (button) == nav->priv->button_right);

        gdm_icon_nav_scroll_step (nav);
}

static void
gdm_icon_nav_start_scroll (GtkButton  *button,
                           GdmIconNav *nav)
{
        nav->priv->scroll_dir = (GTK_WIDGET (button) == nav->priv->button_right);

        nav->priv->scroll_id = g_timeout_add (GDM_ICON_NAV_SCROLL_TIMEOUT,
                                              gdm_icon_nav_scroll_step,
                                              nav);
}

static void
gdm_icon_nav_stop_scroll (GtkButton  *button,
                          GdmIconNav *nav)
{
        if (nav->priv->scroll_id > 0) {
                g_source_remove (nav->priv->scroll_id);
                nav->priv->scroll_id = 0;
                nav->priv->scroll_pos = 0;
        }
}

static void
gdm_icon_nav_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        GdmIconNav *nav = GDM_ICON_NAV (object);

        switch (prop_id) {
        case PROP_SHOW_BUTTONS:
                g_value_set_boolean (value,
                                     gdm_icon_nav_get_show_buttons (nav));
                break;
        case PROP_ICON_VIEW:
                g_value_set_object (value, nav->priv->iconview);
                break;
        case PROP_MODE:
                g_value_set_int (value, gdm_icon_nav_get_mode (nav));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_icon_nav_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
        GdmIconNav *nav = GDM_ICON_NAV (object);

        switch (prop_id) {
        case PROP_SHOW_BUTTONS:
                gdm_icon_nav_set_show_buttons (nav, g_value_get_boolean (value));
                break;
        case PROP_ICON_VIEW:
                nav->priv->iconview = GTK_WIDGET (g_value_get_object (value));
                break;
        case PROP_MODE:
                gdm_icon_nav_set_mode (nav, g_value_get_int (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_icon_nav_constructor (GType                  type,
                          guint                  n_construct_properties,
                          GObjectConstructParam *construct_params)
{
        GObject *object;
        GdmIconNavPrivate *priv;

        object = G_OBJECT_CLASS (gdm_icon_nav_parent_class)->constructor
                (type, n_construct_properties, construct_params);

        priv = GDM_ICON_NAV (object)->priv;

        if (priv->iconview != NULL) {
                gtk_container_add (GTK_CONTAINER (priv->sw), priv->iconview);
                gtk_widget_show_all (priv->sw);
        }

        return object;
}

static void
gdm_icon_nav_class_init (GdmIconNavClass *class)
{
        GObjectClass *g_object_class = (GObjectClass *) class;

        g_object_class->constructor  = gdm_icon_nav_constructor;
        g_object_class->get_property = gdm_icon_nav_get_property;
        g_object_class->set_property = gdm_icon_nav_set_property;

        g_object_class_install_property (g_object_class,
                                         PROP_SHOW_BUTTONS,
                                         g_param_spec_boolean ("show-buttons",
                                                               "Show Buttons",
                                                               "Whether to show navigation buttons or not",
                                                               TRUE,
                                                               (G_PARAM_READABLE | G_PARAM_WRITABLE)));

        g_object_class_install_property (g_object_class,
                                         PROP_ICON_VIEW,
                                         g_param_spec_object ("iconview",
                                                              "Iconnail View",
                                                              "The internal iconnail viewer widget",
                                                              GTK_TYPE_ICON_VIEW,
                                                              (G_PARAM_CONSTRUCT_ONLY |
                                                               G_PARAM_READABLE |
                                                               G_PARAM_WRITABLE)));

        g_object_class_install_property (g_object_class,
                                         PROP_MODE,
                                         g_param_spec_int ("mode",
                                                           "Mode",
                                                           "Icon navigator mode",
                                                           GDM_ICON_NAV_MODE_ONE_ROW,
                                                           GDM_ICON_NAV_MODE_MULTIPLE_ROWS,
                                                           GDM_ICON_NAV_MODE_ONE_ROW,
                                                           (G_PARAM_READABLE | G_PARAM_WRITABLE)));

        g_type_class_add_private (g_object_class, sizeof (GdmIconNavPrivate));
}

static void
gdm_icon_nav_init (GdmIconNav *nav)
{
        GdmIconNavPrivate *priv;
        GtkAdjustment *adj;
        GtkWidget *arrow;

        nav->priv = GDM_ICON_NAV_GET_PRIVATE (nav);

        priv = nav->priv;

        priv->mode = GDM_ICON_NAV_MODE_ONE_ROW;

        priv->show_buttons = TRUE;

        priv->button_left = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (priv->button_left), GTK_RELIEF_NONE);

        arrow = gtk_arrow_new (GTK_ARROW_LEFT, GTK_SHADOW_ETCHED_IN);
        gtk_container_add (GTK_CONTAINER (priv->button_left), arrow);

        gtk_widget_set_size_request (GTK_WIDGET (priv->button_left), 25, 0);

        gtk_box_pack_start (GTK_BOX (nav), priv->button_left, FALSE, FALSE, 0);

        g_signal_connect (priv->button_left,
                          "clicked",
                          G_CALLBACK (gdm_icon_nav_button_clicked),
                          nav);

        g_signal_connect (priv->button_left,
                          "pressed",
                          G_CALLBACK (gdm_icon_nav_start_scroll),
                          nav);

        g_signal_connect (priv->button_left,
                          "released",
                          G_CALLBACK (gdm_icon_nav_stop_scroll),
                          nav);

        priv->sw = gtk_scrolled_window_new (NULL, NULL);

        gtk_widget_set_name (GTK_SCROLLED_WINDOW (priv->sw)->hscrollbar,
                             "gdm-image-collection-scrollbar");

        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (priv->sw),
                                             GTK_SHADOW_IN);

        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw),
                                        GTK_POLICY_AUTOMATIC,
                                        GTK_POLICY_NEVER);

        g_signal_connect (priv->sw,
                          "scroll-event",
                          G_CALLBACK (gdm_icon_nav_scroll_event),
                          nav);

        adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (priv->sw));

        g_signal_connect (adj,
                          "changed",
                          G_CALLBACK (gdm_icon_nav_adj_changed),
                          nav);

        g_signal_connect (adj,
                          "value-changed",
                          G_CALLBACK (gdm_icon_nav_adj_value_changed),
                          nav);

        gtk_box_pack_start (GTK_BOX (nav), priv->sw, TRUE, TRUE, 0);

        priv->button_right = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (priv->button_right), GTK_RELIEF_NONE);

        arrow = gtk_arrow_new (GTK_ARROW_RIGHT, GTK_SHADOW_NONE);
        gtk_container_add (GTK_CONTAINER (priv->button_right), arrow);

        gtk_widget_set_size_request (GTK_WIDGET (priv->button_right), 25, 0);

        gtk_box_pack_start (GTK_BOX (nav), priv->button_right, FALSE, FALSE, 0);

        g_signal_connect (priv->button_right,
                          "clicked",
                          G_CALLBACK (gdm_icon_nav_button_clicked),
                          nav);

        g_signal_connect (priv->button_right,
                          "pressed",
                          G_CALLBACK (gdm_icon_nav_start_scroll),
                          nav);

        g_signal_connect (priv->button_right,
                          "released",
                          G_CALLBACK (gdm_icon_nav_stop_scroll),
                          nav);

        gtk_adjustment_value_changed (adj);
}

GtkWidget *
gdm_icon_nav_new (GtkWidget       *iconview,
                  GdmIconNavMode   mode,
                  gboolean         show_buttons)
{
        GObject *nav;

        nav = g_object_new (GDM_TYPE_ICON_NAV,
                            "show-buttons", show_buttons,
                            "mode", mode,
                            "iconview", iconview,
                            "homogeneous", FALSE,
                            "spacing", 0,
                            NULL);

        return GTK_WIDGET (nav);
}

gboolean
gdm_icon_nav_get_show_buttons (GdmIconNav *nav)
{
        g_return_val_if_fail (GDM_IS_ICON_NAV (nav), FALSE);

        return nav->priv->show_buttons;
}

void
gdm_icon_nav_set_show_buttons (GdmIconNav *nav,
                               gboolean    show_buttons)
{
        g_return_if_fail (GDM_IS_ICON_NAV (nav));
        g_return_if_fail (nav->priv->button_left  != NULL);
        g_return_if_fail (nav->priv->button_right != NULL);

        nav->priv->show_buttons = show_buttons;

        if (show_buttons &&
            nav->priv->mode == GDM_ICON_NAV_MODE_ONE_ROW) {
                gtk_widget_show_all (nav->priv->button_left);
                gtk_widget_show_all (nav->priv->button_right);
        } else {
                gtk_widget_hide_all (nav->priv->button_left);
                gtk_widget_hide_all (nav->priv->button_right);
        }
}

GdmIconNavMode
gdm_icon_nav_get_mode (GdmIconNav *nav)
{
        g_return_val_if_fail (GDM_IS_ICON_NAV (nav), FALSE);

        return nav->priv->mode;
}

void
gdm_icon_nav_set_mode (GdmIconNav    *nav,
                       GdmIconNavMode mode)
{
        GdmIconNavPrivate *priv;

        g_return_if_fail (GDM_IS_ICON_NAV (nav));

        priv = nav->priv;

        priv->mode = mode;

        switch (mode) {
        case GDM_ICON_NAV_MODE_ONE_ROW:
                gtk_icon_view_set_columns (GTK_ICON_VIEW (priv->iconview),
                                           G_MAXINT);

                gtk_widget_set_size_request (priv->iconview, -1, 123);
#if 0
                gdm_icon_view_set_item_height (GDM_ICON_VIEW (priv->iconview),
                                                115);
#endif
                gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw),
                                                GTK_POLICY_AUTOMATIC,
                                                GTK_POLICY_NEVER);

                gdm_icon_nav_set_show_buttons (nav, priv->show_buttons);

                break;

        case GDM_ICON_NAV_MODE_ONE_COLUMN:
                gtk_icon_view_set_columns (GTK_ICON_VIEW (priv->iconview), 1);

                gtk_widget_set_size_request (priv->iconview, 113, -1);
#if 0
                gdm_icon_view_set_item_height (GDM_ICON_VIEW (priv->iconview),
                                                -1);
#endif

                gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw),
                                                GTK_POLICY_NEVER,
                                                GTK_POLICY_AUTOMATIC);

                gtk_widget_hide_all (priv->button_left);
                gtk_widget_hide_all (priv->button_right);

                break;

        case GDM_ICON_NAV_MODE_MULTIPLE_ROWS:
                gtk_icon_view_set_columns (GTK_ICON_VIEW (priv->iconview), -1);

                gtk_widget_set_size_request (priv->iconview, -1, 220);
#if 0
                gdm_icon_view_set_item_height (GDM_ICON_VIEW (priv->iconview),
                                                -1);
#endif
                gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw),
                                                GTK_POLICY_NEVER,
                                                GTK_POLICY_AUTOMATIC);

                gtk_widget_hide_all (priv->button_left);
                gtk_widget_hide_all (priv->button_right);

                break;

        case GDM_ICON_NAV_MODE_MULTIPLE_COLUMNS:
                gtk_icon_view_set_columns (GTK_ICON_VIEW (priv->iconview), -1);

                gtk_widget_set_size_request (priv->iconview, 230, -1);
#if 0
                gdm_icon_view_set_item_height (GDM_ICON_VIEW (priv->iconview),
                                                -1);
#endif
                gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw),
                                                GTK_POLICY_NEVER,
                                                GTK_POLICY_AUTOMATIC);

                gtk_widget_hide_all (priv->button_left);
                gtk_widget_hide_all (priv->button_right);

                break;
        }
}
