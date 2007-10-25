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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gdm-settings-manager.h"
#include "gdm-common.h"

int
main (int argc, char *argv[])
{
        GdmSettingsManager *manager;
        gboolean            res;
        GError             *error;

        bindtextdomain (GETTEXT_PACKAGE, GDM_SETTINGS_LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        g_type_init ();

        gtk_init (&argc, &argv);

        manager = gdm_settings_manager_new ();

        res = gdm_settings_manager_start (manager, &error);
        if (! res) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                goto out;
        }

        gtk_main ();

 out:
        if (manager != NULL) {
                g_object_unref (manager);
        }

        return 0;
}
