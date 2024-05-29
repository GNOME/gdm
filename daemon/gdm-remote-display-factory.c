/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2022 Joan Torres <joan.torres@suse.com>
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

#include "gdm-remote-display.h"
#include "gdm-remote-display-factory.h"
#include "gdm-remote-display-factory-glue.h"

#define GDM_REMOTE_DISPLAY_FACTORY_DBUS_PATH "/org/gnome/DisplayManager/RemoteDisplayFactory"

struct _GdmRemoteDisplayFactory
{
        GdmDisplayFactory            parent;

        GdmDBusRemoteDisplayFactory *skeleton;
        GDBusConnection             *connection;

        int                          remote_displays_count;

       gboolean                      suspend_inhibited;
       int                           inhibit_suspend_fd;
};

static void     gdm_remote_display_factory_class_init    (GdmRemoteDisplayFactoryClass *klass);
static void     gdm_remote_display_factory_init          (GdmRemoteDisplayFactory      *factory);
static void     gdm_remote_display_factory_finalize      (GObject                     *object);

static gpointer remote_display_factory_object = NULL;

G_DEFINE_TYPE (GdmRemoteDisplayFactory, gdm_remote_display_factory, GDM_TYPE_DISPLAY_FACTORY)

static void
on_display_status_changed (GdmDisplay              *display,
                           GParamSpec              *arg1,
                           GdmRemoteDisplayFactory *factory)
{
        g_debug ("GdmRemoteDisplayFactory: remote display status changed: %d",
                 gdm_display_get_status (display));
}

static gboolean
gdm_remote_display_factory_create_remote_display (GdmRemoteDisplayFactory *factory,
                                                  const char              *remote_id)
{
        GdmDisplay      *display  = NULL;
        GdmDisplayStore *store;

        g_debug ("GdmRemoteDisplayFactory: Creating remote display");

        display = gdm_remote_display_new (remote_id);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_add (store, display);

        if (!gdm_display_prepare (display)) {
                gdm_display_unmanage (display);
                g_object_unref (display);
                return FALSE;
        }

        g_signal_connect_after (display,
                                "notify::status",
                                G_CALLBACK (on_display_status_changed),
                                factory);

        g_object_unref (display);

        return TRUE;
}

static gboolean
handle_create_remote_display (GdmDBusRemoteDisplayFactory *skeleton,
                              GDBusMethodInvocation       *invocation,
                              const char                  *remote_id,
                              GdmRemoteDisplayFactory     *factory)
{
        if (!gdm_remote_display_factory_create_remote_display (factory, remote_id))
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_FAILED,
                                                               "Error creating remote display");
        else
                gdm_dbus_remote_display_factory_complete_create_remote_display (factory->skeleton,
                                                                                invocation);

        return TRUE;
}

