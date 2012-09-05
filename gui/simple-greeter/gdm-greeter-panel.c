/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011 Red Hat, Inc.
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
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#ifdef ENABLE_RBAC_SHUTDOWN
#include <auth_attr.h>
#include <secdb.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef HAVE_UPOWER
#include <upower.h>
#endif

#include "gdm-greeter-panel.h"
#include "gdm-clock-widget.h"
#include "gdm-timer.h"
#include "gdm-profile.h"
#include "gdm-common.h"

#define CK_NAME              "org.freedesktop.ConsoleKit"
#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"

#define LOGIN1_NAME      "org.freedesktop.login1"
#define LOGIN1_PATH      "/org/freedesktop/login1"
#define LOGIN1_INTERFACE "org.freedesktop.login1.Manager"

#define GPM_DBUS_NAME      "org.gnome.SettingsDaemon"
#define GPM_DBUS_PATH      "/org/gnome/SettingsDaemon/Power"
#define GPM_DBUS_INTERFACE "org.gnome.SettingsDaemon.Power"

#define LOGIN_SCREEN_SCHEMA           "org.gnome.login-screen"

#define KEY_DISABLE_RESTART_BUTTONS   "disable-restart-buttons"

#define KEY_NOTIFICATION_AREA_PADDING "/apps/notification_area_applet/prefs/padding"

#define GDM_GREETER_PANEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_PANEL, GdmGreeterPanelPrivate))

struct GdmGreeterPanelPrivate
{
        int                     monitor;
        GdkRectangle            geometry;
        GtkWidget              *hbox;
        GtkWidget              *left_hbox;
        GtkWidget              *right_hbox;
        GtkWidget              *alignment;
        GtkWidget              *hostname_label;
        GtkWidget              *clock;
        GtkWidget              *status_menubar;
        GtkWidget              *shutdown_menu;

        GdmTimer               *animation_timer;
        double                  progress;

        GtkWidget              *power_image;
        GtkWidget              *power_menu_item;
        GtkWidget              *power_menubar_item;
        GDBusProxy             *power_proxy;
        gulong                  power_proxy_signal_handler;
        gulong                  power_proxy_properties_changed_handler;

        guint                   display_is_local : 1;
};

enum {
        PROP_0,
        PROP_MONITOR,
        PROP_DISPLAY_IS_LOCAL
};

enum {
        DISCONNECTED,
        NUMBER_OF_SIGNALS
};

static guint signals [NUMBER_OF_SIGNALS] = { 0, };

static void     gdm_greeter_panel_class_init  (GdmGreeterPanelClass *klass);
static void     gdm_greeter_panel_init        (GdmGreeterPanel      *greeter_panel);
static void     gdm_greeter_panel_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmGreeterPanel, gdm_greeter_panel, GTK_TYPE_WINDOW)

static void
gdm_greeter_panel_set_monitor (GdmGreeterPanel *panel,
                               int              monitor)
{
        g_return_if_fail (GDM_IS_GREETER_PANEL (panel));

        if (panel->priv->monitor == monitor) {
                return;
        }

        panel->priv->monitor = monitor;

        gtk_widget_queue_resize (GTK_WIDGET (panel));

        g_object_notify (G_OBJECT (panel), "monitor");
}

static void
_gdm_greeter_panel_set_display_is_local (GdmGreeterPanel *panel,
                                         gboolean         is)
{
        if (panel->priv->display_is_local != is) {
                panel->priv->display_is_local = is;
                g_object_notify (G_OBJECT (panel), "display-is-local");
        }
}

