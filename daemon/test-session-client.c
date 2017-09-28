/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <glib.h>

#include "gdm-manager-glue.h"
#include "gdm-session-glue.h"

static GMainLoop *loop;

static void
on_conversation_stopped (GdmDBusUserVerifier *user_verifier,
                         const char          *service_name)
{
        g_print ("\n** WARNING: conversation stopped\n");

        g_main_loop_quit (loop);
}

static void
on_reset (GdmDBusUserVerifier *user_verifier)
{
        g_print ("\n** NOTE: reset\n");

        g_main_loop_quit (loop);
}

static void
on_verification_complete (GdmDBusUserVerifier *user_verifier,
                          const char          *service_name)
{
        g_print ("\n** INFO: verification complete\n");

        g_main_loop_quit (loop);
}

static void
on_info_query (GdmDBusUserVerifier *user_verifier,
               const char          *service_name,
               const char          *query_text)
{
        char  answer[1024];
        char *res;

        g_print ("%s ", query_text);

        answer[0] = '\0';
        res = fgets (answer, sizeof (answer), stdin);
        if (res == NULL) {
                g_warning ("Couldn't get an answer");
        }

        answer[strlen (answer) - 1] = '\0';

        if (answer[0] == '\0') {
                gdm_dbus_user_verifier_call_cancel_sync (user_verifier,
                                                   NULL,
                                                   NULL);
                g_main_loop_quit (loop);
        } else {
                gdm_dbus_user_verifier_call_answer_query_sync (user_verifier,
                                                         service_name,
                                                         answer,
                                                         NULL,
                                                         NULL);
        }
}

static void
on_info (GdmDBusUserVerifier *user_verifier,
         const char          *service_name,
         const char          *info)
{
        g_print ("\n** NOTE: %s\n", info);
}

static void
on_problem (GdmDBusUserVerifier *user_verifier,
            const char           *service_name,
            const char           *problem)
{
        g_print ("\n** WARNING: %s\n", problem);
}

static void
on_secret_info_query (GdmDBusUserVerifier *user_verifier,
                      const char          *service_name,
                      const char          *query_text)
{
        char           answer[1024];
        char          *res;
        struct termios ts0;
        struct termios ts1;

        tcgetattr (fileno (stdin), &ts0);
        ts1 = ts0;
        ts1.c_lflag &= ~ECHO;

        g_print ("%s", query_text);

        if (tcsetattr (fileno (stdin), TCSAFLUSH, &ts1) != 0) {
                fprintf (stderr, "Could not set terminal attributes\n");
                exit (EXIT_FAILURE);
        }

        answer[0] = '\0';
        res = fgets (answer, sizeof (answer), stdin);
        answer[strlen (answer) - 1] = '\0';
        if (res == NULL) {
                g_warning ("Couldn't get an answer");
        }

        tcsetattr (fileno (stdin), TCSANOW, &ts0);

        g_print ("\n");

        gdm_dbus_user_verifier_call_answer_query_sync (user_verifier,
                                                 service_name,
                                                 answer,
                                                 NULL,
                                                 NULL);
}

int
main (int   argc,
      char *argv[])
{
        GError *error;
        GdmDBusManager *manager;
        GdmDBusUserVerifier *user_verifier;
        GDBusConnection *system_bus;
        GDBusConnection *connection;
        char *address;
        gboolean ok;

        g_debug ("creating instance of GdmDBusDisplay object...");

        error = NULL;
        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (system_bus == NULL) {
                g_critical ("Failed connecting to the system bus (this is pretty bad): %s", error->message);
                exit (EXIT_FAILURE);
        }

        manager = GDM_DBUS_MANAGER (gdm_dbus_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                             G_DBUS_PROXY_FLAGS_NONE,
                                                                             "org.gnome.DisplayManager",
                                                                             "/org/gnome/DisplayManager/Manager",
                                                                             NULL,
                                                                             &error));
        if (manager == NULL) {
                g_critical ("Failed creating display proxy: %s", error->message);
                exit (EXIT_FAILURE);
        }

        address = NULL;
        gdm_dbus_manager_call_open_reauthentication_channel_sync (manager,
                                                                  g_get_user_name (),
                                                                  &address,
                                                                  NULL,
                                                                  &error);
        if (address == NULL) {
                g_critical ("Failed opening reauthentication channel: %s", error->message);
                exit (EXIT_FAILURE);
        }

        connection = g_dbus_connection_new_for_address_sync (address,
                                                             G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                             NULL,
                                                             NULL,
                                                             &error);
        if (connection == NULL) {
                g_critical ("Failed connecting to the manager: %s", error->message);
                exit (EXIT_FAILURE);
        }

        user_verifier = GDM_DBUS_USER_VERIFIER (gdm_dbus_user_verifier_proxy_new_sync (connection,
                                                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                                                       NULL,
                                                                                       "/org/gnome/DisplayManager/Session",
                                                                                       NULL,
                                                                                       &error));
        if (user_verifier == NULL) {
                g_critical ("Failed creating user verifier proxy: %s", error->message);
                exit (EXIT_FAILURE);
        }

        g_signal_connect (user_verifier,
                          "info",
                          G_CALLBACK (on_info),
                          NULL);
        g_signal_connect (user_verifier,
                          "problem",
                          G_CALLBACK (on_problem),
                          NULL);
        g_signal_connect (user_verifier,
                          "info-query",
                          G_CALLBACK (on_info_query),
                          NULL);
        g_signal_connect (user_verifier,
                          "secret-info-query",
                          G_CALLBACK (on_secret_info_query),
                          NULL);
        g_signal_connect (user_verifier,
                          "conversation-stopped",
                          G_CALLBACK (on_conversation_stopped),
                          NULL);
        g_signal_connect (user_verifier,
                          "verification-complete",
                          G_CALLBACK (on_verification_complete),
                          NULL);
        g_signal_connect (user_verifier,
                          "reset",
                          G_CALLBACK (on_reset),
                          NULL);

        ok = gdm_dbus_user_verifier_call_begin_verification_for_user_sync (user_verifier,
                                                                           "gdm-password",
                                                                           g_get_user_name (),
                                                                           NULL,
                                                                           &error);
        if (!ok) {
                g_critical ("Failed to start PAM session: %s", error->message);
                exit (EXIT_FAILURE);
        }


        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        return 0;
}
