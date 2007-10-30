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
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */
#include <X11/cursorfont.h> /* for watch cursor */

#include "gdm-common.h"

#include "gdm-slave.h"
#include "gdm-slave-glue.h"

#include "gdm-server.h"

#define GDM_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SLAVE, GdmSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmSlavePrivate
{
        char            *id;
        GPid             pid;
        guint            output_watch_id;
        guint            error_watch_id;

        Display         *server_display;

        /* cached display values */
        char            *display_id;
        char            *display_name;
        int              display_number;
        char            *display_hostname;
        gboolean         display_is_local;
        gboolean         display_is_parented;
        char            *display_x11_authority_file;
        char            *display_x11_cookie;
        char            *parent_display_name;
        char            *parent_display_x11_authority_file;

        /* user selected */
        char            *selected_session;
        char            *selected_language;

        DBusGProxy      *display_proxy;
        DBusGConnection *connection;
};

enum {
        PROP_0,
        PROP_DISPLAY_ID,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_NUMBER,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_X11_AUTHORITY_FILE,
        PROP_DISPLAY_X11_COOKIE,
};

enum {
        STOPPED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_slave_class_init    (GdmSlaveClass *klass);
static void     gdm_slave_init          (GdmSlave      *slave);
static void     gdm_slave_finalize      (GObject       *object);

G_DEFINE_ABSTRACT_TYPE (GdmSlave, gdm_slave, G_TYPE_OBJECT)

#define CURSOR_WATCH XC_watch

static void
gdm_slave_whack_temp_auth_file (GdmSlave *slave)
{
#if 0
        uid_t old;

        old = geteuid ();
        if (old != 0)
                seteuid (0);
        if (d->parent_temp_auth_file != NULL) {
                VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
        }
        g_free (d->parent_temp_auth_file);
        d->parent_temp_auth_file = NULL;
        if (old != 0)
                seteuid (old);
#endif
}


static void
create_temp_auth_file (GdmSlave *slave)
{
#if 0
        if (d->type == TYPE_FLEXI_XNEST &&
            d->parent_auth_file != NULL) {
                if (d->parent_temp_auth_file != NULL) {
                        VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
                }
                g_free (d->parent_temp_auth_file);
                d->parent_temp_auth_file =
                        copy_auth_file (d->server_uid,
                                        gdm_daemon_config_get_gdmuid (),
                                        d->parent_auth_file);
        }
#endif
}

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("GdmSlave: script environment: %s", str);
        g_ptr_array_add (env, str);
}

static GPtrArray *
get_script_environment (GdmSlave   *slave,
                        const char *username)
{
        GPtrArray     *env;
        GHashTable    *hash;
        struct passwd *pwent;
        char          *x_servers_file;
        char          *temp;

        env = g_ptr_array_new ();

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        /* modify environment here */
        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (username));
        g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (username));
        g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (username));

        pwent = getpwnam (username);
        if (pwent != NULL) {
                if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
                        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
                }

                g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
        }

#if 0
        if (display_is_parented) {
                g_hash_table_insert (hash, g_strdup ("GDM_PARENT_DISPLAY"), g_strdup (parent_display_name));

                /*g_hash_table_insert (hash, "GDM_PARENT_XAUTHORITY"), slave->priv->parent_temp_auth_file));*/
        }
#endif

        /* some env for use with the Pre and Post scripts */
        temp = g_strconcat (slave->priv->display_name, ".Xservers", NULL);
        x_servers_file = g_build_filename (AUTHDIR, temp, NULL);
        g_free (temp);

        g_hash_table_insert (hash, g_strdup ("X_SERVERS"), x_servers_file);

        if (! slave->priv->display_is_local) {
                g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (slave->priv->display_hostname));
        }

        /* Runs as root */
        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (slave->priv->display_x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (slave->priv->display_name));

        /*g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);*/

        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

        g_hash_table_remove (hash, "MAIL");


        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

