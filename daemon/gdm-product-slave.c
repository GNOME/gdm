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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-product-slave.h"
#include "gdm-product-slave-glue.h"

#include "gdm-server.h"
#include "gdm-session.h"

#include "ck-connector.h"

extern char **environ;

#define GDM_PRODUCT_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_PRODUCT_SLAVE, GdmProductSlavePrivate))

#define GDM_DBUS_NAME                      "org.gnome.DisplayManager"
#define GDM_DBUS_PRODUCT_DISPLAY_INTERFACE "org.gnome.DisplayManager.ProductDisplay"

#define SERVER_DBUS_PATH      "/org/gnome/DisplayManager/SessionRelay"
#define SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.SessionRelay"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmProductSlavePrivate
{
        char             *id;
        GPid              pid;
        guint             output_watch_id;
        guint             error_watch_id;

        char             *relay_address;

        GPid              server_pid;
        Display          *server_display;
        guint             connection_attempts;

        /* user selected */
        char             *selected_session;
        char             *selected_language;
        char             *selected_user;

        CkConnector      *ckc;
        GdmServer        *server;
        GdmSession       *session;
        DBusGProxy       *session_relay_proxy;
        DBusGConnection  *session_relay_connection;
        DBusGProxy       *product_display_proxy;
        DBusGConnection  *connection;
};

enum {
        PROP_0,
        PROP_DISPLAY_ID,
};

static void     gdm_product_slave_class_init    (GdmProductSlaveClass *klass);
static void     gdm_product_slave_init          (GdmProductSlave      *product_slave);
static void     gdm_product_slave_finalize      (GObject             *object);

G_DEFINE_TYPE (GdmProductSlave, gdm_product_slave, GDM_TYPE_SLAVE)

static void
relay_session_started (GdmProductSlave *slave)
{
        GError *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "SessionStarted",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SessionStarted: %s", error->message);
                g_error_free (error);
        }
}

static void
relay_session_opened (GdmProductSlave *slave)
{
        GError *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "Opened",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send Opened: %s", error->message);
                g_error_free (error);
        }
}

static void
on_session_opened (GdmSession      *session,
                   GdmProductSlave *slave)
{
        g_debug ("session opened");

        relay_session_opened (slave);
}

static void
disconnect_relay (GdmProductSlave *slave)
{
        /* drop the connection */
        g_object_unref (slave->priv->session_relay_proxy);

        dbus_connection_close (dbus_g_connection_get_connection (slave->priv->session_relay_connection));
        slave->priv->session_relay_connection = NULL;
}

static void
on_session_started (GdmSession      *session,
                    GPid             pid,
                    GdmProductSlave *slave)
{
        g_debug ("session started on pid %d", (int) pid);

        relay_session_started (slave);

        disconnect_relay (slave);
}

