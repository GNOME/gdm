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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include "gdm-dbus-util.h"
#include <string.h>

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

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
        g_autoptr(GDBusNodeInfo) bus_info = NULL;
        GDBusInterfaceVTable bus_vtable = { handle_bus_method };

        bus_info = g_dbus_node_info_new_for_xml (dbus_introspection,
                                                 NULL);

        g_debug ("GdmDBusServer: new connection %p", new_connection);

        g_dbus_connection_register_object (new_connection,
                                           "/org/freedesktop/DBus",
                                           bus_info->interfaces[0],
                                           &bus_vtable,
                                           NULL, NULL, NULL);

        /* We're not handling the signal */
        return FALSE;
}

GDBusServer *
gdm_dbus_setup_private_server (GDBusAuthObserver  *observer,
                               GError            **error)
{
        g_autofree char *guid = NULL;
        const char *client_address;
        g_autoptr(GDBusServer) server = NULL;

        /* Ensure the private bus directory exists */
        if (g_mkdir_with_parents (GDM_PRIVATE_DBUS_DIR, 0711) == -1) {
                g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno), g_strerror (errno));
                return NULL;
        }
        if (g_chmod (GDM_PRIVATE_DBUS_DIR, 0711) == -1) {
                g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno), g_strerror (errno));
                return NULL;
        }

        guid = g_dbus_generate_guid ();

        server = g_dbus_server_new_sync ("unix:dir=" GDM_PRIVATE_DBUS_DIR,
                                         G_DBUS_SERVER_FLAGS_NONE,
                                         guid,
                                         observer,
                                         NULL,
                                         error);

        client_address = g_dbus_server_get_client_address (server);

        if (g_str_has_prefix (client_address, "unix:path=")) {
                client_address += strlen("unix:path=");
                g_chmod (client_address, 0666);
        }

        g_signal_connect (server, "new-connection",
                          G_CALLBACK (handle_connection),
                          NULL);

        return g_steal_pointer (&server);
}

gboolean
gdm_dbus_get_pid_for_name (const char  *system_bus_name,
                           pid_t       *out_pid,
                           GError     **error)
{
        g_autoptr(GDBusConnection) bus = NULL;
        g_autoptr(GVariant) reply = NULL;
        unsigned int v;

        g_return_val_if_fail (system_bus_name != NULL, FALSE);
        g_return_val_if_fail (out_pid != NULL, FALSE);

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
        if (bus == NULL) {
                return FALSE;
        }

        reply = g_dbus_connection_call_sync (bus,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "GetConnectionUnixProcessID",
                                             g_variant_new ("(s)", system_bus_name),
                                             G_VARIANT_TYPE ("(u)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, error);
        if (reply == NULL) {
                return FALSE;
        }

        g_variant_get (reply, "(u)", &v);
        *out_pid = v;

        return TRUE;
}

gboolean
gdm_dbus_get_uid_for_name (const char  *system_bus_name,
                           uid_t       *out_uid,
                           GError     **error)
{
        g_autoptr(GDBusConnection) bus = NULL;
        g_autoptr(GVariant) reply = NULL;
        unsigned int v;

        g_return_val_if_fail (system_bus_name != NULL, FALSE);
        g_return_val_if_fail (out_uid != NULL, FALSE);

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
        if (bus == NULL) {
                return FALSE;
        }

        reply = g_dbus_connection_call_sync (bus,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "GetConnectionUnixUser",
                                             g_variant_new ("(s)", system_bus_name),
                                             G_VARIANT_TYPE ("(u)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, error);
        if (reply == NULL) {
                return FALSE;
        }

        g_variant_get (reply, "(u)", &v);
        *out_uid = v;

        return TRUE;
}

void
gdm_dbus_error_ensure (GQuark domain)
{
        /* The primary purpose of this function is to make sure the error quark
         * is registered internally with gdbus before any bus traffic occurs,
         * so we get remote errors mapped correctly to their local counterparts.
         * This error quark registration happens implicitly the first time the
         * quark is used.
         * Note that g_debug is never optimized away, only the output is suppressed.
         */
        g_debug ("GdmDBusUtils: Registered DBus error domain '%s'",
                 g_quark_to_string (domain));
}
