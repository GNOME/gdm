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

#ifdef  HAVE_LOGINDEVPERM
#include <libdevinfo.h>
#endif  /* HAVE_LOGINDEVPERM */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-simple-slave.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-welcome-session.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS  10
#define DEFAULT_PING_INTERVAL 15

struct GdmSimpleSlavePrivate
{
        GPid               pid;
        gint               greeter_reset_id;
        guint              start_session_id;

        char              *start_session_service_name;

        int                ping_interval;

        GPid               server_pid;
        guint              connection_attempts;

        GdmServer         *server;
        GdmSession        *session;
        GdmWelcomeSession *greeter;

        GHashTable        *open_reauthentication_requests;

        guint              start_session_when_ready : 1;
        guint              waiting_to_start_session : 1;
        guint              session_is_running : 1;
#ifdef  HAVE_LOGINDEVPERM
        gboolean           use_logindevperm;
#endif
#ifdef  WITH_PLYMOUTH
        guint              plymouth_is_running : 1;
#endif
};

enum {
        PROP_0,
};

static void     gdm_simple_slave_class_init     (GdmSimpleSlaveClass *klass);
static void     gdm_simple_slave_init           (GdmSimpleSlave      *simple_slave);
static void     gdm_simple_slave_finalize       (GObject             *object);

G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

static void create_new_session (GdmSimpleSlave  *slave);
static void start_greeter      (GdmSimpleSlave *slave);
static void start_session      (GdmSimpleSlave  *slave);
static void queue_start_session (GdmSimpleSlave *slave,
                                 const char     *service_name);

static void
on_session_started (GdmSession       *session,
                    const char       *service_name,
                    int               pid,
                    GdmSimpleSlave   *slave)
{
        char *username;
        char *session_id;

        g_debug ("GdmSimpleSlave: session started %d", pid);

        slave->priv->session_is_running = TRUE;

        session_id = gdm_session_get_session_id (session);
        g_object_set (GDM_SLAVE (slave), "session-id", session_id, NULL);
        g_free (session_id);

        /* Run the PreSession script. gdmslave suspends until script has terminated */
        username = gdm_session_get_username (slave->priv->session);
        if (username != NULL) {
                gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/PreSession", username);
        }
        g_free (username);

        /* FIXME: should we do something here?
         * Note that error return status from PreSession script should
         * be ignored in the case of a X-GDM-BypassXsession session, which can
         * be checked by calling:
         * gdm_session_bypasses_xsession (session)
         */
}

#ifdef  HAVE_LOGINDEVPERM
static void
gdm_simple_slave_grant_console_permissions (GdmSimpleSlave *slave)
{
        char *username;
        char *display_device;
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
        char *username;
        char *display_device;

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

        g_free (username);
        g_free (display_device);
}
#endif  /* HAVE_LOGINDEVPERM */

static void
on_session_exited (GdmSession       *session,
                   int               exit_code,
                   GdmSimpleSlave   *slave)
{
        slave->priv->session_is_running = FALSE;
        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        g_debug ("GdmSimpleSlave: session exited with code %d\n", exit_code);
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        }
}

