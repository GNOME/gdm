/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007 Ray Strode <rstrode@redhat.com>
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

#include "gdm-user-manager.h"
#include "gdm-user-chooser-widget.h"

enum {
        USER_NO_DISPLAY              = 1 << 0,
        USER_ACCOUNT_DISABLED        = 1 << 1,
};

#define GDM_USER_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_CHOOSER_WIDGET, GdmUserChooserWidgetPrivate))

#define ICON_SIZE 64

struct GdmUserChooserWidgetPrivate
{
        GdmUserManager     *manager;

        GdkPixbuf          *logged_in_pixbuf;

        guint               show_other_user : 1;
        guint               show_guest_user : 1;
};

enum {
        PROP_0,
};

static void     gdm_user_chooser_widget_class_init  (GdmUserChooserWidgetClass *klass);
static void     gdm_user_chooser_widget_init        (GdmUserChooserWidget      *user_chooser_widget);
static void     gdm_user_chooser_widget_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmUserChooserWidget, gdm_user_chooser_widget, GDM_TYPE_CHOOSER_WIDGET)

void
gdm_user_chooser_widget_set_show_other_user (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_other_user != show_user) {
                widget->priv->show_other_user = show_user;
        }
}

void
gdm_user_chooser_widget_set_show_guest_user (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_guest_user != show_user) {
                widget->priv->show_guest_user = show_user;
        }
}

char *
gdm_user_chooser_widget_get_chosen_user_name (GdmUserChooserWidget *widget)
{
        g_return_val_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget), NULL);

        return gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (widget));
}

void
gdm_user_chooser_widget_set_chosen_user_name (GdmUserChooserWidget *widget,
                                              const char           *name)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_active_item (GDM_CHOOSER_WIDGET (widget), name);
}

void
gdm_user_chooser_widget_set_show_only_chosen (GdmUserChooserWidget *widget,
                                              gboolean              show_only) {
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_hide_inactive_items (GDM_CHOOSER_WIDGET (widget),
                                                    show_only);

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

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_user_chooser_widget_class_init (GdmUserChooserWidgetClass *klass)
{
        GObjectClass          *object_class = G_OBJECT_CLASS (klass);
        object_class->get_property = gdm_user_chooser_widget_get_property;
        object_class->set_property = gdm_user_chooser_widget_set_property;
        object_class->constructor = gdm_user_chooser_widget_constructor;
        object_class->dispose = gdm_user_chooser_widget_dispose;
        object_class->finalize = gdm_user_chooser_widget_finalize;

        g_type_class_add_private (klass, sizeof (GdmUserChooserWidgetPrivate));
}

static GdkPixbuf *
get_pixbuf_for_user (GdmUserChooserWidget *widget,
                     const char           *username)
{
        GtkIconTheme *theme;
        GdkPixbuf    *pixbuf;

        theme = gtk_icon_theme_get_default ();
        pixbuf = gtk_icon_theme_load_icon (theme, "stock_person", ICON_SIZE, 0, NULL);

        return pixbuf;
}

static GdkPixbuf *
get_logged_in_pixbuf (GdmUserChooserWidget *widget)
{
        GtkIconTheme *theme;
        GdkPixbuf    *pixbuf;

        theme = gtk_icon_theme_get_default ();
        pixbuf = gtk_icon_theme_load_icon (theme,
                                           "emblem-default",
                                           ICON_SIZE / 3,
                                           0,
                                           NULL);

        return pixbuf;
}

static gboolean
add_special_users (GdmUserChooserWidget *widget)
{
        GdkPixbuf  *pixbuf;

        widget->priv->logged_in_pixbuf = get_logged_in_pixbuf (widget);

        pixbuf = get_pixbuf_for_user (widget, NULL);

        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_USER_CHOOSER_USER_OTHER,
                                     pixbuf, _("Other..."),
                                     _("Choose a different account"), FALSE,
                                     TRUE);

        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_USER_CHOOSER_USER_GUEST,
                                     pixbuf, _("Guest"),
                                     _("Login as a temporary guest"), FALSE,
                                     TRUE);
        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }

        return FALSE;
}

static void
on_user_added (GdmUserManager       *manager,
               GdmUser              *user,
               GdmUserChooserWidget *widget)
{
        GdkPixbuf    *pixbuf;
        char         *tooltip;

        g_debug ("GdmUserChooserWidget: User added: %s", gdm_user_get_user_name (user));

        pixbuf = gdm_user_render_icon (user, GTK_WIDGET (widget), ICON_SIZE);

        tooltip = g_strdup_printf (_("Log in as %s"),
                                   gdm_user_get_user_name (user));

        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     gdm_user_get_user_name (user),
                                     pixbuf, gdm_user_get_real_name (user),
                                     tooltip, FALSE, FALSE);
        g_free (tooltip);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }
}

static void
on_user_removed (GdmUserManager       *manager,
                 GdmUser              *user,
                 GdmUserChooserWidget *widget)
{
        const char *user_name;

        g_debug ("GdmUserChooserWidget: User removed: %s", gdm_user_get_user_name (user));

        user_name = gdm_user_get_user_name (user);

        gdm_chooser_widget_remove_item (GDM_CHOOSER_WIDGET (widget),
                                        user_name);
}

static void
on_user_is_logged_in_changed (GdmUserManager       *manager,
                              GdmUser              *user,
                              GdmUserChooserWidget *widget)
{
        const char *user_name;
        gboolean    is_logged_in;

        g_debug ("GdmUserChooserWidget: User logged in changed: %s", gdm_user_get_user_name (user));

        user_name = gdm_user_get_user_name (user);
        is_logged_in = gdm_user_get_num_sessions (user) > 0;

        gdm_chooser_widget_set_item_in_use (GDM_CHOOSER_WIDGET (widget),
                                            user_name, is_logged_in);
}

static void
gdm_user_chooser_widget_init (GdmUserChooserWidget *widget)
{
        widget->priv = GDM_USER_CHOOSER_WIDGET_GET_PRIVATE (widget);

        gdm_chooser_widget_set_separator_position (GDM_CHOOSER_WIDGET (widget),
                                                   GDM_CHOOSER_WIDGET_POSITION_BOTTOM);
        gdm_chooser_widget_set_in_use_message (GDM_CHOOSER_WIDGET (widget),
                                               _("Currently logged in"));

        widget->priv->manager = gdm_user_manager_ref_default ();
        g_signal_connect (widget->priv->manager,
                          "user-added",
                          G_CALLBACK (on_user_added),
                          widget);
        g_signal_connect (widget->priv->manager,
                          "user-removed",
                          G_CALLBACK (on_user_removed),
                          widget);
        g_signal_connect (widget->priv->manager,
                          "user-is-logged-in-changed",
                          G_CALLBACK (on_user_is_logged_in_changed),
                          widget);

        add_special_users (widget);
}

static void
gdm_user_chooser_widget_finalize (GObject *object)
{
        GdmUserChooserWidget *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (object));

        widget = GDM_USER_CHOOSER_WIDGET (object);

        g_return_if_fail (widget->priv != NULL);

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_user_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_USER_CHOOSER_WIDGET,
                               "inactive-text", _("_Users:"),
                               "active-text", _("_User:"),
                               NULL);

        return GTK_WIDGET (object);
}