static void
on_session_exited (GdmSession      *session,
                   int              exit_code,
                   GdmProductSlave *slave)
{
        g_debug ("session exited with code %d", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_session_died (GdmSession *session,
                 int         signal_number,
                 GdmProductSlave   *slave)
{
        g_debug ("session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static gboolean
is_prog_in_path (const char *prog)
{
        char    *f;
        gboolean ret;

        f = g_find_program_in_path (prog);
        ret = (f != NULL);
        g_free (f);
        return ret;
}

static gboolean
get_session_command (const char *file,
                     char      **command)
{
        GKeyFile   *key_file;
        GError     *error;
        char       *full_path;
        char       *exec;
        gboolean    ret;
        gboolean    res;
        const char *search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
                NULL
        };

        exec = NULL;
        ret = FALSE;
        if (command != NULL) {
                *command = NULL;
        }

        key_file = g_key_file_new ();

        error = NULL;
        full_path = NULL;
        res = g_key_file_load_from_dirs (key_file,
                                         file,
                                         search_dirs,
                                         &full_path,
                                         G_KEY_FILE_NONE,
                                         &error);
        if (! res) {
                g_debug ("File '%s' not found: %s", file, error->message);
                g_error_free (error);
                if (command != NULL) {
                        *command = NULL;
                }
                goto out;
        }

        error = NULL;
        res = g_key_file_get_boolean (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_HIDDEN,
                                      &error);
        if (error == NULL && res) {
                g_debug ("Session %s is marked as hidden", file);
                goto out;
        }

        error = NULL;
        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC,
                                      &error);
        if (exec == NULL) {
                g_debug ("%s key not found", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
                goto out;
        }

        res = is_prog_in_path (exec);
        g_free (exec);

        if (! res) {
                g_debug ("Command not found: %s", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
                goto out;
        }

        error = NULL;
        exec = g_key_file_get_string (key_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_EXEC,
                                      &error);
        if (error != NULL) {
                g_debug ("%s key not found: %s",
                         G_KEY_FILE_DESKTOP_KEY_EXEC,
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (command != NULL) {
                *command = g_strdup (exec);
        }
        ret = TRUE;

out:
        g_free (exec);

        return ret;
}

static gboolean
slave_open_ck_session (GdmProductSlave *slave,
                       const char      *display_name,
                       const char      *display_hostname,
                       gboolean         display_is_local)
{
        char          *username;
        char          *x11_display_device;
        struct passwd *pwent;
        gboolean       ret;
        int            res;
        DBusError      error;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        username = gdm_session_get_username (slave->priv->session);

        x11_display_device = NULL;

        pwent = getpwnam (username);
        if (pwent == NULL) {
                return FALSE;
        }

        slave->priv->ckc = ck_connector_new ();
        if (slave->priv->ckc == NULL) {
                g_warning ("Couldn't create new ConsoleKit connector");
                goto out;
        }

        if (slave->priv->server != NULL) {
                x11_display_device = gdm_server_get_display_device (slave->priv->server);
        }

        if (x11_display_device == NULL) {
                x11_display_device = g_strdup ("");
        }

        dbus_error_init (&error);
        res = ck_connector_open_session_with_parameters (slave->priv->ckc,
                                                         &error,
                                                         "unix-user", &pwent->pw_uid,
                                                         "x11-display", &display_name,
                                                         "x11-display-device", &x11_display_device,
                                                         "remote-host-name", &display_hostname,
                                                         "is-local", &display_is_local,
                                                         NULL);
        g_free (x11_display_device);

        if (! res) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("%s\n", error.message);
                        dbus_error_free (&error);
                } else {
                        g_warning ("cannot open CK session: OOM, D-Bus system bus not available,\n"
                                   "ConsoleKit not available or insufficient privileges.\n");
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
setup_session_environment (GdmProductSlave *slave)
{
        int         display_number;
        char       *display_x11_cookie;
        char       *display_name;
        char       *display_hostname;
        char       *auth_file;
        const char *session_cookie;
        gboolean    display_is_local;

        display_name = NULL;
        display_hostname = NULL;
        display_x11_cookie = NULL;
        auth_file = NULL;
        session_cookie = NULL;
        display_is_local = FALSE;

        g_object_get (slave,
                      "display-number", &display_number,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-is-local", &display_is_local,
                      "display-x11-cookie", &display_x11_cookie,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        if (slave_open_ck_session (slave,
                                   display_name,
                                   display_hostname,
                                   display_is_local)) {
                session_cookie = ck_connector_get_cookie (slave->priv->ckc);
        }

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        gdm_session_set_environment_variable (slave->priv->session,
                                              "GDMSESSION",
                                              slave->priv->selected_session);
        gdm_session_set_environment_variable (slave->priv->session,
                                              "DESKTOP_SESSION",
                                              slave->priv->selected_session);

        gdm_session_set_environment_variable (slave->priv->session,
                                              "LANG",
                                              slave->priv->selected_language);
        gdm_session_set_environment_variable (slave->priv->session,
                                              "GDM_LANG",
                                              slave->priv->selected_language);

        gdm_session_set_environment_variable (slave->priv->session,
                                              "DISPLAY",
                                              display_name);
        gdm_session_set_environment_variable (slave->priv->session,
                                              "XAUTHORITY",
                                              auth_file);

        gdm_session_set_environment_variable (slave->priv->session,
                                              "PATH",
                                              "/bin:/usr/bin:" BINDIR);

        g_free (display_name);
        g_free (auth_file);
}

static void
setup_server (GdmProductSlave *slave)
{
        gboolean       display_is_local;
        char          *display_name;
        char          *auth_file;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));

        /* FIXME: send a signal back to the master */

#if 0

        /* OK from now on it's really the user whacking us most likely,
         * we have already started up well */
        do_xfailed_on_xio_error = FALSE;
#endif

#if 0
        /* checkout xinerama */
        gdm_screen_init (slave);
#endif

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR"/Init", "gdm");

        g_free (display_name);
        g_free (auth_file);
}

static gboolean
setup_session (GdmProductSlave *slave)
{
        char    *username;
        char    *command;
        char    *filename;
        gboolean res;

        username = gdm_session_get_username (slave->priv->session);

        g_debug ("%s%ssuccessfully authenticated\n",
                 username ? username : "",
                 username ? " " : "");
        g_free (username);

        if (slave->priv->selected_session != NULL) {
                filename = g_strdup (slave->priv->selected_session);
        } else {
                filename = g_strdup ("gnome.desktop");
        }

        setup_session_environment (slave);

        res = get_session_command (filename, &command);
        if (! res) {
                g_warning ("Could find session file: %s", filename);
                return FALSE;
        }

        gdm_session_start_program (slave->priv->session, command);

        g_free (filename);
        g_free (command);

        return TRUE;
}

static gboolean
idle_connect_to_display (GdmProductSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                /* FIXME: handle wait-for-go */

                setup_server (slave);
                setup_session (slave);
        } else {
                if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
                        exit (1);
                }
        }

        return FALSE;
}

static void
server_ready_cb (GdmServer *server,
                 GdmProductSlave  *slave)
{
        g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static gboolean
gdm_product_slave_create_server (GdmProductSlave *slave)
{
        char    *display_name;
        gboolean display_is_local;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      NULL);

        /* if this is local display start a server if one doesn't
         * exist */
        if (display_is_local) {
                gboolean res;

                slave->priv->server = gdm_server_new (display_name);

                g_signal_connect (slave->priv->server,
                                  "ready",
                                  G_CALLBACK (server_ready_cb),
                                  slave);

                res = gdm_server_start (slave->priv->server);
                if (! res) {
                        g_warning (_("Could not start the X "
                                     "server (your graphical environment) "
                                     "due to some internal error. "
                                     "Please contact your system administrator "
                                     "or check your syslog to diagnose. "
                                     "In the meantime this display will be "
                                     "disabled.  Please restart GDM when "
                                     "the problem is corrected."));
                        exit (1);
                }

                g_debug ("Started X server");
        } else {
                g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
        }

        g_free (display_name);

        return TRUE;
}

static void
on_session_user_verified (GdmSession      *session,
                          GdmProductSlave *slave)
{
        GError  *error;
        gboolean res;

        g_debug ("Session user verified");

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "UserVerified",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send UserVerified: %s", error->message);
                g_error_free (error);
        }

        gdm_product_slave_create_server (slave);
}

static void
on_session_user_verification_error (GdmSession      *session,
                                    GError          *error,
                                    GdmProductSlave *slave)
{
        char    *username;
        GError  *local_error;
        gboolean res;

        username = gdm_session_get_username (session);

        g_debug ("%s%scould not be successfully authenticated: %s\n",
                 username ? username : "",
                 username ? " " : "",
                 error->message);

        g_free (username);

        local_error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "UserVerificationError",
                                 &local_error,
                                 G_TYPE_STRING, error->message,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send UserVerificationError: %s", local_error->message);
                g_error_free (local_error);
        }
}

static void
on_session_info (GdmSession      *session,
                 const char      *text,
                 GdmProductSlave *slave)
{
        GError *error;
        gboolean res;

        g_debug ("Info: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "Info",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send Info: %s", error->message);
                g_error_free (error);
        }
}

