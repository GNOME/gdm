/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Written by: Ray Strode <rstrode@redhat.com>
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

#include "gdm-extension-list.h"

#define GDM_EXTENSION_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_EXTENSION_LIST, GdmExtensionListPrivate))
typedef gboolean (* GdmExtensionListForeachFunc) (GdmExtensionList    *extension_list,
                                                  GdmLoginExtension *extension,
                                                  gpointer             data);


struct GdmExtensionListPrivate
{
        GtkWidget *box;
        GList     *extensions;
};

enum {
        ACTIVATED = 0,
        DEACTIVATED,
        NUMBER_OF_SIGNALS
};

static guint    signals[NUMBER_OF_SIGNALS];

static void     gdm_extension_list_class_init  (GdmExtensionListClass *klass);
static void     gdm_extension_list_init        (GdmExtensionList      *extension_list);
static void     gdm_extension_list_finalize    (GObject          *object);

G_DEFINE_TYPE (GdmExtensionList, gdm_extension_list, GTK_TYPE_ALIGNMENT);

static void
on_extension_toggled (GdmExtensionList *widget,
                      GtkRadioButton   *button)
{
        GdmLoginExtension *extension;

        extension = g_object_get_data (G_OBJECT (button), "gdm-extension");

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {

                GList     *extension_node;
                /* Sort the list such that the extensions the user clicks last end
                 * up first.  This doesn't change the order in which the extensions
                 * appear in the UI, but will affect which extensions we implicitly
                 * activate if the currently active extension gets disabled.
                 */
                extension_node = g_list_find (widget->priv->extensions, extension);
                if (extension_node != NULL) {
                        widget->priv->extensions = g_list_delete_link (widget->priv->extensions, extension_node);
                        widget->priv->extensions = g_list_prepend (widget->priv->extensions,
                                                              extension);
                }

                g_signal_emit (widget, signals[ACTIVATED], 0, extension);
        } else {
                g_signal_emit (widget, signals[DEACTIVATED], 0, extension);
        }
}

static GdmLoginExtension *
gdm_extension_list_foreach_extension (GdmExtensionList           *extension_list,
                                      GdmExtensionListForeachFunc search_func,
                                      gpointer                           data)
{
        GList *node;

        for (node = extension_list->priv->extensions; node != NULL; node = node->next) {
                GdmLoginExtension *extension;

                extension = node->data;

                if (search_func (extension_list, extension, data)) {
                        return g_object_ref (extension);
                }
        }

        return NULL;
}

static void
on_extension_enabled (GdmExtensionList *extension_list,
                      GdmLoginExtension     *extension)
{
        GtkWidget *button;

        button = g_object_get_data (G_OBJECT (extension), "gdm-extension-list-button");

        gtk_widget_set_sensitive (button, TRUE);
}

static gboolean
gdm_extension_list_set_active_extension (GdmExtensionList    *widget,
                                         GdmLoginExtension *extension)
{
        GtkWidget *button;
        gboolean   was_sensitive;
        gboolean   was_activated;

        if (!gdm_login_extension_is_visible (extension)) {
                return FALSE;
        }

        was_sensitive = gtk_widget_get_sensitive (GTK_WIDGET (widget));
        gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);

        button = GTK_WIDGET (g_object_get_data (G_OBJECT (extension),
                             "gdm-extension-list-button"));

        was_activated = FALSE;
        if (gtk_widget_is_sensitive (button)) {
                if (gtk_widget_activate (button)) {
                        was_activated = TRUE;
                }
        }

        gtk_widget_set_sensitive (GTK_WIDGET (widget), was_sensitive);
        return was_activated;
}

static void
activate_first_available_extension (GdmExtensionList *extension_list)
{
        GList *node;

        node = extension_list->priv->extensions;
        while (node != NULL) {
                GdmLoginExtension   *extension;

                extension = GDM_LOGIN_EXTENSION (node->data);

                if (gdm_extension_list_set_active_extension (extension_list, extension)) {
                        break;
                }

                node = node->next;
        }
}

static void
on_extension_disabled (GdmExtensionList  *extension_list,
                       GdmLoginExtension *extension)
{
        GtkWidget *button;
        gboolean   was_active;

        button = g_object_get_data (G_OBJECT (extension), "gdm-extension-list-button");
        was_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

        gtk_widget_set_sensitive (button, FALSE);

        if (was_active) {
                activate_first_available_extension (extension_list);
        }
}

