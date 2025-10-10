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
#include "gdm-legacy-display.h"

#define GDM_DBUS_PATH                       "/org/gnome/DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH GDM_DBUS_PATH "/LocalDisplayFactory"
#define GDM_MANAGER_DBUS_NAME               "org.gnome.DisplayManager.LocalDisplayFactory"

#define MAX_DISPLAY_FAILURES 10
#define N_FAILURES_UNTIL_FALLBACK 5
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
        GHashTable      *used_display_numbers;

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

#if defined(ENABLE_USER_DISPLAY_SERVER)
        unsigned int     active_vt;
        guint            active_vt_watch_id;
        guint            wait_to_finish_timeout_id;
#endif

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
listify_hash (gpointer    key,
              GdmDisplay *display,
              GList     **list)
{
        *list = g_list_prepend (*list, key);
}

static int
sort_nums (gpointer a,
           gpointer b)
{
        guint32 num_a;
        guint32 num_b;

        num_a = GPOINTER_TO_UINT (a);
        num_b = GPOINTER_TO_UINT (b);

        if (num_a > num_b) {
                return 1;
        } else if (num_a < num_b) {
                return -1;
        } else {
                return 0;
        }
}

static guint32
take_next_display_number (GdmLocalDisplayFactory *factory)
{
        GList  *list;
        GList  *l;
        guint32 ret;

        ret = 0;
        list = NULL;

        g_hash_table_foreach (factory->used_display_numbers, (GHFunc)listify_hash, &list);
        if (list == NULL) {
                goto out;
        }

        /* sort low to high */
        list = g_list_sort (list, (GCompareFunc)sort_nums);

        g_debug ("GdmLocalDisplayFactory: Found the following X displays:");
        for (l = list; l != NULL; l = l->next) {
                g_debug ("GdmLocalDisplayFactory: %u", GPOINTER_TO_UINT (l->data));
        }

        for (l = list; l != NULL; l = l->next) {
                guint32 num;
                num = GPOINTER_TO_UINT (l->data);

                /* always fill zero */
                if (l->prev == NULL && num != 0) {
                        ret = 0;
                        break;
                }
                /* now find the first hole */
                if (l->next == NULL || GPOINTER_TO_UINT (l->next->data) != (num + 1)) {
                        ret = num + 1;
                        break;
                }
        }
 out:

        /* now reserve this number */
        g_debug ("GdmLocalDisplayFactory: Reserving X display: %u", ret);
        g_hash_table_insert (factory->used_display_numbers, GUINT_TO_POINTER (ret), NULL);

        return ret;
}

static char *
get_preferred_display_server (GdmLocalDisplayFactory *factory)
{
        g_autofree gchar *preferred_display_server = NULL;
        gboolean wayland_enabled = FALSE, xorg_enabled = FALSE;

#if defined (ENABLE_WAYLAND_SUPPORT) && defined (ENABLE_X11_SUPPORT)
        gdm_settings_direct_get_boolean (GDM_KEY_WAYLAND_ENABLE, &wayland_enabled);
        gdm_settings_direct_get_boolean (GDM_KEY_XORG_ENABLE, &xorg_enabled);
#elif defined (ENABLE_WAYLAND_SUPPORT)
        wayland_enabled = TRUE;
#elif defined (ENABLE_X11_SUPPORT)
        xorg_enabled = TRUE;
#else
#error "GDM needs to be compiled with support for either wayland or Xorg or both"
#endif

        if (wayland_enabled && !xorg_enabled) {
                return g_strdup ("wayland");
        }

        if (!wayland_enabled && !xorg_enabled) {
                return g_strdup ("none");
        }

        gdm_settings_direct_get_string (GDM_KEY_PREFERRED_DISPLAY_SERVER, &preferred_display_server);

        if (g_strcmp0 (preferred_display_server, "wayland") == 0) {
                if (wayland_enabled)
                        return g_strdup (preferred_display_server);
#ifdef ENABLE_X11_SUPPORT
                else
                        return g_strdup ("xorg");
#endif
        }

        if (g_strcmp0 (preferred_display_server, "xorg") == 0) {
#ifdef ENABLE_X11_SUPPORT
                if (xorg_enabled)
                        return g_strdup (preferred_display_server);
                else
#endif
                        return g_strdup ("wayland");
        }

#ifdef ENABLE_X11_SUPPORT
        if (g_strcmp0 (preferred_display_server, "legacy-xorg") == 0) {
                if (xorg_enabled)
                        return g_strdup (preferred_display_server);
        }
#endif

        return g_strdup ("none");
}

