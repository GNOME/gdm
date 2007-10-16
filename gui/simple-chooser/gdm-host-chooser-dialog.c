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

#include "gdm-host-chooser-dialog.h"
#include "gdm-host-chooser-widget.h"

#define GDM_HOST_CHOOSER_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_HOST_CHOOSER_DIALOG, GdmHostChooserDialogPrivate))

struct GdmHostChooserDialogPrivate
{
        GtkWidget *chooser_widget;
};

enum {
        PROP_0,
};

static void     gdm_host_chooser_dialog_class_init  (GdmHostChooserDialogClass *klass);
static void     gdm_host_chooser_dialog_init        (GdmHostChooserDialog      *host_chooser_dialog);
static void     gdm_host_chooser_dialog_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmHostChooserDialog, gdm_host_chooser_dialog, GTK_TYPE_DIALOG)

char *
gdm_host_chooser_dialog_get_current_hostname (GdmHostChooserDialog *dialog)
{
        char *hostname;

        g_return_val_if_fail (GDM_IS_HOST_CHOOSER_DIALOG (dialog), NULL);

        hostname = gdm_host_chooser_widget_get_current_hostname (GDM_HOST_CHOOSER_WIDGET (dialog->priv->chooser_widget));

        return hostname;
}
static void
gdm_host_chooser_dialog_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserDialog *self;

        self = GDM_HOST_CHOOSER_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_host_chooser_dialog_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserDialog *self;

        self = GDM_HOST_CHOOSER_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_host_chooser_dialog_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmHostChooserDialog      *host_chooser_dialog;
        GdmHostChooserDialogClass *klass;

        klass = GDM_HOST_CHOOSER_DIALOG_CLASS (g_type_class_peek (GDM_TYPE_HOST_CHOOSER_DIALOG));

        host_chooser_dialog = GDM_HOST_CHOOSER_DIALOG (G_OBJECT_CLASS (gdm_host_chooser_dialog_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (host_chooser_dialog);
}

static void
gdm_host_chooser_dialog_dispose (GObject *object)
{
        GdmHostChooserDialog *host_chooser_dialog;

        host_chooser_dialog = GDM_HOST_CHOOSER_DIALOG (object);

        g_debug ("Disposing host_chooser_dialog");

        G_OBJECT_CLASS (gdm_host_chooser_dialog_parent_class)->dispose (object);
}

static void
gdm_host_chooser_dialog_class_init (GdmHostChooserDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_host_chooser_dialog_get_property;
        object_class->set_property = gdm_host_chooser_dialog_set_property;
        object_class->constructor = gdm_host_chooser_dialog_constructor;
        object_class->dispose = gdm_host_chooser_dialog_dispose;
        object_class->finalize = gdm_host_chooser_dialog_finalize;

        g_type_class_add_private (klass, sizeof (GdmHostChooserDialogPrivate));
}

static void
on_response (GdmHostChooserDialog *dialog,
             gint                  response_id)
{
        switch (response_id) {
        case GTK_RESPONSE_APPLY:
                gdm_host_chooser_widget_refresh (GDM_HOST_CHOOSER_WIDGET (dialog->priv->chooser_widget));
                g_signal_stop_emission_by_name (dialog, "response");
                break;
        default:
                break;
        }
}

static void
gdm_host_chooser_dialog_init (GdmHostChooserDialog *dialog)
{

        dialog->priv = GDM_HOST_CHOOSER_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->chooser_widget = gdm_host_chooser_widget_new ();
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), dialog->priv->chooser_widget);

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_REFRESH, GTK_RESPONSE_APPLY,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                GTK_STOCK_OK, GTK_RESPONSE_OK,
                                NULL);
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);

        gtk_widget_show_all (GTK_WIDGET (dialog));
}

static void
gdm_host_chooser_dialog_finalize (GObject *object)
{
        GdmHostChooserDialog *host_chooser_dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_HOST_CHOOSER_DIALOG (object));

        host_chooser_dialog = GDM_HOST_CHOOSER_DIALOG (object);

        g_return_if_fail (host_chooser_dialog->priv != NULL);

        G_OBJECT_CLASS (gdm_host_chooser_dialog_parent_class)->finalize (object);
}

GtkWidget *
gdm_host_chooser_dialog_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_HOST_CHOOSER_DIALOG,
                               NULL);

        return GTK_WIDGET (object);
}
