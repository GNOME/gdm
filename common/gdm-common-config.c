/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <syslog.h>

#include <glib.h>

#include "gdm-common-config.h"

gboolean
gdm_common_config_parse_key_string (const char *keystring,
				    char      **group,
				    char      **key,
				    char      **locale,
				    char      **value)
{
	char   **split1;
	char   **split2;
	char    *g;
	char    *k;
	char    *l;
	char    *v;
	char    *tmp1;
	char    *tmp2;
	gboolean ret;

	ret = FALSE;
	g = k = v = l = NULL;
	split1 = split2 = NULL;

	split1 = g_strsplit (keystring, "/", 2);
	if (split1 == NULL) {
		goto out;
	}

	g = split1 [0];

	split2 = g_strsplit (split1 [1], "=", 2);
	if (split2 == NULL) {
		k = split1 [1];
	} else {
		k = split2 [0];
		v = split2 [1];
	}

	/* trim off the locale */
	tmp1 = strchr (k, '[');
	tmp2 = strchr (k, ']');
	if (tmp1 != NULL && tmp2 != NULL && tmp2 > tmp1) {
		l = g_strndup (tmp1 + 1, tmp2 - tmp1 - 1);
		*tmp1 = '\0';
	}

	ret = TRUE;
 out:
	if (group != NULL) {
		*group = g_strdup (g);
	}
	if (key != NULL) {
		*key = g_strdup (k);
	}
	if (locale != NULL) {
		*locale = g_strdup (l);
	}
	if (value != NULL) {
		*value = g_strdup (v);
	}

	g_strfreev (split1);
	g_strfreev (split2);

	return ret;
}

GKeyFile *
gdm_common_config_load (const char *filename,
			GError    **error)
{
	GKeyFile *config;
	GError   *local_error;
	gboolean  res;

	config = g_key_file_new ();

	local_error = NULL;
	res = g_key_file_load_from_file (config,
					 filename,
					 G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
					 &local_error);
	if (! res) {
		g_propagate_error (error, local_error);
		g_key_file_free (config);
		return NULL;
	}

	return config;
}

GKeyFile *
gdm_common_config_load_from_dirs (const char  *filename,
				  const char **dirs,
				  GError     **error)
{
	GKeyFile *config;
	int       i;

	config = NULL;

	/* FIXME: hope to have g_key_file_load_from_dirs
	   see GNOME bug #355334
	 */

	for (i = 0; dirs[i] != NULL; i++) {
		char *path;

		path = g_build_filename (dirs[i], filename, NULL);
		config = gdm_common_config_load (path, NULL);
		g_free (path);
		if (config != NULL) {
			break;
		}
	}

	if (config == NULL) {
		g_set_error (error,
			     G_KEY_FILE_ERROR,
			     G_KEY_FILE_ERROR_NOT_FOUND,
			     "Unable to find file in specified directories");
	}

	return config;
}

gboolean
gdm_common_config_save (GKeyFile   *config,
			const char *filename,
			GError    **error)
{
	GError   *local_error;
	gboolean  res;
	char     *contents;
	gsize     length;

	local_error = NULL;
	contents = g_key_file_to_data (config, &length, &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	local_error = NULL;
	res = g_file_set_contents (filename,
				   contents,
				   length,
				   &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		g_free (contents);
		return FALSE;
	}

	g_free (contents);
	return TRUE;
}

gboolean
gdm_common_config_get_int (GKeyFile   *config,
			   const char *keystring,
			   int        *value,
			   GError    **error)
{
	char   *group;
	char   *key;
	char   *default_value;
	int     val;
	GError *local_error;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value))
		return FALSE;

	local_error = NULL;
	val = g_key_file_get_integer (config,
				      group,
				      key,
				      &local_error);
	if (local_error != NULL) {
		/* use the default */
		if (default_value != NULL) {
			val = atoi (default_value);
		} else {
			val = 0;
		}
		g_propagate_error (error, local_error);
	}

	*value = val;

	g_free (key);
	g_free (group);
	g_free (default_value);

	return TRUE;
}

gboolean
gdm_common_config_get_translated_string (GKeyFile   *config,
					 const char *keystring,
					 char      **value,
					 GError    **error)
{
	char   *group;
	char   *key;
	char   *default_value;
	char   *val;
	const char * const *langs;
	int     i;

	val = NULL;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value))
		return FALSE;

	langs = g_get_language_names ();

	for (i = 0; langs[i] != NULL; i++) {
		const char *locale;
		locale = langs[i];

		val = g_key_file_get_locale_string (config,
						    group,
						    key,
						    locale,
                                                    NULL);
		if (val != NULL) {
			break;
		}
	}

	if (val == NULL) {
		/* use the default */
		val = g_strdup (default_value);
	}

	*value = val;

	g_free (key);
	g_free (group);
	g_free (default_value);

	return TRUE;
}

