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

#ifdef ENABLE_X11_SUPPORT
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#endif

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-display-glue.h"
#include "gdm-display-access-file.h"
#include "gdm-launch-environment.h"
#include "gdm-remote-display.h"

#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#include "gdm-launch-environment.h"
#include "gdm-dbus-util.h"

#define GNOME_SESSION_SESSIONS_PATH DATADIR "/gnome-session/sessions"

typedef struct _GdmDisplayPrivate
{
        GObject               parent;

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

        char                 *x11_cookie;
        gsize                 x11_cookie_size;
        GdmDisplayAccessFile *access_file;

        guint                 finish_idle_id;

#ifdef ENABLE_X11_SUPPORT
        xcb_connection_t     *xcb_connection;
        int                   xcb_screen_number;
#endif

        GDBusConnection      *connection;
        GdmDisplayAccessFile *user_access_file;

        GdmDBusDisplay       *display_skeleton;
        GDBusObjectSkeleton  *object_skeleton;

        GDBusProxy           *accountsservice_proxy;

        /* this spawns and controls the greeter session */
        GdmLaunchEnvironment *launch_environment;

        guint                 is_local : 1;
        guint                 is_initial : 1;
        guint                 allow_timed_login : 1;
        guint                 have_existing_user_accounts : 1;
        guint                 doing_initial_setup : 1;
        guint                 session_registered : 1;

        GStrv                 supported_session_types;
} GdmDisplayPrivate;

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
        PROP_SESSION_REGISTERED,
        PROP_SUPPORTED_SESSION_TYPES,
};

static void     gdm_display_class_init  (GdmDisplayClass *klass);
static void     gdm_display_init        (GdmDisplay      *self);
static void     gdm_display_finalize    (GObject         *object);
static void     queue_finish            (GdmDisplay      *self);
static void     _gdm_display_set_status (GdmDisplay *self,
                                         int         status);
static gboolean wants_initial_setup (GdmDisplay *self);
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GdmDisplay, gdm_display, G_TYPE_OBJECT)

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
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), 0);

        priv = gdm_display_get_instance_private (self);
        return priv->creation_time;
}

int
gdm_display_get_status (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), 0);

        priv = gdm_display_get_instance_private (self);
        return priv->status;
}

const char *
gdm_display_get_session_id (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), NULL);

        priv = gdm_display_get_instance_private (self);
        return priv->session_id;
}

static GdmDisplayAccessFile *
_create_access_file_for_user (GdmDisplay  *self,
                              const char  *username,
                              GError     **error)
{
        GdmDisplayAccessFile *access_file;

        access_file = gdm_display_access_file_new (username);
        if (!gdm_display_access_file_open (access_file, error)) {
                return NULL;
        }

        return access_file;
}

gboolean
gdm_display_create_authority (GdmDisplay *self)
{
        GdmDisplayPrivate    *priv;
        g_autoptr(GdmDisplayAccessFile) access_file = NULL;
        g_autoptr(GError) error = NULL;
        gboolean              res;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        g_return_val_if_fail (priv->access_file == NULL, FALSE);

        access_file = _create_access_file_for_user (self, GDM_USERNAME, &error);

        if (access_file == NULL) {
                g_critical ("could not create display access file: %s", error->message);
                return FALSE;
        }

        g_free (priv->x11_cookie);
        priv->x11_cookie = NULL;
        res = gdm_display_access_file_add_display (access_file,
                                                   self,
                                                   &priv->x11_cookie,
                                                   &priv->x11_cookie_size,
                                                   &error);

        if (! res) {
                g_critical ("could not add display to access file: %s", error->message);
                gdm_display_access_file_close (access_file);
                return FALSE;
        }

        priv->access_file = g_steal_pointer (&access_file);

        return TRUE;
}

#ifdef ENABLE_X11_SUPPORT
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
#endif

