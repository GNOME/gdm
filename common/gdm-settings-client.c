/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "gdm-settings-client.h"
#include "gdm-settings-utils.h"

#define SETTINGS_DBUS_NAME      "org.gnome.DisplayManager"
#define SETTINGS_DBUS_PATH      "/org/gnome/DisplayManager/Settings"
#define SETTINGS_DBUS_INTERFACE "org.gnome.DisplayManager.Settings"

static char            *schemas_file   = NULL;
static char            *schemas_root   = NULL;
static GHashTable      *schemas        = NULL;
static DBusGProxy      *settings_proxy = NULL;
static DBusGConnection *connection     = NULL;

static GdmSettingsEntry *
get_entry_for_key (const char *key)
{
	GdmSettingsEntry *entry;

	entry = g_hash_table_lookup (schemas, key);

	return entry;
}

static gboolean
get_value (const char *key,
	   char      **value)
{
	GError  *error;
	char    *str;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (settings_proxy,
				 "GetValue",
				 &error,
				 G_TYPE_STRING, key,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &str,
				 G_TYPE_INVALID);
	if (! res) {
		if (error != NULL) {
			g_warning ("Failed to get value for %s: %s", key, error->message);
			g_error_free (error);
		} else {
			g_warning ("Failed to get value for %s", key);
		}

		return FALSE;
	}

	if (value != NULL) {
		*value = g_strdup (str);
	}

	g_free (str);

	return TRUE;
}

static void
assert_signature (GdmSettingsEntry *entry,
		  const char       *signature)
{
	const char *sig;

	sig = gdm_settings_entry_get_signature (entry);

	g_assert (sig != NULL);
	g_assert (signature != NULL);
	g_assert (strcmp (signature, sig) == 0);
}

gboolean
gdm_settings_client_get_string (const char  *key,
				char       **value)
{
	GdmSettingsEntry *entry;
	gboolean          ret;
	gboolean          res;
	char             *str;

	g_return_val_if_fail (key != NULL, FALSE);

	entry = get_entry_for_key (key);
	g_assert (entry != NULL);

	assert_signature (entry, "s");

	ret = FALSE;

	res = get_value (key, &str);

	if (! res) {
		/* use the default */
		str = g_strdup (gdm_settings_entry_get_default_value (entry));
	}

	if (value != NULL) {
		*value = g_strdup (str);
	}

	g_free (str);

	return ret;
}

gboolean
gdm_settings_client_get_boolean (const char *key,
				 gboolean   *value)
{
	GdmSettingsEntry *entry;
	gboolean          ret;
	gboolean          res;
	char             *str;

	g_return_val_if_fail (key != NULL, FALSE);

	entry = get_entry_for_key (key);
	g_assert (entry != NULL);

	assert_signature (entry, "b");

	ret = FALSE;

	res = get_value (key, &str);

	if (! res) {
		/* use the default */
		str = g_strdup (gdm_settings_entry_get_default_value (entry));
	}

	ret = gdm_settings_parse_value_as_boolean  (str, value);

	g_free (str);

	return ret;
}

gboolean
gdm_settings_client_get_int (const char *key,
			     int        *value)
{
	GdmSettingsEntry *entry;
	gboolean          ret;
	gboolean          res;
	char             *str;

	g_return_val_if_fail (key != NULL, FALSE);

	entry = get_entry_for_key (key);
	g_assert (entry != NULL);

	assert_signature (entry, "i");

	ret = FALSE;

	res = get_value (key, &str);

	if (! res) {
		/* use the default */
		str = g_strdup (gdm_settings_entry_get_default_value (entry));
	}

	ret = gdm_settings_parse_value_as_integer (str, value);

	g_free (str);

	return ret;
}

gboolean
gdm_settings_client_set_int (const char *key,
			     int         value)
{
	g_return_val_if_fail (key != NULL, FALSE);
	return TRUE;
}

gboolean
gdm_settings_client_set_string (const char *key,
				const char *value)
{
	g_return_val_if_fail (key != NULL, FALSE);
	return TRUE;
}

gboolean
gdm_settings_client_set_boolean (const char *key,
				 gboolean    value)
{
	g_return_val_if_fail (key != NULL, FALSE);
	return TRUE;
}

static void
hashify_list (GdmSettingsEntry *entry,
	      gpointer          data)
{
	g_hash_table_insert (schemas, g_strdup (gdm_settings_entry_get_key (entry)), entry);
}

gboolean
gdm_settings_client_init (const char *file,
			  const char *root)
{
        GError  *error;
	GSList  *list;

	g_return_val_if_fail (file != NULL, FALSE);
	g_return_val_if_fail (root != NULL, FALSE);

	g_assert (schemas == NULL);

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                if (error != NULL) {
                        g_warning ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
		return FALSE;
        }

        settings_proxy = dbus_g_proxy_new_for_name (connection,
						    SETTINGS_DBUS_NAME,
						    SETTINGS_DBUS_PATH,
						    SETTINGS_DBUS_INTERFACE);
	if (settings_proxy == NULL) {
		g_warning ("Unable to connect to settings server");
		return FALSE;
	}

	list = NULL;
	if (! gdm_settings_parse_schemas (file, root, &list)) {
		g_warning ("Unable to parse schemas");
		return FALSE;
	}

	schemas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)gdm_settings_entry_free);
	g_slist_foreach (list, (GFunc)hashify_list, NULL);

	schemas_file = g_strdup (file);
	schemas_root = g_strdup (root);

	return TRUE;
}

void
gdm_settings_client_shutdown (void)
{

}
