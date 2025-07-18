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
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <act/act-user-manager.h>

#include <systemd/sd-login.h>

#include "gdm-common.h"

#include "gdm-dbus-util.h"
#include "gdm-manager.h"
#include "gdm-manager-glue.h"
#include "gdm-display-store.h"
#include "gdm-display-factory.h"
#include "gdm-launch-environment.h"
#include "gdm-local-display.h"
#include "gdm-local-display-factory.h"
#include "gdm-remote-display.h"
#include "gdm-remote-display-factory.h"
#include "gdm-session.h"
#include "gdm-session-record.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"
#include "gdm-xdmcp-display-factory.h"
#include "gdm-xdmcp-chooser-display.h"

#define GDM_DBUS_PATH             "/org/gnome/DisplayManager"
#define GDM_MANAGER_PATH          GDM_DBUS_PATH "/Manager"
#define GDM_MANAGER_DISPLAYS_PATH GDM_DBUS_PATH "/Displays"

#define INITIAL_SETUP_USERNAME "gnome-initial-setup"
#define ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT GDM_RUN_DIR "/gdm.ran-initial-setup"

typedef struct
{
        GdmManager *manager;
        GdmSession *session;
        char *service_name;
        guint idle_id;
} StartUserSessionOperation;

struct _GdmManager
{
        GdmDBusManagerSkeleton parent;
        GdmDisplayStore        *display_store;
        GdmLocalDisplayFactory *local_factory;
#ifdef HAVE_LIBXDMCP
        GdmXdmcpDisplayFactory *xdmcp_factory;
#endif
        GdmRemoteDisplayFactory *remote_factory;
        GdmDisplay             *automatic_login_display;
        GList                  *user_sessions;
        GHashTable             *transient_sessions;
        GHashTable             *open_reauthentication_requests;
        gboolean                xdmcp_enabled;
        gboolean                remote_login_enabled;

        gboolean                started;
        gboolean                show_local_greeter;

        GDBusConnection          *connection;
        GDBusObjectManagerServer *object_manager;

#ifdef  WITH_PLYMOUTH
        guint                     plymouth_is_running : 1;
#endif
        guint                     did_automatic_login : 1;
};

enum {
        PROP_0,
        PROP_XDMCP_ENABLED,
        PROP_SHOW_LOCAL_GREETER,
        PROP_REMOTE_LOGIN_ENABLED
};

enum {
        DISPLAY_ADDED,
        DISPLAY_REMOVED,
        LAST_SIGNAL
};

typedef enum {
        SESSION_RECORD_LOGIN,
        SESSION_RECORD_LOGOUT,
        SESSION_RECORD_FAILED,
} SessionRecord;

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_manager_class_init  (GdmManagerClass *klass);
static void     gdm_manager_init        (GdmManager      *manager);
static void     gdm_manager_dispose     (GObject         *object);

static void     create_user_session_for_display (GdmManager *manager,
                                                 GdmDisplay *display,
                                                 uid_t       allowed_user);
static void     start_user_session (GdmManager                *manager,
                                    StartUserSessionOperation *operation);
static void     clean_user_session (GdmSession *session);

static gpointer manager_object = NULL;

static void manager_interface_init (GdmDBusManagerIface *interface);

G_DEFINE_TYPE_WITH_CODE (GdmManager,
                         gdm_manager,
                         GDM_DBUS_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (GDM_DBUS_TYPE_MANAGER,
                                                manager_interface_init));

#ifdef WITH_PLYMOUTH
static gboolean
plymouth_is_running (void)
{
        int      status;
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("plymouth --ping",
                                         NULL, NULL, &status, &error);
        if (! res) {
                g_debug ("Could not ping plymouth: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

static void
plymouth_prepare_for_transition (void)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_sync ("plymouth deactivate",
                                         NULL, NULL, NULL, &error);
        if (! res) {
                g_warning ("Could not deactivate plymouth: %s", error->message);
                g_error_free (error);
        }
}

static gboolean
plymouth_quit_with_transition (void)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_async ("plymouth quit --retain-splash", &error);
        if (! res) {
                g_warning ("Could not quit plymouth: %s", error->message);
                g_error_free (error);
        }

        return G_SOURCE_REMOVE;
}

static void
plymouth_quit_without_transition (void)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = g_spawn_command_line_async ("plymouth quit", &error);
        if (! res) {
                g_warning ("Could not quit plymouth: %s", error->message);
                g_error_free (error);
        }
}
#endif

static char *
get_session_id_for_pid (pid_t    pid,
                        GError **error)
{
        char *session, *gsession;
        int ret;

        session = NULL;
        ret = sd_pid_get_session (pid, &session);
        if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting session id from systemd: %s",
                             g_strerror (-ret));
                return NULL;
        }

        if (session != NULL) {
                gsession = g_strdup (session);
                free (session);

                return gsession;
        } else {
                return NULL;
        }
}

static gboolean
get_uid_for_session_id (const char  *session_id,
                        uid_t       *uid,
                        GError     **error)
{
        int ret;

        ret = sd_session_get_uid (session_id, uid);
        if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting uid for session id %s from systemd: %s",
                             session_id,
                             g_strerror (-ret));
                return FALSE;
        }

        return TRUE;
}

static gboolean
lookup_by_session_id (const char *id,
                      GdmDisplay *display,
                      gpointer    user_data)
{
        const char *looking_for = user_data;
        const char *current;

        current = gdm_display_get_session_id (display);
        return g_strcmp0 (current, looking_for) == 0;
}

static gboolean
is_login_session (GdmManager  *self,
                  const char  *session_id,
                  GError     **error)
{
        char *session_class = NULL;
        int ret;

        ret = sd_session_get_class (session_id, &session_class);

        if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting class for session id %s from systemd: %s",
                             session_id,
                             g_strerror (-ret));
                return FALSE;
        }

        if (g_strcmp0 (session_class, "greeter") != 0) {
                g_free (session_class);
                return FALSE;
        }

        g_free (session_class);
        return TRUE;
}

static gboolean
session_unlock (GdmManager *manager,
                const char *ssid)
{
        GError *error = NULL;
        GVariant *reply;

        g_debug ("Unlocking session %s", ssid);

        reply = g_dbus_connection_call_sync (manager->connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "UnlockSession",
                                             g_variant_new ("(s)", ssid),
                                             NULL, /* expected reply */
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);
        if (reply == NULL) {
                g_debug ("GdmManager: logind 'UnlockSession' %s raised:\n %s\n\n",
                         g_dbus_error_get_remote_error (error), error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

static GdmSession *
find_session_for_user (GdmManager *manager,
                       const char *username,
                       GdmSession *dont_count_session)
{
        GList *node;

        for (node = manager->user_sessions; node != NULL; node = node->next) {
                GdmSession *candidate_session = node->data;
                const char *candidate_username, *candidate_seat_id, *candidate_session_id;

                candidate_session_id = gdm_session_get_session_id (candidate_session);

                if (candidate_session == dont_count_session) {
                        g_debug ("GdmSession: Ignoring session %s as requested",
                                 candidate_session_id);
                        continue;
                }

                if (!gdm_session_is_running (candidate_session)) {
                        g_debug ("GdmSession: Ignoring session %s as it isn't running",
                                 candidate_session_id);
                        continue;
                }

                candidate_username = gdm_session_get_username (candidate_session);
                candidate_seat_id = gdm_session_get_display_seat_id (candidate_session);

                g_debug ("GdmManager: Considering session %s on seat %s belonging to user %s",
                         candidate_session_id,
                         candidate_seat_id,
                         candidate_username);

                if (g_strcmp0 (candidate_username, username) == 0) {
                        g_debug ("GdmManager: yes, found session %s", candidate_session_id);
                        return candidate_session;
                }

                g_debug ("GdmManager: no, will not use session %s", candidate_session_id);
        }

        g_debug ("GdmManager: no matching sessions found");
        return NULL;
}

static gboolean
is_remote_session (GdmManager  *self,
                   const char  *session_id,
                   GError     **error)
{
        int ret;

        ret = sd_session_is_remote (session_id);

        if (ret < 0 && ret != -ENXIO) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "%s", g_strerror (-ret));
                return FALSE;
        }

        return ret != FALSE;
}

static char *
get_seat_id_for_session_id (const char  *session_id,
                            GError     **error)
{
        int ret;
        char *seat, *out_seat;

        seat = NULL;
        ret = sd_session_get_seat (session_id, &seat);

        if (ret == -ENXIO) {
                out_seat = NULL;
        } else if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting uid for session id %s from systemd: %s",
                             session_id,
                             g_strerror (-ret));
                out_seat = NULL;
        } else {
                out_seat = g_strdup (seat);
                free (seat);
        }

        return out_seat;
}

static char *
get_tty_for_session_id (const char  *session_id,
                        GError     **error)
{
        int ret;
        char *tty, *out_tty;

        ret = sd_session_get_tty (session_id, &tty);

        if (ret == -ENXIO) {
                out_tty = NULL;
        } else if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting tty for session id %s from systemd: %s",
                             session_id,
                             g_strerror (-ret));
                out_tty = NULL;
        } else {
                out_tty = g_strdup (tty);
                free (tty);
        }

        return out_tty;
}

