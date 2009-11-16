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

#include "gdm-user-manager.h"
#include "gdm-settings-client.h"

static GdmUserManager *manager = NULL;

static void
on_users_loaded (GdmUserManager *manager,
                 gpointer        data)
{
        GSList *users;

        g_debug ("Users loaded");

        users = gdm_user_manager_list_users (manager);
        while (users != NULL) {
                g_print ("User: %s\n", gdm_user_get_user_name (users->data));
                users = g_slist_delete_link (users, users);
        }

}

static void
on_user_added (GdmUserManager *manager,
               GdmUser        *user,
               gpointer        data)
{
        g_debug ("User added: %s", gdm_user_get_user_name (user));
}

static void
on_user_removed (GdmUserManager *manager,
                 GdmUser        *user,
                 gpointer        data)
{
        g_debug ("User removed: %s", gdm_user_get_user_name (user));
}

int
main (int argc, char *argv[])
{

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        gtk_init (&argc, &argv);

        if (! gdm_settings_client_init (GDMCONFDIR "/gdm.schemas", "/")) {
                g_critical ("Unable to initialize settings client");
                exit (1);
        }

        manager = gdm_user_manager_ref_default ();
        g_signal_connect (manager,
                          "users-loaded",
                          G_CALLBACK (on_users_loaded),
                          NULL);
        g_signal_connect (manager,
                          "user-added",
                          G_CALLBACK (on_user_added),
                          NULL);
        g_signal_connect (manager,
                          "user-removed",
                          G_CALLBACK (on_user_removed),
                          NULL);

        gtk_main ();

        return 0;
}
