/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * cc -o test-hal-seats `pkg-config --cflags --libs glib-2.0 dbus-glib-1` test-hal-seats.c
 */

#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define HAL_DBUS_NAME                           "org.freedesktop.Hal"
#define HAL_DBUS_MANAGER_PATH                   "/org/freedesktop/Hal/Manager"
#define HAL_DBUS_MANAGER_INTERFACE              "org.freedesktop.Hal.Manager"
#define HAL_DBUS_DEVICE_INTERFACE               "org.freedesktop.Hal.Device"
#define SEAT_PCI_DEVICE_CLASS                   3

static GMainLoop *loop;

static void
get_pci_seats (DBusGConnection *bus,
	       DBusGProxy      *proxy,
	       GList           *seats)
{
	char      **devices;
	const char *key;
	const char *value;
	GError     *error;
	gboolean    res;
	int         i;

	g_message ("Getting PCI seats");

	key = "info.bus";
	value = "pci";

	devices = NULL;
	error = NULL;
        res = dbus_g_proxy_call (proxy,
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

		device_proxy = dbus_g_proxy_new_for_name (bus,
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
			g_message ("Found device: %s", devices [i]);
			seats = g_list_prepend (seats, devices [i]);
		}

		g_object_unref (device_proxy);
	}

	g_strfreev (devices);
}

static void
list_seats (GList *seats)
{
	GList *l;
	for (l = seats; l != NULL; l = l->next) {
		g_message ("Found device: %s", l->data);
	}
}

static gboolean
test_hal_seats (void)
{
	GError		*error;
	DBusGConnection *bus;
        DBusGProxy      *proxy;
	GList           *seats;

	proxy = NULL;

	error = NULL;
	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

        proxy = dbus_g_proxy_new_for_name (bus,
                                           HAL_DBUS_NAME,
                                           HAL_DBUS_MANAGER_PATH,
                                           HAL_DBUS_MANAGER_INTERFACE);
	if (proxy == NULL) {
		g_warning ("Couldn't create proxy for HAL Manager");
		goto out;
	}

	seats = NULL;

	get_pci_seats (bus, proxy, seats);

	list_seats (seats);

 out:
	if (proxy != NULL) {
		g_object_unref (proxy);
	}

	return FALSE;
}

int
main (int   argc,
      char *argv[])
{
	g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

	g_type_init ();

        g_idle_add ((GSourceFunc)test_hal_seats, NULL);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return 0;
}
