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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <libgnomevfs/gnome-vfs.h>

#include "gdm-user-manager.h"
#include "gdm-user-private.h"

#define GDM_USER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_MANAGER, GdmUserManagerPrivate))

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

/* Prefs Defaults */
#define DEFAULT_ALLOW_ROOT      TRUE
#define DEFAULT_MAX_ICON_SIZE   128
#define DEFAULT_USER_MAX_FILE   65536

#ifdef __sun
#define DEFAULT_MINIMAL_UID     100
#else
#define DEFAULT_MINIMAL_UID     500
#endif

#define DEFAULT_GLOBAL_FACE_DIR DATADIR "/faces"
#define DEFAULT_USER_ICON       "stock_person"
#define DEFAULT_EXCLUDE         { "bin",        \
                                  "daemon",     \
                                  "adm",        \
                                  "lp",         \
                                  "sync",       \
                                  "shutdown",   \
                                  "halt",       \
                                  "mail",       \
                                  "news",       \
                                  "uucp",       \
                                  "operator",   \
                                  "nobody",     \
                                  "gdm",        \
                                  "postgres",   \
                                  "pvm",        \
                                  "rpm",        \
                                  "nfsnobody",  \
                                  "pcap",       \
                                  NULL }

struct GdmUserManagerPrivate
{
        GHashTable            *users;
        GHashTable            *sessions;
        GHashTable            *shells;
        GHashTable            *exclusions;
        GnomeVFSMonitorHandle *passwd_monitor;
        GnomeVFSMonitorHandle *shells_monitor;
        DBusGProxy            *seat_proxy;
        char                  *seat_id;

        guint                  reload_id;
        uid_t                  minimal_uid;

        guint8                 users_dirty : 1;
};

enum {
        USER_ADDED,
        USER_REMOVED,
        USER_IS_LOGGED_IN_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_user_manager_class_init (GdmUserManagerClass *klass);
static void     gdm_user_manager_init       (GdmUserManager      *user_manager);
static void     gdm_user_manager_finalize   (GObject             *object);

static gpointer user_manager_object = NULL;

G_DEFINE_TYPE (GdmUserManager, gdm_user_manager, G_TYPE_OBJECT)

GQuark
gdm_user_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_user_manager_error");
        }

        return ret;
}

static void
on_user_sessions_changed (GdmUser        *user,
                          GdmUserManager *manager)
{
        guint nsessions;

        nsessions = gdm_user_get_num_sessions (user);

        g_debug ("GdmUserManager: sessions changed user=%s num=%d",
                 gdm_user_get_user_name (user),
                 nsessions);

        /* only signal on zero and one */
        if (nsessions > 1) {
                return;
        }

        g_signal_emit (manager, signals [USER_IS_LOGGED_IN_CHANGED], 0, user);
}

static void
on_user_icon_changed (GdmUser        *user,
                      GdmUserManager *manager)
{
        g_debug ("GdmUserManager: user icon changed");
}

static char *
get_seat_id_for_session (DBusGConnection *connection,
                         const char      *session_id)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *seat_id;
        gboolean         res;

        proxy = NULL;
        seat_id = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit seat object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSeatId",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &seat_id,
                                 G_TYPE_INVALID);
        if (! res) {
                g_debug ("Failed to identify the current seat: %s", error->message);
                g_error_free (error);
        }
 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return seat_id;
}

static void
add_sessions_for_user (GdmUserManager *manager,
                       GdmUser        *user)
{
        DBusGConnection *connection;
        DBusGProxy      *proxy;
        GError          *error;
        gboolean         res;
        guint32          uid;
        GPtrArray       *sessions;
        int              i;

        proxy = NULL;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
                g_error_free (error);
                goto out;
        }

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit manager object");
                goto out;
        }

        uid = gdm_user_get_uid (user);

        g_debug ("Getting list of sessions for user %u", uid);

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSessionsForUnixUser",
                                 &error,
                                 G_TYPE_UINT, uid,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                 &sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                g_debug ("Failed to find sessions for user: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_debug ("Found %d sessions for user %s", sessions->len, gdm_user_get_user_name (user));

        for (i = 0; i < sessions->len; i++) {
                char *ssid;
                char *sid;

                ssid = g_ptr_array_index (sessions, i);

                /* skip if on another seat */

                sid = get_seat_id_for_session (connection, ssid);
                if (sid == NULL
                    || manager->priv->seat_id == NULL
                    || strcmp (sid, manager->priv->seat_id) != 0) {
                        continue;
                }

                g_hash_table_insert (manager->priv->sessions,
                                     g_strdup (ssid),
                                     g_strdup (gdm_user_get_user_name (user)));

                _gdm_user_add_session (user, ssid);

                g_free (ssid);
        }

        g_ptr_array_free (sessions, TRUE);

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }
}