static void
get_display_and_details_for_bus_sender (GdmManager       *self,
                                        GDBusConnection  *connection,
                                        const char       *sender,
                                        GdmDisplay      **out_display,
                                        char            **out_seat_id,
                                        char            **out_session_id,
                                        char            **out_tty,
                                        GPid             *out_pid,
                                        uid_t            *out_uid,
                                        gboolean         *out_is_login_screen,
                                        gboolean         *out_is_remote)
{
        GdmDisplay *display = NULL;
        char       *session_id = NULL;
        GError     *error = NULL;
        int         ret;
        GPid        pid;
        uid_t       caller_uid, session_uid;

        ret = gdm_dbus_get_pid_for_name (sender, &pid, &error);

        if (!ret) {
                g_debug ("GdmManager: Error while retrieving pid for sender: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (out_pid != NULL) {
                *out_pid = pid;
        }

        ret = gdm_dbus_get_uid_for_name (sender, &caller_uid, &error);

        if (!ret) {
                g_debug ("GdmManager: Error while retrieving uid for sender: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        ret = gdm_find_display_session (pid, caller_uid, &session_id, &error);

        if (!ret) {
                g_debug ("GdmManager: Unable to find display session for uid %d: %s",
                         (int) caller_uid,
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (out_session_id != NULL) {
                *out_session_id = g_strdup (session_id);
        }

        if (out_is_login_screen != NULL) {
                *out_is_login_screen = is_login_session (self, session_id, &error);

                if (error != NULL) {
                        g_debug ("GdmManager: Error while checking if sender is login screen: %s",
                                 error->message);
                        g_error_free (error);
                        goto out;
                }
        }

        if (!get_uid_for_session_id (session_id, &session_uid, &error)) {
                g_debug ("GdmManager: Error while retrieving uid for session: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (out_uid != NULL) {
                *out_uid = caller_uid;
        }

        if (caller_uid != session_uid) {
                g_debug ("GdmManager: uid for sender and uid for session don't match");
                goto out;
        }

        if (out_seat_id != NULL) {
                *out_seat_id = get_seat_id_for_session_id (session_id, &error);

                if (error != NULL) {
                        g_debug ("GdmManager: Error while retrieving seat id for session: %s",
                                 error->message);
                        g_clear_error (&error);
                }
        }

        if (out_is_remote != NULL) {
                *out_is_remote = is_remote_session (self, session_id, &error);

                if (error != NULL) {
                        g_debug ("GdmManager: Error while retrieving remoteness for session %s: %s",
                                 session_id, error->message);
                        g_clear_error (&error);
                }
        }

        if (out_tty != NULL) {
                *out_tty = get_tty_for_session_id (session_id, &error);

                if (error != NULL) {
                        g_debug ("GdmManager: Error while retrieving tty for session: %s",
                                 error->message);
                        g_clear_error (&error);
                }
        }

        display = gdm_display_store_find (self->display_store,
                                          lookup_by_session_id,
                                          (gpointer) session_id);

out:
        if (out_display != NULL) {
                *out_display = display;
        }

        g_free (session_id);
}

static gboolean
switch_to_compatible_user_session (GdmManager *manager,
                                   GdmSession *session,
                                   gboolean    fail_if_already_switched)
{
        gboolean    res;
        gboolean    ret;
        const char *username;
        const char *seat_id;
        const char *ssid_to_activate;
        GdmSession *existing_session;

        ret = FALSE;

        username = gdm_session_get_username (session);
        seat_id = gdm_session_get_display_seat_id (session);

        if (!fail_if_already_switched) {
                session = NULL;
        }

        existing_session = find_session_for_user (manager, username, session);

        if (existing_session != NULL) {
                ssid_to_activate = gdm_session_get_session_id (existing_session);
                if (seat_id != NULL) {
                        res = gdm_activate_session_by_id (manager->connection, NULL, seat_id, ssid_to_activate);
                        if (! res) {
                                g_debug ("GdmManager: unable to activate session: %s", ssid_to_activate);
                                goto out;
                        }
                }

                res = session_unlock (manager, ssid_to_activate);
                if (!res) {
                        /* this isn't fatal */
                        g_debug ("GdmManager: unable to unlock session: %s", ssid_to_activate);
                }
        } else {
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static GdmDisplay *
get_display_for_user_session (GdmSession *session)
{
        return g_object_get_data (G_OBJECT (session), "gdm-display");
}

static GdmSession *
get_user_session_for_display (GdmDisplay *display)
{
        if (display == NULL) {
                return NULL;
        }

        return g_object_get_data (G_OBJECT (display), "gdm-user-session");
}

static gboolean
add_session_record (GdmManager    *manager,
                    GdmSession    *session,
                    GPid           pid,
                    SessionRecord  record)
{
        const char *username;
        char *display_name, *hostname, *display_device, *display_seat_id;
        gboolean recorded = FALSE;

        display_name = NULL;
        username = NULL;
        hostname = NULL;
        display_device = NULL;
        display_seat_id = NULL;

        username = gdm_session_get_username (session);

        if (username == NULL) {
                goto out;
        }

        g_object_get (G_OBJECT (session),
                      "display-name", &display_name,
                      "display-hostname", &hostname,
                      "display-device", &display_device,
                      "display-seat-id", &display_seat_id,
                      NULL);

        if (display_name == NULL && display_device == NULL) {
                if (display_seat_id == NULL)
                        goto out;

                display_name = g_strdup ("login screen");
                display_device = g_strdup (display_seat_id);
        }

        switch (record) {
            case SESSION_RECORD_LOGIN:
                gdm_session_record_login (pid,
                                          username,
                                          hostname,
                                          display_name,
                                          display_device);
                break;
            case SESSION_RECORD_LOGOUT:
                gdm_session_record_logout (pid,
                                           username,
                                           hostname,
                                           display_name,
                                           display_device);
                break;
            case SESSION_RECORD_FAILED:
                gdm_session_record_failed (pid,
                                           username,
                                           hostname,
                                           display_name,
                                           display_device);
                break;
        }

        recorded = TRUE;
out:
        g_free (display_name);
        g_free (hostname);
        g_free (display_device);
        g_free (display_seat_id);

        return recorded;
}

static GdmSession *
find_user_session_for_display (GdmManager *self,
                               GdmDisplay *display)
{

        GList *node = self->user_sessions;

        while (node != NULL) {
                GdmSession *session = node->data;
                GdmDisplay *candidate_display;
                GList *next_node = node->next;

                candidate_display = get_display_for_user_session (session);

                if (candidate_display == display)
                        return session;

                node = next_node;
        }

        return NULL;
}

static gboolean
gdm_manager_handle_register_display (GdmDBusManager        *manager,
                                     GDBusMethodInvocation *invocation,
                                     GVariant              *details)
{
        GdmManager      *self = GDM_MANAGER (manager);
        const char      *sender;
        GDBusConnection *connection;
        GdmDisplay      *display = NULL;
        GdmSession      *session;
        GVariantIter     iter;
        char            *key = NULL;
        char            *value = NULL;
        char            *x11_display_name = NULL;
        char            *tty = NULL;

        g_debug ("GdmManager: trying to register new display");

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);
        get_display_and_details_for_bus_sender (self, connection, sender, &display, NULL, NULL, &tty, NULL, NULL, NULL, NULL);

        if (display == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("No display available"));

                return TRUE;
        }

        g_variant_iter_init (&iter, details);
        while (g_variant_iter_loop (&iter, "{&s&s}", &key, &value)) {
                if (g_strcmp0 (key, "x11-display-name") == 0) {
                        x11_display_name = g_strdup (value);
                        break;
                }
        }

        session = find_user_session_for_display (self, display);

        if (session != NULL) {
                GPid pid;

                if (x11_display_name != NULL) {
                        g_object_set (G_OBJECT (session), "display-name", x11_display_name, NULL);
                        g_object_set (G_OBJECT (display), "x11-display-name", x11_display_name, NULL);
                }

                /* FIXME: this should happen in gdm-session.c when the session is opened
                 */
                if (tty != NULL)
                        g_object_set (G_OBJECT (session), "display-device", tty, NULL);

                pid = gdm_session_get_pid (session);

                if (pid > 0) {
                        add_session_record (self, session, pid, SESSION_RECORD_LOGIN);
                }
        }

        g_object_set (G_OBJECT (display), "status", GDM_DISPLAY_MANAGED, NULL);

        gdm_dbus_manager_complete_register_display (GDM_DBUS_MANAGER (manager),
                                                    invocation);

        g_clear_pointer (&x11_display_name, g_free);
        g_clear_pointer (&tty, g_free);
        return TRUE;
}

static gboolean
gdm_manager_handle_register_session (GdmDBusManager        *manager,
                                     GDBusMethodInvocation *invocation,
                                     GVariant              *details)
{
        GdmManager      *self = GDM_MANAGER (manager);
        GdmDisplay      *display = NULL;
        const char      *sender;
        GDBusConnection *connection;

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);

        get_display_and_details_for_bus_sender (self, connection, sender, &display,
                                                NULL, NULL, NULL, NULL, NULL, NULL, NULL);

        g_debug ("GdmManager: trying to register new session on display %p", display);

        if (display != NULL)
                g_object_set (G_OBJECT (display), "session-registered", TRUE, NULL);
        else
                g_debug ("GdmManager: No display, not registering");

        gdm_dbus_manager_complete_register_session (GDM_DBUS_MANAGER (manager),
                                                    invocation);

        return TRUE;
}

static gboolean
gdm_manager_handle_open_session (GdmDBusManager        *manager,
                                 GDBusMethodInvocation *invocation)
{
        GdmManager       *self = GDM_MANAGER (manager);
        const char       *sender;
        GDBusConnection  *connection;
        GdmDisplay       *display = NULL;
        GdmSession       *session = NULL;
        const char       *address;
        GPid              pid = 0;
        uid_t             uid = (uid_t) -1;
        uid_t             allowed_user;

        g_debug ("GdmManager: trying to open new session");

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);
        get_display_and_details_for_bus_sender (self, connection, sender, &display, NULL, NULL, NULL, &pid, &uid, NULL, NULL);

        if (display == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("No session available"));

                return TRUE;
        }

#ifdef HAVE_LIBXDMCP
        if (GDM_IS_XDMCP_CHOOSER_DISPLAY (display)) {
                GdmLaunchEnvironment *launch_environment;

                g_object_get (display, "launch-environment", &launch_environment, NULL);

                if (launch_environment != NULL) {
                        session = gdm_launch_environment_get_session (launch_environment);
                }

                if (session == NULL) {
                        g_dbus_method_invocation_return_error_literal (invocation,
                                                                       G_DBUS_ERROR,
                                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                                       _("Chooser session unavailable"));
                        return TRUE;
                }
        }
#endif
        if (session == NULL) {
                session = get_user_session_for_display (display);
                g_debug ("GdmSession: Considering session %s for username %s",
                         gdm_session_get_session_id (session),
                         gdm_session_get_username (session));

                if (gdm_session_is_running (session)) {
                        g_debug ("GdmSession: the session is running, and therefore can't be used");
                        g_dbus_method_invocation_return_error_literal (invocation,
                                                                       G_DBUS_ERROR,
                                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                                       _("Can only be called before user is logged in"));
                        return TRUE;
                }
        }

        allowed_user = gdm_session_get_allowed_user (session);

        if (uid != allowed_user) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("Caller not GDM"));
                return TRUE;
        }

        address = gdm_session_get_server_address (session);

        if (address == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("Unable to open private communication channel"));
                return TRUE;
        }

        gdm_dbus_manager_complete_open_session (GDM_DBUS_MANAGER (manager),
                                                invocation,
                                                address);
        return TRUE;
}

