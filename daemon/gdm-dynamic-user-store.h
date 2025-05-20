/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright Red Hat
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
 */

#pragma once

#include <sys/types.h>

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GDM_TYPE_DYNAMIC_USER_STORE (gdm_dynamic_user_store_get_type ())
G_DECLARE_FINAL_TYPE (GdmDynamicUserStore, gdm_dynamic_user_store, GDM, DYNAMIC_USER_STORE, GObject)

typedef enum
{
  GDM_DYNAMIC_USER_STORE_ERROR_NO_FREE_UID,
  GDM_DYNAMIC_USER_STORE_ERROR_NO_SUCH_GROUP,
} GdmDynamicUserStoreError;

#define GDM_DYNAMIC_USER_STORE_ERROR (gdm_dynamic_user_store_error_quark ())

GQuark               gdm_dynamic_user_store_error_quark (void);

GdmDynamicUserStore *gdm_dynamic_user_store_new         (void);

gboolean             gdm_dynamic_user_store_create      (GdmDynamicUserStore  *store,
                                                         const char           *preferred_username,
                                                         const char           *display_name,
                                                         const char           *member_of,
                                                         char                **ret_username,
                                                         uid_t                *ret_uid,
                                                         char                **ret_home,
                                                         GError              **error);

void                 gdm_dynamic_user_store_remove      (GdmDynamicUserStore *store,
                                                         uid_t                uid);

G_END_DECLS
