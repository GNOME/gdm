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

#include <libbackground/preferences.h>

#include "gdm-greeter-background.h"

#define GDM_GREETER_BACKGROUND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_BACKGROUND, GdmGreeterBackgroundPrivate))

struct GdmGreeterBackgroundPrivate
{
        char *filename;
};

enum {
        PROP_0,
};

static void     gdm_greeter_background_class_init  (GdmGreeterBackgroundClass *klass);
static void     gdm_greeter_background_init        (GdmGreeterBackground      *greeter_background);
static void     gdm_greeter_background_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmGreeterBackground, gdm_greeter_background, GTK_TYPE_WINDOW)

static void
gdm_greeter_background_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec)
{
        GdmGreeterBackground *self;

        self = GDM_GREETER_BACKGROUND (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_background_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec)
{
        GdmGreeterBackground *self;

        self = GDM_GREETER_BACKGROUND (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_greeter_background_constructor (GType                  type,
                                    guint                  n_construct_properties,
                                    GObjectConstructParam *construct_properties)
{
        GdmGreeterBackground      *greeter_background;
        GdmGreeterBackgroundClass *klass;

        klass = GDM_GREETER_BACKGROUND_CLASS (g_type_class_peek (GDM_TYPE_GREETER_BACKGROUND));

        greeter_background = GDM_GREETER_BACKGROUND (G_OBJECT_CLASS (gdm_greeter_background_parent_class)->constructor (type,
                                                                                                                        n_construct_properties,
                                                                                                                        construct_properties));

        return G_OBJECT (greeter_background);
}

static void
gdm_greeter_background_dispose (GObject *object)
{
        GdmGreeterBackground *greeter_background;

        greeter_background = GDM_GREETER_BACKGROUND (object);

        g_debug ("Disposing greeter_background");

        G_OBJECT_CLASS (gdm_greeter_background_parent_class)->dispose (object);
}

static void
gdm_greeter_background_real_map (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize (widget);
        }

        gdk_window_lower (widget->window);
}

static void
on_screen_size_changed (GdkScreen            *screen,
                        GdmGreeterBackground *background)
{
        gtk_widget_queue_resize (GTK_WIDGET (background));
}

static void
gdm_greeter_background_real_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize (widget);
        }

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);
}

static void
gdm_greeter_background_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->unrealize (widget);
        }
}

static void
gdm_greeter_background_class_init (GdmGreeterBackgroundClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_background_get_property;
        object_class->set_property = gdm_greeter_background_set_property;
        object_class->constructor = gdm_greeter_background_constructor;
        object_class->dispose = gdm_greeter_background_dispose;
        object_class->finalize = gdm_greeter_background_finalize;

        widget_class->map = gdm_greeter_background_real_map;
        widget_class->realize = gdm_greeter_background_real_realize;
        widget_class->unrealize = gdm_greeter_background_real_unrealize;

        g_type_class_add_private (klass, sizeof (GdmGreeterBackgroundPrivate));
}

static gint
on_delete_event (GdmGreeterBackground *background)
{
        /* Returning true tells GTK+ not to delete the window. */
        return TRUE;
}

static void
gdm_greeter_background_init (GdmGreeterBackground *background)
{

        background->priv = GDM_GREETER_BACKGROUND_GET_PRIVATE (background);

        gtk_window_set_decorated (GTK_WINDOW (background), FALSE);

        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (background), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (background), TRUE);
        gtk_window_set_resizable (GTK_WINDOW (background), FALSE);
        gtk_window_set_keep_above (GTK_WINDOW (background), TRUE);
        gtk_window_set_type_hint (GTK_WINDOW (background), GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_fullscreen (GTK_WINDOW (background));

        g_signal_connect (background, "delete_event", G_CALLBACK (on_delete_event), NULL);
}

static void
gdm_greeter_background_finalize (GObject *object)
{
        GdmGreeterBackground *greeter_background;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_BACKGROUND (object));

        greeter_background = GDM_GREETER_BACKGROUND (object);

        g_return_if_fail (greeter_background->priv != NULL);

        G_OBJECT_CLASS (gdm_greeter_background_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_background_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_BACKGROUND,
                               NULL);

        return GTK_WIDGET (object);
}