static void
close_transient_session (GdmManager *self,
                         GdmSession *session)
{
        GPid pid;
        pid = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (session), "caller-pid"));
        gdm_session_close (session);
        g_hash_table_remove (self->transient_sessions,
                             GUINT_TO_POINTER (pid));
}

static void
on_reauthentication_client_connected (GdmSession              *session,
                                      GCredentials            *credentials,
                                      GPid                     pid_of_client,
                                      GdmManager              *self)
{
        g_debug ("GdmManager: client connected to reauthentication server");
}

static void
on_reauthentication_client_disconnected (GdmSession              *session,
                                         GCredentials            *credentials,
                                         GPid                     pid_of_client,
                                         GdmManager              *self)
{
        g_debug ("GdmManger: client disconnected from reauthentication server");
        close_transient_session (self, session);
}

static void
on_reauthentication_client_rejected (GdmSession              *session,
                                     GCredentials            *credentials,
                                     GPid                     pid_of_client,
                                     GdmManager              *self)
{
        GPid pid;

        g_debug ("GdmManger: client with pid %ld rejected from reauthentication server", (long) pid_of_client);

        if (gdm_session_client_is_connected (session)) {
                /* we already have a client connected, ignore this rejected one */
                return;
        }

        pid = (GPid) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (session), "caller-pid"));

        if (pid != pid_of_client) {
                const char *session_id;
                g_autofree char *client_session_id = NULL;

                /* rejected client isn't the process that started the
                 * transient reauthentication session. If it's not even from the
                 * same audit session, ignore it since it doesn't "own" the
                 * reauthentication session
                 */
                client_session_id = get_session_id_for_pid (pid_of_client,
                                                            NULL);
                session_id = g_object_get_data (G_OBJECT (session), "caller-session-id");

                if (g_strcmp0 (session_id, client_session_id) != 0) {
                        return;
                }
        }

        /* client was rejected, so clean up its session object
         */
        close_transient_session (self, session);
}

static void
on_reauthentication_cancelled (GdmSession *session,
                               GdmManager *self)
{
        g_debug ("GdmManager: client cancelled reauthentication request");
        close_transient_session (self, session);
}

static void
on_reauthentication_conversation_started (GdmSession *session,
                                          const char *service_name,
                                          GdmManager *self)
{
        g_debug ("GdmManager: reauthentication service '%s' started",
                 service_name);
}

static void
on_reauthentication_conversation_stopped (GdmSession *session,
                                          const char *service_name,
                                          GdmManager *self)
{
        g_debug ("GdmManager: reauthentication service '%s' stopped",
                 service_name);
}

static void
on_reauthentication_verification_complete (GdmSession *session,
                                           const char *service_name,
                                           GdmManager *self)
{
        GdmSession *user_session;
        const char *caller_session_id;

        user_session = g_object_get_data (G_OBJECT (session), "user-session");
        caller_session_id = g_object_get_data (G_OBJECT (session), "caller-session-id");

        if (user_session != NULL) {
                g_debug ("GdmManager: reauthenticated user in frozen session '%s' with service '%s'",
                         gdm_session_get_session_id (user_session), service_name);

                switch_to_compatible_user_session (self, user_session, FALSE);
        } else if (caller_session_id != NULL) {
                g_debug ("GdmManager: reauthenticated user in unmanaged session '%s' with service '%s'",
                         caller_session_id, service_name);

                session_unlock (self, caller_session_id);
        }

        close_transient_session (self, session);
}

static char *
open_temporary_reauthentication_channel (GdmManager            *self,
                                         GdmSession            *user_session,
                                         char                  *seat_id,
                                         char                  *caller_session_id,
                                         GPid                   pid,
                                         uid_t                  uid,
                                         gboolean               is_remote)
{
        GdmSession *session;
        char **environment;
        const char *display, *auth_file;
        const char *address;

        /* Note we're just using a minimal environment here rather than the
         * session's environment because the caller is unprivileged and the
         * associated worker will be privileged */
        environment = g_get_environ ();
        display = "";
        auth_file = "/dev/null";

        session = gdm_session_new (GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE,
                                   uid,
                                   display,
                                   NULL,
                                   NULL,
                                   seat_id,
                                   auth_file,
                                   is_remote == FALSE,
                                   (const char * const *)
                                   environment);
        g_strfreev (environment);

        g_debug ("GdmSession: Created session for temporary reauthentication channel for user %d (seat %s)",
                 (int) uid,
                 seat_id);

        g_object_set_data_full (G_OBJECT (session),
                                "caller-session-id",
                                g_strdup (caller_session_id),
                                (GDestroyNotify)
                                g_free);
        g_object_set_data_full (G_OBJECT (session),
                                "user-session",
                                user_session? g_object_ref (user_session) : NULL,
                                (GDestroyNotify)
                                g_object_unref);
        g_object_set_data (G_OBJECT (session),
                           "caller-pid",
                           GUINT_TO_POINTER (pid));
        g_hash_table_insert (self->transient_sessions,
                             GINT_TO_POINTER (pid),
                             session);

        g_signal_connect (session,
                          "client-connected",
                          G_CALLBACK (on_reauthentication_client_connected),
                          self);
        g_signal_connect (session,
                          "client-disconnected",
                          G_CALLBACK (on_reauthentication_client_disconnected),
                          self);
        g_signal_connect (session,
                          "client-rejected",
                          G_CALLBACK (on_reauthentication_client_rejected),
                          self);
        g_signal_connect (session,
                          "cancelled",
                          G_CALLBACK (on_reauthentication_cancelled),
                          self);
        g_signal_connect (session,
                          "conversation-started",
                          G_CALLBACK (on_reauthentication_conversation_started),
                          self);
        g_signal_connect (session,
                          "conversation-stopped",
                          G_CALLBACK (on_reauthentication_conversation_stopped),
                          self);
        g_signal_connect (session,
                          "verification-complete",
                          G_CALLBACK (on_reauthentication_verification_complete),
                          self);

        address = gdm_session_get_server_address (session);

        return g_strdup (address);
}

static gboolean
are_sessions_compatible (GdmSession *session_a,
                         GdmSession *session_b)
{
        gboolean session_a_is_local = FALSE;
        gboolean session_b_is_local = FALSE;
        const char *session_a_seat_id, *session_b_seat_id;

        g_object_get (G_OBJECT (session_a), "display-is-local", &session_a_is_local, NULL);
        g_object_get (G_OBJECT (session_b), "display-is-local", &session_b_is_local, NULL);

        session_a_seat_id = gdm_session_get_display_seat_id (session_a);
        session_b_seat_id = gdm_session_get_display_seat_id (session_b);

        if (session_a_is_local != session_b_is_local)
                return FALSE;

        if (!session_a_is_local && !session_b_is_local)
                return TRUE;

        return g_strcmp0 (session_a_seat_id, session_b_seat_id) == 0;
}

