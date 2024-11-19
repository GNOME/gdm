/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * gdm-display-access-file.c - Abstraction around xauth cookies
 *
 * Copyright (C) 2007 Ray Strode <rstrode@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#ifdef ENABLE_X11_SUPPORT
#include <X11/Xauth.h>
#endif

#include "gdm-display-access-file.h"
#include "gdm-common.h"

struct _GdmDisplayAccessFile
{
        GObject parent;

        char *username;
        FILE *fp;
        char *path;
};

#ifndef GDM_DISPLAY_ACCESS_COOKIE_SIZE
#define GDM_DISPLAY_ACCESS_COOKIE_SIZE 16
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

static void gdm_display_access_file_finalize (GObject * object);

enum
{
        PROP_0 = 0,
        PROP_USERNAME,
        PROP_PATH
};

G_DEFINE_TYPE (GdmDisplayAccessFile, gdm_display_access_file, G_TYPE_OBJECT)

static void
gdm_display_access_file_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
        GdmDisplayAccessFile *access_file;

        access_file = GDM_DISPLAY_ACCESS_FILE (object);

        switch (prop_id) {
            case PROP_USERNAME:
                g_value_set_string (value, access_file->username);
                break;

            case PROP_PATH:
                g_value_set_string (value, access_file->path);
                break;

            default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gdm_display_access_file_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
        GdmDisplayAccessFile *access_file;

        access_file = GDM_DISPLAY_ACCESS_FILE (object);

        switch (prop_id) {
            case PROP_USERNAME:
                g_assert (access_file->username == NULL);
                access_file->username = g_value_dup_string (value);
                break;

            default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gdm_display_access_file_class_init (GdmDisplayAccessFileClass *access_file_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (access_file_class);

        object_class->finalize = gdm_display_access_file_finalize;
        object_class->get_property = gdm_display_access_file_get_property;
        object_class->set_property = gdm_display_access_file_set_property;

        param_spec = g_param_spec_string ("username",
                                          "Username",
                                          "Owner of Xauthority file",
                                          NULL,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_USERNAME, param_spec);
        param_spec = g_param_spec_string ("path",
                                          "Path",
                                          "Path to Xauthority file",
                                          NULL,
                                          G_PARAM_READABLE);
        g_object_class_install_property (object_class, PROP_PATH, param_spec);
}

static void
gdm_display_access_file_init (GdmDisplayAccessFile *access_file)
{
}

static void
gdm_display_access_file_finalize (GObject *object)
{
        GdmDisplayAccessFile *file;
        GObjectClass *parent_class;

        file = GDM_DISPLAY_ACCESS_FILE (object);
        parent_class = G_OBJECT_CLASS (gdm_display_access_file_parent_class);

        if (file->fp != NULL) {
            gdm_display_access_file_close (file);
        }
        g_assert (file->path == NULL);

        if (file->username != NULL) {
                g_free (file->username);
                file->username = NULL;
                g_object_notify (object, "username");
        }

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

GQuark
gdm_display_access_file_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gdm-display-access-file");
        }

        return error_quark;
}

GdmDisplayAccessFile *
gdm_display_access_file_new (const char *username)
{
        GdmDisplayAccessFile *access_file;
        g_return_val_if_fail (username != NULL, NULL);

        access_file = g_object_new (GDM_TYPE_DISPLAY_ACCESS_FILE,
                                    "username", username,
                                    NULL);

        return access_file;
}

static gboolean
_get_uid_and_gid_for_user (const char *username,
                           uid_t      *uid,
                           gid_t      *gid)
{
        struct passwd *passwd_entry;

        g_assert (username != NULL);
        g_assert (uid != NULL);
        g_assert (gid != NULL);

        errno = 0;
        gdm_get_pwent_for_name (username, &passwd_entry);

        if (passwd_entry == NULL) {
                return FALSE;
        }

        *uid = passwd_entry->pw_uid;
        *gid = passwd_entry->pw_gid;

        return TRUE;
}

static void
clean_up_stale_auth_subdirs (void)
{
        g_autoptr(GDir) dir = NULL;
        const char *filename;

        dir = g_dir_open (GDM_XAUTH_DIR, 0, NULL);

        if (dir == NULL) {
                return;
        }

        while ((filename = g_dir_read_name (dir)) != NULL) {
                g_autofree char *path = NULL;

                path = g_build_filename (GDM_XAUTH_DIR, filename, NULL);

                /* Will only succeed if the directory is empty
                 */
                g_rmdir (path);
        }
}

static FILE *
_create_xauth_file_for_user (const char  *username,
                             char       **filename,
                             GError     **error)
{
        g_autofree char *template = NULL;
        g_autofree char *auth_filename = NULL;
        const char *dir_name;
        int     fd;
        FILE   *fp;
        uid_t   uid;
        gid_t   gid;

        g_assert (filename != NULL);

        *filename = NULL;

        /* Create directory if not exist, then set permission 0711 and ownership root:gdm */
        if (g_file_test (GDM_XAUTH_DIR, G_FILE_TEST_IS_DIR) == FALSE) {
                g_remove (GDM_XAUTH_DIR);
                if (g_mkdir (GDM_XAUTH_DIR, 0711) != 0) {
                        g_set_error_literal (error,
                                             G_FILE_ERROR,
                                             g_file_error_from_errno (errno),
                                             g_strerror (errno));
                        return NULL;
                }

                g_chmod (GDM_XAUTH_DIR, 0711);
                if (!_get_uid_and_gid_for_user (GDM_USERNAME, &uid, &gid)) {
                        g_set_error (error,
                                     GDM_DISPLAY_ERROR,
                                     GDM_DISPLAY_ERROR_GETTING_USER_INFO,
                                     _("Could not find user “%s” on system"),
                                     GDM_USERNAME);
                        return NULL;
                }

                if (chown (GDM_XAUTH_DIR, 0, gid) != 0) {
                        g_warning ("Unable to change owner of '%s'",
                                   GDM_XAUTH_DIR);
                }
        } else {
                /* if it does exist make sure it has correct mode 0711 */
                g_chmod (GDM_XAUTH_DIR, 0711);

                /* and clean up any stale auth subdirs */
                clean_up_stale_auth_subdirs ();
        }

        if (!_get_uid_and_gid_for_user (username, &uid, &gid)) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_USER_INFO,
                             _("Could not find user “%s” on system"),
                             username);
                return NULL;
        }

        template = g_strdup_printf (GDM_XAUTH_DIR
                                    "/auth-for-%s-XXXXXX",
                                    username);

        g_debug ("GdmDisplayAccessFile: creating xauth directory %s", template);
        /* Initially create with mode 01700 then later chmod after we create database */
        errno = 0;
        dir_name = g_mkdtemp (template);
        if (dir_name == NULL) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to create temp dir from tempalte '%s': %s",
                             template,
                             g_strerror (errno));
                return NULL;
        }

        g_debug ("GdmDisplayAccessFile: chowning %s to %u:%u",
                 dir_name, (guint)uid, (guint)gid);
        errno = 0;
        if (chown (dir_name, uid, gid) < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to change permission of '%s': %s",
                             dir_name,
                             g_strerror (errno));
                return NULL;
        }

        auth_filename = g_build_filename (dir_name, "database", NULL);

        g_debug ("GdmDisplayAccessFile: creating %s", auth_filename);
        /* mode 00600 */
        errno = 0;
        fd = g_open (auth_filename,
                     O_RDWR | O_CREAT | O_EXCL | O_BINARY,
                     S_IRUSR | S_IWUSR);

        if (fd < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to open '%s': %s",
                             auth_filename,
                             g_strerror (errno));
                return NULL;
        }

        g_debug ("GdmDisplayAccessFile: chowning %s to %u:%u", auth_filename, (guint)uid, (guint)gid);
        errno = 0;
        if (fchown (fd, uid, gid) < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to change owner for '%s': %s",
                             auth_filename,
                             g_strerror (errno));
                close (fd);
                return NULL;
        }

        /* now open up permissions on per-session directory */
        g_debug ("GdmDisplayAccessFile: chmoding %s to 0711", dir_name);
        g_chmod (dir_name, 0711);

        errno = 0;
        fp = fdopen (fd, "w");
        if (fp == NULL) {
                g_set_error_literal (error,
                                     G_FILE_ERROR,
                                     g_file_error_from_errno (errno),
                                     g_strerror (errno));
                close (fd);
                return NULL;
        }

        *filename = g_steal_pointer (&auth_filename);

        /* don't close fd on purpose */
        return fp;
}

