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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gdm-host-chooser-widget.h"

#define GDM_HOST_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_HOST_CHOOSER_WIDGET, GdmHostChooserWidgetPrivate))

struct GdmHostChooserWidgetPrivate
{
        GtkWidget *treeview;
};

enum {
        PROP_0,
};

enum {
        HOSTNAME_ACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };


static void     gdm_host_chooser_widget_class_init  (GdmHostChooserWidgetClass *klass);
static void     gdm_host_chooser_widget_init        (GdmHostChooserWidget      *host_chooser_widget);
static void     gdm_host_chooser_widget_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmHostChooserWidget, gdm_host_chooser_widget, GTK_TYPE_VBOX)

void
gdm_host_chooser_widget_refresh (GdmHostChooserWidget *widget)
{
        g_return_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (widget));
}

char *
gdm_host_chooser_widget_get_current_hostname (GdmHostChooserWidget *widget)
{
        char *hostname;

        g_return_val_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (widget), NULL);

        hostname = NULL;

        return hostname;
}

static void
gdm_host_chooser_widget_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserWidget *self;

        self = GDM_HOST_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_host_chooser_widget_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserWidget *self;

        self = GDM_HOST_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_host_chooser_widget_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmHostChooserWidget      *host_chooser_widget;
        GdmHostChooserWidgetClass *klass;

        klass = GDM_HOST_CHOOSER_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_HOST_CHOOSER_WIDGET));

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (host_chooser_widget);
}

static void
gdm_host_chooser_widget_dispose (GObject *object)
{
        GdmHostChooserWidget *host_chooser_widget;

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (object);

        g_debug ("Disposing host_chooser_widget");

        G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_host_chooser_widget_class_init (GdmHostChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_host_chooser_widget_get_property;
        object_class->set_property = gdm_host_chooser_widget_set_property;
        object_class->constructor = gdm_host_chooser_widget_constructor;
        object_class->dispose = gdm_host_chooser_widget_dispose;
        object_class->finalize = gdm_host_chooser_widget_finalize;

        signals [HOSTNAME_ACTIVATED] = g_signal_new ("hostname-activated",
                                                     G_TYPE_FROM_CLASS (object_class),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GdmHostChooserWidgetClass, hostname_activated),
                                                     NULL,
                                                     NULL,
                                                     g_cclosure_marshal_VOID__STRING,
                                                     G_TYPE_NONE,
                                                     1, G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (GdmHostChooserWidgetPrivate));
}

static void
on_row_activated (GtkTreeView          *tree_view,
                  GtkTreePath          *tree_path,
                  GtkTreeViewColumn    *tree_column,
                  GdmHostChooserWidget *widget)
{
}


static void
gdm_host_chooser_widget_init (GdmHostChooserWidget *widget)
{
        GtkWidget *scrolled;

        widget->priv = GDM_HOST_CHOOSER_WIDGET_GET_PRIVATE (widget);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget->priv->treeview), FALSE);
        g_signal_connect (widget->priv->treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->treeview);

}

static void
gdm_host_chooser_widget_finalize (GObject *object)
{
        GdmHostChooserWidget *host_chooser_widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (object));

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (object);

        g_return_if_fail (host_chooser_widget->priv != NULL);

        G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_host_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_HOST_CHOOSER_WIDGET,
                               NULL);

        return GTK_WIDGET (object);
}
