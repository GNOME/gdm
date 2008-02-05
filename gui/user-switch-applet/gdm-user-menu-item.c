/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <unistd.h>
#include <sys/types.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gdm-user-menu-item.h"

#define DEFAULT_ICON_SIZE               24
#define CLOSE_ENOUGH_SIZE               2

enum
{
        PROP_0,
        PROP_USER,
        PROP_ICON_SIZE
};

struct _GdmUserMenuItem
{
        GtkImageMenuItem parent;

        GdmUser         *user;

        GtkWidget       *image;
        GtkWidget       *label;

        gulong           user_notify_id;
        gulong           user_icon_changed_id;
        gulong           user_sessions_changed_id;
        gint             icon_size;
};

struct _GdmUserMenuItemClass
{
        GtkImageMenuItemClass parent_class;
};

G_DEFINE_TYPE (GdmUserMenuItem, gdm_user_menu_item, GTK_TYPE_IMAGE_MENU_ITEM);


static void
user_weak_notify (gpointer  data,
                  GObject  *user_ptr)
{
        GDM_USER_MENU_ITEM (data)->user = NULL;

        gtk_widget_destroy (data);
}


static void
reset_label (GdmUserMenuItem *item)
{
        gtk_label_set_markup (GTK_LABEL (item->label),
                              gdm_user_get_real_name (item->user));
}

static void
reset_icon (GdmUserMenuItem *item)
{
        GdkPixbuf *pixbuf;

        if (!item->user || !gtk_widget_has_screen (GTK_WIDGET (item)))
                return;

        g_assert (item->icon_size != 0);

        pixbuf = gdm_user_render_icon (item->user, item->icon_size);
        if (pixbuf != NULL) {
                gtk_image_set_from_pixbuf (GTK_IMAGE (item->image), pixbuf);
                g_object_unref (pixbuf);
        }
}

static void
user_notify_cb (GObject    *object,
                GParamSpec *pspec,
                gpointer    data)
{
        if (!pspec || !pspec->name)
                return;

        if (strcmp (pspec->name, "user-name") == 0 ||
            strcmp (pspec->name, "display-name") == 0)
                reset_label (data);
}


static void
user_icon_changed_cb (GdmUser *user,
                      gpointer  data)
{
        if (gtk_widget_has_screen (data))
                reset_icon (data);
}

static void
user_sessions_changed_cb (GdmUser *user,
                          gpointer  data)
{
        if (gdm_user_get_uid (user) == getuid ())
                gtk_widget_set_sensitive (data, (gdm_user_get_num_sessions (user) > 1));
}

static void
_gdm_user_menu_item_set_user (GdmUserMenuItem *item,
                              GdmUser         *user)
{
        item->user = user;
        g_object_weak_ref (G_OBJECT (item->user), user_weak_notify, item);
        item->user_notify_id = g_signal_connect (item->user, "notify",
                                                 G_CALLBACK (user_notify_cb), item);
        item->user_icon_changed_id = g_signal_connect (item->user, "icon-changed",
                                                       G_CALLBACK (user_icon_changed_cb), item);
        item->user_sessions_changed_id = g_signal_connect (item->user, "sessions-changed",
                                                           G_CALLBACK (user_sessions_changed_cb), item);

        if (gtk_widget_get_style (GTK_WIDGET (item))) {
                reset_icon (item);
                reset_label (item);
        }
}

