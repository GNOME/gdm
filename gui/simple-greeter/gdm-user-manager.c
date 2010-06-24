/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
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

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-user-manager.h"
#include "gdm-user-private.h"

#define GDM_USER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_MANAGER, GdmUserManagerPrivate))

#define CK_NAME      "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

/* Prefs Defaults */

#ifdef __sun
#define FALLBACK_MINIMAL_UID     100
#else
#define FALLBACK_MINIMAL_UID     500
#endif

#ifndef _PATH_SHELLS
#define _PATH_SHELLS    "/etc/shells"
#endif
#define PATH_PASSWD     "/etc/passwd"

#ifndef GDM_USERNAME
#define GDM_USERNAME "gdm"
#endif

#define RELOAD_PASSWD_THROTTLE_SECS 5

/* approximately two months */
#define LOGIN_FREQUENCY_TIME_WINDOW_SECS (60 * 24 * 60 * 60)

#define ACCOUNTS_NAME      "org.freedesktop.Accounts"
#define ACCOUNTS_PATH      "/org/freedesktop/Accounts"
#define ACCOUNTS_INTERFACE "org.freedesktop.Accounts"

struct GdmUserManagerPrivate
{
        GHashTable            *users_by_name;
        GHashTable            *users_by_object_path;
        GHashTable            *sessions;
        GHashTable            *shells;
        DBusGConnection       *connection;
        DBusGProxy            *seat_proxy;
        DBusGProxy            *accounts_proxy;
        char                  *seat_id;

        GFileMonitor          *passwd_monitor;
        GFileMonitor          *shells_monitor;

        GSList                *exclude_usernames;
        GSList                *include_usernames;
        gboolean               include_all;

        gboolean               load_passwd_pending;

        guint                  load_id;
        guint                  reload_passwd_id;
        guint                  ck_history_id;
        guint                  ck_history_watchdog_id;
        GPid                   ck_history_pid;

        gboolean               is_loaded;
        gboolean               has_multiple_users;
};

enum {
        PROP_0,
        PROP_INCLUDE_ALL,
        PROP_INCLUDE_USERNAMES_LIST,
        PROP_EXCLUDE_USERNAMES_LIST,
        PROP_IS_LOADED,
        PROP_HAS_MULTIPLE_USERS
};

enum {
        USER_ADDED,
        USER_REMOVED,
        USER_IS_LOGGED_IN_CHANGED,
        USER_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_user_manager_class_init (GdmUserManagerClass *klass);
static void     gdm_user_manager_init       (GdmUserManager      *user_manager);
static void     gdm_user_manager_finalize   (GObject             *object);

static void load_users_manually (GdmUserManager *manager);
static void monitor_local_users (GdmUserManager *manager);

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

static gboolean
start_new_login_session (GdmUserManager *manager)
{
        GError  *error;
        gboolean res;

        res = g_spawn_command_line_async ("gdmflexiserver -s", &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Unable to start new login: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to start new login");
                }
        }

        return res;
}

static gboolean
activate_session_id (GdmUserManager *manager,
                     const char     *seat_id,
                     const char     *session_id)
{
        DBusError    local_error;
        DBusMessage *message;
        DBusMessage *reply;
        gboolean     ret;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                seat_id,
                                                "org.freedesktop.ConsoleKit.Seat",
                                                "ActivateSession");
        if (message == NULL) {
                goto out;
        }

        if (! dbus_message_append_args (message,
                                        DBUS_TYPE_OBJECT_PATH, &session_id,
                                        DBUS_TYPE_INVALID)) {
                goto out;
        }


        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (dbus_g_connection_get_connection (manager->priv->connection),
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to activate session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        ret = TRUE;
 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static gboolean
session_is_login_window (GdmUserManager *manager,
                         const char     *session_id)
{
        DBusGProxy      *proxy;
        GError          *error;
        gboolean         res;
        gboolean         ret;
        char            *session_type;

        ret = FALSE;

        proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit seat object");
                goto out;
        }

        session_type = NULL;
        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSessionType",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &session_type,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("GdmUserManager: Failed to identify the session type: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("GdmUserManager: Failed to identify the session type");
                }
                goto out;
        }

        if (session_type == NULL || session_type[0] == '\0' || strcmp (session_type, "LoginWindow") != 0) {
                goto out;
        }

        ret = TRUE;

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return ret;
}