gboolean
gdm_common_config_get_string (GKeyFile   *config,
			      const char *keystring,
			      char      **value,
			      GError    **error)
{
	char   *group;
	char   *key;
	char   *default_value;
	char   *val;
	GError *local_error;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		g_set_error (error,
			     G_KEY_FILE_ERROR,
			     G_KEY_FILE_ERROR_PARSE,
			     "Unable to parse key: %s",
			     keystring);
		return FALSE;
	}

	local_error = NULL;
	val = g_key_file_get_string (config,
				     group,
				     key,
				     &local_error);
	if (local_error != NULL) {
		/* use the default */
		val = g_strdup (default_value);
		g_propagate_error (error, local_error);
	}

	*value = val;

	g_free (key);
	g_free (group);
	g_free (default_value);

	return TRUE;
}

gboolean
gdm_common_config_get_string_list (GKeyFile   *config,
				   const char *keystring,
				   char     ***value,
				   gsize      *length,
				   GError    **error)
{
	char   *group;
	char   *key;
	char   *default_value;
	char  **val;
	GError *local_error;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		g_set_error (error,
			     G_KEY_FILE_ERROR,
			     G_KEY_FILE_ERROR_PARSE,
			     "Unable to parse key: %s",
			     keystring);
		return FALSE;
	}

	local_error = NULL;
	val = g_key_file_get_string_list (config,
					  group,
					  key,
					  length,
					  &local_error);
	if (local_error != NULL) {
		/* use the default */
		val = g_strsplit (default_value, ";", -1);
		g_propagate_error (error, local_error);
	}

	*value = val;

	g_free (key);
	g_free (group);
	g_free (default_value);

	return TRUE;
}

gboolean
gdm_common_config_get_boolean (GKeyFile   *config,
			       const char *keystring,
			       gboolean   *value,
			       GError    **error)
{
	char    *group;
	char    *key;
	char    *default_value;
	gboolean val;
	GError  *local_error;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value))
		return FALSE;

	local_error = NULL;
	val = g_key_file_get_boolean (config,
				      group,
				      key,
				      &local_error);
	if (local_error != NULL) {
		/* use the default */
		if (default_value != NULL &&
		    (default_value[0] == 'T' ||
		     default_value[0] == 't' ||
		     default_value[0] == 'Y' ||
		     default_value[0] == 'y' ||
		     atoi (default_value) != 0)) {
			val = TRUE;
		} else {
			val = FALSE;
		}
		g_propagate_error (error, local_error);
	}

	*value = val;

	g_free (key);
	g_free (group);
	g_free (default_value);

	return TRUE;
}

void
gdm_common_config_set_string (GKeyFile   *config,
			      const char *keystring,
			      const char *value)
{
	char    *group;
	char    *key;
	char    *default_value;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		return;
	}

	g_key_file_set_string (config, group, key, value);

	g_free (key);
	g_free (group);
	g_free (default_value);
}

void
gdm_common_config_set_boolean (GKeyFile   *config,
			       const char *keystring,
			       gboolean    value)
{
	char    *group;
	char    *key;
	char    *default_value;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		return;
	}

	g_key_file_set_boolean (config, group, key, value);

	g_free (key);
	g_free (group);
	g_free (default_value);
}

void
gdm_common_config_set_int (GKeyFile   *config,
			   const char *keystring,
			   int         value)
{
	char    *group;
	char    *key;
	char    *default_value;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		return;
	}

	g_key_file_set_integer (config, group, key, value);

	g_free (key);
	g_free (group);
	g_free (default_value);
}

void
gdm_common_config_remove_key (GKeyFile   *config,
			      const char *keystring,
			      GError    **error)
{
	char    *group;
	char    *key;
	char    *default_value;
	GError  *local_error;

	group = key = default_value = NULL;
	if (! gdm_common_config_parse_key_string (keystring, &group, &key, NULL, &default_value)) {
		return;
	}

	local_error = NULL;
	g_key_file_remove_key (config, group, key, &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
	}

	g_free (key);
	g_free (group);
	g_free (default_value);
}
