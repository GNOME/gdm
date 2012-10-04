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

#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-simple-slave.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS  10
#define DEFAULT_PING_INTERVAL 15

#define INITIAL_SETUP_USERNAME "gnome-initial-setup"
#define INITIAL_SETUP_TRIGGER_FILE LOCALSTATEDIR "/lib/gdm/run-initial-setup"

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

        /* we control the user session */
        GdmSession        *session;

        /* this spawns and controls the greeter session */
        GdmLaunchEnvironment *greeter_environment;

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
static void     gdm_simple_slave_open_reauthentication_channel (GdmSlave             *slave,
                                                                const char           *username,
                                                                GPid                  pid_of_caller,
                                                                uid_t                 uid_of_caller,
                                                                GAsyncReadyCallback   callback,
                                                                gpointer              user_data,
                                                                GCancellable         *cancellable);

static gboolean wants_initial_setup (GdmSimpleSlave *slave);
static void destroy_initial_setup_user (GdmSimpleSlave *slave);
G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

static void create_new_session (GdmSimpleSlave  *slave);
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
        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        g_debug ("GdmSimpleSlave: session exited with code %d\n", exit_code);
        gdm_slave_stop (GDM_SLAVE (slave));
}

static void
on_session_died (GdmSession       *session,
                 int               signal_number,
                 GdmSimpleSlave   *slave)
{
        g_object_set (GDM_SLAVE (slave), "session-id", NULL, NULL);

        g_debug ("GdmSimpleSlave: session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));
        gdm_slave_stop (GDM_SLAVE (slave));
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
switch_to_and_unlock_session (GdmSimpleSlave  *slave,
                              gboolean         fail_if_already_switched)
{
        char    *username;
        gboolean res;

        username = gdm_session_get_username (slave->priv->session);

        g_debug ("GdmSimpleSlave: trying to switch to session for user %s", username);

        /* try to switch to an existing session */
        res = gdm_slave_switch_to_user_session (GDM_SLAVE (slave), username, fail_if_already_switched);
        g_free (username);

        return res;
}

static void
stop_greeter (GdmSimpleSlave *slave)
{
        char *username;
        gboolean script_successful;

        g_debug ("GdmSimpleSlave: Stopping greeter");

        if (slave->priv->greeter_environment == NULL) {
                g_debug ("GdmSimpleSlave: No greeter running");
                return;
        }

        /* Run the PostLogin script. gdmslave suspends until script has terminated */
        username = NULL;
        if (slave->priv->session != NULL) {
                username = gdm_session_get_username (slave->priv->session);
        }

        if (username != NULL) {
                script_successful = gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/PostLogin", username);
        } else {
                script_successful = TRUE;
        }
        g_free (username);

        if (!script_successful) {
                g_debug ("GdmSimpleSlave: PostLogin script unsuccessful");

                slave->priv->start_session_id = 0;
                queue_greeter_reset (slave);
                return;
        }

        gdm_launch_environment_stop (GDM_LAUNCH_ENVIRONMENT (slave->priv->greeter_environment));
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
        gboolean fail_if_already_switched = TRUE;

        g_debug ("GdmSimpleSlave: accredited");

        /* If there's already a session running, jump to it.
         * If the only session running is the one we just opened,
         * start a session on it.
         *
         * We assume we're in the former case if we need to switch
         * VTs, and we assume we're in the latter case if we don't.
         */
        migrated = switch_to_and_unlock_session (slave, fail_if_already_switched);
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
                if (slave->priv->greeter_environment == NULL) {
                        /* auto login */
                        start_session (slave);
                } else {
                        /* Session actually gets started from on_greeter_environment_session_stop */
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
        gboolean fail_if_already_switched = FALSE;

        /* There should already be a session running, so jump to it's
         * VT. In the event we're already on the right VT, (i.e. user
         * used an unlock screen instead of a user switched login screen),
         * then silently succeed and unlock the session.
         */
        switch_to_and_unlock_session (slave, fail_if_already_switched);
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

        if (delay == 0) {
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

        if (g_file_test (GDM_RAN_ONCE_MARKER_FILE, G_FILE_TEST_EXISTS)) {
                return;
        }

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

        g_hash_table_remove (slave->priv->open_reauthentication_requests,
                             source_tag);
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
on_ready_to_request_timed_login (GdmSession         *session,
                                 GSimpleAsyncResult *result,
                                 gpointer           *user_data)
{
        int delay = GPOINTER_TO_INT (user_data);
        GCancellable *cancellable;
        char         *username;

        cancellable = g_object_get_data (G_OBJECT (result),
                                         "cancellable");
        if (g_cancellable_is_cancelled (cancellable)) {
                return;
        }

        username = g_simple_async_result_get_source_tag (result);

        gdm_session_request_timed_login (session, username, delay);

        g_object_weak_unref (G_OBJECT (session),
                             (GWeakNotify)
                             g_cancellable_cancel,
                             cancellable);
        g_object_weak_unref (G_OBJECT (session),
                             (GWeakNotify)
                             g_object_unref,
                             cancellable);
        g_object_weak_unref (G_OBJECT (session),
                             (GWeakNotify)
                             g_free,
                             username);

        g_free (username);
}

static gboolean
on_wait_for_greeter_timeout (GSimpleAsyncResult *result)
{
        g_simple_async_result_complete (result);

        return FALSE;
}

static void
on_session_client_connected (GdmSession          *session,
                             GCredentials        *credentials,
                             GPid                 pid_of_client,
                             GdmSimpleSlave      *slave)
{
        gboolean timed_login_enabled;
        char    *username;
        int      delay;
        gboolean display_is_local;

        g_debug ("GdmSimpleSlave: client connected");

        g_object_get (slave,
                      "display-is-local", &display_is_local,
                      NULL);

        /* If XDMCP stop pinging */
        if ( ! display_is_local) {
                alarm (0);
        }

        timed_login_enabled = FALSE;
        gdm_slave_get_timed_login_details (GDM_SLAVE (slave), &timed_login_enabled, &username, &delay);

        if (! timed_login_enabled) {
                return;
        }

        /* temporary hack to fix timed login
         * http://bugzilla.gnome.org/680348
         */
        if (delay > 0) {
                GSimpleAsyncResult *result;
                GCancellable       *cancellable;
                guint               timeout_id;
                gpointer            source_tag;

                delay = MAX (delay, 4);

                cancellable = g_cancellable_new ();
                source_tag = g_strdup (username);
                result = g_simple_async_result_new (G_OBJECT (session),
                                                    (GAsyncReadyCallback)
                                                    on_ready_to_request_timed_login,
                                                    GINT_TO_POINTER (delay),
                                                    source_tag);
                g_simple_async_result_set_check_cancellable (result, cancellable);
                g_object_set_data (G_OBJECT (result),
                                   "cancellable",
                                   cancellable);

                timeout_id = g_timeout_add_seconds_full (delay - 2,
                                                         G_PRIORITY_DEFAULT,
                                                         (GSourceFunc)
                                                         on_wait_for_greeter_timeout,
                                                         g_object_ref (result),
                                                         (GDestroyNotify)
                                                         g_object_unref);
                g_cancellable_connect (cancellable,
                                       G_CALLBACK (g_source_remove),
                                       GINT_TO_POINTER (timeout_id),
                                       NULL);

                g_object_weak_ref (G_OBJECT (session),
                                   (GWeakNotify)
                                   g_cancellable_cancel,
                                   cancellable);
                g_object_weak_ref (G_OBJECT (session),
                                   (GWeakNotify)
                                   g_object_unref,
                                   cancellable);
                g_object_weak_ref (G_OBJECT (session),
                                   (GWeakNotify)
                                   g_free,
                                   source_tag);
        }

        g_free (username);
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

        if ( ! display_is_local && !slave->priv->session_is_running) {
                gdm_slave_stop (GDM_SLAVE (slave));
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
touch_marker_file (GdmSimpleSlave *slave)
{
        int fd;

        fd = g_creat (GDM_RAN_ONCE_MARKER_FILE, 0644);

        if (fd < 0 && errno != EEXIST) {
                g_warning ("could not create %s to mark run, this may cause auto login "
                           "to repeat: %m", GDM_RAN_ONCE_MARKER_FILE);
                return;
        }

        fsync (fd);
        close (fd);
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

        if (slave->priv->greeter_environment != NULL) {
                greeter_session = gdm_launch_environment_get_session (GDM_LAUNCH_ENVIRONMENT (slave->priv->greeter_environment));
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
                                                display_is_local,
                                                NULL);

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

        touch_marker_file (slave);
}

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
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stop (GDM_SLAVE (slave));
        } else {
                if (wants_initial_setup (slave)) {
                        destroy_initial_setup_user (slave);
                }
                start_session (slave);
        }

        g_object_unref (slave->priv->greeter_environment);
        slave->priv->greeter_environment = NULL;
}

static void
on_greeter_environment_session_exited (GdmLaunchEnvironment    *greeter_environment,
                                       int                      code,
                                       GdmSimpleSlave          *slave)
{
        g_debug ("GdmSimpleSlave: Greeter exited: %d", code);
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stop (GDM_SLAVE (slave));
        }
}

static void
on_greeter_environment_session_died (GdmLaunchEnvironment    *greeter_environment,
                                     int                      signal,
                                     GdmSimpleSlave          *slave)
{
        g_debug ("GdmSimpleSlave: Greeter died: %d", signal);
        if (slave->priv->start_session_service_name == NULL) {
                gdm_slave_stop (GDM_SLAVE (slave));
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

        g_free (display_id);
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

#define RULES_DIR DATADIR "/polkit-1/rules.d/"
#define RULES_FILE "20-gnome-initial-setup.rules"

static const gboolean
create_initial_setup_user (GdmSimpleSlave *slave)
{
        gboolean ret = TRUE;
        ActUserManager *act;
        ActUser *user;
        GFile *src_file, *dest_file;
        GError *error = NULL;
        const char *e = NULL;

        /* First, create the user */
        act = act_user_manager_get_default ();

        user = act_user_manager_create_user (act, INITIAL_SETUP_USERNAME, "", 0, &error);
        if (user == NULL) {
                if (g_dbus_error_is_remote_error (error)) {
                        e = g_dbus_error_get_remote_error (error);
		}

                g_warning ("Creating user '%s' failed: %s / %s",
                           INITIAL_SETUP_USERNAME, e, error->message);

                if (g_strcmp0 (e, "org.freedesktop.Accounts.Error.UserExists") != 0) {
                        ret = FALSE;
                        goto out;
                }

                g_clear_error (&error);
        } else {
                g_object_unref (user);
        }

        /* Now, make sure the PolicyKit policy is in place */
        src_file = g_file_new_for_path (DATADIR "/gnome-initial-setup/" RULES_FILE);
        dest_file = g_file_new_for_path (RULES_DIR RULES_FILE);

        if (!g_file_copy (src_file,
                          dest_file,
                          G_FILE_COPY_OVERWRITE,
                          NULL, NULL, NULL, &error)) {
                g_warning ("Failed to copy '%s' to '%s': %s",
                           g_file_get_path (src_file),
                           g_file_get_path (dest_file),
                           error->message);
                ret = FALSE;
                goto out_clear_files;
        }

 out_clear_files:
        g_object_unref (src_file);
        g_object_unref (dest_file);

 out:
        g_clear_pointer (&e, g_free);
        g_clear_error (&error);
        return ret;
}

static void
destroy_initial_setup_user (GdmSimpleSlave *slave)
{
        ActUserManager *act;
        ActUser *user;
        const char *filename;
        GError *error;

        filename = RULES_DIR RULES_FILE;

        if (g_remove (filename) < 0) {
                g_warning ("Failed to remove '%s': %s", filename, g_strerror (errno));
        }

        act = act_user_manager_get_default ();

        error = NULL;
        user = act_user_manager_get_user (act, INITIAL_SETUP_USERNAME);
        if (user != NULL) {
                if (!act_user_manager_delete_user (act, user, TRUE, &error)) {
                        g_warning ("Failed to delete user '%s': %s", INITIAL_SETUP_USERNAME, error->message);
                        g_error_free (error);
                }
                g_object_unref (user);
        }

        if (g_remove (INITIAL_SETUP_TRIGGER_FILE) < 0) {
                g_warning ("Failed to remove '%s': %s", INITIAL_SETUP_TRIGGER_FILE, g_strerror (errno));
        }
}

static void
start_initial_setup (GdmSimpleSlave *slave)
{
        create_initial_setup_user (slave);
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
        gboolean enabled;

        if (!g_file_test (INITIAL_SETUP_TRIGGER_FILE, G_FILE_TEST_EXISTS)) {
                return FALSE;
        }

        if (!gdm_settings_direct_get_boolean (GDM_KEY_INITIAL_SETUP_ENABLE, &enabled)) {
                return FALSE;
        }

        if (!enabled) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
idle_connect_to_display (GdmSimpleSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
        if (res) {
                setup_server (slave);

                if (wants_initial_setup (slave)) {
                        start_initial_setup (slave);
                } else if (wants_autologin (slave)) {
                        /* Run the init script. gdmslave suspends until script has terminated */
                        gdm_slave_run_script (GDM_SLAVE (slave), GDMCONFDIR "/Init", GDM_USERNAME);
                } else {
                        start_greeter (slave);
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

        g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                              G_OBJECT (self),
                                                              gdm_simple_slave_open_reauthentication_channel), NULL);

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
                                            gdm_simple_slave_open_reauthentication_channel);

        g_simple_async_result_set_check_cancellable (result, cancellable);

        if (!self->priv->session_is_running) {
                g_simple_async_result_set_error (result,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_ACCESS_DENIED,
                                                 _("User not logged in"));
                g_simple_async_result_complete_in_idle (result);

        } else {
                g_hash_table_insert (self->priv->open_reauthentication_requests,
                                     GINT_TO_POINTER (pid_of_caller),
                                     g_object_ref (result));

                gdm_session_start_reauthentication (self->priv->session,
                                                    pid_of_caller,
                                                    uid_of_caller);
        }

        g_object_unref (result);
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

        if (self->priv->greeter_environment != NULL) {
                stop_greeter (self);
                self->priv->greeter_environment = NULL;
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

                self->priv->session_is_running = FALSE;
        }

        if (self->priv->session != NULL) {
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

        gdm_slave_stop (GDM_SLAVE (slave));

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