static char *
_get_login_window_session_id (GdmUserManager *manager)
{
        gboolean    res;
        gboolean    can_activate_sessions;
        GError     *error;
        GPtrArray  *sessions;
        char       *primary_ssid;
        int         i;

        if (manager->priv->seat_id == NULL || manager->priv->seat_id[0] == '\0') {
                g_debug ("GdmUserManager: display seat ID is not set; can't switch sessions");
                return NULL;
        }

        primary_ssid = NULL;
        sessions = NULL;

        can_activate_sessions = gdm_user_manager_can_switch (manager);

        if (! can_activate_sessions) {
                g_debug ("GdmUserManager: seat is unable to activate sessions");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (manager->priv->seat_proxy,
                                 "GetSessions",
                                 &error,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("unable to determine sessions for user: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("unable to determine sessions for user");
                }
                goto out;
        }

        for (i = 0; i < sessions->len; i++) {
                char *ssid;

                ssid = g_ptr_array_index (sessions, i);

                if (session_is_login_window (manager, ssid)) {
                        primary_ssid = g_strdup (ssid);
                        break;
                }
        }
        g_ptr_array_foreach (sessions, (GFunc)g_free, NULL);
        g_ptr_array_free (sessions, TRUE);

 out:

        return primary_ssid;
}

gboolean
gdm_user_manager_goto_login_session (GdmUserManager *manager)
{
        gboolean ret;
        gboolean res;
        char    *ssid;

        g_return_val_if_fail (GDM_IS_USER_MANAGER (manager), FALSE);

        ret = FALSE;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        ssid = _get_login_window_session_id (manager);
        if (ssid != NULL) {
                res = activate_session_id (manager, manager->priv->seat_id, ssid);
                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret) {
                res = start_new_login_session (manager);
                if (res) {
                        ret = TRUE;
                }
        }

        return ret;
}

gboolean
gdm_user_manager_can_switch (GdmUserManager *manager)
{
        gboolean    res;
        gboolean    can_activate_sessions;
        GError     *error;

        if (manager->priv->seat_id == NULL || manager->priv->seat_id[0] == '\0') {
                g_debug ("GdmUserManager: display seat ID is not set; can't switch sessions");
                return FALSE;
        }

        g_debug ("GdmUserManager: checking if seat can activate sessions");

        error = NULL;
        res = dbus_g_proxy_call (manager->priv->seat_proxy,
                                 "CanActivateSessions",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_BOOLEAN, &can_activate_sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("unable to determine if seat can activate sessions: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("unable to determine if seat can activate sessions");
                }
                return FALSE;
        }

        return can_activate_sessions;
}

gboolean
gdm_user_manager_activate_user_session (GdmUserManager *manager,
                                        GdmUser        *user)
{
        gboolean ret;
        const char *ssid;
        gboolean res;

        gboolean can_activate_sessions;
        g_return_val_if_fail (GDM_IS_USER_MANAGER (manager), FALSE);
        g_return_val_if_fail (GDM_IS_USER (user), FALSE);

        ret = FALSE;

        can_activate_sessions = gdm_user_manager_can_switch (manager);

        if (! can_activate_sessions) {
                g_debug ("GdmUserManager: seat is unable to activate sessions");
                goto out;
        }

        ssid = gdm_user_get_primary_session_id (user);
        if (ssid == NULL) {
                goto out;
        }

        res = activate_session_id (manager, manager->priv->seat_id, ssid);
        if (! res) {
                g_debug ("GdmUserManager: unable to activate session: %s", ssid);
                goto out;
        }

        ret = TRUE;
 out:
        return ret;
}

static void
on_user_sessions_changed (GdmUser        *user,
                          GdmUserManager *manager)
{
        guint nsessions;

        if (! manager->priv->is_loaded) {
                return;
        }

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
on_user_changed (GdmUser        *user,
                 GdmUserManager *manager)
{
        if (manager->priv->is_loaded) {
                g_debug ("GdmUserManager: user changed");
                g_signal_emit (manager, signals[USER_CHANGED], 0, user);
        }
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
                g_warning ("Failed to connect to the ConsoleKit session object");
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
                if (error != NULL) {
                        g_debug ("GdmUserManager: Failed to identify the current seat: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("GdmUserManager: Failed to identify the current seat");
                }
        }
 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return seat_id;
}

static char *
get_x11_display_for_session (DBusGConnection *connection,
                             const char      *session_id)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *x11_display;
        gboolean         res;

        proxy = NULL;
        x11_display = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit session object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetX11Display",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &x11_display,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("GdmUserManager: Failed to identify the x11 display: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("GdmUserManager: Failed to identify the x11 display");
                }
        }
 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return x11_display;
}

static gint
match_name_cmpfunc (gconstpointer a,
                    gconstpointer b)
{
        return g_strcmp0 ((char *) a,
                          (char *) b);
}

static gboolean
username_in_exclude_list (GdmUserManager *manager,
                          const char     *username)
{
        GSList   *found;
        gboolean  ret = FALSE;

        /* always exclude the "gdm" user. */
        if (username == NULL || (strcmp (username, GDM_USERNAME) == 0)) {
                return TRUE;
        }

        if (manager->priv->exclude_usernames != NULL) {
                found = g_slist_find_custom (manager->priv->exclude_usernames,
                                             username,
                                             match_name_cmpfunc);
                if (found != NULL) {
                        ret = TRUE;
                }
        }

        return ret;
}

static void
add_session_for_user (GdmUserManager *manager,
                      GdmUser        *user,
                      const char     *ssid)
{
        g_hash_table_insert (manager->priv->sessions,
                             g_strdup (ssid),
                             g_strdup (gdm_user_get_user_name (user)));

        _gdm_user_add_session (user, ssid);
        g_debug ("GdmUserManager: added session for user: %s", gdm_user_get_user_name (user));
}

static void
set_has_multiple_users (GdmUserManager *manager,
                        gboolean        has_multiple_users)
{
        if (manager->priv->has_multiple_users != has_multiple_users) {
                manager->priv->has_multiple_users = has_multiple_users;
                g_object_notify (G_OBJECT (manager), "has-multiple-users");
        }
}

static void
add_user (GdmUserManager *manager,
          GdmUser        *user)
{
        const char *object_path;

        g_hash_table_insert (manager->priv->users_by_name,
                             g_strdup (gdm_user_get_user_name (user)),
                             g_object_ref (user));

        object_path = gdm_user_get_object_path (user);
        if (object_path != NULL) {
                g_hash_table_insert (manager->priv->users_by_object_path,
                                     (gpointer) object_path,
                                     g_object_ref (user));
        }

        g_signal_connect (user,
                          "sessions-changed",
                          G_CALLBACK (on_user_sessions_changed),
                          manager);
        g_signal_connect (user,
                          "changed",
                          G_CALLBACK (on_user_changed),
                          manager);

        if (manager->priv->is_loaded) {
                g_signal_emit (manager, signals[USER_ADDED], 0, user);
        }

        if (g_hash_table_size (manager->priv->users_by_name) > 1) {
                set_has_multiple_users (manager, TRUE);
        }
}

static GdmUser *
add_new_user_for_pwent (GdmUserManager *manager,
                        struct passwd  *pwent)
{
        GdmUser *user;

        g_debug ("GdmUserManager: Creating new user from password entry: %s", pwent->pw_name);

        user = g_object_new (GDM_TYPE_USER, NULL);
        _gdm_user_update_from_pwent (user, pwent);

        /* increases ref count */
        add_user (manager, user);
        g_object_unref (user);

        return user;
}

static void
remove_user (GdmUserManager *manager,
             GdmUser        *user)
{
        g_object_ref (user);

        g_signal_handlers_disconnect_by_func (user, on_user_changed, manager);
        g_signal_handlers_disconnect_by_func (user, on_user_sessions_changed, manager);
        if (gdm_user_get_object_path (user) != NULL) {
                g_hash_table_remove (manager->priv->users_by_object_path, gdm_user_get_object_path (user));
        }
        g_hash_table_remove (manager->priv->users_by_name, gdm_user_get_user_name (user));
        g_signal_emit (manager, signals[USER_REMOVED], 0, user);

        g_object_unref (user);

        if (g_hash_table_size (manager->priv->users_by_name) > 1) {
                set_has_multiple_users (manager, FALSE);
        }
}