static void
on_session_died (GdmSession       *session,
                 int               signal_number,
                 GdmSimpleSlave   *slave)
{
        slave->priv->session_is_running = FALSE;
        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        g_debug ("GdmSimpleSlave: session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        }
}

static gboolean
add_user_authorization (GdmSimpleSlave *slave,
                        char          **filename)
{
        char    *username;
        gboolean ret;

        username = gdm_session_get_username (slave->priv->session);
        ret = gdm_slave_add_user_authorization (GDM_SLAVE (slave),
                                                username,
                                                filename);
        g_free (username);

        return ret;
}

static void
reset_session (GdmSimpleSlave  *slave)
{
        if (slave->priv->session == NULL) {
                return;
        }

        gdm_session_reset (slave->priv->session);
}

static gboolean
greeter_reset_timeout (GdmSimpleSlave  *slave)
{
        g_debug ("GdmSimpleSlave: resetting greeter");

        reset_session (slave);

        slave->priv->greeter_reset_id = 0;
        return FALSE;
}

static void
queue_greeter_reset (GdmSimpleSlave  *slave)
{
        if (slave->priv->greeter_reset_id > 0) {
                return;
        }

        slave->priv->greeter_reset_id = g_idle_add ((GSourceFunc)greeter_reset_timeout, slave);
}

static void
gdm_simple_slave_start_session_when_ready (GdmSimpleSlave *slave,
                                           const char     *service_name)
{
        if (slave->priv->start_session_when_ready) {
                slave->priv->waiting_to_start_session = FALSE;
                queue_start_session (slave, service_name);
        } else {
                slave->priv->waiting_to_start_session = TRUE;
        }
}

static gboolean
try_migrate_session (GdmSimpleSlave  *slave)
{
        char    *username;
        gboolean res;

        g_debug ("GdmSimpleSlave: trying to migrate session");

        username = gdm_session_get_username (slave->priv->session);

        /* try to switch to an existing session */
        res = gdm_slave_switch_to_user_session (GDM_SLAVE (slave), username);
        g_free (username);

        return res;
}

static void
stop_greeter (GdmSimpleSlave *slave)
{
        char *username;

        g_debug ("GdmSimpleSlave: Stopping greeter");

        if (slave->priv->greeter == NULL) {
                g_debug ("GdmSimpleSlave: No greeter running");
                return;
        }

        /* Run the PostLogin script. gdmslave suspends until script has terminated */
        username = NULL;
        if (slave->priv->session != NULL) {
                username = gdm_session_get_username (slave->priv->session);
        }

        if (username != NULL) {
                gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/PostLogin", username);
        }
        g_free (username);

        gdm_welcome_session_stop (GDM_WELCOME_SESSION (slave->priv->greeter));
}

static void
start_session (GdmSimpleSlave  *slave)
{
        char           *auth_file;

        auth_file = NULL;
        add_user_authorization (slave, &auth_file);

        g_assert (auth_file != NULL);

        g_object_set (slave->priv->session,
                      "user-x11-authority-file", auth_file,
                      NULL);

        g_free (auth_file);

        gdm_session_start_session (slave->priv->session,
                                   slave->priv->start_session_service_name);

        slave->priv->start_session_id = 0;
        g_free (slave->priv->start_session_service_name);
        slave->priv->start_session_service_name = NULL;
}

static gboolean
start_session_timeout (GdmSimpleSlave  *slave)
{
        gboolean migrated;


        g_debug ("GdmSimpleSlave: accredited");

        migrated = try_migrate_session (slave);
        g_debug ("GdmSimpleSlave: migrated: %d", migrated);
        if (migrated) {
                /* We don't stop the slave here because
                   when Xorg exits it switches to the VT it was
                   started from.  That interferes with fast
                   user switching. */
                gdm_session_reset (slave->priv->session);

                slave->priv->start_session_id = 0;
                g_free (slave->priv->start_session_service_name);
                slave->priv->start_session_service_name = NULL;
        } else {
                if (slave->priv->greeter == NULL) {
                        /* auto login */
                        start_session (slave);
                } else {
                        /* Session actually gets started from on_greeter_session_stop */
                        stop_greeter (slave);
                }
        }

        return FALSE;
}

static void
queue_start_session (GdmSimpleSlave *slave,
                     const char     *service_name)
{
        if (slave->priv->start_session_id > 0) {
                return;
        }

        slave->priv->start_session_id = g_idle_add ((GSourceFunc)start_session_timeout, slave);
        slave->priv->start_session_service_name = g_strdup (service_name);
}

static void
on_session_reauthenticated (GdmSession       *session,
                            const char       *service_name,
                            GdmSimpleSlave   *slave)
{
        try_migrate_session (slave);
}

static void
on_session_opened (GdmSession       *session,
                   const char       *service_name,
                   const char       *session_id,
                   GdmSimpleSlave   *slave)
{

#ifdef  HAVE_LOGINDEVPERM
        gdm_simple_slave_grant_console_permissions (slave);
#endif  /* HAVE_LOGINDEVPERM */

        if (gdm_session_client_is_connected (slave->priv->session)) {
                gdm_simple_slave_start_session_when_ready (slave, service_name);
        } else {
                /* Auto login */
                slave->priv->start_session_when_ready = TRUE;
                gdm_simple_slave_start_session_when_ready (slave, service_name);
        }
}

static void
on_session_conversation_started (GdmSession       *session,
                                 const char       *service_name,
                                 GdmSimpleSlave   *slave)
{
        gboolean enabled;
        char    *username;
        int      delay;


        g_debug ("GdmSimpleSlave: session conversation started");
        enabled = FALSE;
        gdm_slave_get_timed_login_details (GDM_SLAVE (slave), &enabled, &username, &delay);
        if (! enabled) {
                return;
        }

        if (delay > 0) {
                gdm_session_request_timed_login (session, username, delay);
        } else {
                g_debug ("GdmSimpleSlave: begin auto login for user '%s'", username);
                /* service_name will be "gdm-autologin"
                 */
                gdm_session_setup_for_user (slave->priv->session, service_name, username);
        }

        g_free (username);
}

static void
on_session_conversation_stopped (GdmSession       *session,
                                 const char       *service_name,
                                 GdmSimpleSlave   *slave)
{
        g_debug ("GdmSimpleSlave: conversation stopped");

}

static void
start_autologin_conversation_if_necessary (GdmSimpleSlave  *slave)
{
        gboolean enabled;

        gdm_slave_get_timed_login_details (GDM_SLAVE (slave), &enabled, NULL, NULL);

        if (!enabled) {
                return;
        }

        g_debug ("GdmSimpleSlave: Starting automatic login conversation");
        gdm_session_start_conversation (slave->priv->session, "gdm-autologin");
}

static void
on_session_reauthentication_started (GdmSession      *session,
                                     int              pid_of_caller,
                                     const char      *address,
                                     GdmSimpleSlave  *slave)
{
        GSimpleAsyncResult *result;
        gpointer            source_tag;

        g_debug ("GdmSimpleSlave: reauthentication started");

        source_tag = GINT_TO_POINTER (pid_of_caller);

        result = g_hash_table_lookup (slave->priv->open_reauthentication_requests,
                                      source_tag);

        if (result != NULL) {
                g_simple_async_result_set_op_res_gpointer (result,
                                                           g_strdup (address),
                                                           (GDestroyNotify)
                                                           g_free);
                g_simple_async_result_complete_in_idle (result);
        }
}

static void
on_session_client_ready_for_session_to_start (GdmSession      *session,
                                              const char      *service_name,
                                              gboolean         client_is_ready,
                                              GdmSimpleSlave  *slave)
{
        if (client_is_ready) {
                g_debug ("GdmSimpleSlave: Will start session when ready");
        } else {
                g_debug ("GdmSimpleSlave: Will start session when ready and told");
        }

        if (slave->priv->greeter_reset_id > 0) {
                return;
        }

        slave->priv->start_session_when_ready = client_is_ready;

        if (client_is_ready && slave->priv->waiting_to_start_session) {
                gdm_simple_slave_start_session_when_ready (slave, service_name);
        }
}
static void
on_session_client_connected (GdmSession          *session,
                             GCredentials        *credentials,
                             GPid                 pid_of_client,
                             GdmSimpleSlave      *slave)
{
        gboolean display_is_local;

        g_debug ("GdmSimpleSlave: client connected");

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      NULL);

        /* If XDMCP stop pinging */
        if ( ! display_is_local) {
                alarm (0);
        }
}

