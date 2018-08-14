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

#define GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LOCAL_DISPLAY_FACTORY, GdmLocalDisplayFactoryPrivate))

#define GDM_DBUS_PATH                       "/org/gnome/DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH GDM_DBUS_PATH "/LocalDisplayFactory"
#define GDM_MANAGER_DBUS_NAME               "org.gnome.DisplayManager.LocalDisplayFactory"

#define MAX_DISPLAY_FAILURES 5

struct GdmLocalDisplayFactoryPrivate
{
        GdmDBusLocalDisplayFactory *skeleton;
        GDBusConnection *connection;
        GHashTable      *used_display_numbers;

        /* FIXME: this needs to be per seat? */
        guint            num_failures;

        guint            seat_new_id;
        guint            seat_removed_id;

#if defined(ENABLE_WAYLAND_SUPPORT) && defined(ENABLE_USER_DISPLAY_SERVER)
        char            *tty_of_active_vt;
        guint            active_vt_watch_id;
#endif
};

enum {
        PROP_0,
};

static void     gdm_local_display_factory_class_init    (GdmLocalDisplayFactoryClass *klass);
static void     gdm_local_display_factory_init          (GdmLocalDisplayFactory      *factory);
static void     gdm_local_display_factory_finalize      (GObject                     *object);

static GdmDisplay *create_display                       (GdmLocalDisplayFactory      *factory,
                                                         const char                  *seat_id,
                                                         const char                  *session_type,
                                                         gboolean                    initial_display);

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

        g_hash_table_foreach (factory->priv->used_display_numbers, (GHFunc)listify_hash, &list);
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
        g_hash_table_insert (factory->priv->used_display_numbers, GUINT_TO_POINTER (ret), NULL);

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

