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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define GDM_DBUS_NAME                            "org.gnome.DisplayManager"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH      "/org/gnome/DisplayManager/LocalDisplayFactory"
#define GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE "org.gnome.DisplayManager.LocalDisplayFactory"

static const char *send_command     = NULL;
static gboolean    use_xnest        = FALSE;
static gboolean    no_lock          = FALSE;
static gboolean    debug_in         = FALSE;
static gboolean    authenticate     = FALSE;
static gboolean    startnew         = FALSE;
static gboolean    monte_carlo_pi   = FALSE;
static char      **args_remaining   = NULL;

/* Keep all config options for compatibility even if they are noops */
GOptionEntry options [] = {
        { "command", 'c', 0, G_OPTION_ARG_STRING, &send_command, N_("Send the specified protocol command to GDM"), N_("COMMAND") },
        { "xnest", 'n', 0, G_OPTION_ARG_NONE, &use_xnest, N_("Xnest mode"), NULL },
        { "no-lock", 'l', 0, G_OPTION_ARG_NONE, &no_lock, N_("Do not lock current screen"), NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug_in, N_("Debugging output"), NULL },
        { "authenticate", 'a', 0, G_OPTION_ARG_NONE, &authenticate, N_("Authenticate before running --command"), NULL },
        { "startnew", 's', 0, G_OPTION_ARG_NONE, &startnew, N_("Start new flexible session; do not show popup"), NULL },
        { "monte-carlo-pi", 0, 0, G_OPTION_ARG_NONE, &monte_carlo_pi, NULL, NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args_remaining, NULL, NULL },
        { NULL }
};

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
        GdkScreen *screen;

        if (is_program_in_path ("gnome-screensaver-command"))
                use_gscreensaver = TRUE;
        else if (! is_program_in_path ("xscreensaver-command"))
                return;

        if (use_gscreensaver) {
                command = g_strdup ("gnome-screensaver-command --lock");
        } else {
                command = g_strdup ("xscreensaver-command -lock");
        }

        screen = gdk_screen_get_default ();

        if (! gdk_spawn_command_line_on_screen (screen, command, &error)) {
                g_warning ("Cannot lock screen: %s", error->message);
                g_error_free (error);
        }

        g_free (command);

        if (! use_gscreensaver) {
                command = g_strdup ("xscreensaver-command -throttle");
                if (! gdk_spawn_command_line_on_screen (screen, command, &error)) {
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
create_transient_display (GError **error)
{
        DBusGProxy      *proxy;
        DBusGConnection *connection;
        GError          *local_error;
        char            *display_id;
        gboolean         res;
        gboolean         ret;

        ret = FALSE;

        local_error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &local_error);
        if (connection == NULL) {
                if (local_error != NULL) {
                        g_critical ("error getting system bus: %s", local_error->message);
                        g_error_free (local_error);
                }
                exit (1);
        }

        proxy = dbus_g_proxy_new_for_name (connection,
                                           GDM_DBUS_NAME,
                                           GDM_DBUS_LOCAL_DISPLAY_FACTORY_PATH,
                                           GDM_DBUS_LOCAL_DISPLAY_FACTORY_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to create local display factory proxy");
                goto out;
        }

        local_error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "CreateTransientDisplay",
                                 &local_error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &display_id,
                                 G_TYPE_INVALID);
        if (res) {
                g_print ("Started %s\n", display_id);
        }
        g_free (display_id);
        if (! res) {
                if (local_error != NULL) {
                        g_warning ("Failed to create transient display: %s", local_error->message);
                        g_error_free (local_error);
                } else {
                        g_warning ("Failed to create transient display");
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
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

        /* Option parsing */
        ctx = g_option_context_new ("- New GDM login");
        g_option_context_add_main_entries (ctx, options, _("Main Options"));
        g_option_context_parse (ctx, &argc, &argv, NULL);
        g_option_context_free (ctx);

        /* don't support commands */
        if (send_command != NULL) {
                g_warning ("No longer supported");
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
        res = create_transient_display (&error);
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
                                                 GTK_BUTTONS_OK,
                                                 "%s", _("Unable to start new display"));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                          "%s", message);
                g_free (message);

                gtk_window_set_title (GTK_WINDOW (dialog), "");
                gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
                gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 14);

                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
        } else {
                maybe_lock_screen ();
        }

        return 1;
}