static void
on_session_problem (GdmSession      *session,
                    const char      *text,
                    GdmProductSlave *slave)
{
        GError *error;
        gboolean res;

        g_debug ("Problem: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "Problem",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send Problem: %s", error->message);
                g_error_free (error);
        }

}

static void
on_session_info_query (GdmSession      *session,
                       const char      *text,
                       GdmProductSlave *slave)
{
        GError  *error;
        gboolean res;

        g_debug ("Info query: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "InfoQuery",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send InfoQuery: %s", error->message);
                g_error_free (error);
        }
}

static void
on_session_secret_info_query (GdmSession      *session,
                              const char      *text,
                              GdmProductSlave *slave)
{
        GError *error;
        gboolean res;


        g_debug ("Secret info query: %s", text);

        error = NULL;
        res = dbus_g_proxy_call (slave->priv->session_relay_proxy,
                                 "SecretInfoQuery",
                                 &error,
                                 G_TYPE_STRING, text,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to send SecretInfoQuery: %s", error->message);
                g_error_free (error);
        }
}

static void
on_relay_begin_verification (DBusGProxy *proxy,
                             gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);
        GError          *error;
        gboolean         res;

        g_debug ("Relay BeginVerification");

        error = NULL;
        res = gdm_session_begin_verification (slave->priv->session,
                                              NULL,
                                              &error);
        if (! res) {
                g_warning ("Unable to begin verification: %s", error->message);
                g_error_free (error);
        }
}

