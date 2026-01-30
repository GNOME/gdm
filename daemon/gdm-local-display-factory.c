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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#ifdef HAVE_UDEV
#include <gudev/gudev.h>
#endif

#include <systemd/sd-login.h>

#include "gdm-common.h"
#include "gdm-manager.h"
#include "gdm-display-factory.h"
#include "gdm-local-display-factory.h"
#include "gdm-local-display-factory-glue.h"

#include "gdm-settings-keys.h"
#include "gdm-settings-direct.h"
#include "gdm-display-store.h"
#include "gdm-local-display.h"

#define GDM_DBUS_PATH                       "/org/gnome/DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH GDM_DBUS_PATH "/LocalDisplayFactory"
#define GDM_MANAGER_DBUS_NAME               "org.gnome.DisplayManager.LocalDisplayFactory"

#define MAX_DISPLAY_FAILURES 5
#define WAIT_TO_FINISH_TIMEOUT 10 /* seconds */
#define SEAT0_GRAPHICS_CHECK_TIMEOUT 10 /* seconds */

struct _GdmLocalDisplayFactory
{
        GdmDisplayFactory  parent;
#ifdef HAVE_UDEV
        GUdevClient       *gudev_client;
#endif

        GdmDBusLocalDisplayFactory *skeleton;
        GDBusConnection *connection;

        /* FIXME: this needs to be per seat? */
        guint            num_failures;

        guint            seat_new_id;
        guint            seat_removed_id;
        guint            seat_properties_changed_id;
        guint            seat_attention_key;


        gboolean         seat0_has_platform_graphics;
        gboolean         seat0_has_boot_up_graphics;

        gboolean         seat0_graphics_check_timed_out;
        guint            seat0_graphics_check_timeout_id;

        gulong           uevent_handler_id;

        unsigned int     active_vt;
        guint            active_vt_watch_id;
        guint            wait_to_finish_timeout_id;

        gboolean         is_started;
};

enum {
        PROP_0,
};

enum {
        GRAPHICS_UNSUPPORTED,
        LAST_SIGNAL,
};

static guint signals [LAST_SIGNAL] = { 0 };

static void     gdm_local_display_factory_class_init    (GdmLocalDisplayFactoryClass *klass);
static void     gdm_local_display_factory_init          (GdmLocalDisplayFactory      *factory);
static void     gdm_local_display_factory_finalize      (GObject                     *object);

static void     ensure_display_for_seat                 (GdmLocalDisplayFactory      *factory,
                                                         const char                  *seat_id);

static void     on_display_status_changed               (GdmDisplay                  *display,
                                                         GParamSpec                  *arg1,
                                                         GdmLocalDisplayFactory      *factory);

static gboolean gdm_local_display_factory_sync_seats    (GdmLocalDisplayFactory *factory);
static gpointer local_display_factory_object = NULL;
static gboolean lookup_by_session_id (const char *id,
                                      GdmDisplay *display,
                                      gpointer    user_data);

G_DEFINE_TYPE (GdmLocalDisplayFactory, gdm_local_display_factory, GDM_TYPE_DISPLAY_FACTORY)

GQuark
gdm_local_display_factory_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_local_display_factory_error");
        }

        return ret;
}

static void
on_display_disposed (GdmLocalDisplayFactory *factory,
                     GdmDisplay             *display)
{
        g_debug ("GdmLocalDisplayFactory: Display %p disposed", display);
}

static void
store_display (GdmLocalDisplayFactory *factory,
               GdmDisplay             *display)
{
        GdmDisplayStore *store;

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_add (store, display);
}

static char **
gdm_local_display_factory_get_session_types (GdmLocalDisplayFactory *factory)
{
        g_autoptr (GStrvBuilder) builder = NULL;

        builder = g_strv_builder_new ();

        g_strv_builder_add (builder, "wayland");

#ifdef ENABLE_X11_SUPPORT
        gboolean x11_enabled = FALSE;
        gdm_settings_direct_get_boolean (GDM_KEY_XORG_ENABLE, &x11_enabled);
        if (x11_enabled && gdm_find_x_server () != NULL)
                g_strv_builder_add (builder, "x11");
#endif

        return g_strv_builder_end (builder);
}

/*
  Example:
  dbus-send --system --dest=org.gnome.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/DisplayManager/Manager \
  org.gnome.DisplayManager.Manager.GetDisplays
*/
static gboolean
gdm_local_display_factory_create_display (GdmLocalDisplayFactory  *factory,
                                          const char              *autologin_user,
                                          char                   **id,
                                          GError                 **error)
{
        g_auto(GStrv) session_types = NULL;
        g_autoptr (GdmDisplay) display = NULL;

        g_debug ("GdmLocalDisplayFactory: Creating local display");

        session_types = gdm_local_display_factory_get_session_types (factory);

        display = gdm_local_display_new ();
        g_object_set (G_OBJECT (display),
                      "supported-session-types", session_types,
                      "seat-id", "seat0",
                      "allow-timed-login", FALSE,
                      "is-initial", TRUE,
                      "autologin-user", autologin_user,
                      NULL);

        store_display (factory, display);

        if (!gdm_display_prepare (display)) {
                g_set_error_literal (error,
                                     GDM_DISPLAY_ERROR,
                                     GDM_DISPLAY_ERROR_GENERAL,
                                     "Failed preparing display");
                gdm_display_unmanage (display);
                return FALSE;
        }

        if (!gdm_display_get_id (display, id, NULL)) {
                g_set_error_literal (error,
                                     GDM_DISPLAY_ERROR,
                                     GDM_DISPLAY_ERROR_GENERAL,
                                     "Failed getting display id");
                gdm_display_unmanage (display);
                return FALSE;
        }

        return TRUE;
}