static void
gdm_greeter_panel_set_property (GObject        *object,
                                guint           prop_id,
                                const GValue   *value,
                                GParamSpec     *pspec)
{
        GdmGreeterPanel *self;

        self = GDM_GREETER_PANEL (object);

        switch (prop_id) {
        case PROP_MONITOR:
                gdm_greeter_panel_set_monitor (self, g_value_get_int (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_greeter_panel_set_display_is_local (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_panel_get_property (GObject        *object,
                                guint           prop_id,
                                GValue         *value,
                                GParamSpec     *pspec)
{
        GdmGreeterPanel *self;

        self = GDM_GREETER_PANEL (object);

        switch (prop_id) {
        case PROP_MONITOR:
                g_value_set_int (value, self->priv->monitor);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_panel_dispose (GObject *object)
{
        GdmGreeterPanel *panel;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_PANEL (object));

        panel = GDM_GREETER_PANEL (object);

        if (panel->priv->power_proxy != NULL) {
                g_object_unref (panel->priv->power_proxy);
                panel->priv->power_proxy = NULL;
        }

        G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->dispose (object);
}

/* copied from panel-toplevel.c */
static void
gdm_greeter_panel_move_resize_window (GdmGreeterPanel *panel,
                                      gboolean         move,
                                      gboolean         resize)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (panel);

        g_assert (gtk_widget_get_realized (widget));

        if (move && resize) {
                gdk_window_move_resize (gtk_widget_get_window (widget),
                                        panel->priv->geometry.x,
                                        panel->priv->geometry.y,
                                        panel->priv->geometry.width,
                                        panel->priv->geometry.height);
        } else if (move) {
                gdk_window_move (gtk_widget_get_window (widget),
                                 panel->priv->geometry.x,
                                 panel->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (gtk_widget_get_window (widget),
                                   panel->priv->geometry.width,
                                   panel->priv->geometry.height);
        }
}

static void
on_screen_size_changed (GdkScreen       *screen,
                        GdmGreeterPanel *panel)
{
        gtk_widget_queue_resize (GTK_WIDGET (panel));
}

static void
update_power_icon (GdmGreeterPanel *panel)
{
        GVariant *variant;

        g_assert (panel->priv->power_proxy != NULL);

        variant = g_dbus_proxy_get_cached_property (panel->priv->power_proxy, "Icon");
        if (variant == NULL) {
                /* FIXME: use an indeterminant icon */
                return;
        }

        if (g_variant_is_of_type (variant, G_VARIANT_TYPE ("s"))) {
                const char *name;

                name = g_variant_get_string (variant, NULL);

                if (name != NULL && *name != '\0') {
                        GError *error;
                        GIcon  *icon;
                        error = NULL;
                        icon = g_icon_new_for_string (name, &error);
                        if (icon != NULL) {
                                g_debug ("setting power icon %s", name);
                                gtk_image_set_from_gicon (GTK_IMAGE (panel->priv->power_image),
                                                          icon,
                                                          GTK_ICON_SIZE_MENU);
                                gtk_widget_show_all (panel->priv->power_menubar_item);
                        } else {
                                gtk_widget_hide (panel->priv->power_menubar_item);
                        }
                } else {
                        gtk_widget_hide (panel->priv->power_menubar_item);
                }
        }

        g_variant_unref (variant);
}

static void
update_power_menu (GdmGreeterPanel *panel)
{
        GVariant *variant;

        g_assert (panel->priv->power_proxy != NULL);

        variant = g_dbus_proxy_get_cached_property (panel->priv->power_proxy, "Tooltip");
        if (variant == NULL) {
                /* FIXME: use an indeterminant message */
                return;
        }

        if (g_variant_is_of_type (variant, G_VARIANT_TYPE ("s"))) {
                const char *txt;

                txt = g_variant_get_string (variant, NULL);
                if (txt != NULL) {
                        gtk_menu_item_set_label (GTK_MENU_ITEM (panel->priv->power_menu_item), txt);
                }
        }

        g_variant_unref (variant);
}

static void
on_power_proxy_g_signal (GDBusProxy      *proxy,
                         const char      *sender_name,
                         const char      *signal_name,
                         GVariant        *parameters,
                         GdmGreeterPanel *panel)
{
        if (g_strcmp0 (signal_name, "Changed") == 0) {
                //update_power_icon (panel);
        }
}

static void
on_power_proxy_g_properties_changed (GDBusProxy      *proxy,
                                     GVariant        *changed_properties,
                                     GStrv           *invalidated_properties,
                                     GdmGreeterPanel *panel)
{
        g_debug ("Got power properties changed");
        if (g_variant_n_children (changed_properties) > 0) {
                GVariantIter iter;
                GVariant    *value;
                char        *key;

                g_variant_iter_init (&iter, changed_properties);

                while (g_variant_iter_loop (&iter, "{&sv}", &key, &value)) {
                        if (g_strcmp0 (key, "Icon") == 0) {
                                g_debug ("Got power Icon changed");
                                update_power_icon (panel);
                        } else if (g_strcmp0 (key, "Tooltip") == 0) {
                                g_debug ("Got power tooltip changed");
                                update_power_menu (panel);
                        }
                }
        }
}

static void
gdm_greeter_panel_real_realize (GtkWidget *widget)
{
        GdmGreeterPanel *panel = GDM_GREETER_PANEL (widget);

        if (GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->realize) {
                GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->realize (widget);
        }

        gdk_window_set_geometry_hints (gtk_widget_get_window (widget), NULL, GDK_HINT_POS);

        gdm_greeter_panel_move_resize_window (GDM_GREETER_PANEL (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);

        if (panel->priv->power_proxy != NULL) {
                update_power_icon (panel);
                update_power_menu (panel);
                panel->priv->power_proxy_signal_handler = g_signal_connect (panel->priv->power_proxy,
                                                                            "g-signal",
                                                                            G_CALLBACK (on_power_proxy_g_signal),
                                                                            panel);
        panel->priv->power_proxy_properties_changed_handler = g_signal_connect (panel->priv->power_proxy,
                                                                                "g-properties-changed",
                                                                                G_CALLBACK (on_power_proxy_g_properties_changed),
                                                                                panel);
        }

}

static void
gdm_greeter_panel_real_unrealize (GtkWidget *widget)
{
        GdmGreeterPanel *panel = GDM_GREETER_PANEL (widget);

        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (panel->priv->power_proxy != NULL
            && panel->priv->power_proxy_signal_handler != 0) {
                g_signal_handler_disconnect (panel->priv->power_proxy, panel->priv->power_proxy_signal_handler);
                g_signal_handler_disconnect (panel->priv->power_proxy, panel->priv->power_proxy_properties_changed_handler);
        }

        if (GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->unrealize (widget);
        }
}

static void
set_struts (GdmGreeterPanel *panel,
            int              x,
            int              y,
            int              width,
            int              height)
{
        gulong        data[12] = { 0, };

        /* _NET_WM_STRUT_PARTIAL: CARDINAL[12]/32
         *
         * 0: left          1: right       2:  top             3:  bottom
         * 4: left_start_y  5: left_end_y  6:  right_start_y   7:  right_end_y
         * 8: top_start_x   9: top_end_x   10: bottom_start_x  11: bottom_end_x
         *
         * Note: In xinerama use struts relative to combined screen dimensions,
         *       not just the current monitor.
         */

        /* top */
        data[2] = panel->priv->geometry.y + height;
        /* top_start_x */
        data[8] = x;
        /* top_end_x */
        data[9] = x + width;

#if 0
        g_debug ("Setting strut: top=%lu top_start_x=%lu top_end_x=%lu", data[2], data[8], data[9]);
#endif

        gdk_error_trap_push ();
        if (gtk_widget_get_window (GTK_WIDGET (panel)) != NULL) {
                gdk_property_change (gtk_widget_get_window (GTK_WIDGET (panel)),
                                     gdk_atom_intern ("_NET_WM_STRUT_PARTIAL", FALSE),
                                     gdk_atom_intern ("CARDINAL", FALSE),
                                     32,
                                     GDK_PROP_MODE_REPLACE,
                                     (guchar *) &data,
                                     12);

                gdk_property_change (gtk_widget_get_window (GTK_WIDGET (panel)),
                                     gdk_atom_intern ("_NET_WM_STRUT", FALSE),
                                     gdk_atom_intern ("CARDINAL", FALSE),
                                     32,
                                     GDK_PROP_MODE_REPLACE,
                                     (guchar *) &data,
                                     4);
        }

        gdk_error_trap_pop_ignored ();
}

static void
update_struts (GdmGreeterPanel *panel)
{
        /* FIXME: assumes only one panel */
        set_struts (panel,
                    panel->priv->geometry.x,
                    panel->priv->geometry.y,
                    panel->priv->geometry.width,
                    panel->priv->geometry.height);
}

static void
update_geometry (GdmGreeterPanel *panel,
                 GtkRequisition  *requisition)
{
        GdkRectangle geometry;

        gdk_screen_get_monitor_geometry (gtk_window_get_screen (GTK_WINDOW (panel)),
                                         panel->priv->monitor,
                                         &geometry);

        panel->priv->geometry.width = geometry.width;
        panel->priv->geometry.height = requisition->height + 2 * gtk_container_get_border_width (GTK_CONTAINER (panel));

        panel->priv->geometry.x = geometry.x;
        panel->priv->geometry.y = geometry.y - panel->priv->geometry.height + panel->priv->progress * panel->priv->geometry.height;

#if 0
        panel->priv->geometry.y += 50;
#endif
#if 0
        g_debug ("Setting geometry x:%d y:%d w:%d h:%d",
                 panel->priv->geometry.x,
                 panel->priv->geometry.y,
                 panel->priv->geometry.width,
                 panel->priv->geometry.height);
#endif

        update_struts (panel);
}

static void
gdm_greeter_panel_get_preferred_size (GtkWidget      *widget,
                                      GtkOrientation  orientation,
                                      gint           *minimum_size,
                                      gint           *natural_size)
{
        GdmGreeterPanel *panel;
        GtkBin          *bin;
        GtkWidget       *child;
        GdkRectangle     old_geometry;
        int              position_changed = FALSE;
        int              size_changed = FALSE;
        GtkRequisition   minimum_req, natural_req;

        panel = GDM_GREETER_PANEL (widget);
        bin = GTK_BIN (widget);
        child = gtk_bin_get_child (bin);

        minimum_req.width = 0;
        minimum_req.height = 0;
        natural_req.width = minimum_req.width;
        natural_req.height = minimum_req.height;

        if (child != NULL && gtk_widget_get_visible (child)) {
                int min_child_width, nat_child_width;
                int min_child_height, nat_child_height;

                gtk_widget_get_preferred_width (gtk_bin_get_child (bin),
                                                &min_child_width,
                                                &nat_child_width);
                gtk_widget_get_preferred_height (gtk_bin_get_child (bin),
                                                 &min_child_height,
                                                 &nat_child_height);

                minimum_req.width += min_child_width;
                natural_req.width += nat_child_width;
                minimum_req.height += min_child_height;
                natural_req.height += nat_child_height;
        }

        old_geometry = panel->priv->geometry;
        update_geometry (panel, &natural_req);

        if (!gtk_widget_get_realized (widget))
                goto out;

        if (old_geometry.width  != panel->priv->geometry.width ||
            old_geometry.height != panel->priv->geometry.height) {
                size_changed = TRUE;
        }

        if (old_geometry.x != panel->priv->geometry.x ||
            old_geometry.y != panel->priv->geometry.y) {
                position_changed = TRUE;
        }

        gdm_greeter_panel_move_resize_window (panel, position_changed, size_changed);

 out:

        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
                if (minimum_size)
                        *minimum_size = panel->priv->geometry.width;
                if (natural_size)
                        *natural_size = panel->priv->geometry.width;
        } else {
                if (minimum_size)
                        *minimum_size = panel->priv->geometry.height;
                if (natural_size)
                        *natural_size = panel->priv->geometry.height;
        }
}

static void
gdm_greeter_panel_real_get_preferred_width (GtkWidget *widget,
                                            gint      *minimum_size,
                                            gint      *natural_size)
{
        gdm_greeter_panel_get_preferred_size (widget, GTK_ORIENTATION_HORIZONTAL, minimum_size, natural_size);
}

static void
gdm_greeter_panel_real_get_preferred_height (GtkWidget *widget,
                                             gint      *minimum_size,
                                             gint      *natural_size)
{
        gdm_greeter_panel_get_preferred_size (widget, GTK_ORIENTATION_VERTICAL, minimum_size, natural_size);
}

static void
gdm_greeter_panel_real_show (GtkWidget *widget)
{
        GdmGreeterPanel *panel;
        GtkSettings *settings;
        gboolean     animations_are_enabled;

        settings = gtk_settings_get_for_screen (gtk_widget_get_screen (widget));
        g_object_get (settings, "gtk-enable-animations", &animations_are_enabled, NULL);

        panel = GDM_GREETER_PANEL (widget);

        if (animations_are_enabled) {
                gdm_timer_start (panel->priv->animation_timer, 1.0);
        } else {
                panel->priv->progress = 1.0;
        }

        GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->show (widget);
}

static void
gdm_greeter_panel_real_hide (GtkWidget *widget)
{
        GdmGreeterPanel *panel;

        panel = GDM_GREETER_PANEL (widget);

        gdm_timer_stop (panel->priv->animation_timer);
        panel->priv->progress = 0.0;

        GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->hide (widget);
}

static void
on_animation_tick (GdmGreeterPanel *panel,
                   double           progress)
{
        panel->priv->progress = progress * log ((G_E - 1.0) * progress + 1.0);

        gtk_widget_queue_resize (GTK_WIDGET (panel));
}

static gboolean
try_system_stop (GDBusConnection *connection,
                 GError         **error)
{
        GVariant  *reply;
        gboolean   res;
        GError    *call_error;

        g_debug ("GdmGreeterPanel: trying to stop system");

        call_error = NULL;
        reply = g_dbus_connection_call_sync (connection,
                                             LOGIN1_NAME,
                                             LOGIN1_PATH,
                                             LOGIN1_INTERFACE,
                                             "PowerOff",
                                             g_variant_new ("(b)", TRUE),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             INT_MAX,
                                             NULL,
                                             &call_error);

        if (reply == NULL && g_error_matches (call_error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
                g_clear_error (&call_error);
                reply = g_dbus_connection_call_sync (connection,
                                                     CK_NAME,
                                                     CK_MANAGER_PATH,
                                                     CK_MANAGER_INTERFACE,
                                                     "Stop",
                                                     NULL,
                                                     NULL,
                                                     G_DBUS_CALL_FLAGS_NONE,
                                                     INT_MAX,
                                                     NULL,
                                                     &call_error);
        }

        if (reply != NULL) {
                res = TRUE;
                g_variant_unref (reply);
        } else {
                g_propagate_error (error, call_error);
                res = FALSE;
        }

        return res;
}

static gboolean
try_system_restart (GDBusConnection *connection,
                    GError         **error)
{
        GVariant  *reply;
        gboolean   res;
        GError    *call_error;

        g_debug ("GdmGreeterPanel: trying to restart system");

        call_error = NULL;
        reply = g_dbus_connection_call_sync (connection,
                                             LOGIN1_NAME,
                                             LOGIN1_PATH,
                                             LOGIN1_INTERFACE,
                                             "Reboot",
                                             g_variant_new ("(b)", TRUE),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             INT_MAX,
                                             NULL,
                                             &call_error);

        if (reply == NULL && g_error_matches (call_error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
                g_clear_error (&call_error);
                reply = g_dbus_connection_call_sync (connection,
                                                     CK_NAME,
                                                     CK_MANAGER_PATH,
                                                     CK_MANAGER_INTERFACE,
                                                     "Restart",
                                                     NULL,
                                                     NULL,
                                                     G_DBUS_CALL_FLAGS_NONE,
                                                     INT_MAX,
                                                     NULL,
                                                     &call_error);
        }

        if (reply != NULL) {
                res = TRUE;
                g_variant_unref (reply);
        } else {
                g_propagate_error (error, call_error);
                res = FALSE;
        }

        return res;
}

static gboolean
can_suspend (void)
{
        gboolean ret = FALSE;

#ifdef HAVE_UPOWER
        UpClient *up_client;

        /* use UPower to get data */
        up_client = up_client_new ();
	ret = up_client_get_can_suspend (up_client);
        g_object_unref (up_client);
#endif

        return ret;
}

static void
do_system_suspend (void)
{
#ifdef HAVE_UPOWER
        gboolean ret;
        UpClient *up_client;
        GError *error = NULL;

        /* use UPower to trigger suspend */
        up_client = up_client_new ();
        ret = up_client_suspend_sync (up_client, NULL, &error);
        if (!ret) {
                g_warning ("Couldn't suspend: %s", error->message);
                g_error_free (error);
                return;
        }
        g_object_unref (up_client);
#endif
}

static void
do_system_restart (void)
{
        gboolean         res;
        GError          *error;
        GDBusConnection *connection;

        error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", error->message);
                g_error_free (error);
                return;
        }

        res = try_system_restart (connection, &error);
        if (!res) {
                g_debug ("GdmGreeterPanel: unable to restart system: %s",
                         error->message);
                g_error_free (error);
        }
}

static void
do_system_stop (void)
{
        gboolean         res;
        GError          *error;
        GDBusConnection *connection;

        error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (connection == NULL) {
                g_warning ("Unable to get system bus connection: %s", error->message);
                g_error_free (error);
                return;
        }

        res = try_system_stop (connection, &error);
        if (!res) {
                g_debug ("GdmGreeterPanel: unable to stop system: %s",
                         error->message);
                g_error_free (error);
        }
}

static void
do_disconnect (GtkWidget       *widget,
               GdmGreeterPanel *panel)
{
        g_signal_emit (panel, signals[DISCONNECTED], 0);
}

static gboolean
get_show_restart_buttons (GdmGreeterPanel *panel)
{
        gboolean   show;
        GSettings *settings;

        settings = g_settings_new (LOGIN_SCREEN_SCHEMA);

        show = ! g_settings_get_boolean (settings, KEY_DISABLE_RESTART_BUTTONS);

#ifdef ENABLE_RBAC_SHUTDOWN
        {
                char *username;

                username = g_get_user_name ();
                if (username == NULL || !chkauthattr (RBAC_SHUTDOWN_KEY, username)) {
                        show = FALSE;
                        g_debug ("GdmGreeterPanel: Not showing stop/restart buttons for user %s due to RBAC key %s",
                                 username, RBAC_SHUTDOWN_KEY);
                } else {
                        g_debug ("GdmGreeterPanel: Showing stop/restart buttons for user %s due to RBAC key %s",
                                 username, RBAC_SHUTDOWN_KEY);
                }
        }
#endif
        g_object_unref (settings);

        return show;
}

static inline void
override_style (GtkWidget *widget)
{
        GtkCssProvider  *provider;
        GtkStyleContext *context;
        GError          *error;

        g_debug ("updating style");

        context = gtk_widget_get_style_context (widget);

        provider = gtk_css_provider_new ();
        gtk_style_context_add_provider (context,
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        error = NULL;
        gtk_css_provider_load_from_data (provider,
                                         "* {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "  border-width: 0;\n"
                                         "}\n"
                                         "*:selected {\n"
                                         "  background-color: #666666;\n"
                                         "  color: white;\n"
                                         "}\n"
                                         ".menu,\n"
                                         ".menubar,\n"
                                         ".menu.check,\n"
                                         ".menu.radio {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "  border-style: none;\n"
                                         "}\n"
                                         ".menu:hover,\n"
                                         ".menubar:hover,\n"
                                         ".menu.check:hover,\n"
                                         ".menu.radio:hover {\n"
                                         "  background-color: #666666;\n"
                                         "  color: #ccc;\n"
                                         "  border-style: none;\n"
                                         "}\n"
                                         "GtkLabel:selected {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "}\n"
                                         "\n"
                                         "GtkLabel:selected:focused {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "}\n"
                                         "GtkMenuBar {\n"
                                         "  background-color: black;\n"
                                         "  background-image: none;\n"
                                         "  color: #ccc;\n"
                                         "  -GtkMenuBar-internal-padding: 0;\n"
                                         "  -GtkMenuBar-shadow-type: none;\n"
                                         "  border-width: 0;\n"
                                         "  border-style: none;\n"
                                         "}\n"
                                         "GtkMenuItem {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "}\n"
                                         "GtkImage {\n"
                                         "  background-color: black;\n"
                                         "  color: #ccc;\n"
                                         "}\n",
                                         -1,
                                         &error);
        if (error != NULL) {
                g_warning ("Error loading style data: %s", error->message);
                g_error_free (error);
        }
}

static void
add_shutdown_menu (GdmGreeterPanel *panel)
{
        GtkWidget *item;
        GtkWidget *menu_item;
        GtkWidget *box;
        GtkWidget *image;
        GIcon     *gicon;

        item = gtk_menu_item_new ();
        override_style (item);
        box = gtk_hbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (item), box);
        gtk_menu_shell_append (GTK_MENU_SHELL (panel->priv->status_menubar), item);
        image = gtk_image_new ();
        override_style (image);

        gicon = g_themed_icon_new ("system-shutdown-symbolic");
        gtk_image_set_from_gicon (GTK_IMAGE (image), gicon, GTK_ICON_SIZE_MENU);
        g_object_unref (gicon);

        gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

        panel->priv->shutdown_menu = gtk_menu_new ();
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), panel->priv->shutdown_menu);

        if (! panel->priv->display_is_local) {
                menu_item = gtk_menu_item_new_with_label ("Disconnect");
                g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (do_disconnect), panel);
                gtk_menu_shell_append (GTK_MENU_SHELL (panel->priv->shutdown_menu), menu_item);
        } else if (get_show_restart_buttons (panel)) {
                if (can_suspend ()) {
                        menu_item = gtk_menu_item_new_with_label (_("Suspend"));
                        g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (do_system_suspend), NULL);
                        gtk_menu_shell_append (GTK_MENU_SHELL (panel->priv->shutdown_menu), menu_item);
                }

                menu_item = gtk_menu_item_new_with_label (_("Restart"));
                g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (do_system_restart), NULL);
                gtk_menu_shell_append (GTK_MENU_SHELL (panel->priv->shutdown_menu), menu_item);

                menu_item = gtk_menu_item_new_with_label (_("Shut Down"));
                g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (do_system_stop), NULL);
                gtk_menu_shell_append (GTK_MENU_SHELL (panel->priv->shutdown_menu), menu_item);
        }
        gtk_widget_show_all (item);
}

