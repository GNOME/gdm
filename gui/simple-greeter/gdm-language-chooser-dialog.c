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
gdm_language_chooser_dialog_size_request (GtkWidget      *widget,
                                       GtkRequisition *requisition)
{
        int            screen_w;
        int            screen_h;
        GtkRequisition child_requisition;

        if (GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gdm_language_chooser_dialog_parent_class)->size_request (widget, requisition);
        }

        screen_w = gdk_screen_get_width (gtk_widget_get_screen (widget));
        screen_h = gdk_screen_get_height (gtk_widget_get_screen (widget));

        gtk_widget_size_request (GTK_BIN (widget)->child, &child_requisition);
        *requisition = child_requisition;

        requisition->width += 2 * GTK_CONTAINER (widget)->border_width;
        requisition->height += 2 * GTK_CONTAINER (widget)->border_width;

        requisition->width = MIN (requisition->width, .50 * screen_w);
        requisition->height = MIN (requisition->height, .80 * screen_h);
}

static void
gdm_language_chooser_dialog_response (GtkDialog *dialog,
                                      int        response_id)
{
        GdmLanguageChooserDialog *chooser_dialog;

        chooser_dialog = GDM_LANGUAGE_CHOOSER_DIALOG (dialog);

        if (response_id == GTK_RESPONSE_OK) {
                gdm_chooser_widget_activate_selected_item (GDM_CHOOSER_WIDGET (chooser_dialog->priv->chooser_widget));
        }
}

static void
gdm_language_chooser_dialog_class_init (GdmLanguageChooserDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
        GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);

        object_class->finalize = gdm_language_chooser_dialog_finalize;
        widget_class->size_request = gdm_language_chooser_dialog_size_request;
#ifdef I_COULD_GO_BACK_IN_TIME_AND_MAKE_RESPONSE_RUN_FIRST
        dialog_class->response = gdm_language_chooser_dialog_response;
#endif

        g_type_class_add_private (klass, sizeof (GdmLanguageChooserDialogPrivate));
}

static void
gdm_language_chooser_dialog_init (GdmLanguageChooserDialog *dialog)
{

        dialog->priv = GDM_LANGUAGE_CHOOSER_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->chooser_widget = gdm_language_chooser_widget_new ();
        gdm_chooser_widget_set_hide_inactive_items (GDM_CHOOSER_WIDGET (dialog->priv->chooser_widget),
                                                    FALSE);
        gtk_widget_show (dialog->priv->chooser_widget);

#ifndef I_COULD_GO_BACK_IN_TIME_AND_MAKE_RESPONSE_RUN_FIRST
        g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (gdm_language_chooser_dialog_response), NULL);
#endif

        gdm_language_chooser_widget_set_current_language_name (GDM_LANGUAGE_CHOOSER_WIDGET (dialog->priv->chooser_widget),
                                                               setlocale (LC_MESSAGES, NULL));
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), dialog->priv->chooser_widget);

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                GTK_STOCK_OK, GTK_RESPONSE_OK,
                                NULL);
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "config-language");
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
        gtk_container_set_border_width (GTK_CONTAINER (dialog->priv->chooser_widget), 5);
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
                               "title", _("Languages"),
                               "border-width", 8,
                               "modal", TRUE,
                               NULL);

        return GTK_WIDGET (object);
}