static GdmUser *
create_user (GdmUserManager *manager)
{
        GdmUser *user;

        user = g_object_new (GDM_TYPE_USER, "manager", manager, NULL);
        g_signal_connect (user,
                          "sessions-changed",
                          G_CALLBACK (on_user_sessions_changed),
                          manager);
        g_signal_connect (user,
                          "icon-changed",
                          G_CALLBACK (on_user_icon_changed),
                          manager);
        return user;
}

static void
add_user (GdmUserManager *manager,
          GdmUser        *user)
{
        add_sessions_for_user (manager, user);
        g_hash_table_insert (manager->priv->users,
                             g_strdup (gdm_user_get_user_name (user)),
                             g_object_ref (user));

        g_signal_emit (manager, signals[USER_ADDED], 0, user);
}

static GdmUser *
add_new_user_for_pwent (GdmUserManager *manager,
                        struct passwd  *pwent)
{
        GdmUser *user;

        g_debug ("Creating new user");

        user = create_user (manager);
        _gdm_user_update (user, pwent);

        add_user (manager, user);

        return user;
}

static char *
get_current_seat_id (DBusGConnection *connection)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *session_id;
        char            *seat_id;
        gboolean         res;

        proxy = NULL;
        session_id = NULL;
        seat_id = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit manager object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetCurrentSession",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 &session_id,
                                 G_TYPE_INVALID);
        if (! res) {
                g_debug ("Failed to identify the current session: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_object_unref (proxy);

        seat_id = get_seat_id_for_session (connection, session_id);

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }
        g_free (session_id);

        return seat_id;
}

static gboolean
get_uid_from_session_id (const char *session_id,
                         uid_t      *uidp)
{
        DBusGConnection *connection;
        DBusGProxy      *proxy;
        GError          *error;
        int              uid;
        gboolean         res;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit session object");
                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetUnixUser",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INT, &uid,
                                 G_TYPE_INVALID);
        g_object_unref (proxy);

        if (! res) {
                g_warning ("Failed to query the session: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        if (uidp != NULL) {
                *uidp = (uid_t) uid;
        }

        return TRUE;
}

static void
seat_session_added (DBusGProxy     *seat_proxy,
                    const char     *session_id,
                    GdmUserManager *manager)
{
        uid_t          uid;
        gboolean       res;
        struct passwd *pwent;
        GdmUser       *user;

        g_debug ("Session added: %s", session_id);

        res = get_uid_from_session_id (session_id, &uid);
        if (! res) {
                g_warning ("Unable to lookup user for session");
                return;
        }

        errno = 0;
        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("Unable to lookup user id %d: %s", (int)uid, g_strerror (errno));
                return;
        }

        user = g_hash_table_lookup (manager->priv->users, pwent->pw_name);
        if (user == NULL) {
                /* add the user */
                user = add_new_user_for_pwent (manager, pwent);
        }

        g_debug ("GdmUserManager: Session added for %d", (int)uid);
        g_hash_table_insert (manager->priv->sessions,
                             g_strdup (session_id),
                             g_strdup (gdm_user_get_user_name (user)));
        _gdm_user_add_session (user, session_id);
}