static gboolean
gdm_manager_handle_open_reauthentication_channel (GdmDBusManager        *manager,
                                                  GDBusMethodInvocation *invocation,
                                                  const char            *username)
{
        GdmManager       *self = GDM_MANAGER (manager);
        const char       *sender;
        GdmDisplay       *display = NULL;
        GdmSession       *session;
        GdmSession       *login_session = NULL;
        GDBusConnection  *connection;
        char             *seat_id = NULL;
        char             *session_id = NULL;
        GPid              pid = 0;
        uid_t             uid = (uid_t) -1;
        gboolean          is_login_screen = FALSE;
        gboolean          is_remote = FALSE;
        char             *address = NULL;

        g_debug ("GdmManager: trying to open reauthentication channel for user %s", username);

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);
        get_display_and_details_for_bus_sender (self, connection, sender, &display, &seat_id, &session_id, NULL, &pid, &uid, &is_login_screen, &is_remote);

        if (session_id == NULL || pid == 0 || uid == (uid_t) -1) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("No session available"));

                return TRUE;
        }

        if (is_login_screen) {
                g_debug ("GdmManager: looking for login screen session for user %s", username);
                session = find_session_for_user (self,
                                                 username,
                                                 NULL);
                login_session = get_user_session_for_display (display);
                g_object_set_data (G_OBJECT (display), "reauth-pid-of-caller", GINT_TO_POINTER (pid));
        } else {
                g_debug ("GdmManager: looking for user session on display");
                session = get_user_session_for_display (display);
        }

        if (session != NULL && login_session != NULL &&
            !are_sessions_compatible (session, login_session)) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               "Login session is not compatible with user session");
                return TRUE;
        } else if (session != NULL && gdm_session_is_running (session)) {
                if (!gdm_session_is_frozen (session)) {
                        gdm_session_start_reauthentication (session, pid, uid);
                        g_hash_table_insert (self->open_reauthentication_requests,
                                             GINT_TO_POINTER (pid),
                                             invocation);
                        return TRUE;
                } else {
                        g_debug("GdmManager: user session is frozen; using temporary reauthentication channel");
                }
        } else if (is_login_screen) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               "Login screen only allowed to open reauthentication channels for running sessions");
                return TRUE;
        }

        address = open_temporary_reauthentication_channel (self,
                                                           is_login_screen? session : NULL,
                                                           seat_id,
                                                           session_id,
                                                           pid,
                                                           uid,
                                                           is_remote);
        gdm_dbus_manager_complete_open_reauthentication_channel (GDM_DBUS_MANAGER (manager),
                                                                 invocation,
                                                                 address);
        g_free (address);
        return TRUE;
}

static void
manager_interface_init (GdmDBusManagerIface *interface)
{
        interface->handle_register_display = gdm_manager_handle_register_display;
        interface->handle_register_session = gdm_manager_handle_register_session;
        interface->handle_open_session = gdm_manager_handle_open_session;
        interface->handle_open_reauthentication_channel = gdm_manager_handle_open_reauthentication_channel;
}

static gboolean
display_is_on_seat0 (GdmDisplay *display)
{
        gboolean is_on_seat0 = TRUE;
        char *seat_id = NULL;

        g_object_get (G_OBJECT (display), "seat-id", &seat_id, NULL);

        if (g_strcmp0 (seat_id, "seat0") != 0) {
            is_on_seat0 = FALSE;
        }

        g_free (seat_id);

        return is_on_seat0;
}

static gboolean
get_timed_login_details (GdmManager *manager,
                         char      **usernamep,
                         int        *delayp)
{
        gboolean res;
        gboolean enabled;

        int      delay;
        char    *username = NULL;

        enabled = FALSE;
        username = NULL;
        delay = 0;

        res = gdm_settings_direct_get_boolean (GDM_KEY_TIMED_LOGIN_ENABLE, &enabled);
        if (res && ! enabled) {
                goto out;
        }

        res = gdm_settings_direct_get_string (GDM_KEY_TIMED_LOGIN_USER, &username);
        if (res && (username == NULL || username[0] == '\0')) {
                g_clear_pointer (&username, g_free);
                goto out;
        }

        delay = 0;
        res = gdm_settings_direct_get_int (GDM_KEY_TIMED_LOGIN_DELAY, &delay);

        if (res && delay <= 0) {
                /* we don't allow the timed login to have a zero delay */
                delay = 10;
        }

 out:
        if (enabled) {
                g_debug ("GdmDisplay: Got timed login details for display: %d %s %d",
                         enabled,
                         username,
                         delay);
        } else {
                g_debug ("GdmDisplay: Got timed login details for display: 0");
        }

        if (usernamep != NULL) {
                *usernamep = username;
        } else {
                g_free (username);
        }
        if (delayp != NULL) {
                *delayp = delay;
        }

        return enabled;
}

static gboolean
get_automatic_login_details (GdmManager *manager,
                             char      **usernamep)
{
        gboolean res;
        gboolean enabled;
        char    *username = NULL;

        enabled = FALSE;
        username = NULL;

        res = gdm_settings_direct_get_boolean (GDM_KEY_AUTO_LOGIN_ENABLE, &enabled);
        if (res && enabled) {
            res = gdm_settings_direct_get_string (GDM_KEY_AUTO_LOGIN_USER, &username);
        }

        if (enabled && res && username != NULL && username[0] != '\0') {
                goto out;
        }

        g_free (username);
        username = NULL;
        enabled = FALSE;

 out:
        if (enabled) {
                g_debug ("GdmDisplay: Got automatic login details for display: %d %s",
                         enabled,
                         username);
        } else {
                g_debug ("GdmDisplay: Got automatic login details for display: 0");
        }

        if (usernamep != NULL) {
                *usernamep = username;
        } else {
                g_free (username);
        }

        return enabled;
}

static const char *
get_username_for_greeter_display (GdmManager *manager,
                                  GdmDisplay *display)
{
        gboolean doing_initial_setup = FALSE;

        g_object_get (G_OBJECT (display),
                      "doing-initial-setup", &doing_initial_setup,
                      NULL);

        if (doing_initial_setup) {
                return INITIAL_SETUP_USERNAME;
        } else {
                return GDM_USERNAME;
        }
}

static void
set_up_automatic_login_session (GdmManager *manager,
                                GdmDisplay *display)
{
        GdmSession *session;
        g_auto (GStrv) supported_session_types = NULL;

        /* 0 is root user; since the daemon talks to the session object
         * directly, itself, for automatic login
         */
        create_user_session_for_display (manager, display, 0);
        session = get_user_session_for_display (display);

        g_object_get (G_OBJECT (display),
                      "supported-session-types", &supported_session_types,
                      NULL);

        g_object_set (G_OBJECT (session),
                      "display-is-initial", FALSE,
                      "supported-session-types", supported_session_types,
                      NULL);

        g_debug ("GdmManager: Starting automatic login conversation");
        gdm_session_start_conversation (session, "gdm-autologin");
}

#ifdef HAVE_LIBXDMCP
static void
set_up_chooser_session (GdmManager *manager,
                        GdmDisplay *display)
{
        const char *allowed_user;
        struct passwd *passwd_entry;

        allowed_user = get_username_for_greeter_display (manager, display);

        if (!gdm_get_pwent_for_name (allowed_user, &passwd_entry)) {
                g_warning ("GdmManager: couldn't look up username %s",
                           allowed_user);
                gdm_display_unmanage (display);
                gdm_display_finish (display);
                return;
        }

        gdm_display_start_greeter_session (display);
}
#endif

static void
set_up_greeter_session (GdmManager *manager,
                        GdmDisplay *display)
{
        const char *allowed_user;
        struct passwd *passwd_entry;

        allowed_user = get_username_for_greeter_display (manager, display);

        if (!gdm_get_pwent_for_name (allowed_user, &passwd_entry)) {
                g_warning ("GdmManager: couldn't look up username %s",
                           allowed_user);
                gdm_display_unmanage (display);
                gdm_display_finish (display);
                return;
        }

        create_user_session_for_display (manager, display, passwd_entry->pw_uid);
        gdm_display_start_greeter_session (display);
}

static void
set_up_automatic_login_session_if_user_exists (GdmManager *manager,
                                               GdmDisplay *display,
                                               ActUser    *user)
{
        if (act_user_is_nonexistent (user))
                set_up_greeter_session (manager, display);
        else
                set_up_automatic_login_session (manager, display);
}

typedef struct {
        GdmManager *manager;
        GdmDisplay *display;
        char *username;
} UsernameLookupOperation;

static void
destroy_username_lookup_operation (UsernameLookupOperation *operation)
{
        g_object_unref (operation->manager);
        g_object_unref (operation->display);
        g_free (operation->username);
        g_free (operation);
}

static void
on_user_is_loaded_changed (ActUser                 *user,
                           GParamSpec              *pspec,
                           UsernameLookupOperation *operation)
{
        if (act_user_is_loaded (user)) {
                set_up_automatic_login_session_if_user_exists (operation->manager, operation->display, user);
                g_signal_handlers_disconnect_by_func (G_OBJECT (user),
                                                      G_CALLBACK (on_user_is_loaded_changed),
                                                      operation);
                destroy_username_lookup_operation (operation);
        }
}

static void
set_up_session (GdmManager *manager,
                GdmDisplay *display)
{
        ActUserManager *user_manager;
        ActUser *user;
        gboolean loaded;
        gboolean seat_can_autologin = FALSE, seat_did_autologin = FALSE;
        gboolean autologin_enabled = FALSE;
        g_autofree char *seat_id = NULL;
        char *username = NULL;

        g_object_get (G_OBJECT (display), "seat-id", &seat_id, NULL);

        if (g_strcmp0 (seat_id, "seat0") == 0)
                seat_can_autologin = TRUE;

        if (manager->did_automatic_login || manager->automatic_login_display != NULL)
                seat_did_autologin = TRUE;

        if (seat_can_autologin && !seat_did_autologin)
                autologin_enabled = get_automatic_login_details (manager, &username);

        if (!autologin_enabled) {
                g_free (username);

#ifdef HAVE_LIBXDMCP
                if (GDM_IS_XDMCP_CHOOSER_DISPLAY (display)) {
                        set_up_chooser_session (manager, display);
                        return;
                }
#endif

                set_up_greeter_session (manager, display);
                return;
        }

        /* Check whether the user really exists before committing to autologin. */
        user_manager = act_user_manager_get_default ();
        user = act_user_manager_get_user (user_manager, username);
        g_object_get (user_manager, "is-loaded", &loaded, NULL);

        if (loaded) {
                set_up_automatic_login_session_if_user_exists (manager, display, user);
        } else {
                UsernameLookupOperation *operation;

                operation = g_new (UsernameLookupOperation, 1);
                operation->manager = g_object_ref (manager);
                operation->display = g_object_ref (display);
                operation->username = username;

                g_signal_connect (user,
                                  "notify::is-loaded",
                                  G_CALLBACK (on_user_is_loaded_changed),
                                  operation);
        }
}