static gboolean
gdm_local_display_factory_use_wayland (void)
{
#ifdef ENABLE_WAYLAND_SUPPORT
        gboolean wayland_enabled = FALSE;
        if (gdm_settings_direct_get_boolean (GDM_KEY_WAYLAND_ENABLE, &wayland_enabled)) {
                if (wayland_enabled && g_file_test ("/usr/bin/Xwayland", G_FILE_TEST_IS_EXECUTABLE) )
                        return TRUE;
        }
#endif
        return FALSE;
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

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        g_debug ("GdmLocalDisplayFactory: Creating transient display");

#ifdef ENABLE_USER_DISPLAY_SERVER
        display = gdm_local_display_new ();
        if (gdm_local_display_factory_use_wayland ())
                g_object_set (G_OBJECT (display), "session-type", "wayland", NULL);
#else
        if (display == NULL) {
                guint32 num;

                num = take_next_display_number (factory);

                display = gdm_legacy_display_new (num);
        }
#endif

        g_object_set (display,
                      "seat-id", "seat0",
                      "allow-timed-login", FALSE,
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
on_display_status_changed (GdmDisplay             *display,
                           GParamSpec             *arg1,
                           GdmLocalDisplayFactory *factory)
{
        int              status;
        int              num;
        char            *seat_id = NULL;
        char            *session_type = NULL;
        char            *session_class = NULL;
        gboolean         is_initial = TRUE;
        gboolean         is_local = TRUE;

        num = -1;
        gdm_display_get_x11_display_number (display, &num, NULL);

        g_object_get (display,
                      "seat-id", &seat_id,
                      "is-initial", &is_initial,
                      "is-local", &is_local,
                      "session-type", &session_type,
                      "session-class", &session_class,
                      NULL);

        status = gdm_display_get_status (display);

        g_debug ("GdmLocalDisplayFactory: display status changed: %d", status);
        switch (status) {
        case GDM_DISPLAY_FINISHED:
                /* remove the display number from factory->priv->used_display_numbers
                   so that it may be reused */
                if (num != -1) {
                        g_hash_table_remove (factory->priv->used_display_numbers, GUINT_TO_POINTER (num));
                }
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* if this is a local display, do a full resync.  Only
                 * seats without displays will get created anyway.  This
                 * ensures we get a new login screen when the user logs out,
                 * if there isn't one.
                 */
                if (is_local && g_strcmp0 (session_class, "greeter") != 0) {
                        /* reset num failures */
                        factory->priv->num_failures = 0;

                        gdm_local_display_factory_sync_seats (factory);
                }
                break;
        case GDM_DISPLAY_FAILED:
                /* leave the display number in factory->priv->used_display_numbers
                   so that it doesn't get reused */
                gdm_display_factory_queue_purge_displays (GDM_DISPLAY_FACTORY (factory));

                /* Create a new equivalent display if it was static */
                if (is_local) {

                        factory->priv->num_failures++;

                        if (factory->priv->num_failures > MAX_DISPLAY_FAILURES) {
                                /* oh shit */
                                g_warning ("GdmLocalDisplayFactory: maximum number of X display failures reached: check X server log for errors");
                        } else {
#ifdef ENABLE_WAYLAND_SUPPORT
                                if (g_strcmp0 (session_type, "wayland") == 0) {
                                        g_free (session_type);
                                        session_type = NULL;
                                }

#endif
                                create_display (factory, seat_id, session_type, is_initial);
                        }
                }
                break;
        case GDM_DISPLAY_UNMANAGED:
                break;
        case GDM_DISPLAY_PREPARED:
                break;
        case GDM_DISPLAY_MANAGED:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_free (seat_id);
        g_free (session_type);
        g_free (session_class);
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

static GdmDisplay *
create_display (GdmLocalDisplayFactory *factory,
                const char             *seat_id,
                const char             *session_type,
                gboolean                initial)
{
        GdmDisplayStore *store;
        GdmDisplay      *display = NULL;
        char            *active_session_id = NULL;
        int              ret;

        g_debug ("GdmLocalDisplayFactory: %s login display for seat %s requested",
                 session_type? : "X11", seat_id);
        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        if (sd_seat_can_multi_session (seat_id))
                display = gdm_display_store_find (store, lookup_prepared_display_by_seat_id, (gpointer) seat_id);
        else
                display = gdm_display_store_find (store, lookup_by_seat_id, (gpointer) seat_id);

        /* Ensure we don't create the same display more than once */
        if (display != NULL) {
                g_debug ("GdmLocalDisplayFactory: display already created");
                return NULL;
        }

        ret = sd_seat_get_active (seat_id, &active_session_id, NULL);

        if (ret == 0) {
                char *login_session_id = NULL;

                /* If we already have a login window, switch to it */
                if (gdm_get_login_window_session_id (seat_id, &login_session_id)) {
                        GdmDisplay *display;

                        display = gdm_display_store_find (store,
                                                          lookup_by_session_id,
                                                          (gpointer) login_session_id);
                        if (display != NULL && gdm_display_get_status (display) == GDM_DISPLAY_MANAGED) {
                                if (g_strcmp0 (active_session_id, login_session_id) != 0) {
                                        g_debug ("GdmLocalDisplayFactory: session %s found, activating.",
                                                 login_session_id);
                                        gdm_activate_session_by_id (factory->priv->connection, seat_id, login_session_id);
                                }
                                g_clear_pointer (&login_session_id, g_free);
                                g_clear_pointer (&active_session_id, g_free);
                                return NULL;
                        }
                        g_clear_pointer (&login_session_id, g_free);
                }
                g_clear_pointer (&active_session_id, g_free);
        }

        g_debug ("GdmLocalDisplayFactory: Adding display on seat %s", seat_id);

#ifdef ENABLE_USER_DISPLAY_SERVER
        if (g_strcmp0 (seat_id, "seat0") == 0) {
                display = gdm_local_display_new ();
                if (session_type != NULL) {
                        g_object_set (G_OBJECT (display), "session-type", session_type, NULL);
                }
        }
#endif

        if (display == NULL) {
                guint32 num;

                num = take_next_display_number (factory);

                display = gdm_legacy_display_new (num);
        }

        g_object_set (display, "seat-id", seat_id, NULL);
        g_object_set (display, "is-initial", initial, NULL);

        store_display (factory, display);

        /* let store own the ref */
        g_object_unref (display);

        if (! gdm_display_manage (display)) {
                gdm_display_unmanage (display);
        }

        return display;
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
        result = g_dbus_connection_call_sync (factory->priv->connection,
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
                gboolean is_initial;
                const char *session_type = NULL;

                if (g_strcmp0 (seat, "seat0") == 0) {
                        is_initial = TRUE;
                        if (gdm_local_display_factory_use_wayland ())
                                session_type = "wayland";
                } else {
                        is_initial = FALSE;
                }

                create_display (factory, seat, session_type, is_initial);
        }

        g_variant_unref (result);
        g_variant_unref (array);
        return TRUE;
}

static void
on_seat_new (GDBusConnection *connection,
             const gchar     *sender_name,
             const gchar     *object_path,
             const gchar     *interface_name,
             const gchar     *signal_name,
             GVariant        *parameters,
             gpointer         user_data)
{
        const char *seat;

        g_variant_get (parameters, "(&s&o)", &seat, NULL);
        create_display (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat, NULL, FALSE);
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

#if defined(ENABLE_WAYLAND_SUPPORT) && defined(ENABLE_USER_DISPLAY_SERVER)
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

static void
maybe_stop_greeter_display (GdmDisplay *display)
{
        g_autofree char *display_session_type = NULL;

        if (gdm_display_get_status (display) != GDM_DISPLAY_MANAGED) {
                g_debug ("GdmLocalDisplayFactory: login window not in managed state, so ignoring");
                return;
        }

        g_object_get (G_OBJECT (display),
                      "session-type", &display_session_type,
                      NULL);

        /* we can only stop greeter for wayland sessions, since
         * X server would jump back on exit */
        if (g_strcmp0 (display_session_type, "wayland") != 0) {
                g_debug ("GdmLocalDisplayFactory: login window is running on Xorg, so ignoring");
                return;
        }

        g_debug ("GdmLocalDisplayFactory: killing login window since its now unused");
        gdm_display_stop_greeter_session (display);
        gdm_display_unmanage (display);
        gdm_display_finish (display);
}

static gboolean
on_vt_changed (GIOChannel    *source,
               GIOCondition   condition,
               GdmLocalDisplayFactory *factory)
{
        GIOStatus status;
        static const char *tty_of_initial_vt = "tty" GDM_INITIAL_VT;
        g_autofree char *tty_of_previous_vt = NULL;
        g_autofree char *tty_of_active_vt = NULL;
        g_autofree char *login_session_id = NULL;
        g_autofree char *active_session_id = NULL;
        const char *session_type = NULL;
        int ret;

        g_debug ("GdmLocalDisplayFactory: received VT change event");
        g_io_channel_seek_position (source, 0, G_SEEK_SET, NULL);

        if (condition & G_IO_PRI) {
                g_autoptr (GError) error = NULL;
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

        /* don't do anything if we're on the same VT we were before */
        if (g_strcmp0 (tty_of_active_vt, factory->priv->tty_of_active_vt) == 0) {
                g_debug ("GdmLocalDisplayFactory: VT changed to the same VT, ignoring");
                return G_SOURCE_CONTINUE;
        }

        tty_of_previous_vt = g_steal_pointer (&factory->priv->tty_of_active_vt);
        factory->priv->tty_of_active_vt = g_steal_pointer (&tty_of_active_vt);

        /* don't do anything at start up */
        if (tty_of_previous_vt == NULL) {
                g_debug ("GdmLocalDisplayFactory: VT is %s at startup",
                         factory->priv->tty_of_active_vt);
                return G_SOURCE_CONTINUE;
        }

        g_debug ("GdmLocalDisplayFactory: VT changed from %s to %s",
                 tty_of_previous_vt, factory->priv->tty_of_active_vt);

        /* if the old VT was running a wayland login screen kill it
         */
        if (gdm_get_login_window_session_id ("seat0", &login_session_id)) {
                unsigned int vt;

                ret = sd_session_get_vt (login_session_id, &vt);
                if (ret == 0 && vt != 0) {
                        g_autofree char *tty_of_login_window_vt = NULL;

                        tty_of_login_window_vt = g_strdup_printf ("tty%u", vt);

                        g_debug ("GdmLocalDisplayFactory: tty of login window is %s", tty_of_login_window_vt);
                        if (g_strcmp0 (tty_of_login_window_vt, tty_of_previous_vt) == 0) {
                                GdmDisplayStore *store;
                                GdmDisplay *display;

                                g_debug ("GdmLocalDisplayFactory: VT switched from login window");

                                store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
                                display = gdm_display_store_find (store,
                                                                  lookup_by_session_id,
                                                                  (gpointer) login_session_id);

                                if (display != NULL)
                                        maybe_stop_greeter_display (display);
                        } else {
                                g_debug ("GdmLocalDisplayFactory: VT not switched from login window");
                        }
                }
        }

        /* if user jumped back to initial vt and it's empty put a login screen
         * on it (unless a login screen is already running elsewhere, then
         * jump to that login screen)
         */
        if (strcmp (factory->priv->tty_of_active_vt, tty_of_initial_vt) != 0) {
                g_debug ("GdmLocalDisplayFactory: active VT is not initial VT, so ignoring");
                return G_SOURCE_CONTINUE;
        }

        ret = sd_seat_get_active ("seat0", &active_session_id, NULL);

        if (ret == 0) {
                g_autofree char *state = NULL;
                ret = sd_session_get_state (active_session_id, &state);

                /* if there's something already running on the active VT then bail */
                if (ret == 0 && g_strcmp0 (state, "closing") != 0) {
                        g_debug ("GdmLocalDisplayFactory: initial VT is in use, so ignoring");
                        return G_SOURCE_CONTINUE;
                }
        }

        if (gdm_local_display_factory_use_wayland ())
                session_type = "wayland";

        g_debug ("GdmLocalDisplayFactory: creating new display on seat0 because of VT change");

        create_display (factory, "seat0", session_type, TRUE);

        return G_SOURCE_CONTINUE;
}
#endif

static void
gdm_local_display_factory_start_monitor (GdmLocalDisplayFactory *factory)
{
        g_autoptr (GIOChannel) io_channel = NULL;

        factory->priv->seat_new_id = g_dbus_connection_signal_subscribe (factory->priv->connection,
                                                                         "org.freedesktop.login1",
                                                                         "org.freedesktop.login1.Manager",
                                                                         "SeatNew",
                                                                         "/org/freedesktop/login1",
                                                                         NULL,
                                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                                         on_seat_new,
                                                                         g_object_ref (factory),
                                                                         g_object_unref);
        factory->priv->seat_removed_id = g_dbus_connection_signal_subscribe (factory->priv->connection,
                                                                             "org.freedesktop.login1",
                                                                             "org.freedesktop.login1.Manager",
                                                                             "SeatRemoved",
                                                                             "/org/freedesktop/login1",
                                                                             NULL,
                                                                             G_DBUS_SIGNAL_FLAGS_NONE,
                                                                             on_seat_removed,
                                                                             g_object_ref (factory),
                                                                             g_object_unref);

#if defined(ENABLE_WAYLAND_SUPPORT) && defined(ENABLE_USER_DISPLAY_SERVER)
        io_channel = g_io_channel_new_file ("/sys/class/tty/tty0/active", "r", NULL);

        if (io_channel != NULL) {
                factory->priv->active_vt_watch_id =
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
        if (factory->priv->seat_new_id) {
                g_dbus_connection_signal_unsubscribe (factory->priv->connection,
                                                      factory->priv->seat_new_id);
                factory->priv->seat_new_id = 0;
        }
        if (factory->priv->seat_removed_id) {
                g_dbus_connection_signal_unsubscribe (factory->priv->connection,
                                                      factory->priv->seat_removed_id);
                factory->priv->seat_removed_id = 0;
        }
#if defined(ENABLE_WAYLAND_SUPPORT) && defined(ENABLE_USER_DISPLAY_SERVER)
        if (factory->priv->active_vt_watch_id) {
                g_source_remove (factory->priv->active_vt_watch_id);
                factory->priv->active_vt_watch_id = 0;
        }

        g_clear_pointer (&factory->priv->tty_of_active_vt, g_free);
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
        GError *error = NULL;
        gboolean created;
        char *id = NULL;

        created = gdm_local_display_factory_create_transient_display (factory,
                                                                      &id,
                                                                      &error);
        if (!created) {
                g_dbus_method_invocation_return_gerror (invocation, error);
        } else {
                gdm_dbus_local_display_factory_complete_create_transient_display (skeleton, invocation, id);
        }

        g_free (id);
        return TRUE;
}

static gboolean
register_factory (GdmLocalDisplayFactory *factory)
{
        GError *error = NULL;

        error = NULL;
        factory->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (factory->priv->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        factory->priv->skeleton = GDM_DBUS_LOCAL_DISPLAY_FACTORY (gdm_dbus_local_display_factory_skeleton_new ());

        g_signal_connect (factory->priv->skeleton,
                          "handle-create-transient-display",
                          G_CALLBACK (handle_create_transient_display),
                          factory);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (factory->priv->skeleton),
                                               factory->priv->connection,
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

        g_type_class_add_private (klass, sizeof (GdmLocalDisplayFactoryPrivate));
}

static void
gdm_local_display_factory_init (GdmLocalDisplayFactory *factory)
{
        factory->priv = GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE (factory);

        factory->priv->used_display_numbers = g_hash_table_new (NULL, NULL);
}

static void
gdm_local_display_factory_finalize (GObject *object)
{
        GdmLocalDisplayFactory *factory;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (object));

        factory = GDM_LOCAL_DISPLAY_FACTORY (object);

        g_return_if_fail (factory->priv != NULL);

        g_clear_object (&factory->priv->connection);
        g_clear_object (&factory->priv->skeleton);

        g_hash_table_destroy (factory->priv->used_display_numbers);

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