static void
on_session_client_disconnected (GdmSession          *session,
                                GCredentials        *credentials,
                                GPid                 pid_of_client,
                                GdmSimpleSlave      *slave)
{
        gboolean display_is_local;

        g_debug ("GdmSimpleSlave: client disconnected");

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      NULL);

        if ( ! display_is_local) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        }
}

static void
on_session_cancelled (GdmSession      *session,
                      GdmSimpleSlave  *slave)
{
        g_debug ("GdmSimpleSlave: Session was cancelled");
        queue_greeter_reset (slave);
}

static void
create_new_session (GdmSimpleSlave  *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *display_hostname;
        char          *display_device;
        char          *display_seat_id;
        char          *display_x11_authority_file;
        GdmSession    *greeter_session;
        uid_t          greeter_uid;

        g_debug ("GdmSimpleSlave: Creating new session");

        if (slave->priv->greeter != NULL) {
                greeter_session = gdm_welcome_session_get_session (GDM_WELCOME_SESSION (slave->priv->greeter));
                greeter_uid = gdm_session_get_allowed_user (greeter_session);
        } else {
                greeter_uid = 0;
        }

        g_object_get (slave,
                      "display-id", &display_id,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-is-local", &display_is_local,
                      "display-x11-authority-file", &display_x11_authority_file,
                      "display-seat-id", &display_seat_id,
                      NULL);

        display_device = NULL;
        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        slave->priv->session = gdm_session_new (GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                                greeter_uid,
                                                display_name,
                                                display_hostname,
                                                display_device,
                                                display_seat_id,
                                                display_x11_authority_file,
                                                display_is_local);

        g_free (display_id);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);

        g_signal_connect (slave->priv->session,
                          "reauthentication-started",
                          G_CALLBACK (on_session_reauthentication_started),
                          slave);
        g_signal_connect (slave->priv->session,
                          "reauthenticated",
                          G_CALLBACK (on_session_reauthenticated),
                          slave);
        g_signal_connect (slave->priv->session,
                          "client-ready-for-session-to-start",
                          G_CALLBACK (on_session_client_ready_for_session_to_start),
                          slave);
        g_signal_connect (slave->priv->session,
                          "client-connected",
                          G_CALLBACK (on_session_client_connected),
                          slave);
        g_signal_connect (slave->priv->session,
                          "client-disconnected",
                          G_CALLBACK (on_session_client_disconnected),
                          slave);
        g_signal_connect (slave->priv->session,
                          "cancelled",
                          G_CALLBACK (on_session_cancelled),
                          slave);
        g_signal_connect (slave->priv->session,
                          "conversation-started",
                          G_CALLBACK (on_session_conversation_started),
                          slave);
        g_signal_connect (slave->priv->session,
                          "conversation-stopped",
                          G_CALLBACK (on_session_conversation_stopped),
                          slave);
        g_signal_connect (slave->priv->session,
                          "session-opened",
                          G_CALLBACK (on_session_opened),
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

        start_autologin_conversation_if_necessary (slave);
}

