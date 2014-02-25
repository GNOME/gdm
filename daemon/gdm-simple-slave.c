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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#ifdef  HAVE_LOGINDEVPERM
#include <libdevinfo.h>
#endif  /* HAVE_LOGINDEVPERM */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */

#include <act/act-user-manager.h>

#include "gdm-common.h"

#include "gdm-settings-keys.h"

#include "gdm-simple-slave.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define MAX_CONNECT_ATTEMPTS  10
#define DEFAULT_PING_INTERVAL 15

#define INITIAL_SETUP_USERNAME "gnome-initial-setup"
#define GNOME_SESSION_SESSIONS_PATH DATADIR "/gnome-session/sessions"

struct GdmSimpleSlavePrivate
{
        GPid               pid;
        char              *username;
        gint               greeter_reset_id;

        int                ping_interval;

        GPid               server_pid;
        guint              connection_attempts;

        GdmServer         *server;

        /* this spawns and controls the greeter session */
        GdmLaunchEnvironment *greeter_environment;

        GDBusProxy        *accountsservice_proxy;
        guint              have_existing_user_accounts : 1;
        guint              accountsservice_ready : 1;
        guint              waiting_to_connect_to_display : 1;

#ifdef  HAVE_LOGINDEVPERM
        gboolean           use_logindevperm;
#endif
#ifdef  WITH_PLYMOUTH
        guint              plymouth_is_running : 1;
#endif
        guint              doing_initial_setup : 1;
};

enum {
        PROP_0,
};

static void     gdm_simple_slave_class_init     (GdmSimpleSlaveClass *klass);
static void     gdm_simple_slave_init           (GdmSimpleSlave      *simple_slave);
static void     gdm_simple_slave_finalize       (GObject             *object);

static gboolean wants_initial_setup (GdmSimpleSlave *slave);
G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

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

#ifdef  HAVE_LOGINDEVPERM
static void
gdm_simple_slave_grant_console_permissions (GdmSimpleSlave *slave)
{
        const char *username;
        const char *display_device;
        struct passwd *passwd_entry;

        username = gdm_session_get_username (slave->priv->session);
        display_device = gdm_session_get_display_device (slave->priv->session);

        if (username != NULL) {
                gdm_get_pwent_for_name (username, &passwd_entry);

                /*
                 * Only do logindevperm processing if /dev/console or
                 * a device associated with a VT
                 */
                if (display_device != NULL &&
                   (strncmp (display_device, "/dev/vt/", strlen ("/dev/vt/")) == 0 ||
                    strcmp  (display_device, "/dev/console") == 0)) {
                        g_debug ("Logindevperm login for user %s, device %s",
                                 username, display_device);
                        (void) di_devperm_login (display_device,
                                                 passwd_entry->pw_uid,
                                                 passwd_entry->pw_gid,
                                                 NULL);
                        slave->priv->use_logindevperm = TRUE;
                }
        }

        if (!slave->priv->use_logindevperm) {
                g_debug ("Not calling di_devperm_login login for user %s, device %s",
                         username, display_device);
        }
}

static void
gdm_simple_slave_revoke_console_permissions (GdmSimpleSlave *slave)
{
        const char *username;
        const char *display_device;

        username = gdm_session_get_username (slave->priv->session);
        display_device = gdm_session_get_display_device (slave->priv->session);

        /*
         * Only do logindevperm processing if /dev/console or a device
         * associated with a VT.  Do this after processing the PostSession
         * script so that permissions for devices are not returned to root
         * before running the script.
         */
        if (slave->priv->use_logindevperm == TRUE &&
            display_device != NULL &&
           (strncmp (display_device, "/dev/vt/", strlen ("/dev/vt/")) == 0 ||
            strcmp  (display_device, "/dev/console") == 0)) {
                g_debug ("di_devperm_logout for user %s, device %s",
                         username, display_device);
                (void) di_devperm_logout (display_device);
                slave->priv->use_logindevperm = FALSE;
        } else {
                g_debug ("Not calling di_devperm_logout logout for user %s, device %s",
                         username, display_device);
        }
}
#endif  /* HAVE_LOGINDEVPERM */

static void
on_greeter_environment_session_opened (GdmLaunchEnvironment *greeter_environment,
                                       GdmSimpleSlave       *slave)
{
        char       *session_id;

        g_debug ("GdmSimpleSlave: Greeter session opened");
        session_id = gdm_launch_environment_get_session_id (GDM_LAUNCH_ENVIRONMENT (greeter_environment));

        g_object_set (GDM_SLAVE (slave), "session-id", session_id, NULL);
        g_free (session_id);
}

