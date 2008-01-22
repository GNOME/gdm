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
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-display-glue.h"
#include "gdm-display-access-file.h"

#include "gdm-slave-proxy.h"

static guint32 display_serial = 1;

#define GDM_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_DISPLAY, GdmDisplayPrivate))

#define DEFAULT_SLAVE_COMMAND LIBEXECDIR"/gdm-simple-slave"

struct GdmDisplayPrivate
{
        char            *id;
        char            *seat_id;

        char            *remote_hostname;
        int              x11_display_number;
        char            *x11_display_name;
        int              status;
        time_t           creation_time;
        char            *slave_command;

        char                 *x11_cookie;
        gsize                 x11_cookie_size;
        GdmDisplayAccessFile *access_file;

        gboolean         is_local;
        guint            finish_idle_id;

        GdmSlaveProxy   *slave_proxy;
        DBusGConnection *connection;
        GdmDisplayAccessFile *user_access_file;
};

enum {
        PROP_0,
        PROP_ID,
        PROP_SEAT_ID,
        PROP_REMOTE_HOSTNAME,
        PROP_X11_DISPLAY_NUMBER,
        PROP_X11_DISPLAY_NAME,
        PROP_X11_COOKIE,
        PROP_X11_AUTHORITY_FILE,
        PROP_IS_LOCAL,
        PROP_SLAVE_COMMAND,
};

static void     gdm_display_class_init  (GdmDisplayClass *klass);
static void     gdm_display_init        (GdmDisplay      *display);
static void     gdm_display_finalize    (GObject         *object);

G_DEFINE_ABSTRACT_TYPE (GdmDisplay, gdm_display, G_TYPE_OBJECT)

GQuark
gdm_display_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_display_error");
        }

        return ret;
}

static guint32
get_next_display_serial (void)
{
        guint32 serial;

        serial = display_serial++;

        if ((gint32)display_serial < 0) {
                display_serial = 1;
        }

        return serial;
}

time_t
gdm_display_get_creation_time (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), 0);

        return display->priv->creation_time;
}

int
gdm_display_get_status (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), 0);

        return display->priv->status;
}

static GdmDisplayAccessFile *
_create_access_file_for_user (GdmDisplay  *display,
                              const char  *username,
                              GError     **error)
{
        GdmDisplayAccessFile *access_file;
        GError *file_error;

        access_file = gdm_display_access_file_new (username);

        file_error = NULL;
        if (!gdm_display_access_file_open (access_file, &file_error)) {
                g_propagate_error (error, file_error);
                return FALSE;
        }

        return access_file;
}

static gboolean
gdm_display_real_create_authority (GdmDisplay *display)
{
        GdmDisplayAccessFile *access_file;
        GError *error;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);
        g_return_val_if_fail (display->priv->access_file == NULL, FALSE);

        error = NULL;
        access_file = _create_access_file_for_user (display, "gdm", &error);

        if (access_file == NULL) {
            g_critical ("could not create display access file: %s", error->message);
            g_error_free (error);
            return FALSE;
        }

        if (!gdm_display_access_file_add_display (access_file, display,
                                                  &display->priv->x11_cookie,
                                                  &display->priv->x11_cookie_size,
                                                  &error)) {

                g_critical ("could not add display to access file: %s", error->message);
                g_error_free (error);
                gdm_display_access_file_close (access_file);
                g_object_unref (access_file);
                return FALSE;
        }

        display->priv->access_file = access_file;

        return TRUE;
}

gboolean
gdm_display_create_authority (GdmDisplay *display)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->create_authority (display);
        g_object_unref (display);

        return ret;
}