static void
seat_session_removed (DBusGProxy     *seat_proxy,
                      const char     *session_id,
                      GdmUserManager *manager)
{
        GdmUser *user;
        char    *username;

        g_debug ("Session removed: %s", session_id);

        /* since the session object may already be gone
         * we can't query CK directly */

        username = g_hash_table_lookup (manager->priv->sessions, session_id);
        if (username == NULL) {
                return;
        }

        user = g_hash_table_lookup (manager->priv->users, username);
        if (user == NULL) {
                /* nothing to do */
                return;
        }

        g_debug ("GdmUserManager: Session removed for %s", username);
        _gdm_user_remove_session (user, session_id);
}

static void
on_proxy_destroy (DBusGProxy     *proxy,
                  GdmUserManager *manager)
{
        g_debug ("GdmUserManager: seat proxy destroyed");

        manager->priv->seat_proxy = NULL;
}

static void
get_seat_proxy (GdmUserManager *manager)
{
        DBusGConnection *connection;
        DBusGProxy      *proxy;
        GError          *error;

        g_assert (manager->priv->seat_proxy == NULL);

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
                g_error_free (error);
                return;
        }

        manager->priv->seat_id = get_current_seat_id (connection);
        if (manager->priv->seat_id == NULL) {
                return;
        }

        g_debug ("GdmUserManager: Found current seat: %s", manager->priv->seat_id);

        error = NULL;
        proxy = dbus_g_proxy_new_for_name_owner (connection,
                                                 CK_NAME,
                                                 manager->priv->seat_id,
                                                 CK_SEAT_INTERFACE,
                                                 &error);

        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit seat object: %s", error->message);
                g_error_free (error);
                return;
        }

        g_signal_connect (proxy, "destroy", G_CALLBACK (on_proxy_destroy), manager);

        dbus_g_proxy_add_signal (proxy,
                                 "SessionAdded",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "SessionRemoved",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (proxy,
                                     "SessionAdded",
                                     G_CALLBACK (seat_session_added),
                                     manager,
                                     NULL);
        dbus_g_proxy_connect_signal (proxy,
                                     "SessionRemoved",
                                     G_CALLBACK (seat_session_removed),
                                     manager,
                                     NULL);
        manager->priv->seat_proxy = proxy;

}

/**
 * gdm_manager_get_user:
 * @manager: the manager to query.
 * @username: the login name of the user to get.
 *
 * Retrieves a pointer to the #GdmUser object for the login named @username
 * from @manager. This pointer is not a reference, and should not be released.
 *
 * Returns: a pointer to a #GdmUser object.
 **/
GdmUser *
gdm_user_manager_get_user (GdmUserManager *manager,
                           const char     *username)
{
        GdmUser *user;

        g_return_val_if_fail (GDM_IS_USER_MANAGER (manager), NULL);
        g_return_val_if_fail (username != NULL && username[0] != '\0', NULL);

        user = g_hash_table_lookup (manager->priv->users, username);

        if (user == NULL) {
                struct passwd *pwent;

                pwent = getpwnam (username);

                if (pwent != NULL) {
                        user = add_new_user_for_pwent (manager, pwent);
                }
        }

        return user;
}

static void
listify_hash_values_hfunc (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
        GSList **list = user_data;

        *list = g_slist_prepend (*list, value);
}

GSList *
gdm_user_manager_list_users (GdmUserManager *manager)
{
        GSList *retval;

        g_return_val_if_fail (GDM_IS_USER_MANAGER (manager), NULL);

        retval = NULL;
        g_hash_table_foreach (manager->priv->users, listify_hash_values_hfunc, &retval);

        return g_slist_sort (retval, (GCompareFunc) gdm_user_collate);
}