static void
on_greeter_session_opened (GdmWelcomeSession *greeter,
                           GdmSimpleSlave    *slave)
{
        char       *session_id;

        g_debug ("GdmSimpleSlave: Greeter session opened");
        session_id = gdm_welcome_session_get_session_id (GDM_WELCOME_SESSION (greeter));

        g_object_set (GDM_SLAVE (slave), "session-id", session_id, NULL);
        g_free (session_id);
}

static void
on_greeter_session_started (GdmWelcomeSession *greeter,
                            GdmSimpleSlave    *slave)
{
        g_debug ("GdmSimpleSlave: Greeter started");
}

static void
on_greeter_session_stopped (GdmWelcomeSession *greeter,
                            GdmSimpleSlave    *slave)
{
        g_debug ("GdmSimpleSlave: Greeter stopped");
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        } else {
                start_session (slave);
        }

        g_object_unref (slave->priv->greeter);
        slave->priv->greeter = NULL;

}

static void
on_greeter_session_exited (GdmWelcomeSession    *greeter,
                           int                   code,
                           GdmSimpleSlave       *slave)
{
        g_debug ("GdmSimpleSlave: Greeter exited: %d", code);
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        }
}

static void
on_greeter_session_died (GdmWelcomeSession    *greeter,
                         int                   signal,
                         GdmSimpleSlave       *slave)
{
        g_debug ("GdmSimpleSlave: Greeter died: %d", signal);
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stopped (GDM_SLAVE (slave));
        }
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

        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));

        /* Set the initial keyboard layout to something reasonable */
        gdm_slave_set_initial_keyboard_layout (GDM_SLAVE (slave));
        /* The root window has a background that may be useful
         * to cross fade or transition from when setting the
         * login screen background.  We read it here, and stuff
         * it into the standard _XROOTPMAP_ID root window property,
         * so gnome-settings-daemon can get at it.
         */
        gdm_slave_save_root_windows (GDM_SLAVE (slave));

#ifdef WITH_PLYMOUTH
        /* Plymouth is waiting for the go-ahead to exit */
        if (slave->priv->plymouth_is_running) {
                plymouth_quit_with_transition (slave);
        }
#endif
}

static GdmWelcomeSession *
create_greeter_session (const char *display_name,
                        const char *seat_id,
                        const char *display_device,
                        const char *display_hostname,
                        gboolean    display_is_local)
{
        gboolean debug = FALSE;
        char *command = BINDIR "/gnome-session -f";

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);

        if (debug) {
                command = BINDIR "/gnome-session -f --debug";
        }

        return g_object_new (GDM_TYPE_WELCOME_SESSION,
                             "command", command,
                             "x11-display-name", display_name,
                             "x11-display-seat-id", seat_id,
                             "x11-display-device", display_device,
                             "x11-display-hostname", display_hostname,
                             "x11-display-is-local", display_is_local,
                             "runtime-dir", GDM_SCREENSHOT_DIR,
                             NULL);
}