static void
add_new_user_for_object_path (const char     *object_path,
                              GdmUserManager *manager)
{
        GdmUser *user;
        const char *username;

        if (g_hash_table_lookup (manager->priv->users_by_object_path, object_path)) {
                return;
        }
        user = gdm_user_new_from_object_path (object_path);

        if (user == NULL) {
                return;
        }

        username = gdm_user_get_user_name (user);
        if (username_in_exclude_list (manager, username)) {
                g_debug ("GdmUserManager: excluding user '%s'", username);
                g_object_unref (user);
                return;
        }

        add_user (manager, user);
        g_object_unref (user);
}

static void
on_new_user_in_accounts_service (DBusGProxy *proxy,
                                 const char *object_path,
                                 gpointer    user_data)
{
        GdmUserManager *manager = GDM_USER_MANAGER (user_data);

        add_new_user_for_object_path (object_path, manager);
}

static void
on_user_removed_in_accounts_service (DBusGProxy *proxy,
                                     const char *object_path,
                                     gpointer    user_data)
{
        GdmUserManager *manager = GDM_USER_MANAGER (user_data);
        GdmUser        *user;

        user = g_hash_table_lookup (manager->priv->users_by_object_path, object_path);
        remove_user (manager, user);
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
                if (error != NULL) {
                        g_debug ("GdmUserManager: Failed to identify the current session: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("GdmUserManager: Failed to identify the current session");
                }
                goto out;
        }

        seat_id = get_seat_id_for_session (connection, session_id);

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }
        g_free (session_id);

        return seat_id;
}

static gboolean
get_uid_from_session_id (GdmUserManager *manager,
                         const char     *session_id,
                         uid_t          *uidp)
{
        DBusGProxy      *proxy;
        GError          *error;
        guint            uid;
        gboolean         res;

        proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
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
                                 G_TYPE_UINT, &uid,
                                 G_TYPE_INVALID);
        g_object_unref (proxy);

        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to query the session: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to query the session");
                }
                return FALSE;
        }

        if (uidp != NULL) {
                *uidp = (uid_t) uid;
        }

        return TRUE;
}

static char *
get_user_object_path_from_accounts_service (GdmUserManager *manager,
                                            const char     *name)
{
        GError          *error;
        char            *object_path;
        gboolean         res;

        g_assert (manager->priv->accounts_proxy != NULL);

        error = NULL;
        object_path = NULL;
        res = dbus_g_proxy_call (manager->priv->accounts_proxy,
                                 "FindUserByName",
                                 &error,
                                 G_TYPE_STRING,
                                 name,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 &object_path,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("GdmUserManager: Failed to find user %s: %s", name, error->message);
                        g_error_free (error);
                } else {
                        g_debug ("GdmUserManager: Failed to find user %s", name);
                }
                return NULL;
        }
        return object_path;
}

static void
set_is_loaded (GdmUserManager *manager,
               gboolean        is_loaded)
{
        if (manager->priv->is_loaded != is_loaded) {
                manager->priv->is_loaded = is_loaded;
                g_object_notify (G_OBJECT (manager), "is-loaded");
        }
}

static void
on_list_cached_users_finished (DBusGProxy     *proxy,
                               DBusGProxyCall *call_id,
                               gpointer        data)
{
        GdmUserManager *manager = data;
        GError *error = NULL;
        GPtrArray *paths;

        if (!dbus_g_proxy_end_call (proxy,
                                    call_id,
                                    &error,
                                    dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &paths,
                                    G_TYPE_INVALID)) {
                g_debug ("GdmUserManager: ListCachedUsers failed: %s", error->message);
                g_error_free (error);

                g_object_unref (manager->priv->accounts_proxy);
                manager->priv->accounts_proxy = NULL;

                load_users_manually (manager);

                return;
        }

        g_ptr_array_foreach (paths, (GFunc)add_new_user_for_object_path, manager);

        g_ptr_array_foreach (paths, (GFunc)g_free, NULL);
        g_ptr_array_free (paths, TRUE);

        /* Add users who are specifically included */
        if (manager->priv->include_usernames != NULL) {
                GSList *l;

                for (l = manager->priv->include_usernames; l != NULL; l = l->next) {
                        GdmUser *user;

                        g_debug ("GdmUserManager: Adding included user %s", (char *)l->data);
                        /*
                         * The call to gdm_user_manager_get_user will add the user if it is
                         * valid and not already in the hash.
                         */
                        user = gdm_user_manager_get_user (manager, l->data);
                        if (user == NULL) {
                                g_debug ("GdmUserManager: unable to lookup user '%s'", (char *)l->data);
                        }
                }
        }

        set_is_loaded (manager, TRUE);
}

static void
maybe_add_session (GdmUserManager *manager,
                   const char     *session_id)
{
        uid_t          uid;
        gboolean       res;
        struct passwd *pwent;
        GdmUser       *user;
        char          *x11_display;

        res = get_uid_from_session_id (manager, session_id, &uid);
        if (! res) {
                g_warning ("Unable to lookup user for session");
                return;
        }

        errno = 0;
        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("Unable to lookup user ID %d: %s", (int)uid, g_strerror (errno));
                return;
        }

        /* skip if doesn't have an x11 display */
        x11_display = get_x11_display_for_session (manager->priv->connection, session_id);
        if (x11_display == NULL || x11_display[0] == '\0') {
                g_debug ("GdmUserManager: not adding session without a x11 display: %s", session_id);
                g_free (x11_display);
                return;
        }
        g_free (x11_display);

        /* check exclusions up front */
        if (username_in_exclude_list (manager, pwent->pw_name)) {
                g_debug ("GdmUserManager: excluding user '%s'", pwent->pw_name);
                return;
        }

        user = gdm_user_manager_get_user (manager, pwent->pw_name);
        if (user == NULL) {
                return;
        }

        add_session_for_user (manager, user, session_id);

        /* if we haven't yet gotten the login frequency
           then at least add one because the session exists */
        if (gdm_user_get_login_frequency (user) == 0) {
                _gdm_user_update_login_frequency (user, 1);
        }

}

static void
seat_session_added (DBusGProxy     *seat_proxy,
                    const char     *session_id,
                    GdmUserManager *manager)
{
        g_debug ("GdmUserManager: Session added: %s", session_id);

        maybe_add_session (manager, session_id);
}

static void
seat_session_removed (DBusGProxy     *seat_proxy,
                      const char     *session_id,
                      GdmUserManager *manager)
{
        GdmUser *user;
        char    *username;

        g_debug ("GdmUserManager: Session removed: %s", session_id);

        /* since the session object may already be gone
         * we can't query CK directly */

        username = g_hash_table_lookup (manager->priv->sessions, session_id);
        if (username == NULL) {
                return;
        }

        user = g_hash_table_lookup (manager->priv->users_by_name, username);
        if (user == NULL) {
                /* nothing to do */
                return;
        }

        g_debug ("GdmUserManager: Session removed for %s", username);
        _gdm_user_remove_session (user, session_id);
}

