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

#ifndef _GDM_COMMON_H
#define _GDM_COMMON_H

#include <glib.h>
#include <glib/gstdio.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <locale.h>

#include "ve-signal.h"
#include "gdm-common-config.h"

G_BEGIN_DECLS
void           ve_clearenv (void);
char *	       ve_first_word (const char *s);
gboolean       ve_first_word_executable (const char *s,
				   gboolean only_existance);
char *         ve_rest (const char *s);

/* Gets the first existing command out of a list separated by semicolons */
char *	       ve_get_first_working_command (const char *list,
					     gboolean only_existance);

gboolean       ve_bool_equal (gboolean a, gboolean b);

gboolean       ve_is_string_in_list (const GList *list,
				     const char *string);
gboolean       ve_is_string_in_list_case_no_locale (const GList *list,
						    const char *string);

#define        ve_string_empty(x) ((x)==NULL||(x)[0]=='\0')
#define        ve_sure_string(x) ((x)!=NULL?(x):"")

/* Find a file using the specified list of directories, an absolute path is
 * just checked, whereas a relative path is search in the given directories,
 */
char *	       ve_find_file_simple		(const char *filename,
						 const GList *directories);

/* Find a file using the specified list of directories, an absolute path is
 * just checked, whereas a relative path is search in the given directories,
 * gnome_datadir, g_get_prgname() subdirectory, in GNOME_PATH
 * under /share/ and /share/<g_get_prgname()> */
char *	       ve_find_file			(const char *filename,
						 const GList *directories);

/* These two functions will ALWAYS return a non-NULL string,
 * if there is an error, they return the unconverted string */
char *         ve_locale_to_utf8 (const char *str);
char *         ve_locale_from_utf8 (const char *str);

/* These two functions will ALWAYS return a non-NULL string,
 * if there is an error, they return the unconverted string */
char *         ve_filename_to_utf8 (const char *str);
char *         ve_filename_from_utf8 (const char *str);

/* works with utf-8 strings and always returns something */
char *         ve_strftime (struct tm *the_tm, const char *format);

/* function which doesn't stop on signals */
pid_t          ve_waitpid_no_signal (pid_t pid, int *status, int options);

/* Testing for existance of a certain locale */
gboolean       ve_locale_exists (const char *loc);

char *         ve_find_prog_in_path (const char *prog, const char *path);
gboolean       ve_is_prog_in_path (const char *prog, const char *path);

char          *ve_shell_quote_filename (const char *name);

#define VE_IGNORE_EINTR(expr) \
	do {		\
		errno = 0;	\
		expr;		\
	} while G_UNLIKELY (errno == EINTR);

G_END_DECLS

#endif /* _GDM_COMMON_H */
