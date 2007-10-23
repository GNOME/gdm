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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gdm-log.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-greeter-session.h"

int
main (int argc, char *argv[])
{
        GError            *error;
        GdmGreeterSession *session;
        gboolean           res;

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        g_type_init ();

        if (! gdm_settings_client_init (GDMCONFDIR "/gdm.schemas", "/")) {
                exit (1);
        }

        g_debug ("Greeter session for display %s xauthority:%s",
                 g_getenv ("DISPLAY"),
                 g_getenv ("XAUTHORITY"));

        gdm_log_init ();
        gdm_log_set_debug (TRUE);

        gdk_init (&argc, &argv);
        /*gdm_common_atspi_launch ();*/
        gtk_init (&argc, &argv);

        session = gdm_greeter_session_new ();
        if (session == NULL) {
                g_critical ("Unable to create greeter session");
                exit (1);
        }

        res = gdm_greeter_session_start (session, &error);
        if (! res) {
                g_warning ("Unable to start greeter session: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        gtk_main ();

        if (session != NULL) {
                g_object_unref (session);
        }

        return 0;
}
