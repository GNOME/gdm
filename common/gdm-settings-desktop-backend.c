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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

struct _GdmSettingsDesktopBackend
{
        GdmSettingsBackend parent;

        char       *filename;
        GKeyFile   *key_file;
        gboolean    dirty;
        guint       save_id;
};

enum {
        PROP_0,
        PROP_FILENAME,
};

static void     gdm_settings_desktop_backend_class_init (GdmSettingsDesktopBackendClass *klass);
static void     gdm_settings_desktop_backend_init       (GdmSettingsDesktopBackend      *settings_desktop_backend);
static void     gdm_settings_desktop_backend_finalize   (GObject                        *object);

G_DEFINE_TYPE (GdmSettingsDesktopBackend, gdm_settings_desktop_backend, GDM_TYPE_SETTINGS_BACKEND)

static void
_gdm_settings_desktop_backend_set_file_name (GdmSettingsDesktopBackend *backend,
                                             const char                *filename)
{
        gboolean res;
        g_autoptr(GError) error = NULL;
        g_autofree char *contents = NULL;

        g_free (backend->filename);
        backend->filename = g_strdup (filename);

        backend->key_file = g_key_file_new ();

        res = g_key_file_load_from_file (backend->key_file,
                                         backend->filename,
                                         G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                         &error);
        if (! res) {
                g_warning ("Unable to load file '%s': %s", backend->filename, error->message);
        }

        contents = g_key_file_to_data (backend->key_file, NULL, NULL);

        if (contents != NULL) {
                g_debug ("GdmSettings: %s is:\n%s\n", backend->filename, contents);
        }

}