static gboolean
gdm_display_real_add_user_authorization (GdmDisplay *display,
                                         const char *username,
                                         char      **filename,
                                         GError    **error)
{
        GdmDisplayAccessFile *access_file;
        GError               *access_file_error;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);
        g_return_val_if_fail (display->priv->access_file != NULL, FALSE);

        g_debug ("GdmDisplay: Adding user authorization for %s", username);

        access_file_error = NULL;
        access_file = _create_access_file_for_user (display,
                                                    username,
                                                    &access_file_error);

        if (access_file == NULL) {
            g_propagate_error (error, access_file_error);
            return FALSE;
        }

        if (!gdm_display_access_file_add_display_with_cookie (access_file,
                                                              display,
                                                              display->priv->x11_cookie,
                                                              display->priv->x11_cookie_size,
                                                              &access_file_error)) {
                g_debug ("GdmDisplay: Unable to add user authorization for %s: %s",
                         username,
                         access_file_error->message);
                g_propagate_error (error, access_file_error);
                gdm_display_access_file_close (access_file);
                g_object_unref (access_file);
                return FALSE;
        }

        *filename = gdm_display_access_file_get_path (access_file);
        display->priv->user_access_file = access_file;

        g_debug ("GdmDisplay: Added user authorization for %s: %s", username, *filename);

        return TRUE;
}

gboolean
gdm_display_add_user_authorization (GdmDisplay *display,
                                    const char *username,
                                    char      **filename,
                                    GError    **error)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("Adding authorization for user:%s on display %s", username, display->priv->x11_display_name);

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->add_user_authorization (display, username, filename, error);
        g_object_unref (display);

        return ret;
}

static gboolean
gdm_display_real_remove_user_authorization (GdmDisplay *display,
                                            const char *username,
                                            GError    **error)
{
        gdm_display_access_file_close (display->priv->user_access_file);

        return TRUE;
}

gboolean
gdm_display_remove_user_authorization (GdmDisplay *display,
                                       const char *username,
                                       GError    **error)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("Removing authorization for user:%s on display %s", username, display->priv->x11_display_name);

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->remove_user_authorization (display, username, error);
        g_object_unref (display);

        return ret;
}

gboolean
gdm_display_get_x11_cookie (GdmDisplay *display,
                            char      **x11_cookie,
                            gsize      *x11_cookie_size,
                            GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        if (x11_cookie != NULL) {
                *x11_cookie = g_memdup (display->priv->x11_cookie,
                                        display->priv->x11_cookie_size);
        }

        if (x11_cookie_size != NULL) {
                *x11_cookie_size = display->priv->x11_cookie_size;
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_authority_file (GdmDisplay *display,
                                    char      **filename,
                                    GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);
        g_return_val_if_fail (filename != NULL, FALSE);

        if (display->priv->access_file != NULL) {
                *filename = gdm_display_access_file_get_path (display->priv->access_file);
        } else {
                *filename = NULL;
        }

        return TRUE;
}

gboolean
gdm_display_get_remote_hostname (GdmDisplay *display,
                                 char      **hostname,
                                 GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        if (hostname != NULL) {
                *hostname = g_strdup (display->priv->remote_hostname);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_number (GdmDisplay *display,
                                    int        *number,
                                    GError    **error)
{
       g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

       if (number != NULL) {
               *number = display->priv->x11_display_number;
       }

       return TRUE;
}

static gboolean
finish_idle (GdmDisplay *display)
{
        gdm_display_finish (display);
        display->priv->finish_idle_id = 0;
        return FALSE;
}

static void
queue_finish (GdmDisplay *display)
{
        if (display->priv->finish_idle_id == 0) {
                display->priv->finish_idle_id = g_idle_add ((GSourceFunc)finish_idle, display);
        }
}

static void
slave_exited (GdmSlaveProxy       *proxy,
              int                  code,
              GdmDisplay          *display)
{
        g_debug ("Slave exited: %d", code);

        queue_finish (display);
}

static void
slave_died (GdmSlaveProxy       *proxy,
            int                  signum,
            GdmDisplay          *display)
{
        g_debug ("Slave died: %d", signum);

        queue_finish (display);
}

static gboolean
gdm_display_real_manage (GdmDisplay *display)
{
        char *command;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("GdmDisplay manage display");

        display->priv->status = GDM_DISPLAY_MANAGED;

        g_assert (display->priv->slave_proxy == NULL);

        display->priv->slave_proxy = gdm_slave_proxy_new ();
        g_signal_connect (display->priv->slave_proxy,
                          "exited",
                          G_CALLBACK (slave_exited),
                          display);
        g_signal_connect (display->priv->slave_proxy,
                          "died",
                          G_CALLBACK (slave_died),
                          display);

        command = g_strdup_printf ("%s --display-id %s",
                                   display->priv->slave_command,
                                   display->priv->id);

        gdm_slave_proxy_set_command (display->priv->slave_proxy, command);
        g_free (command);

        gdm_slave_proxy_start (display->priv->slave_proxy);

        return TRUE;
}

gboolean
gdm_display_manage (GdmDisplay *display)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("Managing display: %s", display->priv->id);

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->manage (display);
        g_object_unref (display);

        return ret;
}

static gboolean
gdm_display_real_finish (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        display->priv->status = GDM_DISPLAY_FINISHED;

        g_debug ("GdmDisplay finish display");

        return TRUE;
}

gboolean
gdm_display_finish (GdmDisplay *display)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("Finishing display: %s", display->priv->id);

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->finish (display);
        g_object_unref (display);

        return ret;
}

static gboolean
gdm_display_real_unmanage (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        display->priv->status = GDM_DISPLAY_UNMANAGED;

        g_debug ("GdmDisplay: unmanage display");

        if (display->priv->slave_proxy != NULL) {
                gdm_slave_proxy_stop (display->priv->slave_proxy);

                g_object_unref (display->priv->slave_proxy);
                display->priv->slave_proxy = NULL;
        }

        if (display->priv->user_access_file != NULL) {
                gdm_display_access_file_close (display->priv->user_access_file);
                g_object_unref (display->priv->user_access_file);
                display->priv->user_access_file = NULL;
        }

        if (display->priv->user_access_file != NULL) {
                gdm_display_access_file_close (display->priv->access_file);
                g_object_unref (display->priv->access_file);
                display->priv->access_file = NULL;
        }

        return TRUE;
}

gboolean
gdm_display_unmanage (GdmDisplay *display)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("GdmDisplay: Unmanaging display");

        g_object_ref (display);
        ret = GDM_DISPLAY_GET_CLASS (display)->unmanage (display);
        g_object_unref (display);

        return ret;
}