gboolean
gdm_slave_run_script (GdmSlave   *slave,
                      const char *dir,
                      const char *login)
{
        char      *script;
        char     **argv;
        gint       status;
        GError    *error;
        GPtrArray *env;
        gboolean   res;
        gboolean   ret;

        ret = FALSE;

        g_assert (dir != NULL);
        g_assert (login != NULL);

        script = g_build_filename (dir, slave->priv->display_name, NULL);
        if (g_access (script, R_OK | X_OK) != 0) {
                g_free (script);
                script = NULL;
        }

        if (script == NULL &&
            slave->priv->display_hostname != NULL) {
                script = g_build_filename (dir, slave->priv->display_hostname, NULL);
                if (g_access (script, R_OK | X_OK) != 0) {
                        g_free (script);
                        script = NULL;
                }
        }

#if 0
        if (script == NULL &&
            SERVER_IS_XDMCP (d)) {
                script = g_build_filename (dir, "XDMCP", NULL);
                if (g_access (script, R_OK | X_OK) != 0) {
                        g_free (script);
                        script = NULL;
                }
        }
        if (script == NULL &&
            SERVER_IS_FLEXI (d)) {
                script = g_build_filename (dir, "Flexi", NULL);
                if (g_access (script, R_OK | X_OK) != 0) {
                        g_free (script);
                        script = NULL;
                }
        }
#endif

        if (script == NULL) {
                script = g_build_filename (dir, "Default", NULL);
                if (g_access (script, R_OK | X_OK) != 0) {
                        g_free (script);
                        script = NULL;
                }
        }

        if (script == NULL) {
                return TRUE;
        }

        create_temp_auth_file (slave);

        g_debug ("GdmSlave: Running process: %s", script);
        error = NULL;
        if (! g_shell_parse_argv (script, NULL, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                goto out;
        }

        env = get_script_environment (slave, login);

        res = g_spawn_sync (NULL,
                            argv,
                            (char **)env->pdata,
                            G_SPAWN_SEARCH_PATH,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            &status,
                            &error);

        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

        gdm_slave_whack_temp_auth_file (slave);

        if (WIFEXITED (status)) {
                g_debug ("GdmSlave: Process exit status: %d", WEXITSTATUS (status));
                ret = WEXITSTATUS (status) != 0;
        } else {
                ret = TRUE;
        }

 out:
        g_free (script);

        return ret;
}

void
gdm_slave_set_busy_cursor (GdmSlave *slave)
{
        if (slave->priv->server_display != NULL) {
                Cursor xcursor;

                xcursor = XCreateFontCursor (slave->priv->server_display, CURSOR_WATCH);
                XDefineCursor (slave->priv->server_display,
                               DefaultRootWindow (slave->priv->server_display),
                               xcursor);
                XFreeCursor (slave->priv->server_display, xcursor);
                XSync (slave->priv->server_display, False);
        }
}

static void
set_local_auth (GdmSlave *slave)
{
        GString *binary_cookie;
        GString *cookie;

        g_debug ("GdmSlave: Setting authorization key for display %s", slave->priv->display_x11_cookie);

        cookie = g_string_new (slave->priv->display_x11_cookie);
        binary_cookie = g_string_new (NULL);
        if (! gdm_string_hex_decode (cookie,
                                     0,
                                     NULL,
                                     binary_cookie,
                                     0)) {
                g_warning ("Unable to decode hex cookie");
                goto out;
        }

        g_debug ("GdmSlave: Decoded cookie len %d", (int) binary_cookie->len);

        XSetAuthorization ("MIT-MAGIC-COOKIE-1",
                           (int) strlen ("MIT-MAGIC-COOKIE-1"),
                           (char *)binary_cookie->str,
                           binary_cookie->len);

 out:
        g_string_free (binary_cookie, TRUE);
        g_string_free (cookie, TRUE);
}

gboolean
gdm_slave_connect_to_x11_display (GdmSlave *slave)
{
        gboolean ret;
        sigset_t mask;
        sigset_t omask;

        ret = FALSE;

        /* We keep our own (windowless) connection (dsp) open to avoid the
         * X server resetting due to lack of active connections. */

        g_debug ("GdmSlave: Server is ready - opening display %s", slave->priv->display_name);

        g_setenv ("DISPLAY", slave->priv->display_name, TRUE);
        g_unsetenv ("XAUTHORITY"); /* just in case it's set */

        set_local_auth (slave);

#if 0
        /* X error handlers to avoid the default one (i.e. exit (1)) */
        do_xfailed_on_xio_error = TRUE;
        XSetErrorHandler (gdm_slave_xerror_handler);
        XSetIOErrorHandler (gdm_slave_xioerror_handler);
#endif

        sigemptyset (&mask);
        sigaddset (&mask, SIGCHLD);
        sigprocmask (SIG_BLOCK, &mask, &omask);

        slave->priv->server_display = XOpenDisplay (slave->priv->display_name);

        sigprocmask (SIG_SETMASK, &omask, NULL);


        if (slave->priv->server_display == NULL) {
                g_warning ("Unable to connect to display %s", slave->priv->display_name);
                ret = FALSE;
        } else {
                g_debug ("GdmSlave: Connected to display %s", slave->priv->display_name);
                ret = TRUE;
        }

        return ret;
}

static void
display_proxy_destroyed_cb (DBusGProxy *display_proxy,
                            GdmSlave   *slave)
{
        g_debug ("GdmSlave: Disconnected from display");

        slave->priv->display_proxy = NULL;
}

static gboolean
gdm_slave_real_start (GdmSlave *slave)
{
        gboolean res;
        char    *id;
        GError  *error;

        g_debug ("GdmSlave: Starting slave");

        g_assert (slave->priv->display_proxy == NULL);

        g_debug ("GdmSlave: Creating proxy for %s", slave->priv->display_id);
        error = NULL;
        slave->priv->display_proxy = dbus_g_proxy_new_for_name_owner (slave->priv->connection,
                                                                      GDM_DBUS_NAME,
                                                                      slave->priv->display_id,
                                                                      GDM_DBUS_DISPLAY_INTERFACE,
                                                                      &error);
        g_signal_connect (slave->priv->display_proxy,
                          "destroy",
                          G_CALLBACK (display_proxy_destroyed_cb),
                          slave);

        if (slave->priv->display_proxy == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to create display proxy %s: %s", slave->priv->display_id, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to create display proxy");
                }
                return FALSE;
        }

        /* Make sure display ID works */
        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetId",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &id,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get display id %s: %s", slave->priv->display_id, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get display id %s", slave->priv->display_id);
                }

                return FALSE;
        }

        g_debug ("GdmSlave: Got display id: %s", id);

        if (strcmp (id, slave->priv->display_id) != 0) {
                g_critical ("Display ID doesn't match");
                exit (1);
        }

        /* cache some values up front */
        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "IsLocal",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_BOOLEAN, &slave->priv->display_is_local,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetX11DisplayName",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &slave->priv->display_name,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetX11DisplayNumber",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INT, &slave->priv->display_number,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetRemoteHostname",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &slave->priv->display_hostname,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetX11Cookie",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &slave->priv->display_x11_cookie,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "GetX11AuthorityFile",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &slave->priv->display_x11_authority_file,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to get value: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to get value");
                }

                return FALSE;
        }

        return TRUE;
}

