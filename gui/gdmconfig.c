/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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

#include <stdlib.h>
#include <syslog.h>
#include <gtk/gtk.h>

#include "config.h"

#include "gdm.h"
#include "gdmcomm.h"
#include "gdmconfig.h"

#include "vicious.h"

static GHashTable *int_hash       = NULL;
static GHashTable *bool_hash      = NULL;
static GHashTable *string_hash    = NULL;

/*
 * Hack to keep track if config functions should be printing error messages
 * to syslog or stdout
 */
static gboolean using_syslog = FALSE;

void
gdm_openlog (const char *ident, int logopt, int facility)
{
   openlog (ident, logopt, facility);
   using_syslog = TRUE;
}

/**
 * gdm_config_hash_lookup
 *
 * Accesses hash with key, stripping it so it doesn't contain
 * a default value.
 */
static gpointer 
gdm_config_hash_lookup (GHashTable *hash, gchar *key)
{
	gchar *p;
	gpointer *ret;
	gchar *newkey = g_strdup (key);

	g_strstrip (newkey);
	p = strchr (newkey, '=');
	if (p != NULL)
		*p = '\0';

	ret = g_hash_table_lookup (hash, newkey);
	g_free (newkey);
	return (ret);
}

/**
 * gdm_config_add_hash
 *
 * Adds value to hash, stripping the key so it doesn't contain
 * a default value.
 */
static void
gdm_config_add_hash (GHashTable *hash, gchar *key, gpointer value)
{
	gchar *p;
	gchar *newkey = g_strdup (key);

	g_strstrip (newkey);
	p = strchr (newkey, '=');
	if (p != NULL)
		*p = '\0';

	g_hash_table_insert (hash, newkey, value);
}

/**
 * gdm_config_get_result
 *
 * Calls daemon to get config result, stripping the key so it
 * doesn't contain a default value.
 */
static gchar *
gdm_config_get_result (gchar *key)
{
	gchar *p;
	gchar *newkey  = g_strdup (key);
	gchar *command = NULL;;
	gchar *result  = NULL;

	g_strstrip (newkey);
	p = strchr (newkey, '=');
	if (p != NULL)
		*p = '\0';

	command = g_strdup_printf ("GET_CONFIG %s", key);
	result = gdmcomm_call_gdm (command, NULL /* auth cookie */,
		"2.13.0.1", 5);

	g_free (newkey);
	return result;
}

/**
 * gdm_config_get_string
 *
 * Gets string configuration value from daemon via GET_CONFIG
 * socket command.  It stores the value in a hash so subsequent
 * access is faster.
 */
static gchar *
_gdm_config_get_string (gchar *key, gboolean reload, gboolean *changed, gboolean show_error)
{
	gchar **hashretval = NULL;
	gboolean *valueset = NULL;
	gchar *result = NULL;
	gchar *temp;

        if (string_hash == NULL)
		string_hash = g_hash_table_new (g_str_hash, g_str_equal);

	hashretval = gdm_config_hash_lookup (string_hash, key);

	if (reload == FALSE && hashretval != NULL)
		return *hashretval;

	result = gdm_config_get_result (key);

	if (! result || ve_string_empty (result) ||
	    strncmp (result, "OK ", 3) != 0) {

		if (show_error) {
			if (using_syslog)
				syslog (LOG_ERR, "Could not access configuration key %s", key);
			else
				printf ("Could not access configuration key %s\n", key);
		}

		if (result)
			g_free (result);
		return NULL;
	}

	/* skip the "OK " */
	temp = g_strdup (result + 3);
	g_free (result);

	if (hashretval == NULL) {
		gchar** charval = g_new0 (gchar *, 1);
		*charval = temp;
		gdm_config_add_hash (string_hash, key, charval);

		if (changed != NULL)
			*changed = TRUE;

		return *charval;
	} else {
		if (changed != NULL) {
			if (strcmp (ve_sure_string (*hashretval), temp) != 0)
				*changed = TRUE;
			else
				*changed = FALSE;
		}

		*hashretval = temp;
		return *hashretval;
	}
}

gchar *
gdm_config_get_string (gchar *key)
{
   return _gdm_config_get_string (key, FALSE, NULL, TRUE);
}

/**
 * gdm_config_get_translated_string
 *
 * Gets translated string configuration value from daemon via
 * GET_CONFIG socket command.  It stores the value in a hash so
 * subsequent access is faster.  This does similar logic to
 * ve_config_get_trasnlated_string, requesting the value for 
 * each language and returning the default value if none is
 * found.
 */ 
static gchar *
_gdm_config_get_translated_string (gchar *key, gboolean reload, gboolean *changed)
{
        const GList *li;
        char *dkey;
        char *def;
        char *ret;

	/* Strip key */
        dkey = g_strdup (key);
        def = strchr (dkey, '=');
        if (def != NULL) {
                *def = '\0';
                def++;
        }

        for (li = ve_i18n_get_language_list ("LC_MESSAGES");
             li != NULL;
             li = li->next) {
                gchar *full = g_strdup_printf ("%s[%s]", dkey, (char *)li->data);

		/*
		 * Pass FALSE for last argument so it doesn't print errors for
		 * failing to find the key, since this is expected
		 */
	        gchar *val = _gdm_config_get_string (full, reload, changed, FALSE);

                if (val != NULL)
			return val;
        }

	/* Print error if it fails this time */
	return _gdm_config_get_string (key, reload, changed, TRUE);
}