static void
start_greeter (GdmSimpleSlave *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *seat_id;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        gboolean       res;

        g_debug ("GdmSimpleSlave: Running greeter");

        display_is_local = FALSE;
        display_id = NULL;
        display_name = NULL;
        seat_id = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-id", &display_id,
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

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", GDM_USERNAME);

        g_debug ("GdmSimpleSlave: Creating greeter on %s %s %s", display_name, display_device, display_hostname);
        slave->priv->greeter = create_greeter_session (display_name,
                                                       seat_id,
                                                       display_device,
                                                       display_hostname,
                                                       display_is_local);
        g_signal_connect (slave->priv->greeter,
                          "opened",
                          G_CALLBACK (on_greeter_session_opened),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "started",
                          G_CALLBACK (on_greeter_session_started),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "stopped",
                          G_CALLBACK (on_greeter_session_stopped),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "exited",
                          G_CALLBACK (on_greeter_session_exited),
                          slave);
        g_signal_connect (slave->priv->greeter,
                          "died",
                          G_CALLBACK (on_greeter_session_died),
                          slave);
        g_object_set (slave->priv->greeter,
                      "x11-authority-file", auth_file,
                      NULL);

        gdm_welcome_session_start (GDM_WELCOME_SESSION (slave->priv->greeter));

        g_free (display_id);
        g_free (display_name);
        g_free (seat_id);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmSimpleSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                gboolean enabled;
                int      delay;

                /* FIXME: handle wait-for-go */

                setup_server (slave);

                delay = 0;
                enabled = FALSE;
                gdm_slave_get_timed_login_details (GDM_SLAVE (slave), &enabled, NULL, &delay);
                if (! enabled || delay > 0) {
                        start_greeter (slave);
                } else {
                        /* Run the init script. gdmslave suspends until script has terminated */
                        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", GDM_USERNAME);
                }

                create_new_session (slave);
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
on_server_ready (GdmServer      *server,
                 GdmSimpleSlave *slave)
{
        g_idle_add ((GSourceFunc)idle_connect_to_display, slave);
}

static void
on_server_exited (GdmServer      *server,
                  int             exit_code,
                  GdmSimpleSlave *slave)
{
        g_debug ("GdmSimpleSlave: server exited with code %d\n", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));

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

        gdm_slave_stopped (GDM_SLAVE (slave));

#ifdef WITH_PLYMOUTH
        if (slave->priv->plymouth_is_running) {
                plymouth_quit_without_transition (slave);
        }
#endif
}

static gboolean
gdm_simple_slave_run (GdmSimpleSlave *slave)
{
        char    *display_name;
        char    *auth_file;
        char    *seat_id;
        gboolean display_is_local;

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-seat-id", &seat_id,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        /* if this is local display start a server if one doesn't
         * exist */
        if (display_is_local) {
                gboolean res;
                gboolean disable_tcp;

                slave->priv->server = gdm_server_new (display_name, seat_id, auth_file);

                disable_tcp = TRUE;
                if (gdm_settings_client_get_boolean (GDM_KEY_DISALLOW_TCP,
                                                     &disable_tcp)) {
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

#ifdef WITH_PLYMOUTH
                slave->priv->plymouth_is_running = plymouth_is_running ();

                if (slave->priv->plymouth_is_running) {
                        plymouth_prepare_for_transition (slave);
                        res = gdm_server_start_on_active_vt (slave->priv->server);
                } else
#endif
                {
                        res = gdm_server_start (slave->priv->server);
                }
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
gdm_simple_slave_open_session (GdmSlave  *slave,
                               GPid       pid_of_caller,
                               uid_t      uid_of_caller,
                               char     **address,
                               GError   **error)
{
        GdmSimpleSlave     *self = GDM_SIMPLE_SLAVE (slave);
        uid_t               allowed_user;

        if (self->priv->session_is_running) {
                g_set_error (error,
                             G_DBUS_ERROR,
                             G_DBUS_ERROR_ACCESS_DENIED,
                             _("Can only be called before user is logged in"));
                return FALSE;
        }

        allowed_user = gdm_session_get_allowed_user (self->priv->session);

        if (uid_of_caller != allowed_user) {
                g_set_error (error,
                             G_DBUS_ERROR,
                             G_DBUS_ERROR_ACCESS_DENIED,
                             _("Caller not GDM"));
                return FALSE;
        }

        *address = gdm_session_get_server_address (self->priv->session);

        return TRUE;
}

static char *
gdm_simple_slave_open_reauthentication_channel_finish (GdmSlave      *slave,
                                                       GAsyncResult  *result,
                                                       GError       **error)
{
        GdmSimpleSlave  *self = GDM_SIMPLE_SLAVE (slave);
        const char      *address;
        GPid             pid_of_caller;

        pid_of_caller = GPOINTER_TO_INT (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)));

        g_hash_table_remove (self->priv->open_reauthentication_requests,
                             GINT_TO_POINTER (pid_of_caller));

        address = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error)) {
                return NULL;
        }

        return g_strdup (address);
}