static gboolean
gdm_slave_real_stop (GdmSlave *slave)
{
        g_debug ("GdmSlave: Stopping slave");

        if (slave->priv->display_proxy != NULL) {
                g_object_unref (slave->priv->display_proxy);
        }

        return TRUE;
}

gboolean
gdm_slave_start (GdmSlave *slave)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        g_debug ("GdmSlave: starting slave");

        g_object_ref (slave);
        ret = GDM_SLAVE_GET_CLASS (slave)->start (slave);
        g_object_unref (slave);

        return ret;
}

gboolean
gdm_slave_stop (GdmSlave *slave)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        g_debug ("GdmSlave: stopping slave");

        g_object_ref (slave);
        ret = GDM_SLAVE_GET_CLASS (slave)->stop (slave);
        g_object_unref (slave);

        return ret;
}

void
gdm_slave_stopped (GdmSlave *slave)
{
        g_return_if_fail (GDM_IS_SLAVE (slave));

        g_signal_emit (slave, signals [STOPPED], 0);
}

gboolean
gdm_slave_add_user_authorization (GdmSlave   *slave,
                                  const char *username,
                                  char      **filenamep)
{
        gboolean res;
        GError  *error;
        char    *filename;

        filename = NULL;

        if (filenamep != NULL) {
                *filenamep = NULL;
        }

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->display_proxy,
                                 "AddUserAuthorization",
                                 &error,
                                 G_TYPE_STRING, username,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &filename,
                                 G_TYPE_INVALID);
        if (filenamep != NULL) {
                *filenamep = g_strdup (filename);
        }
        g_free (filename);

        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to add user authorization: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to add user authorization");
                }
        }

        return res;
}

static void
_gdm_slave_set_display_id (GdmSlave   *slave,
                           const char *id)
{
        g_free (slave->priv->display_id);
        slave->priv->display_id = g_strdup (id);
}

static void
_gdm_slave_set_display_name (GdmSlave   *slave,
                             const char *name)
{
        g_free (slave->priv->display_name);
        slave->priv->display_name = g_strdup (name);
}

