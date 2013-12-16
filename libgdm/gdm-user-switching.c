/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#include "common/gdm-common.h"
#include "gdm-user-switching.h"
#include "gdm-client.h"

#ifdef WITH_CONSOLE_KIT
#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"
#endif

static gboolean
create_transient_display (GDBusConnection *connection,
                          GCancellable    *cancellable,
                          GError         **error)
{
        GVariant *reply;
        const char     *value;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.gnome.DisplayManager",
                                             "/org/gnome/DisplayManager/LocalDisplayFactory",
                                             "org.gnome.DisplayManager.LocalDisplayFactory",
                                             "CreateTransientDisplay",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             cancellable, error);
        if (reply == NULL) {
                g_prefix_error (error, _("Unable to create transient display: "));
                return FALSE;
        }

        g_variant_get (reply, "(&o)", &value);
        g_debug ("Started %s", value);

        g_variant_unref (reply);
        return TRUE;
}

#ifdef WITH_CONSOLE_KIT

static gboolean
get_current_session_id (GDBusConnection  *connection,
                        char            **session_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             CK_MANAGER_PATH,
                                             CK_MANAGER_INTERFACE,
                                             "GetCurrentSession",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine session: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(o)", session_id);
        g_variant_unref (reply);

        return TRUE;
}

static gboolean
get_seat_id_for_session (GDBusConnection  *connection,
                         const char       *session_id,
                         char            **seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetSeatId",
                                             NULL, /* parameters */
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine seat: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(o)", seat_id);
        g_variant_unref (reply);

        return TRUE;
}

static char *
get_current_seat_id (GDBusConnection *connection)
{
        gboolean res;
        char    *session_id;
        char    *seat_id;

        session_id = NULL;
        seat_id = NULL;

        res = get_current_session_id (connection, &session_id);
        if (res) {
                res = get_seat_id_for_session (connection, session_id, &seat_id);
        }
        g_free (session_id);

        return seat_id;
}