static void
gdm_simple_slave_open_reauthentication_channel (GdmSlave             *slave,
                                                const char           *username,
                                                GPid                  pid_of_caller,
                                                uid_t                 uid_of_caller,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data,
                                                GCancellable         *cancellable)
{
        GdmSimpleSlave     *self = GDM_SIMPLE_SLAVE (slave);
        GSimpleAsyncResult *result;

        result = g_simple_async_result_new (G_OBJECT (slave),
                                            callback,
                                            user_data,
                                            GINT_TO_POINTER (pid_of_caller));

        g_simple_async_result_set_check_cancellable (result, cancellable);

        g_hash_table_insert (self->priv->open_reauthentication_requests,
                             GINT_TO_POINTER (pid_of_caller),
                             g_object_ref (result));

        if (!self->priv->session_is_running) {
                g_simple_async_result_set_error (result,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_ACCESS_DENIED,
                                                 _("User not logged in"));
                g_simple_async_result_complete_in_idle (result);

        } else {
                gdm_session_start_reauthentication (self->priv->session,
                                                    pid_of_caller,
                                                    uid_of_caller);
        }
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

        if (self->priv->greeter != NULL) {
                stop_greeter (self);
        }

        if (self->priv->session_is_running) {
                char *username;

                /* Run the PostSession script. gdmslave suspends until script
                 * has terminated
                 */
                username = gdm_session_get_username (self->priv->session);
                if (username != NULL) {
                        gdm_slave_run_script (slave, GDMCONFDIR "/PostSession", username);
                }
                g_free (username);

#ifdef  HAVE_LOGINDEVPERM
                gdm_simple_slave_revoke_console_permissions (self);
#endif

                gdm_session_close (self->priv->session);
                g_clear_object (&self->priv->session);
        }

        if (self->priv->server != NULL) {
                gdm_server_stop (self->priv->server);
                g_clear_object (&self->priv->server);
        }

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
        slave_class->open_session = gdm_simple_slave_open_session;
        slave_class->open_reauthentication_channel = gdm_simple_slave_open_reauthentication_channel;
        slave_class->open_reauthentication_channel_finish = gdm_simple_slave_open_reauthentication_channel_finish;

        g_type_class_add_private (klass, sizeof (GdmSimpleSlavePrivate));
}

static void
gdm_simple_slave_init (GdmSimpleSlave *slave)
{
        slave->priv = GDM_SIMPLE_SLAVE_GET_PRIVATE (slave);
#ifdef  HAVE_LOGINDEVPERM
        slave->priv->use_logindevperm = FALSE;
#endif

        slave->priv->open_reauthentication_requests = g_hash_table_new_full (NULL,
                                                                             NULL,
                                                                             (GDestroyNotify)
                                                                             NULL,
                                                                             (GDestroyNotify)
                                                                             g_object_unref);
}

static void
gdm_simple_slave_finalize (GObject *object)
{
        GdmSimpleSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SIMPLE_SLAVE (object));

        slave = GDM_SIMPLE_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        gdm_simple_slave_stop (GDM_SLAVE (slave));

        g_hash_table_unref (slave->priv->open_reauthentication_requests);

        if (slave->priv->greeter_reset_id > 0) {
                g_source_remove (slave->priv->greeter_reset_id);
                slave->priv->greeter_reset_id = 0;
        }

        G_OBJECT_CLASS (gdm_simple_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_simple_slave_new (const char *id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SIMPLE_SLAVE,
                               "display-id", id,
                               NULL);

        return GDM_SLAVE (object);
}
