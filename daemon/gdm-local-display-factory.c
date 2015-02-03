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

#include "gdm-common.h"
#include "gdm-manager.h"
#include "gdm-display-factory.h"
#include "gdm-local-display-factory.h"
#include "gdm-local-display-factory-glue.h"

#include "gdm-display-store.h"
#include "gdm-local-display.h"

#define GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LOCAL_DISPLAY_FACTORY, GdmLocalDisplayFactoryPrivate))

#define CK_SEAT1_PATH                       "/org/freedesktop/ConsoleKit/Seat1"
#define SYSTEMD_SEAT0_PATH                  "seat0"

#define GDM_DBUS_PATH                       "/org/gnome/DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH GDM_DBUS_PATH "/LocalDisplayFactory"
#define GDM_MANAGER_DBUS_NAME               "org.gnome.DisplayManager.LocalDisplayFactory"

#define MAX_DISPLAY_FAILURES 5

struct GdmLocalDisplayFactoryPrivate
{
        GdmDBusLocalDisplayFactory *skeleton;
        GDBusConnection *connection;
        GHashTable      *displays;

        /* FIXME: this needs to be per seat? */
        guint            num_failures;

#ifdef WITH_SYSTEMD
        guint            seat_new_id;
        guint            seat_removed_id;
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
                                                         gboolean                    initial_display);

static void     on_display_status_changed               (GdmDisplay                  *display,
                                                         GParamSpec                  *arg1,
                                                         GdmLocalDisplayFactory      *factory);

static gpointer local_display_factory_object = NULL;

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

        g_hash_table_foreach (factory->priv->displays, (GHFunc)listify_hash, &list);
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
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (ret), NULL);

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
               guint32                 num,
               GdmDisplay             *display)
{
        GdmDisplayStore *store;

        g_signal_connect (display, "notify::status",
                          G_CALLBACK (on_display_status_changed), factory);

        g_object_weak_ref (G_OBJECT (display), (GWeakNotify)on_display_disposed, factory);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_add (store, display);

        /* now fill our reserved spot */
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (num), NULL);
}

static const char *
get_seat_of_transient_display (GdmLocalDisplayFactory *factory)
{
        const char *seat_id;

        /* FIXME: don't hardcode seat */
#ifdef WITH_SYSTEMD
        seat_id = SYSTEMD_SEAT0_PATH;
#else
        seat_id = CK_SEAT1_PATH;
#endif

        return seat_id;
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
        GdmDisplay      *display;
        guint32          num;
        const char      *seat_id;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        num = take_next_display_number (factory);

        g_debug ("GdmLocalDisplayFactory: Creating transient display %d", num);

        display = gdm_local_display_new (num);

        seat_id = get_seat_of_transient_display (factory);
        g_object_set (display,
                      "seat-id", seat_id,
                      "allow-timed-login", FALSE,
                      NULL);

        store_display (factory, num, display);

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
        GdmDisplayStore *store;
        int              num;
        char            *seat_id = NULL;
        gboolean         is_initial = TRUE;

        num = -1;
        gdm_display_get_x11_display_number (display, &num, NULL);
        g_assert (num != -1);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        g_object_get (display, "seat-id", &seat_id, NULL);
        g_object_get (display, "is-initial", &is_initial, NULL);

        status = gdm_display_get_status (display);

        g_debug ("GdmLocalDisplayFactory: display status changed: %d", status);
        switch (status) {
        case GDM_DISPLAY_FINISHED:
                /* remove the display number from factory->priv->displays
                   so that it may be reused */
                g_hash_table_remove (factory->priv->displays, GUINT_TO_POINTER (num));
                gdm_display_store_remove (store, display);

                /* Create a new equivalent display if it was static */
                if (GDM_IS_LOCAL_DISPLAY (display)) {
                        /* reset num failures */
                        factory->priv->num_failures = 0;

                        create_display (factory, seat_id, is_initial);
                }
                break;
        case GDM_DISPLAY_FAILED:
                /* leave the display number in factory->priv->displays
                   so that it doesn't get reused */
                gdm_display_store_remove (store, display);

                /* Create a new equivalent display if it was static */
                if (GDM_IS_LOCAL_DISPLAY (display)) {

                        factory->priv->num_failures++;

                        if (factory->priv->num_failures > MAX_DISPLAY_FAILURES) {
                                /* oh shit */
                                g_warning ("GdmLocalDisplayFactory: maximum number of X display failures reached: check X server log for errors");
                                /* FIXME: should monitor hardware changes to
                                   try again when seats change */
                        } else {
                                create_display (factory, seat_id, is_initial);
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

static GdmDisplay *
create_display (GdmLocalDisplayFactory *factory,
                const char             *seat_id,
                gboolean                initial)
{
        GdmDisplayStore *store;
        GdmDisplay      *display;
        guint32          num;

        /* Ensure we don't create the same display more than once */
        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        display = gdm_display_store_find (store, lookup_by_seat_id, (gpointer) seat_id);
        if (display != NULL) {
                return NULL;
        }

        g_debug ("GdmLocalDisplayFactory: Adding display on seat %s", seat_id);

        num = take_next_display_number (factory);

        display = gdm_local_display_new (num);

        g_object_set (display, "seat-id", seat_id, NULL);
        g_object_set (display, "is-initial", initial, NULL);

        store_display (factory, num, display);

        /* let store own the ref */
        g_object_unref (display);

        if (! gdm_display_manage (display)) {
                gdm_display_unmanage (display);
        }

        return display;
}

#ifdef WITH_SYSTEMD

static void
delete_display (GdmLocalDisplayFactory *factory,
                const char             *seat_id) {

        GdmDisplayStore *store;

        g_debug ("GdmLocalDisplayFactory: Removing displays on seat %s", seat_id);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_foreach_remove (store, lookup_by_seat_id, (gpointer) seat_id);
}

static gboolean gdm_local_display_factory_sync_seats (GdmLocalDisplayFactory *factory)
{
        GError *error = NULL;
        GVariant *result;
        GVariant *array;
        GVariantIter iter;
        const char *seat;

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

        while (g_variant_iter_loop (&iter, "(&so)", &seat, NULL))
                create_display (factory, seat, TRUE);

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
        create_display (GDM_LOCAL_DISPLAY_FACTORY (user_data), seat, FALSE);
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
gdm_local_display_factory_start_monitor (GdmLocalDisplayFactory *factory)
{
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
}

#endif

static gboolean
gdm_local_display_factory_start (GdmDisplayFactory *base_factory)
{
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);
        GdmDisplay             *display;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                gdm_local_display_factory_start_monitor (factory);
                return gdm_local_display_factory_sync_seats (factory);
        }
#endif

        /* On ConsoleKit just create Seat1, and that's it. */
        display = create_display (factory, CK_SEAT1_PATH, TRUE);

        return display != NULL;
}

static gboolean
gdm_local_display_factory_stop (GdmDisplayFactory *base_factory)
{
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

#ifdef WITH_SYSTEMD
        gdm_local_display_factory_stop_monitor (factory);
#endif

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
                exit (1);
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
                exit (1);
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

        factory->priv->displays = g_hash_table_new (NULL, NULL);
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

        g_hash_table_destroy (factory->priv->displays);

#ifdef WITH_SYSTEMD
        gdm_local_display_factory_stop_monitor (factory);
#endif

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
