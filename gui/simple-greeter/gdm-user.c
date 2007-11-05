/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

#include "gdm-user-manager.h"
#include "gdm-user-private.h"

#define GDM_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_USER, GdmUserClass))
#define GDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_USER))
#define GDM_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), GDM_TYPE_USER, GdmUserClass))

#define GLOBAL_FACEDIR    DATADIR "/faces"
#define MAX_ICON_SIZE     128
#define MAX_FILE_SIZE     65536
#define MINIMAL_UID       100
#define RELAX_GROUP       TRUE
#define RELAX_OTHER       TRUE
#define DEFAULT_USER_ICON "stock_person"

enum {
        PROP_0,
        PROP_MANAGER,
        PROP_REAL_NAME,
        PROP_USER_NAME,
        PROP_UID,
        PROP_HOME_DIR,
        PROP_SHELL,
};

enum {
        ICON_CHANGED,
        SESSIONS_CHANGED,
        LAST_SIGNAL
};

struct _GdmUser {
        GObject         parent;

        GdmUserManager *manager;

        uid_t           uid;
        char           *user_name;
        char           *real_name;
        char           *home_dir;
        char           *shell;
        GSList         *sessions;
};

typedef struct _GdmUserClass
{
        GObjectClass parent_class;

        void (* icon_changed)     (GdmUser *user);
        void (* sessions_changed) (GdmUser *user);
} GdmUserClass;

static void gdm_user_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdmUser, gdm_user, G_TYPE_OBJECT);

void
_gdm_user_add_session (GdmUser    *user,
                       const char *ssid)
{
        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        if (! g_slist_find (user->sessions, ssid)) {
                user->sessions = g_slist_append (user->sessions, g_strdup (ssid));
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        }
}

void
_gdm_user_remove_session (GdmUser    *user,
                          const char *ssid)
{
        GSList *li;

        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_slist_find (user->sessions, ssid);
        if (li != NULL) {
                g_free (li->data);
                user->sessions = g_slist_delete_link (user->sessions, li);
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        }
}

guint
gdm_user_get_num_sessions (GdmUser    *user)
{
        return g_slist_length (user->sessions);
}