static void
add_battery_menu (GdmGreeterPanel *panel)
{
        GtkWidget *item;
        GtkWidget *box;
        GtkWidget *menu;
        GError    *error;
        GIcon     *gicon;

        error = NULL;
        panel->priv->power_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                                  NULL,
                                                                  GPM_DBUS_NAME,
                                                                  GPM_DBUS_PATH,
                                                                  GPM_DBUS_INTERFACE,
                                                                  NULL,
                                                                  &error);
        if (panel->priv->power_proxy == NULL) {
                g_warning ("Unable to connect to power manager: %s", error->message);
                g_error_free (error);
                return;
        }

        item = gtk_menu_item_new ();

        override_style (item);
        box = gtk_hbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (item), box);
        gtk_menu_shell_prepend (GTK_MENU_SHELL (panel->priv->status_menubar), item);
        panel->priv->power_image = gtk_image_new ();
        override_style (panel->priv->power_image);

        gicon = g_themed_icon_new ("battery-caution-symbolic");
        gtk_image_set_from_gicon (GTK_IMAGE (panel->priv->power_image), gicon, GTK_ICON_SIZE_MENU);
        g_object_unref (gicon);

        gtk_box_pack_start (GTK_BOX (box), panel->priv->power_image, FALSE, FALSE, 0);

        menu = gtk_menu_new ();
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);

        panel->priv->power_menu_item = gtk_menu_item_new_with_label (_("Unknown time remaining"));
        gtk_widget_set_sensitive (panel->priv->power_menu_item, FALSE);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), panel->priv->power_menu_item);
        panel->priv->power_menubar_item = item;
}

