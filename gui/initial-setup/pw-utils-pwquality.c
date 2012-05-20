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

#include <pwquality.h>


static pwquality_settings_t *
get_pwq (void)
{
        static pwquality_settings_t *settings;

        if (settings == NULL) {
                gchar *err = NULL;
                settings = pwquality_default_settings ();
                if (pwquality_read_config (settings, NULL, (gpointer)&err) < 0) {
                        g_error ("failed to read pwquality configuration: %s\n", err);
                }
        }

        return settings;
}

gint
pw_min_length (void)
{
        gint value = 0;

        if (pwquality_get_int_value (get_pwq (), PWQ_SETTING_MIN_LENGTH, &value) < 0) {
                g_error ("Failed to read pwquality setting\n" );
        }

        return value;
}

gchar *
pw_generate (void)
{
        gchar *res;
        gint rv;

        rv = pwquality_generate (get_pwq (), 0, &res);

        if (rv < 0) {
                g_error ("Password generation failed: %s\n",
                         pwquality_strerror (NULL, 0, rv, NULL));
                return NULL;
        }

        return res;
}

gdouble
pw_strength (const gchar  *password,
             const gchar  *old_password,
             const gchar  *username,
             const gchar **hint,
             const gchar **long_hint)
{
        gint rv;
        gdouble strength;
        void *auxerror;

        rv = pwquality_check (get_pwq (),
                              password, old_password, username,
                              &auxerror);

        if (rv == PWQ_ERROR_MIN_LENGTH) {
                *hint = C_("Password strength", "Too short");
                *long_hint = pwquality_strerror (NULL, 0, rv, auxerror);
                return 0.0;
        }
        else if (rv < 0) {
                *hint = C_("Password strength", "Not good enough");
                *long_hint = pwquality_strerror (NULL, 0, rv, auxerror);
                return 0.0;
        }

        strength = CLAMP (0.01 * rv, 0.0, 1.0);

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