static void
on_display_status_changed (GdmDisplay *display,
                           GParamSpec *arg1,
                           GdmManager *manager)
{
        int         status;
        int         display_number = -1;
        g_autofree char *session_type = NULL;
        gboolean    doing_initial_setup = FALSE;
#ifdef WITH_PLYMOUTH
        gboolean    display_is_local = FALSE;
        gboolean    quit_plymouth = FALSE;

        g_object_get (display,
                      "is-local", &display_is_local,
                      NULL);
        quit_plymouth = display_is_local && manager->plymouth_is_running;
#endif

        g_object_get (display,
                      "x11-display-number", &display_number,
                      "session-type", &session_type,
                      "doing-initial-setup", &doing_initial_setup,
                      NULL);

        status = gdm_display_get_status (display);

        switch (status) {
                case GDM_DISPLAY_PREPARED:
                case GDM_DISPLAY_MANAGED:
                        if ((display_number == -1 && status == GDM_DISPLAY_PREPARED) ||
                            (display_number != -1 && status == GDM_DISPLAY_MANAGED)) {
                                g_autofree char *session_class = NULL;

                                g_object_get (display,
                                              "session-class", &session_class,
                                              NULL);
                                if (g_strcmp0 (session_class, "greeter") == 0)
                                        set_up_session (manager, display);
                        }
                        break;
                case GDM_DISPLAY_FAILED:
                case GDM_DISPLAY_UNMANAGED:
                case GDM_DISPLAY_FINISHED:
#ifdef WITH_PLYMOUTH
                        if (quit_plymouth) {
                                plymouth_quit_without_transition ();
                                manager->plymouth_is_running = FALSE;
                        }
#endif

                        g_object_set_data (G_OBJECT (display), "gdm-user-session", NULL);

                        if (display == manager->automatic_login_display) {
                                g_clear_weak_pointer (&manager->automatic_login_display);

                                manager->did_automatic_login = TRUE;

#ifdef ENABLE_WAYLAND_SUPPORT
                                if (g_strcmp0 (session_type, "wayland") != 0 && status == GDM_DISPLAY_FAILED) {
                                        /* we're going to fall back to X11, so try to autologin again
                                         */
                                        manager->did_automatic_login = FALSE;
                                }
#endif
                        }
                        break;
                default:
                        break;
        }

}

static void
on_display_removed (GdmDisplayStore *display_store,
                    GdmDisplay      *display,
                    GdmManager      *manager)
{
        char    *id;

        gdm_display_get_id (display, &id, NULL);
        g_dbus_object_manager_server_unexport (manager->object_manager, id);
        g_free (id);

        g_signal_handlers_disconnect_by_func (display, G_CALLBACK (on_display_status_changed), manager);

        g_signal_emit (manager, signals[DISPLAY_REMOVED], 0, display);
}

static void
destroy_start_user_session_operation (StartUserSessionOperation *operation)
{
        g_object_set_data (G_OBJECT (operation->session),
                           "start-user-session-operation",
                           NULL);
        g_object_unref (operation->session);
        g_free (operation->service_name);
        g_slice_free (StartUserSessionOperation, operation);
}

static void
start_user_session (GdmManager *manager,
                    StartUserSessionOperation *operation)
{
        GdmDisplay *display;

        display = get_display_for_user_session (operation->session);

        if (display != NULL) {
                gboolean is_connected = FALSE;

                g_object_get (G_OBJECT (display), "is-connected", &is_connected, NULL);

                if (is_connected) {
                        char *auth_file = NULL;
                        const char *username;

                        username = gdm_session_get_username (operation->session);
                        gdm_display_add_user_authorization (display,
                                                            username,
                                                            &auth_file,
                                                            NULL);

                        g_assert (auth_file != NULL);

                        g_object_set (operation->session,
                                      "user-x11-authority-file", auth_file,
                                      NULL);

                        g_free (auth_file);
                }
        }

        gdm_session_start_session (operation->session,
                                   operation->service_name);

        destroy_start_user_session_operation (operation);
}

static void
create_display_for_user_session (GdmManager *self,
                                 GdmSession *session,
                                 const char *session_id)
{
        GdmDisplay *display = NULL;
        const char *seat_id = gdm_session_get_display_seat_id (session);
        gboolean display_is_local;

        g_object_get (G_OBJECT (session), "display-is-local", &display_is_local, NULL);

        if (!display_is_local) {
                g_autofree char *remote_id = NULL;

                g_object_get (G_OBJECT (session),
                              "remote-id", &remote_id,
                              NULL);

                display = gdm_remote_display_new (remote_id);
        }

        if (display == NULL)
                display = gdm_local_display_new ();

        g_object_set (G_OBJECT (display),
                      "session-class", "user",
                      "seat-id", seat_id,
                      "session-id", session_id,
                      NULL);
        gdm_display_store_add (self->display_store,
                               display);
        g_object_set_data (G_OBJECT (session), "gdm-display", display);
        g_object_set_data_full (G_OBJECT (display),
                                "gdm-user-session",
                                g_object_ref (session),
                                (GDestroyNotify)
                                clean_user_session);
}

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

        g_debug ("Changing ownership of %s to %u:%u", gis_dir_path, pwe->pw_uid, pwe->pw_gid);

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

static gboolean
on_start_user_session (StartUserSessionOperation *operation)
{
        GdmManager *self = operation->manager;
        gboolean migrated;
        gboolean fail_if_already_switched = TRUE;
        GdmDisplay *display;
        const char *session_id;

        g_debug ("GdmManager: start or jump to session");

        /* If there's already a session running, jump to it.
         * If the only session running is the one we just opened,
         * start a session on it.
         */
        migrated = switch_to_compatible_user_session (operation->manager, operation->session, fail_if_already_switched);

        g_debug ("GdmManager: migrated: %d", migrated);
        if (migrated) {
                /* We don't stop the manager here because
                   when Xorg exits it switches to the VT it was
                   started from.  That interferes with fast
                   user switching. */
                gdm_session_reset (operation->session);
                destroy_start_user_session_operation (operation);
                goto out;
        }

        display = get_display_for_user_session (operation->session);

        session_id = gdm_session_get_conversation_session_id (operation->session,
                                                              operation->service_name);

        if (gdm_session_get_display_mode (operation->session) == GDM_SESSION_DISPLAY_MODE_REUSE_VT) {
                /* In this case, the greeter's display is morphing into
                 * the user session display. Kill the greeter on this session
                 * and let the user session follow the same display. */
                gdm_display_stop_greeter_session (display);
                g_object_set (G_OBJECT (display),
                                "session-class", "user",
                                "session-id", session_id,
                                NULL);
        } else {
                gboolean doing_initial_setup = FALSE;
                uid_t allowed_uid;

                g_object_get (G_OBJECT (display),
                              "doing-initial-setup", &doing_initial_setup,
                              NULL);

                g_object_ref (display);
                if (doing_initial_setup) {
                        g_autoptr(GError) error = NULL;

                        g_debug ("GdmManager: closing down initial setup display in background");
                        g_object_set (G_OBJECT (display), "status", GDM_DISPLAY_WAITING_TO_FINISH, NULL);

                        if (!g_file_set_contents (ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT,
                                                  "1",
                                                  1,
                                                  &error)) {
                                g_warning ("GdmDisplay: Could not write initial-setup-done marker to %s: %s",
                                           ALREADY_RAN_INITIAL_SETUP_ON_THIS_BOOT,
                                           error->message);
                                g_clear_error (&error);
                        }
                } else {
                        g_debug ("GdmManager: session has its display server, reusing our server for another login screen");
                }

                /* The user session is going to follow the session worker
                 * into the new display. Untie it from this display and
                 * create a new session for a future user login. */
                allowed_uid = gdm_session_get_allowed_user (operation->session);
                g_object_set_data (G_OBJECT (display), "gdm-user-session", NULL);
                g_object_set_data (G_OBJECT (operation->session), "gdm-display", NULL);
                create_user_session_for_display (operation->manager, display, allowed_uid);

                /* Give the user session a new display object for bookkeeping purposes */
                create_display_for_user_session (operation->manager,
                                                 operation->session,
                                                 session_id);

                if (g_strcmp0 (operation->service_name, "gdm-autologin") == 0 &&
	            !gdm_session_client_is_connected (operation->session)) {
                        /* remove the unused prepared greeter display since we're not going
                         * to have a greeter */
                        gdm_display_store_remove (self->display_store, display);
                        g_object_unref (display);

                        self->automatic_login_display = g_object_get_data (G_OBJECT (operation->session), "gdm-display");
                        g_object_add_weak_pointer (G_OBJECT (self->automatic_login_display), (gpointer *) &self->automatic_login_display);
                }
        }

        start_user_session (operation->manager, operation);

 out:
        return G_SOURCE_REMOVE;
}