gboolean
gdm_display_add_user_authorization (GdmDisplay *self,
                                    const char *username,
                                    char      **filename,
                                    GError    **error)
{
#ifdef ENABLE_X11_SUPPORT
        GdmDisplayPrivate    *priv;
        g_autoptr(GdmDisplayAccessFile) access_file = NULL;
        g_autoptr(GError) access_file_error = NULL;
        gboolean              res;

        int                       i;
        XHostAddress              host_entries[3];
        xcb_void_cookie_t         cookies[3];

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        g_return_val_if_fail (username != NULL, FALSE);
        g_return_val_if_fail (filename != NULL, FALSE);

        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Adding authorization for user:%s on display %s", username, priv->x11_display_name);

        if (priv->user_access_file != NULL) {
                g_set_error (error,
                             G_DBUS_ERROR,
                             G_DBUS_ERROR_ACCESS_DENIED,
                             "user access already assigned");
                return FALSE;
        }

        g_debug ("GdmDisplay: Adding user authorization for %s", username);

        access_file = _create_access_file_for_user (self,
                                                    username,
                                                    &access_file_error);

        if (access_file == NULL) {
                g_propagate_error (error, g_steal_pointer (&access_file_error));
                return FALSE;
        }

        res = gdm_display_access_file_add_display_with_cookie (access_file,
                                                               self,
                                                               priv->x11_cookie,
                                                               priv->x11_cookie_size,
                                                               &access_file_error);
        if (! res) {
                g_debug ("GdmDisplay: Unable to add user authorization for %s: %s",
                         username,
                         access_file_error->message);
                g_propagate_error (error, g_steal_pointer (&access_file_error));
                gdm_display_access_file_close (access_file);
                return FALSE;
        }

        *filename = gdm_display_access_file_get_path (access_file);
        priv->user_access_file = g_steal_pointer (&access_file);

        g_debug ("GdmDisplay: Added user authorization for %s: %s", username, *filename);
        /* Remove access for the programs run by greeter now that the
         * user session is starting.
         */
        setup_xhost_auth (host_entries);

        for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                cookies[i] = xcb_change_hosts_checked (priv->xcb_connection,
                                                       XCB_HOST_MODE_DELETE,
                                                       host_entries[i].family,
                                                       host_entries[i].length,
                                                       (uint8_t *) host_entries[i].address);
        }

        for (i = 0; i < G_N_ELEMENTS (cookies); i++) {
                xcb_generic_error_t *xcb_error;

                xcb_error = xcb_request_check (priv->xcb_connection, cookies[i]);

                if (xcb_error != NULL) {
                        g_warning ("Failed to remove greeter program access to the display. Trying to proceed.");
                        free (xcb_error);
                }
        }

        return TRUE;
#else
    return FALSE;
#endif
}

gboolean
gdm_display_remove_user_authorization (GdmDisplay *self,
                                       const char *username,
                                       GError    **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        g_return_val_if_fail (username != NULL, FALSE);

        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Removing authorization for user:%s on display %s", username, priv->x11_display_name);

        gdm_display_access_file_close (priv->user_access_file);

        return TRUE;
}

