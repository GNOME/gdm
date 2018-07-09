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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <xcb/xcb.h>
#include <X11/Xlib.h>

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-display-glue.h"
#include "gdm-display-access-file.h"
#include "gdm-launch-environment.h"

#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#include "gdm-launch-environment.h"
#include "gdm-dbus-util.h"

#define INITIAL_SETUP_USERNAME "gnome-initial-setup"
#define GNOME_SESSION_SESSIONS_PATH DATADIR "/gnome-session/sessions"

#define GDM_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_DISPLAY, GdmDisplayPrivate))

struct GdmDisplayPrivate
{
        char                 *id;
        char                 *seat_id;
        char                 *session_id;
        char                 *session_class;
        char                 *session_type;

        char                 *remote_hostname;
        int                   x11_display_number;
        char                 *x11_display_name;
        int                   status;
        time_t                creation_time;
        GTimer               *server_timer;

        char                 *x11_cookie;
        gsize                 x11_cookie_size;
        GdmDisplayAccessFile *access_file;

        guint                 finish_idle_id;

        xcb_connection_t     *xcb_connection;
        int                   xcb_screen_number;

        GDBusConnection      *connection;
        GdmDisplayAccessFile *user_access_file;

        GdmDBusDisplay       *display_skeleton;
        GDBusObjectSkeleton  *object_skeleton;

        GDBusProxy           *accountsservice_proxy;

        /* this spawns and controls the greeter session */
        GdmLaunchEnvironment *launch_environment;
        guint                 kill_greeter_id;

        guint                 is_local : 1;
        guint                 is_initial : 1;
        guint                 allow_timed_login : 1;
        guint                 have_existing_user_accounts : 1;
        guint                 doing_initial_setup : 1;
};

enum {
        PROP_0,
        PROP_ID,
        PROP_STATUS,
        PROP_SEAT_ID,
        PROP_SESSION_ID,
        PROP_SESSION_CLASS,
        PROP_SESSION_TYPE,
        PROP_REMOTE_HOSTNAME,
        PROP_X11_DISPLAY_NUMBER,
        PROP_X11_DISPLAY_NAME,
        PROP_X11_COOKIE,
        PROP_X11_AUTHORITY_FILE,
        PROP_IS_CONNECTED,
        PROP_IS_LOCAL,
        PROP_LAUNCH_ENVIRONMENT,
        PROP_IS_INITIAL,
        PROP_ALLOW_TIMED_LOGIN,
        PROP_HAVE_EXISTING_USER_ACCOUNTS,
        PROP_DOING_INITIAL_SETUP,
};

static void     gdm_display_class_init  (GdmDisplayClass *klass);
static void     gdm_display_init        (GdmDisplay      *self);
static void     gdm_display_finalize    (GObject         *object);
static void     queue_finish            (GdmDisplay      *self);
static void     _gdm_display_set_status (GdmDisplay *self,
                                         int         status);
static gboolean wants_initial_setup (GdmDisplay *self);
G_DEFINE_ABSTRACT_TYPE (GdmDisplay, gdm_display, G_TYPE_OBJECT)

static gboolean
chown_file (GFile   *file,
            uid_t    uid,
            gid_t    gid,
            GError **error)
{
        if (!g_file_set_attribute_uint32 (file, G_FILE_ATTRIBUTE_UNIX_UID, uid,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, error)) {
                return FALSE;
        }
        if (!g_file_set_attribute_uint32 (file, G_FILE_ATTRIBUTE_UNIX_GID, gid,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, error)) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
chown_recursively (GFile   *dir,
                   uid_t    uid,
                   gid_t    gid,
                   GError **error)
{
        GFile *file = NULL;
        GFileInfo *info = NULL;
        GFileEnumerator *enumerator = NULL;
        gboolean retval = FALSE;

        if (chown_file (dir, uid, gid, error) == FALSE) {
                goto out;
        }

        enumerator = g_file_enumerate_children (dir,
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE","
                                                G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                NULL, error);
        if (!enumerator) {
                goto out;
        }

        while ((info = g_file_enumerator_next_file (enumerator, NULL, error)) != NULL) {
                file = g_file_get_child (dir, g_file_info_get_name (info));

                if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
                        if (chown_recursively (file, uid, gid, error) == FALSE) {
                                goto out;
                        }
                } else if (chown_file (file, uid, gid, error) == FALSE) {
                        goto out;
                }

                g_clear_object (&file);
                g_clear_object (&info);
        }

        if (*error) {
                goto out;
        }

        retval = TRUE;
out:
        g_clear_object (&file);
        g_clear_object (&info);
        g_clear_object (&enumerator);

        return retval;
}

GQuark
gdm_display_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_display_error");
        }

        return ret;
}

time_t
gdm_display_get_creation_time (GdmDisplay *self)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), 0);

        return self->priv->creation_time;
}

int
gdm_display_get_status (GdmDisplay *self)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), 0);

        return self->priv->status;
}

const char *
gdm_display_get_session_id (GdmDisplay *self)
{
        return self->priv->session_id;
}

static GdmDisplayAccessFile *
_create_access_file_for_user (GdmDisplay  *self,
                              const char  *username,
                              GError     **error)
{
        GdmDisplayAccessFile *access_file;
        GError *file_error;

        access_file = gdm_display_access_file_new (username);

        file_error = NULL;
        if (!gdm_display_access_file_open (access_file, &file_error)) {
                g_propagate_error (error, file_error);
                return NULL;
        }

        return access_file;
}

