/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef gboolean (*GdmRecursiveFileCallback)(GFile     *file,
                                             gpointer   userdata,
                                             GError   **error);

gboolean gdm_walk_dir_recursively (GFile                     *dir,
                                   GdmRecursiveFileCallback   cb,
                                   gpointer                   userdata,
                                   GError                   **error);

gboolean gdm_chown_recursively (GFile   *dir,
                                uid_t    uid,
                                gid_t    gid,
                                GError **error);
gboolean gdm_rm_recursively (GFile   *dir,
                             GError **error);

gboolean gdm_ensure_dir (const char  *path,
                         uid_t        uid,
                         gid_t        gid,
                         mode_t       mode,
                         gboolean     recursive_chown,
                         GError     **error);

gboolean gdm_copy_dir_recursively (const char  *source,
                                   const char  *dest,
                                   GError     **error);

G_END_DECLS