static void
on_greeter_environment_session_started (GdmLaunchEnvironment *greeter_environment,
                                        GdmSimpleSlave       *slave)
{
        g_debug ("GdmSimpleSlave: Greeter started");
}

static void
on_greeter_environment_session_stopped (GdmLaunchEnvironment *greeter_environment,
                                        GdmSimpleSlave       *slave)
{
        g_debug ("GdmSimpleSlave: Greeter stopped");
        gdm_slave_stop (GDM_SLAVE (slave));

        g_object_unref (slave->priv->greeter_environment);
        slave->priv->greeter_environment = NULL;
}

static void
on_greeter_environment_session_exited (GdmLaunchEnvironment    *greeter_environment,
                                       int                      code,
                                       GdmSimpleSlave          *slave)
{
        g_debug ("GdmSimpleSlave: Greeter exited: %d", code);
        gdm_slave_stop (GDM_SLAVE (slave));
}

static void
on_greeter_environment_session_died (GdmLaunchEnvironment    *greeter_environment,
                                     int                      signal,
                                     GdmSimpleSlave          *slave)
{
        g_debug ("GdmSimpleSlave: Greeter died: %d", signal);
        gdm_slave_stop (GDM_SLAVE (slave));
}

#ifdef  WITH_PLYMOUTH
static gboolean
plymouth_is_running (void)
{
        int      status;
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("/bin/plymouth --ping",
                                         NULL, NULL, &status, &error);
        if (! res) {
                g_debug ("Could not ping plymouth: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

static void
plymouth_prepare_for_transition (GdmSimpleSlave *slave)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("/bin/plymouth deactivate",
                                         NULL, NULL, NULL, &error);
        if (! res) {
                g_warning ("Could not deactivate plymouth: %s", error->message);
                g_error_free (error);
        }
}

static void
plymouth_quit_with_transition (GdmSimpleSlave *slave)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("/bin/plymouth quit --retain-splash",
                                         NULL, NULL, NULL, &error);
        if (! res) {
                g_warning ("Could not quit plymouth: %s", error->message);
                g_error_free (error);
        }
        slave->priv->plymouth_is_running = FALSE;
}

static void
plymouth_quit_without_transition (GdmSimpleSlave *slave)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("/bin/plymouth quit",
                                         NULL, NULL, NULL, &error);
        if (! res) {
                g_warning ("Could not quit plymouth: %s", error->message);
                g_error_free (error);
        }
        slave->priv->plymouth_is_running = FALSE;
}
#endif

static void
setup_server (GdmSimpleSlave *slave)
{
        /* Put cursor out of the way on first head */
        gdm_slave_set_initial_cursor_position (GDM_SLAVE (slave));

#ifdef WITH_PLYMOUTH
        /* Plymouth is waiting for the go-ahead to exit */
        if (slave->priv->plymouth_is_running) {
                plymouth_quit_with_transition (slave);
        }
#endif
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

static GdmLaunchEnvironment *
create_environment (const char *session_id,
                    const char *user_name,
                    const char *display_name,
                    const char *seat_id,
                    const char *display_device,
                    const char *display_hostname,
                    gboolean    display_is_local)
{
        gboolean debug = FALSE;
        char *command;
        GdmLaunchEnvironment *launch_environment;
        char **argv;
        GPtrArray *args;

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);

        args = g_ptr_array_new ();
        g_ptr_array_add (args, BINDIR "/gnome-session");

        g_ptr_array_add (args, "--autostart");
        g_ptr_array_add (args, DATADIR "/gdm/greeter/autostart");

        if (debug) {
                g_ptr_array_add (args, "--debug");
        }

        if (session_id != NULL) {
                g_ptr_array_add (args, " --session");
                g_ptr_array_add (args, (char *) session_id);
        }

        g_ptr_array_add (args, NULL);

        argv = (char **) g_ptr_array_free (args, FALSE);
        command = g_strjoinv (" ", argv);
        g_free (argv);

        launch_environment = g_object_new (GDM_TYPE_LAUNCH_ENVIRONMENT,
                                           "command", command,
                                           "user-name", user_name,
                                           "x11-display-name", display_name,
                                           "x11-display-seat-id", seat_id,
                                           "x11-display-device", display_device,
                                           "x11-display-hostname", display_hostname,
                                           "x11-display-is-local", display_is_local,
                                           "runtime-dir", GDM_SCREENSHOT_DIR,
                                           NULL);

        g_free (command);
        return launch_environment;
}

