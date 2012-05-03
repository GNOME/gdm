/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <gio/gio.h>

#include "gdm-settings.h"

#define GDM_DBUS_NAME "org.gnome.DisplayManager"

static GdmSettings     *settings      = NULL;

static GDBusConnection *
get_system_bus (void)
{
        GError          *error;
        GDBusConnection *bus;

        error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        g_dbus_connection_set_exit_on_close (bus, FALSE);

 out:
        return bus;
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
        settings = gdm_settings_new ();
        if (settings == NULL) {
                g_warning ("Unable to initialize settings");
                exit (1);
        }
}

int
main (int argc, char **argv)
{
        GMainLoop          *main_loop;
        GDBusConnection    *connection;

        g_type_init ();

        connection = get_system_bus ();
        if (connection == NULL) {
                goto out;
        }

        g_bus_own_name (G_BUS_TYPE_SYSTEM,
                        GDM_DBUS_NAME,
                        G_BUS_NAME_OWNER_FLAGS_NONE,
                        NULL, /* bus acquired */
                        on_name_acquired,
                        NULL, /* name lost */
                        NULL, NULL);

        main_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (main_loop);

        g_main_loop_unref (main_loop);

        g_object_unref (settings);

 out:
        return 0;
}