gboolean
gdm_display_get_id (GdmDisplay         *display,
                    char              **id,
                    GError            **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        if (id != NULL) {
                *id = g_strdup (display->priv->id);
        }

        return TRUE;
}

gboolean
gdm_display_get_x11_display_name (GdmDisplay   *display,
                                  char        **x11_display,
                                  GError      **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        if (x11_display != NULL) {
                *x11_display = g_strdup (display->priv->x11_display_name);
        }

        return TRUE;
}

gboolean
gdm_display_is_local (GdmDisplay *display,
                      gboolean   *local,
                      GError    **error)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        if (local != NULL) {
                *local = display->priv->is_local;
        }

        return TRUE;
}

static void
_gdm_display_set_id (GdmDisplay     *display,
                     const char     *id)
{
        g_free (display->priv->id);
        display->priv->id = g_strdup (id);
}

static void
_gdm_display_set_seat_id (GdmDisplay     *display,
                          const char     *seat_id)
{
        g_free (display->priv->seat_id);
        display->priv->seat_id = g_strdup (seat_id);
}

static void
_gdm_display_set_remote_hostname (GdmDisplay     *display,
                                  const char     *hostname)
{
        g_free (display->priv->remote_hostname);
        display->priv->remote_hostname = g_strdup (hostname);
}

static void
_gdm_display_set_x11_display_number (GdmDisplay     *display,
                                     int             num)
{
        display->priv->x11_display_number = num;
}

static void
_gdm_display_set_x11_display_name (GdmDisplay     *display,
                                   const char     *x11_display)
{
        g_free (display->priv->x11_display_name);
        display->priv->x11_display_name = g_strdup (x11_display);
}

static void
_gdm_display_set_x11_cookie (GdmDisplay     *display,
                             const char     *x11_cookie)
{
        g_free (display->priv->x11_cookie);
        display->priv->x11_cookie = g_strdup (x11_cookie);
}

static void
_gdm_display_set_is_local (GdmDisplay     *display,
                           gboolean        is_local)
{
        display->priv->is_local = is_local;
}

