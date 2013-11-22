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

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-session-worker.h"

#include "gdm-settings.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

static GdmSettings *settings = NULL;

static gboolean
on_shutdown_signal_cb (gpointer user_data)
{
        GMainLoop *mainloop = user_data;

        g_main_loop_quit (mainloop);

        return FALSE;
}

static gboolean
on_sigusr1_cb (gpointer user_data)
{
        g_debug ("Got USR1 signal");

        gdm_log_toggle_debug ();

        return TRUE;
}

static gboolean
is_debug_set (void)
{
        gboolean debug;
        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        return debug;
}

int
main (int    argc,
      char **argv)
{
        GMainLoop        *main_loop;
        GOptionContext   *context;
        GdmSessionWorker *worker;
        const char       *address;
        gboolean          is_for_reauth;
        static GOptionEntry entries []   = {
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        /* Translators: worker is a helper process that does the work
           of starting up a session */
        context = g_option_context_new (_("GNOME Display Manager Session Worker"));
        g_option_context_add_main_entries (context, entries, NULL);

        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        gdm_log_init ();

        settings = gdm_settings_new ();
        if (settings == NULL) {
                g_warning ("Unable to initialize settings");
                exit (1);
        }

        if (! gdm_settings_direct_init (settings, DATADIR "/gdm/gdm.schemas", "/")) {
                g_warning ("Unable to initialize settings");
                exit (1);
        }

        gdm_log_set_debug (is_debug_set ());

        address = g_getenv ("GDM_SESSION_DBUS_ADDRESS");
        if (address == NULL) {
                g_warning ("GDM_SESSION_DBUS_ADDRESS not set");
                exit (1);
        }

        is_for_reauth = g_getenv ("GDM_SESSION_FOR_REAUTH") != NULL;

        worker = gdm_session_worker_new (address, is_for_reauth);

        main_loop = g_main_loop_new (NULL, FALSE);

        g_unix_signal_add (SIGTERM, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGINT, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGUSR1, on_sigusr1_cb, NULL);

        g_main_loop_run (main_loop);

        if (worker != NULL) {
                g_object_unref (worker);
        }

        g_main_loop_unref (main_loop);

        g_debug ("Worker finished");

        return 0;
}