static void
start_launch_environment (GdmSimpleSlave *slave,
                          char           *username,
                          char           *session_id)
{
        gboolean       display_is_local;
        char          *display_name;
        char          *seat_id;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        gboolean       res;

        g_debug ("GdmSimpleSlave: Running greeter");

        display_is_local = FALSE;
        display_name = NULL;
        seat_id = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-seat-id", &seat_id,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        g_debug ("GdmSimpleSlave: Creating greeter for %s %s", display_name, display_hostname);

        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        /* FIXME: send a signal back to the master */

        /* If XDMCP setup pinging */
        slave->priv->ping_interval = DEFAULT_PING_INTERVAL;
        res = gdm_settings_direct_get_int (GDM_KEY_PING_INTERVAL,
                                           &(slave->priv->ping_interval));

        if ( ! display_is_local && res && slave->priv->ping_interval > 0) {
                alarm (slave->priv->ping_interval);
        }

        g_debug ("GdmSimpleSlave: Creating greeter on %s %s %s", display_name, display_device, display_hostname);
        slave->priv->greeter_environment = create_environment (session_id,
                                                               username,
                                                               display_name,
                                                               seat_id,
                                                               display_device,
                                                               display_hostname,
                                                               display_is_local);
        g_signal_connect (slave->priv->greeter_environment,
                          "opened",
                          G_CALLBACK (on_greeter_environment_session_opened),
                          slave);
        g_signal_connect (slave->priv->greeter_environment,
                          "started",
                          G_CALLBACK (on_greeter_environment_session_started),
                          slave);
        g_signal_connect (slave->priv->greeter_environment,
                          "stopped",
                          G_CALLBACK (on_greeter_environment_session_stopped),
                          slave);
        g_signal_connect (slave->priv->greeter_environment,
                          "exited",
                          G_CALLBACK (on_greeter_environment_session_exited),
                          slave);
        g_signal_connect (slave->priv->greeter_environment,
                          "died",
                          G_CALLBACK (on_greeter_environment_session_died),
                          slave);
        g_object_set (slave->priv->greeter_environment,
                      "x11-authority-file", auth_file,
                      NULL);

        gdm_launch_environment_start (GDM_LAUNCH_ENVIRONMENT (slave->priv->greeter_environment));

        g_free (display_name);
        g_free (seat_id);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static void
start_greeter (GdmSimpleSlave *slave)
{
        start_launch_environment (slave, GDM_USERNAME, NULL);
}

static void
start_initial_setup (GdmSimpleSlave *slave)
{
        slave->priv->doing_initial_setup = TRUE;
        start_launch_environment (slave, INITIAL_SETUP_USERNAME, "gnome-initial-setup");
}

static gboolean
wants_autologin (GdmSimpleSlave *slave)
{
        gboolean enabled = FALSE;
        int delay = 0;
        /* FIXME: handle wait-for-go */

        if (g_file_test (GDM_RAN_ONCE_MARKER_FILE, G_FILE_TEST_EXISTS)) {
                return FALSE;
        }

        gdm_slave_get_timed_login_details (GDM_SLAVE (slave), &enabled, NULL, &delay);
        return enabled && delay == 0;
}

static gboolean
wants_initial_setup (GdmSimpleSlave *slave)
{
        gboolean enabled = FALSE;
        gboolean display_is_local = FALSE;

        g_object_get (G_OBJECT (slave),
                      "display-is-local", &display_is_local,
                      NULL);

        /* don't run initial-setup on remote displays
         */
        if (!display_is_local) {
                return FALSE;
        }

        /* don't run if the system has existing users */
        if (slave->priv->have_existing_user_accounts) {
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

static void
gdm_simple_slave_set_up_greeter_session (GdmSlave  *slave,
                                         char     **username)
{
        GdmSimpleSlave *self = GDM_SIMPLE_SLAVE (slave);

        if (wants_initial_setup (self)) {
                *username = g_strdup (INITIAL_SETUP_USERNAME);
        } else if (wants_autologin (self)) {
                *username = g_strdup ("root");
        } else {
                *username = g_strdup (GDM_USERNAME);
        }
}

static void
gdm_simple_slave_stop_greeter_session (GdmSlave *slave)
{
        GdmSimpleSlave *self = GDM_SIMPLE_SLAVE (slave);

        if (self->priv->greeter_environment != NULL) {
                g_signal_handlers_disconnect_by_func (self->priv->greeter_environment,
                                                      G_CALLBACK (on_greeter_environment_session_opened),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->greeter_environment,
                                                      G_CALLBACK (on_greeter_environment_session_started),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->greeter_environment,
                                                      G_CALLBACK (on_greeter_environment_session_stopped),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->greeter_environment,
                                                      G_CALLBACK (on_greeter_environment_session_exited),
                                                      self);
                g_signal_handlers_disconnect_by_func (self->priv->greeter_environment,
                                                      G_CALLBACK (on_greeter_environment_session_died),
                                                      self);
                gdm_launch_environment_stop (GDM_LAUNCH_ENVIRONMENT (self->priv->greeter_environment));
                g_clear_object (&self->priv->greeter_environment);
        }

        if (GDM_SIMPLE_SLAVE (slave)->priv->doing_initial_setup) {
                chown_initial_setup_home_dir ();
        }
}

static void
gdm_simple_slave_start_greeter_session (GdmSlave *slave)
{
        if (wants_initial_setup (GDM_SIMPLE_SLAVE (slave))) {
                start_initial_setup (GDM_SIMPLE_SLAVE (slave));
        } else if (!wants_autologin (GDM_SIMPLE_SLAVE (slave))) {
                start_greeter (GDM_SIMPLE_SLAVE (slave));
        }
}

static gboolean
idle_connect_to_display (GdmSimpleSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                setup_server (slave);
        } else {
                if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
                        g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
                        exit (1);
                }
                return TRUE;
        }

        return FALSE;
}

static void
connect_to_display_when_accountsservice_ready (GdmSimpleSlave *slave)
{
        if (slave->priv->accountsservice_ready) {
                slave->priv->waiting_to_connect_to_display = FALSE;
                g_idle_add ((GSourceFunc)idle_connect_to_display, slave);
        } else {
                slave->priv->waiting_to_connect_to_display = TRUE;
        }
}

static void
on_server_ready (GdmServer      *server,
                 GdmSimpleSlave *slave)
{
        connect_to_display_when_accountsservice_ready (slave);
}

static void
on_server_exited (GdmServer      *server,
                  int             exit_code,
                  GdmSimpleSlave *slave)
{
        g_debug ("GdmSimpleSlave: server exited with code %d\n", exit_code);

        gdm_slave_stop (GDM_SLAVE (slave));

#ifdef WITH_PLYMOUTH
        if (slave->priv->plymouth_is_running) {
                plymouth_quit_without_transition (slave);
        }
#endif
}

static void
on_server_died (GdmServer      *server,
                int             signal_number,
                GdmSimpleSlave *slave)
{
        g_debug ("GdmSimpleSlave: server died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stop (GDM_SLAVE (slave));

#ifdef WITH_PLYMOUTH
        if (slave->priv->plymouth_is_running) {
                plymouth_quit_without_transition (slave);
        }
#endif
}

static void
on_list_cached_users_complete (GObject       *proxy,
                               GAsyncResult  *result,
                               gpointer       user_data)
{
        GdmSimpleSlave *slave = GDM_SIMPLE_SLAVE (user_data);
        GVariant *call_result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), result, NULL);
        GVariant *user_list;

        if (!call_result) {
                slave->priv->have_existing_user_accounts = FALSE;
        } else {
                g_variant_get (call_result, "(@ao)", &user_list);
                slave->priv->have_existing_user_accounts = g_variant_n_children (user_list) > 0;
                g_variant_unref (user_list);
                g_variant_unref (call_result);
        }

        slave->priv->accountsservice_ready = TRUE;

        if (slave->priv->waiting_to_connect_to_display) {
                connect_to_display_when_accountsservice_ready (slave);
        }
}