static void
_gdm_slave_set_display_number (GdmSlave   *slave,
                               int         number)
{
        slave->priv->display_number = number;
}

static void
_gdm_slave_set_display_hostname (GdmSlave   *slave,
                                 const char *name)
{
        g_free (slave->priv->display_hostname);
        slave->priv->display_hostname = g_strdup (name);
}

static void
_gdm_slave_set_display_x11_authority_file (GdmSlave   *slave,
                                           const char *name)
{
        g_free (slave->priv->display_x11_authority_file);
        slave->priv->display_x11_authority_file = g_strdup (name);
}

static void
_gdm_slave_set_display_x11_cookie (GdmSlave   *slave,
                                   const char *name)
{
        g_free (slave->priv->display_x11_cookie);
        slave->priv->display_x11_cookie = g_strdup (name);
}

static void
_gdm_slave_set_display_is_local (GdmSlave   *slave,
                                 gboolean    is)
{
        slave->priv->display_is_local = is;
}

static void
gdm_slave_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        GdmSlave *self;

        self = GDM_SLAVE (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                _gdm_slave_set_display_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_NAME:
                _gdm_slave_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_NUMBER:
                _gdm_slave_set_display_number (self, g_value_get_int (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                _gdm_slave_set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                _gdm_slave_set_display_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_X11_COOKIE:
                _gdm_slave_set_display_x11_cookie (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_slave_set_display_is_local (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_slave_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
        GdmSlave *self;

        self = GDM_SLAVE (object);

        switch (prop_id) {
        case PROP_DISPLAY_ID:
                g_value_set_string (value, self->priv->display_id);
                break;
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->priv->display_name);
                break;
        case PROP_DISPLAY_NUMBER:
                g_value_set_int (value, self->priv->display_number);
                break;
        case PROP_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->priv->display_hostname);
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->display_x11_authority_file);
                break;
        case PROP_DISPLAY_X11_COOKIE:
                g_value_set_string (value, self->priv->display_x11_cookie);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
register_slave (GdmSlave *slave)
{
        GError *error = NULL;

        error = NULL;
        slave->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (slave->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (slave->priv->connection, slave->priv->id, G_OBJECT (slave));

        return TRUE;
}

static GObject *
gdm_slave_constructor (GType                  type,
                       guint                  n_construct_properties,
                       GObjectConstructParam *construct_properties)
{
        GdmSlave      *slave;
        GdmSlaveClass *klass;
        gboolean       res;
        const char    *id;

        klass = GDM_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_SLAVE));

        slave = GDM_SLAVE (G_OBJECT_CLASS (gdm_slave_parent_class)->constructor (type,
                                                                                 n_construct_properties,
                                                                                 construct_properties));

        id = NULL;
        if (g_str_has_prefix (slave->priv->display_id, "/org/gnome/DisplayManager/Display")) {
                id = slave->priv->display_id + strlen ("/org/gnome/DisplayManager/Display");
        }

        slave->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Slave%s", id);
        g_debug ("GdmSlave: Registering %s", slave->priv->id);

        res = register_slave (slave);
        if (! res) {
                g_warning ("Unable to register slave with system bus");
        }

        return G_OBJECT (slave);
}

static void
gdm_slave_class_init (GdmSlaveClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_slave_get_property;
        object_class->set_property = gdm_slave_set_property;
        object_class->constructor = gdm_slave_constructor;
        object_class->finalize = gdm_slave_finalize;

        klass->start = gdm_slave_real_start;
        klass->stop = gdm_slave_real_stop;

        g_type_class_add_private (klass, sizeof (GdmSlavePrivate));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_ID,
                                         g_param_spec_string ("display-id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NUMBER,
                                         g_param_spec_int ("display-number",
                                                           "display number",
                                                           "display number",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "display hostname",
                                                              "display hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("display-x11-authority-file",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_COOKIE,
                                         g_param_spec_string ("display-x11-cookie",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmSlaveClass, stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        dbus_g_object_type_install_info (GDM_TYPE_SLAVE, &dbus_glib_gdm_slave_object_info);
}

static void
gdm_slave_init (GdmSlave *slave)
{

        slave->priv = GDM_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;
}

static void
gdm_slave_finalize (GObject *object)
{
        GdmSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SLAVE (object));

        slave = GDM_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        gdm_slave_real_stop (slave);

        g_free (slave->priv->display_id);

        G_OBJECT_CLASS (gdm_slave_parent_class)->finalize (object);
}