static void
reload_passwd (GdmUserManager *manager)
{
        struct passwd *pwent;
        GSList        *old_users;
        GSList        *new_users;
        GSList        *list;

        old_users = NULL;
        new_users = NULL;

        g_hash_table_foreach (manager->priv->users, listify_hash_values_hfunc, &old_users);
        g_slist_foreach (old_users, (GFunc) g_object_ref, NULL);

        /* Make sure we keep users who are logged in no matter what. */
        for (list = old_users; list; list = list->next) {
                if (gdm_user_get_num_sessions (list->data) > 0) {
                        g_object_freeze_notify (G_OBJECT (list->data));
                        new_users = g_slist_prepend (new_users, g_object_ref (list->data));
                }
        }

        setpwent ();

        for (pwent = getpwent (); pwent; pwent = getpwent ()) {
                GdmUser *user;

                user = NULL;

                /* Skip users below MinimalUID... */
                if (pwent->pw_uid < manager->priv->minimal_uid) {
                        continue;
                }

                /* ...And users w/ invalid shells... */
                if (!pwent->pw_shell ||
                    !g_hash_table_lookup (manager->priv->shells, pwent->pw_shell)) {
                        continue;
                }

                /* ...And explicitly excluded users */
                if (g_hash_table_lookup (manager->priv->exclusions, pwent->pw_name)) {
                        continue;
                }

                user = g_hash_table_lookup (manager->priv->users, pwent->pw_name);

                /* Update users already in the *new* list */
                if (g_slist_find (new_users, user)) {
                        _gdm_user_update (user, pwent);
                        continue;
                }

                if (user == NULL) {
                        user = create_user (manager);
                } else {
                        g_object_ref (user);
                }

                /* Freeze & update users not already in the new list */
                g_object_freeze_notify (G_OBJECT (user));
                _gdm_user_update (user, pwent);

                new_users = g_slist_prepend (new_users, user);
        }

        endpwent ();

        /* Go through and handle added users */
        for (list = new_users; list; list = list->next) {
                if (! g_slist_find (old_users, list->data)) {
                        add_user (manager, list->data);
                }
        }

        /* Go through and handle removed users */
        for (list = old_users; list; list = list->next) {
                if (! g_slist_find (new_users, list->data)) {
                        g_signal_emit (manager, signals[USER_REMOVED], 0, list->data);
                        g_hash_table_remove (manager->priv->users,
                                             gdm_user_get_user_name (list->data));
                }
        }

        /* Cleanup */
        g_slist_foreach (new_users, (GFunc) g_object_thaw_notify, NULL);
        g_slist_foreach (new_users, (GFunc) g_object_unref, NULL);
        g_slist_free (new_users);

        g_slist_foreach (old_users, (GFunc) g_object_unref, NULL);
        g_slist_free (old_users);
}

static void
reload_shells (GdmUserManager *manager)
{
        char *shell;

        setusershell ();

        g_hash_table_remove_all (manager->priv->shells);
        for (shell = getusershell (); shell; shell = getusershell ()) {
                g_hash_table_insert (manager->priv->shells,
                                     g_strdup (shell),
                                     GUINT_TO_POINTER (TRUE));
        }

        endusershell ();
}

static void
shells_monitor_cb (GnomeVFSMonitorHandle    *handle,
                   const gchar              *text_uri,
                   const gchar              *info_uri,
                   GnomeVFSMonitorEventType  event_type,
                   GdmUserManager           *manager)
{
        if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED &&
            event_type != GNOME_VFS_MONITOR_EVENT_CREATED)
                return;

        reload_shells (manager);
        reload_passwd (manager);
}

static void
passwd_monitor_cb (GnomeVFSMonitorHandle    *handle,
                   const gchar              *text_uri,
                   const gchar              *info_uri,
                   GnomeVFSMonitorEventType  event_type,
                   GdmUserManager           *manager)
{
        if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED &&
            event_type != GNOME_VFS_MONITOR_EVENT_CREATED)
                return;

        reload_passwd (manager);
}