gboolean
gdm_display_access_file_open (GdmDisplayAccessFile  *file,
                              GError               **error)
{
        g_autoptr(GError) create_error = NULL;

        g_return_val_if_fail (GDM_IS_DISPLAY_ACCESS_FILE (file), FALSE);
        g_return_val_if_fail (file->fp == NULL, FALSE);
        g_return_val_if_fail (file->path == NULL, FALSE);

        file->fp = _create_xauth_file_for_user (file->username,
                                                &file->path,
                                                &create_error);

        if (file->fp == NULL) {
                g_propagate_error (error, g_steal_pointer (&create_error));
                return FALSE;
        }

        return TRUE;
}

#ifdef ENABLE_X11_SUPPORT
static void
_get_auth_info_for_display (GdmDisplayAccessFile *file,
                            GdmDisplay           *display,
                            unsigned short       *family,
                            unsigned short       *address_length,
                            char                **address,
                            unsigned short       *number_length,
                            char                **number,
                            unsigned short       *name_length,
                            char                **name)
{
        int display_number;
        gboolean is_local;

        gdm_display_is_local (display, &is_local, NULL);

        if (is_local) {
                /* We could just use FamilyWild here except xauth
                 * (and by extension su and ssh) doesn't support it yet
                 *
                 * https://bugs.freedesktop.org/show_bug.cgi?id=43425
                 */
                char localhost[_POSIX_HOST_NAME_MAX + 1] = "";
                *family = FamilyLocal;
                if (gethostname (localhost, _POSIX_HOST_NAME_MAX) == 0) {
                        *address = g_strdup (localhost);
                } else {
                        *address = g_strdup ("localhost");
                }
        } else {
                *family = FamilyWild;
                gdm_display_get_remote_hostname (display, address, NULL);
        }
        *address_length = strlen (*address);

        gdm_display_get_x11_display_number (display, &display_number, NULL);
        *number = g_strdup_printf ("%d", display_number);
        *number_length = strlen (*number);

        *name = g_strdup ("MIT-MAGIC-COOKIE-1");
        *name_length = strlen (*name);
}
#endif

