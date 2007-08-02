/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-local-display-factory.h"
#include "gdm-display-store.h"
#include "gdm-static-display.h"
#include "gdm-static-factory-display.h"

#define GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LOCAL_DISPLAY_FACTORY, GdmLocalDisplayFactoryPrivate))

#define HAL_DBUS_NAME                           "org.freedesktop.Hal"
#define HAL_DBUS_MANAGER_PATH                   "/org/freedesktop/Hal/Manager"
#define HAL_DBUS_MANAGER_INTERFACE              "org.freedesktop.Hal.Manager"
#define HAL_DBUS_DEVICE_INTERFACE               "org.freedesktop.Hal.Device"
#define SEAT_PCI_DEVICE_CLASS                   3

struct GdmLocalDisplayFactoryPrivate
{
	GdmDisplayStore *display_store;

	DBusGConnection *connection;
	DBusGProxy      *proxy;

};

enum {
	DISPLAY_ADDED,
	DISPLAY_REMOVED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_DISPLAY_STORE,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_local_display_factory_class_init	(GdmLocalDisplayFactoryClass *klass);
static void	gdm_local_display_factory_init	        (GdmLocalDisplayFactory      *factory);
static void	gdm_local_display_factory_finalize	(GObject	             *object);

static gpointer local_display_factory_object = NULL;

G_DEFINE_TYPE (GdmLocalDisplayFactory, gdm_local_display_factory, G_TYPE_OBJECT)

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
get_pci_seat_devices (GdmLocalDisplayFactory *factory,
		      GList                  *seats)
{
	char      **devices;
	const char *key;
	const char *value;
	GError     *error;
	gboolean    res;
	int         i;

	g_debug ("Getting PCI seat devices");

	key = "info.bus";
	value = "pci";

	devices = NULL;
	error = NULL;
        res = dbus_g_proxy_call (factory->priv->proxy,
				 "FindDeviceStringMatch",
				 &error,
                                 G_TYPE_STRING, key,
                                 G_TYPE_STRING, value,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRV, &devices,
                                 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to query HAL: %s", error->message);
		g_error_free (error);
	}

	/* now look for pci class 3 */
	key = "pci.device_class";
	for (i = 0; devices [i] != NULL; i++) {
		DBusGProxy *device_proxy;
		int         class_val;

		device_proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
							  HAL_DBUS_NAME,
							  devices [i],
							  HAL_DBUS_DEVICE_INTERFACE);
		if (device_proxy == NULL) {
			continue;
		}

		res = dbus_g_proxy_call (device_proxy,
					 "GetPropertyInteger",
					 &error,
					 G_TYPE_STRING, key,
					 G_TYPE_INVALID,
					 G_TYPE_INT, &class_val,
					 G_TYPE_INVALID);
		if (class_val == SEAT_PCI_DEVICE_CLASS) {
			g_debug ("Found device: %s", devices [i]);
			seats = g_list_prepend (seats, devices [i]);
		}

		g_object_unref (device_proxy);
	}

	g_strfreev (devices);
}

gboolean
gdm_local_display_factory_start (GdmLocalDisplayFactory *factory,
				 GError                **error)
{
	gboolean    ret;
	GdmDisplay *display;

	g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

	ret = TRUE;

	/* FIXME: */

	display = gdm_static_display_new (0);
	if (display == NULL) {
		g_warning ("Unable to create display: %d", 0);
		return FALSE;
	}

	gdm_display_store_add (factory->priv->display_store, display);
	/* let store own the ref */
	g_object_unref (display);

	if (! gdm_display_manage (display)) {
		gdm_display_unmanage (display);
	}

	return ret;
}

gboolean
gdm_local_display_factory_stop (GdmLocalDisplayFactory *factory,
				GError                **error)
{
	g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

	return TRUE;
}

static void
gdm_local_display_factory_set_display_store (GdmLocalDisplayFactory *factory,
					     GdmDisplayStore        *display_store)
{
	if (factory->priv->display_store != NULL) {
		g_object_unref (factory->priv->display_store);
		factory->priv->display_store = NULL;
	}

	if (display_store != NULL) {
		factory->priv->display_store = g_object_ref (display_store);
	}
}

static void
gdm_local_display_factory_set_property (GObject	      *object,
					guint	       prop_id,
					const GValue  *value,
					GParamSpec    *pspec)
{
	GdmLocalDisplayFactory *self;

	self = GDM_LOCAL_DISPLAY_FACTORY (object);

	switch (prop_id) {
	case PROP_DISPLAY_STORE:
		gdm_local_display_factory_set_display_store (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_local_display_factory_get_property (GObject	   *object,
					guint  	    prop_id,
					GValue	   *value,
					GParamSpec *pspec)
{
	GdmLocalDisplayFactory *self;

	self = GDM_LOCAL_DISPLAY_FACTORY (object);

	switch (prop_id) {
	case PROP_DISPLAY_STORE:
		g_value_set_object (value, self->priv->display_store);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_local_display_factory_class_init (GdmLocalDisplayFactoryClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_local_display_factory_get_property;
	object_class->set_property = gdm_local_display_factory_set_property;
	object_class->finalize = gdm_local_display_factory_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_STORE,
                                         g_param_spec_object ("display-store",
							      "display store",
							      "display store",
							      GDM_TYPE_DISPLAY_STORE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (GdmLocalDisplayFactoryPrivate));
}

static gboolean
connect_to_hal (GdmLocalDisplayFactory *factory)
{
	GError *error;

	error = NULL;
	factory->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (factory->priv->connection == NULL) {
		g_critical ("Couldn't connect to system bus: %s",
			   error->message);
		g_error_free (error);

		return FALSE;
	}

        factory->priv->proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
							  HAL_DBUS_NAME,
							  HAL_DBUS_MANAGER_PATH,
							  HAL_DBUS_MANAGER_INTERFACE);
	if (factory->priv->proxy == NULL) {
		g_warning ("Couldn't create proxy for HAL Manager");
		return FALSE;
	}

	return TRUE;
}

static void
disconnect_from_hal (GdmLocalDisplayFactory *factory)
{
	if (factory->priv->proxy == NULL) {
		g_object_unref (factory->priv->proxy);
	}
}

static void
gdm_local_display_factory_init (GdmLocalDisplayFactory *factory)
{
	factory->priv = GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE (factory);

	connect_to_hal (factory);
}

static void
gdm_local_display_factory_finalize (GObject *object)
{
	GdmLocalDisplayFactory *factory;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (object));

	factory = GDM_LOCAL_DISPLAY_FACTORY (object);

	g_return_if_fail (factory->priv != NULL);

	disconnect_from_hal (factory);

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