void
gdm_extension_list_add_extension (GdmExtensionList  *extension_list,
                                  GdmLoginExtension *extension)
{
        GtkWidget *image;
        GtkWidget *button;
        GIcon     *icon;
        char      *description;

        if (extension_list->priv->extensions == NULL) {
                button = gtk_radio_button_new (NULL);
        } else {
                GdmLoginExtension *previous_extension;
                GtkRadioButton *previous_button;

                previous_extension = GDM_LOGIN_EXTENSION (extension_list->priv->extensions->data);
                previous_button = GTK_RADIO_BUTTON (g_object_get_data (G_OBJECT (previous_extension), "gdm-extension-list-button"));
                button = gtk_radio_button_new_from_widget (previous_button);
        }
        g_object_set_data (G_OBJECT (extension), "gdm-extension-list-button", button);

        g_object_set (G_OBJECT (button), "draw-indicator", FALSE, NULL);
        g_object_set_data (G_OBJECT (button), "gdm-extension", extension);
        g_signal_connect_swapped (button, "toggled",
                                  G_CALLBACK (on_extension_toggled),
                                  extension_list);

        gtk_button_set_focus_on_click (GTK_BUTTON (button), FALSE);
        gtk_widget_set_sensitive (button, gdm_login_extension_is_enabled (extension));

        g_signal_connect_swapped (G_OBJECT (extension), "enabled",
                                  G_CALLBACK (on_extension_enabled),
                                  extension_list);

        g_signal_connect_swapped (G_OBJECT (extension), "disabled",
                                  G_CALLBACK (on_extension_disabled),
                                  extension_list);

        icon = gdm_login_extension_get_icon (extension);
        image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_SMALL_TOOLBAR);
        g_object_unref (icon);

        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (button), image);
        description = gdm_login_extension_get_description (extension);
        gtk_widget_set_tooltip_text (button, description);
        g_free (description);
        gtk_widget_show (button);

        gtk_container_add (GTK_CONTAINER (extension_list->priv->box), button);
        extension_list->priv->extensions = g_list_append (extension_list->priv->extensions,
                                                g_object_ref (extension));

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
                g_signal_emit (extension_list, signals[ACTIVATED], 0, extension);
        }
}

void
gdm_extension_list_remove_extension (GdmExtensionList  *extension_list,
                                     GdmLoginExtension *extension)
{
        GtkWidget *button;
        GList     *node;

        node = g_list_find (extension_list->priv->extensions, extension);

        if (node == NULL) {
                return;
        }

        extension_list->priv->extensions = g_list_delete_link (extension_list->priv->extensions, node);

        button = g_object_get_data (G_OBJECT (extension), "gdm-extension-list-button");

        if (button != NULL) {
            g_signal_handlers_disconnect_by_func (G_OBJECT (extension),
                                                  G_CALLBACK (on_extension_enabled),
                                                  extension_list);
            g_signal_handlers_disconnect_by_func (G_OBJECT (extension),
                                                  G_CALLBACK (on_extension_disabled),
                                                  extension_list);
            gtk_widget_destroy (button);
            g_object_set_data (G_OBJECT (extension), "gdm-extension-list-button", NULL);
        }

        g_object_unref (extension);

        activate_first_available_extension (extension_list);
}

static void
gdm_extension_list_class_init (GdmExtensionListClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_extension_list_finalize;

        signals [ACTIVATED] = g_signal_new ("activated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmExtensionListClass, activated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__OBJECT,
                                            G_TYPE_NONE,
                                            1, G_TYPE_OBJECT);

        signals [DEACTIVATED] = g_signal_new ("deactivated",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_FIRST,
                                            G_STRUCT_OFFSET (GdmExtensionListClass, deactivated),
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__OBJECT,
                                            G_TYPE_NONE,
                                            1, G_TYPE_OBJECT);

        g_type_class_add_private (klass, sizeof (GdmExtensionListPrivate));
}

static void
gdm_extension_list_init (GdmExtensionList *widget)
{
        widget->priv = GDM_EXTENSION_LIST_GET_PRIVATE (widget);

        gtk_alignment_set_padding (GTK_ALIGNMENT (widget), 0, 0, 0, 0);
        gtk_alignment_set (GTK_ALIGNMENT (widget), 0.0, 0.0, 0, 0);

        widget->priv->box = gtk_hbox_new (TRUE, 2);
        gtk_widget_show (widget->priv->box);
        gtk_container_add (GTK_CONTAINER (widget),
                           widget->priv->box);
}

static void
gdm_extension_list_finalize (GObject *object)
{
        GdmExtensionList *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_EXTENSION_LIST (object));

        widget = GDM_EXTENSION_LIST (object);

        g_list_foreach (widget->priv->extensions, (GFunc) g_object_unref, NULL);
        g_list_free (widget->priv->extensions);

        G_OBJECT_CLASS (gdm_extension_list_parent_class)->finalize (object);
}

GtkWidget *
gdm_extension_list_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_EXTENSION_LIST, NULL);

        return GTK_WIDGET (object);
}

static gboolean
gdm_extension_list_extension_is_active (GdmExtensionList  *extension_list,
                                        GdmLoginExtension *extension)
{
        GtkWidget *button;

        button = g_object_get_data (G_OBJECT (extension), "gdm-extension-list-button");

        return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
}

GdmLoginExtension *
gdm_extension_list_get_active_extension (GdmExtensionList *widget)
{
        return gdm_extension_list_foreach_extension (widget,
                                                     (GdmExtensionListForeachFunc)
                                                     gdm_extension_list_extension_is_active,
                                                     NULL);
}

int
gdm_extension_list_get_number_of_visible_extensions (GdmExtensionList *widget)
{
        GList *node;
        int number_of_visible_extensions;

        number_of_visible_extensions = 0;
        for (node = widget->priv->extensions; node != NULL; node = node->next) {
                GdmLoginExtension *extension;

                extension = node->data;

                if (gdm_login_extension_is_enabled (extension) && gdm_login_extension_is_visible (extension)) {
                        number_of_visible_extensions++;
                }
        }

        return number_of_visible_extensions;
}