gboolean
gdm_display_access_file_add_display (GdmDisplayAccessFile  *file,
                                     GdmDisplay            *display,
                                     char                 **cookie,
                                     gsize                 *cookie_size,
                                     GError               **error)
{
        g_autoptr(GError) add_error = NULL;
        gboolean display_added;

        g_return_val_if_fail (GDM_IS_DISPLAY_ACCESS_FILE (file), FALSE);
        g_return_val_if_fail (file->path != NULL, FALSE);
        g_return_val_if_fail (display != NULL, FALSE);
        g_return_val_if_fail (cookie != NULL, FALSE);

        *cookie = gdm_generate_random_bytes (GDM_DISPLAY_ACCESS_COOKIE_SIZE,
                                             &add_error);

        if (*cookie == NULL) {
                g_propagate_error (error, g_steal_pointer (&add_error));
                return FALSE;
        }

        *cookie_size = GDM_DISPLAY_ACCESS_COOKIE_SIZE;

        display_added = gdm_display_access_file_add_display_with_cookie (file, display,
                                                                         *cookie,
                                                                         *cookie_size,
                                                                         &add_error);
        if (!display_added) {
                g_free (*cookie);
                *cookie = NULL;
                g_propagate_error (error, g_steal_pointer (&add_error));
                return FALSE;
        }

        return TRUE;
}

