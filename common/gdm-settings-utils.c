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

#include "gdm-settings-utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

struct _GdmSettingsEntry
{
        char   *key;
        char   *signature;
        char   *default_value;
        char   *value;
};

GdmSettingsEntry *
gdm_settings_entry_new (void)
{
        GdmSettingsEntry *entry = NULL;

        entry = g_new0 (GdmSettingsEntry, 1);
        entry->key = NULL;
        entry->signature = NULL;
        entry->value = NULL;
        entry->default_value = NULL;

        return entry;
}

const char *
gdm_settings_entry_get_key (GdmSettingsEntry *entry)
{
        g_return_val_if_fail (entry != NULL, NULL);

        return entry->key;
}

const char *
gdm_settings_entry_get_signature (GdmSettingsEntry *entry)
{
        g_return_val_if_fail (entry != NULL, NULL);

        return entry->signature;
}

const char *
gdm_settings_entry_get_default_value (GdmSettingsEntry *entry)
{
        g_return_val_if_fail (entry != NULL, NULL);

        return entry->default_value;
}

void
gdm_settings_entry_free (GdmSettingsEntry *entry)
{
        g_return_if_fail (entry != NULL);

        g_free (entry->key);
        g_free (entry->signature);
        g_free (entry->default_value);
        g_free (entry->value);
        g_free (entry);
}

typedef struct {
        GSList                 *list;
        GdmSettingsEntry       *entry;
        gboolean                in_key;
        gboolean                in_signature;
        gboolean                in_default;
} ParserInfo;

static void
start_element_cb (GMarkupParseContext *ctx,
                  const char          *element_name,
                  const char         **attribute_names,
                  const char         **attribute_values,
                  gpointer             user_data,
                  GError             **error)
{
        ParserInfo *info;

        info = (ParserInfo *) user_data;

        /*g_debug ("parsing start: '%s'", element_name);*/

        if (strcmp (element_name, "schema") == 0) {
                info->entry = gdm_settings_entry_new ();
        } else if (strcmp (element_name, "key") == 0) {
                info->in_key = TRUE;
        } else if (strcmp (element_name, "signature") == 0) {
                info->in_signature = TRUE;
        } else if (strcmp (element_name, "default") == 0) {
                info->in_default = TRUE;
        }
}

static void
add_schema_entry (ParserInfo *info)
{
        /*g_debug ("Inserting entry %s", info->entry->key);*/

        info->list = g_slist_prepend (info->list, info->entry);
}

static void
end_element_cb (GMarkupParseContext *ctx,
                const char          *element_name,
                gpointer             user_data,
                GError             **error)
{
        ParserInfo *info;

        info = (ParserInfo *) user_data;

        /*g_debug ("parsing end: '%s'", element_name);*/

        if (strcmp (element_name, "schema") == 0) {
                add_schema_entry (info);
        } else if (strcmp (element_name, "key") == 0) {
                info->in_key = FALSE;
        } else if (strcmp (element_name, "signature") == 0) {
                info->in_signature = FALSE;
        } else if (strcmp (element_name, "default") == 0) {
                info->in_default = FALSE;
        }
}

static void
text_cb (GMarkupParseContext *ctx,
         const char          *text,
         gsize                text_len,
         gpointer             user_data,
         GError             **error)
{
        ParserInfo *info;
        char       *t;

        info = (ParserInfo *) user_data;

        t = g_strndup (text, text_len);

        if (info->in_key) {
                info->entry->key = g_strdup (t);
        } else if (info->in_signature) {
                info->entry->signature = g_strdup (t);
        } else if (info->in_default) {
                info->entry->default_value = g_strdup (t);
        }

        g_free (t);

}

static void
error_cb (GMarkupParseContext *ctx,
          GError              *error,
          gpointer             user_data)
{
}

static GMarkupParser parser = {
        start_element_cb,
        end_element_cb,
        text_cb,
        NULL,
        error_cb
};

gboolean
gdm_settings_parse_schemas (const char  *file,
                            const char  *root,
                            GSList     **schemas)
{
        GMarkupParseContext *ctx;
        ParserInfo          *info;
        char                *contents;
        gsize                len;
        GError              *error = NULL;
        gboolean             res;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (root != NULL, FALSE);
        g_return_val_if_fail (schemas != NULL, FALSE);

        contents = NULL;
        error = NULL;
        res = g_file_get_contents (file, &contents, &len, &error);
        if (! res) {
                g_warning ("Unable to read schemas file: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        info = g_new0 (ParserInfo, 1);
        ctx = g_markup_parse_context_new (&parser, 0, info, NULL);
        g_markup_parse_context_parse (ctx, contents, len, NULL);

        *schemas = info->list;

        g_markup_parse_context_free (ctx);
        g_free (info);
        g_free (contents);

        return TRUE;
}

/* adapted from GKeyFile */
gboolean
gdm_settings_parse_value_as_boolean (const char *value,
                                     gboolean   *boolval)
{
        g_return_val_if_fail (value != NULL, FALSE);
        g_return_val_if_fail (boolval != NULL, FALSE);

        if (g_ascii_strcasecmp (value, "true") == 0 || strcmp (value, "1") == 0) {
                *boolval = TRUE;
                return TRUE;
        } else if (g_ascii_strcasecmp (value, "false") == 0 || strcmp (value, "0") == 0) {
                *boolval = FALSE;
                return TRUE;
        } else {
                return FALSE;
        }
}

gboolean
gdm_settings_parse_value_as_integer (const char *value,
                                     int        *intval)
{
        char *end_of_valid_int;
        glong long_value;
        gint  int_value;

        g_return_val_if_fail (value != NULL, FALSE);
        g_return_val_if_fail (intval != NULL, FALSE);

        errno = 0;
        long_value = strtol (value, &end_of_valid_int, 10);

        if (*value == '\0' || *end_of_valid_int != '\0') {
                return FALSE;
        }

        int_value = long_value;
        if (int_value != long_value || errno == ERANGE) {
                return FALSE;
        }

        *intval = int_value;

        return TRUE;
}
