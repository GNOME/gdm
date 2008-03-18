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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <X11/Xauth.h>

#include "gdm-display-access-file.h"
#include "gdm-common.h"

struct _GdmDisplayAccessFilePrivate
{
        char *username;
        FILE *fp;
        char *path;
};

#ifndef GDM_DISPLAY_ACCESS_COOKIE_SIZE
#define GDM_DISPLAY_ACCESS_COOKIE_SIZE 16
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
                g_value_set_string (value, access_file->priv->username);
                break;

            case PROP_PATH:
                g_value_set_string (value, access_file->priv->path);
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
                g_assert (access_file->priv->username == NULL);
                access_file->priv->username = g_value_dup_string (value);
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
        g_type_class_add_private (access_file_class, sizeof (GdmDisplayAccessFilePrivate));
}

static void
gdm_display_access_file_init (GdmDisplayAccessFile *access_file)
{
        access_file->priv = G_TYPE_INSTANCE_GET_PRIVATE (access_file,
                                                         GDM_TYPE_DISPLAY_ACCESS_FILE,
                                                         GdmDisplayAccessFilePrivate);
}

static void
gdm_display_access_file_finalize (GObject *object)
{
        GdmDisplayAccessFile *file;
        GObjectClass *parent_class;

        file = GDM_DISPLAY_ACCESS_FILE (object);
        parent_class = G_OBJECT_CLASS (gdm_display_access_file_parent_class);

        if (file->priv->fp != NULL) {
            gdm_display_access_file_close (file);
        }
        g_assert (file->priv->path == NULL);

        if (file->priv->username != NULL) {
                g_free (file->priv->username);
                file->priv->username = NULL;
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
        passwd_entry = getpwnam (username);

        if (passwd_entry == NULL) {
                return FALSE;
        }

        *uid = passwd_entry->pw_uid;
        *gid = passwd_entry->pw_gid;

        return TRUE;
}

static FILE *
_create_xauth_file_for_user (const char  *username,
                             char       **filename,
                             GError     **error)
{
        char   *template;
        GError *open_error;
        int     fd;
        FILE   *fp;
        uid_t   uid;
        gid_t   gid;

        fp = NULL;

        template = g_strdup_printf (".gdm-xauth-%s.XXXXXX", username);

        open_error = NULL;
        fd = g_file_open_tmp (template, filename, &open_error);
        g_free (template);

        if (fd < 0) {
                g_propagate_error (error, open_error);
                goto out;
        }

        if (!_get_uid_and_gid_for_user (username, &uid, &gid)) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_USER_INFO,
                             _("could not find user \"%s\" on system"),
                             username);
                close (fd);
                fd = -1;
                goto out;

        }

        if (fchown (fd, uid, gid) < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                close (fd);
                fd = -1;
                goto out;
        }

        fp = fdopen (fd, "w");

        if (fp == NULL) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                close (fd);
                fd = -1;
                goto out;
        }
out:
        return fp;
}

gboolean
gdm_display_access_file_open (GdmDisplayAccessFile  *file,
                              GError               **error)
{
        GError *create_error;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->fp == NULL, FALSE);
        g_return_val_if_fail (file->priv->path == NULL, FALSE);

        create_error = NULL;
        file->priv->fp = _create_xauth_file_for_user (file->priv->username,
                                                      &file->priv->path,
                                                      &create_error);

        if (file->priv->fp == NULL) {
                g_propagate_error (error, create_error);
                return FALSE;
        }

        return TRUE;
}

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
                *family = FamilyLocal;
                *address = g_strdup (g_get_host_name ());
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

gboolean
gdm_display_access_file_add_display (GdmDisplayAccessFile  *file,
                                     GdmDisplay            *display,
                                     char                 **cookie,
                                     gsize                 *cookie_size,
                                     GError               **error)
{
        GError  *add_error;
        gboolean display_added;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);
        g_return_val_if_fail (cookie != NULL, FALSE);

        add_error = NULL;
        *cookie = gdm_generate_random_bytes (GDM_DISPLAY_ACCESS_COOKIE_SIZE,
                                             &add_error);

        if (*cookie == NULL) {
                g_propagate_error (error, add_error);
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
                g_propagate_error (error, add_error);
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
        Xauth auth_entry;
        gboolean display_added;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);
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
        if (!XauWriteAuth (file->priv->fp, &auth_entry)
            || fflush (file->priv->fp) == EOF) {
                g_set_error (error,
                        G_FILE_ERROR,
                        g_file_error_from_errno (errno),
                        "%s", g_strerror (errno));
                display_added = FALSE;
        } else {
                display_added = TRUE;
        }


        g_free (auth_entry.address);
        g_free (auth_entry.number);
        g_free (auth_entry.name);

        return display_added;
}

gboolean
gdm_display_access_file_remove_display (GdmDisplayAccessFile  *file,
                                        GdmDisplay            *display,
                                        GError               **error)
{
        Xauth           *auth_entry;
        unsigned short  family;
        unsigned short  address_length;
        char           *address;
        unsigned short  number_length;
        char           *number;
        unsigned short  name_length;
        char           *name;


        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);

        _get_auth_info_for_display (file, display,
                                    &family,
                                    &address_length,
                                    &address,
                                    &number_length,
                                    &number,
                                    &name_length,
                                    &name);

        auth_entry = XauGetAuthByAddr (family,
                                       address_length,
                                       address,
                                       number_length,
                                       number,
                                       name_length,
                                       name);
        g_free (address);
        g_free (number);
        g_free (name);

        if (auth_entry == NULL) {
                g_set_error (error,
                             GDM_DISPLAY_ACCESS_FILE_ERROR,
                             GDM_DISPLAY_ACCESS_FILE_ERROR_FINDING_AUTH_ENTRY,
                             "could not find authorization entry");
                return FALSE;
        }

        XauDisposeAuth (auth_entry);

        if (fflush (file->priv->fp) == EOF) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                return FALSE;
        }

        return TRUE;
}

void
gdm_display_access_file_close (GdmDisplayAccessFile  *file)
{
        g_return_if_fail (file != NULL);
        g_return_if_fail (file->priv->fp != NULL);
        g_return_if_fail (file->priv->path != NULL);

        g_unlink (file->priv->path);
        if (file->priv->path != NULL) {
                g_free (file->priv->path);
                file->priv->path = NULL;
                g_object_notify (G_OBJECT (file), "path");
        }

        fclose (file->priv->fp);
        file->priv->fp = NULL;
}

char *
gdm_display_access_file_get_path (GdmDisplayAccessFile *access_file)
{
        return g_strdup (access_file->priv->path);
}