gchar *
gdm_config_get_translated_string (gchar *key)
{
   return _gdm_config_get_translated_string (key, FALSE, NULL);
}

/**
 * gdm_config_get_int
 *
 * Gets int configuration value from daemon via GET_CONFIG
 * socket command.  It stores the value in a hash so subsequent
 * access is faster.
 */
static gint
_gdm_config_get_int (gchar *key, gboolean reload, gboolean *changed)
{
	gint  *hashretval = NULL;
	gchar *result = NULL;
	gint  temp;

        if (int_hash == NULL)
		int_hash = g_hash_table_new (g_str_hash, g_str_equal);

	hashretval = gdm_config_hash_lookup (int_hash, key);
	if (reload == FALSE && hashretval != NULL)
		return *hashretval;

	result = gdm_config_get_result (key);

	if (! result || ve_string_empty (result) ||
	    strncmp (result, "OK ", 3) != 0) {

		if (using_syslog)
			syslog (LOG_ERR, "Could not access configuration key %s", key);
		else
			printf ("Could not access configuration key %s\n", key);

		if (result)
			g_free (result);
		return 0;
	}

	/* skip the "OK " */
	temp = atoi (result + 3);
	g_free (result);

	if (hashretval == NULL) {
		gint *intval = g_new0 (gint, 1);
		*intval = temp;
		gdm_config_add_hash (int_hash, key, intval);

		if (changed != NULL)
			*changed = TRUE;

		return *intval;
	} else {
		if (changed != NULL) {
			if (*hashretval != temp)
				*changed = TRUE;
			else
				*changed = FALSE;
		}

		*hashretval = temp;
		return *hashretval;
	}
}

gint
gdm_config_get_int (gchar *key)
{
   return _gdm_config_get_int (key, FALSE, NULL);
}

/**
 * gdm_config_get_bool
 *
 * Gets int configuration value from daemon via GET_CONFIG
 * socket command.  It stores the value in a hash so subsequent
 * access is faster.
 */
gboolean
_gdm_config_get_bool (gchar *key, gboolean reload, gboolean *changed)
{
	gboolean *hashretval = NULL;
	gchar    *result;
	gboolean temp;

        if (bool_hash == NULL)
           bool_hash = g_hash_table_new (g_str_hash, g_str_equal);

	hashretval = gdm_config_hash_lookup (bool_hash, key);
	if (reload == FALSE && hashretval != NULL)
		return *hashretval;

	result = gdm_config_get_result (key);

	if (! result || ve_string_empty (result) ||
	    strncmp (result, "OK ", 3) != 0) {

		if (using_syslog)
			syslog (LOG_ERR, "Could not access configuration key %s", key);
		else
			printf ("Could not access configuration key %s\n", key);

		if (result)
			g_free (result);
		return FALSE;
	}

	/* skip the "OK " */
	if (strcmp (ve_sure_string (result + 3), "true") == 0)
		temp = TRUE;
	else
		temp = FALSE;
	g_free (result);

	if (hashretval == NULL) {
		gboolean *boolval = g_new0 (gboolean, 1);
		*boolval = temp;
		gdm_config_add_hash (bool_hash, key, boolval);

		if (changed != NULL)
			*changed = TRUE;

		return *boolval;
	} else {
		if (changed != NULL) {
			if (*hashretval != temp)
				*changed = TRUE;
			else
				*changed = FALSE;
		}

		*hashretval = temp;
		return *hashretval;
	}
}

gboolean
gdm_config_get_bool (gchar *key)
{
   return _gdm_config_get_bool (key, FALSE, NULL);
}

/**
 * gdm_config_reload_string
 * gdm_config_reload_translated_string
 * gdm_config_reload_int
 * gdm_config_reload_bool
 * 
 * Reload values returning TRUE if value changed, FALSE
 * otherwise.
 */
gboolean
gdm_config_reload_string (gchar *key)
{
	gboolean changed;
	_gdm_config_get_string (key, TRUE, &changed, TRUE);
	return changed;
}

gboolean
gdm_config_reload_translated_string (gchar *key)
{
	gboolean changed;
	_gdm_config_get_translated_string (key, TRUE, &changed);
	return changed;
}

gboolean
gdm_config_reload_int (gchar *key)
{
	gboolean changed;
	_gdm_config_get_int (key, TRUE, &changed);
	return changed;
}

gboolean
gdm_config_reload_bool (gchar *key)
{
	gboolean changed;
	_gdm_config_get_bool (key, TRUE, &changed);
	return changed;
}

void
gdm_set_servauth (gchar *file, gchar *key, gchar *id)
{
	VeConfig *cfg;
	cfg = ve_config_get (file);
	g_free (file);
	ve_config_set_string (cfg, key, ve_sure_string (id));
	ve_config_save (cfg, FALSE);
}

gchar *
gdm_get_theme_greeter (gchar *file, const char *fallback)
{
	VeConfig *config = ve_config_new (file);
	gchar *s;

	s = ve_config_get_translated_string (config, "GdmGreeterTheme/Greeter");

	if (s == NULL || s[0] == '\0') {
		g_free (s);
		s = g_strdup_printf ("%s.xml", fallback);
	}

	return s;
}

