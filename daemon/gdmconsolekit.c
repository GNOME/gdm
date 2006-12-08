/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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

#include <pwd.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "misc.h" /* for gdm_debug */
#include "gdmconsolekit.h"


#define CK_NAME              "org.freedesktop.ConsoleKit"
#define CK_PATH              "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE         "org.freedesktop.ConsoleKit"
#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define CK_TYPE_PARAMETER_STRUCT (dbus_g_type_get_struct ("GValueArray", \
							  G_TYPE_STRING, \
							  G_TYPE_VALUE, \
							  G_TYPE_INVALID))
#define CK_TYPE_PARAMETER_LIST (dbus_g_type_get_collection ("GPtrArray", \
							    CK_TYPE_PARAMETER_STRUCT))
static void
add_param_int (GPtrArray       *parameters,
	       const char      *key,
	       int              value)
{
	GValue val = { 0, };
	GValue param_val = { 0, };

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, value);
	g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
	g_value_take_boxed (&param_val,
			    dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));
	dbus_g_type_struct_set (&param_val,
				0, key,
				1, &val,
				G_MAXUINT);
	g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
}

static void
add_param_boolean (GPtrArray       *parameters,
		   const char      *key,
		   gboolean         value)
{
	GValue val = { 0, };
	GValue param_val = { 0, };

	g_value_init (&val, G_TYPE_BOOLEAN);
	g_value_set_boolean (&val, value);
	g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
	g_value_take_boxed (&param_val,
			    dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));
	dbus_g_type_struct_set (&param_val,
				0, key,
				1, &val,
				G_MAXUINT);
	g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
}

static void
add_param_string (GPtrArray       *parameters,
		  const char      *key,
		  const char      *value)
{
	GValue val = { 0, };
	GValue param_val = { 0, };

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, value);

	g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
	g_value_take_boxed (&param_val,
			    dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));

	dbus_g_type_struct_set (&param_val,
				0, key,
				1, &val,
				G_MAXUINT);
	g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
}

static gboolean
get_string (DBusGProxy *proxy,
	    const char *method,
	    char      **str)
{
	GError	*error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (proxy,
				 method,
				 &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, str,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("%s failed: %s", method, error->message);
		g_error_free (error);
	}

	return res;
}

void
unlock_ck_session (const char *user,
		   const char *x11_display)
{
	DBusGConnection *connection;
	DBusGProxy	*proxy;
	GError		*error;
	gboolean	 res;
	struct passwd	*pwent;
	GPtrArray	*sessions;
	int		 i;

	gdm_debug ("Unlocking ConsoleKit session for %s on %s", user, x11_display);

	error = NULL;
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (connection == NULL) {
		gdm_debug ("Failed to connect to the D-Bus daemon: %s", error->message);
		g_error_free (error);
		return;
	}

	proxy = dbus_g_proxy_new_for_name (connection,
					   CK_NAME,
					   CK_MANAGER_PATH,
					   CK_MANAGER_INTERFACE);
	if (proxy == NULL) {
		return;
	}

	pwent = getpwnam (user);

	error = NULL;
	res = dbus_g_proxy_call (proxy,
				 "GetSessionsForUser",
				 &error,
				 G_TYPE_UINT,
				 pwent->pw_uid,
				 G_TYPE_INVALID,
				 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
				 &sessions,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Failed to get list of sessions: %s", error->message);
		g_error_free (error);
		goto out;
	}

	for (i = 0; i < sessions->len; i++) {
		char	   *ssid;
		DBusGProxy *session_proxy;

		ssid = g_ptr_array_index (sessions, i);

		session_proxy = dbus_g_proxy_new_for_name (connection,
							   CK_NAME,
							   ssid,
							   CK_SESSION_INTERFACE);
		if (session_proxy != NULL) {
			char *xdisplay;

			get_string (session_proxy, "GetX11Display", &xdisplay);
			if (xdisplay != NULL
			    && x11_display != NULL
			    && strcmp (xdisplay, x11_display) == 0) {
				res = dbus_g_proxy_call (session_proxy,
							 "Unlock",
							 &error,
							 G_TYPE_INVALID,
							 G_TYPE_INVALID);
				if (! res) {
					g_warning ("Unable to unlock %s: %s", ssid, error->message);
					g_error_free (error);
				}
			}
		}

		g_object_unref (session_proxy);
		g_free (ssid);
	}

	g_ptr_array_free (sessions, TRUE);

 out:
	g_object_unref (proxy);
}

char *
open_ck_session (struct passwd *pwent,
		 GdmDisplay    *d,
		 const char    *session)
{
	DBusGConnection *connection;
	DBusGProxy	*proxy;
	GError		*error;
	gboolean	 res;
	char		*cookie;
	GPtrArray	*parameters;

	cookie = NULL;

	gdm_debug ("Opening ConsoleKit session for %s", pwent->pw_name);

	error = NULL;
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (connection == NULL) {
		gdm_debug ("Failed to connect to the D-Bus daemon: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	proxy = dbus_g_proxy_new_for_name (connection,
					   CK_NAME,
					   CK_MANAGER_PATH,
					   CK_MANAGER_INTERFACE);
	if (proxy == NULL) {
		return NULL;
	}

	parameters = g_ptr_array_sized_new (10);

	add_param_int (parameters, "user", pwent->pw_uid);
	add_param_string (parameters, "x11-display", d->name);
	add_param_string (parameters, "host-name", d->hostname);
	add_param_boolean (parameters, "is-local", d->attached);

	/* FIXME: this isn't really a reliable value to use */
	add_param_string (parameters, "session-type", session);

	if (d->vt > 0) {
		char *device;

		/* FIXME: how does xorg construct this */
		device = g_strdup_printf ("/dev/tty%d", d->vt);
		add_param_string (parameters, "display-device", device);
		g_free (device);
	}

	error = NULL;
	res = dbus_g_proxy_call (proxy,
				 "OpenSessionWithParameters",
				 &error,
				 CK_TYPE_PARAMETER_LIST,
				 parameters,
				 G_TYPE_INVALID,
				 G_TYPE_STRING,
				 &cookie,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("OpenSession failed: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (proxy);

	g_ptr_array_free (parameters, TRUE);

	return cookie;
}

void
close_ck_session (const char *cookie)
{
	DBusGConnection *connection;
	DBusGProxy	*proxy;
	GError		*error;
	gboolean	 res;

	g_return_if_fail (cookie != NULL);

	error = NULL;
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (connection == NULL) {
		gdm_debug ("Failed to connect to the D-Bus daemon: %s", error->message);
		g_error_free (error);
		return;
	}

	proxy = dbus_g_proxy_new_for_name (connection,
					   CK_NAME,
					   CK_MANAGER_PATH,
					   CK_MANAGER_INTERFACE);
	if (proxy == NULL) {
		return;
	}

	error = NULL;
	res = dbus_g_proxy_call (proxy,
				 "CloseSession",
				 &error,
				 G_TYPE_STRING,
				 &cookie,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("CloseSession failed: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (proxy);
}