struct GdmDisplayServerConfiguration {
        const char *display_server;
        const char *key;
        const char *binary;
        const char *session_type;
} display_server_configuration[] = {
#ifdef ENABLE_WAYLAND_SUPPORT
        { "wayland", GDM_KEY_WAYLAND_ENABLE, NULL, "wayland" },
#endif
#ifdef ENABLE_X11_SUPPORT
        { "xorg", GDM_KEY_XORG_ENABLE, "/usr/bin/Xorg", "x11" },
#endif
        { NULL, NULL, NULL },
};

static gboolean
display_server_enabled (GdmLocalDisplayFactory *factory,
                        const char             *display_server)
{
        size_t i;

        for (i = 0; display_server_configuration[i].display_server != NULL; i++) {
                const char *key = display_server_configuration[i].key;
                const char *binary = display_server_configuration[i].binary;
                gboolean enabled = FALSE;

                if (!g_str_equal (display_server_configuration[i].display_server,
                                  display_server))
                        continue;

#if defined (ENABLE_WAYLAND_SUPPORT) && defined (ENABLE_X11_SUPPORT)
                if (!gdm_settings_direct_get_boolean (key, &enabled) || !enabled)
                        return FALSE;
#endif

                if (binary && !g_file_test (binary, G_FILE_TEST_IS_EXECUTABLE))
                        return FALSE;

                return TRUE;
        }

        return FALSE;
}

static const char *
get_session_type_for_display_server (GdmLocalDisplayFactory *factory,
                                     const char             *display_server)
{
        size_t i;

        for (i = 0; display_server_configuration[i].display_server != NULL; i++) {
                if (!g_str_equal (display_server_configuration[i].display_server,
                                  display_server))
                        continue;

                return display_server_configuration[i].session_type;
        }

        return NULL;
}

