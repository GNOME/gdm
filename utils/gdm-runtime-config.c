/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Red Hat, Inc.
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

#include <locale.h>
#include <stdlib.h>
#include <sysexits.h>

#include <glib.h>

int
main (int argc, char *argv[])
{
        g_autoptr(GKeyFile) key_file = NULL;
        g_autoptr(GError) error = NULL;
        gchar *group, *key, *value;
        gboolean saved_okay;

        if (argc < 5 || g_strcmp0(argv[1], "set") != 0) {
                g_printerr("gdm-runtime-config: command format should be " \
                           "'gdm-runtime-config set <group> <key> <value>'\n" \
                           "For example, 'gdm-runtime-config set daemon WaylandEnable true'\n");
                return EX_USAGE;
        }

        group = argv[2];
        key = argv[3];
        value = argv[4];

        setlocale (LC_ALL, "");

        key_file = g_key_file_new ();

        /* Just load the runtime conf file and ignore the error.  A new file
         * will be created later if it is file not found.
         * So that more than one config item can be set.
         */
        g_key_file_load_from_file (key_file,
                                   GDM_RUNTIME_CONF,
                                   G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                   &error);
        g_clear_error (&error);

        g_key_file_set_value (key_file, group, key, value);

        g_mkdir_with_parents (GDM_RUN_DIR, 0711);

        saved_okay = g_key_file_save_to_file (key_file, GDM_RUNTIME_CONF, &error);

        if (!saved_okay) {
                g_printerr ("gdm-runtime-config: unable to set '%s' in '%s' group to '%s': %s\n",
                            key, group, value, error->message);
                return EX_CANTCREAT;
        }

        return EX_OK;
}