static void
gdm_user_set_property (GObject      *object,
                       guint         param_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
        GdmUser *user;

        user = GDM_USER (object);

        switch (param_id) {
        case PROP_MANAGER:
                user->manager = g_value_get_object (value);
                g_assert (user->manager);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_user_get_property (GObject    *object,
                       guint       param_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
        GdmUser *user;

        user = GDM_USER (object);

        switch (param_id) {
        case PROP_MANAGER:
                g_value_set_object (value, user->manager);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, user->user_name);
                break;
        case PROP_REAL_NAME:
                g_value_set_string (value, user->real_name);
                break;
        case PROP_HOME_DIR:
                g_value_set_string (value, user->home_dir);
                break;
        case PROP_UID:
                g_value_set_ulong (value, user->uid);
                break;
        case PROP_SHELL:
                g_value_set_string (value, user->shell);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_user_class_init (GdmUserClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->set_property = gdm_user_set_property;
        gobject_class->get_property = gdm_user_get_property;
        gobject_class->finalize = gdm_user_finalize;

        g_object_class_install_property (gobject_class,
                                         PROP_MANAGER,
                                         g_param_spec_object ("manager",
                                                              _("Manager"),
                                                              _("The user manager object this user is controlled by."),
                                                              GDM_TYPE_USER_MANAGER,
                                                              (G_PARAM_READWRITE |
                                                               G_PARAM_CONSTRUCT_ONLY)));

        g_object_class_install_property (gobject_class,
                                         PROP_REAL_NAME,
                                         g_param_spec_string ("real-name",
                                                              "Real Name",
                                                              "The real name to display for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_UID,
                                         g_param_spec_ulong ("uid",
                                                             "User ID",
                                                             "The UID for this user.",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "User Name",
                                                              "The login name for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_HOME_DIR,
                                         g_param_spec_string ("home-directory",
                                                              "Home Directory",
                                                              "The home directory for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_SHELL,
                                         g_param_spec_string ("shell",
                                                              "Shell",
                                                              "The shell for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));

        signals [ICON_CHANGED] =
                g_signal_new ("icon-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserClass, icon_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [SESSIONS_CHANGED] =
                g_signal_new ("sessions-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserClass, sessions_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
gdm_user_init (GdmUser *user)
{
        user->manager = NULL;
        user->user_name = NULL;
        user->real_name = NULL;
        user->sessions = NULL;
}

static void
gdm_user_finalize (GObject *object)
{
        GdmUser *user;

        user = GDM_USER (object);

        g_free (user->user_name);
        g_free (user->real_name);

        if (G_OBJECT_CLASS (gdm_user_parent_class)->finalize)
                (*G_OBJECT_CLASS (gdm_user_parent_class)->finalize) (object);
}

/**
 * _gdm_user_update:
 * @user: the user object to update.
 * @pwent: the user data to use.
 *
 * Updates the properties of @user using the data in @pwent.
 *
 * Since: 1.0
 **/
void
_gdm_user_update (GdmUser             *user,
                  const struct passwd *pwent)
{
        gchar *real_name;

        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (pwent != NULL);

        g_object_freeze_notify (G_OBJECT (user));

        /* Display Name */
        if (pwent->pw_gecos && pwent->pw_gecos[0] != '\0') {
                gchar *first_comma;

                first_comma = strchr (pwent->pw_gecos, ',');
                if (first_comma) {
                        real_name = g_strndup (pwent->pw_gecos,
                                                  (first_comma - pwent->pw_gecos));
                } else {
                        real_name = g_strdup (pwent->pw_gecos);
                }

                if (real_name[0] == '\0') {
                        g_free (real_name);
                        real_name = NULL;
                }
        } else {
                real_name = NULL;
        }

        if ((real_name && !user->real_name) ||
            (!real_name && user->real_name) ||
            (real_name &&
             user->real_name &&
             strcmp (real_name, user->real_name) != 0)) {
                g_free (user->real_name);
                user->real_name = real_name;
                g_object_notify (G_OBJECT (user), "real-name");
        } else {
                g_free (real_name);
        }

        /* UID */
        if (pwent->pw_uid != user->uid) {
                user->uid = pwent->pw_uid;
                g_object_notify (G_OBJECT (user), "uid");
        }

        /* Username */
        if ((pwent->pw_name && !user->user_name) ||
            (!pwent->pw_name && user->user_name) ||
            (pwent->pw_name &&
             user->user_name &&
             strcmp (user->user_name, pwent->pw_name) != 0)) {
                g_free (user->user_name);
                user->user_name = g_strdup (pwent->pw_name);
                g_object_notify (G_OBJECT (user), "user-name");
        }

        /* Home Directory */
        if ((pwent->pw_dir && !user->home_dir) ||
            (!pwent->pw_dir && user->home_dir) ||
            strcmp (user->home_dir, pwent->pw_dir) != 0) {
                g_free (user->home_dir);
                user->home_dir = g_strdup (pwent->pw_dir);
                g_object_notify (G_OBJECT (user), "home-directory");
                g_signal_emit (user, signals[ICON_CHANGED], 0);
        }

        /* Shell */
        if ((pwent->pw_shell && !user->shell) ||
            (!pwent->pw_shell && user->shell) ||
            (pwent->pw_shell &&
             user->shell &&
             strcmp (user->shell, pwent->pw_shell) != 0)) {
                g_free (user->shell);
                user->shell = g_strdup (pwent->pw_shell);
                g_object_notify (G_OBJECT (user), "shell");
        }

        g_object_thaw_notify (G_OBJECT (user));
}

/**
 * _gdm_user_icon_changed:
 * @user: the user to emit the signal for.
 *
 * Emits the "icon-changed" signal for @user.
 *
 * Since: 1.0
 **/
void
_gdm_user_icon_changed (GdmUser *user)
{
        g_return_if_fail (GDM_IS_USER (user));

        g_signal_emit (user, signals[ICON_CHANGED], 0);
}

/**
 * gdm_user_get_uid:
 * @user: the user object to examine.
 *
 * Retrieves the ID of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

uid_t
gdm_user_get_uid (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), -1);

        return user->uid;
}

/**
 * gdm_user_get_real_name:
 * @user: the user object to examine.
 *
 * Retrieves the display name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/
G_CONST_RETURN gchar *
gdm_user_get_real_name (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return (user->real_name ? user->real_name : user->user_name);
}

/**
 * gdm_user_get_user_name:
 * @user: the user object to examine.
 *
 * Retrieves the login name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
gdm_user_get_user_name (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return user->user_name;
}

/**
 * gdm_user_get_home_directory:
 * @user: the user object to examine.
 *
 * Retrieves the home directory of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
gdm_user_get_home_directory (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return user->home_dir;
}

/**
 * gdm_user_get_shell:
 * @user: the user object to examine.
 *
 * Retrieves the login shell of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
gdm_user_get_shell (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return user->shell;
}

gint
gdm_user_collate (GdmUser *user1,
                  GdmUser *user2)
{
        const gchar *str1, *str2;

        g_return_val_if_fail (user1 == NULL || GDM_IS_USER (user1), 0);
        g_return_val_if_fail (user2 == NULL || GDM_IS_USER (user2), 0);

        if (!user1 && user2)
                return -1;

        if (user1 && !user2)
                return 1;

        if (!user1 && !user2)
                return 0;

        if (user1->real_name)
                str1 = user1->real_name;
        else
                str1 = user1->user_name;

        if (user2->real_name)
                str2 = user2->real_name;
        else
                str2 = user2->user_name;

        if (!str1 && str2)
                return -1;

        if (str1 && !str2)
                return 1;

        if (!str1 && !str2)
                return 0;

        return g_utf8_collate (str1, str2);
}

static gboolean
check_user_file (const char *filename,
                 uid_t       user,
                 gssize      max_file_size,
                 gboolean    relax_group,
                 gboolean    relax_other)
{
        struct stat fileinfo;

        if (max_file_size < 0) {
                max_file_size = G_MAXSIZE;
        }

        /* Exists/Readable? */
        if (stat (filename, &fileinfo) < 0) {
                return FALSE;
        }

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode))) {
                return FALSE;
        }

        /* Owned by user? */
        if (G_UNLIKELY (fileinfo.st_uid != user)) {
                return FALSE;
        }

        /* Group not writable or relax_group? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group)) {
                return FALSE;
        }

        /* Other not writable or relax_other? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other)) {
                return FALSE;
        }

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size)) {
                return FALSE;
        }

        return TRUE;
}

static GdkPixbuf *
render_icon_from_home (GdmUser     *user,
                       GtkWidget   *widget,
                       gint         icon_size)
{
        GdkPixbuf *  retval;
        char        *path;
        GnomeVFSURI *uri;
        gboolean     is_local;
        gboolean     res;

        /* special case: look at parent of home to detect autofs
           this is so we don't try to trigger an automount */
        path = g_path_get_dirname (user->home_dir);
        uri = gnome_vfs_uri_new (path);
        is_local = gnome_vfs_uri_is_local (uri);
        gnome_vfs_uri_unref (uri);
        g_free (path);

        /* now check that home dir itself is local */
        if (is_local) {
                uri = gnome_vfs_uri_new (user->home_dir);
                is_local = gnome_vfs_uri_is_local (uri);
                gnome_vfs_uri_unref (uri);
        }

        /* only look at local home directories so we don't try to
           read from remote (e.g. NFS) volumes */
        if (!is_local) {
                return NULL;
        }

        /* First, try "~/.face" */
        path = g_build_filename (user->home_dir, ".face", NULL);
        res = check_user_file (path,
                               user->uid,
                               MAX_FILE_SIZE,
                               RELAX_GROUP,
                               RELAX_OTHER);
        if (res) {
                retval = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                retval = NULL;
        }
        g_free (path);

        /* Next, try "~/.face.icon" */
        if (retval == NULL) {
                path = g_build_filename (user->home_dir,
                                         ".face.icon",
                                         NULL);
                res = check_user_file (path,
                                       user->uid,
                                       MAX_FILE_SIZE,
                                       RELAX_GROUP,
                                       RELAX_OTHER);
                if (res) {
                        retval = gdk_pixbuf_new_from_file_at_size (path,
                                                                   icon_size,
                                                                   icon_size,
                                                                   NULL);
                } else {
                        retval = NULL;
                }

                g_free (path);
        }

        /* Still nothing, try the user's personal GDM config */
        if (retval == NULL) {
                path = g_build_filename (user->home_dir,
                                         ".gnome",
                                         "gdm",
                                         NULL);
                res = check_user_file (path,
                                       user->uid,
                                       MAX_FILE_SIZE,
                                       RELAX_GROUP,
                                       RELAX_OTHER);
                if (res) {
                        GKeyFile *keyfile;
                        char     *icon_path;

                        keyfile = g_key_file_new ();
                        g_key_file_load_from_file (keyfile,
                                                   path,
                                                   G_KEY_FILE_NONE,
                                                   NULL);

                        icon_path = g_key_file_get_string (keyfile,
                                                           "face",
                                                           "picture",
                                                           NULL);
                        res = check_user_file (icon_path,
                                               user->uid,
                                               MAX_FILE_SIZE,
                                               RELAX_GROUP,
                                               RELAX_OTHER);
                        if (icon_path && res) {
                                retval = gdk_pixbuf_new_from_file_at_size (path,
                                                                           icon_size,
                                                                           icon_size,
                                                                           NULL);
                        } else {
                                retval = NULL;
                        }

                        g_free (icon_path);
                        g_key_file_free (keyfile);
                } else {
                        retval = NULL;
                }

                g_free (path);
        }

        return retval;
}

GdkPixbuf *
gdm_user_render_icon (GdmUser   *user,
                      GtkWidget *widget,
                      gint       icon_size)
{
        GdkPixbuf    *pixbuf;
        char         *path;
        char         *tmp;
        GtkIconTheme *theme;
        gboolean      res;

        g_return_val_if_fail (GDM_IS_USER (user), NULL);
        g_return_val_if_fail (widget == NULL || GTK_IS_WIDGET (widget), NULL);
        g_return_val_if_fail (icon_size > 12, NULL);

        pixbuf = render_icon_from_home (user, widget, icon_size);

        if (pixbuf != NULL) {
                return pixbuf;
        }

        /* Try ${GlobalFaceDir}/${username} */
        path = g_build_filename (GLOBAL_FACEDIR, user->user_name, NULL);
        res = check_user_file (path,
                               user->uid,
                               MAX_FILE_SIZE,
                               RELAX_GROUP,
                               RELAX_OTHER);
        if (res) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                pixbuf = NULL;
        }

        g_free (path);
        if (pixbuf != NULL) {
                return pixbuf;
        }

        /* Finally, ${GlobalFaceDir}/${username}.png */
        tmp = g_strconcat (user->user_name, ".png", NULL);
        path = g_build_filename (GLOBAL_FACEDIR, tmp, NULL);
        g_free (tmp);
        res = check_user_file (path,
                               user->uid,
                               MAX_FILE_SIZE,
                               RELAX_GROUP,
                               RELAX_OTHER);
        if (res) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                pixbuf = NULL;
        }

        g_free (path);
        if (pixbuf != NULL) {
                return pixbuf;
        }

        /* Nothing yet, use stock icon */
        if (widget != NULL && gtk_widget_has_screen (widget)) {
                theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
        } else {
                theme = gtk_icon_theme_get_default ();
        }

        pixbuf = gtk_icon_theme_load_icon (theme,
                                           DEFAULT_USER_ICON,
                                           icon_size,
                                           0,
                                           NULL);

        if (pixbuf == NULL) {
                pixbuf = gtk_icon_theme_load_icon (theme,
                                                   GTK_STOCK_MISSING_IMAGE,
                                                   icon_size,
                                                   0,
                                                   NULL);
        }

        return pixbuf;
}