static void
on_seat_proxy_destroy (DBusGProxy     *proxy,
                       GdmUserManager *manager)
{
        g_debug ("GdmUserManager: seat proxy destroyed");

        manager->priv->seat_proxy = NULL;
}

static void
get_seat_proxy (GdmUserManager *manager)
{
        DBusGProxy      *proxy;
        GError          *error;

        g_assert (manager->priv->seat_proxy == NULL);

        manager->priv->seat_id = get_current_seat_id (manager->priv->connection);
        if (manager->priv->seat_id == NULL) {
                return;
        }

        g_debug ("GdmUserManager: Found current seat: %s", manager->priv->seat_id);

        error = NULL;
        proxy = dbus_g_proxy_new_for_name_owner (manager->priv->connection,
                                                 CK_NAME,
                                                 manager->priv->seat_id,
                                                 CK_SEAT_INTERFACE,
                                                 &error);

        if (proxy == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to connect to the ConsoleKit seat object: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to connect to the ConsoleKit seat object");
                }
                return;
        }

        g_signal_connect (proxy, "destroy", G_CALLBACK (on_seat_proxy_destroy), manager);

        dbus_g_proxy_add_signal (proxy,
                                 "SessionAdded",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "SessionRemoved",
                                 DBUS_TYPE_G_OBJECT_PATH,
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

static void
on_accounts_proxy_destroy (DBusGProxy     *proxy,
                           GdmUserManager *manager)
{
        g_debug ("GdmUserManager: accounts proxy destroyed");

        manager->priv->accounts_proxy = NULL;
}

static void
get_accounts_proxy (GdmUserManager *manager)
{
        DBusGProxy      *proxy;
        GError          *error;

        g_assert (manager->priv->accounts_proxy == NULL);

        error = NULL;
        proxy = dbus_g_proxy_new_for_name_owner (manager->priv->connection,
                                                 ACCOUNTS_NAME,
                                                 ACCOUNTS_PATH,
                                                 ACCOUNTS_INTERFACE,
                                                 &error);
        manager->priv->accounts_proxy = proxy;

        if (proxy != NULL) {
                g_signal_connect (proxy, "destroy",
                                  G_CALLBACK (on_accounts_proxy_destroy),
                                  manager);

                dbus_g_proxy_add_signal (proxy,
                                         "UserAdded",
                                         DBUS_TYPE_G_OBJECT_PATH,
                                         G_TYPE_INVALID);
                dbus_g_proxy_add_signal (proxy,
                                         "UserDeleted",
                                         DBUS_TYPE_G_OBJECT_PATH,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (proxy,
                                             "UserAdded",
                                             G_CALLBACK (on_new_user_in_accounts_service),
                                             manager,
                                             NULL);
                dbus_g_proxy_connect_signal (proxy,
                                             "UserDeleted",
                                             G_CALLBACK (on_user_removed_in_accounts_service),
                                             manager,
                                             NULL);
        } else {
                g_debug ("GdmUserManager: Unable to connect to accounts service");
        }
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

        user = g_hash_table_lookup (manager->priv->users_by_name, username);

        /* if we don't have it loaded try to load it now */
        if (user == NULL) {
                if (manager->priv->accounts_proxy != NULL) {
                        char *object_path;

                        object_path = get_user_object_path_from_accounts_service (manager, username);

                        if (object_path != NULL) {
                                add_new_user_for_object_path (object_path, manager);
                                g_free (object_path);
                                user = g_hash_table_lookup (manager->priv->users_by_name, username);
                        }
                } else {
                        struct passwd *pwent;
                        pwent = getpwnam (username);
                        if (pwent != NULL) {
                                user = add_new_user_for_pwent (manager, pwent);
                        }
                }
        }

        return user;
}

GdmUser *
gdm_user_manager_get_user_by_uid (GdmUserManager *manager,
                                  gulong          uid)
{
        struct passwd *pwent;

        g_return_val_if_fail (GDM_IS_USER_MANAGER (manager), NULL);

        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("GdmUserManager: unable to lookup uid %d", (int)uid);
                return NULL;
        }

        return gdm_user_manager_get_user (manager, pwent->pw_name);
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
        g_hash_table_foreach (manager->priv->users_by_name, listify_hash_values_hfunc, &retval);

        return g_slist_sort (retval, (GCompareFunc) gdm_user_collate);
}

static gboolean
parse_value_as_ulong (const char *value,
                      gulong     *ulongval)
{
        char  *end_of_valid_long;
        glong  long_value;
        gulong ulong_value;

        errno = 0;
        long_value = strtol (value, &end_of_valid_long, 10);

        if (*value == '\0' || *end_of_valid_long != '\0') {
                return FALSE;
        }

        ulong_value = long_value;
        if (ulong_value != long_value || errno == ERANGE) {
                return FALSE;
        }

        *ulongval = ulong_value;

        return TRUE;
}

static gboolean
parse_ck_history_line (const char *line,
                       char      **user_namep,
                       gulong     *frequencyp)
{
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        gboolean    ret;
        GError     *error;

        ret = FALSE;
        re = NULL;
        match_info = NULL;

        error = NULL;
        re = g_regex_new ("(?P<username>[0-9a-zA-Z]+)[ ]+(?P<frequency>[0-9]+)", 0, 0, &error);
        if (re == NULL) {
                if (error != NULL) {
                        g_critical ("%s", error->message);
                } else {
                        g_critical ("Error in regex call");
                }
                goto out;
        }

        g_regex_match (re, line, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse history: %s", line);
                goto out;
        }

        if (user_namep != NULL) {
                *user_namep = g_match_info_fetch_named (match_info, "username");
        }

        if (frequencyp != NULL) {
                char *freq;
                freq = g_match_info_fetch_named (match_info, "frequency");
                res = parse_value_as_ulong (freq, frequencyp);
                g_free (freq);
                if (! res) {
                        goto out;
                }
        }

        ret = TRUE;

 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }
        return ret;
}

