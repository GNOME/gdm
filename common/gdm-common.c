/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * (c) 2000 Eazel, Inc.
 * (c) 2001,2002 George Lebl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>

#ifdef HAVE_CRT_EXTERNS_H
#include <crt_externs.h>
#endif

#include "gdm-common.h"

/**
 * ve_clearenv:
 *
 * Description: Clears out the environment completely.
 * In case there is no native implementation of clearenv,
 * this could cause leaks depending on the implementation
 * of environment.
 *
 **/
void
ve_clearenv (void)
{
#ifdef HAVE_CLEARENV
	clearenv ();
#else

#ifdef HAVE__NSGETENVIRON
#define environ (*_NSGetEnviron())
#else
        extern char **environ;
#endif

	if (environ != NULL)
		environ[0] = NULL;
#endif
}

char *
ve_first_word (const char *s)
{
	int argc;
	char **argv;
	char *ret;

	if (s == NULL)
		return NULL;

	if ( ! g_shell_parse_argv (s, &argc, &argv, NULL)) {
		char *p;
		ret = g_strdup (s);
		p = strchr (ret, ' ');
		if (p != NULL)
			*p = '\0';
		return ret;
	}

	ret = g_strdup (argv[0]);

	g_strfreev (argv);

	return ret;
}

gboolean
ve_first_word_executable (const char *s, gboolean only_existance)
{
	char *bin = ve_first_word (s);
	if (bin == NULL)
		return FALSE;
	if (g_access (bin, only_existance ? F_OK : X_OK) == 0) {
		g_free (bin);
		return TRUE;
	} else {
		g_free (bin);
		return FALSE;
	}
}

char *
ve_get_first_working_command (const char *list,
			      gboolean only_existance)
{
	int i;
	char **vector;
	char *ret = NULL;

	if (list == NULL)
		return NULL;

	vector = g_strsplit (list, ";", -1);
	for (i = 0; vector[i] != NULL; i++) {
		if (ve_first_word_executable (vector[i],
					      only_existance)) {
			ret = g_strdup (vector[i]);
			break;
		}
	}
	g_strfreev (vector);
	return ret;
}

char *
ve_locale_to_utf8 (const char *str)
{
	char *ret = g_locale_to_utf8 (str, -1, NULL, NULL, NULL);

	if (ret == NULL) {
		g_warning ("string not in proper locale encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_locale_from_utf8 (const char *str)
{
	char *ret = g_locale_from_utf8 (str, -1, NULL, NULL, NULL);

	if (ret == NULL) {
		g_warning ("string not in proper utf8 encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_filename_to_utf8 (const char *str)
{
	char *ret = g_filename_to_utf8 (str, -1, NULL, NULL, NULL);
	if (ret == NULL) {
		g_warning ("string not in proper locale encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

char *
ve_filename_from_utf8 (const char *str)
{
	char *ret = g_filename_from_utf8 (str, -1, NULL, NULL, NULL);
	if (ret == NULL) {
		g_warning ("string not in proper utf8 encoding: \"%s\"", str);
		return g_strdup (str);
	} else {
		return ret;
	}
}

pid_t
ve_waitpid_no_signal (pid_t pid, int *status, int options)
{
	pid_t ret;

	for (;;) {
		ret = waitpid (pid, status, options);
		if (ret == 0)
			return 0;
		if (errno != EINTR)
			return ret;
	}
}

gboolean
ve_locale_exists (const char *loc)
{
	gboolean ret;
	char *old = g_strdup (setlocale (LC_MESSAGES, NULL));
	if (setlocale (LC_MESSAGES, loc) != NULL)
		ret = TRUE;
	else
		ret = FALSE;
	setlocale (LC_MESSAGES, old);
	g_free (old);
	return ret;
}
