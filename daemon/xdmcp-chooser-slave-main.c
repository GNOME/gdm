/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-xerrors.h"
#include "gdm-log.h"
#include "gdm-common.h"
#include "gdm-xdmcp-chooser-slave.h"

#include "gdm-settings.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

static GdmSettings     *settings        = NULL;
static int              gdm_return_code = 0;

static GDBusConnection *
get_system_bus (void)
{
        GError          *error;
        GDBusConnection *bus;

        error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_dbus_connection_set_exit_on_close (bus, FALSE);

 out:
        return bus;
}

static void
on_slave_stopped (GdmSlave   *slave,
                  GMainLoop  *main_loop)
{
        g_debug ("slave finished");
        gdm_return_code = 0;
        g_main_loop_quit (main_loop);
}

static gboolean
on_shutdown_signal_cb (gpointer user_data)
{
        GMainLoop *mainloop = user_data;

        g_main_loop_quit (mainloop);

        return FALSE;
}

static gboolean
on_sigusr2_cb (gpointer user_data)
{
        g_debug ("Got USR2 signal");

        gdm_log_toggle_debug ();

        return TRUE;
}

static gboolean
is_debug_set (void)
{
        gboolean debug = FALSE;

        /* enable debugging for unstable builds */
        if (gdm_is_version_unstable ()) {
                return TRUE;
        }

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        return debug;
}

int
main (int    argc,
      char **argv)
{
        GMainLoop        *main_loop;
        GOptionContext   *context;
        GDBusConnection  *connection;
        GdmSlave         *slave;
        static char      *display_id = NULL;
        static GOptionEntry entries []   = {
                { "display-id", 0, 0, G_OPTION_ARG_STRING, &display_id, N_("Display ID"), N_("ID") },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        context = g_option_context_new (_("GNOME Display Manager Slave"));
        g_option_context_add_main_entries (context, entries, NULL);

        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        connection = get_system_bus ();
        if (connection == NULL) {
                goto out;
        }

        gdm_xerrors_init ();
        gdm_log_init ();

        settings = gdm_settings_new ();
        if (settings == NULL) {
                g_warning ("Unable to initialize settings");
                goto out;
        }

        if (! gdm_settings_direct_init (settings, DATADIR "/gdm/gdm.schemas", "/")) {
                g_warning ("Unable to initialize settings");
                goto out;
        }

        gdm_log_set_debug (is_debug_set ());

        if (display_id == NULL) {
                g_critical ("No display ID set");
                exit (1);
        }

        main_loop = g_main_loop_new (NULL, FALSE);

        g_unix_signal_add (SIGTERM, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGINT, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGUSR2, on_sigusr2_cb, NULL);

        slave = gdm_xdmcp_chooser_slave_new (display_id);
        if (slave == NULL) {
                goto out;
        }
        g_signal_connect (slave,
                          "stopped",
                          G_CALLBACK (on_slave_stopped),
                          main_loop);
        gdm_slave_start (slave);

        g_main_loop_run (main_loop);

        if (slave != NULL) {
                g_object_unref (slave);
        }

        g_main_loop_unref (main_loop);

 out:
        g_debug ("Slave finished");

        g_object_unref (connection);

        return gdm_return_code;
}