static void
process_ck_history_line (GdmUserManager *manager,
                         const char     *line)
{
        gboolean res;
        char    *username;
        gulong   frequency;
        GdmUser *user;

        frequency = 0;
        username = NULL;
        res = parse_ck_history_line (line, &username, &frequency);
        if (! res) {
                return;
        }

        if (username_in_exclude_list (manager, username)) {
                g_debug ("GdmUserManager: excluding user '%s'", username);
                g_free (username);
                return;
        }

        user = gdm_user_manager_get_user (manager, username);
        if (user == NULL) {
                g_debug ("GdmUserManager: unable to lookup user '%s'", username);
                g_free (username);
                return;
        }

        _gdm_user_update_login_frequency (user, frequency);
        g_free (username);
}

static void
maybe_set_is_loaded (GdmUserManager *manager)
{
        if (manager->priv->is_loaded) {
                return;
        }

        if (manager->priv->ck_history_pid != 0) {
                return;
        }

        if (manager->priv->load_passwd_pending) {
                return;
        }

        set_is_loaded (manager, TRUE);
}

static gboolean
ck_history_watch (GIOChannel     *source,
                  GIOCondition    condition,
                  GdmUserManager *manager)
{
        GIOStatus status;
        gboolean  done  = FALSE;

        g_return_val_if_fail (manager != NULL, FALSE);

        if (condition & G_IO_IN) {
                char   *str;
                GError *error;

                error = NULL;
                status = g_io_channel_read_line (source, &str, NULL, NULL, &error);
                if (error != NULL) {
                        g_warning ("GdmUserManager: unable to read line: %s", error->message);
                        g_error_free (error);
                }

                if (status == G_IO_STATUS_NORMAL) {
                        g_debug ("GdmUserManager: history output: %s", str);
                        process_ck_history_line (manager, str);
                } else if (status == G_IO_STATUS_EOF) {
                        done = TRUE;
                }

                g_free (str);
        } else if (condition & G_IO_HUP) {
                done = TRUE;
        }

        if (done) {
                manager->priv->ck_history_id = 0;
                if (manager->priv->ck_history_watchdog_id != 0) {
                        g_source_remove (manager->priv->ck_history_watchdog_id);
                        manager->priv->ck_history_watchdog_id = 0;
                }
                manager->priv->ck_history_pid = 0;

                maybe_set_is_loaded (manager);

                return FALSE;
        }

        return TRUE;
}

static int
signal_pid (int pid,
            int signal)
{
        int status = -1;

        status = kill (pid, signal);

        if (status < 0) {
                if (errno == ESRCH) {
                        g_debug ("Child process %lu was already dead.",
                                 (unsigned long) pid);
                } else {
                        char buf [1024];
                        snprintf (buf,
                                  sizeof (buf),
                                  "Couldn't kill child process %lu",
                                  (unsigned long) pid);
                        perror (buf);
                }
        }

        return status;
}

static gboolean
ck_history_watchdog (GdmUserManager *manager)
{
        if (manager->priv->ck_history_pid > 0) {
                g_debug ("Killing ck-history process");
                signal_pid (manager->priv->ck_history_pid, SIGTERM);
                manager->priv->ck_history_pid = 0;
        }

        manager->priv->ck_history_watchdog_id = 0;
        return FALSE;
}

static gboolean
load_ck_history (GdmUserManager *manager)
{
        char       *command;
        char       *since;
        const char *seat_id;
        GError     *error;
        gboolean    res;
        char      **argv;
        int         standard_out;
        GIOChannel *channel;
        GTimeVal    tv;

        g_assert (manager->priv->ck_history_id == 0);

        command = NULL;

        seat_id = NULL;
        if (manager->priv->seat_id != NULL
            && g_str_has_prefix (manager->priv->seat_id, "/org/freedesktop/ConsoleKit/")) {

                seat_id = manager->priv->seat_id + strlen ("/org/freedesktop/ConsoleKit/");
        }

        if (seat_id == NULL) {
                g_warning ("Unable to load CK history: no seat-id found");
                goto out;
        }

        g_get_current_time (&tv);
        tv.tv_sec -= LOGIN_FREQUENCY_TIME_WINDOW_SECS;
        since = g_time_val_to_iso8601 (&tv);

        command = g_strdup_printf ("ck-history --frequent --since='%s' --seat='%s' --session-type=''",
                                   since,
                                   seat_id);
        g_free (since);
        g_debug ("GdmUserManager: running '%s'", command);
        error = NULL;
        if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
                if (error != NULL) {
                        g_warning ("Could not parse command: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Could not parse command");
                }
                goto out;
        }

        error = NULL;
        res = g_spawn_async_with_pipes (NULL,
                                        argv,
                                        NULL,
                                        G_SPAWN_SEARCH_PATH,
                                        NULL,
                                        NULL,
                                        &manager->priv->ck_history_pid, /* pid */
                                        NULL,
                                        &standard_out,
                                        NULL,
                                        &error);
        g_strfreev (argv);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Unable to run ck-history: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to run ck-history");
                }
                goto out;
        }

        channel = g_io_channel_unix_new (standard_out);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        manager->priv->ck_history_watchdog_id = g_timeout_add_seconds (1, (GSourceFunc) ck_history_watchdog, manager);
        manager->priv->ck_history_id = g_io_add_watch (channel,
                                                       G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                                       (GIOFunc)ck_history_watch,
                                                       manager);
        g_io_channel_unref (channel);

 out:

        g_free (command);

        return manager->priv->ck_history_id != 0;
}