static void
gdm_user_menu_item_set_property (GObject      *object,
                                 guint         param_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        GdmUserMenuItem *item;

        item = GDM_USER_MENU_ITEM (object);

        switch (param_id) {
        case PROP_USER:
                _gdm_user_menu_item_set_user (item, g_value_get_object (value));
                break;
        case PROP_ICON_SIZE:
                gdm_user_menu_item_set_icon_size (item, g_value_get_int (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_user_menu_item_get_property (GObject    *object,
                                  guint       param_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmUserMenuItem *item;

        item = GDM_USER_MENU_ITEM (object);

        switch (param_id) {
        case PROP_USER:
                g_value_set_object (value, item->user);
                break;
        case PROP_ICON_SIZE:
                g_value_set_int (value, item->icon_size);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}


static void
gdm_user_menu_item_finalize (GObject *object)
{
        GdmUserMenuItem *item;

        item = GDM_USER_MENU_ITEM (object);

        if (item->user) {
                g_signal_handler_disconnect (item->user, item->user_notify_id);
                g_signal_handler_disconnect (item->user, item->user_icon_changed_id);
                g_signal_handler_disconnect (item->user, item->user_sessions_changed_id);
                g_object_weak_unref (G_OBJECT (item->user), user_weak_notify, object);
        }

        if (G_OBJECT_CLASS (gdm_user_menu_item_parent_class)->finalize)
                (*G_OBJECT_CLASS (gdm_user_menu_item_parent_class)->finalize) (object);
}


static gboolean
gdm_user_menu_item_expose_event (GtkWidget      *widget,
                                 GdkEventExpose *event)
{
        int           horizontal_padding;
        int           indicator_size;
        int           indicator_spacing;
        int           offset;
        int           x;
        int           y;
        GtkShadowType shadow_type;
        gboolean      retval;

        if (GTK_WIDGET_CLASS (gdm_user_menu_item_parent_class)->expose_event) {
                retval = (*GTK_WIDGET_CLASS (gdm_user_menu_item_parent_class)->expose_event) (widget,
                                                                                              event);
        } else {
                retval = TRUE;
        }

        if (! GTK_WIDGET_DRAWABLE (widget)) {
                return retval;
        }

        horizontal_padding = 0;
        indicator_size = 0;
        indicator_spacing = 0;
        gtk_widget_style_get (widget,
                              "horizontal-padding", &horizontal_padding,
                              "indicator-size", &indicator_size,
                              "indicator-spacing", &indicator_spacing,
                              NULL);

        offset = GTK_CONTAINER (widget)->border_width + widget->style->xthickness + 2;

        if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR) {
                x = widget->allocation.x + widget->allocation.width -
                        offset - horizontal_padding - indicator_size + indicator_spacing +
                        (indicator_size - indicator_spacing - indicator_size) / 2;
        } else {
                x = widget->allocation.x + offset + horizontal_padding +
                        (indicator_size - indicator_spacing - indicator_size) / 2;
        }

        y = widget->allocation.y + (widget->allocation.height - indicator_size) / 2;

        if (gdm_user_get_num_sessions (GDM_USER_MENU_ITEM (widget)->user) > 0) {
                shadow_type = GTK_SHADOW_IN; /* they have displays, so mark it checked */
        } else {
                shadow_type = GTK_SHADOW_OUT; /* they haave no displays, so no check */
        }

        gtk_paint_check (widget->style, widget->window, GTK_WIDGET_STATE (widget),
                         shadow_type, &(event->area), widget, "check",
                         x, y, indicator_size, indicator_size);

        return TRUE;
}

static void
gdm_user_menu_item_size_request (GtkWidget      *widget,
                                 GtkRequisition *req)
{
        int indicator_size;
        int indicator_spacing;

        if (GTK_WIDGET_CLASS (gdm_user_menu_item_parent_class)->size_request)
                (*GTK_WIDGET_CLASS (gdm_user_menu_item_parent_class)->size_request) (widget,
                                                                                     req);

        indicator_size = 0;
        indicator_spacing = 0;
        gtk_widget_style_get (widget,
                              "indicator-size", &indicator_size,
                              "indicator-spacing", &indicator_spacing,
                              NULL);
        req->width += indicator_size + indicator_spacing;
}

static void
gdm_user_menu_item_class_init (GdmUserMenuItemClass *class)
{
        GObjectClass   *gobject_class;
        GtkWidgetClass *widget_class;

        gobject_class = G_OBJECT_CLASS (class);
        widget_class = GTK_WIDGET_CLASS (class);

        gobject_class->set_property = gdm_user_menu_item_set_property;
        gobject_class->get_property = gdm_user_menu_item_get_property;
        gobject_class->finalize = gdm_user_menu_item_finalize;

        widget_class->size_request = gdm_user_menu_item_size_request;
        widget_class->expose_event = gdm_user_menu_item_expose_event;

        g_object_class_install_property (gobject_class,
                                         PROP_USER,
                                         g_param_spec_object ("user",
                                                              _("User"),
                                                              _("The user this menu item represents."),
                                                              GDM_TYPE_USER,
                                                              (G_PARAM_READWRITE |
                                                               G_PARAM_CONSTRUCT_ONLY)));
        g_object_class_install_property (gobject_class,
                                         PROP_ICON_SIZE,
                                         g_param_spec_int ("icon-size",
                                                           _("Icon Size"),
                                                           _("The size of the icon to use."),
                                                           12, G_MAXINT, DEFAULT_ICON_SIZE,
                                                           G_PARAM_READWRITE));

        gtk_widget_class_install_style_property (widget_class,
                                                 g_param_spec_int ("indicator-size",
                                                                   _("Indicator Size"),
                                                                   _("Size of check indicator"),
                                                                   0, G_MAXINT, 12,
                                                                   G_PARAM_READABLE));
        gtk_widget_class_install_style_property (widget_class,
                                                 g_param_spec_int ("indicator-spacing",
                                                                   _("Indicator Spacing"),
                                                                   _("Space between the username and the indicator"),
                                                                   0, G_MAXINT, 8,
                                                                   G_PARAM_READABLE));
}


static void
image_style_set_cb (GtkWidget *widget,
                    GtkStyle  *old_style,
                    gpointer   data)
{
        reset_icon (data);
}

static void
label_style_set_cb (GtkWidget *widget,
                    GtkStyle  *old_style,
                    gpointer   data)
{
        reset_label (data);
}

static void
gdm_user_menu_item_init (GdmUserMenuItem *item)
{
        GtkWidget *box;

        item->icon_size = DEFAULT_ICON_SIZE;

        item->image = gtk_image_new ();
        g_signal_connect (item->image, "style-set",
                          G_CALLBACK (image_style_set_cb), item);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                       item->image);
        gtk_widget_show (item->image);

        box = gtk_hbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (item), box);
        gtk_widget_show (box);

        item->label = gtk_label_new (NULL);
        gtk_label_set_use_markup (GTK_LABEL (item->label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (item->label), 0.0, 0.5);
        g_signal_connect (item->label, "style-set",
                          G_CALLBACK (label_style_set_cb), item);
        gtk_container_add (GTK_CONTAINER (box), item->label);
        gtk_widget_show (item->label);
}

GtkWidget *
gdm_user_menu_item_new (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return g_object_new (GDM_TYPE_USER_MENU_ITEM, "user", user, NULL);
}

GdmUser *
gdm_user_menu_item_get_user (GdmUserMenuItem *item)
{
        g_return_val_if_fail (GDM_IS_USER_MENU_ITEM (item), NULL);

        return item->user;
}

int
gdm_user_menu_item_get_icon_size (GdmUserMenuItem *item)
{
        g_return_val_if_fail (GDM_IS_USER_MENU_ITEM (item), -1);

        return item->icon_size;
}

void
gdm_user_menu_item_set_icon_size (GdmUserMenuItem *item,
                                  int              pixel_size)
{
        g_return_if_fail (GDM_IS_USER_MENU_ITEM (item));
        g_return_if_fail (pixel_size != 0);

        if (pixel_size < 0)
                item->icon_size = DEFAULT_ICON_SIZE;
        else
                item->icon_size = pixel_size;

        if (gtk_widget_get_style (GTK_WIDGET (item))) {
                reset_icon (item);
                reset_label (item);
        }

        g_object_notify (G_OBJECT (item), "icon-size");
}