gboolean
gdm_display_get_x11_cookie (GdmDisplay  *self,
                            const char **x11_cookie,
                            gsize       *x11_cookie_size,
                            GError     **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        if (x11_cookie != NULL) {
                *x11_cookie = priv->x11_cookie;
        }

        if (x11_cookie_size != NULL) {
                *x11_cookie_size = priv->x11_cookie_size;
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_authority_file (GdmDisplay *self,
                                    char      **filename,
                                    GError    **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        g_return_val_if_fail (filename != NULL, FALSE);

        priv = gdm_display_get_instance_private (self);
        if (priv->access_file != NULL) {
                *filename = gdm_display_access_file_get_path (priv->access_file);
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
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (hostname != NULL) {
                *hostname = g_strdup (priv->remote_hostname);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_number (GdmDisplay *self,
                                    int        *number,
                                    GError    **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (number != NULL) {
                *number = priv->x11_display_number;
        }

        return TRUE;
}

gboolean
gdm_display_get_seat_id (GdmDisplay *self,
                         char      **seat_id,
                         GError    **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (seat_id != NULL) {
                *seat_id = g_strdup (priv->seat_id);
        }

        return TRUE;
}

gboolean
gdm_display_is_initial (GdmDisplay  *self,
                        gboolean    *is_initial,
                        GError     **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (is_initial != NULL) {
                *is_initial = priv->is_initial;
        }

        return TRUE;
}

static gboolean
finish_idle (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        priv->finish_idle_id = 0;
        /* finish may end up finalizing object */
        gdm_display_finish (self);
        return FALSE;
}

static void
queue_finish (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        if (priv->finish_idle_id == 0) {
                priv->finish_idle_id = g_idle_add ((GSourceFunc)finish_idle, self);
        }
}

static void
_gdm_display_set_status (GdmDisplay *self,
                         int         status)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        if (status != priv->status) {
                priv->status = status;
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

static gboolean
look_for_existing_users_sync (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;
        g_autoptr(GError) error = NULL;
        g_autoptr(GVariant) call_result = NULL;
        g_autoptr(GVariant) user_list = NULL;

        priv = gdm_display_get_instance_private (self);
        priv->accountsservice_proxy = g_dbus_proxy_new_sync (priv->connection,
                                                             0, NULL,
                                                             "org.freedesktop.Accounts",
                                                             "/org/freedesktop/Accounts",
                                                             "org.freedesktop.Accounts",
                                                             NULL,
                                                             &error);

        if (!priv->accountsservice_proxy) {
                g_critical ("Failed to contact accountsservice: %s", error->message);
                return FALSE;
        }

        call_result = g_dbus_proxy_call_sync (priv->accountsservice_proxy,
                                              "ListCachedUsers",
                                              NULL,
                                              0,
                                              -1,
                                              NULL,
                                              &error);

        if (!call_result) {
                g_critical ("Failed to list cached users: %s", error->message);
                return FALSE;
        }

        g_variant_get (call_result, "(@ao)", &user_list);
        priv->have_existing_user_accounts = g_variant_n_children (user_list) > 0;

        return TRUE;
}

gboolean
gdm_display_prepare (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Preparing display: %s", priv->id);

        /* FIXME: we should probably do this in a more global place,
         * asynchronously
         */
        if (!look_for_existing_users_sync (self)) {
                exit (EXIT_FAILURE);
        }

        priv->doing_initial_setup = wants_initial_setup (self);

        g_object_ref (self);
        ret = GDM_DISPLAY_GET_CLASS (self)->prepare (self);
        g_object_unref (self);

        return ret;
}

gboolean
gdm_display_manage (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;
        gboolean res;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Managing display: %s", priv->id);

        /* If not explicitly prepared, do it now */
        if (priv->status == GDM_DISPLAY_UNMANAGED) {
                res = gdm_display_prepare (self);
                if (! res) {
                        return FALSE;
                }
        }

        if (g_strcmp0 (priv->session_class, "greeter") == 0) {
                if (GDM_DISPLAY_GET_CLASS (self)->manage != NULL) {
                        GDM_DISPLAY_GET_CLASS (self)->manage (self);
                }
        }

        return TRUE;
}

gboolean
gdm_display_finish (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        g_clear_handle_id (&priv->finish_idle_id, g_source_remove);

        _gdm_display_set_status (self, GDM_DISPLAY_FINISHED);

        g_debug ("GdmDisplay: finish display");

        return TRUE;
}

static void
gdm_display_disconnect (GdmDisplay *self)
{
#ifdef ENABLE_X11_SUPPORT
        GdmDisplayPrivate *priv;
        /* These 3 bits are reserved/unused by the X protocol */
        guint32 unused_bits = 0b11100000000000000000000000000000;
        XID highest_client, client;
        guint32 client_increment;
        const xcb_setup_t *setup;

        priv = gdm_display_get_instance_private (self);

        if (priv->xcb_connection == NULL) {
                return;
        }

        setup = xcb_get_setup (priv->xcb_connection);

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
                        xcb_kill_client (priv->xcb_connection, client);
        }

        xcb_flush (priv->xcb_connection);

        g_clear_pointer (&priv->xcb_connection, xcb_disconnect);
#endif
}

gboolean
gdm_display_unmanage (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        gdm_display_disconnect (self);

        if (priv->user_access_file != NULL) {
                gdm_display_access_file_close (priv->user_access_file);
                g_object_unref (priv->user_access_file);
                priv->user_access_file = NULL;
        }

        if (priv->access_file != NULL) {
                gdm_display_access_file_close (priv->access_file);
                g_object_unref (priv->access_file);
                priv->access_file = NULL;
        }

        if (!priv->session_registered) {
                g_warning ("GdmDisplay: Session never registered, failing");
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
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (id != NULL) {
                *id = g_strdup (priv->id);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_name (GdmDisplay   *self,
                                  char        **x11_display,
                                  GError      **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (x11_display != NULL) {
                *x11_display = g_strdup (priv->x11_display_name);
        }

        return TRUE;
}

gboolean
gdm_display_is_local (GdmDisplay *self,
                      gboolean   *local,
                      GError    **error)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);
        if (local != NULL) {
                *local = priv->is_local;
        }

        return TRUE;
}

static void
_gdm_display_set_id (GdmDisplay     *self,
                     const char     *id)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: id: %s", id);
        g_free (priv->id);
        priv->id = g_strdup (id);
}

static void
_gdm_display_set_seat_id (GdmDisplay     *self,
                          const char     *seat_id)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: seat id: %s", seat_id);
        g_free (priv->seat_id);
        priv->seat_id = g_strdup (seat_id);
}

static void
_gdm_display_set_session_id (GdmDisplay     *self,
                             const char     *session_id)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: session id: %s", session_id);
        g_free (priv->session_id);
        priv->session_id = g_strdup (session_id);
}

static void
_gdm_display_set_session_class (GdmDisplay *self,
                                const char *session_class)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: session class: %s", session_class);
        g_free (priv->session_class);
        priv->session_class = g_strdup (session_class);
}

static void
_gdm_display_set_session_type (GdmDisplay *self,
                               const char *session_type)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: session type: %s", session_type);
        g_free (priv->session_type);
        priv->session_type = g_strdup (session_type);
}

static void
_gdm_display_set_remote_hostname (GdmDisplay     *self,
                                  const char     *hostname)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_free (priv->remote_hostname);
        priv->remote_hostname = g_strdup (hostname);
}

static void
_gdm_display_set_x11_display_number (GdmDisplay     *self,
                                     int             num)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        priv->x11_display_number = num;
}

static void
_gdm_display_set_x11_display_name (GdmDisplay     *self,
                                   const char     *x11_display)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_free (priv->x11_display_name);
        priv->x11_display_name = g_strdup (x11_display);
}

