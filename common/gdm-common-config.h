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

#ifndef _GDM_COMMON_CONFIG_H
#define _GDM_COMMON_CONFIG_H

#include <glib.h>

G_BEGIN_DECLS

GKeyFile * gdm_common_config_load             (const char *filename,
					       GError    **error);
GKeyFile * gdm_common_config_load_from_dirs   (const char  *filename,
					       const char **dirs,
					       GError    **error);
gboolean   gdm_common_config_save             (GKeyFile   *config,
					       const char *filename,
					       GError    **error);

gboolean   gdm_common_config_get_string       (GKeyFile   *config,
					       const char *keystring,
					       char      **value,
					       GError    **error);
gboolean   gdm_common_config_get_translated_string (GKeyFile   *config,
						    const char *keystring,
						    char      **value,
						    GError    **error);
gboolean   gdm_common_config_get_string_list       (GKeyFile   *config,
						    const char *keystring,
						    char     ***value,
						    gsize      *length,
						    GError    **error);
gboolean   gdm_common_config_get_int          (GKeyFile   *config,
					       const char *keystring,
					       int        *value,
					       GError    **error);
gboolean   gdm_common_config_get_boolean      (GKeyFile   *config,
					       const char *keystring,
					       gboolean   *value,
					       GError    **error);
gboolean   gdm_common_config_parse_key_string (const char *keystring,
					       char      **group,
					       char      **key,
					       char      **locale,
					       char      **value);

void       gdm_common_config_set_string       (GKeyFile   *config,
					       const char *keystring,
					       const char *value);
void       gdm_common_config_set_boolean      (GKeyFile   *config,
					       const char *keystring,
					       gboolean    value);
void       gdm_common_config_set_int          (GKeyFile   *config,
					       const char *keystring,
					       int         value);

void       gdm_common_config_remove_key       (GKeyFile   *config,
					       const char *keystring,
					       GError    **error);

G_END_DECLS

#endif /* _GDM_COMMON_CONFIG_H */