static void
reload_passwd_file (GHashTable *valid_shells,
                    GSList     *exclude_users,
                    GSList     *include_users,
                    gboolean    include_all,
                    GHashTable *current_users_by_name,
                    GSList    **added_users,
                    GSList    **removed_users)
{
        FILE           *fp;
        GHashTableIter  iter;
        GHashTable     *new_users_by_name;
        GdmUser        *user;
        char           *name;

        new_users_by_name = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   NULL,
                                                   g_object_unref);

        errno = 0;
        fp = fopen (PATH_PASSWD, "r");
        if (fp == NULL) {
                g_warning ("Unable to open %s: %s", PATH_PASSWD, g_strerror (errno));
                goto out;
        }

        /* Make sure we keep users who are logged in no matter what. */
        g_hash_table_iter_init (&iter, current_users_by_name);
        while (g_hash_table_iter_next (&iter, (gpointer *) &name, (gpointer *) &user)) {
                struct passwd *pwent;
                pwent = getpwnam (name);
                if (pwent == NULL) {
                        continue;
                }

                g_object_freeze_notify (G_OBJECT (user));
                _gdm_user_update_from_pwent (user, pwent);
                g_hash_table_insert (new_users_by_name, (char *)gdm_user_get_user_name (user), g_object_ref (user));
        }

        if (include_users != NULL) {
                GSList *l;
                for (l = include_users; l != NULL; l = l->next) {
                        struct passwd *pwent;
                        pwent = getpwnam (l->data);
                        if (pwent == NULL) {
                                continue;
                        }

                        user = g_hash_table_lookup (new_users_by_name, pwent->pw_name);
                        if (user != NULL) {
                                /* already there */
                                continue;
                        }

                        user = g_hash_table_lookup (current_users_by_name, pwent->pw_name);
                        if (user == NULL) {
                                user = g_object_new (GDM_TYPE_USER, NULL);
                        } else {
                                g_object_ref (user);
                        }
                        g_object_freeze_notify (G_OBJECT (user));
                        _gdm_user_update_from_pwent (user, pwent);
                        g_hash_table_insert (new_users_by_name, (char *)gdm_user_get_user_name (user), user);
                }
        }

        if (include_all != TRUE) {
                g_debug ("GdmUserManager: include_all is FALSE");
        } else {
                struct passwd *pwent;

                g_debug ("GdmUserManager: include_all is TRUE");

                for (pwent = fgetpwent (fp);
                     pwent != NULL;
                     pwent = fgetpwent (fp)) {

                        /* Skip users below MinimalUID... */
                        if (pwent->pw_uid < FALLBACK_MINIMAL_UID) {
                                continue;
                        }

                        /* ...And users w/ invalid shells... */
                        if (pwent->pw_shell == NULL
                            || !g_hash_table_lookup (valid_shells, pwent->pw_shell)) {
                                g_debug ("GdmUserManager: skipping user with bad shell: %s", pwent->pw_name);
                                continue;
                        }

                        /* always exclude the "gdm" user. */
                        if (strcmp (pwent->pw_name, GDM_USERNAME) == 0) {
                                continue;
                        }

                        /* ...And explicitly excluded users */
                        if (exclude_users != NULL) {
                                GSList   *found;

                                found = g_slist_find_custom (exclude_users,
                                                             pwent->pw_name,
                                                             match_name_cmpfunc);
                                if (found != NULL) {
                                        g_debug ("GdmUserManager: explicitly skipping user: %s", pwent->pw_name);
                                        continue;
                                }
                        }

                        user = g_hash_table_lookup (new_users_by_name, pwent->pw_name);
                        if (user != NULL) {
                                /* already there */
                                continue;
                        }

                        user = g_hash_table_lookup (current_users_by_name, pwent->pw_name);
                        if (user == NULL) {
                                user = g_object_new (GDM_TYPE_USER, NULL);
                        } else {
                                g_object_ref (user);
                        }

                        /* Freeze & update users not already in the new list */
                        g_object_freeze_notify (G_OBJECT (user));
                        _gdm_user_update_from_pwent (user, pwent);
                        g_hash_table_insert (new_users_by_name, (char *)gdm_user_get_user_name (user), user);
                }
        }

        /* Go through and handle added users */
        g_hash_table_iter_init (&iter, new_users_by_name);
        while (g_hash_table_iter_next (&iter, (gpointer *) &name, (gpointer *) &user)) {
                GdmUser *user2;
                user2 = g_hash_table_lookup (current_users_by_name, name);
                if (user2 == NULL) {
                        *added_users = g_slist_prepend (*added_users, g_object_ref (user));
                }
        }

        /* Go through and handle removed users */
        g_hash_table_iter_init (&iter, current_users_by_name);
        while (g_hash_table_iter_next (&iter, (gpointer *) &name, (gpointer *) &user)) {
                GdmUser *user2;
                user2 = g_hash_table_lookup (new_users_by_name, name);
                if (user2 == NULL) {
                        *removed_users = g_slist_prepend (*removed_users, g_object_ref (user));
                }
        }

 out:
        /* Cleanup */

        fclose (fp);

        g_hash_table_iter_init (&iter, new_users_by_name);
        while (g_hash_table_iter_next (&iter, (gpointer *) &name, (gpointer *) &user)) {
                g_object_thaw_notify (G_OBJECT (user));
        }

        g_hash_table_destroy (new_users_by_name);
}

typedef struct {
        GdmUserManager *manager;
        GSList         *exclude_users;
        GSList         *include_users;
        gboolean        include_all;
        GHashTable     *shells;
        GHashTable     *current_users_by_name;
        GSList         *added_users;
        GSList         *removed_users;
} PasswdData;

static void
passwd_data_free (PasswdData *data)
{
        if (data->manager != NULL) {
                g_object_unref (data->manager);
        }

        g_slist_foreach (data->added_users, (GFunc) g_object_unref, NULL);
        g_slist_free (data->added_users);

        g_slist_foreach (data->removed_users, (GFunc) g_object_unref, NULL);
        g_slist_free (data->removed_users);

        g_slist_foreach (data->exclude_users, (GFunc) g_free, NULL);
        g_slist_free (data->exclude_users);

        g_slist_foreach (data->include_users, (GFunc) g_free, NULL);
        g_slist_free (data->include_users);

        g_slice_free (PasswdData, data);
}

static gboolean
reload_passwd_job_done (PasswdData *data)
{
        GSList *l;

        g_debug ("GdmUserManager: done reloading passwd file");

        /* Go through and handle added users */
        for (l = data->added_users; l != NULL; l = l->next) {
                add_user (data->manager, l->data);
        }

        /* Go through and handle removed users */
        for (l = data->removed_users; l != NULL; l = l->next) {
                remove_user (data->manager, l->data);
        }

        data->manager->priv->load_passwd_pending = FALSE;

        if (! data->manager->priv->is_loaded) {
                maybe_set_is_loaded (data->manager);

                if (data->manager->priv->include_all == TRUE) {
                        monitor_local_users (data->manager);
                }
        }

        passwd_data_free (data);

        return FALSE;
}

static gboolean
do_reload_passwd_job (GIOSchedulerJob *job,
                      GCancellable    *cancellable,
                      PasswdData      *data)
{
        g_debug ("GdmUserManager: reloading passwd file worker");

        reload_passwd_file (data->shells,
                            data->exclude_users,
                            data->include_users,
                            data->include_all,
                            data->current_users_by_name,
                            &data->added_users,
                            &data->removed_users);

        g_io_scheduler_job_send_to_mainloop_async (job,
                                                   (GSourceFunc) reload_passwd_job_done,
                                                   data,
                                                   NULL);

        return FALSE;
}

