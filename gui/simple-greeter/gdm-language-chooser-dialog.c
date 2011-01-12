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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gdm-language-chooser-widget.h"
#include "gdm-language-chooser-dialog.h"

#define GDM_LANGUAGE_CHOOSER_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LANGUAGE_CHOOSER_DIALOG, GdmLanguageChooserDialogPrivate))

struct GdmLanguageChooserDialogPrivate
{
        GtkWidget *chooser_widget;
};


static void     gdm_language_chooser_dialog_class_init  (GdmLanguageChooserDialogClass *klass);
static void     gdm_language_chooser_dialog_init        (GdmLanguageChooserDialog      *language_chooser_dialog);
static void     gdm_language_chooser_dialog_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmLanguageChooserDialog, gdm_language_chooser_dialog, GTK_TYPE_DIALOG)

char *
gdm_language_chooser_dialog_get_current_language_name (GdmLanguageChooserDialog *dialog)
{
        char *language_name;

        g_return_val_if_fail (GDM_IS_LANGUAGE_CHOOSER_DIALOG (dialog), NULL);

        language_name = gdm_language_chooser_widget_get_current_language_name (GDM_LANGUAGE_CHOOSER_WIDGET (dialog->priv->chooser_widget));

        return language_name;
}

void
gdm_language_chooser_dialog_set_current_language_name (GdmLanguageChooserDialog *dialog,
                                                       const char               *language_name)
{

        g_return_if_fail (GDM_IS_LANGUAGE_CHOOSER_DIALOG (dialog));

        gdm_language_chooser_widget_set_current_language_name (GDM_LANGUAGE_CHOOSER_WIDGET (dialog->priv->chooser_widget), language_name);
}

static void
gdm_language_chooser_dialog_get_preferred_width (GtkWidget *widget,
                                                 gint      *minimum_size,
                                                 gint      *natural_size)
{
        GtkWidget *child;
        int        min_size, nat_size;
        int        screen_w;

        /* FIXME: this should use monitor size */
        screen_w = gdk_screen_get_width (gtk_widget_get_screen (widget));

        child = gtk_bin_get_child (GTK_BIN (widget));

        min_size = 0;
        nat_size = 0;

        if (GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->get_preferred_width) {
                GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->get_preferred_width (widget, &min_size, &nat_size);
        }

        if (child && gtk_widget_get_visible (child)) {
                gtk_widget_get_preferred_width (child,
                                                &min_size,
                                                &nat_size);
        }

        min_size += 2 * gtk_container_get_border_width (GTK_CONTAINER (widget));
        min_size = MIN (min_size, .50 * screen_w);
        nat_size += 2 * gtk_container_get_border_width (GTK_CONTAINER (widget));
        nat_size = MIN (nat_size, .50 * screen_w);

        if (minimum_size)
                *minimum_size = min_size;
        if (natural_size)
                *natural_size = nat_size;
}

static void
gdm_language_chooser_dialog_get_preferred_height (GtkWidget *widget,
                                                  gint      *minimum_size,
                                                  gint      *natural_size)
{
        GtkWidget *child;
        int        min_size, nat_size;
        int        screen_w;

        /* FIXME: this should use monitor size */
        screen_w = gdk_screen_get_height (gtk_widget_get_screen (widget));

        child = gtk_bin_get_child (GTK_BIN (widget));

        min_size = 0;
        nat_size = 0;

        if (GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->get_preferred_height) {
                GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->get_preferred_height (widget, &min_size, &nat_size);
        }

        if (child && gtk_widget_get_visible (child)) {
                gtk_widget_get_preferred_height (child,
                                                &min_size,
                                                &nat_size);
        }

        min_size += 2 * gtk_container_get_border_width (GTK_CONTAINER (widget));
        min_size = MIN (min_size, .50 * screen_w);
        nat_size += 2 * gtk_container_get_border_width (GTK_CONTAINER (widget));
        nat_size = MIN (nat_size, .50 * screen_w);

        if (minimum_size)
                *minimum_size = min_size;
        if (natural_size)
                *natural_size = nat_size;
}

static void
gdm_language_chooser_dialog_realize (GtkWidget *widget)
{
        GdmLanguageChooserDialog *chooser_dialog;
        GdkWindow *root_window;
        GdkCursor *cursor;

        root_window = gdk_screen_get_root_window (gdk_screen_get_default ());
        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (root_window, cursor);
        gdk_cursor_unref (cursor);

        chooser_dialog = GDM_LANGUAGE_CHOOSER_DIALOG (widget);

        gtk_widget_show (chooser_dialog->priv->chooser_widget);

        GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->realize (widget);

        cursor = gdk_cursor_new (GDK_LEFT_PTR);
        gdk_window_set_cursor (root_window, cursor);
        gdk_cursor_unref (cursor);
}

static void
gdm_language_chooser_dialog_class_init (GdmLanguageChooserDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = gdm_language_chooser_dialog_finalize;
        widget_class->get_preferred_width = gdm_language_chooser_dialog_get_preferred_width;
        widget_class->get_preferred_height = gdm_language_chooser_dialog_get_preferred_height;
        widget_class->realize = gdm_language_chooser_dialog_realize;

        g_type_class_add_private (klass, sizeof (GdmLanguageChooserDialogPrivate));
}

static gboolean
respond (GdmLanguageChooserDialog *dialog)
{
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
        return FALSE;
}

static void
queue_response (GdmLanguageChooserDialog *dialog)
{
        g_idle_add ((GSourceFunc) respond, dialog);
}

static void
gdm_language_chooser_dialog_init (GdmLanguageChooserDialog *dialog)
{

        dialog->priv = GDM_LANGUAGE_CHOOSER_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->chooser_widget = gdm_language_chooser_widget_new ();
        gdm_chooser_widget_set_hide_inactive_items (GDM_CHOOSER_WIDGET (dialog->priv->chooser_widget),
                                                    FALSE);

        gdm_language_chooser_widget_set_current_language_name (GDM_LANGUAGE_CHOOSER_WIDGET (dialog->priv->chooser_widget),
                                                               setlocale (LC_MESSAGES, NULL));
        gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), dialog->priv->chooser_widget);

        g_signal_connect_swapped (G_OBJECT (dialog->priv->chooser_widget),
                                  "activated", G_CALLBACK (queue_response),
                                  dialog);

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                GTK_STOCK_OK, GTK_RESPONSE_OK,
                                NULL);

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
        gtk_container_set_border_width (GTK_CONTAINER (dialog->priv->chooser_widget), 5);
        gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_default_size (GTK_WINDOW (dialog), 512, 440);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
gdm_language_chooser_dialog_finalize (GObject *object)
{
        GdmLanguageChooserDialog *language_chooser_dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LANGUAGE_CHOOSER_DIALOG (object));

        language_chooser_dialog = GDM_LANGUAGE_CHOOSER_DIALOG (object);

        g_return_if_fail (language_chooser_dialog->priv != NULL);

        G_OBJECT_CLASS (gdm_language_chooser_dialog_parent_class)->finalize (object);
}

GtkWidget *
gdm_language_chooser_dialog_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_LANGUAGE_CHOOSER_DIALOG,
                               "icon-name", "preferences-desktop-locale",
                               "title", _("Languages"),
                               "border-width", 8,
                               "modal", TRUE,
                               NULL);

        return GTK_WIDGET (object);
}
