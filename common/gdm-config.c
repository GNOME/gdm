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
#include <errno.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "gdm-config.h"

struct _GdmConfig
{
	char            *mandatory_filename;
	char            *default_filename;
	char            *custom_filename;

	gboolean         mandatory_loaded;
	gboolean         default_loaded;
	gboolean         custom_loaded;

	GKeyFile        *mandatory_key_file;
	GKeyFile        *default_key_file;
	GKeyFile        *custom_key_file;

	time_t           mandatory_mtime;
	time_t           default_mtime;
	time_t           custom_mtime;

	GPtrArray       *entries;

	GHashTable      *value_hash;

	GdmConfigFunc    validate_func;
	gpointer         validate_func_data;
	GdmConfigFunc    notify_func;
	gpointer         notify_func_data;
};


typedef struct _GdmConfigRealValue
{
	GdmConfigValueType type;
	union {
		gboolean bool;
		int      integer;
		char    *str;
		char   **array;
	} val;
} GdmConfigRealValue;

#define REAL_VALUE(x) ((GdmConfigRealValue *)(x))

GQuark
gdm_config_error_quark (void)
{
	return g_quark_from_static_string ("gdm-config-error-quark");
}

GdmConfigEntry *
gdm_config_entry_copy (const GdmConfigEntry *src)
{
	GdmConfigEntry *dest;

	dest = g_new0 (GdmConfigEntry, 1);
	dest->group = g_strdup (src->group);
	dest->key = g_strdup (src->key);
	dest->default_value = g_strdup (src->default_value);
	dest->type = src->type;
	dest->id = src->id;

	return dest;
}

void
gdm_config_entry_free (GdmConfigEntry *entry)
{
	g_free (entry->group);
	g_free (entry->key);
	g_free (entry->default_value);

	g_free (entry);
}

GdmConfigValue *
gdm_config_value_new (GdmConfigValueType type)
{
	GdmConfigValue *value;

	g_return_val_if_fail (type != GDM_CONFIG_VALUE_INVALID, NULL);

	value = (GdmConfigValue *) g_slice_new0 (GdmConfigRealValue);
	value->type = type;

	return value;
}