static void
gdm_user_manager_class_init (GdmUserManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_user_manager_finalize;

        signals [USER_ADDED] =
                g_signal_new ("user-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserManagerClass, user_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_USER);
        signals [USER_REMOVED] =
                g_signal_new ("user-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserManagerClass, user_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_USER);
        signals [USER_IS_LOGGED_IN_CHANGED] =
                g_signal_new ("user-is-logged-in-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserManagerClass, user_is_logged_in_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_USER);

        g_type_class_add_private (klass, sizeof (GdmUserManagerPrivate));
}

static gboolean
reload_passwd_timeout (GdmUserManager *manager)
{
        reload_passwd (manager);
        manager->priv->reload_id = 0;
        return FALSE;
}

static void
queue_reload_passwd (GdmUserManager *manager)
{
        if (manager->priv->reload_id > 0) {
                return;
        }

        manager->priv->reload_id = g_idle_add ((GSourceFunc)reload_passwd_timeout, manager);
}

static void
gdm_user_manager_init (GdmUserManager *manager)
{
        GError        *error;
        char          *uri;
        GnomeVFSResult result;
        int            i;
        const char    *exclude_default[] = DEFAULT_EXCLUDE;

        if (! gnome_vfs_initialized ()) {
                gnome_vfs_init ();
        }

        manager->priv = GDM_USER_MANAGER_GET_PRIVATE (manager);

        manager->priv->minimal_uid = DEFAULT_MINIMAL_UID;

        /* sessions */
        manager->priv->sessions = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         g_free);

        /* exclusions */
        manager->priv->exclusions = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           g_free,
                                                           NULL);
        for (i = 0; exclude_default[i] != NULL; i++) {
                g_hash_table_insert (manager->priv->exclusions,
                                     g_strdup (exclude_default [i]),
                                     GUINT_TO_POINTER (TRUE));
        }

        /* /etc/shells */
        manager->priv->shells = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       NULL);
        reload_shells (manager);
        error = NULL;
        uri = g_filename_to_uri ("/etc/shells", NULL, &error);
        if (uri == NULL) {
                g_warning ("Could not create URI for shells file `/etc/shells': %s",
                            error->message);
                g_error_free (error);
        } else {
                result = gnome_vfs_monitor_add (&(manager->priv->shells_monitor),
                                                uri,
                                                GNOME_VFS_MONITOR_FILE,
                                                (GnomeVFSMonitorCallback)shells_monitor_cb,
                                                manager);
                g_free (uri);

                if (result != GNOME_VFS_OK)
                        g_warning ("Could not install monitor for shells file `/etc/shells': %s",
                                    gnome_vfs_result_to_string (result));
        }

        /* /etc/passwd */
        manager->priv->users = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_object_run_dispose);
        error = NULL;
        uri = g_filename_to_uri ("/etc/passwd", NULL, &error);
        if (uri == NULL) {
                g_warning ("Could not create URI for password file `/etc/passwd': %s",
                            error->message);
                g_error_free (error);
        } else {
                result = gnome_vfs_monitor_add (&(manager->priv->passwd_monitor),
                                                uri,
                                                GNOME_VFS_MONITOR_FILE,
                                                (GnomeVFSMonitorCallback)passwd_monitor_cb,
                                                manager);
                g_free (uri);

                if (result != GNOME_VFS_OK)
                        g_warning ("Could not install monitor for password file `/etc/passwd: %s",
                                   gnome_vfs_result_to_string (result));
        }

        get_seat_proxy (manager);

        queue_reload_passwd (manager);

        manager->priv->users_dirty = FALSE;

}

static void
gdm_user_manager_finalize (GObject *object)
{
        GdmUserManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_MANAGER (object));

        manager = GDM_USER_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->seat_proxy != NULL) {
                g_object_unref (manager->priv->seat_proxy);
        }

        if (manager->priv->reload_id > 0) {
                g_source_remove (manager->priv->reload_id);
                manager->priv->reload_id = 0;
        }

        g_hash_table_destroy (manager->priv->sessions);

        gnome_vfs_monitor_cancel (manager->priv->shells_monitor);
        g_hash_table_destroy (manager->priv->shells);

        gnome_vfs_monitor_cancel (manager->priv->passwd_monitor);
        g_hash_table_destroy (manager->priv->users);

        g_free (manager->priv->seat_id);

        G_OBJECT_CLASS (gdm_user_manager_parent_class)->finalize (object);
}

GdmUserManager *
gdm_user_manager_ref_default (void)
{
        if (user_manager_object != NULL) {
                g_object_ref (user_manager_object);
        } else {
                user_manager_object = g_object_new (GDM_TYPE_USER_MANAGER, NULL);
                g_object_add_weak_pointer (user_manager_object,
                                           (gpointer *) &user_manager_object);
        }

        return GDM_USER_MANAGER (user_manager_object);
}