static void
on_accountsservice_ready (GObject       *object,
                          GAsyncResult  *result,
                          gpointer       user_data)
{
        GdmSimpleSlave *slave = GDM_SIMPLE_SLAVE (user_data);
        GError *local_error = NULL;

        slave->priv->accountsservice_proxy = g_dbus_proxy_new_for_bus_finish (result, &local_error);
        if (!slave->priv->accountsservice_proxy) {
                g_error ("Failed to contact accountsservice: %s", local_error->message);
        } 

        g_dbus_proxy_call (slave->priv->accountsservice_proxy, "ListCachedUsers", NULL, 0, -1, NULL,
                           on_list_cached_users_complete, slave);
}
                          

static gboolean
gdm_simple_slave_run (GdmSimpleSlave *slave)
{
        char    *display_name;
        char    *auth_file;
        char    *seat_id;
        gboolean display_is_local;
        gboolean display_is_initial;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-seat-id", &seat_id,
                      "display-x11-authority-file", &auth_file,
                      "display-is-initial", &display_is_initial,
                      NULL);

        /* if this is local display start a server if one doesn't
         * exist */
        if (display_is_local) {
                gboolean res;
                gboolean disable_tcp;

                slave->priv->server = gdm_server_new (display_name, seat_id, auth_file, display_is_initial);

                disable_tcp = TRUE;
                if (gdm_settings_direct_get_boolean (GDM_KEY_DISALLOW_TCP, &disable_tcp)) {
                        g_object_set (slave->priv->server,
                                      "disable-tcp", disable_tcp,
                                      NULL);
                }

                g_signal_connect (slave->priv->server,
                                  "exited",
                                  G_CALLBACK (on_server_exited),
                                  slave);
                g_signal_connect (slave->priv->server,
                                  "died",
                                  G_CALLBACK (on_server_died),
                                  slave);
                g_signal_connect (slave->priv->server,
                                  "ready",
                                  G_CALLBACK (on_server_ready),
                                  slave);

                g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                          0, NULL,
                                          "org.freedesktop.Accounts",
                                          "/org/freedesktop/Accounts",
                                          "org.freedesktop.Accounts",
                                          NULL,
                                          on_accountsservice_ready, slave);
                