static void
on_relay_begin_verification_for_user (DBusGProxy *proxy,
                                      const char *username,
                                      gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);
        GError          *error;
        gboolean         res;

        g_debug ("Relay BeginVerificationForUser");

        error = NULL;
        res = gdm_session_begin_verification (slave->priv->session,
                                              username,
                                              &error);
        if (! res) {
                g_warning ("Unable to begin verification for user: %s", error->message);
                g_error_free (error);
        }
}

static void
on_relay_answer (DBusGProxy *proxy,
                 const char *text,
                 gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);

        g_debug ("Relay Answer");

        gdm_session_answer_query (slave->priv->session, text);
}

static void
on_relay_session_selected (DBusGProxy *proxy,
                           const char *text,
                           gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);

        g_debug ("Session: %s", text);

        g_free (slave->priv->selected_session);
        slave->priv->selected_session = g_strdup (text);
}

static void
on_relay_language_selected (DBusGProxy *proxy,
                            const char *text,
                            gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);

        g_debug ("Language: %s", text);

        g_free (slave->priv->selected_language);
        slave->priv->selected_language = g_strdup (text);
}

static gboolean
reset_session (GdmProductSlave *slave)
{
        char            *display_name;
        gboolean         res;
        GError          *error;

        g_object_get (slave,
                      "display-name", &display_name,
                      NULL);

        gdm_session_close (slave->priv->session);
        res = gdm_session_open (slave->priv->session,
                                "gdm",
                                "",
                                display_name,
                                "/dev/console",
                                &error);
        if (! res) {
                g_warning ("Unable to open session: %s", error->message);
                g_error_free (error);
        }

        g_free (display_name);

        return res;
}

static void
on_relay_user_selected (DBusGProxy *proxy,
                        const char *text,
                        gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);

        g_debug ("User: %s", text);

        g_free (slave->priv->selected_user);
        slave->priv->selected_user = g_strdup (text);

        reset_session (slave);
}

static void
on_relay_open (DBusGProxy *proxy,
               gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);
        char            *display_name;
        gboolean         res;
        GError          *error;

        g_debug ("Relay open: opening session");

        g_object_get (slave,
                      "display-name", &display_name,
                      NULL);

        error = NULL;
        res = gdm_session_open (slave->priv->session,
                                "gdm",
                                "",
                                display_name,
                                "/dev/console",
                                &error);
        if (! res) {
                g_warning ("Unable to open session: %s", error->message);
                g_error_free (error);
        }

        g_free (display_name);
}

static void
create_new_session (GdmProductSlave *slave)
{
        slave->priv->session = gdm_session_new ();

        g_signal_connect (slave->priv->session,
                          "opened",
                          G_CALLBACK (on_session_opened),
                          slave);

        g_signal_connect (slave->priv->session,
                          "info",
                          G_CALLBACK (on_session_info),
                          slave);

        g_signal_connect (slave->priv->session,
                          "problem",
                          G_CALLBACK (on_session_problem),
                          slave);

        g_signal_connect (slave->priv->session,
                          "info-query",
                          G_CALLBACK (on_session_info_query),
                          slave);

        g_signal_connect (slave->priv->session,
                          "secret-info-query",
                          G_CALLBACK (on_session_secret_info_query),
                          slave);

        g_signal_connect (slave->priv->session,
                          "user-verified",
                          G_CALLBACK (on_session_user_verified),
                          slave);

        g_signal_connect (slave->priv->session,
                          "user-verification-error",
                          G_CALLBACK (on_session_user_verification_error),
                          slave);

        g_signal_connect (slave->priv->session,
                          "session-started",
                          G_CALLBACK (on_session_started),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-exited",
                          G_CALLBACK (on_session_exited),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-died",
                          G_CALLBACK (on_session_died),
                          slave);
}