static void
queue_start_user_session (GdmManager *manager,
                          GdmSession *session,
                          const char *service_name)
{
        StartUserSessionOperation *operation;

        operation = g_slice_new0 (StartUserSessionOperation);
        operation->manager = manager;
        operation->session = g_object_ref (session);
        operation->service_name = g_strdup (service_name);

        operation->idle_id = g_idle_add ((GSourceFunc) on_start_user_session, operation);
        g_object_set_data (G_OBJECT (session), "start-user-session-operation", operation);
}

static void
start_user_session_if_ready (GdmManager *manager,
                             GdmSession *session,
                             const char *service_name)
{
        gboolean start_when_ready;

        start_when_ready = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (session), "start-when-ready"));
        if (start_when_ready) {
                g_object_set_data (G_OBJECT (session), "waiting-to-start", GINT_TO_POINTER (FALSE));
                queue_start_user_session (manager, session, service_name);
        } else {
                g_object_set_data (G_OBJECT (session), "waiting-to-start", GINT_TO_POINTER (TRUE));
        }
}

static void
on_session_authentication_failed (GdmSession *session,
                                  const char *service_name,
                                  GPid        conversation_pid,
                                  GdmManager *manager)
{
        add_session_record (manager, session, conversation_pid, SESSION_RECORD_FAILED);
}

static void
on_session_credentials_established (GdmSession *session,
                                    const char *service_name,
                                    GdmManager *manager)
{
        GdmDisplay *display;
        gboolean    doing_initial_setup = FALSE;

        display = get_display_for_user_session (session);
        if (display == NULL) {
                g_debug ("GdmManager: session has no associated display");
                return;
        }

        /* If this is the initial setup, chown the g-i-s homedir now before the
         * PAM session is opened and the consumers are started.
         */
        g_object_get (G_OBJECT (display),
                      "doing-initial-setup", &doing_initial_setup,
                      NULL);
        if (doing_initial_setup)
                chown_initial_setup_home_dir ();
}

static void
on_user_session_opened (GdmSession       *session,
                        const char       *service_name,
                        const char       *session_id,
                        GdmManager       *manager)
{
        manager->user_sessions = g_list_append (manager->user_sessions,
                                                      g_object_ref (session));
        if (g_strcmp0 (service_name, "gdm-autologin") == 0 &&
            !gdm_session_client_is_connected (session)) {
                /* If we're auto logging in then don't wait for the go-ahead from a greeter,
                 * (since there is no greeter) */
                g_object_set_data (G_OBJECT (session), "start-when-ready", GINT_TO_POINTER (TRUE));
        }

        start_user_session_if_ready (manager, session, service_name);
}

static void
on_user_session_started (GdmSession      *session,
                         const char      *service_name,
                         GPid             pid,
                         GdmManager      *manager)
{
        g_debug ("GdmManager: session started %d", pid);
        add_session_record (manager, session, pid, SESSION_RECORD_LOGIN);

#ifdef WITH_PLYMOUTH
        if (g_strcmp0 (service_name, "gdm-autologin") == 0) {
                if (manager->plymouth_is_running) {
                        g_timeout_add_seconds (20, (GSourceFunc) plymouth_quit_with_transition, NULL);
                        manager->plymouth_is_running = FALSE;
                }
        }
#endif
}

static void
remove_user_session (GdmManager *manager,
                     GdmSession *session)
{
        GList *node;
        GdmDisplay *display;

        display = get_display_for_user_session (session);

        if (display != NULL) {
                gdm_display_unmanage (display);
                gdm_display_finish (display);
        }

        node = g_list_find (manager->user_sessions, session);

        if (node != NULL) {
                manager->user_sessions = g_list_delete_link (manager->user_sessions, node);
                gdm_session_close (session);
                g_object_unref (session);
        }
}

static void
on_session_start_failed (GdmSession *session,
                         const char *service_name,
                         const char *message,
                         GdmManager *manager)
{
        g_debug ("GdmManager: session failed to start: %s", message);
        remove_user_session (manager, session);
}

static void
on_user_session_exited (GdmSession *session,
                        int         code,
                        GdmManager *manager)
{
        GPid pid;

        g_debug ("GdmManager: session exited with status %d", code);
        pid = gdm_session_get_pid (session);

        if (pid > 0) {
                add_session_record (manager, session, pid, SESSION_RECORD_LOGOUT);
        }

        remove_user_session (manager, session);
}

static void
on_user_session_died (GdmSession *session,
                      int         signal_number,
                      GdmManager *manager)
{
        g_debug ("GdmManager: session died with signal %s", strsignal (signal_number));
        remove_user_session (manager, session);
}

static char *
get_display_device (GdmManager *manager,
                    GdmDisplay *display)
{
        /* systemd finds the display device out on its own based on the display */
        return NULL;
}

static gboolean
lookup_by_reauth_pid (const char *id,
                      GdmDisplay *display,
                      gpointer    user_data)
{
        GPid looking_for;
        GPid current;

        looking_for = GPOINTER_TO_INT (user_data);
        current = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (display), "reauth-pid-of-caller"));

        if (looking_for == 0 || current == 0)
                return FALSE;

        return looking_for == current;
}

static void
on_session_reauthenticated (GdmSession *session,
                            const char *service_name,
                            int         pid_of_caller,
                            GdmManager *manager)
{
        gboolean fail_if_already_switched = FALSE;
        GdmDisplay *login_display = gdm_display_store_find (manager->display_store,
                                                            lookup_by_reauth_pid,
                                                            GINT_TO_POINTER (pid_of_caller));

        if (login_display != NULL) {
                if (GDM_IS_REMOTE_DISPLAY (login_display)) {
                        const char *session_id;
                        GdmDisplay *user_display;

                        session_id = gdm_session_get_session_id (session);
                        user_display = gdm_display_store_find (manager->display_store,
                                                               lookup_by_session_id,
                                                               (gpointer) session_id);

                        if (user_display != NULL && GDM_IS_REMOTE_DISPLAY (user_display)) {
                                /* Transferring the remote id from the new login screen display to the
                                 * preexisting user session display, notifies the remoting software to redirect
                                 * the connection. That same software will also ensure after the redirection has
                                 * happened that the login screen session is closed, so we don't have to manage that
                                 * ourselves.
                                 */
                                g_autofree char *remote_id = gdm_remote_display_get_remote_id (GDM_REMOTE_DISPLAY (login_display));
                                gdm_remote_display_set_remote_id (GDM_REMOTE_DISPLAY (user_display), remote_id);
                        } else {
                                g_warning ("GdmManager: Couldn't find remote display associated with reauthenticated user session");
                        }
                }

                if (gdm_session_get_display_mode (session) == GDM_SESSION_DISPLAY_MODE_REUSE_VT) {
                        gdm_display_stop_greeter_session (login_display);
                        gdm_display_unmanage (login_display);
                        gdm_display_finish (login_display);
                }
        }

        /* There should already be a session running, so jump to its
         * VT. In the event we're already on the right VT, (i.e. user
         * used an unlock screen instead of a user switched login screen),
         * then silently succeed and unlock the session.
         */
        switch_to_compatible_user_session (manager, session, fail_if_already_switched);
}

static void
on_stop_conflicting_session (GdmSession *login_session,
                             const char *username,
                             GdmManager *manager)
{
        const char *session_id;
        GdmSession *user_session;

        user_session = find_session_for_user (manager,
                                              username,
                                              NULL);
        if (user_session == NULL) {
                g_warning ("Couldn't find session for user");
                return;
        }

        if (are_sessions_compatible (login_session, user_session)) {
                g_warning ("Session requested to stop is compatible, it won't be stopped");
                return;
        }

        session_id = gdm_session_get_session_id (user_session);
        if (!gdm_terminate_session_by_id (manager->connection, NULL, session_id)) {
                g_warning ("Failed to terminate conflicting session");
                return;
        }
}

static void
on_session_client_ready_for_session_to_start (GdmSession      *session,
                                              const char      *service_name,
                                              gboolean         client_is_ready,
                                              GdmManager      *manager)
{
        gboolean waiting_to_start_user_session;

        if (client_is_ready) {
                g_debug ("GdmManager: Will start session when ready");
        } else {
                g_debug ("GdmManager: Will start session when ready and told");
        }

        waiting_to_start_user_session = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (session),
                                                                       "waiting-to-start"));

        g_object_set_data (G_OBJECT (session),
                           "start-when-ready",
                           GINT_TO_POINTER (client_is_ready));

        if (client_is_ready && waiting_to_start_user_session) {
                start_user_session_if_ready (manager, session, service_name);
        }
}

static void
on_session_client_connected (GdmSession      *session,
                             GCredentials    *credentials,
                             GPid             pid_of_client,
                             GdmManager      *manager)
{
        GdmDisplay *display;
        char    *username;
        int      delay;
        gboolean enabled;
        gboolean allow_timed_login = FALSE;

        g_debug ("GdmManager: client with pid %d connected", (int) pid_of_client);

        if (gdm_session_is_running (session)) {
                const char *session_username;
                session_username = gdm_session_get_username (session);
                g_debug ("GdmManager: ignoring connection, since session already running (for user %s)",
                         session_username);
                return;
        }

        display = get_display_for_user_session (session);

        if (display == NULL) {
                return;
        }

        if (!display_is_on_seat0 (display)) {
                return;
        }

#ifdef WITH_PLYMOUTH
        if (manager->plymouth_is_running) {
                plymouth_quit_with_transition ();
                manager->plymouth_is_running = FALSE;
        }
#endif

        g_object_get (G_OBJECT (display), "allow-timed-login", &allow_timed_login, NULL);

        if (!allow_timed_login) {
                return;
        }

        enabled = get_timed_login_details (manager, &username, &delay);

        if (! enabled) {
                return;
        }

        gdm_session_set_timed_login_details (session, username, delay);

        g_debug ("GdmManager: Starting automatic login conversation (for timed login)");
        gdm_session_start_conversation (session, "gdm-autologin");

        g_free (username);

}