static void
gdm_settings_desktop_backend_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
        GdmSettingsDesktopBackend *self;

        self = GDM_SETTINGS_DESKTOP_BACKEND (object);

        switch (prop_id) {
                case PROP_FILENAME:
                        _gdm_settings_desktop_backend_set_file_name (self, g_value_get_string (value));
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
gdm_settings_desktop_backend_get_property (GObject      *object,
                                           guint         prop_id,
                                           GValue       *value,
                                           GParamSpec   *pspec)
{
        GdmSettingsDesktopBackend *self;

        self = GDM_SETTINGS_DESKTOP_BACKEND (object);

        switch (prop_id) {
                case PROP_FILENAME:
                        g_value_set_string (value, self->filename);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static gboolean
parse_key_string (const char *keystring,
                  char      **group,
                  char      **key,
                  char      **locale,
                  char      **value)
{
        g_auto(GStrv) split1 = NULL;
        g_auto(GStrv) split2 = NULL;
        char    *g;
        char    *k;
        char    *l;
        char    *v;
        char    *tmp1;
        char    *tmp2;

        g_return_val_if_fail (keystring != NULL, FALSE);

        g = k = v = l = NULL;

        if (group != NULL) {
                *group = g;
        }
        if (key != NULL) {
                *key = k;
        }
        if (locale != NULL) {
                *locale = l;
        }
        if (value != NULL) {
                *value = v;
        }

        /*g_debug ("Attempting to parse key string: %s", keystring);*/

        split1 = g_strsplit (keystring, "/", 2);
        if (split1 == NULL
            || split1 [0] == NULL
            || split1 [1] == NULL
            || split1 [0][0] == '\0'
            || split1 [1][0] == '\0') {
                g_warning ("GdmSettingsDesktopBackend: invalid key: %s", keystring);
                return FALSE;
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

        return TRUE;
}

static gboolean
gdm_settings_desktop_backend_get_value (GdmSettingsBackend *backend,
                                        const char         *key,
                                        char              **value,
                                        GError            **error)
{
        g_autoptr(GError) local_error = NULL;
        g_autofree char *val = NULL;
        g_autofree char *g = NULL;
        g_autofree char *k = NULL;
        g_autofree char *l = NULL;

        g_return_val_if_fail (GDM_IS_SETTINGS_BACKEND (backend), FALSE);
        g_return_val_if_fail (key != NULL, FALSE);
        g_return_val_if_fail (value != NULL, FALSE);

        *value = NULL;

        /*GDM_SETTINGS_BACKEND_CLASS (gdm_settings_desktop_backend_parent_class)->get_value (display);*/
        if (! parse_key_string (key, &g, &k, &l, NULL)) {
                g_set_error (error, GDM_SETTINGS_BACKEND_ERROR, GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND, "Key not found");
                return FALSE;
        }

        /*g_debug ("Getting key: %s %s %s", g, k, l);*/
        val = g_key_file_get_value (GDM_SETTINGS_DESKTOP_BACKEND (backend)->key_file,
                                    g,
                                    k,
                                    &local_error);
        if (local_error != NULL) {
                g_set_error (error, GDM_SETTINGS_BACKEND_ERROR, GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND, "Key not found");
                return FALSE;
        }

        *value = g_strdup (val);

        return TRUE;
}

static void
save_settings (GdmSettingsDesktopBackend *backend)
{
        g_autoptr(GError) local_error = NULL;
        g_autofree char *contents = NULL;
        gsize     length;

        if (! backend->dirty) {
                return;
        }

        g_debug ("Saving settings to %s", backend->filename);

        contents = g_key_file_to_data (backend->key_file, &length, &local_error);
        if (local_error != NULL) {
                g_warning ("Unable to save settings: %s", local_error->message);
                return;
        }

        g_file_set_contents (backend->filename,
                             contents,
                             length,
                             &local_error);
        if (local_error != NULL) {
                g_warning ("Unable to save settings: %s", local_error->message);
                return;
        }

        backend->dirty = FALSE;
}

static gboolean
save_settings_timer (GdmSettingsDesktopBackend *backend)
{
        save_settings (backend);
        backend->save_id = 0;
        return FALSE;
}

static void
queue_save (GdmSettingsDesktopBackend *backend)
{
        if (! backend->dirty) {
                return;
        }

        if (backend->save_id != 0) {
                /* already pending */
                return;
        }

        backend->save_id = g_timeout_add_seconds (5, (GSourceFunc)save_settings_timer, backend);
}

static gboolean
gdm_settings_desktop_backend_set_value (GdmSettingsBackend *backend,
                                        const char         *key,
                                        const char         *value,
                                        GError            **error)
{
        g_autofree char *old_val = NULL;
        g_autofree char *g = NULL;
        g_autofree char *k = NULL;
        g_autofree char *l = NULL;

        g_return_val_if_fail (GDM_IS_SETTINGS_BACKEND (backend), FALSE);
        g_return_val_if_fail (key != NULL, FALSE);
        g_return_val_if_fail (value != NULL, FALSE);

        /*GDM_SETTINGS_BACKEND_CLASS (gdm_settings_desktop_backend_parent_class)->get_value (display);*/
        if (! parse_key_string (key, &g, &k, &l, NULL)) {
                g_set_error (error, GDM_SETTINGS_BACKEND_ERROR, GDM_SETTINGS_BACKEND_ERROR_KEY_NOT_FOUND, "Key not found");
                return FALSE;
        }

        old_val = g_key_file_get_value (GDM_SETTINGS_DESKTOP_BACKEND (backend)->key_file,
                                        g,
                                        k,
                                        NULL);

        /*g_debug ("Setting key: %s %s %s", g, k, l);*/
        g_key_file_set_value (GDM_SETTINGS_DESKTOP_BACKEND (backend)->key_file,
                              g,
                              k,
                              value);

        GDM_SETTINGS_DESKTOP_BACKEND (backend)->dirty = TRUE;
        queue_save (GDM_SETTINGS_DESKTOP_BACKEND (backend));

        gdm_settings_backend_value_changed (backend, key, old_val, value);

        return TRUE;
}

static void
gdm_settings_desktop_backend_class_init (GdmSettingsDesktopBackendClass *klass)
{
        GObjectClass            *object_class = G_OBJECT_CLASS (klass);
        GdmSettingsBackendClass *backend_class = GDM_SETTINGS_BACKEND_CLASS (klass);

        object_class->get_property = gdm_settings_desktop_backend_get_property;
        object_class->set_property = gdm_settings_desktop_backend_set_property;
        object_class->finalize = gdm_settings_desktop_backend_finalize;

        backend_class->get_value = gdm_settings_desktop_backend_get_value;
        backend_class->set_value = gdm_settings_desktop_backend_set_value;

        g_object_class_install_property (object_class,
                                         PROP_FILENAME,
                                         g_param_spec_string ("filename",
                                                              "File Name",
                                                              "The name of the configuration file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gdm_settings_desktop_backend_init (GdmSettingsDesktopBackend *backend)
{
}

static void
gdm_settings_desktop_backend_finalize (GObject *object)
{
        GdmSettingsDesktopBackend *backend;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SETTINGS_DESKTOP_BACKEND (object));

        backend = GDM_SETTINGS_DESKTOP_BACKEND (object);

        save_settings (backend);
        g_key_file_free (backend->key_file);
        g_free (backend->filename);

        G_OBJECT_CLASS (gdm_settings_desktop_backend_parent_class)->finalize (object);
}

GdmSettingsBackend *
gdm_settings_desktop_backend_new (const char* filename)
{
        GObject *object;

        g_return_val_if_fail (filename != NULL, NULL);

        if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR))
                return NULL;

        object = g_object_new (GDM_TYPE_SETTINGS_DESKTOP_BACKEND,
                               "filename", filename,
                               NULL);
        return GDM_SETTINGS_BACKEND (object);
}