static gboolean
activate_session_id_for_ck (GDBusConnection *connection,
                            GCancellable    *cancellable,
                            const char      *seat_id,
                            const char      *session_id,
                            GError         **error)
{
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "ActivateSession",
                                             g_variant_new ("(o)", session_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, error);
        if (reply == NULL) {
                g_prefix_error (error, _("Unable to activate session: "));
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

static gboolean
session_is_login_window (GDBusConnection *connection,
                         const char      *session_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char *value;
        gboolean ret;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetSessionType",
                                             NULL,
                                             G_VARIANT_TYPE ("(s)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine session type: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(&s)", &value);

        if (value == NULL || value[0] == '\0' || strcmp (value, "LoginWindow") != 0) {
                ret = FALSE;
        } else {
                ret = TRUE;
        }

        g_variant_unref (reply);

        return ret;
}

static gboolean
seat_can_activate_sessions (GDBusConnection *connection,
                            const char      *seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        gboolean ret;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "CanActivateSessions",
                                             NULL,
                                             G_VARIANT_TYPE ("(b)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine if can activate sessions: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(b)", &ret);
        g_variant_unref (reply);

        return ret;
}

static const char **
seat_get_sessions (GDBusConnection *connection,
                   const char      *seat_id)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char **value;

        reply = g_dbus_connection_call_sync (connection,
                                             CK_NAME,
                                             seat_id,
                                             CK_SEAT_INTERFACE,
                                             "GetSessions",
                                             NULL,
                                             G_VARIANT_TYPE ("(ao)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to list sessions: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(^ao)", &value);
        g_variant_unref (reply);

        return value;
}

static gboolean
get_login_window_session_id_for_ck (GDBusConnection  *connection,
                                    const char       *seat_id,
                                    char            **session_id)
{
        gboolean     can_activate_sessions;
        const char **sessions;
        int          i;

        *session_id = NULL;
        sessions = NULL;

        g_debug ("checking if seat can activate sessions");

        can_activate_sessions = seat_can_activate_sessions (connection, seat_id);
        if (! can_activate_sessions) {
                g_debug ("seat is unable to activate sessions");
                return FALSE;
        }

        sessions = seat_get_sessions (connection, seat_id);
        for (i = 0; sessions [i] != NULL; i++) {
                const char *ssid;

                ssid = sessions [i];

                if (session_is_login_window (connection, ssid)) {
                        *session_id = g_strdup (ssid);
                        break;
                }
        }
        g_free (sessions);

        return TRUE;
}

static gboolean
goto_login_session_for_ck (GDBusConnection  *connection,
                           GCancellable     *cancellable,
                           GError          **error)
{
        gboolean        ret;
        gboolean        res;
        char           *session_id;
        char           *seat_id;

        ret = FALSE;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        seat_id = get_current_seat_id (connection);
        if (seat_id == NULL || seat_id[0] == '\0') {
                g_debug ("seat id is not set; can't switch sessions");
                g_set_error (error, GDM_CLIENT_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = get_login_window_session_id_for_ck (connection, seat_id, &session_id);
        if (! res) {
                g_set_error (error, GDM_CLIENT_ERROR, 0, _("User unable to switch sessions."));
                return FALSE;
        }

        if (session_id != NULL) {
                res = activate_session_id_for_ck (connection, cancellable, seat_id, session_id, error);
                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "/org/freedesktop/ConsoleKit/Seat1") == 0) {
                res = create_transient_display (connection, cancellable, error);
                if (res) {
                        ret = TRUE;
                }
        }

        return ret;
}
#endif

#ifdef WITH_SYSTEMD

static gboolean
activate_session_id_for_systemd (GDBusConnection  *connection,
                                 GCancellable     *cancellable,
                                 const char       *seat_id,
                                 const char       *session_id,
                                 GError          **error)
{
        GVariant *reply;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "ActivateSessionOnSeat",
                                             g_variant_new ("(ss)", session_id, seat_id),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             cancellable, error);
        if (reply == NULL) {
                g_prefix_error (error, _("Unable to activate session: "));
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}

static gboolean
get_login_window_session_id_for_systemd (const char  *seat_id,
                                         char       **session_id)
{
        gboolean   ret;
        int        res, i;
        char     **sessions;
        char      *service_id;
        char      *service_class;
        char      *state;

        res = sd_seat_get_sessions (seat_id, &sessions, NULL, NULL);
        if (res < 0) {
                g_debug ("Failed to determine sessions: %s", strerror (-res));
                return FALSE;
        }

        if (sessions == NULL || sessions[0] == NULL) {
                *session_id = NULL;
                ret = TRUE;
                goto out;
        }

        for (i = 0; sessions[i]; i ++) {

                res = sd_session_get_class (sessions[i], &service_class);
                if (res < 0) {
                        g_debug ("failed to determine class of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_class, "greeter") != 0) {
                        free (service_class);
                        continue;
                }

                free (service_class);

                ret = sd_session_get_state (sessions[i], &state);
                if (ret < 0) {
                        g_debug ("failed to determine state of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (g_strcmp0 (state, "closing") == 0) {
                        free (state);
                        continue;
                }
                free (state);

                res = sd_session_get_service (sessions[i], &service_id);
                if (res < 0) {
                        g_debug ("failed to determine service of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_id, "gdm-launch-environment") == 0) {
                        *session_id = g_strdup (sessions[i]);
                        ret = TRUE;

                        free (service_id);
                        goto out;
                }

                free (service_id);
        }

        *session_id = NULL;
        ret = TRUE;

out:
        if (sessions) {
                for (i = 0; sessions[i]; i ++) {
                        free (sessions[i]);
                }

                free (sessions);
        }

        return ret;
}

static gboolean
goto_login_session_for_systemd (GDBusConnection  *connection,
                                GCancellable     *cancellable,
                                GError          **error)
{
        gboolean        ret;
        int             res;
        char           *our_session;
        char           *session_id;
        char           *seat_id;

        ret = FALSE;
        session_id = NULL;
        seat_id = NULL;

        /* First look for any existing LoginWindow sessions on the seat.
           If none are found, create a new one. */

        /* Note that we mostly use free () here, instead of g_free ()
         * since the data allocated is from libsystemd-logind, which
         * does not use GLib's g_malloc (). */

        res = sd_pid_get_session (0, &our_session);
        if (res < 0) {
                g_debug ("failed to determine own session: %s", strerror (-res));
                g_set_error (error, GDM_CLIENT_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = sd_session_get_seat (our_session, &seat_id);
        free (our_session);
        if (res < 0) {
                g_debug ("failed to determine own seat: %s", strerror (-res));
                g_set_error (error, GDM_CLIENT_ERROR, 0, _("Could not identify the current seat."));

                return FALSE;
        }

        res = sd_seat_can_multi_session (seat_id);
        if (res < 0) {
                free (seat_id);

                g_debug ("failed to determine whether seat can do multi session: %s", strerror (-res));
                g_set_error (error, GDM_CLIENT_ERROR, 0, _("The system is unable to determine whether to switch to an existing login screen or start up a new login screen."));

                return FALSE;
        }

        if (res == 0) {
                free (seat_id);

                g_set_error (error, GDM_CLIENT_ERROR, 0, _("The system is unable to start up a new login screen."));

                return FALSE;
        }

        res = get_login_window_session_id_for_systemd (seat_id, &session_id);
        if (res && session_id != NULL) {
                res = activate_session_id_for_systemd (connection, cancellable, seat_id, session_id, error);

                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "seat0") == 0) {
                res = create_transient_display (connection, cancellable, error);
                if (res) {
                        ret = TRUE;
                }
        }

        free (seat_id);
        g_free (session_id);

        return ret;
}
#endif

gboolean
gdm_goto_login_session_sync (GCancellable  *cancellable,
			     GError       **error)
{
        GDBusConnection *connection;
        gboolean retval;

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
        if (!connection)
                return FALSE;

#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                retval = goto_login_session_for_systemd (connection,
                                                         cancellable,
                                                         error);

                g_object_unref (connection);
                return retval;
        }
#endif

#ifdef WITH_CONSOLE_KIT
        retval = goto_login_session_for_ck (connection, cancellable, error);

        g_object_unref (connection);
        return retval;
#endif
}