void
gdm_config_value_free (GdmConfigValue *value)
{
	GdmConfigRealValue *real;

	real = REAL_VALUE (value);

	switch (real->type) {
        case GDM_CONFIG_VALUE_INVALID:
        case GDM_CONFIG_VALUE_BOOL:
        case GDM_CONFIG_VALUE_INT:
		break;
        case GDM_CONFIG_VALUE_STRING:
        case GDM_CONFIG_VALUE_LOCALE_STRING:
		g_free (real->val.str);
		break;
	case GDM_CONFIG_VALUE_STRING_ARRAY:
	case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
		g_strfreev (real->val.array);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_slice_free (GdmConfigRealValue, real);
}

static void
set_string (char      **dest,
	    const char *src)
{
	if (*dest != NULL) {
		g_free (*dest);
	}

	*dest = src ? g_strdup (src) : NULL;
}

static void
set_string_array (char      ***dest,
		  const char **src)
{
	if (*dest != NULL) {
		g_strfreev (*dest);
	}

	*dest = src ? g_strdupv ((char **)src) : NULL;
}

GdmConfigValue *
gdm_config_value_copy (const GdmConfigValue *src)
{
	GdmConfigRealValue *dest;
	GdmConfigRealValue *real;

	g_return_val_if_fail (src != NULL, NULL);

	real = REAL_VALUE (src);
	dest = REAL_VALUE (gdm_config_value_new (src->type));

	switch (real->type) {
	case GDM_CONFIG_VALUE_INT:
	case GDM_CONFIG_VALUE_BOOL:
	case GDM_CONFIG_VALUE_INVALID:
		dest->val = real->val;
		break;
	case GDM_CONFIG_VALUE_STRING:
	case GDM_CONFIG_VALUE_LOCALE_STRING:
		set_string (&dest->val.str, real->val.str);
		break;
	case GDM_CONFIG_VALUE_STRING_ARRAY:
	case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
		set_string_array (&dest->val.array, (const char **)real->val.array);
		break;
	default:
		g_assert_not_reached();
	}

	return (GdmConfigValue *) dest;
}

const char *
gdm_config_value_get_string (const GdmConfigValue *value)
{
	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (value->type == GDM_CONFIG_VALUE_STRING, NULL);
	return REAL_VALUE (value)->val.str;
}

const char *
gdm_config_value_get_locale_string (const GdmConfigValue *value)
{
	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (value->type == GDM_CONFIG_VALUE_LOCALE_STRING, NULL);
	return REAL_VALUE (value)->val.str;
}

gboolean
gdm_config_value_get_bool (const GdmConfigValue *value)
{
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (value->type == GDM_CONFIG_VALUE_BOOL, FALSE);
	return REAL_VALUE (value)->val.bool;
}

int
gdm_config_value_get_int (const GdmConfigValue *value)
{
	g_return_val_if_fail (value != NULL, 0);
	g_return_val_if_fail (value->type == GDM_CONFIG_VALUE_INT, 0);
	return REAL_VALUE (value)->val.integer;
}

static gint
safe_strcmp (const char *a,
	     const char *b)
{
	return strcmp (a ? a : "", b ? b : "");
}

/* based on code from gconf */
int
gdm_config_value_compare (const GdmConfigValue *value_a,
			  const GdmConfigValue *value_b)
{
	g_return_val_if_fail (value_a != NULL, 0);
	g_return_val_if_fail (value_b != NULL, 0);

	if (value_a->type < value_b->type) {
		return -1;
	} else if (value_a->type > value_b->type) {
		return 1;
	}

	switch (value_a->type) {
	case GDM_CONFIG_VALUE_INT:
		if (gdm_config_value_get_int (value_a) < gdm_config_value_get_int (value_b)) {
			return -1;
		} else if (gdm_config_value_get_int (value_a) > gdm_config_value_get_int (value_b)) {
			return 1;
		} else {
			return 0;
		}
	case GDM_CONFIG_VALUE_STRING:
		return safe_strcmp (gdm_config_value_get_string (value_a),
				    gdm_config_value_get_string (value_b));
	case GDM_CONFIG_VALUE_LOCALE_STRING:
		return safe_strcmp (gdm_config_value_get_locale_string (value_a),
				    gdm_config_value_get_locale_string (value_b));
	case GDM_CONFIG_VALUE_STRING_ARRAY:
	case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
		{
			char *str_a;
			char *str_b;
			int   res;

			str_a = gdm_config_value_to_string (value_a);
			str_b = gdm_config_value_to_string (value_a);
			res = safe_strcmp (str_a, str_b);
			g_free (str_a);
			g_free (str_b);

			return res;
		}
	case GDM_CONFIG_VALUE_BOOL:
		if (gdm_config_value_get_bool (value_a) == gdm_config_value_get_bool (value_b)) {
			return 0;
		} else if (gdm_config_value_get_bool (value_a)) {
			return 1;
		} else {
			return -1;
		}
	case GDM_CONFIG_VALUE_INVALID:
	default:
		g_assert_not_reached ();
		break;
	}

	return 0;
}

/* based on code from gconf */
GdmConfigValue *
gdm_config_value_new_from_string (GdmConfigValueType type,
				  const char        *value_str,
				  GError           **error)
{
	GdmConfigValue *value;

	g_return_val_if_fail (type != GDM_CONFIG_VALUE_INVALID, NULL);
	g_return_val_if_fail (value_str != NULL, NULL);

	value = gdm_config_value_new (type);

        switch (value->type) {
        case GDM_CONFIG_VALUE_INT:
		{
			char* endptr = NULL;
			glong result;

			errno = 0;
			result = strtol (value_str, &endptr, 10);
			if (endptr == value_str) {
				g_set_error (error,
					     GDM_CONFIG_ERROR,
					     GDM_CONFIG_ERROR_PARSE_ERROR,
					     _("Didn't understand `%s' (expected integer)"),
					     value_str);
				gdm_config_value_free (value);
				value = NULL;
			} else if (errno == ERANGE) {
				g_set_error (error,
					     GDM_CONFIG_ERROR,
					     GDM_CONFIG_ERROR_PARSE_ERROR,
					     _("Integer `%s' is too large or small"),
					     value_str);
				gdm_config_value_free (value);
				value = NULL;
			} else {
				gdm_config_value_set_int (value, result);
			}
		}
                break;
        case GDM_CONFIG_VALUE_BOOL:
		switch (*value_str) {
		case 't':
		case 'T':
		case '1':
		case 'y':
		case 'Y':
			gdm_config_value_set_bool (value, TRUE);
			break;

		case 'f':
		case 'F':
		case '0':
		case 'n':
		case 'N':
			gdm_config_value_set_bool (value, FALSE);
			break;
		default:
			g_set_error (error,
				     GDM_CONFIG_ERROR,
				     GDM_CONFIG_ERROR_PARSE_ERROR,
				     _("Didn't understand `%s' (expected true or false)"),
				     value_str);
			gdm_config_value_free (value);
			value = NULL;
			break;
		}
		break;
        case GDM_CONFIG_VALUE_STRING:
		if (! g_utf8_validate (value_str, -1, NULL)) {
			g_set_error (error,
				     GDM_CONFIG_ERROR,
				     GDM_CONFIG_ERROR_PARSE_ERROR,
				     _("Text contains invalid UTF-8"));
			gdm_config_value_free (value);
			value = NULL;
		} else {
			gdm_config_value_set_string (value, value_str);
		}
                break;
        case GDM_CONFIG_VALUE_LOCALE_STRING:
		if (! g_utf8_validate (value_str, -1, NULL)) {
			g_set_error (error,
				     GDM_CONFIG_ERROR,
				     GDM_CONFIG_ERROR_PARSE_ERROR,
				     _("Text contains invalid UTF-8"));
			gdm_config_value_free (value);
			value = NULL;
		} else {
			gdm_config_value_set_locale_string (value, value_str);
		}
		break;
        case GDM_CONFIG_VALUE_STRING_ARRAY:
		if (! g_utf8_validate (value_str, -1, NULL)) {
			g_set_error (error,
				     GDM_CONFIG_ERROR,
				     GDM_CONFIG_ERROR_PARSE_ERROR,
				     _("Text contains invalid UTF-8"));
			gdm_config_value_free (value);
			value = NULL;
		} else {
			char **split;
			split = g_strsplit (value_str, ";", -1);
			gdm_config_value_set_string_array (value, (const char **)split);
			g_strfreev (split);
		}
                break;
        case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
		if (! g_utf8_validate (value_str, -1, NULL)) {
			g_set_error (error,
				     GDM_CONFIG_ERROR,
				     GDM_CONFIG_ERROR_PARSE_ERROR,
				     _("Text contains invalid UTF-8"));
			gdm_config_value_free (value);
			value = NULL;
		} else {
			char **split;
			split = g_strsplit (value_str, ";", -1);
			gdm_config_value_set_locale_string_array (value, (const char **)split);
			g_strfreev (split);
		}
                break;
        case GDM_CONFIG_VALUE_INVALID:
        default:
		g_assert_not_reached ();
                break;
        }

	return value;
}

void
gdm_config_value_set_string_array (GdmConfigValue *value,
				   const char    **array)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_STRING_ARRAY);

	real = REAL_VALUE (value);

	g_strfreev (real->val.array);
	real->val.array = g_strdupv ((char **)array);
}

