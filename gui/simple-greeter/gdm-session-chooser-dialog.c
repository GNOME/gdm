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

#include "gdm-session-chooser-widget.h"
#include "gdm-session-chooser-dialog.h"

#define GDM_SESSION_CHOOSER_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_CHOOSER_DIALOG, GdmSessionChooserDialogPrivate))

struct GdmSessionChooserDialogPrivate
{
        GtkWidget *chooser_widget;
};

enum {
        PROP_0,
};

static void     gdm_session_chooser_dialog_class_init  (GdmSessionChooserDialogClass *klass);
static void     gdm_session_chooser_dialog_init        (GdmSessionChooserDialog      *session_chooser_dialog);
static void     gdm_session_chooser_dialog_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmSessionChooserDialog, gdm_session_chooser_dialog, GTK_TYPE_DIALOG)

char *
gdm_session_chooser_dialog_get_current_session_name (GdmSessionChooserDialog *dialog)
{
        char *session_name;

        g_return_val_if_fail (GDM_IS_SESSION_CHOOSER_DIALOG (dialog), NULL);

        session_name = gdm_session_chooser_widget_get_current_session_name (GDM_SESSION_CHOOSER_WIDGET (dialog->priv->chooser_widget));

        return session_name;
}

static void
gdm_session_chooser_dialog_set_property (GObject        *object,
                                         guint           prop_id,
                                         const GValue   *value,
                                         GParamSpec     *pspec)
{
        GdmSessionChooserDialog *self;

        self = GDM_SESSION_CHOOSER_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_chooser_dialog_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmSessionChooserDialog *self;

        self = GDM_SESSION_CHOOSER_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_session_chooser_dialog_constructor (GType                  type,
                                        guint                  n_construct_properties,
                                        GObjectConstructParam *construct_properties)
{
        GdmSessionChooserDialog      *session_chooser_dialog;
        GdmSessionChooserDialogClass *klass;

        klass = GDM_SESSION_CHOOSER_DIALOG_CLASS (g_type_class_peek (GDM_TYPE_SESSION_CHOOSER_DIALOG));

        session_chooser_dialog = GDM_SESSION_CHOOSER_DIALOG (G_OBJECT_CLASS (gdm_session_chooser_dialog_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (session_chooser_dialog);
}

static void
gdm_session_chooser_dialog_dispose (GObject *object)
{
        GdmSessionChooserDialog *session_chooser_dialog;

        session_chooser_dialog = GDM_SESSION_CHOOSER_DIALOG (object);

        G_OBJECT_CLASS (gdm_session_chooser_dialog_parent_class)->dispose (object);
}

static void
gdm_session_chooser_dialog_class_init (GdmSessionChooserDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_chooser_dialog_get_property;
        object_class->set_property = gdm_session_chooser_dialog_set_property;
        object_class->constructor = gdm_session_chooser_dialog_constructor;
        object_class->dispose = gdm_session_chooser_dialog_dispose;
        object_class->finalize = gdm_session_chooser_dialog_finalize;

        g_type_class_add_private (klass, sizeof (GdmSessionChooserDialogPrivate));
}

static void
on_response (GdmSessionChooserDialog *dialog,
             gint                      response_id)
{
        switch (response_id) {
        default:
                break;
        }
}

static void
gdm_session_chooser_dialog_init (GdmSessionChooserDialog *dialog)
{

        dialog->priv = GDM_SESSION_CHOOSER_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->chooser_widget = gdm_session_chooser_widget_new ();
        gdm_session_chooser_widget_set_current_session_name (GDM_SESSION_CHOOSER_WIDGET (dialog->priv->chooser_widget), g_getenv ("LANG"));

        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), dialog->priv->chooser_widget);

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
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
gdm_session_chooser_dialog_finalize (GObject *object)
{
        GdmSessionChooserDialog *session_chooser_dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_CHOOSER_DIALOG (object));

        session_chooser_dialog = GDM_SESSION_CHOOSER_DIALOG (object);

        g_return_if_fail (session_chooser_dialog->priv != NULL);

        G_OBJECT_CLASS (gdm_session_chooser_dialog_parent_class)->finalize (object);
}

GtkWidget *
gdm_session_chooser_dialog_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_CHOOSER_DIALOG,
                               NULL);

        return GTK_WIDGET (object);
}