static void
on_relay_cancelled (DBusGProxy *proxy,
                    gpointer    data)
{
        GdmProductSlave *slave = GDM_PRODUCT_SLAVE (data);

        g_debug ("Relay cancelled");

        if (slave->priv->session != NULL) {
                gdm_session_close (slave->priv->session);
                g_object_unref (slave->priv->session);
        }

        create_new_session (slave);
}

static void
session_relay_proxy_destroyed (GObject         *object,
                               GdmProductSlave *slave)
{
        g_debug ("Session server relay destroyed");

        slave->priv->session_relay_proxy = NULL;
}

static void
get_relay_address (GdmProductSlave *slave)
{
        GError  *error;
        char    *text;
        gboolean res;

        text = NULL;
        error = NULL;
        res = dbus_g_proxy_call (slave->priv->product_display_proxy,
                                 "GetRelayAddress",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &text,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to get relay address: %s", error->message);
                g_error_free (error);
        } else {
                g_free (slave->priv->relay_address);
                slave->priv->relay_address = g_strdup (text);
                g_debug ("Got relay address: %s", slave->priv->relay_address);
        }

        g_free (text);
}

static gboolean
connect_to_session_relay (GdmProductSlave *slave)
{
        DBusError       error;
        DBusConnection *connection;

        get_relay_address (slave);

        g_debug ("connecting to session relay address: %s", slave->priv->relay_address);

        dbus_error_init (&error);
        connection = dbus_connection_open_private (slave->priv->relay_address, &error);
        dbus_connection_setup_with_g_main (connection, NULL);

        slave->priv->session_relay_connection = dbus_connection_get_g_connection (connection);
        if (slave->priv->session_relay_connection == NULL) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("error opening connection: %s", error.message);
                        dbus_error_free (&error);
                } else {
                        g_warning ("Unable to open connection");
                }
                exit (1);
        }

        g_debug ("creating session server proxy for peer: %s", SERVER_DBUS_PATH);
        slave->priv->session_relay_proxy = dbus_g_proxy_new_for_peer (slave->priv->session_relay_connection,
                                                                      SERVER_DBUS_PATH,
                                                                      SERVER_DBUS_INTERFACE);
        if (slave->priv->session_relay_proxy == NULL) {
                g_warning ("Unable to create proxy for peer");
                exit (1);
        }

        /* FIXME: not sure why introspection isn't working */
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "BeginVerification",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "AnswerQuery",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "SessionSelected",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "LanguageSelected",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "UserSelected",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "Open",
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (slave->priv->session_relay_proxy,
                                 "Cancelled",
                                 G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "BeginVerification",
                                     G_CALLBACK (on_relay_begin_verification),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "BeginVerificationForUser",
                                     G_CALLBACK (on_relay_begin_verification_for_user),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "AnswerQuery",
                                     G_CALLBACK (on_relay_answer),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "SessionSelected",
                                     G_CALLBACK (on_relay_session_selected),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "LanguageSelected",
                                     G_CALLBACK (on_relay_language_selected),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "UserSelected",
                                     G_CALLBACK (on_relay_user_selected),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "Open",
                                     G_CALLBACK (on_relay_open),
                                     slave,
                                     NULL);
        dbus_g_proxy_connect_signal (slave->priv->session_relay_proxy,
                                     "Cancelled",
                                     G_CALLBACK (on_relay_cancelled),
                                     slave,
                                     NULL);

        g_signal_connect (slave->priv->session_relay_proxy,
                          "destroy",
                          G_CALLBACK (session_relay_proxy_destroyed),
                          slave);

        return TRUE;
}