static void
finish_display_on_seat_if_waiting (GdmDisplayStore *display_store,
                                   GdmDisplay      *display,
                                   const char      *seat_id)
{
        if (gdm_display_get_status (display) != GDM_DISPLAY_WAITING_TO_FINISH)
                return;

        g_debug ("GdmLocalDisplayFactory: finish background display\n");
        gdm_display_stop_greeter_session (display);
        gdm_display_unmanage (display);
        gdm_display_finish (display);
}

static void
finish_waiting_displays_on_seat (GdmLocalDisplayFactory *factory,
                                 const char             *seat_id)
{
        GdmDisplayStore *store;

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        gdm_display_store_foreach (store,
                                   (GdmDisplayStoreFunc) finish_display_on_seat_if_waiting,
                                   (gpointer)
                                   seat_id);
}

static gboolean
on_finish_waiting_for_seat0_displays_timeout (GdmLocalDisplayFactory *factory)
{
        g_debug ("GdmLocalDisplayFactory: timeout following VT switch to registered session complete, looking for any background displays to kill");
        finish_waiting_displays_on_seat (factory, "seat0");
        return G_SOURCE_REMOVE;
}

static void
on_display_status_changed (GdmDisplay             *display,
                           GParamSpec             *arg1,
                           GdmLocalDisplayFactory *factory)
{
        int              status;
        char            *seat_id = NULL;
        char            *seat_active_session = NULL;
        char            *session_class = NULL;
        char            *session_id = NULL;
        gboolean         is_initial = TRUE;
        gboolean         is_local = TRUE;
        gboolean         registered = FALSE;


        if (!factory->is_started)
                return;

        g_object_get (display,
                      "seat-id", &seat_id,
                      "is-initial", &is_initial,
                      "is-local", &is_local,
                      "session-class", &session_class,
                      "session-id", &session_id,
                      NULL);

        sd_seat_get_active (seat_id, &seat_active_session, NULL);

        status = gdm_display_get_status (display);

        g_debug ("GdmLocalDisplayFactory: display status changed: %d", status);
        switch (status) {
        case GDM_DISPLAY_FINISHED:
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* if this is a local display, ensure that we get a login
                 * screen when the user logs out.
                 */
                if (is_local &&
                    ((g_strcmp0 (session_class, "greeter") != 0 &&
                      (!seat_active_session || g_strcmp0(session_id, seat_active_session) == 0)) ||
                     factory->active_vt == GDM_INITIAL_VT || g_strcmp0 (seat_id, "seat0") != 0)) {
                        /* reset num failures */
                        factory->num_failures = 0;

                        ensure_display_for_seat (factory, seat_id);
                }
                break;
        case GDM_DISPLAY_FAILED:
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* Create a new equivalent display if it was static */
                if (is_local) {

                        factory->num_failures++;

                        /* oh shit */
                        if (factory->num_failures > MAX_DISPLAY_FAILURES)
                                g_warning ("GdmLocalDisplayFactory: maximum number of display failures reached. Giving up.");
                        else
                                ensure_display_for_seat (factory, seat_id);
                }
                break;
        case GDM_DISPLAY_UNMANAGED:
                break;
        case GDM_DISPLAY_PREPARED:
                break;
        case GDM_DISPLAY_MANAGED:
                g_object_get (display, "session-registered", &registered, NULL);
                if (registered) {
                        g_debug ("GdmLocalDisplayFactory: session registered on display, looking for any background displays to kill");
                        finish_waiting_displays_on_seat (factory, "seat0");
                }
                break;
        case GDM_DISPLAY_WAITING_TO_FINISH:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_free (seat_id);
        g_free (seat_active_session);
        g_free (session_class);
        g_free (session_id);
}

static gboolean
lookup_by_seat_id (const char *id,
                   GdmDisplay *display,
                   gpointer    user_data)
{
        const char *looking_for = user_data;
        char *current;
        gboolean res;

        g_object_get (G_OBJECT (display), "seat-id", &current, NULL);

        res = g_strcmp0 (current, looking_for) == 0;

        g_free(current);

        return res;
}

static gboolean
lookup_prepared_display_by_seat_id (const char *id,
                                    GdmDisplay *display,
                                    gpointer    user_data)
{
        int status;

        status = gdm_display_get_status (display);

        if (status != GDM_DISPLAY_PREPARED)
                return FALSE;

        return lookup_by_seat_id (id, display, user_data);
}

static gboolean
lookup_managed_display_by_seat_id (const char *id,
                                   GdmDisplay *display,
                                   gpointer    user_data)
{
        int status;

        status = gdm_display_get_status (display);

        if (status != GDM_DISPLAY_MANAGED)
                return FALSE;

        return lookup_by_seat_id (id, display, user_data);
}

#ifdef HAVE_UDEV
static gboolean
udev_is_settled (GdmLocalDisplayFactory *factory)
{
        g_autoptr (GUdevEnumerator) enumerator = NULL;
        GList *devices;
        GList *node;

        gboolean is_settled = FALSE;

        if (factory->seat0_has_platform_graphics) {
                g_debug ("GdmLocalDisplayFactory: udev settled, platform graphics enabled.");
                return TRUE;
        }

        if (factory->seat0_has_boot_up_graphics) {
                g_debug ("GdmLocalDisplayFactory: udev settled, boot up graphics available.");
                return TRUE;
        }

        if (factory->seat0_graphics_check_timed_out) {
                g_debug ("GdmLocalDisplayFactory: udev timed out, proceeding anyway.");
                g_clear_signal_handler (&factory->uevent_handler_id, factory->gudev_client);
                return TRUE;
        }

        g_debug ("GdmLocalDisplayFactory: Checking if udev has settled enough to support graphics.");

        enumerator = g_udev_enumerator_new (factory->gudev_client);

        g_udev_enumerator_add_match_name (enumerator, "card*");
        g_udev_enumerator_add_match_tag (enumerator, "master-of-seat");
        g_udev_enumerator_add_match_subsystem (enumerator, "drm");

        devices = g_udev_enumerator_execute (enumerator);
        if (!devices) {
                g_debug ("GdmLocalDisplayFactory: udev has no candidate graphics devices available yet.");
                return FALSE;
        }

        node = devices;
        while (node != NULL) {
                GUdevDevice *device = node->data;
                GList *next_node = node->next;
                const gchar *id_path = g_udev_device_get_property (device, "ID_PATH");
                g_autoptr (GUdevDevice) platform_device = NULL;
                g_autoptr (GUdevDevice) pci_device = NULL;
                g_autoptr (GUdevDevice) drm_device = NULL;

                if (g_strrstr (id_path, "platform-simple-framebuffer") != NULL) {
                        node = next_node;
                        continue;
                }

                platform_device = g_udev_device_get_parent_with_subsystem (device, "platform", NULL);

                if (platform_device != NULL) {
                        g_debug ("GdmLocalDisplayFactory: Found embedded platform graphics, proceeding.");
                        factory->seat0_has_platform_graphics = TRUE;
                        is_settled = TRUE;
                        break;
                }

                pci_device = g_udev_device_get_parent_with_subsystem (device, "pci", NULL);

                if (pci_device != NULL) {
                        gboolean boot_vga;

                        boot_vga = g_udev_device_get_sysfs_attr_as_int (pci_device, "boot_vga");

                        if (boot_vga == 1) {
                                 g_debug ("GdmLocalDisplayFactory: Found primary PCI graphics adapter, proceeding.");
                                 factory->seat0_has_boot_up_graphics = TRUE;
                                 is_settled = TRUE;
                                 break;
                        }
                }

                drm_device = g_udev_device_get_parent_with_subsystem (device, "drm", NULL);

                if (drm_device != NULL) {
                        gboolean boot_display;

                        boot_display = g_udev_device_get_sysfs_attr_as_int (drm_device, "boot_display");

                        if (boot_display == 1) {
                                 g_debug ("GdmLocalDisplayFactory: Found primary PCI graphics adapter, proceeding.");
                                 factory->seat0_has_boot_up_graphics = TRUE;
                                 is_settled = TRUE;
                                 break;
                        }
                }

                if (pci_device != NULL || drm_device != NULL) {
                        g_debug ("GdmLocalDisplayFactory: Found secondary PCI graphics adapter, not proceeding yet.");
                }

                node = next_node;
        }

        g_debug ("GdmLocalDisplayFactory: udev has %ssettled enough for graphics.", is_settled? "" : "not ");
        g_list_free_full (devices, g_object_unref);

        if (is_settled)
                g_clear_signal_handler (&factory->uevent_handler_id, factory->gudev_client);

        return is_settled;
}
#endif

static gboolean
on_seat0_graphics_check_timeout (gpointer user_data)
{
        GdmLocalDisplayFactory *factory = user_data;

        g_warning ("It appears that your system does not have a primary GPU! Proceeding with any GPU");

        factory->seat0_graphics_check_timeout_id = 0;

        /* Simply try to re-add seat0. If it is there already (i.e. CanGraphical
         * turned TRUE, then we'll find it and it will not be created again).
         */
        factory->seat0_graphics_check_timed_out = TRUE;
        ensure_display_for_seat (factory, "seat0");

        return G_SOURCE_REMOVE;
}

static GdmDisplay *
get_display_for_seat (GdmLocalDisplayFactory *factory,
                      const char             *seat_id)
{
        GdmDisplay *display;
        GdmDisplayStore *store;
        gboolean is_seat0;

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        is_seat0 = g_strcmp0 (seat_id, "seat0") == 0;
        if (is_seat0)
                display = gdm_display_store_find (store, lookup_prepared_display_by_seat_id, (gpointer) seat_id);
        else
                display = gdm_display_store_find (store, lookup_managed_display_by_seat_id, (gpointer) seat_id);

        return display != NULL ? g_object_ref (display) : NULL;
}

static void
ensure_display_for_seat (GdmLocalDisplayFactory *factory,
                         const char             *seat_id)
{
        gboolean is_seat0;
        g_auto (GStrv) session_types = NULL;
        g_autoptr (GdmDisplay) display = NULL;
        g_autofree char *login_session_id = NULL;
        int ret;

        g_debug ("GdmLocalDisplayFactory: display for seat %s requested", seat_id);

        /* If we already have a login window, switch to it */
        if (gdm_get_login_window_session_id (seat_id, &login_session_id)) {
                GdmDisplay *display;
                GdmDisplayStore *store;

                store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
                display = gdm_display_store_find (store,
                                                  lookup_by_session_id,
                                                  (gpointer) login_session_id);
                if (display != NULL &&
                    (gdm_display_get_status (display) == GDM_DISPLAY_MANAGED ||
                     gdm_display_get_status (display) == GDM_DISPLAY_WAITING_TO_FINISH)) {
                        g_object_set (G_OBJECT (display), "status", GDM_DISPLAY_MANAGED, NULL);
                        g_debug ("GdmLocalDisplayFactory: session %s found, activating.",
                                 login_session_id);
                        gdm_activate_session_by_id (factory->connection, NULL, seat_id, login_session_id);
                        return;
                }
        }

        is_seat0 = g_strcmp0 (seat_id, "seat0") == 0;

#ifdef HAVE_UDEV
        if (!udev_is_settled (factory)) {
                g_debug ("GdmLocalDisplayFactory: udev is still settling, so not creating display yet");

                if (is_seat0 && factory->seat0_graphics_check_timeout_id == 0) {
                        g_debug("GdmLocalDisplayFactory: Waiting for up to %d seconds for a primary GPU to appear",
                                SEAT0_GRAPHICS_CHECK_TIMEOUT);
                        factory->seat0_graphics_check_timeout_id = g_timeout_add_seconds (SEAT0_GRAPHICS_CHECK_TIMEOUT,
                                                                                          on_seat0_graphics_check_timeout,
                                                                                          factory);
                }

                return;
        }
#endif

        g_clear_handle_id (&factory->seat0_graphics_check_timeout_id, g_source_remove);

        ret = sd_seat_can_graphical (seat_id);

        if (ret < 0) {
                g_critical ("Failed to query CanGraphical information for seat %s", seat_id);
                return;
        }

        if (ret == 0) {
                g_debug ("GdmLocalDisplayFactory: System doesn't currently support graphics");
                if (is_seat0)
                        g_signal_emit (factory, signals[GRAPHICS_UNSUPPORTED], 0);
                return;
        }

        g_debug ("GdmLocalDisplayFactory: System supports graphics");

        session_types = gdm_local_display_factory_get_session_types (factory);

        g_debug ("GdmLocalDisplayFactory: %s login display for seat %s requested",
                 session_types[0], seat_id);

        /* Ensure we don't create the same display more than once */
        display = get_display_for_seat (factory, seat_id);
        if (display != NULL) {
                g_debug ("GdmLocalDisplayFactory: display for %s already created", seat_id);
                return;
        }

        g_debug ("GdmLocalDisplayFactory: Adding display on seat %s", seat_id);

        display = gdm_local_display_new ();
        g_object_set (G_OBJECT (display),
                      "supported-session-types", session_types,
                      "seat-id", seat_id,
                      "is-initial", is_seat0,
                      NULL);

        store_display (factory, display);

        if (!gdm_display_prepare (display))
                gdm_display_unmanage (display);
}

static void
delete_display (GdmLocalDisplayFactory *factory,
                const char             *seat_id) {

        GdmDisplayStore *store;

        g_debug ("GdmLocalDisplayFactory: Removing displays on seat %s", seat_id);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_foreach_remove (store, lookup_by_seat_id, (gpointer) seat_id);
}

static gboolean
gdm_local_display_factory_sync_seats (GdmLocalDisplayFactory *factory)
{
        GError *error = NULL;
        GVariant *result;
        GVariant *array;
        GVariantIter iter;
        const char *seat;

        g_debug ("GdmLocalDisplayFactory: enumerating seats from logind");
        result = g_dbus_connection_call_sync (factory->connection,
                                              "org.freedesktop.login1",
                                              "/org/freedesktop/login1",
                                              "org.freedesktop.login1.Manager",
                                              "ListSeats",
                                              NULL,
                                              G_VARIANT_TYPE ("(a(so))"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL, &error);

        if (!result) {
                g_warning ("GdmLocalDisplayFactory: Failed to issue method call: %s", error->message);
                g_clear_error (&error);
                return FALSE;
        }

        array = g_variant_get_child_value (result, 0);
        g_variant_iter_init (&iter, array);

        while (g_variant_iter_loop (&iter, "(&so)", &seat, NULL)) {
                ensure_display_for_seat (factory, seat);
        }

        g_variant_unref (result);
        g_variant_unref (array);
        return TRUE;
}

static void
on_seat_activate_greeter (GDBusConnection *connection,
                          const gchar     *sender_name,
                          const gchar     *object_path,
                          const gchar     *interface_name,
                          const gchar     *signal_name,
                          GVariant        *parameters,
                          gpointer         user_data)
{
        const char *seat;

        g_variant_get (parameters, "(&s&o)", &seat, NULL);
        ensure_display_for_seat (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat);
}

static void
on_seat_removed (GDBusConnection *connection,
                 const gchar     *sender_name,
                 const gchar     *object_path,
                 const gchar     *interface_name,
                 const gchar     *signal_name,
                 GVariant        *parameters,
                 gpointer         user_data)
{
        const char *seat;

        g_variant_get (parameters, "(&s&o)", &seat, NULL);
        delete_display (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat);
}

static void
on_seat_properties_changed (GDBusConnection *connection,
                            const gchar     *sender_name,
                            const gchar     *object_path,
                            const gchar     *interface_name,
                            const gchar     *signal_name,
                            GVariant        *parameters,
                            gpointer         user_data)
{
        const gchar *seat = NULL;
        g_autoptr(GVariant) changed_props = NULL;
        g_autoptr(GVariant) changed_prop = NULL;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GVariant) reply_value = NULL;
        g_autoptr(GError) error = NULL;
        g_autofree const gchar **invalidated_props = NULL;
        gboolean changed = FALSE;
        int ret;

        /* Acquire seat name */
        reply = g_dbus_connection_call_sync (connection,
                                             sender_name,
                                             object_path,
                                             "org.freedesktop.DBus.Properties",
                                             "Get",
                                             g_variant_new ("(ss)",
                                                            "org.freedesktop.login1.Seat",
                                                            "Id"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, NULL, &error);

        if (reply == NULL) {
                g_debug ("could not acquire seat name: %s", error->message);
                return;
        }

        g_variant_get (reply, "(v)", &reply_value);

        seat = g_variant_get_string (reply_value, NULL);

        if (seat == NULL) {
                g_debug ("seat name is not string");
                return;
        }

        g_variant_get (parameters, "(s@a{sv}^a&s)", NULL, &changed_props, &invalidated_props);

        changed_prop = g_variant_lookup_value (changed_props, "CanGraphical", NULL);
        if (changed_prop)
                changed = TRUE;
        if (!changed && g_strv_contains (invalidated_props, "CanGraphical"))
                changed = TRUE;

        if (!changed)
                return;

        ret = sd_seat_can_graphical (seat);
        if (ret < 0)
                return;

        if (ret != 0) {
                gdm_settings_direct_reload ();
                ensure_display_for_seat (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat);
        } else {
                delete_display (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat);
        }
}

static gboolean
lookup_by_session_id (const char *id,
                      GdmDisplay *display,
                      gpointer    user_data)
{
        const char *looking_for = user_data;
        const char *current;

        current = gdm_display_get_session_id (display);
        return g_strcmp0 (current, looking_for) == 0;
}

static gboolean
lookup_by_tty (const char *id,
              GdmDisplay *display,
              gpointer    user_data)
{
        const char *tty_to_find = user_data;
        g_autofree char *tty_to_check = NULL;
        const char *session_id;
        int ret;

        session_id = gdm_display_get_session_id (display);

        if (!session_id)
                return FALSE;

        ret = sd_session_get_tty (session_id, &tty_to_check);

        if (ret != 0)
                return FALSE;

        return g_strcmp0 (tty_to_check, tty_to_find) == 0;
}

static void
maybe_stop_greeter_in_background (GdmLocalDisplayFactory *factory,
                                  GdmDisplay             *display)
{
        gboolean doing_initial_setup = FALSE;

        if (gdm_display_get_status (display) != GDM_DISPLAY_MANAGED) {
                g_debug ("GdmLocalDisplayFactory: login window not in managed state, so ignoring");
                return;
        }

        g_object_get (G_OBJECT (display),
                      "doing-initial-setup", &doing_initial_setup,
                      NULL);

        /* we don't ever stop initial-setup implicitly */
        if (doing_initial_setup) {
                g_debug ("GdmLocalDisplayFactory: login window is performing initial-setup, so ignoring");
                return;
        }

        g_debug ("GdmLocalDisplayFactory: killing login window once its unused");

        g_object_set (G_OBJECT (display), "status", GDM_DISPLAY_WAITING_TO_FINISH, NULL);
}

static gboolean
on_vt_changed (GIOChannel    *source,
               GIOCondition   condition,
               GdmLocalDisplayFactory *factory)
{
        GdmDisplayStore *store;
        g_autofree char *tty_of_active_vt = NULL;
        g_autofree char *login_session_id = NULL;
        unsigned int previous_vt, new_vt, login_window_vt = 0;
        int n_returned;

        g_debug ("GdmLocalDisplayFactory: received VT change event");
        g_io_channel_seek_position (source, 0, G_SEEK_SET, NULL);

        if (condition & G_IO_PRI) {
                g_autoptr (GError) error = NULL;
                GIOStatus status;

                status = g_io_channel_read_line (source, &tty_of_active_vt, NULL, NULL, &error);

                if (error != NULL) {
                        g_warning ("could not read active VT from kernel: %s", error->message);
                }
                switch (status) {
                        case G_IO_STATUS_ERROR:
                            return G_SOURCE_REMOVE;
                        case G_IO_STATUS_EOF:
                            return G_SOURCE_REMOVE;
                        case G_IO_STATUS_AGAIN:
                            return G_SOURCE_CONTINUE;
                        case G_IO_STATUS_NORMAL:
                            break;
                }
        }

        if ((condition & G_IO_ERR) || (condition & G_IO_HUP)) {
                g_debug ("GdmLocalDisplayFactory: kernel hung up active vt watch");
                return G_SOURCE_REMOVE;
        }

        if (tty_of_active_vt == NULL) {
                g_debug ("GdmLocalDisplayFactory: unable to read active VT from kernel");
                return G_SOURCE_CONTINUE;
        }

        g_strchomp (tty_of_active_vt);

        errno = 0;
        n_returned = sscanf (tty_of_active_vt, "tty%u", &new_vt);

        if (n_returned != 1 || errno != 0) {
                g_critical ("GdmLocalDisplayFactory: Couldn't read active VT (got '%s')",
                            tty_of_active_vt);
                return G_SOURCE_CONTINUE;
        }

        /* don't do anything if we're on the same VT we were before */
        if (new_vt == factory->active_vt) {
                g_debug ("GdmLocalDisplayFactory: VT changed to the same VT, ignoring");
                return G_SOURCE_CONTINUE;
        }

        previous_vt = factory->active_vt;
        factory->active_vt = new_vt;

        /* don't do anything at start up */
        if (previous_vt == 0) {
                g_debug ("GdmLocalDisplayFactory: VT is %u at startup",
                         factory->active_vt);
                return G_SOURCE_CONTINUE;
        }

        g_debug ("GdmLocalDisplayFactory: VT changed from %u to %u",
                 previous_vt, factory->active_vt);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        /* if the old VT was running a wayland login screen kill it
         */
        if (gdm_get_login_window_session_id ("seat0", &login_session_id)) {
                int ret = sd_session_get_vt (login_session_id, &login_window_vt);
                if (ret == 0 && login_window_vt != 0) {
                        g_debug ("GdmLocalDisplayFactory: VT of login window is %u", login_window_vt);
                        if (login_window_vt == previous_vt) {
                                GdmDisplay *display;

                                g_debug ("GdmLocalDisplayFactory: VT switched from login window");

                                display = gdm_display_store_find (store,
                                                                  lookup_by_session_id,
                                                                  (gpointer) login_session_id);
                                if (display != NULL)
                                        maybe_stop_greeter_in_background (factory, display);
                        } else {
                                g_debug ("GdmLocalDisplayFactory: VT not switched from login window");
                        }
                }
        }

        /* If we jumped to a registered user session, we can kill
         * the login screen (after a suitable timeout to avoid flicker)
         */
        if (factory->active_vt != login_window_vt) {
                GdmDisplay *display;

                g_clear_handle_id (&factory->seat0_graphics_check_timeout_id, g_source_remove);

                display = gdm_display_store_find (store,
                                                  lookup_by_tty,
                                                  (gpointer) tty_of_active_vt);

                if (display != NULL) {
                        gboolean registered;

                        g_object_get (display, "session-registered", &registered, NULL);

                        if (registered) {
                                g_debug ("GdmLocalDisplayFactory: switched to registered user session, so reaping login screen in %d seconds",
                                         WAIT_TO_FINISH_TIMEOUT);
                                if (factory->wait_to_finish_timeout_id != 0) {
                                         g_debug ("GdmLocalDisplayFactory: deferring previous login screen clean up operation");
                                         g_source_remove (factory->wait_to_finish_timeout_id);
                                }

                                factory->wait_to_finish_timeout_id = g_timeout_add_seconds (WAIT_TO_FINISH_TIMEOUT,
                                                                                            (GSourceFunc)
                                                                                            on_finish_waiting_for_seat0_displays_timeout,
                                                                                            factory);
                        }
                }
        }

        /* if user jumped back to initial vt and it's empty put a login screen
         * on it (unless a login screen is already running elsewhere, then
         * jump to that login screen)
         */
        if (factory->active_vt != GDM_INITIAL_VT) {
                g_debug ("GdmLocalDisplayFactory: active VT is not initial VT, so ignoring");
                return G_SOURCE_CONTINUE;
        }

        g_debug ("GdmLocalDisplayFactory: creating new display on seat0 because of VT change");

        ensure_display_for_seat (factory, "seat0");

        return G_SOURCE_CONTINUE;
}

#ifdef HAVE_UDEV
static void
on_uevent (GUdevClient *client,
           const char  *action,
           GUdevDevice *device,
           GdmLocalDisplayFactory *factory)
{
        if (!g_udev_device_get_device_file (device))
                return;

        if (g_strcmp0 (action, "add") != 0 &&
            g_strcmp0 (action, "change") != 0)
                return;

        if (!udev_is_settled (factory))
                return;

        gdm_settings_direct_reload ();
        ensure_display_for_seat (factory, "seat0");
}
#endif

static void
gdm_local_display_factory_start_monitor (GdmLocalDisplayFactory *factory)
{
        g_autoptr (GIOChannel) io_channel = NULL;
        const char *subsystems[] = { "drm", NULL };

        factory->seat_new_id = g_dbus_connection_signal_subscribe (factory->connection,
                                                                         "org.freedesktop.login1",
                                                                         "org.freedesktop.login1.Manager",
                                                                         "SeatNew",
                                                                         "/org/freedesktop/login1",
                                                                         NULL,
                                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                                         on_seat_activate_greeter,
                                                                         g_object_ref (factory),
                                                                         g_object_unref);
        factory->seat_removed_id = g_dbus_connection_signal_subscribe (factory->connection,
                                                                             "org.freedesktop.login1",
                                                                             "org.freedesktop.login1.Manager",
                                                                             "SeatRemoved",
                                                                             "/org/freedesktop/login1",
                                                                             NULL,
                                                                             G_DBUS_SIGNAL_FLAGS_NONE,
                                                                             on_seat_removed,
                                                                             g_object_ref (factory),
                                                                             g_object_unref);
        factory->seat_properties_changed_id = g_dbus_connection_signal_subscribe (factory->connection,
                                                                                  "org.freedesktop.login1",
                                                                                  "org.freedesktop.DBus.Properties",
                                                                                  "PropertiesChanged",
                                                                                  NULL,
                                                                                  "org.freedesktop.login1.Seat",
                                                                                  G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE,
                                                                                  on_seat_properties_changed,
                                                                                  g_object_ref (factory),
                                                                                  g_object_unref);
        factory->seat_attention_key = g_dbus_connection_signal_subscribe (factory->connection,
                                                                                "org.freedesktop.login1",
                                                                                "org.freedesktop.login1.Manager",
                                                                                "SecureAttentionKey",
                                                                                "/org/freedesktop/login1",
                                                                                NULL,
                                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                                on_seat_activate_greeter,
                                                                                g_object_ref (factory),
                                                                                g_object_unref);

#ifdef HAVE_UDEV
        factory->gudev_client = g_udev_client_new (subsystems);
        factory->uevent_handler_id = g_signal_connect (factory->gudev_client,
                                                       "uevent",
                                                       G_CALLBACK (on_uevent),
                                                       factory);
#endif

        io_channel = g_io_channel_new_file ("/sys/class/tty/tty0/active", "r", NULL);

        if (io_channel != NULL) {
                factory->active_vt_watch_id =
                        g_io_add_watch (io_channel,
                                        G_IO_PRI,
                                        (GIOFunc)
                                        on_vt_changed,
                                        factory);
        }
}

static void
gdm_local_display_factory_stop_monitor (GdmLocalDisplayFactory *factory)
{
        if (factory->uevent_handler_id) {
                g_signal_handler_disconnect (factory->gudev_client, factory->uevent_handler_id);
                factory->uevent_handler_id = 0;
        }
        g_clear_object (&factory->gudev_client);

        if (factory->seat_new_id) {
                g_dbus_connection_signal_unsubscribe (factory->connection,
                                                      factory->seat_new_id);
                factory->seat_new_id = 0;
        }
        if (factory->seat_removed_id) {
                g_dbus_connection_signal_unsubscribe (factory->connection,
                                                      factory->seat_removed_id);
                factory->seat_removed_id = 0;
        }
        if (factory->seat_properties_changed_id) {
                g_dbus_connection_signal_unsubscribe (factory->connection,
                                                      factory->seat_properties_changed_id);
                factory->seat_properties_changed_id = 0;
        }
        if (factory->seat_attention_key) {
                g_dbus_connection_signal_unsubscribe (factory->connection,
                                                      factory->seat_attention_key);
                factory->seat_attention_key = 0;
        }
        g_clear_handle_id (&factory->active_vt_watch_id, g_source_remove);
        g_clear_handle_id (&factory->wait_to_finish_timeout_id, g_source_remove);
        g_clear_handle_id (&factory->seat0_graphics_check_timeout_id, g_source_remove);
}

static void
on_display_added (GdmDisplayStore        *display_store,
                  const char             *id,
                  GdmLocalDisplayFactory *factory)
{
        GdmDisplay *display;

        display = gdm_display_store_lookup (display_store, id);

        if (display != NULL) {
                g_signal_connect_object (display, "notify::status",
                                         G_CALLBACK (on_display_status_changed),
                                         factory,
                                         0);

                g_object_weak_ref (G_OBJECT (display), (GWeakNotify)on_display_disposed, factory);
        }
}

static void
on_display_removed (GdmDisplayStore        *display_store,
                    GdmDisplay             *display,
                    GdmLocalDisplayFactory *factory)
{
        g_signal_handlers_disconnect_by_func (display, G_CALLBACK (on_display_status_changed), factory);
        g_object_weak_unref (G_OBJECT (display), (GWeakNotify)on_display_disposed, factory);
}

static gboolean
gdm_local_display_factory_start (GdmDisplayFactory *base_factory)
{
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);
        GdmDisplayStore *store;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        factory->is_started = TRUE;

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        g_signal_connect_object (G_OBJECT (store),
                                 "display-added",
                                 G_CALLBACK (on_display_added),
                                 factory,
                                 0);

        g_signal_connect_object (G_OBJECT (store),
                                 "display-removed",
                                 G_CALLBACK (on_display_removed),
                                 factory,
                                 0);

        gdm_local_display_factory_start_monitor (factory);
        return gdm_local_display_factory_sync_seats (factory);
}

static gboolean
gdm_local_display_factory_stop (GdmDisplayFactory *base_factory)
{
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);
        GdmDisplayStore *store;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        gdm_local_display_factory_stop_monitor (factory);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        g_signal_handlers_disconnect_by_func (G_OBJECT (store),
                                              G_CALLBACK (on_display_added),
                                              factory);
        g_signal_handlers_disconnect_by_func (G_OBJECT (store),
                                              G_CALLBACK (on_display_removed),
                                              factory);

        factory->is_started = FALSE;

        return TRUE;
}