static void
_gdm_display_set_x11_cookie (GdmDisplay     *self,
                             const char     *x11_cookie)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_free (priv->x11_cookie);
        priv->x11_cookie = g_strdup (x11_cookie);
}

static void
_gdm_display_set_is_local (GdmDisplay     *self,
                           gboolean        is_local)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: local: %s", is_local? "yes" : "no");
        priv->is_local = is_local;
}

static void
_gdm_display_set_session_registered (GdmDisplay     *self,
                                     gboolean        registered)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: session registered: %s", registered? "yes" : "no");
        priv->session_registered = registered;
}

static void
_gdm_display_set_launch_environment (GdmDisplay           *self,
                                     GdmLaunchEnvironment *launch_environment)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);

        g_clear_object (&priv->launch_environment);

        priv->launch_environment = g_object_ref (launch_environment);
}

static void
_gdm_display_set_is_initial (GdmDisplay     *self,
                             gboolean        initial)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: initial: %s", initial? "yes" : "no");
        priv->is_initial = initial;
}

static void
_gdm_display_set_allow_timed_login (GdmDisplay     *self,
                                    gboolean        allow_timed_login)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: allow timed login: %s", allow_timed_login? "yes" : "no");
        priv->allow_timed_login = allow_timed_login;
}

static void
_gdm_display_set_supported_session_types (GdmDisplay         *self,
                                          const char * const *supported_session_types)

