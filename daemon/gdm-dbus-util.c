/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include "gdm-dbus-util.h"

/* a subset of org.freedesktop.DBus interface, to be used by internal servers */
static const char *dbus_introspection = 
"<node name=\"/org/freedesktop/DBus\">"
"  <interface name=\"org.freedesktop.DBus\">"
"    <method name=\"AddMatch\">"
"      <arg name=\"match_rule\" type=\"s\" direction=\"in\" />"
"    </method>"
"  </interface>"
"</node>";

static void
handle_bus_method (GDBusConnection       *connection,
		   const char            *sender,
		   const char            *object_path,
		   const char            *interface_name,
		   const char            *method_name,
		   GVariant              *parameters,
		   GDBusMethodInvocation *invocation,
		   gpointer               user_data)
{
        g_dbus_method_invocation_return_value (invocation, NULL);
}

static gboolean
handle_connection (GDBusServer      *server,
                   GDBusConnection  *new_connection,
                   gpointer          user_data)
{
        GDBusInterfaceVTable bus_vtable = { handle_bus_method };
        GDBusNodeInfo *bus_info;

        bus_info = g_dbus_node_info_new_for_xml (dbus_introspection,
                                                 NULL);

        g_debug ("GdmDBusServer: new connection %p", new_connection);

        g_dbus_connection_register_object (new_connection,
                                           "/org/freedesktop/DBus",
                                           bus_info->interfaces[0],
                                           &bus_vtable,
                                           NULL, NULL, NULL);
        g_dbus_node_info_unref (bus_info);

        /* We're not handling the signal */
        return FALSE;
}

/* Note: Use abstract sockets like dbus does by default on Linux. Abstract
 * sockets are only available on Linux.
 */
static char *
generate_address (void)
{
        char *path;
#if defined (__linux__)
        int   i;
        char  tmp[9];

        for (i = 0; i < 8; i++) {
                if (g_random_int_range (0, 2) == 0) {
                        tmp[i] = g_random_int_range ('a', 'z' + 1);
                } else {
                        tmp[i] = g_random_int_range ('A', 'Z' + 1);
                }
        }
        tmp[8] = '\0';

        path = g_strdup_printf ("unix:abstract=/tmp/gdm-greeter-%s", tmp);
#else
        path = g_strdup ("unix:tmpdir=/tmp");
#endif

        return path;
}

GDBusServer *
gdm_dbus_setup_private_server (GDBusAuthObserver  *observer,
                               GError            **error)
{
        char *address, *guid;
        GDBusServer *server;

        address = generate_address ();
        guid = g_dbus_generate_guid ();

        server = g_dbus_server_new_sync (address,
                                         G_DBUS_SERVER_FLAGS_NONE,
                                         guid,
                                         observer,
                                         NULL,
                                         error);

        g_signal_connect (server, "new-connection",
                          G_CALLBACK (handle_connection), NULL);

        g_free (address);
        g_free (guid);

        return server;
}
