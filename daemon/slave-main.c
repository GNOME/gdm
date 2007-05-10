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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-log.h"
#include "gdm-slave.h"

static DBusGConnection *
get_system_bus (void)
{
	GError		*error;
	DBusGConnection *bus;
	DBusConnection	*connection;

	error = NULL;
	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	connection = dbus_g_connection_get_connection (bus);
	dbus_connection_set_exit_on_disconnect (connection, FALSE);

 out:
	return bus;
}

static void
setup_signal_handlers (void)
{
	/* FIXME */
}

int
main (int    argc,
      char **argv)
{
	GMainLoop	*loop;
	GOptionContext	*context;
	DBusGConnection *connection;
	int		 ret;
	GdmSlave        *slave;
	static char	*display_id = NULL;
	static GOptionEntry entries []	 = {
		{ "display-id", 0, 0, G_OPTION_ARG_STRING, &display_id, N_("Display ID"), N_("id") },
		{ NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);
	setlocale (LC_ALL, "");

	ret = 1;

	g_type_init ();

	context = g_option_context_new (_("GNOME Display Manager Slave"));
	g_option_context_add_main_entries (context, entries, NULL);

	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	connection = get_system_bus ();
	if (connection == NULL) {
		goto out;
	}

	gdm_log_init ();

	gdm_log_set_debug (TRUE);

	if (display_id == NULL) {
		g_critical ("No display ID set");
		exit (1);
	}

	setup_signal_handlers ();

	slave = gdm_slave_new (display_id);

	if (slave == NULL) {
		goto out;
	}

	loop = g_main_loop_new (NULL, FALSE);

	gdm_slave_start (slave);

	g_main_loop_run (loop);

	if (slave != NULL) {
		g_object_unref (slave);
	}

	g_main_loop_unref (loop);

	ret = 0;

 out:

	return ret;
}
