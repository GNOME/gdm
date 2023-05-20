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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>

#include <glib/gi18n.h>

#include "common/gdm-common.h"

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
        ctx = g_option_context_new (_("— New GDM login"));
        g_option_context_set_translation_domain (ctx, GETTEXT_PACKAGE);
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_parse (ctx, &argc, &argv, NULL);
        g_option_context_free (ctx);


        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (EXIT_FAILURE);
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

        if (monte_carlo_pi) {
                calc_pi ();
                return 0;
        }

        if (use_xnest) {
                g_warning ("Not yet implemented");
                return 1;
        }

        error = NULL;
        res = gdm_goto_login_session (NULL, &error);
        if (! res) {
                g_printerr ("%s", error->message);
        } else {
                maybe_lock_screen ();
        }

        return 1;
}
