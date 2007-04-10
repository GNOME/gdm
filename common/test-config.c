/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>

#include "gdm-common.h"

#include "../daemon/gdm-daemon-config-entries.h"

static const char *
source_to_name (GdmConfigSourceType source)
{
        const char *name;

        switch (source) {
        case GDM_CONFIG_SOURCE_DEFAULT:
                name = "default";
                break;
        case GDM_CONFIG_SOURCE_MANDATORY:
                name = "mandatory";
                break;
        case GDM_CONFIG_SOURCE_CUSTOM:
                name = "custom";
                break;
        case GDM_CONFIG_SOURCE_BUILT_IN:
                name = "built-in";
                break;
        case GDM_CONFIG_SOURCE_RUNTIME_USER:
                name = "runtime-user";
                break;
        case GDM_CONFIG_SOURCE_INVALID:
                name = "Invalid";
                break;
        default:
                name = "Unknown";
                break;
        }

        return name;
}

static const char *
type_to_name (GdmConfigValueType type)
{
        const char *name;

        switch (type) {
        case GDM_CONFIG_VALUE_INT:
                name = "int";
                break;
        case GDM_CONFIG_VALUE_BOOL:
                name = "boolean";
                break;
        case GDM_CONFIG_VALUE_STRING:
                name = "string";
                break;
        case GDM_CONFIG_VALUE_LOCALE_STRING:
                name = "locale-string";
                break;
        case GDM_CONFIG_VALUE_STRING_ARRAY:
                name = "string-array";
                break;
        case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
                name = "locale-string-array";
                break;
        case GDM_CONFIG_VALUE_INVALID:
                name = "invalid";
                break;
        default:
                name = "unknown";
                break;
        }

        return name;
}

static gboolean
notify_cb (GdmConfig          *config,
           GdmConfigSourceType source,
           const char         *group,
           const char         *key,
           GdmConfigValue     *value,
           int                 id,
           gpointer            data)
{
        char *str;

        if (value == NULL) {
                return FALSE;
        }

        str = gdm_config_value_to_string (value);

        g_print ("SOURCE=%s GROUP=%s KEY=%s ID=%d TYPE=%s VALUE=%s\n", source_to_name (source), group, key, id, type_to_name (value->type), str);
        if (strcmp (group, GDM_CONFIG_GROUP_CUSTOM_CMD) == 0 &&
            g_str_has_prefix (key, "CustomCommand") &&
            strlen (key) == 14) {
                g_message ("NOTIFY: Custom command");
        }

        g_free (str);
        return TRUE;
}

static gboolean
validate_cb (GdmConfig          *config,
             GdmConfigSourceType source,
             const char         *group,
             const char         *key,
             GdmConfigValue     *value,
             int                 id,
             gpointer            data)
{
        /* Here you can do validation or override the values */

        switch (id) {
        case GDM_ID_SOUND_PROGRAM:
                gdm_config_value_set_string (value, "NONE");
                break;
        case GDM_ID_NONE:
        default:
                /* doesn't have an ID : match group/key */
                break;
        }

        return TRUE;
}

static void
load_servers_group (GdmConfig *config)
{
        char     **keys;
        gsize      len;
        int        i;

        keys = gdm_config_get_keys_for_group (config, GDM_CONFIG_GROUP_SERVERS, &len, NULL);
        g_message ("Got %d keys for group %s", len, GDM_CONFIG_GROUP_SERVERS);

        /* now construct entries for these groups */
        for (i = 0; i < len; i++) {
                GdmConfigEntry  entry;
                GdmConfigValue *value;
                char           *new_group;
                gboolean        res;
                int             j;

                entry.group = GDM_CONFIG_GROUP_SERVERS;
                entry.key = keys[i];
                entry.type = GDM_CONFIG_VALUE_STRING;
                entry.default_value = NULL;
                entry.id = GDM_CONFIG_INVALID_ID;

                gdm_config_add_entry (config, &entry);
                gdm_config_process_entry (config, &entry, NULL);

                res = gdm_config_get_value (config, entry.group, entry.key, &value);
                if (! res) {
                        continue;
                }

                new_group = g_strdup_printf ("server-%s", gdm_config_value_get_string (value));
                gdm_config_value_free (value);

                for (j = 0; j < G_N_ELEMENTS (gdm_daemon_server_config_entries); j++) {
                        GdmConfigEntry *srv_entry;
                        if (gdm_daemon_server_config_entries[j].key == NULL) {
                                continue;
                        }
                        srv_entry = gdm_config_entry_copy (&gdm_daemon_server_config_entries[j]);
                        g_free (srv_entry->group);
                        srv_entry->group = g_strdup (new_group);
                        gdm_config_process_entry (config, srv_entry, NULL);
                        gdm_config_entry_free (srv_entry);
                }
                g_free (new_group);
        }
}

static void
test_config (void)
{
        GdmConfig *config;
        GError    *error;
        int        i;

        config = gdm_config_new ();

        gdm_config_set_notify_func (config, notify_cb, NULL);
        gdm_config_set_validate_func (config, validate_cb, NULL);

        gdm_config_add_static_entries (config, gdm_daemon_config_entries);

        /* At first try loading with only defaults */
        gdm_config_set_default_file (config, DATADIR "/gdm/defaults.conf");

        g_message ("Loading configuration: Default source only");

        /* load the data files */
        error = NULL;
        gdm_config_load (config, &error);
        if (error != NULL) {
                g_warning ("Unable to load configuration: %s", error->message);
                g_error_free (error);
        } else {
                /* populate the database with all specified entries */
                gdm_config_process_all (config, &error);
        }

        g_message ("Getting all standard values");
        /* now test retrieving these values */
        for (i = 0; gdm_daemon_config_entries [i].group != NULL; i++) {
                GdmConfigValue       *value;
                const GdmConfigEntry *entry;
                gboolean              res;
                char                 *str;

                entry = &gdm_daemon_config_entries [i];

                res = gdm_config_get_value (config, entry->group, entry->key, &value);
                if (! res) {
                        g_warning ("Unable to lookup entry g=%s k=%s", entry->group, entry->key);
                        continue;
                }

                str = gdm_config_value_to_string (value);

                g_print ("Got g=%s k=%s: %s\n", entry->group, entry->key, str);

                g_free (str);
                gdm_config_value_free (value);
        }

        g_message ("Setting values");
        /* now test setting a few values */
        {
                GdmConfigValue *value;
                value = gdm_config_value_new_from_string  (GDM_CONFIG_VALUE_BOOL, "false", NULL);
                gdm_config_set_value (config, "greeter", "ShowLastSession", value);
                /* should only see one notification */
                gdm_config_set_value (config, "greeter", "ShowLastSession", value);
                gdm_config_value_free (value);
        }

        g_message ("Loading the server entries");
        load_servers_group (config);

        g_message ("Loading configuration: Default and Custom sources");
        /* Now try adding a custom config */
        gdm_config_set_custom_file (config, GDMCONFDIR "/custom.conf");
        /* load the data files */
        error = NULL;
        gdm_config_load (config, &error);
        if (error != NULL) {
                g_warning ("Unable to load configuration: %s", error->message);
                g_error_free (error);
        } else {
                /* populate the database with all specified entries */
                gdm_config_process_all (config, &error);
        }


        /* Test translated keys */

        gdm_config_free (config);
}

int
main (int argc, char **argv)
{

        test_config ();

	return 0;
}
