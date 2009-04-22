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
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-session-worker.h"

#define SERVER_DBUS_PATH      "/org/gnome/DisplayManager/SessionServer"
#define SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.SessionServer"

static gboolean
is_debug_set (gboolean arg)
{
        /* enable debugging for unstable builds */
        if (gdm_is_version_unstable ()) {
                return TRUE;
        }

        return arg;
}

int
main (int    argc,
      char **argv)
{
        GMainLoop        *main_loop;
        GOptionContext   *context;
        GdmSessionWorker *worker;
        const char       *address;
        static gboolean   debug      = FALSE;
        static GOptionEntry entries []   = {
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        g_type_init ();

        gdm_set_fatal_warnings_if_unstable ();

        context = g_option_context_new (_("GNOME Display Manager Session Worker"));
        g_option_context_add_main_entries (context, entries, NULL);

        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        gdm_log_init ();
        gdm_log_set_debug (is_debug_set (debug));

        address = g_getenv ("GDM_SESSION_DBUS_ADDRESS");
        if (address == NULL) {
                g_warning ("GDM_SESSION_DBUS_ADDRESS not set");
                exit (1);
        }

        worker = gdm_session_worker_new (address);

        main_loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (main_loop);

        if (worker != NULL) {
                g_object_unref (worker);
        }

        g_main_loop_unref (main_loop);


        g_debug ("Worker finished");

        return 0;
}