static GSList *
slist_deep_copy (const GSList *list)
{
        GSList *retval;
        GSList *l;

        if (list == NULL)
                return NULL;

        retval = g_slist_copy ((GSList *) list);
        for (l = retval; l != NULL; l = l->next) {
                l->data = g_strdup (l->data);
        }

        return retval;
}

static void
schedule_reload_passwd (GdmUserManager *manager)
{
        PasswdData *passwd_data;

        manager->priv->load_passwd_pending = TRUE;

        passwd_data = g_slice_new0 (PasswdData);
        passwd_data->manager = g_object_ref (manager);
        passwd_data->shells = manager->priv->shells;
        passwd_data->exclude_users = slist_deep_copy (manager->priv->exclude_usernames);
        passwd_data->include_users = slist_deep_copy (manager->priv->include_usernames);
        passwd_data->include_all = manager->priv->include_all;
        passwd_data->current_users_by_name = manager->priv->users_by_name;
        passwd_data->added_users = NULL;
        passwd_data->removed_users = NULL;

        g_debug ("GdmUserManager: scheduling a passwd file update");

        g_io_scheduler_push_job ((GIOSchedulerJobFunc) do_reload_passwd_job,
                                 passwd_data,
                                 NULL,
                                 G_PRIORITY_DEFAULT,
                                 NULL);
}

static void
load_sessions (GdmUserManager *manager)
{
        gboolean    res;
        GError     *error;
        GPtrArray  *sessions;
        int         i;

        if (manager->priv->seat_proxy == NULL) {
                g_debug ("GdmUserManager: no seat proxy; can't load sessions");
                return;
        }

        sessions = NULL;
        error = NULL;
        res = dbus_g_proxy_call (manager->priv->seat_proxy,
                                 "GetSessions",
                                 &error,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("unable to determine sessions for seat: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("unable to determine sessions for seat");
                }
                return;
        }

        for (i = 0; i < sessions->len; i++) {
                char *ssid;

                ssid = g_ptr_array_index (sessions, i);

                maybe_add_session (manager, ssid);
        }
        g_ptr_array_foreach (sessions, (GFunc)g_free, NULL);
        g_ptr_array_free (sessions, TRUE);
}

static void
reload_shells (GdmUserManager *manager)
{
        char *shell;

        setusershell ();

        g_hash_table_remove_all (manager->priv->shells);
        for (shell = getusershell (); shell != NULL; shell = getusershell ()) {
                /* skip well known not-real shells */
                if (shell == NULL
                    || strcmp (shell, "/sbin/nologin") == 0
                    || strcmp (shell, "/bin/false") == 0) {
                        g_debug ("GdmUserManager: skipping shell %s", shell);
                        continue;
                }
                g_hash_table_insert (manager->priv->shells,
                                     g_strdup (shell),
                                     GUINT_TO_POINTER (TRUE));
        }

        endusershell ();
}

static void
load_users_manually (GdmUserManager *manager)
{
        gboolean res;

        manager->priv->shells = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       NULL);
        reload_shells (manager);

        load_sessions (manager);

        res = load_ck_history (manager);
        schedule_reload_passwd (manager);
}

static void
load_users (GdmUserManager *manager)
{
        if (manager->priv->accounts_proxy != NULL) {
                g_debug ("GdmUserManager: calling 'ListCachedUsers'");

                dbus_g_proxy_begin_call (manager->priv->accounts_proxy,
                                         "ListCachedUsers",
                                         on_list_cached_users_finished,
                                         manager,
                                         NULL,
                                         G_TYPE_INVALID);
        } else {
                g_debug ("GdmUserManager: Getting users manually");
                load_users_manually (manager);
        }
}

static gboolean
load_users_idle (GdmUserManager *manager)
{
        load_users (manager);
        manager->priv->load_id = 0;

        return FALSE;
}

static void
queue_load_users (GdmUserManager *manager)
{
        if (manager->priv->load_id > 0) {
                return;
        }

        manager->priv->load_id = g_idle_add ((GSourceFunc)load_users_idle, manager);
}

static gboolean
reload_passwd_idle (GdmUserManager *manager)
{
        schedule_reload_passwd (manager);
        manager->priv->reload_passwd_id = 0;

        return FALSE;
}

static void
queue_reload_passwd (GdmUserManager *manager)
{
        if (manager->priv->reload_passwd_id > 0) {
                g_source_remove (manager->priv->reload_passwd_id);
        }

        manager->priv->reload_passwd_id = g_timeout_add_seconds (RELOAD_PASSWD_THROTTLE_SECS, (GSourceFunc)reload_passwd_idle, manager);
}

static void
on_shells_monitor_changed (GFileMonitor     *monitor,
                           GFile            *file,
                           GFile            *other_file,
                           GFileMonitorEvent event_type,
                           GdmUserManager   *manager)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        reload_shells (manager);
        queue_reload_passwd (manager);
}

static void
on_passwd_monitor_changed (GFileMonitor     *monitor,
                           GFile            *file,
                           GFile            *other_file,
                           GFileMonitorEvent event_type,
                           GdmUserManager   *manager)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        queue_reload_passwd (manager);
}