#ifdef WITH_PLYMOUTH
                slave->priv->plymouth_is_running = plymouth_is_running ();

                if (slave->priv->plymouth_is_running) {
                        plymouth_prepare_for_transition (slave);
                }
#endif
                res = gdm_server_start (slave->priv->server);
                if (! res) {
                        g_warning (_("Could not start the X "
                                     "server (your graphical environment) "
                                     "due to an internal error. "
                                     "Please contact your system administrator "
                                     "or check your syslog to diagnose. "
                                     "In the meantime this display will be "
                                     "disabled.  Please restart GDM when "
                                     "the problem is corrected."));
#ifdef WITH_PLYMOUTH
                        if (slave->priv->plymouth_is_running) {
                                plymouth_quit_without_transition (slave);
                        }
#endif
                        exit (1);
                }

                g_debug ("GdmSimpleSlave: Started X server");
        } else {
                g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
        }

        g_free (display_name);
        g_free (auth_file);

        return TRUE;
}

static gboolean
gdm_simple_slave_start (GdmSlave *slave)
{
        GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->start (slave);

        gdm_simple_slave_run (GDM_SIMPLE_SLAVE (slave));

        return TRUE;
}

static gboolean
gdm_simple_slave_stop (GdmSlave *slave)
{
        GdmSimpleSlave *self = GDM_SIMPLE_SLAVE (slave);

        g_debug ("GdmSimpleSlave: Stopping simple_slave");

        GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->stop (slave);

        gdm_simple_slave_stop_greeter_session (slave);

        if (self->priv->server != NULL) {
                gdm_server_stop (self->priv->server);
                g_clear_object (&self->priv->server);
        }

        g_clear_object (&self->priv->accountsservice_proxy);

        return TRUE;
}

static void
gdm_simple_slave_class_init (GdmSimpleSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);
        GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

        object_class->finalize = gdm_simple_slave_finalize;

        slave_class->start = gdm_simple_slave_start;
        slave_class->stop = gdm_simple_slave_stop;
        slave_class->set_up_greeter_session = gdm_simple_slave_set_up_greeter_session;
        slave_class->start_greeter_session = gdm_simple_slave_start_greeter_session;
        slave_class->stop_greeter_session = gdm_simple_slave_stop_greeter_session;

        g_type_class_add_private (klass, sizeof (GdmSimpleSlavePrivate));
}

static void
gdm_simple_slave_init (GdmSimpleSlave *slave)
{
        slave->priv = GDM_SIMPLE_SLAVE_GET_PRIVATE (slave);
#ifdef  HAVE_LOGINDEVPERM
        slave->priv->use_logindevperm = FALSE;
#endif
}

static void
gdm_simple_slave_finalize (GObject *object)
{
        GdmSimpleSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SIMPLE_SLAVE (object));

        slave = GDM_SIMPLE_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        if (slave->priv->greeter_reset_id > 0) {
                g_source_remove (slave->priv->greeter_reset_id);
                slave->priv->greeter_reset_id = 0;
        }

        G_OBJECT_CLASS (gdm_simple_slave_parent_class)->finalize (object);
}
