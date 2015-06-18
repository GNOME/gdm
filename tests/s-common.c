/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andrew Ziem <ahz001@gmail.com>
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2015 Alexander Larsson <alexl@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <check.h>

#include "gdm-common.h"
#include "s-common.h"

static void
setup (void)
{
}

static void
teardown (void)
{
}

static char *
expand_fn (const char *var, gpointer data)
{
        if (strcmp (var, "FOO") == 0)
                return g_strdup ("BAR");
        if (strcmp (var, "FOO9") == 0)
                return g_strdup ("XXX");
        if (strcmp (var, "_FOO") == 0)
                return g_strdup ("YYY");
        if (strcmp (var, "FOO_FOO") == 0)
                return g_strdup ("ZZZ");
        return NULL;
}

static gboolean expands_to (const char *to_expand, const char *expanded)
{
        return strcmp (gdm_shell_expand (to_expand, expand_fn, NULL), expanded) == 0;
}

START_TEST (test_gdm_shell_expand)
{
        fail_unless (expands_to ("foo", "foo"));
        fail_unless (expands_to ("foo ", "foo "));
        fail_unless (expands_to ("foo#bar", "foo#bar"));
        fail_unless (expands_to ("foo #bar", "foo "));
        fail_unless (expands_to ("#bar", ""));
        fail_unless (expands_to ("foo #bar gazonk", "foo "));
        fail_unless (expands_to ("foo #bar gazonk", "foo "));
        fail_unless (expands_to ("foo #bar gazonk", "foo "));
        fail_unless (expands_to ("$FOO", "BAR"));
        fail_unless (expands_to ("$9FOO", "$9FOO"));
        fail_unless (expands_to ("$FOO9", "XXX"));
        fail_unless (expands_to ("${FOO}9", "BAR9"));
        fail_unless (expands_to ("$_FOO", "YYY"));
        fail_unless (expands_to ("$FOO_FOO", "ZZZ"));
        fail_unless (expands_to ("${FOO}", "BAR"));
        fail_unless (expands_to ("$FOO$FOO", "BARBAR"));
        fail_unless (expands_to ("${FOO}${FOO}", "BARBAR"));
        fail_unless (expands_to ("$FOO${FOO}", "BARBAR"));
        fail_unless (expands_to ("$foo", ""));
        fail_unless (expands_to ("$FOOBAR", ""));
        fail_unless (expands_to ("$FOO/BAR", "BAR/BAR"));
        fail_unless (expands_to ("${FOO}BAR", "BARBAR"));
        fail_unless (expands_to ("$/BAR", "$/BAR"));
        fail_unless (expands_to ("${FOO BAR}BAR", "${FOO BAR}BAR"));
        fail_unless (expands_to ("${}BAR", "${}BAR"));
        fail_unless (expands_to ("${$FOO}BAR", "${BAR}BAR"));
        fail_unless (expands_to ("\\$foo", "$foo"));
        fail_unless (expands_to ("a\\\\b", "a\\b"));
        fail_unless (expands_to ("a\\b", "a\\b"));
        fail_unless (expands_to ("a\\#b", "a#b"));
}
END_TEST

Suite *
suite_common (void)
{
        Suite *s;
        TCase *tc_core;

        s = suite_create ("gdm-common");
        tc_core = tcase_create ("core");

        tcase_add_checked_fixture (tc_core, setup, teardown);
        tcase_add_test (tc_core, test_gdm_shell_expand);
        suite_add_tcase (s, tc_core);

        return s;
}