static void
setup_panel (GdmGreeterPanel *panel)
{
        GtkSizeGroup *sg;

        gdm_profile_start (NULL);

        gtk_widget_set_can_focus (GTK_WIDGET (panel), TRUE);

        override_style (GTK_WIDGET (panel));

        panel->priv->geometry.x      = -1;
        panel->priv->geometry.y      = -1;
        panel->priv->geometry.width  = -1;
        panel->priv->geometry.height = -1;

        gtk_window_set_title (GTK_WINDOW (panel), _("Panel"));
        gtk_window_set_decorated (GTK_WINDOW (panel), FALSE);
        gtk_window_set_has_resize_grip (GTK_WINDOW (panel), FALSE);

        gtk_window_set_keep_above (GTK_WINDOW (panel), TRUE);
        gtk_window_set_type_hint (GTK_WINDOW (panel), GDK_WINDOW_TYPE_HINT_DOCK);

        panel->priv->hbox = gtk_hbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (panel->priv->hbox), 0);
        gtk_widget_show (panel->priv->hbox);
        gtk_container_add (GTK_CONTAINER (panel), panel->priv->hbox);

        panel->priv->left_hbox = gtk_hbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (panel->priv->left_hbox), 0);
        gtk_widget_show (panel->priv->left_hbox);
        gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->left_hbox, TRUE, TRUE, 0);

        panel->priv->alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->alignment, FALSE, FALSE, 0);
        gtk_widget_show (panel->priv->alignment);

        panel->priv->right_hbox = gtk_hbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (panel->priv->right_hbox), 0);
        gtk_widget_show (panel->priv->right_hbox);
        gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->right_hbox, TRUE, TRUE, 0);

        panel->priv->clock = gdm_clock_widget_new ();
        gtk_widget_show (panel->priv->clock);
        gtk_container_add (GTK_CONTAINER (panel->priv->alignment), panel->priv->clock);

        sg = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
        gtk_size_group_add_widget (sg, panel->priv->left_hbox);
        gtk_size_group_add_widget (sg, panel->priv->right_hbox);

        panel->priv->status_menubar = gtk_menu_bar_new ();
        override_style (panel->priv->status_menubar);
        gtk_widget_show (panel->priv->status_menubar);
        gtk_box_pack_end (GTK_BOX (panel->priv->right_hbox), GTK_WIDGET (panel->priv->status_menubar), FALSE, FALSE, 0);

        if (!panel->priv->display_is_local || get_show_restart_buttons (panel)) {
                add_shutdown_menu (panel);
        }

        add_battery_menu (panel);

        /* FIXME: we should only show hostname on panel when connected
           to a remote host */
        if (0) {
                panel->priv->hostname_label = gtk_label_new (g_get_host_name ());
                gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->hostname_label, FALSE, FALSE, 6);
                gtk_widget_show (panel->priv->hostname_label);
        }

        panel->priv->progress = 0.0;
        panel->priv->animation_timer = gdm_timer_new ();
        g_signal_connect_swapped (panel->priv->animation_timer,
                                  "tick",
                                  G_CALLBACK (on_animation_tick),
                                  panel);

        gdm_profile_end (NULL);
}