static void
gdm_local_display_factory_set_property (GObject       *object,
                                        guint          prop_id,
                                        const GValue  *value,
                                        GParamSpec    *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_local_display_factory_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
handle_create_transient_display (GdmDBusLocalDisplayFactory *skeleton,
                                 GDBusMethodInvocation      *invocation,
                                 GdmLocalDisplayFactory     *factory)
{
        g_autoptr(GError) error = NULL;
        g_autofree char *id = NULL;
        gboolean created;

        created = gdm_local_display_factory_create_display (factory,
                                                            NULL,
                                                            &id,
                                                            &error);
        if (!created) {
                g_dbus_method_invocation_return_gerror (invocation, error);
        } else {
                gdm_dbus_local_display_factory_complete_create_transient_display (skeleton, invocation, id);
        }

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_create_user_display (GdmDBusLocalDisplayFactory *skeleton,
                            GDBusMethodInvocation      *invocation,
                            const char                 *user,
                            GdmLocalDisplayFactory     *factory)
{
        g_autoptr (GError) error = NULL;

        if (!gdm_display_factory_on_user_display_creation (GDM_DISPLAY_FACTORY (factory),
                                                           user,
                                                           &error)) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        if (!gdm_local_display_factory_create_display (factory,
                                                       user,
                                                       NULL,
                                                       &error)) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        gdm_dbus_local_display_factory_complete_create_user_display (skeleton,
                                                                     invocation);

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_destroy_user_display (GdmDBusLocalDisplayFactory *skeleton,
                             GDBusMethodInvocation      *invocation,
                             const char                 *user,
                             GdmLocalDisplayFactory     *factory)
{
        g_autoptr (GError) error = NULL;

        if (!gdm_display_factory_on_user_display_destruction (GDM_DISPLAY_FACTORY (factory),
                                                              user,
                                                              &error)) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        gdm_dbus_local_display_factory_complete_destroy_user_display (skeleton,
                                                                      invocation);

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
on_authorize_method (GdmDBusLocalDisplayFactory *skeleton,
                     GDBusMethodInvocation      *invocation,
                     GdmLocalDisplayFactory     *factory)
{
        g_autoptr (GError) error = NULL;

        if (!gdm_display_factory_authorize_manage_user_displays (GDM_DISPLAY_FACTORY (factory),
                                                                 invocation,
                                                                 &error)) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                return FALSE;
        }

        return TRUE;
}

static gboolean
register_factory (GdmLocalDisplayFactory *factory)
{
        GError *error = NULL;

        error = NULL;
        factory->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (factory->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        factory->skeleton = GDM_DBUS_LOCAL_DISPLAY_FACTORY (gdm_dbus_local_display_factory_skeleton_new ());

        g_signal_connect (factory->skeleton,
                          "handle-create-transient-display",
                          G_CALLBACK (handle_create_transient_display),
                          factory);

        g_signal_connect (factory->skeleton,
                          "handle-create-user-display",
                          G_CALLBACK (handle_create_user_display),
                          factory);

        g_signal_connect (factory->skeleton,
                          "handle-destroy-user-display",
                          G_CALLBACK (handle_destroy_user_display),
                          factory);

        g_signal_connect (factory->skeleton, "g-authorize-method",
                          G_CALLBACK (on_authorize_method),
                          factory);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (factory->skeleton),
                                               factory->connection,
                                               GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH,
                                               &error)) {
                g_critical ("error exporting LocalDisplayFactory object: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        return TRUE;
}

static GObject *
gdm_local_display_factory_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GdmLocalDisplayFactory      *factory;
        gboolean                     res;

        factory = GDM_LOCAL_DISPLAY_FACTORY (G_OBJECT_CLASS (gdm_local_display_factory_parent_class)->constructor (type,
                                                                                                                   n_construct_properties,
                                                                                                                   construct_properties));

        res = register_factory (factory);
        if (! res) {
                g_warning ("Unable to register local display factory with system bus");
        }

        return G_OBJECT (factory);
}

static void
gdm_local_display_factory_class_init (GdmLocalDisplayFactoryClass *klass)
{
        GObjectClass           *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayFactoryClass *factory_class = GDM_DISPLAY_FACTORY_CLASS (klass);

        object_class->get_property = gdm_local_display_factory_get_property;
        object_class->set_property = gdm_local_display_factory_set_property;
        object_class->finalize = gdm_local_display_factory_finalize;
        object_class->constructor = gdm_local_display_factory_constructor;

        factory_class->start = gdm_local_display_factory_start;
        factory_class->stop = gdm_local_display_factory_stop;

        signals [GRAPHICS_UNSUPPORTED] =
                g_signal_new ("graphics-unsupported",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              0);
}

static void
gdm_local_display_factory_init (GdmLocalDisplayFactory *factory)
{
}

static void
gdm_local_display_factory_finalize (GObject *object)
{
        GdmLocalDisplayFactory *factory;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (object));

        factory = GDM_LOCAL_DISPLAY_FACTORY (object);

        g_return_if_fail (factory != NULL);

        g_clear_object (&factory->connection);
        g_clear_object (&factory->skeleton);

        gdm_local_display_factory_stop_monitor (factory);

        G_OBJECT_CLASS (gdm_local_display_factory_parent_class)->finalize (object);
}

GdmLocalDisplayFactory *
gdm_local_display_factory_new (GdmDisplayStore *store)
{
        if (local_display_factory_object != NULL) {
                g_object_ref (local_display_factory_object);
        } else {
                local_display_factory_object = g_object_new (GDM_TYPE_LOCAL_DISPLAY_FACTORY,
                                                             "display-store", store,
                                                             NULL);
                g_object_add_weak_pointer (local_display_factory_object,
                                           (gpointer *) &local_display_factory_object);
        }

        return GDM_LOCAL_DISPLAY_FACTORY (local_display_factory_object);
}
