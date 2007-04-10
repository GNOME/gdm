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

#ifndef _GDM_CONFIG_H
#define _GDM_CONFIG_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GdmConfig GdmConfig;

typedef enum {
        GDM_CONFIG_VALUE_INVALID,
        GDM_CONFIG_VALUE_BOOL,
        GDM_CONFIG_VALUE_INT,
        GDM_CONFIG_VALUE_STRING,
        GDM_CONFIG_VALUE_LOCALE_STRING,
        GDM_CONFIG_VALUE_STRING_ARRAY,
        GDM_CONFIG_VALUE_LOCALE_STRING_ARRAY,
} GdmConfigValueType;

typedef enum {
        GDM_CONFIG_SOURCE_INVALID,
        GDM_CONFIG_SOURCE_MANDATORY,
        GDM_CONFIG_SOURCE_DEFAULT,
        GDM_CONFIG_SOURCE_CUSTOM,
        GDM_CONFIG_SOURCE_BUILT_IN,
        GDM_CONFIG_SOURCE_RUNTIME_USER,
} GdmConfigSourceType;

#define GDM_CONFIG_INVALID_ID -1

struct _GdmConfigValue
{
	GdmConfigValueType type;
};

typedef struct _GdmConfigValue GdmConfigValue;

typedef gboolean (* GdmConfigFunc) (GdmConfig          *config,
				    GdmConfigSourceType source,
				    const char         *group,
				    const char         *key,
				    GdmConfigValue     *value,
				    int                 id,
				    gpointer            data);

typedef struct {
	char              *group;
	char              *key;
	GdmConfigValueType type;
	char              *default_value;
	int                id;
} GdmConfigEntry;

#define GDM_CONFIG_ERROR (gdm_config_error_quark ())

typedef enum
{
	GDM_CONFIG_ERROR_UNKNOWN_OPTION,
	GDM_CONFIG_ERROR_BAD_VALUE,
	GDM_CONFIG_ERROR_PARSE_ERROR,
	GDM_CONFIG_ERROR_FAILED
} GdmConfigError;

GQuark                 gdm_config_error_quark            (void);

GdmConfig *            gdm_config_new                    (void);
void                   gdm_config_free                   (GdmConfig       *config);

void                   gdm_config_set_validate_func      (GdmConfig       *config,
							  GdmConfigFunc    func,
							  gpointer         data);
void                   gdm_config_set_notify_func        (GdmConfig       *config,
							  GdmConfigFunc    func,
							  gpointer         data);
void                   gdm_config_set_default_file       (GdmConfig       *config,
							  const char      *name);
void                   gdm_config_set_mandatory_file     (GdmConfig       *config,
							  const char      *name);
void                   gdm_config_set_custom_file        (GdmConfig       *config,
							  const char      *name);
void                   gdm_config_add_entry              (GdmConfig            *config,
							  const GdmConfigEntry *entry);
void                   gdm_config_add_static_entries     (GdmConfig            *config,
							  const GdmConfigEntry *entries);
const GdmConfigEntry * gdm_config_lookup_entry           (GdmConfig            *config,
							  const char           *group,
							  const char           *key);
const GdmConfigEntry * gdm_config_lookup_entry_for_id    (GdmConfig            *config,
							  int                   id);

gboolean               gdm_config_load                   (GdmConfig             *config,
							  GError               **error);
gboolean               gdm_config_process_all            (GdmConfig             *config,
							  GError               **error);
gboolean               gdm_config_process_entry          (GdmConfig             *config,
							  const GdmConfigEntry  *entry,
							  GError               **error);
gboolean               gdm_config_process_entries        (GdmConfig             *config,
							  const GdmConfigEntry **entries,
							  gsize                  n_entries,
							  GError               **error);

gboolean               gdm_config_save_custom_file       (GdmConfig       *config,
							  GError         **error);
char **                gdm_config_get_keys_for_group     (GdmConfig       *config,
							  const gchar     *group_name,
							  gsize           *length,
							  GError         **error);

gboolean               gdm_config_peek_value             (GdmConfig             *config,
							  const char            *group,
							  const char            *key,
							  const GdmConfigValue **value);
gboolean               gdm_config_get_value              (GdmConfig       *config,
							  const char      *group,
							  const char      *key,
							  GdmConfigValue **value);
gboolean               gdm_config_set_value              (GdmConfig       *config,
							  const char      *group,
							  const char      *key,
							  GdmConfigValue  *value);

/* convenience functions */
gboolean               gdm_config_get_value_for_id       (GdmConfig       *config,
							  int              id,
							  GdmConfigValue **value);
gboolean               gdm_config_set_value_for_id       (GdmConfig       *config,
							  int              id,
							  GdmConfigValue  *value);

gboolean               gdm_config_peek_string_for_id     (GdmConfig       *config,
							  int              id,
							  const char     **str);
gboolean               gdm_config_get_string_for_id      (GdmConfig       *config,
							  int              id,
							  char           **str);
gboolean               gdm_config_get_bool_for_id        (GdmConfig       *config,
							  int              id,
							  gboolean        *bool);
gboolean               gdm_config_get_int_for_id         (GdmConfig       *config,
							  int              id,
							  int             *integer);
gboolean               gdm_config_set_string_for_id      (GdmConfig       *config,
							  int              id,
							  char            *str);
gboolean               gdm_config_set_bool_for_id        (GdmConfig       *config,
							  int              id,
							  gboolean         bool);
gboolean               gdm_config_set_int_for_id         (GdmConfig       *config,
							  int              id,
							  int              integer);

/* Config Values */

GdmConfigValue *     gdm_config_value_new              (GdmConfigValueType    type);
void                 gdm_config_value_free             (GdmConfigValue       *value);
GdmConfigValue *     gdm_config_value_copy             (const GdmConfigValue *value);
int                  gdm_config_value_compare          (const GdmConfigValue *value_a,
							const GdmConfigValue *value_b);

GdmConfigValue *     gdm_config_value_new_from_string  (GdmConfigValueType    type,
							const char           *str,
							GError              **error);
const char *         gdm_config_value_get_string       (const GdmConfigValue *value);
const char *         gdm_config_value_get_locale_string       (const GdmConfigValue *value);
int                  gdm_config_value_get_int          (const GdmConfigValue *value);
gboolean             gdm_config_value_get_bool         (const GdmConfigValue *value);

void                 gdm_config_value_set_string       (GdmConfigValue  *value,
							const char      *str);
void                 gdm_config_value_set_locale_string       (GdmConfigValue  *value,
							       const char      *str);

void                 gdm_config_value_set_string_array (GdmConfigValue  *value,
							const char     **array);
void                 gdm_config_value_set_locale_string_array (GdmConfigValue  *value,
							       const char     **array);
void                 gdm_config_value_set_int          (GdmConfigValue  *value,
							int              integer);
void                 gdm_config_value_set_bool         (GdmConfigValue  *value,
							gboolean         bool);
char *               gdm_config_value_to_string        (const GdmConfigValue *value);

/* Config Entries */
GdmConfigEntry *     gdm_config_entry_copy             (const GdmConfigEntry *entry);
void                 gdm_config_entry_free             (GdmConfigEntry       *entry);

G_END_DECLS

#endif /* _GDM_CONFIG_H */