static GObject *
gdm_greeter_panel_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmGreeterPanel      *greeter_panel;

        gdm_profile_start (NULL);

        greeter_panel = GDM_GREETER_PANEL (G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        setup_panel (greeter_panel);

        gdm_profile_end (NULL);

        return G_OBJECT (greeter_panel);
}

static void
gdm_greeter_panel_init (GdmGreeterPanel *panel)
{
        panel->priv = GDM_GREETER_PANEL_GET_PRIVATE (panel);

}

static void
gdm_greeter_panel_finalize (GObject *object)
{
        GdmGreeterPanel *greeter_panel;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_PANEL (object));

        greeter_panel = GDM_GREETER_PANEL (object);

        g_return_if_fail (greeter_panel->priv != NULL);

        g_signal_handlers_disconnect_by_func (object, on_animation_tick, greeter_panel);
        g_object_unref (greeter_panel->priv->animation_timer);

        G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->finalize (object);
}

static void
gdm_greeter_panel_class_init (GdmGreeterPanelClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_panel_get_property;
        object_class->set_property = gdm_greeter_panel_set_property;
        object_class->constructor = gdm_greeter_panel_constructor;
        object_class->dispose = gdm_greeter_panel_dispose;
        object_class->finalize = gdm_greeter_panel_finalize;

        widget_class->realize = gdm_greeter_panel_real_realize;
        widget_class->unrealize = gdm_greeter_panel_real_unrealize;
        widget_class->get_preferred_width = gdm_greeter_panel_real_get_preferred_width;
        widget_class->get_preferred_height = gdm_greeter_panel_real_get_preferred_height;
        widget_class->show = gdm_greeter_panel_real_show;
        widget_class->hide = gdm_greeter_panel_real_hide;

        g_object_class_install_property (object_class,
                                         PROP_MONITOR,
                                         g_param_spec_int ("monitor",
                                                           "Xinerama monitor",
                                                           "The monitor (in terms of Xinerama) which the window is on",
                                                           0,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPanelClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (GdmGreeterPanelPrivate));
}

GtkWidget *
gdm_greeter_panel_new (GdkScreen *screen,
                       int        monitor,
                       gboolean   is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_PANEL,
                               "screen", screen,
                               "monitor", monitor,
                               "display-is-local", is_local,
                               NULL);

        return GTK_WIDGET (object);
}