static void
_gdm_display_set_slave_command (GdmDisplay     *display,
                                const char     *command)
{
        g_free (display->priv->slave_command);
        display->priv->slave_command = g_strdup (command);
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
        case PROP_SEAT_ID:
                _gdm_display_set_seat_id (self, g_value_get_string (value));
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
        case PROP_SLAVE_COMMAND:
                _gdm_display_set_slave_command (self, g_value_get_string (value));
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
        case PROP_SEAT_ID:
                g_value_set_string (value, self->priv->seat_id);
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
                                     gdm_display_access_file_get_path (self->priv->access_file));
                break;
        case PROP_IS_LOCAL:
                g_value_set_boolean (value, self->priv->is_local);
                break;
        case PROP_SLAVE_COMMAND:
                g_value_set_string (value, self->priv->slave_command);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
register_display (GdmDisplay *display)
{
        GError *error = NULL;

        error = NULL;
        display->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (display->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (display->priv->connection, display->priv->id, G_OBJECT (display));

        return TRUE;
}

/*
  dbus-send --system --print-reply --dest=org.gnome.DisplayManager /org/gnome/DisplayManager/Display1 org.freedesktop.DBus.Introspectable.Introspect
*/

static GObject *
gdm_display_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GdmDisplay      *display;
        GdmDisplayClass *klass;
        gboolean         res;

        klass = GDM_DISPLAY_CLASS (g_type_class_peek (GDM_TYPE_DISPLAY));

        display = GDM_DISPLAY (G_OBJECT_CLASS (gdm_display_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));

        display->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Display%u", get_next_display_serial ());

        res = register_display (display);
        if (! res) {
                g_warning ("Unable to register display with system bus");
        }

        return G_OBJECT (display);
}

static void
gdm_display_dispose (GObject *object)
{
        GdmDisplay *display;

        display = GDM_DISPLAY (object);

        if (display->priv->finish_idle_id > 0) {
                g_source_remove (display->priv->finish_idle_id);
                display->priv->finish_idle_id = 0;
        }

        g_debug ("GdmDisplay: Disposing display");
        gdm_display_unmanage (display);

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

        klass->create_authority = gdm_display_real_create_authority;
        klass->add_user_authorization = gdm_display_real_add_user_authorization;
        klass->remove_user_authorization = gdm_display_real_remove_user_authorization;
        klass->manage = gdm_display_real_manage;
        klass->finish = gdm_display_real_finish;
        klass->unmanage = gdm_display_real_unmanage;

        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_SEAT_ID,
                                         g_param_spec_string ("seat-id",
                                                              "seat id",
                                                              "seat id",
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
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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
                                         PROP_SLAVE_COMMAND,
                                         g_param_spec_string ("slave-command",
                                                              "slave command",
                                                              "slave command",
                                                              DEFAULT_SLAVE_COMMAND,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmDisplayPrivate));

        dbus_g_object_type_install_info (GDM_TYPE_DISPLAY, &dbus_glib_gdm_display_object_info);
}

static void
gdm_display_init (GdmDisplay *display)
{

        display->priv = GDM_DISPLAY_GET_PRIVATE (display);

        display->priv->status = GDM_DISPLAY_UNMANAGED;
        display->priv->creation_time = time (NULL);
}

static void
gdm_display_finalize (GObject *object)
{
        GdmDisplay *display;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DISPLAY (object));

        display = GDM_DISPLAY (object);

        g_return_if_fail (display->priv != NULL);

        g_debug ("GdmDisplay: Finalizing display: %s", display->priv->id);
        g_free (display->priv->id);
        g_free (display->priv->seat_id);
        g_free (display->priv->remote_hostname);
        g_free (display->priv->x11_display_name);
        g_free (display->priv->x11_cookie);
        g_free (display->priv->slave_command);

        if (display->priv->access_file != NULL) {
                g_object_unref (display->priv->access_file);
        }

        if (display->priv->user_access_file != NULL) {
                g_object_unref (display->priv->user_access_file);
        }

        G_OBJECT_CLASS (gdm_display_parent_class)->finalize (object);
}