static char **
gdm_local_display_factory_get_session_types (GdmLocalDisplayFactory *factory,
                                             gboolean                should_fall_back)
{
        g_autofree gchar *preferred_display_server = NULL;
        const char *fallback_display_server = NULL;
        gboolean wayland_preferred = FALSE;
        gboolean xorg_preferred = FALSE;
        g_autoptr (GPtrArray) session_types_array = NULL;
        char **session_types;

        session_types_array = g_ptr_array_new ();

        preferred_display_server = get_preferred_display_server (factory);

        g_debug ("GdmLocalDisplayFactory: Getting session type (prefers %s, falling back: %s)",
                 preferred_display_server, should_fall_back? "yes" : "no");

        wayland_preferred = g_str_equal (preferred_display_server, "wayland");
        xorg_preferred = g_str_equal (preferred_display_server, "xorg");

#ifdef ENABLE_X11_SUPPORT
        if (wayland_preferred)
                fallback_display_server = "xorg";
        else if (xorg_preferred)
                fallback_display_server = "wayland";
        else
                return NULL;
#endif

        if (!should_fall_back || fallback_display_server == NULL) {
                if (display_server_enabled (factory, preferred_display_server))
                      g_ptr_array_add (session_types_array, (gpointer) get_session_type_for_display_server (factory, preferred_display_server));
        }

#ifdef ENABLE_X11_SUPPORT
        if (display_server_enabled (factory, fallback_display_server))
                g_ptr_array_add (session_types_array, (gpointer) get_session_type_for_display_server (factory, fallback_display_server));
#endif

        if (session_types_array->len == 0)
                return NULL;

        g_ptr_array_add (session_types_array, NULL);

        session_types = g_strdupv ((char **) session_types_array->pdata);

        return session_types;
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

/*
  Example:
  dbus-send --system --dest=org.gnome.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/DisplayManager/Manager \
  org.gnome.DisplayManager.Manager.GetDisplays
*/
gboolean
gdm_local_display_factory_create_transient_display (GdmLocalDisplayFactory *factory,
                                                    char                  **id,
                                                    GError                **error)
{
        gboolean         ret;
        GdmDisplay      *display = NULL;
        gboolean         is_initial = FALSE;
        g_autofree gchar *preferred_display_server = NULL;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        g_debug ("GdmLocalDisplayFactory: Creating transient display");

        preferred_display_server = get_preferred_display_server (factory);

#ifdef ENABLE_USER_DISPLAY_SERVER
        if (g_strcmp0 (preferred_display_server, "wayland") == 0 ||
            g_strcmp0 (preferred_display_server, "xorg") == 0) {
                g_auto(GStrv) session_types = NULL;

                session_types = gdm_local_display_factory_get_session_types (factory, FALSE);

                if (session_types == NULL) {
                        g_set_error_literal (error,
                                             GDM_DISPLAY_ERROR,
                                             GDM_DISPLAY_ERROR_GENERAL,
                                             "Both Wayland and Xorg are unavailable");
                        return FALSE;
                }

                display = gdm_local_display_new ();
                g_object_set (G_OBJECT (display),
                              "session-type", session_types[0],
                              "supported-session-types", session_types,
                              NULL);
                is_initial = TRUE;
        }
#endif
        if (g_strcmp0 (preferred_display_server, "legacy-xorg") == 0) {
                if (display == NULL) {
                        guint32 num;

                        num = take_next_display_number (factory);

                        display = gdm_legacy_display_new (num);
                }
        }

        if (display == NULL) {
                g_set_error_literal (error,
                                     GDM_DISPLAY_ERROR,
                                     GDM_DISPLAY_ERROR_GENERAL,
                                     "Invalid preferred display server configured");
                return FALSE;
        }

        g_object_set (display,
                      "seat-id", "seat0",
                      "allow-timed-login", FALSE,
                      "is-initial", is_initial,
                      NULL);

        store_display (factory, display);

        if (! gdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! gdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
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
on_session_registered_cb (GObject *gobject,
                          GParamSpec *pspec,
                          gpointer user_data)
{
        GdmDisplay *display = GDM_DISPLAY (gobject);
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (user_data);
        gboolean registered;

        g_object_get (display, "session-registered", &registered, NULL);

        if (!registered)
                return;

        g_debug ("GdmLocalDisplayFactory: session registered on display, looking for any background displays to kill");

        finish_waiting_displays_on_seat (factory, "seat0");
}

static void
on_display_status_changed (GdmDisplay             *display,
                           GParamSpec             *arg1,
                           GdmLocalDisplayFactory *factory)
{
        int              status;
        int              num;
        char            *seat_id = NULL;
        char            *seat_active_session = NULL;
        char            *session_type = NULL;
        char            *session_class = NULL;
        char            *session_id = NULL;
        gboolean         is_initial = TRUE;
        gboolean         is_local = TRUE;


        if (!factory->is_started)
                return;

        num = -1;
        gdm_display_get_x11_display_number (display, &num, NULL);

        g_object_get (display,
                      "seat-id", &seat_id,
                      "is-initial", &is_initial,
                      "is-local", &is_local,
                      "session-type", &session_type,
                      "session-class", &session_class,
                      "session-id", &session_id,
                      NULL);

        sd_seat_get_active (seat_id, &seat_active_session, NULL);

        status = gdm_display_get_status (display);

        g_debug ("GdmLocalDisplayFactory: display status changed: %d", status);
        switch (status) {
        case GDM_DISPLAY_FINISHED:
                /* remove the display number from factory->used_display_numbers
                   so that it may be reused */
                if (num != -1) {
                        g_hash_table_remove (factory->used_display_numbers, GUINT_TO_POINTER (num));
                }
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* if this is a local display, ensure that we get a login
                 * screen when the user logs out.
                 */
                if (is_local &&
                    ((g_strcmp0 (session_class, "greeter") != 0 &&
                      (!seat_active_session || g_strcmp0(session_id, seat_active_session) == 0)) ||
#if defined(ENABLE_USER_DISPLAY_SERVER)
                     (g_strcmp0 (seat_id, "seat0") == 0 && factory->active_vt == GDM_INITIAL_VT) ||
#endif
                     g_strcmp0 (seat_id, "seat0") != 0)) {
                        /* reset num failures */
                        factory->num_failures = 0;

                        ensure_display_for_seat (factory, seat_id);
                }
                break;
        case GDM_DISPLAY_FAILED:
                /* leave the display number in factory->used_display_numbers
                   so that it doesn't get reused */
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* Create a new equivalent display if it was static */
                if (is_local) {

                        factory->num_failures++;

                        /* oh shit */
                        if (factory->num_failures > MAX_DISPLAY_FAILURES)
                                g_warning ("GdmLocalDisplayFactory: maximum number of X display failures reached: check X server log for errors");
                        else
                                ensure_display_for_seat (factory, seat_id);
                }
                break;
        case GDM_DISPLAY_UNMANAGED:
                break;
        case GDM_DISPLAY_PREPARED:
                break;
        case GDM_DISPLAY_MANAGED:
#if defined(ENABLE_USER_DISPLAY_SERVER)
                g_signal_connect_object (display,
                                         "notify::session-registered",
                                         G_CALLBACK (on_session_registered_cb),
                                         factory,
                                         0);
#endif
                break;
        case GDM_DISPLAY_WAITING_TO_FINISH:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_free (seat_id);
        g_free (seat_active_session);
        g_free (session_type);
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
                        } else {
                                 g_debug ("GdmLocalDisplayFactory: Found secondary PCI graphics adapter, not proceeding yet.");
                        }
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

static int
on_seat0_graphics_check_timeout (gpointer user_data)
{
        GdmLocalDisplayFactory *factory = user_data;

        factory->seat0_graphics_check_timeout_id = 0;

        /* Simply try to re-add seat0. If it is there already (i.e. CanGraphical
         * turned TRUE, then we'll find it and it will not be created again).
         */
        factory->seat0_graphics_check_timed_out = TRUE;
        ensure_display_for_seat (factory, "seat0");

        return G_SOURCE_REMOVE;
}

GdmDisplay *
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

        return display;
}

static void
ensure_display_for_seat (GdmLocalDisplayFactory *factory,
                         const char             *seat_id)
{
        gboolean seat_supports_graphics;
        gboolean is_seat0;
        gboolean falling_back;
        g_auto (GStrv) session_types = NULL;
        const char *legacy_session_types[] = { "x11", NULL };
        GdmDisplay      *display = NULL;
        g_autofree char *login_session_id = NULL;
        g_autofree gchar *preferred_display_server = NULL;
        gboolean waiting_on_udev = FALSE;

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

        preferred_display_server = get_preferred_display_server (factory);

        if (g_strcmp0 (preferred_display_server, "none") == 0) {
               g_debug ("GdmLocalDisplayFactory: Preferred display server is none, so not creating display");
               return;
        }

#ifdef HAVE_UDEV
        waiting_on_udev = !udev_is_settled (factory);
#endif

        if (!waiting_on_udev) {
                int ret;

                ret = sd_seat_can_graphical (seat_id);

                if (ret < 0) {
                        g_critical ("Failed to query CanGraphical information for seat %s", seat_id);
                        return;
                }

                if (ret == 0) {
                        g_debug ("GdmLocalDisplayFactory: System doesn't currently support graphics");
                        seat_supports_graphics = FALSE;
                } else {
                        g_debug ("GdmLocalDisplayFactory: System supports graphics");
                        seat_supports_graphics = TRUE;
                }
        } else {
               g_debug ("GdmLocalDisplayFactory: udev is still settling, so not creating display yet");
               seat_supports_graphics = FALSE;
        }

        is_seat0 = g_strcmp0 (seat_id, "seat0") == 0;

        falling_back = factory->num_failures > N_FAILURES_UNTIL_FALLBACK;
        session_types = gdm_local_display_factory_get_session_types (factory, falling_back);

        if (session_types == NULL) {
                g_debug ("GdmLocalDisplayFactory: Both Wayland and Xorg are unavailable");
                seat_supports_graphics = FALSE;
        } else {
                g_debug ("GdmLocalDisplayFactory: New displays on seat0 will use %s%s",
                         session_types[0], falling_back? " fallback" : "");
        }

        /* For seat0, we have a fallback logic to still try starting it after
         * SEAT0_GRAPHICS_CHECK_TIMEOUT seconds. i.e. we simply continue even if
         * CanGraphical is unset or udev otherwise never finds a suitable graphics card.
         * This is ugly, but it means we'll come up eventually in some
         * scenarios where no master device is present.
         * Note that we'll force an X11 fallback even though there might be
         * cases where an wayland capable device is present and simply not marked as
         * master-of-seat. In these cases, this should likely be fixed in the
         * udev rules.
         *
         * At the moment, systemd always sets CanGraphical for non-seat0 seats.
         * This is because non-seat0 seats are defined by having master-of-seat
         * set. This means we can avoid the fallback check for non-seat0 seats,
         * which simplifies the code.
         */
        if (is_seat0) {
                if (!seat_supports_graphics) {
                        if (!factory->seat0_graphics_check_timed_out) {
                                if (factory->seat0_graphics_check_timeout_id == 0) {
                                        g_debug ("GdmLocalDisplayFactory: seat0 doesn't yet support graphics.  Waiting %d seconds to try again.", SEAT0_GRAPHICS_CHECK_TIMEOUT);
                                        factory->seat0_graphics_check_timeout_id = g_timeout_add_seconds (SEAT0_GRAPHICS_CHECK_TIMEOUT,
                                                                                                          on_seat0_graphics_check_timeout,
                                                                                                          factory);

                                } else {
                                        /* It is not yet time to force X11 fallback. */
                                        g_debug ("GdmLocalDisplayFactory: seat0 display requested when there is no graphics support before graphics check timeout.");
                                }

                                return;
                        }

#ifdef ENABLE_X11_SUPPORT
                        g_debug ("GdmLocalDisplayFactory: Assuming we can use seat0 for X11 even though system says it doesn't support graphics!");
                        g_debug ("GdmLocalDisplayFactory: This might indicate an issue where the framebuffer device is not tagged as master-of-seat in udev.");
                        seat_supports_graphics = TRUE;
                        g_strfreev (session_types);
                        session_types = g_strdupv ((char **) legacy_session_types);
#endif
                } else {
                        g_clear_handle_id (&factory->seat0_graphics_check_timeout_id, g_source_remove);
                }
        }

        if (!seat_supports_graphics) {
                if (is_seat0)
                        g_signal_emit (factory, signals[GRAPHICS_UNSUPPORTED], 0);
                return;
        }

        g_assert (session_types != NULL);

        if (g_strcmp0 (preferred_display_server, "legacy-xorg") == 0)
                g_debug ("GdmLocalDisplayFactory: Legacy Xorg login display for seat %s requested",
                         seat_id);
        else
                g_debug ("GdmLocalDisplayFactory: %s login display for seat %s requested",
                         session_types[0], seat_id);

        /* Ensure we don't create the same display more than once */
        display = get_display_for_seat (factory, seat_id);
        if (display != NULL) {
                g_debug ("GdmLocalDisplayFactory: display for %s already created", seat_id);
                return;
        }

        g_debug ("GdmLocalDisplayFactory: Adding display on seat %s", seat_id);

#ifdef ENABLE_USER_DISPLAY_SERVER
        if (g_strcmp0 (preferred_display_server, "wayland") == 0 ||
            g_strcmp0 (preferred_display_server, "xorg") == 0) {
                display = gdm_local_display_new ();
                g_object_set (G_OBJECT (display),
                              "session-type", session_types[0],
                              "supported-session-types", session_types,
                              NULL);
        }
#endif

        if (display == NULL) {
                guint32 num;

                num = take_next_display_number (factory);

                display = gdm_legacy_display_new (num);
                g_object_set (G_OBJECT (display),
                              "session-type", legacy_session_types[0],
                              "supported-session-types", legacy_session_types,
                              NULL);
        }

        g_object_set (display, "seat-id", seat_id, NULL);
        g_object_set (display, "is-initial", is_seat0, NULL);

        store_display (factory, display);

        /* let store own the ref */
        g_object_unref (display);

        if (! gdm_display_manage (display)) {
                gdm_display_unmanage (display);
        }

        return;
}

static void
delete_display (GdmLocalDisplayFactory *factory,
                const char             *seat_id) {

        GdmDisplayStore *store;

        g_debug ("GdmLocalDisplayFactory: Removing used_display_numbers on seat %s", seat_id);

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

#if defined(ENABLE_USER_DISPLAY_SERVER)
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
#endif

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

#if defined(ENABLE_USER_DISPLAY_SERVER)
        io_channel = g_io_channel_new_file ("/sys/class/tty/tty0/active", "r", NULL);

        if (io_channel != NULL) {
                factory->active_vt_watch_id =
                        g_io_add_watch (io_channel,
                                        G_IO_PRI,
                                        (GIOFunc)
                                        on_vt_changed,
                                        factory);
        }
#endif
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
#if defined(ENABLE_USER_DISPLAY_SERVER)
        g_clear_handle_id (&factory->active_vt_watch_id, g_source_remove);
        g_clear_handle_id (&factory->wait_to_finish_timeout_id, g_source_remove);
#endif
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
        g_clear_handle_id (&factory->seat0_graphics_check_timeout_id, g_source_remove);

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

        created = gdm_local_display_factory_create_transient_display (factory,
                                                                      &id,
                                                                      &error);
        if (!created) {
                g_dbus_method_invocation_return_gerror (invocation, error);
        } else {
                gdm_dbus_local_display_factory_complete_create_transient_display (skeleton, invocation, id);
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
        factory->used_display_numbers = g_hash_table_new (NULL, NULL);
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

        g_hash_table_destroy (factory->used_display_numbers);

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
