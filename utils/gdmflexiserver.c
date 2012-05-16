/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
#include <unistd.h>
#include <string.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#endif

#define GDM_DBUS_NAME                            "org.gnome.DisplayManager"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.LocalDisplayFactory"

#ifdef WITH_CONSOLE_KIT
#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"
#endif

static const char *send_command     = NULL;
static gboolean    use_xnest        = FALSE;
static gboolean    no_lock          = FALSE;
static gboolean    debug_in         = FALSE;
static gboolean    authenticate     = FALSE;
static gboolean    startnew         = FALSE;
static gboolean    monte_carlo_pi   = FALSE;
static gboolean    show_version     = FALSE;
static char      **args_remaining   = NULL;

/* Keep all config options for compatibility even if they are noops */
GOptionEntry options [] = {
        { "command", 'c', 0, G_OPTION_ARG_STRING, &send_command, N_("Only the VERSION command is supported"), N_("COMMAND") },
        { "xnest", 'n', 0, G_OPTION_ARG_NONE, &use_xnest, N_("Ignored — retained for compatibility"), NULL },
        { "no-lock", 'l', 0, G_OPTION_ARG_NONE, &no_lock, N_("Ignored — retained for compatibility"), NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug_in, N_("Debugging output"), NULL },
        { "authenticate", 'a', 0, G_OPTION_ARG_NONE, &authenticate, N_("Ignored — retained for compatibility"), NULL },
        { "startnew", 's', 0, G_OPTION_ARG_NONE, &startnew, N_("Ignored — retained for compatibility"), NULL },
        { "monte-carlo-pi", 0, 0, G_OPTION_ARG_NONE, &monte_carlo_pi, NULL, NULL },
        { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args_remaining, NULL, NULL },
        { NULL }
};

#define GDM_FLEXISERVER_ERROR gdm_flexiserver_error_quark ()
static GQuark
gdm_flexiserver_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_flexiserver_error");
        }

        return ret;
}

static gboolean
is_program_in_path (const char *program)
{
        char *tmp = g_find_program_in_path (program);
        if (tmp != NULL) {
                g_free (tmp);
                return TRUE;
        } else {
                return FALSE;
        }
}

static void
maybe_lock_screen (void)
{
        gboolean   use_gscreensaver = FALSE;
        GError    *error            = NULL;
        char      *command;

        if (is_program_in_path ("gnome-screensaver-command")) {
                use_gscreensaver = TRUE;
        } else if (! is_program_in_path ("xscreensaver-command")) {
                return;
        }

        if (use_gscreensaver) {
                command = g_strdup ("gnome-screensaver-command --lock");
        } else {
                command = g_strdup ("xscreensaver-command -lock");
        }

        if (! g_spawn_command_line_async (command, &error)) {
                g_warning ("Cannot lock screen: %s", error->message);
                g_error_free (error);
        }

        g_free (command);

        if (! use_gscreensaver) {
                command = g_strdup ("xscreensaver-command -throttle");
                if (! g_spawn_command_line_async (command, &error)) {
                        g_warning ("Cannot disable screensaver engines: %s", error->message);
                        g_error_free (error);
                }

                g_free (command);
        }
}

static void
calc_pi (void)
{
        unsigned long n = 0, h = 0;
        double x, y;
        printf ("\n");
        for (;;) {
                x = g_random_double ();
                y = g_random_double ();
                if (x*x + y*y <= 1)
                        h++;
                n++;
                if ( ! (n & 0xfff))
                        printf ("pi ~~ %1.10f\t(%lu/%lu * 4) iteration: %lu \r",
                                ((double)h)/(double)n * 4.0, h, n, n);
        }
}

static gboolean
create_transient_display (GDBusConnection *connection,
                          GError         **error)
{
        GError *local_error = NULL;
        GVariant *reply;
        const char     *value;

        reply = g_dbus_connection_call_sync (connection,
                                             GDM_DBUS_NAME,
                                             GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH,
                                             GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE,
                                             "CreateTransientDisplay",
                                             NULL, /* parameters */
                                             (const GVariantType *) "(o)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to create transient display: %s", local_error->message);
                g_propagate_error (error, local_error);
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
                                             (const GVariantType *) "(o)",
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
                                             (const GVariantType *) "(o)",
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
                            const char      *seat_id,
                            const char      *session_id)
{
        GError *local_error = NULL;
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
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to activate session: %s", local_error->message);
                g_error_free (local_error);
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
                                             (const GVariantType*) "(s)",
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
                                             (const GVariantType*) "(b)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to determine if can activate sessions: %s", local_error->message);
                g_error_free (local_error);
                return FALSE;
        }

        g_variant_get (reply, "(&b)", &ret);
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
                                             (const GVariantType*) "(ao)",
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
                g_set_error (error, GDM_FLEXISERVER_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = get_login_window_session_id_for_ck (connection, seat_id, &session_id);
        if (! res) {
                g_set_error (error, GDM_FLEXISERVER_ERROR, 1, _("User unable to switch sessions."));
                return FALSE;
        }

        if (session_id != NULL) {
                res = activate_session_id_for_ck (connection, seat_id, session_id);
                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "/org/freedesktop/ConsoleKit/Seat1") == 0) {
                res = create_transient_display (connection, error);
                if (res) {
                        ret = TRUE;
                }
        }

        return ret;
}
#endif