static void
on_session_client_disconnected (GdmSession   *session,
                                GCredentials *credentials,
                                GPid          pid_of_client,
                                GdmManager   *manager)
{
        g_debug ("GdmManager: client with pid %d disconnected", (int) pid_of_client);
}

typedef struct
{
        GdmManager *manager;
        GdmSession *session;
        guint idle_id;
} ResetSessionOperation;

static void
destroy_reset_session_operation (ResetSessionOperation *operation)
{
        g_object_set_data (G_OBJECT (operation->session),
                           "reset-session-operation",
                           NULL);
        g_object_unref (operation->session);
        g_slice_free (ResetSessionOperation, operation);
}

static gboolean
on_reset_session (ResetSessionOperation *operation)
{
        gdm_session_reset (operation->session);

        destroy_reset_session_operation (operation);

        return G_SOURCE_REMOVE;
}

static void
queue_session_reset (GdmManager *manager,
                     GdmSession *session)
{
        ResetSessionOperation *operation;

        operation = g_object_get_data (G_OBJECT (session), "reset-session-operation");

        if (operation != NULL) {
                return;
        }

        operation = g_slice_new0 (ResetSessionOperation);
        operation->manager = manager;
        operation->session = g_object_ref (session);
        operation->idle_id = g_idle_add ((GSourceFunc) on_reset_session, operation);

        g_object_set_data (G_OBJECT (session), "reset-session-operation", operation);
}

static void
on_session_cancelled (GdmSession  *session,
                      GdmManager  *manager)
{
        g_debug ("GdmManager: Session was cancelled");
        queue_session_reset (manager, session);
}

static void
on_session_conversation_started (GdmSession *session,
                                 const char *service_name,
                                 GdmManager *manager)
{
        GdmDisplay *display;
        gboolean    enabled;
        char       *username;

        g_debug ("GdmManager: session conversation started for service %s on session", service_name);

        if (g_strcmp0 (service_name, "gdm-autologin") != 0) {
                g_debug ("GdmManager: ignoring session conversation since its not automatic login conversation");
                return;
        }

        display = get_display_for_user_session (session);

        if (display == NULL) {
                g_debug ("GdmManager: conversation has no associated display");
                return;
        }

        if (!display_is_on_seat0 (display)) {
                return;
        }

        enabled = get_automatic_login_details (manager, &username);

        if (! enabled) {
                return;
        }

        g_debug ("GdmManager: begin auto login for user '%s'", username);

        /* service_name will be "gdm-autologin"
         */
        gdm_session_setup_for_user (session, service_name, username);

        g_free (username);
}

static void
on_session_conversation_stopped (GdmSession *session,
                                 const char *service_name,
                                 GdmManager *manager)
{
        g_debug ("GdmManager: session conversation '%s' stopped", service_name);
}

static void
on_session_reauthentication_started (GdmSession *session,
                                     int         pid_of_caller,
                                     const char *address,
                                     GdmManager *manager)
{
        GDBusMethodInvocation *invocation;
        gpointer               source_tag;

        g_debug ("GdmManager: reauthentication started");

        source_tag = GINT_TO_POINTER (pid_of_caller);

        invocation = g_hash_table_lookup (manager->open_reauthentication_requests,
                                          source_tag);

        if (invocation != NULL) {
                g_hash_table_steal (manager->open_reauthentication_requests,
                                    source_tag);
                gdm_dbus_manager_complete_open_reauthentication_channel (GDM_DBUS_MANAGER (manager),
                                                                         invocation,
                                                                         address);
        }
}

static void
clean_user_session (GdmSession *session)
{
        g_object_set_data (G_OBJECT (session), "gdm-display", NULL);
        g_object_unref (session);
}

static void
create_user_session_for_display (GdmManager *manager,
                                 GdmDisplay *display,
                                 uid_t       allowed_user)
{
        GdmSession *session;
        gboolean    display_is_local = FALSE;
        char       *display_name = NULL;
        char       *display_device = NULL;
        char       *remote_hostname = NULL;
        char       *display_auth_file = NULL;
        char       *display_seat_id = NULL;
        char       *display_id = NULL;
        g_auto (GStrv) supported_session_types = NULL;

        g_object_get (G_OBJECT (display),
                      "id", &display_id,
                      "x11-display-name", &display_name,
                      "is-local", &display_is_local,
                      "remote-hostname", &remote_hostname,
                      "x11-authority-file", &display_auth_file,
                      "seat-id", &display_seat_id,
                      "supported-session-types", &supported_session_types,
                      NULL);
        display_device = get_display_device (manager, display);

        session = gdm_session_new (GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                   allowed_user,
                                   display_name,
                                   remote_hostname,
                                   display_device,
                                   display_seat_id,
                                   display_auth_file,
                                   display_is_local,
                                   NULL);

        if (GDM_IS_REMOTE_DISPLAY (display)) {
                g_autofree char *remote_id = gdm_remote_display_get_remote_id (GDM_REMOTE_DISPLAY (display));

                g_object_set (G_OBJECT (session), "remote-id", remote_id, NULL);
        }

        g_object_set (G_OBJECT (session),
                      "supported-session-types", supported_session_types,
                      NULL);

        g_debug ("GdmSession: Created user session for user %d on display %s (seat %s)",
                 (int) allowed_user,
                 display_id,
                 display_seat_id);

        g_free (display_name);
        g_free (remote_hostname);
        g_free (display_auth_file);
        g_free (display_seat_id);

        g_signal_connect (session,
                          "reauthentication-started",
                          G_CALLBACK (on_session_reauthentication_started),
                          manager);
        g_signal_connect (session,
                          "reauthenticated",
                          G_CALLBACK (on_session_reauthenticated),
                          manager);
        g_signal_connect (session,
                          "stop-conflicting-session",
                          G_CALLBACK (on_stop_conflicting_session),
                          manager);
        g_signal_connect (session,
                          "client-ready-for-session-to-start",
                          G_CALLBACK (on_session_client_ready_for_session_to_start),
                          manager);
        g_signal_connect (session,
                          "client-connected",
                          G_CALLBACK (on_session_client_connected),
                          manager);
        g_signal_connect (session,
                          "client-disconnected",
                          G_CALLBACK (on_session_client_disconnected),
                          manager);
        g_signal_connect (session,
                          "cancelled",
                          G_CALLBACK (on_session_cancelled),
                          manager);
        g_signal_connect (session,
                          "conversation-started",
                          G_CALLBACK (on_session_conversation_started),
                          manager);
        g_signal_connect (session,
                          "conversation-stopped",
                          G_CALLBACK (on_session_conversation_stopped),
                          manager);
        g_signal_connect (session,
                          "authentication-failed",
                          G_CALLBACK (on_session_authentication_failed),
                          manager);
        g_signal_connect (session,
                          "credentials-established",
                          G_CALLBACK (on_session_credentials_established),
                          manager);
        g_signal_connect (session,
                          "session-opened",
                          G_CALLBACK (on_user_session_opened),
                          manager);
        g_signal_connect (session,
                          "session-started",
                          G_CALLBACK (on_user_session_started),
                          manager);
        g_signal_connect (session,
                          "session-start-failed",
                          G_CALLBACK (on_session_start_failed),
                          manager);
        g_signal_connect (session,
                          "session-exited",
                          G_CALLBACK (on_user_session_exited),
                          manager);
        g_signal_connect (session,
                          "session-died",
                          G_CALLBACK (on_user_session_died),
                          manager);
        g_object_set_data (G_OBJECT (session), "gdm-display", display);
        g_object_set_data_full (G_OBJECT (display),
                                "gdm-user-session",
                                session,
                                (GDestroyNotify)
                                clean_user_session);
}

static void
on_display_added (GdmDisplayStore *display_store,
                  const char      *id,
                  GdmManager      *manager)
{
        GdmDisplay *display;

        display = gdm_display_store_lookup (display_store, id);

        if (display != NULL) {
                g_dbus_object_manager_server_export (manager->object_manager,
                                                     gdm_display_get_object_skeleton (display));

                g_signal_connect (display, "notify::status",
                                  G_CALLBACK (on_display_status_changed),
                                  manager);
                g_signal_emit (manager, signals[DISPLAY_ADDED], 0, id);
        }
}

GQuark
gdm_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_manager_error");
        }

        return ret;
}

static void
listify_display_ids (const char *id,
                     GdmDisplay *display,
                     GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
}

/*
  Example:
  dbus-send --system --dest=org.gnome.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/DisplayManager/Displays \
  org.freedesktop.ObjectManager.GetAll
*/
gboolean
gdm_manager_get_displays (GdmManager *manager,
                          GPtrArray **displays,
                          GError    **error)
{
        g_return_val_if_fail (GDM_IS_MANAGER (manager), FALSE);

        if (displays == NULL) {
                return FALSE;
        }

        *displays = g_ptr_array_new ();
        gdm_display_store_foreach (manager->display_store,
                                   (GdmDisplayStoreFunc)listify_display_ids,
                                   displays);

        return TRUE;
}

