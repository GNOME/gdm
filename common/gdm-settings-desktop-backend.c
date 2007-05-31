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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "gdm-settings-desktop-backend.h"

#include "gdm-marshal.h"

#define GDM_SETTINGS_DESKTOP_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SETTINGS_DESKTOP_BACKEND, GdmSettingsDesktopBackendPrivate))

struct GdmSettingsDesktopBackendPrivate
{
	char       *filename;
	GKeyFile   *key_file;
	GHashTable *values;
};

static void	gdm_settings_desktop_backend_class_init	(GdmSettingsDesktopBackendClass *klass);
static void	gdm_settings_desktop_backend_init	(GdmSettingsDesktopBackend      *settings_desktop_backend);
static void	gdm_settings_desktop_backend_finalize	(GObject	  *object);

G_DEFINE_TYPE (GdmSettingsDesktopBackend, gdm_settings_desktop_backend, GDM_TYPE_SETTINGS_BACKEND)

static gboolean
parse_key_string (const char *keystring,
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

	g_return_val_if_fail (keystring != NULL, FALSE);

	ret = FALSE;
	g = k = v = l = NULL;
	split1 = split2 = NULL;

	g_debug ("Attempting to parse key string: %s", keystring);

	split1 = g_strsplit (keystring, "/", 2);
	if (split1 == NULL || split1 [0] == NULL || split1 [1] == NULL) {
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

static gboolean
gdm_settings_desktop_backend_get_value (GdmSettingsBackend *backend,
					const char  *key,
					char       **value,
					GError     **error)
{
	GError *local_error;
	char   *val;
	char   *g;
	char   *k;
	char   *l;

	g_return_val_if_fail (GDM_IS_SETTINGS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (value != NULL) {
		*value = NULL;
	}

	/*GDM_SETTINGS_BACKEND_CLASS (gdm_settings_desktop_backend_parent_class)->get_value (display);*/
	if (! parse_key_string (key, &g, &k, &l, NULL)) {
		g_set_error (error, GDM_SETTINGS_BACKEND_ERROR, GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND, "Key not found");
		return FALSE;
	}

	g_debug ("Getting key: %s %s %s", g, k, l);
	local_error = NULL;
	val = g_key_file_get_value (GDM_SETTINGS_DESKTOP_BACKEND (backend)->priv->key_file,
				    g,
				    k,
				    &local_error);
	if (local_error != NULL) {
		g_error_free (local_error);
		g_set_error (error, GDM_SETTINGS_BACKEND_ERROR, GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND, "Key not found");
		return FALSE;
	}

	if (value != NULL) {
		*value = g_strdup (val);
	}

	g_free (val);

	return TRUE;
}

static gboolean
gdm_settings_desktop_backend_set_value (GdmSettingsBackend *settings_backend,
					const char  *key,
					const char  *value,
					GError     **error)
{
	g_return_val_if_fail (GDM_IS_SETTINGS_BACKEND (settings_backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	g_debug ("Setting key %s", key);

	return FALSE;
}

static void
gdm_settings_desktop_backend_class_init (GdmSettingsDesktopBackendClass *klass)
{
	GObjectClass            *object_class = G_OBJECT_CLASS (klass);
	GdmSettingsBackendClass *backend_class = GDM_SETTINGS_BACKEND_CLASS (klass);

	object_class->finalize = gdm_settings_desktop_backend_finalize;

	backend_class->get_value = gdm_settings_desktop_backend_get_value;
	backend_class->set_value = gdm_settings_desktop_backend_set_value;

	g_type_class_add_private (klass, sizeof (GdmSettingsDesktopBackendPrivate));
}

static void
gdm_settings_desktop_backend_init (GdmSettingsDesktopBackend *backend)
{
	gboolean res;
	GError  *error;

	backend->priv = GDM_SETTINGS_DESKTOP_BACKEND_GET_PRIVATE (backend);

	backend->priv->key_file = g_key_file_new ();
	backend->priv->filename = g_strdup (GDMCONFDIR "/custom.conf");

	backend->priv->values = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       (GDestroyNotify)g_free,
						       (GDestroyNotify)g_free);

	error = NULL;
	res = g_key_file_load_from_file (backend->priv->key_file,
					 backend->priv->filename,
					 G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
					 &error);
	if (! res) {
		g_warning ("Unable to load file '%s': %s", backend->priv->filename, error->message);
	}
}

static void
gdm_settings_desktop_backend_finalize (GObject *object)
{
	GdmSettingsDesktopBackend *backend;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SETTINGS_DESKTOP_BACKEND (object));

	backend = GDM_SETTINGS_DESKTOP_BACKEND (object);

	g_return_if_fail (backend->priv != NULL);

	g_key_file_free (backend->priv->key_file);
	g_hash_table_destroy (backend->priv->values);

	G_OBJECT_CLASS (gdm_settings_desktop_backend_parent_class)->finalize (object);
}

GdmSettingsBackend *
gdm_settings_desktop_backend_new (void)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SETTINGS_DESKTOP_BACKEND, NULL);

	return GDM_SETTINGS_BACKEND (object);
}