static void
gdm_user_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GdmUserManager *manager;

        manager = GDM_USER_MANAGER (object);

        switch (prop_id) {
        case PROP_IS_LOADED:
                g_value_set_boolean (value, manager->priv->is_loaded);
                break;
        case PROP_HAS_MULTIPLE_USERS:
                g_value_set_boolean (value, manager->priv->has_multiple_users);
                break;
        case PROP_INCLUDE_ALL:
                g_value_set_boolean (value, manager->priv->include_all);
                break;
        case PROP_INCLUDE_USERNAMES_LIST:
                g_value_set_pointer (value, manager->priv->include_usernames);
                break;
        case PROP_EXCLUDE_USERNAMES_LIST:
                g_value_set_pointer (value, manager->priv->exclude_usernames);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
set_include_usernames (GdmUserManager *manager,
                       GSList         *list)
{
        if (manager->priv->include_usernames != NULL) {
                g_slist_foreach (manager->priv->include_usernames, (GFunc) g_free, NULL);
                g_slist_free (manager->priv->include_usernames);
        }
        manager->priv->include_usernames = slist_deep_copy (list);
}

static void
set_exclude_usernames (GdmUserManager *manager,
                       GSList         *list)
{
        if (manager->priv->exclude_usernames != NULL) {
                g_slist_foreach (manager->priv->exclude_usernames, (GFunc) g_free, NULL);
                g_slist_free (manager->priv->exclude_usernames);
        }
        manager->priv->exclude_usernames = slist_deep_copy (list);
}

static void
set_include_all (GdmUserManager *manager,
                 gboolean        all)
{
        if (manager->priv->include_all != all) {
                manager->priv->include_all = all;
        }
}

static void
gdm_user_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GdmUserManager *self;

        self = GDM_USER_MANAGER (object);

        switch (prop_id) {
        case PROP_INCLUDE_ALL:
                set_include_all (self, g_value_get_boolean (value));
                break;
        case PROP_INCLUDE_USERNAMES_LIST:
                set_include_usernames (self, g_value_get_pointer (value));
                break;
        case PROP_EXCLUDE_USERNAMES_LIST:
                set_exclude_usernames (self, g_value_get_pointer (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
monitor_local_users (GdmUserManager *manager)
{
        GFile *file;
        GError *error;

        g_debug ("GdmUserManager: Monitoring local users");

        /* /etc/shells */
        file = g_file_new_for_path (_PATH_SHELLS);
        error = NULL;
        manager->priv->shells_monitor = g_file_monitor_file (file,
                                                             G_FILE_MONITOR_NONE,
                                                             NULL,
                                                             &error);
        if (manager->priv->shells_monitor != NULL) {
                g_signal_connect (manager->priv->shells_monitor,
                                  "changed",
                                  G_CALLBACK (on_shells_monitor_changed),
                                  manager);
        } else {
                g_warning ("Unable to monitor %s: %s", _PATH_SHELLS, error->message);
                g_error_free (error);
        }
        g_object_unref (file);

        /* /etc/passwd */
        file = g_file_new_for_path (PATH_PASSWD);
        manager->priv->passwd_monitor = g_file_monitor_file (file,
                                                             G_FILE_MONITOR_NONE,
                                                             NULL,
                                                             &error);
        if (manager->priv->passwd_monitor != NULL) {
                g_signal_connect (manager->priv->passwd_monitor,
                                  "changed",
                                  G_CALLBACK (on_passwd_monitor_changed),
                                  manager);
        } else {
                g_warning ("Unable to monitor %s: %s", PATH_PASSWD, error->message);
                g_error_free (error);
        }
        g_object_unref (file);
}

static void
gdm_user_manager_class_init (GdmUserManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_user_manager_finalize;
        object_class->get_property = gdm_user_manager_get_property;
        object_class->set_property = gdm_user_manager_set_property;

        g_object_class_install_property (object_class,
                                         PROP_IS_LOADED,
                                         g_param_spec_boolean ("is-loaded",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_HAS_MULTIPLE_USERS,
                                         g_param_spec_boolean ("has-multiple-users",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_INCLUDE_ALL,
                                         g_param_spec_boolean ("include-all",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_INCLUDE_USERNAMES_LIST,
                                         g_param_spec_pointer ("include-usernames-list",
                                                               NULL,
                                                               NULL,
                                                               G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_EXCLUDE_USERNAMES_LIST,
                                         g_param_spec_pointer ("exclude-usernames-list",
                                                               NULL,
                                                               NULL,
                                                               G_PARAM_READWRITE));

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
        signals [USER_CHANGED] =
                g_signal_new ("user-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmUserManagerClass, user_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GDM_TYPE_USER);

        g_type_class_add_private (klass, sizeof (GdmUserManagerPrivate));
}

void
gdm_user_manager_queue_load (GdmUserManager *manager)
{
        g_return_if_fail (GDM_IS_USER_MANAGER (manager));

        if (! manager->priv->is_loaded) {
                queue_load_users (manager);
        }
}

static void
gdm_user_manager_init (GdmUserManager *manager)
{
        GError        *error;

        manager->priv = GDM_USER_MANAGER_GET_PRIVATE (manager);

        /* sessions */
        manager->priv->sessions = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         g_free);

        /* users */
        manager->priv->users_by_name = g_hash_table_new_full (g_str_hash,
                                                              g_str_equal,
                                                              g_free,
                                                              g_object_unref);

        manager->priv->users_by_object_path = g_hash_table_new_full (g_str_hash,
                                                                     g_str_equal,
                                                                     NULL,
                                                                     g_object_unref);

        g_assert (manager->priv->seat_proxy == NULL);

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to connect to the D-Bus daemon");
                }
                return;
        }

        get_seat_proxy (manager);
        get_accounts_proxy (manager);
}

static void
gdm_user_manager_finalize (GObject *object)
{
        GdmUserManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_MANAGER (object));

        manager = GDM_USER_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->ck_history_pid > 0) {
                g_debug ("Killing ck-history process");
                signal_pid (manager->priv->ck_history_pid, SIGTERM);
        }

        if (manager->priv->exclude_usernames != NULL) {
                g_slist_foreach (manager->priv->exclude_usernames, (GFunc) g_free, NULL);
                g_slist_free (manager->priv->exclude_usernames);
        }

        if (manager->priv->include_usernames != NULL) {
                g_slist_foreach (manager->priv->include_usernames, (GFunc) g_free, NULL);
                g_slist_free (manager->priv->include_usernames);
        }

        if (manager->priv->seat_proxy != NULL) {
                g_object_unref (manager->priv->seat_proxy);
        }

        if (manager->priv->accounts_proxy != NULL) {
                g_object_unref (manager->priv->accounts_proxy);
        }

        if (manager->priv->ck_history_id != 0) {
                g_source_remove (manager->priv->ck_history_id);
                manager->priv->ck_history_id = 0;
        }

        if (manager->priv->ck_history_watchdog_id != 0) {
                g_source_remove (manager->priv->ck_history_watchdog_id);
                manager->priv->ck_history_watchdog_id = 0;
        }

        if (manager->priv->load_id > 0) {
                g_source_remove (manager->priv->load_id);
                manager->priv->load_id = 0;
        }

        if (manager->priv->reload_passwd_id > 0) {
                g_source_remove (manager->priv->reload_passwd_id);
                manager->priv->reload_passwd_id = 0;
        }

        g_hash_table_destroy (manager->priv->sessions);

        if (manager->priv->passwd_monitor != NULL) {
                g_file_monitor_cancel (manager->priv->passwd_monitor);
        }

        g_hash_table_destroy (manager->priv->users_by_name);
        g_hash_table_destroy (manager->priv->users_by_object_path);

        if (manager->priv->shells_monitor != NULL) {
                g_file_monitor_cancel (manager->priv->shells_monitor);
        }
        g_hash_table_destroy (manager->priv->shells);

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