static gboolean
register_factory (GdmRemoteDisplayFactory *factory)
{
        GError *error = NULL;

        factory->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (factory->connection == NULL) {
                g_critical ("Error getting system bus: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        factory->skeleton = GDM_DBUS_REMOTE_DISPLAY_FACTORY (gdm_dbus_remote_display_factory_skeleton_new ());

        g_signal_connect (factory->skeleton,
                          "handle-create-remote-display",
                          G_CALLBACK (handle_create_remote_display),
                          factory);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (factory->skeleton),
                                               factory->connection,
                                               GDM_REMOTE_DISPLAY_FACTORY_DBUS_PATH,
                                               &error)) {
                g_critical ("Error exporting RemoteDisplayFactory object: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        return TRUE;
}

static void
gdm_remote_display_factory_finalize (GObject *object)
{
        GdmRemoteDisplayFactory *factory;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_REMOTE_DISPLAY_FACTORY (object));

        factory = GDM_REMOTE_DISPLAY_FACTORY (object);

        g_return_if_fail (factory != NULL);

        g_clear_object (&factory->connection);
        g_clear_object (&factory->skeleton);

        if (factory->inhibit_suspend_fd != -1)
                close (factory->inhibit_suspend_fd);

        G_OBJECT_CLASS (gdm_remote_display_factory_parent_class)->finalize (object);
}

static GObject *
gdm_remote_display_factory_constructor (GType                  type,
                                        guint                  n_construct_properties,
                                        GObjectConstructParam *construct_properties)
{
        GdmRemoteDisplayFactory *factory;
        gboolean                 res;

        factory = GDM_REMOTE_DISPLAY_FACTORY (G_OBJECT_CLASS (gdm_remote_display_factory_parent_class)->constructor (type,
                                                                                                                     n_construct_properties,
                                                                                                                     construct_properties));

        res = register_factory (factory);
        if (!res) {
                g_warning ("Unable to register remote display factory with system bus");
        }

        return G_OBJECT (factory);
}

static void
on_inhibit_suspend_done (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GdmRemoteDisplayFactory *factory = user_data;
        g_autoptr (GUnixFDList) fd_list = NULL;
        g_autoptr (GVariant) res = NULL;
        g_autoptr (GError) error = NULL;
        int idx;

        res = g_dbus_proxy_call_with_unix_fd_list_finish (proxy,
                                                          &fd_list,
                                                          result,
                                                          &error);
        if (!res) {
                g_warning ("Unable to inhibit suspend: %s", error->message);
                factory->suspend_inhibited = FALSE;
                return;
        }

        g_variant_get (res, "(h)", &idx);
        factory->inhibit_suspend_fd = g_unix_fd_list_get (fd_list, idx, &error);
        if (factory->inhibit_suspend_fd == -1) {
                g_warning ("Failed to receive system inhibitor fd: %s", error->message);
                factory->suspend_inhibited = FALSE;
        }
}

static void
inhibit_suspend (GdmRemoteDisplayFactory *factory)
{
        g_autoptr (GDBusProxy) proxy = NULL;
        g_autoptr (GError) error = NULL;

        if (factory->suspend_inhibited)
                return;

        proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               NULL,
                                               "org.freedesktop.login1",
                                               "/org/freedesktop/login1",
                                               "org.freedesktop.login1.Manager",
                                               NULL,
                                               &error);
        if (!proxy) {
                g_warning ("Failed to acquire login1 manager proxy: %s", error->message);
                return;
        }

        factory->suspend_inhibited = TRUE;
        g_dbus_proxy_call_with_unix_fd_list (proxy,
                                             "Inhibit",
                                             g_variant_new ("(ssss)",
                                                            "sleep",
                                                            g_get_user_name (),
                                                            "Remote sessions would die on suspend",
                                                            "block"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             G_MAXINT,
                                             NULL,
                                             NULL,
                                             on_inhibit_suspend_done,
                                             factory);
}


static void
on_display_added (GdmDisplayStore         *store,
                  const char              *id,
                  GdmRemoteDisplayFactory *factory)
{
        GdmDisplay *display = gdm_display_store_lookup (store, id);

        if (!GDM_IS_REMOTE_DISPLAY (display))
                return;

        factory->remote_displays_count++;

        if (factory->remote_displays_count > 0)
                inhibit_suspend (factory);
}

static void
uninhibit_suspend (GdmRemoteDisplayFactory *factory)
{
        if (factory->inhibit_suspend_fd == -1)
                return;

        close (factory->inhibit_suspend_fd);
        factory->inhibit_suspend_fd = -1;
        factory->suspend_inhibited = FALSE;
}

static void
on_display_removed (GdmDisplayStore         *display_store,
                    GdmDisplay              *display,
                    GdmRemoteDisplayFactory *factory)
{
        g_assert (factory->remote_displays_count >= 0);

        if (!GDM_IS_REMOTE_DISPLAY (display))
                return;

        factory->remote_displays_count--;

        if (factory->remote_displays_count == 0)
                uninhibit_suspend (factory);
}

static gboolean
gdm_remote_display_factory_start (GdmDisplayFactory *base_factory)
{
        GdmRemoteDisplayFactory *factory = GDM_REMOTE_DISPLAY_FACTORY (base_factory);
        GdmDisplayStore *store;

        g_return_val_if_fail (GDM_IS_REMOTE_DISPLAY_FACTORY (factory), FALSE);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        g_signal_connect_object (G_OBJECT (store),
                                 "display-added",
                                 G_CALLBACK (on_display_added),
                                 factory,
                                 G_CONNECT_DEFAULT);

        g_signal_connect_object (G_OBJECT (store),
                                 "display-removed",
                                 G_CALLBACK (on_display_removed),
                                 factory,
                                 G_CONNECT_DEFAULT);

        return TRUE;
}

static gboolean
gdm_remote_display_factory_stop (GdmDisplayFactory *base_factory)
{
        GdmRemoteDisplayFactory *factory = GDM_REMOTE_DISPLAY_FACTORY (base_factory);
        g_return_val_if_fail (GDM_IS_REMOTE_DISPLAY_FACTORY (factory), FALSE);
        return TRUE;
}

static void
gdm_remote_display_factory_class_init (GdmRemoteDisplayFactoryClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayFactoryClass *factory_class = GDM_DISPLAY_FACTORY_CLASS (klass);

        object_class->finalize = gdm_remote_display_factory_finalize;
        object_class->constructor = gdm_remote_display_factory_constructor;

        factory_class->start = gdm_remote_display_factory_start;
        factory_class->stop = gdm_remote_display_factory_stop;
}

static void
gdm_remote_display_factory_init (GdmRemoteDisplayFactory *factory)
{
  factory->inhibit_suspend_fd = -1;
}

GdmRemoteDisplayFactory *
gdm_remote_display_factory_new (GdmDisplayStore *store)
{
        if (remote_display_factory_object != NULL) {
                g_object_ref (remote_display_factory_object);
        } else {
                remote_display_factory_object = g_object_new (GDM_TYPE_REMOTE_DISPLAY_FACTORY,
                                                              "display-store", store,
                                                              NULL);
                g_object_add_weak_pointer (remote_display_factory_object,
                                           (gpointer *) &remote_display_factory_object);
        }

        return GDM_REMOTE_DISPLAY_FACTORY (remote_display_factory_object);
}
