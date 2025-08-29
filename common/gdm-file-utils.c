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

#include <sys/stat.h>

#include "gdm-file-utils.h"

gboolean
gdm_walk_dir_recursively (GFile                     *dir,
                          GdmRecursiveFileCallback   cb,
                          gpointer                   userdata,
                          GError                   **error)
{
        g_autoptr (GFileEnumerator) enumerator = NULL;
        GFileInfo *info;
        GFile *child;

        enumerator = g_file_enumerate_children (dir,
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE","
                                                G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                NULL, error);
        if (enumerator == NULL)
                return FALSE;

        while (TRUE) {
                if (!g_file_enumerator_iterate (enumerator, &info, &child, NULL, error))
                        return FALSE;

                if (info == NULL)
                        break;

                if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
                        if (!gdm_walk_dir_recursively (child, cb, userdata, error))
                                return FALSE;
                } else if (!cb (child, userdata, error))
                          return FALSE;
        }

        return cb (dir, userdata, error);
}

typedef struct {
        uid_t uid;
        gid_t gid;
} GdmRecursiveChownData;

static gboolean
chown_file (GFile   *file,
            gpointer userdata,
            GError **error)
{
        GdmRecursiveChownData *data = userdata;

        if (!g_file_set_attribute_uint32 (file,
                                          G_FILE_ATTRIBUTE_UNIX_UID, data->uid,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, error))
                return FALSE;

        if (!g_file_set_attribute_uint32 (file,
                                          G_FILE_ATTRIBUTE_UNIX_GID, data->gid,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, error))
                return FALSE;

        return TRUE;
}


gboolean
gdm_chown_recursively (GFile   *dir,
                       uid_t    uid,
                       gid_t    gid,
                       GError **error)
{
        GdmRecursiveChownData data = {
                .uid = uid,
                .gid = gid,
        };
        return gdm_walk_dir_recursively (dir, chown_file, &data, error);
}

static gboolean
rm_file (GFile     *file,
         gpointer   userdata,
         GError   **error)
{
        return g_file_delete (file, NULL, error);
}

gboolean
gdm_rm_recursively (GFile   *dir,
                    GError **error)
{
        return gdm_walk_dir_recursively (dir, rm_file, NULL, error);
}

gboolean
gdm_ensure_dir (const char  *path,
                uid_t        uid,
                gid_t        gid,
                mode_t       mode,
                gboolean     recursive_chown,
                GError     **error)
{
        g_autoptr (GFile) gio_dir = NULL;

        if (g_mkdir_with_parents (path, 0755) < 0) {
                int errsv = errno;
                g_set_error (error,
                             G_IO_ERROR,
                             g_io_error_from_errno (errsv),
                             "Failed to create directory '%s': %s",
                             path,
                             g_strerror (errsv));
                return FALSE;
        }

        if (recursive_chown) {
                g_autoptr (GFile) gio_dir = g_file_new_for_path (path);
                if (!gdm_chown_recursively (gio_dir, uid, gid, error))
                        return FALSE;
        } else {
                if (chown (path, uid, gid) < 0) {
                        int errsv = errno;
                        g_set_error (error,
                                     G_IO_ERROR,
                                     g_io_error_from_errno (errsv),
                                     "Failed to chown directory '%s': %s",
                                     path,
                                     g_strerror (errsv));
                        return FALSE;
                }
        }

        if (chmod (path, mode) < 0) {
                int errsv = errno;
                g_set_error (error,
                             G_IO_ERROR,
                             g_io_error_from_errno (errsv),
                             "Failed to chmod directory '%s': %s",
                             path,
                             g_strerror (errsv));
                return FALSE;
        }

        return TRUE;
}

static gboolean
recursive_copy_dir (GFile   *source,
                    GFile   *dest,
                    GError **error)
{
        g_autoptr (GFileEnumerator) enumerator = NULL;
        GFileInfo *info;
        GFile *source_child;

        if (!g_file_make_directory (dest, NULL, error)) {
                if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
                        return FALSE;

                /* It's not an error if the directory exists */
                g_clear_error (error);
        }

        enumerator = g_file_enumerate_children (source,
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE","
                                                G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                NULL, error);
        if (enumerator == NULL)
                return FALSE;

        while (TRUE) {
                g_autoptr (GFile) dest_child = NULL;

                if (!g_file_enumerator_iterate (enumerator, &info, &source_child, NULL, error))
                        return FALSE;

                if (info == NULL)
                        break;

                dest_child = g_file_get_child (dest, g_file_info_get_name (info));

                if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
                        if (!recursive_copy_dir (source_child, dest_child, error))
                                return FALSE;
                } else if (!g_file_copy (source_child, dest_child,
                                         G_FILE_COPY_NOFOLLOW_SYMLINKS |
                                         G_FILE_COPY_ALL_METADATA |
                                         G_FILE_COPY_OVERWRITE,
                                         NULL, NULL, NULL, error))
                        return FALSE;
        }

        return TRUE;
}

gboolean
gdm_copy_dir_recursively (const char *source,
                          const char *dest,
                          GError **error)
{
        g_autoptr (GFile) gio_source = NULL;
        g_autoptr (GFile) gio_dest = NULL;

        gio_source = g_file_new_for_path (source);
        gio_dest = g_file_new_for_path (dest);

        if (!recursive_copy_dir (gio_source, gio_dest, error)) {
                g_autoptr (GError) inner_error = NULL;

                if (!gdm_rm_recursively (gio_dest, &inner_error)) {
                        g_warning ("Failed to clean up '%s' after failed recursive copy: %s",
                                   dest, inner_error->message);
                }

                return FALSE;
        }

        return TRUE;
}
