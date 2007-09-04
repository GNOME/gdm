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
#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-host-chooser-dialog.h"

int
main (int argc, char *argv[])
{
        GtkWidget        *chooser;

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        g_type_init ();

        g_debug ("Chooser for display %s xauthority:%s", g_getenv ("DISPLAY"), g_getenv ("XAUTHORITY"));

        /*
         * gdm_common_atspi_launch () needs gdk initialized.
         * We cannot start gtk before the registry is running
         * because the atk-bridge will crash.
         */
        gdk_init (&argc, &argv);
        /*gdm_common_atspi_launch ();*/
        gtk_init (&argc, &argv);

        chooser = gdm_host_chooser_dialog_new ();
        if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_OK) {
                char *hostname;

                hostname = gdm_host_chooser_dialog_get_current_hostname (GDM_HOST_CHOOSER_DIALOG (chooser));
                g_message ("Hostname: %s", hostname);
                g_free (hostname);
        }

        gtk_widget_destroy (chooser);

        return 0;
}