static gboolean
gdm_product_slave_start (GdmSlave *slave)
{
        gboolean ret;
        gboolean res;
        GError  *error;
        char    *display_id;

        ret = FALSE;

        res = GDM_SLAVE_CLASS (gdm_product_slave_parent_class)->start (slave);

        g_object_get (slave,
                      "display-id", &display_id,
                      NULL);

        error = NULL;
        GDM_PRODUCT_SLAVE (slave)->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (GDM_PRODUCT_SLAVE (slave)->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        error = NULL;
        GDM_PRODUCT_SLAVE (slave)->priv->product_display_proxy = dbus_g_proxy_new_for_name_owner (GDM_PRODUCT_SLAVE (slave)->priv->connection,
                                                                                                  GDM_DBUS_NAME,
                                                                                                  display_id,
                                                                                                  GDM_DBUS_PRODUCT_DISPLAY_INTERFACE,
                                                                                                  &error);
        if (GDM_PRODUCT_SLAVE (slave)->priv->product_display_proxy == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to create display proxy %s: %s", display_id, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to create display proxy");
                }
                goto out;
        }

        create_new_session (GDM_PRODUCT_SLAVE (slave));

        connect_to_session_relay (GDM_PRODUCT_SLAVE (slave));

        ret = TRUE;

 out:
        g_free (display_id);

        return ret;
}

static gboolean
gdm_product_slave_stop (GdmSlave *slave)
{
        gboolean res;

        g_debug ("Stopping product_slave");

        res = GDM_SLAVE_CLASS (gdm_product_slave_parent_class)->stop (slave);

        if (GDM_PRODUCT_SLAVE (slave)->priv->session != NULL) {
                gdm_session_close (GDM_PRODUCT_SLAVE (slave)->priv->session);
                g_object_unref (GDM_PRODUCT_SLAVE (slave)->priv->session);
                GDM_PRODUCT_SLAVE (slave)->priv->session = NULL;
        }

        if (GDM_PRODUCT_SLAVE (slave)->priv->server != NULL) {
                gdm_server_stop (GDM_PRODUCT_SLAVE (slave)->priv->server);
                g_object_unref (GDM_PRODUCT_SLAVE (slave)->priv->server);
                GDM_PRODUCT_SLAVE (slave)->priv->server = NULL;
        }

        if (GDM_PRODUCT_SLAVE (slave)->priv->product_display_proxy != NULL) {
                g_object_unref (GDM_PRODUCT_SLAVE (slave)->priv->product_display_proxy);
        }

        return TRUE;
}

static void
gdm_product_slave_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GdmProductSlave *self;

        self = GDM_PRODUCT_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_product_slave_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GdmProductSlave *self;

        self = GDM_PRODUCT_SLAVE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_product_slave_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmProductSlave      *product_slave;
        GdmProductSlaveClass *klass;

        klass = GDM_PRODUCT_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_PRODUCT_SLAVE));

        product_slave = GDM_PRODUCT_SLAVE (G_OBJECT_CLASS (gdm_product_slave_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));
        return G_OBJECT (product_slave);
}

static void
gdm_product_slave_class_init (GdmProductSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->get_property = gdm_product_slave_get_property;
        object_class->set_property = gdm_product_slave_set_property;
        object_class->constructor = gdm_product_slave_constructor;
        object_class->finalize = gdm_product_slave_finalize;

        slave_class->start = gdm_product_slave_start;
        slave_class->stop = gdm_product_slave_stop;

        g_type_class_add_private (klass, sizeof (GdmProductSlavePrivate));

        dbus_g_object_type_install_info (GDM_TYPE_PRODUCT_SLAVE, &dbus_glib_gdm_product_slave_object_info);
}

static void
gdm_product_slave_init (GdmProductSlave *slave)
{
        const char * const *languages;

        slave->priv = GDM_PRODUCT_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;

        languages = g_get_language_names ();
        if (languages != NULL) {
                slave->priv->selected_language = g_strdup (languages[0]);
        }

        slave->priv->selected_session = g_strdup ("gnome.desktop");
}

static void
gdm_product_slave_finalize (GObject *object)
{
        GdmProductSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_PRODUCT_SLAVE (object));

        slave = GDM_PRODUCT_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        gdm_product_slave_stop (GDM_SLAVE (slave));

        G_OBJECT_CLASS (gdm_product_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_product_slave_new (const char *id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_PRODUCT_SLAVE,
                               "display-id", id,
                               NULL);

        return GDM_SLAVE (object);
}