gboolean
gdm_display_create_authority (GdmDisplay *self)
{
        GdmDisplayAccessFile *access_file;
        GError               *error;
        gboolean              res;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        g_return_val_if_fail (self->priv->access_file == NULL, FALSE);

        error = NULL;
        access_file = _create_access_file_for_user (self, GDM_USERNAME, &error);

        if (access_file == NULL) {
                g_critical ("could not create display access file: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_free (self->priv->x11_cookie);
        self->priv->x11_cookie = NULL;
        res = gdm_display_access_file_add_display (access_file,
                                                   self,
                                                   &self->priv->x11_cookie,
                                                   &self->priv->x11_cookie_size,
                                                   &error);

        if (! res) {

                g_critical ("could not add display to access file: %s", error->message);
                g_error_free (error);
                gdm_display_access_file_close (access_file);
                g_object_unref (access_file);
                return FALSE;
        }

        self->priv->access_file = access_file;

        return TRUE;
}

static void
setup_xhost_auth (XHostAddress              *host_entries)
{
        host_entries[0].family    = FamilyServerInterpreted;
        host_entries[0].address   = "localuser\0root";
        host_entries[0].length    = sizeof ("localuser\0root");
        host_entries[1].family    = FamilyServerInterpreted;
        host_entries[1].address   = "localuser\0" GDM_USERNAME;
        host_entries[1].length    = sizeof ("localuser\0" GDM_USERNAME);
        host_entries[2].family    = FamilyServerInterpreted;
        host_entries[2].address   = "localuser\0gnome-initial-setup";
        host_entries[2].length    = sizeof ("localuser\0gnome-initial-setup");
}

gboolean
gdm_display_add_user_authorization (GdmDisplay *self,
                                    const char *username,
                                    char      **filename,
                                    GError    **error)
{
        GdmDisplayAccessFile *access_file;
        GError               *access_file_error;
        gboolean              res;

        int                       i;
        XHostAddress              host_entries[3];
        xcb_void_cookie_t         cookies[3];

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: Adding authorization for user:%s on display %s", username, self->priv->x11_display_name);

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (self->priv->user_access_file != NULL) {
                g_set_error (error,
                             G_DBUS_ERROR,
                             G_DBUS_ERROR_ACCESS_DENIED,
                             "user access already assigned");
                return FALSE;
        }

        g_debug ("GdmDisplay: Adding user authorization for %s", username);

        access_file_error = NULL;
        access_file = _create_access_file_for_user (self,
                                                    username,
                                                    &access_file_error);

        if (access_file == NULL) {
                g_propagate_error (error, access_file_error);
                return FALSE;
        }

        res = gdm_display_access_file_add_display_with_cookie (access_file,
                                                               self,
                                                               self->priv->x11_cookie,
                                                               self->priv->x11_cookie_size,
                                                               &access_file_error);
        if (! res) {
                g_debug ("GdmDisplay: Unable to add user authorization for %s: %s",
                         username,
                         access_file_error->message);
                g_propagate_error (error, access_file_error);
                gdm_display_access_file_close (access_file);
                g_object_unref (access_file);
                return FALSE;
        }

        *filename = gdm_display_access_file_get_path (access_file);
        self->priv->user_access_file = access_file;

        g_debug ("GdmDisplay: Added user authorization for %s: %s", username, *filename);
        /* Remove access for the programs run by greeter now that the
         * user session is starting.
         */
        setup_xhost_auth (host_entries);

        for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                cookies[i] = xcb_change_hosts_checked (self->priv->xcb_connection,
                                                       XCB_HOST_MODE_DELETE,
                                                       host_entries[i].family,
                                                       host_entries[i].length,
                                                       (uint8_t *) host_entries[i].address);
        }

        for (i = 0; i < G_N_ELEMENTS (cookies); i++) {
                xcb_generic_error_t *xcb_error;

                xcb_error = xcb_request_check (self->priv->xcb_connection, cookies[i]);

                if (xcb_error != NULL) {
                        g_warning ("Failed to remove greeter program access to the display. Trying to proceed.");
                        free (xcb_error);
                }
        }

        return TRUE;
}

gboolean
gdm_display_remove_user_authorization (GdmDisplay *self,
                                       const char *username,
                                       GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: Removing authorization for user:%s on display %s", username, self->priv->x11_display_name);

        gdm_display_access_file_close (self->priv->user_access_file);

        return TRUE;
}

gboolean
gdm_display_get_x11_cookie (GdmDisplay  *self,
                            const char **x11_cookie,
                            gsize       *x11_cookie_size,
                            GError     **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (x11_cookie != NULL) {
                *x11_cookie = self->priv->x11_cookie;
        }

        if (x11_cookie_size != NULL) {
                *x11_cookie_size = self->priv->x11_cookie_size;
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_authority_file (GdmDisplay *self,
                                    char      **filename,
                                    GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        g_return_val_if_fail (filename != NULL, FALSE);

        if (self->priv->access_file != NULL) {
                *filename = gdm_display_access_file_get_path (self->priv->access_file);
        } else {
                *filename = NULL;
        }

        return TRUE;
}

gboolean
gdm_display_get_remote_hostname (GdmDisplay *self,
                                 char      **hostname,
                                 GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (hostname != NULL) {
                *hostname = g_strdup (self->priv->remote_hostname);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_number (GdmDisplay *self,
                                    int        *number,
                                    GError    **error)
{
       g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

       if (number != NULL) {
               *number = self->priv->x11_display_number;
       }

       return TRUE;
}

gboolean
gdm_display_get_seat_id (GdmDisplay *self,
                         char      **seat_id,
                         GError    **error)
{
       g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

       if (seat_id != NULL) {
               *seat_id = g_strdup (self->priv->seat_id);
       }

       return TRUE;
}

gboolean
gdm_display_is_initial (GdmDisplay  *self,
                        gboolean    *is_initial,
                        GError     **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (is_initial != NULL) {
                *is_initial = self->priv->is_initial;
        }

        return TRUE;
}

static gboolean
finish_idle (GdmDisplay *self)
{
        self->priv->finish_idle_id = 0;
        /* finish may end up finalizing object */
        gdm_display_finish (self);
        return FALSE;
}

static void
queue_finish (GdmDisplay *self)
{
        if (self->priv->finish_idle_id == 0) {
                self->priv->finish_idle_id = g_idle_add ((GSourceFunc)finish_idle, self);
        }
}

static void
_gdm_display_set_status (GdmDisplay *self,
                         int         status)
{
        if (status != self->priv->status) {
                self->priv->status = status;
                g_object_notify (G_OBJECT (self), "status");
        }
}

static gboolean
gdm_display_real_prepare (GdmDisplay *self)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: prepare display");

        _gdm_display_set_status (self, GDM_DISPLAY_PREPARED);

        return TRUE;
}

static void
look_for_existing_users_sync (GdmDisplay *self)
{
        GError *error = NULL;
        GVariant *call_result;
        GVariant *user_list;

        self->priv->accountsservice_proxy = g_dbus_proxy_new_sync (self->priv->connection,
                                                                   0, NULL,
                                                                   "org.freedesktop.Accounts",
                                                                   "/org/freedesktop/Accounts",
                                                                   "org.freedesktop.Accounts",
                                                                   NULL,
                                                                   &error);

        if (!self->priv->accountsservice_proxy) {
                g_warning ("Failed to contact accountsservice: %s", error->message);
                goto out;
        }

        call_result = g_dbus_proxy_call_sync (self->priv->accountsservice_proxy,
                                              "ListCachedUsers",
                                              NULL,
                                              0,
                                              -1,
                                              NULL,
                                              &error);

        if (!call_result) {
                g_warning ("Failed to list cached users: %s", error->message);
                goto out;
        }

        g_variant_get (call_result, "(@ao)", &user_list);
        self->priv->have_existing_user_accounts = g_variant_n_children (user_list) > 0;
        g_variant_unref (user_list);
        g_variant_unref (call_result);
out:
        g_clear_error (&error);
}

gboolean
gdm_display_prepare (GdmDisplay *self)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: Preparing display: %s", self->priv->id);

        /* FIXME: we should probably do this in a more global place,
         * asynchronously
         */
        look_for_existing_users_sync (self);

        self->priv->doing_initial_setup = wants_initial_setup (self);

        g_object_ref (self);
        ret = GDM_DISPLAY_GET_CLASS (self)->prepare (self);
        g_object_unref (self);

        return ret;
}

gboolean
gdm_display_manage (GdmDisplay *self)
{
        gboolean res;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: Managing display: %s", self->priv->id);

        /* If not explicitly prepared, do it now */
        if (self->priv->status == GDM_DISPLAY_UNMANAGED) {
                res = gdm_display_prepare (self);
                if (! res) {
                        return FALSE;
                }
        }

        g_timer_start (self->priv->server_timer);

        if (g_strcmp0 (self->priv->session_class, "greeter") == 0) {
                if (GDM_DISPLAY_GET_CLASS (self)->manage != NULL) {
                        GDM_DISPLAY_GET_CLASS (self)->manage (self);
                }
        }

        return TRUE;
}

gboolean
gdm_display_finish (GdmDisplay *self)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (self->priv->finish_idle_id != 0) {
                g_source_remove (self->priv->finish_idle_id);
                self->priv->finish_idle_id = 0;
        }

        _gdm_display_set_status (self, GDM_DISPLAY_FINISHED);

        g_debug ("GdmDisplay: finish display");

        return TRUE;
}

static void
gdm_display_disconnect (GdmDisplay *self)
{
        /* These 3 bits are reserved/unused by the X protocol */
        guint32 unused_bits = 0b11100000000000000000000000000000;
        XID highest_client, client;
        guint32 client_increment;
        const xcb_setup_t *setup;

        if (self->priv->xcb_connection == NULL) {
                return;
        }

        setup = xcb_get_setup (self->priv->xcb_connection);

        /* resource_id_mask is the bits given to each client for
         * addressing resources */
        highest_client = (XID) ~unused_bits & ~setup->resource_id_mask;
        client_increment = setup->resource_id_mask + 1;

        /* Kill every client but ourselves, then close our own connection
         */
        for (client = 0;
             client <= highest_client;
             client += client_increment) {

                if (client != setup->resource_id_base)
                        xcb_kill_client (self->priv->xcb_connection, client);
        }

        xcb_flush (self->priv->xcb_connection);

        g_clear_pointer (&self->priv->xcb_connection, xcb_disconnect);
}

gboolean
gdm_display_unmanage (GdmDisplay *self)
{
        gdouble elapsed;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        g_debug ("GdmDisplay: unmanage display");

        gdm_display_disconnect (self);

        g_timer_stop (self->priv->server_timer);

        if (self->priv->user_access_file != NULL) {
                gdm_display_access_file_close (self->priv->user_access_file);
                g_object_unref (self->priv->user_access_file);
                self->priv->user_access_file = NULL;
        }

        if (self->priv->access_file != NULL) {
                gdm_display_access_file_close (self->priv->access_file);
                g_object_unref (self->priv->access_file);
                self->priv->access_file = NULL;
        }

        elapsed = g_timer_elapsed (self->priv->server_timer, NULL);
        if (elapsed < 3) {
                g_warning ("GdmDisplay: display lasted %lf seconds", elapsed);
                _gdm_display_set_status (self, GDM_DISPLAY_FAILED);
        } else {
                _gdm_display_set_status (self, GDM_DISPLAY_UNMANAGED);
        }

        return TRUE;
}

gboolean
gdm_display_get_id (GdmDisplay         *self,
                    char              **id,
                    GError            **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (id != NULL) {
                *id = g_strdup (self->priv->id);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_name (GdmDisplay   *self,
                                  char        **x11_display,
                                  GError      **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (x11_display != NULL) {
                *x11_display = g_strdup (self->priv->x11_display_name);
        }

        return TRUE;
}

gboolean
gdm_display_is_local (GdmDisplay *self,
                      gboolean   *local,
                      GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        if (local != NULL) {
                *local = self->priv->is_local;
        }

        return TRUE;
}

static void
_gdm_display_set_id (GdmDisplay     *self,
                     const char     *id)
{
        g_debug ("GdmDisplay: id: %s", id);
        g_free (self->priv->id);
        self->priv->id = g_strdup (id);
}

static void
_gdm_display_set_seat_id (GdmDisplay     *self,
                          const char     *seat_id)
{
        g_debug ("GdmDisplay: seat id: %s", seat_id);
        g_free (self->priv->seat_id);
        self->priv->seat_id = g_strdup (seat_id);
}

static void
_gdm_display_set_session_id (GdmDisplay     *self,
                             const char     *session_id)
{
        g_debug ("GdmDisplay: session id: %s", session_id);
        g_free (self->priv->session_id);
        self->priv->session_id = g_strdup (session_id);
}

static void
_gdm_display_set_session_class (GdmDisplay *self,
                                const char *session_class)
{
        g_debug ("GdmDisplay: session class: %s", session_class);
        g_free (self->priv->session_class);
        self->priv->session_class = g_strdup (session_class);
}

static void
_gdm_display_set_session_type (GdmDisplay *self,
                               const char *session_type)
{
        g_debug ("GdmDisplay: session type: %s", session_type);
        g_free (self->priv->session_type);
        self->priv->session_type = g_strdup (session_type);
}

static void
_gdm_display_set_remote_hostname (GdmDisplay     *self,
                                  const char     *hostname)
{
        g_free (self->priv->remote_hostname);
        self->priv->remote_hostname = g_strdup (hostname);
}

static void
_gdm_display_set_x11_display_number (GdmDisplay     *self,
                                     int             num)
{
        self->priv->x11_display_number = num;
}

static void
_gdm_display_set_x11_display_name (GdmDisplay     *self,
                                   const char     *x11_display)
{
        g_free (self->priv->x11_display_name);
        self->priv->x11_display_name = g_strdup (x11_display);
}

static void
_gdm_display_set_x11_cookie (GdmDisplay     *self,
                             const char     *x11_cookie)
{
        g_free (self->priv->x11_cookie);
        self->priv->x11_cookie = g_strdup (x11_cookie);
}

static void
_gdm_display_set_is_local (GdmDisplay     *self,
                           gboolean        is_local)
{
        g_debug ("GdmDisplay: local: %s", is_local? "yes" : "no");
        self->priv->is_local = is_local;
}

static void
_gdm_display_set_launch_environment (GdmDisplay           *self,
                                     GdmLaunchEnvironment *launch_environment)
{
        g_clear_object (&self->priv->launch_environment);

        self->priv->launch_environment = g_object_ref (launch_environment);
}

static void
_gdm_display_set_is_initial (GdmDisplay     *self,
                             gboolean        initial)
{
        g_debug ("GdmDisplay: initial: %s", initial? "yes" : "no");
        self->priv->is_initial = initial;
}

static void
_gdm_display_set_allow_timed_login (GdmDisplay     *self,
                                    gboolean        allow_timed_login)
{
        g_debug ("GdmDisplay: allow timed login: %s", allow_timed_login? "yes" : "no");
        self->priv->allow_timed_login = allow_timed_login;
}

static void
gdm_display_set_property (GObject        *object,
                          guint           prop_id,
                          const GValue   *value,
                          GParamSpec     *pspec)
{
        GdmDisplay *self;

        self = GDM_DISPLAY (object);

        switch (prop_id) {
        case PROP_ID:
                _gdm_display_set_id (self, g_value_get_string (value));
                break;
        case PROP_STATUS:
                _gdm_display_set_status (self, g_value_get_int (value));
                break;
        case PROP_SEAT_ID:
                _gdm_display_set_seat_id (self, g_value_get_string (value));
                break;
        case PROP_SESSION_ID:
                _gdm_display_set_session_id (self, g_value_get_string (value));
                break;
        case PROP_SESSION_CLASS:
                _gdm_display_set_session_class (self, g_value_get_string (value));
                break;
        case PROP_SESSION_TYPE:
                _gdm_display_set_session_type (self, g_value_get_string (value));
                break;
        case PROP_REMOTE_HOSTNAME:
                _gdm_display_set_remote_hostname (self, g_value_get_string (value));
                break;
        case PROP_X11_DISPLAY_NUMBER:
                _gdm_display_set_x11_display_number (self, g_value_get_int (value));
                break;
        case PROP_X11_DISPLAY_NAME:
                _gdm_display_set_x11_display_name (self, g_value_get_string (value));
                break;
        case PROP_X11_COOKIE:
                _gdm_display_set_x11_cookie (self, g_value_get_string (value));
                break;
        case PROP_IS_LOCAL:
                _gdm_display_set_is_local (self, g_value_get_boolean (value));
                break;
        case PROP_ALLOW_TIMED_LOGIN:
                _gdm_display_set_allow_timed_login (self, g_value_get_boolean (value));
                break;
        case PROP_LAUNCH_ENVIRONMENT:
                _gdm_display_set_launch_environment (self, g_value_get_object (value));
                break;
        case PROP_IS_INITIAL:
                _gdm_display_set_is_initial (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_display_get_property (GObject        *object,
                          guint           prop_id,
                          GValue         *value,
                          GParamSpec     *pspec)
{
        GdmDisplay *self;

        self = GDM_DISPLAY (object);

        switch (prop_id) {
        case PROP_ID:
                g_value_set_string (value, self->priv->id);
                break;
        case PROP_STATUS:
                g_value_set_int (value, self->priv->status);
                break;
        case PROP_SEAT_ID:
                g_value_set_string (value, self->priv->seat_id);
                break;
        case PROP_SESSION_ID:
                g_value_set_string (value, self->priv->session_id);
                break;
        case PROP_SESSION_CLASS:
                g_value_set_string (value, self->priv->session_class);
                break;
        case PROP_SESSION_TYPE:
                g_value_set_string (value, self->priv->session_type);
                break;
        case PROP_REMOTE_HOSTNAME:
                g_value_set_string (value, self->priv->remote_hostname);
                break;
        case PROP_X11_DISPLAY_NUMBER:
                g_value_set_int (value, self->priv->x11_display_number);
                break;
        case PROP_X11_DISPLAY_NAME:
                g_value_set_string (value, self->priv->x11_display_name);
                break;
        case PROP_X11_COOKIE:
                g_value_set_string (value, self->priv->x11_cookie);
                break;
        case PROP_X11_AUTHORITY_FILE:
                g_value_take_string (value,
                                     self->priv->access_file?
                                     gdm_display_access_file_get_path (self->priv->access_file) : NULL);
                break;
        case PROP_IS_LOCAL:
                g_value_set_boolean (value, self->priv->is_local);
                break;
        case PROP_IS_CONNECTED:
                g_value_set_boolean (value, self->priv->xcb_connection != NULL);
                break;
        case PROP_LAUNCH_ENVIRONMENT:
                g_value_set_object (value, self->priv->launch_environment);
                break;
        case PROP_IS_INITIAL:
                g_value_set_boolean (value, self->priv->is_initial);
                break;
        case PROP_HAVE_EXISTING_USER_ACCOUNTS:
                g_value_set_boolean (value, self->priv->have_existing_user_accounts);
                break;
        case PROP_DOING_INITIAL_SETUP:
                g_value_set_boolean (value, self->priv->doing_initial_setup);
                break;
        case PROP_ALLOW_TIMED_LOGIN:
                g_value_set_boolean (value, self->priv->allow_timed_login);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
handle_get_id (GdmDBusDisplay        *skeleton,
               GDBusMethodInvocation *invocation,
               GdmDisplay            *self)
{
        char *id;

        gdm_display_get_id (self, &id, NULL);

        gdm_dbus_display_complete_get_id (skeleton, invocation, id);

        g_free (id);
        return TRUE;
}

static gboolean
handle_get_remote_hostname (GdmDBusDisplay        *skeleton,
                            GDBusMethodInvocation *invocation,
                            GdmDisplay            *self)
{
        char *hostname;

        gdm_display_get_remote_hostname (self, &hostname, NULL);

        gdm_dbus_display_complete_get_remote_hostname (skeleton,
                                                       invocation,
                                                       hostname ? hostname : "");

        g_free (hostname);
        return TRUE;
}

static gboolean
handle_get_seat_id (GdmDBusDisplay        *skeleton,
                    GDBusMethodInvocation *invocation,
                    GdmDisplay            *self)
{
        char *seat_id;

        seat_id = NULL;
        gdm_display_get_seat_id (self, &seat_id, NULL);

        if (seat_id == NULL) {
                seat_id = g_strdup ("");
        }
        gdm_dbus_display_complete_get_seat_id (skeleton, invocation, seat_id);

        g_free (seat_id);
        return TRUE;
}

static gboolean
handle_get_x11_display_name (GdmDBusDisplay        *skeleton,
                             GDBusMethodInvocation *invocation,
                             GdmDisplay            *self)
{
        char *name;

        gdm_display_get_x11_display_name (self, &name, NULL);

        gdm_dbus_display_complete_get_x11_display_name (skeleton, invocation, name);

        g_free (name);
        return TRUE;
}

static gboolean
handle_is_local (GdmDBusDisplay        *skeleton,
                 GDBusMethodInvocation *invocation,
                 GdmDisplay            *self)
{
        gboolean is_local;

        gdm_display_is_local (self, &is_local, NULL);

        gdm_dbus_display_complete_is_local (skeleton, invocation, is_local);

        return TRUE;
}

static gboolean
handle_is_initial (GdmDBusDisplay        *skeleton,
                   GDBusMethodInvocation *invocation,
                   GdmDisplay            *self)
{
        gboolean is_initial = FALSE;

        gdm_display_is_initial (self, &is_initial, NULL);

        gdm_dbus_display_complete_is_initial (skeleton, invocation, is_initial);

        return TRUE;
}

static gboolean
register_display (GdmDisplay *self)
{
        GError *error = NULL;

        error = NULL;
        self->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (self->priv->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        self->priv->object_skeleton = g_dbus_object_skeleton_new (self->priv->id);
        self->priv->display_skeleton = GDM_DBUS_DISPLAY (gdm_dbus_display_skeleton_new ());

        g_signal_connect (self->priv->display_skeleton, "handle-get-id",
                          G_CALLBACK (handle_get_id), self);
        g_signal_connect (self->priv->display_skeleton, "handle-get-remote-hostname",
                          G_CALLBACK (handle_get_remote_hostname), self);
        g_signal_connect (self->priv->display_skeleton, "handle-get-seat-id",
                          G_CALLBACK (handle_get_seat_id), self);
        g_signal_connect (self->priv->display_skeleton, "handle-get-x11-display-name",
                          G_CALLBACK (handle_get_x11_display_name), self);
        g_signal_connect (self->priv->display_skeleton, "handle-is-local",
                          G_CALLBACK (handle_is_local), self);
        g_signal_connect (self->priv->display_skeleton, "handle-is-initial",
                          G_CALLBACK (handle_is_initial), self);

        g_dbus_object_skeleton_add_interface (self->priv->object_skeleton,
                                              G_DBUS_INTERFACE_SKELETON (self->priv->display_skeleton));

        return TRUE;
}

/*
  dbus-send --system --print-reply --dest=org.gnome.DisplayManager /org/gnome/DisplayManager/Displays/1 org.freedesktop.DBus.Introspectable.Introspect
*/

static GObject *
gdm_display_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GdmDisplay      *self;
        gboolean         res;

        self = GDM_DISPLAY (G_OBJECT_CLASS (gdm_display_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));

        g_free (self->priv->id);
        self->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Displays/%lu",
                                          (gulong) self);

        res = register_display (self);
        if (! res) {
                g_warning ("Unable to register display with system bus");
        }

        return G_OBJECT (self);
}

static void
gdm_display_dispose (GObject *object)
{
        GdmDisplay *self;

        self = GDM_DISPLAY (object);

        g_debug ("GdmDisplay: Disposing display");

        if (self->priv->finish_idle_id != 0) {
                g_source_remove (self->priv->finish_idle_id);
                self->priv->finish_idle_id = 0;
        }
        gdm_display_cancel_scheduled_kill_greeter (self);
        g_clear_object (&self->priv->launch_environment);

        g_warn_if_fail (self->priv->status != GDM_DISPLAY_MANAGED);
        g_warn_if_fail (self->priv->user_access_file == NULL);
        g_warn_if_fail (self->priv->access_file == NULL);

        G_OBJECT_CLASS (gdm_display_parent_class)->dispose (object);
}

static void
gdm_display_class_init (GdmDisplayClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_display_get_property;
        object_class->set_property = gdm_display_set_property;
        object_class->constructor = gdm_display_constructor;
        object_class->dispose = gdm_display_dispose;
        object_class->finalize = gdm_display_finalize;

        klass->prepare = gdm_display_real_prepare;

        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_REMOTE_HOSTNAME,
                                         g_param_spec_string ("remote-hostname",
                                                              "remote-hostname",
                                                              "remote-hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NUMBER,
                                         g_param_spec_int ("x11-display-number",
                                                          "x11 display number",
                                                          "x11 display number",
                                                          -1,
                                                          G_MAXINT,
                                                          -1,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NAME,
                                         g_param_spec_string ("x11-display-name",
                                                              "x11-display-name",
                                                              "x11-display-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SEAT_ID,
                                         g_param_spec_string ("seat-id",
                                                              "seat id",
                                                              "seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_string ("session-id",
                                                              "session id",
                                                              "session id",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_CLASS,
                                         g_param_spec_string ("session-class",
                                                              NULL,
                                                              NULL,
                                                              "greeter",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_TYPE,
                                         g_param_spec_string ("session-type",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_IS_INITIAL,
                                         g_param_spec_boolean ("is-initial",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_ALLOW_TIMED_LOGIN,
                                         g_param_spec_boolean ("allow-timed-login",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_X11_COOKIE,
                                         g_param_spec_string ("x11-cookie",
                                                              "cookie",
                                                              "cookie",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("x11-authority-file",
                                                              "authority file",
                                                              "authority file",
                                                              NULL,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (object_class,
                                         PROP_IS_LOCAL,
                                         g_param_spec_boolean ("is-local",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_IS_CONNECTED,
                                         g_param_spec_boolean ("is-connected",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_HAVE_EXISTING_USER_ACCOUNTS,
                                         g_param_spec_boolean ("have-existing-user-accounts",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DOING_INITIAL_SETUP,
                                         g_param_spec_boolean ("doing-initial-setup",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_LAUNCH_ENVIRONMENT,
                                         g_param_spec_object ("launch-environment",
                                                              NULL,
                                                              NULL,
                                                              GDM_TYPE_LAUNCH_ENVIRONMENT,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_STATUS,
                                         g_param_spec_int ("status",
                                                           "status",
                                                           "status",
                                                           -1,
                                                           G_MAXINT,
                                                           GDM_DISPLAY_UNMANAGED,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmDisplayPrivate));
}

static void
gdm_display_init (GdmDisplay *self)
{

        self->priv = GDM_DISPLAY_GET_PRIVATE (self);

        self->priv->creation_time = time (NULL);
        self->priv->server_timer = g_timer_new ();
}

static void
gdm_display_finalize (GObject *object)
{
        GdmDisplay *self;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DISPLAY (object));

        self = GDM_DISPLAY (object);

        g_return_if_fail (self->priv != NULL);

        g_debug ("GdmDisplay: Finalizing display: %s", self->priv->id);
        g_free (self->priv->id);
        g_free (self->priv->seat_id);
        g_free (self->priv->session_class);
        g_free (self->priv->remote_hostname);
        g_free (self->priv->x11_display_name);
        g_free (self->priv->x11_cookie);

        g_clear_object (&self->priv->display_skeleton);
        g_clear_object (&self->priv->object_skeleton);
        g_clear_object (&self->priv->connection);
        g_clear_object (&self->priv->accountsservice_proxy);

        if (self->priv->access_file != NULL) {
                g_object_unref (self->priv->access_file);
        }

        if (self->priv->user_access_file != NULL) {
                g_object_unref (self->priv->user_access_file);
        }

        if (self->priv->server_timer != NULL) {
                g_timer_destroy (self->priv->server_timer);
        }

        G_OBJECT_CLASS (gdm_display_parent_class)->finalize (object);
}

GDBusObjectSkeleton *
gdm_display_get_object_skeleton (GdmDisplay *self)
{
        return self->priv->object_skeleton;
}

static void
on_launch_environment_session_opened (GdmLaunchEnvironment *launch_environment,
                                      GdmDisplay           *self)
{
        char       *session_id;

        g_debug ("GdmDisplay: Greeter session opened");
        session_id = gdm_launch_environment_get_session_id (launch_environment);
        _gdm_display_set_session_id (self, session_id);
        g_free (session_id);
}

static void
on_launch_environment_session_started (GdmLaunchEnvironment *launch_environment,
                                       GdmDisplay           *self)
{
        g_debug ("GdmDisplay: Greeter started");
}

static void
self_destruct (GdmDisplay *self)
{
        g_object_ref (self);
        if (gdm_display_get_status (self) == GDM_DISPLAY_MANAGED) {
                gdm_display_unmanage (self);
        }

        if (gdm_display_get_status (self) != GDM_DISPLAY_FINISHED) {
                queue_finish (self);
        }
        g_object_unref (self);
}

static void
on_launch_environment_session_stopped (GdmLaunchEnvironment *launch_environment,
                                       GdmDisplay           *self)
{
        g_debug ("GdmDisplay: Greeter stopped");
        self_destruct (self);
}

static void
on_launch_environment_session_exited (GdmLaunchEnvironment *launch_environment,
                                      int                   code,
                                      GdmDisplay           *self)
{
        g_debug ("GdmDisplay: Greeter exited: %d", code);
        self_destruct (self);
}

static void
on_launch_environment_session_died (GdmLaunchEnvironment *launch_environment,
                                    int                   signal,
                                    GdmDisplay           *self)
{
        g_debug ("GdmDisplay: Greeter died: %d", signal);
        self_destruct (self);
}

static gboolean
can_create_environment (const char *session_id)
{
        char *path;
        gboolean session_exists;

        path = g_strdup_printf (GNOME_SESSION_SESSIONS_PATH "/%s.session", session_id);
        session_exists = g_file_test (path, G_FILE_TEST_EXISTS);

        g_free (path);

        return session_exists;
}

#define ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT GDM_RUN_DIR "/gdm.ran-initial-setup"

static gboolean
already_done_initial_setup_on_this_boot (void)
{
        if (g_file_test (ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT, G_FILE_TEST_EXISTS))
                return TRUE;

        return FALSE;
}

static gboolean
kernel_cmdline_initial_setup_argument (const gchar  *contents,
                                       gchar       **initial_setup_argument,
                                       GError      **error)
{
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;
        gchar *match_group = NULL;

        g_return_val_if_fail (initial_setup_argument != NULL, FALSE);

        regex = g_regex_new ("\\bgnome.initial-setup=([^\\s]*)\\b", 0, 0, error);

        if (!regex)
            return FALSE;

        if (!g_regex_match (regex, contents, 0, &match_info)) {
                g_free (match_info);
                g_free (regex);

                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not match gnome.initial-setup= in kernel cmdline");

                return FALSE;
        }

        match_group = g_match_info_fetch (match_info, 1);

        if (!match_group) {
                g_free (match_info);
                g_free (regex);

                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not match gnome.initial-setup= in kernel cmdline");

                return FALSE;
        }

        *initial_setup_argument = match_group;

        g_free (match_info);
        g_free (regex);

        return TRUE;
}

/* Function returns true if we had a force state in the kernel
 * cmdline */
static gboolean
kernel_cmdline_initial_setup_force_state (gboolean *force_state)
{
        GError *error = NULL;
        gchar *contents = NULL;
        gchar *setup_argument = NULL;

        g_return_val_if_fail (force_state != NULL, FALSE);

        if (!g_file_get_contents ("/proc/cmdline", &contents, NULL, &error)) {
                g_debug ("GdmDisplay: Could not check kernel parameters, not forcing initial setup: %s",
                          error->message);
                g_clear_error (&error);
                return FALSE;
        }

        g_debug ("GdmDisplay: Checking kernel command buffer %s", contents);

        if (!kernel_cmdline_initial_setup_argument (contents, &setup_argument, &error)) {
                g_debug ("GdmDisplay: Failed to read kernel commandline: %s", error->message);
                g_clear_pointer (&contents, g_free);
                return FALSE;
        }

        g_clear_pointer (&contents, g_free);

        /* Poor-man's check for truthy or falsey values */
        *force_state = setup_argument[0] == '1';

        g_free (setup_argument);
        return TRUE;
}

static gboolean
wants_initial_setup (GdmDisplay *self)
{
        gboolean enabled = FALSE;
        gboolean forced = FALSE;

        if (already_done_initial_setup_on_this_boot ()) {
                return FALSE;
        }

        if (kernel_cmdline_initial_setup_force_state (&forced)) {
                if (forced) {
                        g_debug ("GdmDisplay: Forcing gnome-initial-setup");
                        return TRUE;
                }

                g_debug ("GdmDisplay: Forceing no gnome-initial-setup");
                return FALSE;
        }

        /* don't run initial-setup on remote displays
         */
        if (!self->priv->is_local) {
                return FALSE;
        }

        /* don't run if the system has existing users */
        if (self->priv->have_existing_user_accounts) {
                return FALSE;
        }

        /* don't run if initial-setup is unavailable */
        if (!can_create_environment ("gnome-initial-setup")) {
                return FALSE;
        }

        if (!gdm_settings_direct_get_boolean (GDM_KEY_INITIAL_SETUP_ENABLE, &enabled)) {
                return FALSE;
        }

        return enabled;
}

void
gdm_display_start_greeter_session (GdmDisplay *self)
{
        GdmSession    *session;
        char          *display_name;
        char          *seat_id;
        char          *hostname;
        char          *auth_file = NULL;

        g_return_if_fail (g_strcmp0 (self->priv->session_class, "greeter") == 0);

        g_debug ("GdmDisplay: Running greeter");

        display_name = NULL;
        seat_id = NULL;
        hostname = NULL;

        g_object_get (self,
                      "x11-display-name", &display_name,
                      "seat-id", &seat_id,
                      "remote-hostname", &hostname,
                      NULL);
        if (self->priv->access_file != NULL) {
                auth_file = gdm_display_access_file_get_path (self->priv->access_file);
        }

        g_debug ("GdmDisplay: Creating greeter for %s %s", display_name, hostname);

        g_signal_connect_object (self->priv->launch_environment,
                                 "opened",
                                 G_CALLBACK (on_launch_environment_session_opened),
                                 self, 0);
        g_signal_connect_object (self->priv->launch_environment,
                                 "started",
                                 G_CALLBACK (on_launch_environment_session_started),
                                 self, 0);
        g_signal_connect_object (self->priv->launch_environment,
                                 "stopped",
                                 G_CALLBACK (on_launch_environment_session_stopped),
                                 self, 0);
        g_signal_connect_object (self->priv->launch_environment,
                                 "exited",
                                 G_CALLBACK (on_launch_environment_session_exited),
                                 self, 0);
        g_signal_connect_object (self->priv->launch_environment,
                                 "died",
                                 G_CALLBACK (on_launch_environment_session_died),
                                 self, 0);

        if (auth_file != NULL) {
                g_object_set (self->priv->launch_environment,
                              "x11-authority-file", auth_file,
                              NULL);
        }

        gdm_launch_environment_start (self->priv->launch_environment);

        session = gdm_launch_environment_get_session (self->priv->launch_environment);
        g_object_set (G_OBJECT (session),
                      "display-is-initial", self->priv->is_initial,
                      NULL);

        g_free (display_name);
        g_free (seat_id);
        g_free (hostname);
        g_free (auth_file);
}

static void
chown_initial_setup_home_dir (void)
{
        GFile *dir;
        GError *error;
        char *gis_dir_path;
        char *gis_uid_path;
        char *gis_uid_contents;
        struct passwd *pwe;
        uid_t uid;

        if (!gdm_get_pwent_for_name (INITIAL_SETUP_USERNAME, &pwe)) {
                g_warning ("Unknown user %s", INITIAL_SETUP_USERNAME);
                return;
        }

        gis_dir_path = g_strdup (pwe->pw_dir);

        gis_uid_path = g_build_filename (gis_dir_path,
                                         "gnome-initial-setup-uid",
                                         NULL);
        if (!g_file_get_contents (gis_uid_path, &gis_uid_contents, NULL, NULL)) {
                g_warning ("Unable to read %s", gis_uid_path);
                goto out;
        }

        uid = (uid_t) atoi (gis_uid_contents);
        pwe = getpwuid (uid);
        if (uid == 0 || pwe == NULL) {
                g_warning ("UID '%s' in %s is not valid", gis_uid_contents, gis_uid_path);
                goto out;
        }

        error = NULL;
        dir = g_file_new_for_path (gis_dir_path);
        if (!chown_recursively (dir, pwe->pw_uid, pwe->pw_gid, &error)) {
                g_warning ("Failed to change ownership for %s: %s", gis_dir_path, error->message);
                g_error_free (error);
        }
        g_object_unref (dir);
out:
        g_free (gis_uid_contents);
        g_free (gis_uid_path);
        g_free (gis_dir_path);
}

void
gdm_display_stop_greeter_session (GdmDisplay *self)
{
        GError *error = NULL;

        if (self->priv->launch_environment != NULL) {

                g_signal_handlers_disconnect_by_func (self->priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_opened),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_started),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_stopped),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_exited),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_died),
                                                      self);
                gdm_launch_environment_stop (self->priv->launch_environment);
                g_clear_object (&self->priv->launch_environment);
        }

        if (self->priv->doing_initial_setup) {
                chown_initial_setup_home_dir ();

                if (!g_file_set_contents (ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT,
                                          "1",
                                          1,
                                          &error)) {
                        g_warning ("GdmDisplay: Could not write initial-setup-done marker to %s: %s",
                                   ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT,
                                   error->message);
                        g_clear_error (&error);
                }
        }
}

static gboolean
gdm_display_kill_greeter (GdmDisplay *self)
{
        gdm_display_stop_greeter_session (self);
        gdm_display_unmanage (self);
        gdm_display_finish (self);

        self->priv->kill_greeter_id = 0;
        return FALSE;
}

void
gdm_display_schedule_kill_greeter (GdmDisplay *self, guint seconds)
{
        gdm_display_cancel_scheduled_kill_greeter (self);
        self->priv->kill_greeter_id =
                g_timeout_add_seconds (seconds, (GSourceFunc)gdm_display_kill_greeter, self);
}

void
gdm_display_cancel_scheduled_kill_greeter (GdmDisplay *self)
{
        if (self->priv->kill_greeter_id != 0) {
                g_source_remove (self->priv->kill_greeter_id);
                self->priv->kill_greeter_id = 0;
        }
}

static xcb_window_t
get_root_window (xcb_connection_t *connection,
                 int               screen_number)
{
        xcb_screen_t *screen = NULL;
        xcb_screen_iterator_t iter;

        iter = xcb_setup_roots_iterator (xcb_get_setup (connection));
        while (iter.rem) {
                if (screen_number == 0)
                        screen = iter.data;
                screen_number--;
                xcb_screen_next (&iter);
        }

        if (screen != NULL) {
                return screen->root;
        }

        return XCB_WINDOW_NONE;
}

static void
gdm_display_set_windowpath (GdmDisplay *self)
{
        /* setting WINDOWPATH for clients */
        xcb_intern_atom_cookie_t atom_cookie;
        xcb_intern_atom_reply_t *atom_reply = NULL;
        xcb_get_property_cookie_t get_property_cookie;
        xcb_get_property_reply_t *get_property_reply = NULL;
        xcb_window_t root_window = XCB_WINDOW_NONE;
        const char *windowpath;
        char *newwindowpath;
        uint32_t num;
        char nums[10];
        int numn;

        atom_cookie = xcb_intern_atom (self->priv->xcb_connection, 0, strlen("XFree86_VT"), "XFree86_VT");
        atom_reply = xcb_intern_atom_reply (self->priv->xcb_connection, atom_cookie, NULL);

        if (atom_reply == NULL) {
                g_debug ("no XFree86_VT atom\n");
                goto out;
        }

        root_window = get_root_window (self->priv->xcb_connection,
                                       self->priv->xcb_screen_number);

        if (root_window == XCB_WINDOW_NONE) {
                g_debug ("couldn't find root window\n");
                goto out;
        }

        get_property_cookie = xcb_get_property (self->priv->xcb_connection,
                                                FALSE,
                                                root_window,
                                                atom_reply->atom,
                                                XCB_ATOM_INTEGER,
                                                0,
                                                1);

        get_property_reply = xcb_get_property_reply (self->priv->xcb_connection, get_property_cookie, NULL);

        if (get_property_reply == NULL) {
                g_debug ("no XFree86_VT property\n");
                goto out;
        }

        num = ((uint32_t *) xcb_get_property_value (get_property_reply))[0];

        windowpath = getenv ("WINDOWPATH");
        numn = snprintf (nums, sizeof (nums), "%u", num);
        if (!windowpath) {
                newwindowpath = malloc (numn + 1);
                sprintf (newwindowpath, "%s", nums);
        } else {
                newwindowpath = malloc (strlen (windowpath) + 1 + numn + 1);
                sprintf (newwindowpath, "%s:%s", windowpath, nums);
        }

        g_setenv ("WINDOWPATH", newwindowpath, TRUE);
out:
        g_clear_pointer (&atom_reply, free);
        g_clear_pointer (&get_property_reply, free);
}

gboolean
gdm_display_connect (GdmDisplay *self)
{
        xcb_auth_info_t *auth_info = NULL;
        gboolean ret;

        ret = FALSE;

        g_debug ("GdmDisplay: Server is ready - opening display %s", self->priv->x11_display_name);

        /* Get access to the display independent of current hostname */
        if (self->priv->x11_cookie != NULL) {
                auth_info = g_alloca (sizeof (xcb_auth_info_t));

                auth_info->namelen = strlen ("MIT-MAGIC-COOKIE-1");
                auth_info->name = "MIT-MAGIC-COOKIE-1";
                auth_info->datalen = self->priv->x11_cookie_size;
                auth_info->data = self->priv->x11_cookie;

        }

        self->priv->xcb_connection = xcb_connect_to_display_with_auth_info (self->priv->x11_display_name,
                                                                            auth_info,
                                                                            &self->priv->xcb_screen_number);

        if (xcb_connection_has_error (self->priv->xcb_connection)) {
                g_clear_pointer (&self->priv->xcb_connection, xcb_disconnect);
                g_warning ("Unable to connect to display %s", self->priv->x11_display_name);
                ret = FALSE;
        } else if (self->priv->is_local) {
                XHostAddress              host_entries[3];
                xcb_void_cookie_t         cookies[3];
                int                       i;

                g_debug ("GdmDisplay: Connected to display %s", self->priv->x11_display_name);
                ret = TRUE;

                /* Give programs access to the display independent of current hostname
                 */
                setup_xhost_auth (host_entries);

                for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                        cookies[i] = xcb_change_hosts_checked (self->priv->xcb_connection,
                                                               XCB_HOST_MODE_INSERT,
                                                               host_entries[i].family,
                                                               host_entries[i].length,
                                                               (uint8_t *) host_entries[i].address);
                }

                for (i = 0; i < G_N_ELEMENTS (cookies); i++) {
                        xcb_generic_error_t *xcb_error;

                        xcb_error = xcb_request_check (self->priv->xcb_connection, cookies[i]);

                        if (xcb_error != NULL) {
                                g_debug ("Failed to give system user '%s' access to the display. Trying to proceed.", host_entries[i].address + sizeof ("localuser"));
                                free (xcb_error);
                        } else {
                                g_debug ("Gave system user '%s' access to the display.", host_entries[i].address + sizeof ("localuser"));
                        }
                }

                gdm_display_set_windowpath (self);
        } else {
                g_debug ("GdmDisplay: Connected to display %s", self->priv->x11_display_name);
                ret = TRUE;
        }

        if (ret == TRUE) {
                g_object_notify (G_OBJECT (self), "is-connected");
        }

        return ret;
}