void
gdm_config_value_set_locale_string_array (GdmConfigValue *value,
					  const char    **array)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY);

	real = REAL_VALUE (value);

	g_strfreev (real->val.array);
	real->val.array = g_strdupv ((char **)array);
}

void
gdm_config_value_set_int (GdmConfigValue *value,
			  int             integer)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_INT);

	real = REAL_VALUE (value);

	real->val.integer = integer;
}

void
gdm_config_value_set_bool (GdmConfigValue *value,
			   gboolean        bool)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_BOOL);

	real = REAL_VALUE (value);

	real->val.bool = bool;
}

void
gdm_config_value_set_string (GdmConfigValue *value,
			     const char     *str)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_STRING);

	real = REAL_VALUE (value);

	g_free (real->val.str);
	real->val.str = g_strdup (str);
}

void
gdm_config_value_set_locale_string (GdmConfigValue *value,
				    const char     *str)
{
	GdmConfigRealValue *real;

	g_return_if_fail (value != NULL);
	g_return_if_fail (value->type == GDM_CONFIG_VALUE_LOCALE_STRING);

	real = REAL_VALUE (value);

	g_free (real->val.str);
	real->val.str = g_strdup (str);
}

char *
gdm_config_value_to_string (const GdmConfigValue *value)
{
	GdmConfigRealValue *real;
	char               *ret;

	g_return_val_if_fail (value != NULL, NULL);

	ret = NULL;
	real = REAL_VALUE (value);

	switch (real->type) {
        case GDM_CONFIG_VALUE_INVALID:
		break;
        case GDM_CONFIG_VALUE_BOOL:
		ret = real->val.bool ? g_strdup ("true") : g_strdup ("false");
		break;
        case GDM_CONFIG_VALUE_INT:
		ret = g_strdup_printf ("%d", real->val.integer);
		break;
        case GDM_CONFIG_VALUE_STRING:
        case GDM_CONFIG_VALUE_LOCALE_STRING:
		ret = g_strdup (real->val.str);
		break;
	case GDM_CONFIG_VALUE_STRING_ARRAY:
	case GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY:
		ret = g_strjoinv (";", real->val.array);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return ret;
}

static void
gdm_config_init (GdmConfig *config)
{
	config->entries = g_ptr_array_new ();
	config->value_hash = g_hash_table_new_full (g_str_hash,
						    g_str_equal,
						    (GDestroyNotify)g_free,
						    (GDestroyNotify)gdm_config_value_free);
}

GdmConfig *
gdm_config_new (void)
{
	GdmConfig *config;

	config = g_slice_new0 (GdmConfig);
	gdm_config_init (config);

	return config;
}

void
gdm_config_free (GdmConfig *config)
{
	g_return_if_fail (config != NULL);

	g_ptr_array_foreach (config->entries, (GFunc)gdm_config_entry_free, NULL);
	g_ptr_array_free (config->entries, TRUE);

	g_free (config->mandatory_filename);
	g_free (config->default_filename);
	g_free (config->custom_filename);

	if (config->mandatory_key_file != NULL) {
		g_key_file_free (config->mandatory_key_file);
	}
	if (config->default_key_file != NULL) {
		g_key_file_free (config->default_key_file);
	}
	if (config->custom_key_file != NULL) {
		g_key_file_free (config->custom_key_file);
	}
	if (config->value_hash != NULL) {
		g_hash_table_destroy (config->value_hash);
	}

	g_slice_free (GdmConfig, config);
}

const GdmConfigEntry *
gdm_config_lookup_entry (GdmConfig  *config,
			 const char *group,
			 const char *key)
{
	int                   i;
	const GdmConfigEntry *entry;

	g_return_val_if_fail (config != NULL, NULL);
	g_return_val_if_fail (group != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	entry = NULL;

	for (i = 0; i < config->entries->len; i++) {
		GdmConfigEntry *this;
		this = g_ptr_array_index (config->entries, i);
		if (strcmp (this->group, group) == 0
		    && strcmp (this->key, key) == 0) {
			entry = (const GdmConfigEntry *)this;
			break;
		}
	}

	return entry;
}

const GdmConfigEntry *
gdm_config_lookup_entry_for_id (GdmConfig  *config,
				int         id)
{
	int                   i;
	const GdmConfigEntry *entry;

	g_return_val_if_fail (config != NULL, NULL);

	entry = NULL;

	for (i = 0; i < config->entries->len; i++) {
		GdmConfigEntry *this;
		this = g_ptr_array_index (config->entries, i);
		if (this->id == id) {
			entry = (const GdmConfigEntry *)this;
			break;
		}
	}

	return entry;
}

void
gdm_config_add_entry (GdmConfig            *config,
		      const GdmConfigEntry *entry)
{
	GdmConfigEntry *new_entry;

	g_return_if_fail (config != NULL);
	g_return_if_fail (entry != NULL);

	new_entry = gdm_config_entry_copy (entry);
	g_ptr_array_add (config->entries, new_entry);
}

void
gdm_config_add_static_entries (GdmConfig            *config,
			       const GdmConfigEntry *entries)
{
	int i;

	g_return_if_fail (config != NULL);
	g_return_if_fail (entries != NULL);

	for (i = 0; entries[i].group != NULL; i++) {
		gdm_config_add_entry (config, &entries[i]);
	}
}

void
gdm_config_set_validate_func (GdmConfig       *config,
			      GdmConfigFunc    func,
			      gpointer         data)
{
	g_return_if_fail (config != NULL);

	config->validate_func = func;
	config->validate_func_data = data;
}

void
gdm_config_set_mandatory_file (GdmConfig  *config,
			       const char *name)
{
	g_return_if_fail (config != NULL);

	g_free (config->mandatory_filename);
	config->mandatory_filename = g_strdup (name);
}

void
gdm_config_set_default_file (GdmConfig  *config,
			     const char *name)
{
	g_return_if_fail (config != NULL);

	g_free (config->default_filename);
	config->default_filename = g_strdup (name);
}

void
gdm_config_set_custom_file (GdmConfig  *config,
			    const char *name)
{
	g_return_if_fail (config != NULL);

	g_free (config->custom_filename);
	config->custom_filename = g_strdup (name);
}

void
gdm_config_set_notify_func (GdmConfig       *config,
			    GdmConfigFunc    func,
			    gpointer         data)
{
	g_return_if_fail (config != NULL);

	config->notify_func = func;
	config->notify_func_data = data;
}

static gboolean
key_file_get_value (GdmConfig            *config,
		    GKeyFile             *key_file,
		    const char           *group,
		    const char           *key,
		    GdmConfigValueType    type,
		    GdmConfigValue      **valuep)
{
	char           *val;
	GError         *error;
	GdmConfigValue *value;
	gboolean        ret;

	ret = FALSE;
	value = NULL;

	error = NULL;
	if (type == GDM_CONFIG_VALUE_LOCALE_STRING ||
	    type == GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY) {
		/* Use NULL locale to detect current locale */
		val = g_key_file_get_locale_string (key_file,
						    group,
						    key,
						    NULL,
						    &error);
		g_debug ("Loading locale string: %s %s", key, val);

		if (error != NULL) {
			g_debug ("%s", error->message);
			g_error_free (error);
		}
		if (val == NULL) {
			error = NULL;
			val = g_key_file_get_value (key_file,
						    group,
						    key,
						    &error);
			g_debug ("Loading non-locale string: %s %s", key, val);
		}
	} else {
		val = g_key_file_get_value (key_file,
					    group,
					    key,
					    &error);
	}

	if (error != NULL) {
		g_error_free (error);
		goto out;
	}

	if (val == NULL) {
		goto out;
	}

	error = NULL;
	value = gdm_config_value_new_from_string (type, val, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
		goto out;
	}

	ret = TRUE;

 out:
	*valuep = value;

	return ret;
}

static void
entry_get_default_value (GdmConfig            *config,
			 const GdmConfigEntry *entry,
			 GdmConfigValue      **valuep)
{
	GdmConfigValue *value;
	GError         *error;

	error = NULL;
	value = gdm_config_value_new_from_string (entry->type,
						  entry->default_value ? entry->default_value : "",
						  &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	*valuep = value;
}

static gboolean
load_value_entry (GdmConfig            *config,
		  const GdmConfigEntry *entry,
		  GdmConfigValue      **valuep,
		  GdmConfigSourceType  *sourcep)
{
	GdmConfigValue     *value;
	GdmConfigSourceType source;
	gboolean            ret;
	gboolean            res;

	value = NULL;

	/* Look for the first occurence of the key in:
	   mandatory file, custom file, default file, or built-in-default
	 */

	if (config->mandatory_filename != NULL) {
		source = GDM_CONFIG_SOURCE_MANDATORY;
		res = key_file_get_value (config,
					  config->mandatory_key_file,
					  entry->group,
					  entry->key,
					  entry->type,
					  &value);
		if (res) {
			goto done;
		}
	}
	if (config->custom_filename != NULL) {
		source = GDM_CONFIG_SOURCE_CUSTOM;
		res = key_file_get_value (config,
					  config->custom_key_file,
					  entry->group,
					  entry->key,
					  entry->type,
					  &value);
		if (res) {
			goto done;
		}
	}
	if (config->default_filename != NULL) {
		source = GDM_CONFIG_SOURCE_DEFAULT;
		res = key_file_get_value (config,
					  config->default_key_file,
					  entry->group,
					  entry->key,
					  entry->type,
					  &value);
		if (res) {
			goto done;
		}
	}


	source = GDM_CONFIG_SOURCE_BUILT_IN;
	entry_get_default_value (config, entry, &value);

 done:

	if (value != NULL) {
		ret = TRUE;
	} else {
		ret = FALSE;
	}

	*valuep = value;
	*sourcep = source;

	return ret;
}

static int
lookup_id_for_key (GdmConfig  *config,
		   const char *group,
		   const char *key)
{
	int                   id;
	const GdmConfigEntry *entry;

	id = GDM_CONFIG_INVALID_ID;
	entry = gdm_config_lookup_entry (config, group, key);
	if (entry != NULL) {
		id = entry->id;
	}

	return id;
}

static void
internal_set_value (GdmConfig          *config,
		    GdmConfigSourceType source,
		    const char         *group,
		    const char         *key,
		    GdmConfigValue     *value)
{
	char           *key_path;
	int             id;
	GdmConfigValue *v;
	gboolean        res;

	g_return_if_fail (config != NULL);

	key_path = g_strdup_printf ("%s/%s", group, key);

	v = NULL;
	res = g_hash_table_lookup_extended (config->value_hash,
					    key_path,
					    NULL,
					    (gpointer *)&v);

	if (res) {
		if (v != NULL && gdm_config_value_compare (v, value) == 0) {
			/* value is the same - don't update */
			goto out;
		}
	}

	g_hash_table_insert (config->value_hash,
			     g_strdup (key_path),
			     gdm_config_value_copy (value));

	id = lookup_id_for_key (config, group, key);

	if (config->notify_func) {
		(* config->notify_func) (config, source, group, key, value, id, config->notify_func_data);
	}
 out:
	g_free (key_path);
}

static void
store_entry_value (GdmConfig            *config,
		   const GdmConfigEntry *entry,
		   GdmConfigSourceType   source,
		   GdmConfigValue       *value)
{
	internal_set_value (config, source, entry->group, entry->key, value);
}

static gboolean
load_entry (GdmConfig            *config,
	    const GdmConfigEntry *entry)
{
	GdmConfigValue     *value;
	GdmConfigSourceType source;
	gboolean            res;

	value = NULL;
	source = GDM_CONFIG_SOURCE_INVALID;

	res = load_value_entry (config, entry, &value, &source);
	if (!res) {
		return FALSE;
	}

	res = TRUE;
	if (config->validate_func) {
		res = (* config->validate_func) (config, source, entry->group, entry->key, value, entry->id, config->validate_func_data);
	}

	if (res) {
		/* store runs notify */
		store_entry_value (config, entry, source, value);
	}

	return TRUE;
}

static void
add_keys_to_hash (GKeyFile   *key_file,
		  const char *group_name,
		  GHashTable *hash)
{
	GError     *local_error;
	char      **keys;
	gsize       len;
	int         i;

	local_error = NULL;
	len = 0;
	keys = g_key_file_get_keys (key_file,
				    group_name,
				    &len,
				    &local_error);
	if (local_error != NULL) {
		g_error_free (local_error);
		return;
	}

	for (i = 0; i < len; i++) {
		g_hash_table_insert (hash, keys[i], GINT_TO_POINTER (1));
	}
}

static void
collect_hash_keys (const char *key,
		   gpointer    value,
		   GPtrArray **array)
{
	g_message ("Adding %s", key);
	g_ptr_array_add (*array, g_strdup (key));
}

char **
gdm_config_get_keys_for_group (GdmConfig  *config,
			       const char *group,
			       gsize      *length,
			       GError    **error)
{
	GHashTable *hash;
	gsize       len;
	GPtrArray  *array;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	if (config->mandatory_filename != NULL) {
		add_keys_to_hash (config->mandatory_key_file, group, hash);
	}

	if (config->default_filename != NULL) {
		add_keys_to_hash (config->default_key_file, group, hash);
	}

	if (config->custom_filename != NULL) {
		add_keys_to_hash (config->custom_key_file, group, hash);
	}

	len = g_hash_table_size (hash);
	array = g_ptr_array_sized_new (len);

	g_hash_table_foreach (hash, (GHFunc)collect_hash_keys, &array);
	g_ptr_array_add (array, NULL);

	g_hash_table_destroy (hash);

	if (length != NULL) {
		*length = array->len - 1;
	}

	return (char **)g_ptr_array_free (array, FALSE);
}

static gboolean
load_backend (GdmConfig  *config,
	      const char *filename,
	      GKeyFile  **key_file,
	      time_t     *mtime)
{
	GError     *local_error;
	gboolean    res;
	gboolean    ret;
	struct stat statbuf;
	GKeyFile   *kf;
	time_t      lmtime;

	if (filename == NULL) {
		return FALSE;
	}

	if (g_stat (filename, &statbuf) != 0) {
		return FALSE;
	}
	lmtime = statbuf.st_mtime;

	/* if already loaded check whether reload is necessary */
	if (*key_file != NULL) {
		if (lmtime > *mtime) {
			/* needs an update */
			g_key_file_free (*key_file);
		} else {
			/* no reload necessary so we're done */
			return TRUE;
		}
	}

	kf = g_key_file_new ();

	local_error = NULL;
	res = g_key_file_load_from_file (kf,
					 filename,
					 G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
					 &local_error);
	if (! res) {
		g_error_free (local_error);
		g_key_file_free (kf);
		kf = NULL;
		lmtime = 0;
		ret = FALSE;
	} else {
		ret = TRUE;
	}

	*key_file = kf;
	*mtime = lmtime;

	return ret;
}

gboolean
gdm_config_load (GdmConfig *config,
		 GError   **error)
{
	g_return_val_if_fail (config != NULL, FALSE);

	config->mandatory_loaded = load_backend (config,
						 config->mandatory_filename,
						 &config->mandatory_key_file,
						 &config->mandatory_mtime);
	config->default_loaded = load_backend (config,
					       config->default_filename,
					       &config->default_key_file,
					       &config->default_mtime);
	config->custom_loaded = load_backend (config,
					      config->custom_filename,
					      &config->custom_key_file,
					      &config->custom_mtime);

	return TRUE;
}

static gboolean
process_entries (GdmConfig             *config,
		 const GdmConfigEntry **entries,
		 gsize                  n_entries,
		 GError               **error)
{
	gboolean ret;
	int      i;

	ret = TRUE;

	for (i = 0; i < n_entries; i++) {
		load_entry (config, entries[i]);
	}

	return ret;
}

gboolean
gdm_config_process_entry (GdmConfig            *config,
			  const GdmConfigEntry *entry,
			  GError              **error)
{
	gboolean  ret;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (entry != NULL, FALSE);

	ret = load_entry (config, entry);

	return ret;
}

gboolean
gdm_config_process_entries (GdmConfig             *config,
			    const GdmConfigEntry **entries,
			    gsize                  n_entries,
			    GError               **error)
{
	gboolean  ret;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (entries != NULL, FALSE);
	g_return_val_if_fail (n_entries > 0, FALSE);

	ret = process_entries (config, entries, n_entries, error);

	return ret;
}

gboolean
gdm_config_process_all (GdmConfig *config,
			GError   **error)
{
	gboolean  ret;

	g_return_val_if_fail (config != NULL, FALSE);

	ret = process_entries (config,
			       (const GdmConfigEntry **)config->entries->pdata,
			       config->entries->len,
			       error);

	return ret;
}

gboolean
gdm_config_peek_value (GdmConfig             *config,
		       const char            *group,
		       const char            *key,
		       const GdmConfigValue **valuep)
{
	gboolean              ret;
	char                 *key_path;
	const GdmConfigValue *value;

	g_return_val_if_fail (config != NULL, FALSE);

	key_path = g_strdup_printf ("%s/%s", group, key);
	value = NULL;
	ret = g_hash_table_lookup_extended (config->value_hash,
					    key_path,
					    NULL,
					    (gpointer *)&value);
	g_free (key_path);

	if (valuep != NULL) {
		if (ret) {
			*valuep = value;
		} else {
			*valuep = NULL;
		}
	}

	return ret;
}

gboolean
gdm_config_get_value (GdmConfig       *config,
		      const char      *group,
		      const char      *key,
		      GdmConfigValue **valuep)
{
	gboolean              res;
	const GdmConfigValue *value;

	res = gdm_config_peek_value (config, group, key, &value);
	if (valuep != NULL) {
		*valuep = (value == NULL) ? NULL : gdm_config_value_copy (value);
	}

	return res;
}

gboolean
gdm_config_set_value (GdmConfig       *config,
		      const char      *group,
		      const char      *key,
		      GdmConfigValue  *value)
{
	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (group != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	internal_set_value (config, GDM_CONFIG_SOURCE_RUNTIME_USER, group, key, value);

	return TRUE;
}

static gboolean
gdm_config_peek_value_for_id (GdmConfig             *config,
			      int                    id,
			      const GdmConfigValue **valuep)
{
	const GdmConfigEntry *entry;

	g_return_val_if_fail (config != NULL, FALSE);

	entry = gdm_config_lookup_entry_for_id (config, id);
	if (entry == NULL) {
		return FALSE;
	}

	return gdm_config_peek_value (config, entry->group, entry->key, valuep);
}

gboolean
gdm_config_get_value_for_id (GdmConfig       *config,
			     int              id,
			     GdmConfigValue **valuep)
{
	const GdmConfigEntry *entry;

	g_return_val_if_fail (config != NULL, FALSE);

	entry = gdm_config_lookup_entry_for_id (config, id);
	if (entry == NULL) {
		return FALSE;
	}

	return gdm_config_get_value (config, entry->group, entry->key, valuep);
}

gboolean
gdm_config_set_value_for_id (GdmConfig      *config,
			     int             id,
			     GdmConfigValue *valuep)
{
	const GdmConfigEntry *entry;

	g_return_val_if_fail (config != NULL, FALSE);

	entry = gdm_config_lookup_entry_for_id (config, id);
	if (entry == NULL) {
		return FALSE;
	}

	return gdm_config_set_value (config, entry->group, entry->key, valuep);
}

gboolean
gdm_config_peek_string_for_id (GdmConfig       *config,
			       int              id,
			       const char     **strp)
{
	const GdmConfigValue *value;
	const char           *str;
	gboolean              res;

	g_return_val_if_fail (config != NULL, FALSE);

	res = gdm_config_peek_value_for_id (config, id, &value);
	if (! res) {
		return FALSE;
	}

	str = gdm_config_value_get_string (value);
	if (strp != NULL) {
		*strp = str;
	}

	return res;
}

gboolean
gdm_config_get_string_for_id (GdmConfig       *config,
			      int              id,
			      char           **strp)
{
	gboolean    res;
	const char *str;

	res = gdm_config_peek_string_for_id (config, id, &str);
	if (strp != NULL) {
		*strp = g_strdup (str);
	}

	return res;
}

gboolean
gdm_config_get_bool_for_id (GdmConfig       *config,
			    int              id,
			    gboolean        *boolp)
{
	GdmConfigValue *value;
	gboolean        bool;
	gboolean        res;

	g_return_val_if_fail (config != NULL, FALSE);

	res = gdm_config_get_value_for_id (config, id, &value);
	if (! res) {
		return FALSE;
	}

	bool = gdm_config_value_get_bool (value);
	if (boolp != NULL) {
		*boolp = bool;
	}

	gdm_config_value_free (value);

	return res;
}

gboolean
gdm_config_get_int_for_id (GdmConfig       *config,
			   int              id,
			   int             *integerp)
{
	GdmConfigValue *value;
	gboolean        integer;
	gboolean        res;

	g_return_val_if_fail (config != NULL, FALSE);

	res = gdm_config_get_value_for_id (config, id, &value);
	if (! res) {
		return FALSE;
	}

	integer = gdm_config_value_get_int (value);
	if (integerp != NULL) {
		*integerp = integer;
	}

	gdm_config_value_free (value);

	return res;
}

gboolean
gdm_config_set_string_for_id (GdmConfig      *config,
			      int             id,
			      char           *str)
{
	GdmConfigValue *value;
	gboolean        res;

	g_return_val_if_fail (config != NULL, FALSE);

	value = gdm_config_value_new (GDM_CONFIG_VALUE_STRING);
	gdm_config_value_set_string (value, str);

	res = gdm_config_set_value_for_id (config, id, value);
	gdm_config_value_free (value);

	return res;
}

gboolean
gdm_config_set_bool_for_id (GdmConfig      *config,
			    int             id,
			    gboolean        bool)
{
	GdmConfigValue *value;
	gboolean        res;

	g_return_val_if_fail (config != NULL, FALSE);

	value = gdm_config_value_new (GDM_CONFIG_VALUE_BOOL);
	gdm_config_value_set_bool (value, bool);

	res = gdm_config_set_value_for_id (config, id, value);
	gdm_config_value_free (value);

	return res;
}

gboolean
gdm_config_set_int_for_id (GdmConfig      *config,
			   int             id,
			   int             integer)
{
	GdmConfigValue *value;
	gboolean        res;

	g_return_val_if_fail (config != NULL, FALSE);

	value = gdm_config_value_new (GDM_CONFIG_VALUE_INT);
	gdm_config_value_set_int (value, integer);

	res = gdm_config_set_value_for_id (config, id, value);
	gdm_config_value_free (value);

	return res;
}