void
gdm_manager_stop (GdmManager *manager)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        g_debug ("GdmManager: GDM stopping");

        if (manager->local_factory != NULL) {
                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->local_factory));
        }

#ifdef HAVE_LIBXDMCP
        if (manager->xdmcp_factory != NULL) {
                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->xdmcp_factory));
        }
#endif

        manager->started = FALSE;
}

void
gdm_manager_start (GdmManager *manager)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        g_debug ("GdmManager: GDM starting to manage displays");

#ifdef WITH_PLYMOUTH
        manager->plymouth_is_running = plymouth_is_running ();

        if (manager->plymouth_is_running) {
                plymouth_prepare_for_transition ();
        }
#endif
        if (!manager->xdmcp_enabled || manager->show_local_greeter) {
                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->local_factory));
        }

        /* Accept remote connections */
        if (manager->remote_login_enabled) {
                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->remote_factory));

#ifdef WITH_PLYMOUTH
                /* Quit plymouth if remote is the only display */
                if (!manager->show_local_greeter && manager->plymouth_is_running) {
                        plymouth_quit_without_transition ();
                        manager->plymouth_is_running = FALSE;
                }
#endif
        }

#ifdef HAVE_LIBXDMCP
        /* Accept xdmcp connections */
        if (manager->xdmcp_enabled) {
#ifdef WITH_PLYMOUTH
                /* Quit plymouth if xdmcp is the only display */
                if (!manager->show_local_greeter && manager->plymouth_is_running) {
                        plymouth_quit_without_transition ();
                        manager->plymouth_is_running = FALSE;
                }
#endif
                if (manager->xdmcp_factory != NULL) {
                        g_debug ("GdmManager: Accepting XDMCP connections...");
                        gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->xdmcp_factory));
                }
        }
#endif

        manager->started = TRUE;
}

static gboolean
register_manager (GdmManager *manager)
{
        GError *error = NULL;
        GDBusObjectManagerServer *object_server;

        error = NULL;
        manager->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                                              NULL,
                                              &error);
        if (manager->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        object_server = g_dbus_object_manager_server_new (GDM_MANAGER_DISPLAYS_PATH);
        g_dbus_object_manager_server_set_connection (object_server, manager->connection);
        manager->object_manager = object_server;

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager),
                                               manager->connection,
                                               GDM_MANAGER_PATH,
                                               &error)) {
                g_critical ("error exporting interface to %s: %s",
                            GDM_MANAGER_PATH,
                            error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        return TRUE;
}

void
gdm_manager_set_xdmcp_enabled (GdmManager *manager,
                               gboolean    enabled)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        if (manager->xdmcp_enabled != enabled) {
                manager->xdmcp_enabled = enabled;
#ifdef HAVE_LIBXDMCP
                if (manager->xdmcp_enabled) {
                        manager->xdmcp_factory = gdm_xdmcp_display_factory_new (manager->display_store);
                        if (manager->started) {
                                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->xdmcp_factory));
                        }
                } else {
                        if (manager->started) {
                                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->xdmcp_factory));
                        }

                        g_object_unref (manager->xdmcp_factory);
                        manager->xdmcp_factory = NULL;
                }
#endif
        }

}

void
gdm_manager_set_show_local_greeter (GdmManager *manager,
                                    gboolean    show_local_greeter)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        manager->show_local_greeter = show_local_greeter;
}

void
gdm_manager_set_remote_login_enabled (GdmManager *manager,
                                      gboolean    enabled)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        if (manager->remote_login_enabled != enabled) {
                manager->remote_login_enabled = enabled;
                if (manager->remote_login_enabled) {
                        manager->remote_factory = gdm_remote_display_factory_new (manager->display_store);
                } else {
                        g_clear_object (&manager->remote_factory);
                }
        }
}

static void
gdm_manager_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        GdmManager *self;

        self = GDM_MANAGER (object);

        switch (prop_id) {
        case PROP_XDMCP_ENABLED:
                gdm_manager_set_xdmcp_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_SHOW_LOCAL_GREETER:
                gdm_manager_set_show_local_greeter (self, g_value_get_boolean (value));
                break;
        case PROP_REMOTE_LOGIN_ENABLED:
                gdm_manager_set_remote_login_enabled (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GdmManager *self;

        self = GDM_MANAGER (object);

        switch (prop_id) {
        case PROP_XDMCP_ENABLED:
                g_value_set_boolean (value, self->xdmcp_enabled);
                break;
        case PROP_SHOW_LOCAL_GREETER:
                g_value_set_boolean (value, self->show_local_greeter);
                break;
        case PROP_REMOTE_LOGIN_ENABLED:
                g_value_set_boolean (value, self->remote_login_enabled);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_manager_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GdmManager      *manager;

        manager = GDM_MANAGER (G_OBJECT_CLASS (gdm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));

        gdm_dbus_manager_set_version (GDM_DBUS_MANAGER (manager), PACKAGE_VERSION);

        manager->local_factory = gdm_local_display_factory_new (manager->display_store);

        if (manager->remote_login_enabled) {
                manager->remote_factory = gdm_remote_display_factory_new (manager->display_store);
        }

#ifdef HAVE_LIBXDMCP
        if (manager->xdmcp_enabled) {
                manager->xdmcp_factory = gdm_xdmcp_display_factory_new (manager->display_store);
        }
#endif

        return G_OBJECT (manager);
}

static void
gdm_manager_class_init (GdmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_manager_get_property;
        object_class->set_property = gdm_manager_set_property;
        object_class->constructor = gdm_manager_constructor;
        object_class->dispose = gdm_manager_dispose;

        signals [DISPLAY_ADDED] =
                g_signal_new ("display-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [DISPLAY_REMOVED] =
                g_signal_new ("display-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE,
                              1, G_TYPE_OBJECT);

        g_object_class_install_property (object_class,
                                         PROP_XDMCP_ENABLED,
                                         g_param_spec_boolean ("xdmcp-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_REMOTE_LOGIN_ENABLED,
                                         g_param_spec_boolean ("remote-login-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gdm_manager_init (GdmManager *manager)
{
        manager->display_store = gdm_display_store_new ();
        manager->user_sessions = NULL;
        manager->open_reauthentication_requests = g_hash_table_new_full (NULL,
                                                                         NULL,
                                                                         (GDestroyNotify)
                                                                         NULL,
                                                                         (GDestroyNotify)
                                                                         g_object_unref);
        manager->transient_sessions = g_hash_table_new_full (NULL,
                                                             NULL,
                                                             (GDestroyNotify)
                                                             NULL,
                                                             (GDestroyNotify)
                                                             g_object_unref);
        g_signal_connect (G_OBJECT (manager->display_store),
                          "display-added",
                          G_CALLBACK (on_display_added),
                          manager);

        g_signal_connect (G_OBJECT (manager->display_store),
                          "display-removed",
                          G_CALLBACK (on_display_removed),
                          manager);
}

static void
unexport_display (const char *id,
                  GdmDisplay *display,
                  GdmManager *manager)
{
        if (!g_dbus_connection_is_closed (manager->connection))
                g_dbus_object_manager_server_unexport (manager->object_manager, id);
}

static void
finish_display (const char *id,
                GdmDisplay *display,
                GdmManager *manager)
{
        gdm_display_stop_greeter_session (display);
        if (gdm_display_get_status (display) == GDM_DISPLAY_MANAGED)
                gdm_display_unmanage (display);
        gdm_display_finish (display);
}

static void
gdm_manager_dispose (GObject *object)
{
        GdmManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_MANAGER (object));

        manager = GDM_MANAGER (object);

        g_return_if_fail (manager != NULL);

        gdm_manager_stop (manager);

        g_clear_weak_pointer (&manager->automatic_login_display);

#ifdef HAVE_LIBXDMCP
        g_clear_object (&manager->xdmcp_factory);
#endif
        g_clear_object (&manager->local_factory);
        g_clear_object (&manager->remote_factory);
        g_clear_pointer (&manager->open_reauthentication_requests,
                         g_hash_table_unref);
        g_clear_pointer (&manager->transient_sessions,
                         g_hash_table_unref);

        g_list_foreach (manager->user_sessions,
                        (GFunc) gdm_session_close,
                        NULL);
        g_list_free_full (manager->user_sessions, (GDestroyNotify) g_object_unref);
        manager->user_sessions = NULL;

        g_signal_handlers_disconnect_by_func (G_OBJECT (manager->display_store),
                                              G_CALLBACK (on_display_added),
                                              manager);
        g_signal_handlers_disconnect_by_func (G_OBJECT (manager->display_store),
                                              G_CALLBACK (on_display_removed),
                                              manager);

        if (!g_dbus_connection_is_closed (manager->connection)) {
                gdm_display_store_foreach (manager->display_store,
                                           (GdmDisplayStoreFunc)unexport_display,
                                           manager);
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (manager));
        }

        gdm_display_store_foreach (manager->display_store,
                                   (GdmDisplayStoreFunc) finish_display,
                                   manager);

        gdm_display_store_clear (manager->display_store);

        g_dbus_object_manager_server_set_connection (manager->object_manager, NULL);

        g_clear_object (&manager->connection);
        g_clear_object (&manager->object_manager);
        g_clear_object (&manager->display_store);

        G_OBJECT_CLASS (gdm_manager_parent_class)->dispose (object);
}

GdmManager *
gdm_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GDM_TYPE_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GDM_MANAGER (manager_object);
}