#ifdef WITH_SYSTEMD

static gboolean
activate_session_id_for_systemd (GDBusConnection *connection,
                                 const char      *seat_id,
                                 const char      *session_id)
{
        GError *local_error = NULL;
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
                                             NULL, &local_error);
        if (reply == NULL) {
                g_warning ("Unable to activate session: %s", local_error->message);
                g_error_free (local_error);
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

                res = sd_session_get_service (sessions[i], &service_id);
                if (res < 0) {
                        g_debug ("failed to determine service of session %s: %s", sessions[i], strerror (-res));
                        ret = FALSE;
                        goto out;
                }

                if (strcmp (service_id, "gdm-welcome") == 0) {
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
        for (i = 0; sessions[i]; i ++) {
                free (sessions[i]);
        }

        free (sessions);

        return ret;
}

static gboolean
goto_login_session_for_systemd (GDBusConnection  *connection,
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
                g_set_error (error, GDM_FLEXISERVER_ERROR, 0, _("Could not identify the current session."));

                return FALSE;
        }

        res = sd_session_get_seat (our_session, &seat_id);
        free (our_session);
        if (res < 0) {
                g_debug ("failed to determine own seat: %s", strerror (-res));
                g_set_error (error, GDM_FLEXISERVER_ERROR, 0, _("Could not identify the current seat."));

                return FALSE;
        }

        res = sd_seat_can_multi_session (seat_id);
        if (res < 0) {
                free (seat_id);

                g_debug ("failed to determine whether seat can do multi session: %s", strerror (-res));
                g_set_error (error, GDM_FLEXISERVER_ERROR, 0, _("The system is unable to determine whether to switch to an existing login screen or start up a new login screen."));

                return FALSE;
        }

        if (res == 0) {
                free (seat_id);

                g_set_error (error, GDM_FLEXISERVER_ERROR, 0, _("The system is unable to start up a new login screen."));

                return FALSE;
        }

        res = get_login_window_session_id_for_systemd (seat_id, &session_id);
        if (res && session_id != NULL) {
                res = activate_session_id_for_systemd (connection, seat_id, session_id);

                if (res) {
                        ret = TRUE;
                }
        }

        if (! ret && g_strcmp0 (seat_id, "seat0") == 0) {
                res = create_transient_display (connection, error);
                if (res) {
                        ret = TRUE;
                }
        }

        free (seat_id);
        g_free (session_id);

        return ret;
}
#endif

static gboolean
goto_login_session (GError **error)
{
        GError *local_error;
        GDBusConnection *connection;

        local_error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &local_error);
        if (connection == NULL) {
                g_debug ("Failed to connect to the D-Bus daemon: %s", local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

#ifdef WITH_SYSTEMD
        if (sd_booted () > 0) {
                return goto_login_session_for_systemd (connection, error);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return goto_login_session_for_ck (connection, error);
#endif
}

int
main (int argc, char *argv[])
{
        GOptionContext *ctx;
        gboolean        res;
        GError         *error;

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        /* Option parsing */
        ctx = g_option_context_new (_("- New GDM login"));
        g_option_context_set_translation_domain (ctx, GETTEXT_PACKAGE);
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_parse (ctx, &argc, &argv, NULL);
        g_option_context_free (ctx);


        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        /* don't support commands other than VERSION */
        if (send_command != NULL) {
                if (strcmp (send_command, "VERSION") == 0) {
                        g_print ("GDM  %s \n", VERSION);
                        return 0;
                } else {
                        g_warning ("No longer supported");
                }
                return 1;
        }

        gtk_init (&argc, &argv);

        if (monte_carlo_pi) {
                calc_pi ();
                return 0;
        }

        if (args_remaining != NULL && args_remaining[0] != NULL) {

        }

        if (use_xnest) {
                g_warning ("Not yet implemented");
                return 1;
        }

        error = NULL;
        res = goto_login_session (&error);
        if (! res) {
                GtkWidget *dialog;
                char      *message;

                if (error != NULL) {
                        message = g_strdup_printf ("%s", error->message);
                        g_error_free (error);
                } else {
                        message = g_strdup ("");
                }

                dialog = gtk_message_dialog_new (NULL,
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "%s", _("Unable to start new display"));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                          "%s", message);
                g_free (message);

                gtk_window_set_title (GTK_WINDOW (dialog), "");
                gtk_window_set_icon_name (GTK_WINDOW (dialog), "session-properties");
                gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
                gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 14);

                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
        } else {
                maybe_lock_screen ();
        }

        return 1;
}
