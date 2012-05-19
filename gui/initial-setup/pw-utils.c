/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2012  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 * Written by: Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include "pw-utils.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <sys/types.h>
#include <sys/wait.h>


#define MIN_PASSWORD_LEN 6

gint
pw_min_length (void)
{
        return MIN_PASSWORD_LEN;
}

gchar *
pw_generate (void)
{
        static gchar **generated = NULL;
        static gint next;

        gint min_len, max_len;
        gchar *output, *err, *cmdline, *p;
        gint status;
        GError *error;
        gchar *ret;

        if (generated && generated[next]) {
                return g_strdup (generated[next++]);
        }

        g_strfreev (generated);
        generated = NULL;
        next = 0;

        ret = NULL;

        min_len = 6;
        max_len = 12;
        cmdline = g_strdup_printf ("apg -n 10 -M SNC -m %d -x %d", min_len, max_len);
        error = NULL;
        output = NULL;
        err = NULL;
        if (!g_spawn_command_line_sync (cmdline, &output, &err, &status, &error)) {
                g_warning ("Failed to run apg: %s", error->message);
                g_error_free (error);
        } else if (WEXITSTATUS (status) == 0) {
                p = output;
                if (*p == '\n')
                        p++;
                if (p[strlen(p) - 1] == '\n')
                        p[strlen(p) - 1] = '\0';
                generated = g_strsplit (p, "\n", -1);
                next = 0;

                ret = g_strdup (generated[next++]);
        } else {
                g_warning ("agp returned an error: %s", err);
        }

        g_free (cmdline);
        g_free (output);
        g_free (err);

        return ret;
}

/* This code is based on the Master Password dialog in Firefox
 * (pref-masterpass.js)
 * Original code triple-licensed under the MPL, GPL, and LGPL
 * so is license-compatible with this file
 */
gdouble
pw_strength (const gchar  *password,
             const gchar  *old_password,
             const gchar  *username,
             const gchar **hint,
             const gchar **long_hint)
{
        gint length;
        gint upper, lower, digit, misc;
        gint i;
        gdouble strength;

        length = strlen (password);
        upper = 0;
        lower = 0;
        digit = 0;
        misc = 0;

        if (length < MIN_PASSWORD_LEN) {
                *hint = C_("Password strength", "Too short");
                return 0.0;
        }

        for (i = 0; i < length ; i++) {
                if (g_ascii_isdigit (password[i]))
                        digit++;
                else if (g_ascii_islower (password[i]))
                        lower++;
                else if (g_ascii_isupper (password[i]))
                        upper++;
                else
                        misc++;
        }

        if (length > 5)
                length = 5;

        if (digit > 3)
                digit = 3;

        if (upper > 3)
                upper = 3;

        if (misc > 3)
                misc = 3;

        strength = ((length * 0.1) - 0.2) +
                    (digit * 0.1) +
                    (misc * 0.15) +
                    (upper * 0.1);

        strength = CLAMP (strength, 0.0, 1.0);

        if (strength < 0.50)
                *hint = C_("Password strength", "Weak");
        else if (strength < 0.75)
                *hint = C_("Password strength", "Fair");
        else if (strength < 0.90)
                *hint = C_("Password strength", "Good");
        else
                *hint = C_("Password strength", "Strong");

        *long_hint = NULL;

        return strength;
}