{
        GdmDisplayPrivate *priv;
        g_autofree char *supported_session_types_string = NULL;

	if (supported_session_types != NULL)
          supported_session_types_string = g_strjoinv (":", (GStrv) supported_session_types);

        priv = gdm_display_get_instance_private (self);
        g_debug ("GdmDisplay: supported session types: %s", supported_session_types_string);
        g_strfreev (priv->supported_session_types);
        priv->supported_session_types = g_strdupv ((GStrv) supported_session_types);
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
        case PROP_SESSION_REGISTERED:
                _gdm_display_set_session_registered (self, g_value_get_boolean (value));
                break;
        case PROP_SUPPORTED_SESSION_TYPES:
                _gdm_display_set_supported_session_types (self, g_value_get_boxed (value));
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
        GdmDisplayPrivate *priv;

        self = GDM_DISPLAY (object);
        priv = gdm_display_get_instance_private (self);

        switch (prop_id) {
        case PROP_ID:
                g_value_set_string (value, priv->id);
                break;
        case PROP_STATUS:
                g_value_set_int (value, priv->status);
                break;
        case PROP_SEAT_ID:
                g_value_set_string (value, priv->seat_id);
                break;
        case PROP_SESSION_ID:
                g_value_set_string (value, priv->session_id);
                break;
        case PROP_SESSION_CLASS:
                g_value_set_string (value, priv->session_class);
                break;
        case PROP_SESSION_TYPE:
                g_value_set_string (value, priv->session_type);
                break;
        case PROP_REMOTE_HOSTNAME:
                g_value_set_string (value, priv->remote_hostname);
                break;
        case PROP_X11_DISPLAY_NUMBER:
                g_value_set_int (value, priv->x11_display_number);
                break;
        case PROP_X11_DISPLAY_NAME:
                g_value_set_string (value, priv->x11_display_name);
                break;
        case PROP_X11_COOKIE:
                g_value_set_string (value, priv->x11_cookie);
                break;
        case PROP_X11_AUTHORITY_FILE:
                g_value_take_string (value,
                                     priv->access_file?
                                     gdm_display_access_file_get_path (priv->access_file) : NULL);
                break;
        case PROP_IS_LOCAL:
                g_value_set_boolean (value, priv->is_local);
                break;
        case PROP_IS_CONNECTED:
#ifdef ENABLE_X11_SUPPORT
                g_value_set_boolean (value, priv->xcb_connection != NULL);
#else
                g_value_set_boolean (value, FALSE);
#endif
                break;
        case PROP_LAUNCH_ENVIRONMENT:
                g_value_set_object (value, priv->launch_environment);
                break;
        case PROP_IS_INITIAL:
                g_value_set_boolean (value, priv->is_initial);
                break;
        case PROP_HAVE_EXISTING_USER_ACCOUNTS:
                g_value_set_boolean (value, priv->have_existing_user_accounts);
                break;
        case PROP_DOING_INITIAL_SETUP:
                g_value_set_boolean (value, priv->doing_initial_setup);
                break;
        case PROP_SESSION_REGISTERED:
                g_value_set_boolean (value, priv->session_registered);
                break;
        case PROP_ALLOW_TIMED_LOGIN:
                g_value_set_boolean (value, priv->allow_timed_login);
                break;
        case PROP_SUPPORTED_SESSION_TYPES:
                g_value_set_boxed (value, priv->supported_session_types);
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
        g_autofree char *id = NULL;

        gdm_display_get_id (self, &id, NULL);

        gdm_dbus_display_complete_get_id (skeleton, invocation, id);

        return TRUE;
}

static gboolean
handle_get_remote_hostname (GdmDBusDisplay        *skeleton,
                            GDBusMethodInvocation *invocation,
                            GdmDisplay            *self)
{
        g_autofree char *hostname = NULL;

        gdm_display_get_remote_hostname (self, &hostname, NULL);

        gdm_dbus_display_complete_get_remote_hostname (skeleton,
                                                       invocation,
                                                       hostname ? hostname : "");

        return TRUE;
}

static gboolean
handle_get_seat_id (GdmDBusDisplay        *skeleton,
                    GDBusMethodInvocation *invocation,
                    GdmDisplay            *self)
{
        g_autofree char *seat_id = NULL;

        gdm_display_get_seat_id (self, &seat_id, NULL);

        if (seat_id == NULL) {
                seat_id = g_strdup ("");
        }
        gdm_dbus_display_complete_get_seat_id (skeleton, invocation, seat_id);

        return TRUE;
}

static gboolean
handle_get_x11_display_name (GdmDBusDisplay        *skeleton,
                             GDBusMethodInvocation *invocation,
                             GdmDisplay            *self)
{
        g_autofree char *name = NULL;

        gdm_display_get_x11_display_name (self, &name, NULL);

        gdm_dbus_display_complete_get_x11_display_name (skeleton, invocation, name);

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
        GdmDisplayPrivate *priv;
        g_autoptr(GError) error = NULL;

        priv = gdm_display_get_instance_private (self);

        priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (priv->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                exit (EXIT_FAILURE);
        }

        priv->object_skeleton = g_dbus_object_skeleton_new (priv->id);
        priv->display_skeleton = GDM_DBUS_DISPLAY (gdm_dbus_display_skeleton_new ());

        g_signal_connect_object (priv->display_skeleton, "handle-get-id",
                                 G_CALLBACK (handle_get_id), self, 0);
        g_signal_connect_object (priv->display_skeleton, "handle-get-remote-hostname",
                                 G_CALLBACK (handle_get_remote_hostname), self, 0);
        g_signal_connect_object (priv->display_skeleton, "handle-get-seat-id",
                                 G_CALLBACK (handle_get_seat_id), self, 0);
        g_signal_connect_object (priv->display_skeleton, "handle-get-x11-display-name",
                                 G_CALLBACK (handle_get_x11_display_name), self, 0);
        g_signal_connect_object (priv->display_skeleton, "handle-is-local",
                                 G_CALLBACK (handle_is_local), self, 0);
        g_signal_connect_object (priv->display_skeleton, "handle-is-initial",
                                 G_CALLBACK (handle_is_initial), self, 0);

        g_dbus_object_skeleton_add_interface (priv->object_skeleton,
                                              G_DBUS_INTERFACE_SKELETON (priv->display_skeleton));

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
        GdmDisplay        *self;
        GdmDisplayPrivate *priv;
        gboolean           res;

        self = GDM_DISPLAY (G_OBJECT_CLASS (gdm_display_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));

        priv = gdm_display_get_instance_private (self);

        g_free (priv->id);
        priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Displays/%lu",
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
        GdmDisplayPrivate *priv;

        self = GDM_DISPLAY (object);
        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Disposing display");

        g_clear_handle_id (&priv->finish_idle_id, g_source_remove);
        g_clear_object (&priv->launch_environment);
        g_clear_pointer (&priv->supported_session_types, g_strfreev);

        g_warn_if_fail (priv->status != GDM_DISPLAY_MANAGED);
        g_warn_if_fail (priv->user_access_file == NULL);
        g_warn_if_fail (priv->access_file == NULL);

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
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_REMOTE_HOSTNAME,
                                         g_param_spec_string ("remote-hostname",
                                                              "remote-hostname",
                                                              "remote-hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NUMBER,
                                         g_param_spec_int ("x11-display-number",
                                                          "x11 display number",
                                                          "x11 display number",
                                                          -1,
                                                          G_MAXINT,
                                                          -1,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY_NAME,
                                         g_param_spec_string ("x11-display-name",
                                                              "x11-display-name",
                                                              "x11-display-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SEAT_ID,
                                         g_param_spec_string ("seat-id",
                                                              "seat id",
                                                              "seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_string ("session-id",
                                                              "session id",
                                                              "session id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_CLASS,
                                         g_param_spec_string ("session-class",
                                                              NULL,
                                                              NULL,
                                                              "greeter",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_TYPE,
                                         g_param_spec_string ("session-type",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_IS_INITIAL,
                                         g_param_spec_boolean ("is-initial",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_ALLOW_TIMED_LOGIN,
                                         g_param_spec_boolean ("allow-timed-login",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_X11_COOKIE,
                                         g_param_spec_string ("x11-cookie",
                                                              "cookie",
                                                              "cookie",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("x11-authority-file",
                                                              "authority file",
                                                              "authority file",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_IS_LOCAL,
                                         g_param_spec_boolean ("is-local",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_IS_CONNECTED,
                                         g_param_spec_boolean ("is-connected",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_HAVE_EXISTING_USER_ACCOUNTS,
                                         g_param_spec_boolean ("have-existing-user-accounts",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DOING_INITIAL_SETUP,
                                         g_param_spec_boolean ("doing-initial-setup",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_REGISTERED,
                                         g_param_spec_boolean ("session-registered",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_LAUNCH_ENVIRONMENT,
                                         g_param_spec_object ("launch-environment",
                                                              NULL,
                                                              NULL,
                                                              GDM_TYPE_LAUNCH_ENVIRONMENT,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_STATUS,
                                         g_param_spec_int ("status",
                                                           "status",
                                                           "status",
                                                           -1,
                                                           G_MAXINT,
                                                           GDM_DISPLAY_UNMANAGED,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_SUPPORTED_SESSION_TYPES,
                                         g_param_spec_boxed ("supported-session-types",
                                                             "supported session types",
                                                             "supported session types",
                                                             G_TYPE_STRV,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gdm_display_init (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        priv = gdm_display_get_instance_private (self);

        priv->creation_time = time (NULL);
}

static void
gdm_display_finalize (GObject *object)
{
        GdmDisplay *self;
        GdmDisplayPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DISPLAY (object));

        self = GDM_DISPLAY (object);
        priv = gdm_display_get_instance_private (self);

        g_return_if_fail (priv != NULL);

        g_debug ("GdmDisplay: Finalizing display: %s", priv->id);
        g_free (priv->id);
        g_free (priv->seat_id);
        g_free (priv->session_class);
        g_free (priv->remote_hostname);
        g_free (priv->x11_display_name);
        g_free (priv->x11_cookie);

        g_clear_object (&priv->display_skeleton);
        g_clear_object (&priv->object_skeleton);
        g_clear_object (&priv->connection);
        g_clear_object (&priv->accountsservice_proxy);

        if (priv->access_file != NULL) {
                g_object_unref (priv->access_file);
        }

        if (priv->user_access_file != NULL) {
                g_object_unref (priv->user_access_file);
        }

        G_OBJECT_CLASS (gdm_display_parent_class)->finalize (object);
}

GDBusObjectSkeleton *
gdm_display_get_object_skeleton (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), NULL);

        priv = gdm_display_get_instance_private (self);
        return priv->object_skeleton;
}

static void
on_launch_environment_session_opened (GdmLaunchEnvironment *launch_environment,
                                      GdmDisplay           *self)
{
        g_autofree char *session_id = NULL;

        g_debug ("GdmDisplay: Greeter session opened");
        session_id = gdm_launch_environment_get_session_id (launch_environment);
        g_object_set (G_OBJECT (self), "session-id", session_id, NULL);
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

        g_debug ("GdmDisplay: initiating display self-destruct");
        gdm_display_unmanage (self);

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
        g_autofree char *path = NULL;
        gboolean session_exists;

        path = g_strdup_printf (GNOME_SESSION_SESSIONS_PATH "/%s.session", session_id);
        session_exists = g_file_test (path, G_FILE_TEST_EXISTS);

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
        g_autoptr(GRegex) regex = NULL;
        g_autoptr(GMatchInfo) match_info = NULL;
        g_autofree gchar *match_group = NULL;

        g_return_val_if_fail (initial_setup_argument != NULL, FALSE);

        regex = g_regex_new ("\\bgnome.initial-setup=([^\\s]*)\\b", 0, 0, error);

        if (!regex)
            return FALSE;

        if (!g_regex_match (regex, contents, 0, &match_info)) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not match gnome.initial-setup= in kernel cmdline");

                return FALSE;
        }

        match_group = g_match_info_fetch (match_info, 1);

        if (!match_group) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not match gnome.initial-setup= in kernel cmdline");

                return FALSE;
        }

        *initial_setup_argument = g_steal_pointer (&match_group);

        return TRUE;
}

/* Function returns true if we had a force state in the kernel
 * cmdline */
static gboolean
kernel_cmdline_initial_setup_force_state (gboolean *force_state)
{
        g_autoptr(GError) error = NULL;
        g_autofree gchar *contents = NULL;
        g_autofree gchar *setup_argument = NULL;

        g_return_val_if_fail (force_state != NULL, FALSE);

        if (!g_file_get_contents ("/proc/cmdline", &contents, NULL, &error)) {
                g_debug ("GdmDisplay: Could not check kernel parameters, not forcing initial setup: %s",
                          error->message);
                return FALSE;
        }

        g_debug ("GdmDisplay: Checking kernel command buffer %s", contents);

        if (!kernel_cmdline_initial_setup_argument (contents, &setup_argument, &error)) {
                g_debug ("GdmDisplay: Failed to read kernel commandline: %s", error->message);
                return FALSE;
        }

        /* Poor-man's check for truthy or falsey values */
        *force_state = setup_argument[0] == '1';

        return TRUE;
}

static gboolean
wants_initial_setup (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;
        gboolean enabled = FALSE;
        gboolean forced = FALSE;

        priv = gdm_display_get_instance_private (self);

        if (already_done_initial_setup_on_this_boot ()) {
                return FALSE;
        }

        if (kernel_cmdline_initial_setup_force_state (&forced)) {
                if (forced) {
                        g_debug ("GdmDisplay: Forcing gnome-initial-setup");
                        return TRUE;
                }

                g_debug ("GdmDisplay: Forcing no gnome-initial-setup");
                return FALSE;
        }

        /* don't run initial-setup on remote displays
         */
        if (!priv->is_local) {
                return FALSE;
        }

        /* don't run if the system has existing users */
        if (priv->have_existing_user_accounts) {
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

gboolean
gdm_display_prepare_greeter_session (GdmDisplay          *self,
                                     GdmDynamicUserStore *dyn_user_store,
                                     uid_t               *ret_uid)
{
        g_autoptr (GError) error = NULL;
        GdmDisplayPrivate *priv;
        uid_t uid;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);
        priv = gdm_display_get_instance_private (self);
        g_return_val_if_fail (g_strcmp0 (priv->session_class, "greeter") == 0, FALSE);

        if (!gdm_launch_environment_ensure_uid (priv->launch_environment,
                                                dyn_user_store, &uid, &error)) {
                g_warning ("GdmDisplay: Failed to allocate UID for greeter: %s",
                           error->message);
                return FALSE;
        }

        if (ret_uid != NULL)
                *ret_uid = uid;

        return TRUE;
}

void
gdm_display_start_greeter_session (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;
        GdmSession    *session;
        g_autofree char *display_name = NULL;
        g_autofree char *seat_id = NULL;
        g_autofree char *hostname = NULL;
        g_autofree char *auth_file = NULL;

        g_return_if_fail (GDM_IS_DISPLAY (self));

        priv = gdm_display_get_instance_private (self);
        g_return_if_fail (g_strcmp0 (priv->session_class, "greeter") == 0);

        g_debug ("GdmDisplay: Running greeter");

        g_object_get (self,
                      "x11-display-name", &display_name,
                      "seat-id", &seat_id,
                      "remote-hostname", &hostname,
                      NULL);
        if (priv->access_file != NULL) {
                auth_file = gdm_display_access_file_get_path (priv->access_file);
        }

        g_debug ("GdmDisplay: Creating greeter for %s %s", display_name, hostname);

        g_signal_connect_object (priv->launch_environment,
                                 "opened",
                                 G_CALLBACK (on_launch_environment_session_opened),
                                 self, 0);
        g_signal_connect_object (priv->launch_environment,
                                 "started",
                                 G_CALLBACK (on_launch_environment_session_started),
                                 self, 0);
        g_signal_connect_object (priv->launch_environment,
                                 "stopped",
                                 G_CALLBACK (on_launch_environment_session_stopped),
                                 self, 0);
        g_signal_connect_object (priv->launch_environment,
                                 "exited",
                                 G_CALLBACK (on_launch_environment_session_exited),
                                 self, 0);
        g_signal_connect_object (priv->launch_environment,
                                 "died",
                                 G_CALLBACK (on_launch_environment_session_died),
                                 self, 0);

        if (auth_file != NULL) {
                g_object_set (priv->launch_environment,
                              "x11-authority-file", auth_file,
                              NULL);
        }

        gdm_launch_environment_start (priv->launch_environment);

        session = gdm_launch_environment_get_session (priv->launch_environment);
        g_object_set (G_OBJECT (session),
                      "display-is-initial", priv->is_initial,
                      "supported-session-types", priv->supported_session_types,
                      NULL);
}

void
gdm_display_stop_greeter_session (GdmDisplay *self)
{
        GdmDisplayPrivate *priv;

        g_return_if_fail (GDM_IS_DISPLAY (self));

        priv = gdm_display_get_instance_private (self);

        if (priv->launch_environment != NULL) {

                g_signal_handlers_disconnect_by_func (priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_opened),
                                                      self);
                g_signal_handlers_disconnect_by_func (priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_started),
                                                      self);
                g_signal_handlers_disconnect_by_func (priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_stopped),
                                                      self);
                g_signal_handlers_disconnect_by_func (priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_exited),
                                                      self);
                g_signal_handlers_disconnect_by_func (priv->launch_environment,
                                                      G_CALLBACK (on_launch_environment_session_died),
                                                      self);
                gdm_launch_environment_stop (priv->launch_environment);
                g_clear_object (&priv->launch_environment);
        }
}

#ifdef ENABLE_X11_SUPPORT
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
        GdmDisplayPrivate *priv;
        /* setting WINDOWPATH for clients */
        xcb_intern_atom_cookie_t atom_cookie;
        xcb_intern_atom_reply_t *atom_reply = NULL;
        xcb_get_property_cookie_t get_property_cookie;
        xcb_get_property_reply_t *get_property_reply = NULL;
        xcb_window_t root_window = XCB_WINDOW_NONE;
        const char *windowpath;
        g_autofree gchar *newwindowpath = NULL;
        uint32_t num;

        priv = gdm_display_get_instance_private (self);

        atom_cookie = xcb_intern_atom (priv->xcb_connection, 0, strlen("XFree86_VT"), "XFree86_VT");
        atom_reply = xcb_intern_atom_reply (priv->xcb_connection, atom_cookie, NULL);

        if (atom_reply == NULL) {
                g_debug ("no XFree86_VT atom\n");
                goto out;
        }

        root_window = get_root_window (priv->xcb_connection,
                                       priv->xcb_screen_number);

        if (root_window == XCB_WINDOW_NONE) {
                g_debug ("couldn't find root window\n");
                goto out;
        }

        get_property_cookie = xcb_get_property (priv->xcb_connection,
                                                FALSE,
                                                root_window,
                                                atom_reply->atom,
                                                XCB_ATOM_INTEGER,
                                                0,
                                                1);

        get_property_reply = xcb_get_property_reply (priv->xcb_connection, get_property_cookie, NULL);

        if (get_property_reply == NULL) {
                g_debug ("no XFree86_VT property\n");
                goto out;
        }

        num = ((uint32_t *) xcb_get_property_value (get_property_reply))[0];

        windowpath = getenv ("WINDOWPATH");
        if (!windowpath) {
                newwindowpath = g_strdup_printf ("%u", num);
        } else {
                newwindowpath = g_strdup_printf ("%s:%u", windowpath, num);
        }

        g_setenv ("WINDOWPATH", newwindowpath, TRUE);
out:
        g_clear_pointer (&atom_reply, free);
        g_clear_pointer (&get_property_reply, free);
}
#endif

gboolean
gdm_display_connect (GdmDisplay *self)
{
#ifdef ENABLE_X11_SUPPORT
        GdmDisplayPrivate *priv;
        xcb_auth_info_t *auth_info = NULL;
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (self), FALSE);

        priv = gdm_display_get_instance_private (self);

        g_debug ("GdmDisplay: Server is ready - opening display %s", priv->x11_display_name);

        /* Get access to the display independent of current hostname */
        if (priv->x11_cookie != NULL) {
                auth_info = g_alloca (sizeof (xcb_auth_info_t));

                auth_info->namelen = strlen ("MIT-MAGIC-COOKIE-1");
                auth_info->name = "MIT-MAGIC-COOKIE-1";
                auth_info->datalen = priv->x11_cookie_size;
                auth_info->data = priv->x11_cookie;

        }

        priv->xcb_connection = xcb_connect_to_display_with_auth_info (priv->x11_display_name,
                                                                      auth_info,
                                                                      &priv->xcb_screen_number);

        if (xcb_connection_has_error (priv->xcb_connection)) {
                g_clear_pointer (&priv->xcb_connection, xcb_disconnect);
                g_warning ("Unable to connect to display %s", priv->x11_display_name);
                ret = FALSE;
        } else if (priv->is_local) {
                XHostAddress              host_entries[3];
                xcb_void_cookie_t         cookies[3];
                int                       i;

                g_debug ("GdmDisplay: Connected to display %s", priv->x11_display_name);
                ret = TRUE;

                /* Give programs access to the display independent of current hostname
                 */
                setup_xhost_auth (host_entries);

                for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                        cookies[i] = xcb_change_hosts_checked (priv->xcb_connection,
                                                               XCB_HOST_MODE_INSERT,
                                                               host_entries[i].family,
                                                               host_entries[i].length,
                                                               (uint8_t *) host_entries[i].address);
                }

                for (i = 0; i < G_N_ELEMENTS (cookies); i++) {
                        xcb_generic_error_t *xcb_error;

                        xcb_error = xcb_request_check (priv->xcb_connection, cookies[i]);

                        if (xcb_error != NULL) {
                                g_debug ("Failed to give system user '%s' access to the display. Trying to proceed.", host_entries[i].address + sizeof ("localuser"));
                                free (xcb_error);
                        } else {
                                g_debug ("Gave system user '%s' access to the display.", host_entries[i].address + sizeof ("localuser"));
                        }
                }

                gdm_display_set_windowpath (self);
        } else {
                g_debug ("GdmDisplay: Connected to display %s", priv->x11_display_name);
                ret = TRUE;
        }

        if (ret == TRUE) {
                g_object_notify (G_OBJECT (self), "is-connected");
        }

        return ret;
#else
    return FALSE;
#endif
}
