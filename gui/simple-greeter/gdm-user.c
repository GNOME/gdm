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
#include <glib/gi18n.h>
#include <gtk/gtkicontheme.h>

#include "gdm-user-manager.h"
#include "gdm-user-private.h"

#define GDM_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_USER, GdmUserClass))
#define GDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_USER))
#define GDM_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), GDM_TYPE_USER, GdmUserClass))

enum {
        PROP_0,
        PROP_MANAGER,
        PROP_REAL_NAME,
        PROP_USER_NAME,
        PROP_UID,
        PROP_HOME_DIR,
        PROP_SHELL,
        PROP_SESSIONS
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
        gchar          *user_name;
        gchar          *real_name;
        gchar          *home_dir;
        gchar          *shell;
        GSList         *sessions;
};

typedef struct _GdmUserClass
{
        GObjectClass parent_class;

        void (* icon_changed)     (GdmUser *user);
        void (* sessions_changed) (GdmUser *user);
} GdmUserClass;

/* GObject Functions */
static void gdm_user_set_property (GObject      *object,
                                   guint         param_id,
                                   const GValue *value,
                                   GParamSpec   *pspec);
static void gdm_user_get_property (GObject      *object,
                                   guint         param_id,
                                   GValue       *value,
                                   GParamSpec   *pspec);
static void gdm_user_finalize     (GObject      *object);


static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdmUser, gdm_user, G_TYPE_OBJECT);

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
        case PROP_SESSIONS:
                if (user->sessions) {
                        GValueArray *ar;
                        GSList      *list;
                        GValue       tmp = { 0 };

                        ar = g_value_array_new (g_slist_length (user->sessions));
                        g_value_init (&tmp, GDM_TYPE_USER);
                        for (list = user->sessions; list; list = list->next) {
                                g_value_set_object (&tmp, list->data);
                                g_value_array_append (ar, &tmp);
                                g_value_reset (&tmp);
                        }

                        g_value_take_boxed (value, ar);
                } else {
                        g_value_set_boxed (value, NULL);
                }

                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
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

/**
 * _gdm_user_remove_session:
 * @user: the user to modify.
 * @ssid: the session id to remove
 *
 * Removes @ssid from the list of sessions @user is using, and emits the
 * "sessions-changed" signal, if necessary.
 *
 * Since: 1.0
 **/
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

/**
 * gdm_user_get_displays:
 * @user: the user object to examine.
 * 
 * Retrieves a new list of the displays that @user is logged in on. The list
 * itself must be freed with g_slist_free() when no longer needed.
 * 
 * Returns: a list of #GdmDisplay objects which must be freed with
 *  g_slist_free().
 * 
 * Since: 1.0
 **/
GSList *
gdm_user_get_sessions (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return g_slist_copy (user->sessions);
}

/**
 * gdm_user_get_n_sessions:
 * @user: the user object to examine.
 * 
 * Retrieves the number of sessions that @user is logged into.
 * 
 * Returns: an unsigned integer.
 * 
 * Since: 1.0
 **/
guint
gdm_user_get_n_sessions (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), FALSE);

        return g_slist_length (user->sessions);
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