gboolean
gdm_display_access_file_add_display_with_cookie (GdmDisplayAccessFile  *file,
                                                 GdmDisplay            *display,
                                                 const char            *cookie,
                                                 gsize                  cookie_size,
                                                 GError               **error)
{
#ifdef ENABLE_X11_SUPPORT
        Xauth auth_entry;
        gboolean display_added;

        g_return_val_if_fail (GDM_IS_DISPLAY_ACCESS_FILE (file), FALSE);
        g_return_val_if_fail (file->path != NULL, FALSE);
        g_return_val_if_fail (display != NULL, FALSE);
        g_return_val_if_fail (cookie != NULL, FALSE);

        _get_auth_info_for_display (file, display,
                                    &auth_entry.family,
                                    &auth_entry.address_length,
                                    &auth_entry.address,
                                    &auth_entry.number_length,
                                    &auth_entry.number,
                                    &auth_entry.name_length,
                                    &auth_entry.name);

        auth_entry.data = (char *) cookie;
        auth_entry.data_length = cookie_size;

        /* FIXME: We should lock the file in case the X server is
         * trying to use it, too.
         */
        if (!XauWriteAuth (file->fp, &auth_entry)
            || fflush (file->fp) == EOF) {
                g_set_error_literal (error,
                        G_FILE_ERROR,
                        g_file_error_from_errno (errno),
                        g_strerror (errno));
                display_added = FALSE;
        } else {
                display_added = TRUE;
        }

        /* If we wrote a FamilyLocal entry, we still want a FamilyWild
         * entry, because it's more resiliant against hostname changes
         *
         */
        if (auth_entry.family == FamilyLocal) {
                auth_entry.family = FamilyWild;

                if (XauWriteAuth (file->fp, &auth_entry)
                    && fflush (file->fp) != EOF) {
                        display_added = TRUE;
                }
        }

        g_free (auth_entry.address);
        g_free (auth_entry.number);
        g_free (auth_entry.name);

        return display_added;
#else
    return FALSE;
#endif
}

void
gdm_display_access_file_close (GdmDisplayAccessFile  *file)
{
        g_autofree char *auth_dir = NULL;

        g_return_if_fail (GDM_IS_DISPLAY_ACCESS_FILE (file));
        g_return_if_fail (file->fp != NULL);
        g_return_if_fail (file->path != NULL);

        errno = 0;
        if (g_unlink (file->path) != 0) {
                g_warning ("GdmDisplayAccessFile: Unable to remove X11 authority database '%s': %s",
                           file->path,
                           g_strerror (errno));
        }

        /* still try to remove dir even if file remove failed,
           may have already been removed by someone else */
        /* we own the parent directory too */
        auth_dir = g_path_get_dirname (file->path);
        if (auth_dir != NULL) {
                errno = 0;
                if (g_rmdir (auth_dir) != 0) {
                        g_warning ("GdmDisplayAccessFile: Unable to remove X11 authority directory '%s': %s",
                                   auth_dir,
                                   g_strerror (errno));
                }
        }

        g_free (file->path);
        file->path = NULL;
        g_object_notify (G_OBJECT (file), "path");

        fclose (file->fp);
        file->fp = NULL;
}

char *
gdm_display_access_file_get_path (GdmDisplayAccessFile *file)
{
        g_return_val_if_fail (GDM_IS_DISPLAY_ACCESS_FILE (file), NULL);

        return g_strdup (file->path);
}
