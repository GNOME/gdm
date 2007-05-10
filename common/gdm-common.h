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
#include <sys/socket.h>
#include <locale.h>
#include <netdb.h>

#include "ve-signal.h"
#include "gdm-common-config.h"
#include "gdm-config.h"

G_BEGIN_DECLS

#define        ve_string_empty(x) ((x)==NULL||(x)[0]=='\0')
#define        ve_sure_string(x) ((x)!=NULL?(x):"")
#define        VE_IGNORE_EINTR(expr) \
	do {			     \
		errno = 0;	     \
		expr;		     \
	} while G_UNLIKELY (errno == EINTR);

#define NEVER_FAILS_seteuid(uid) \
	{ int r = 0; \
	  if (geteuid () != uid) \
	    r = seteuid (uid); \
	  if G_UNLIKELY (r != 0) \
        g_error ("GDM file %s: line %d (%s): Cannot run seteuid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  G_GNUC_PRETTY_FUNCTION,					\
                  (int)uid,						\
		  strerror (errno));			}
#define NEVER_FAILS_setegid(gid) \
	{ int r = 0; \
	  if (getegid () != gid) \
	    r = setegid (gid); \
	  if G_UNLIKELY (r != 0) \
        g_error ("GDM file %s: line %d (%s): Cannot run setegid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  G_GNUC_PRETTY_FUNCTION,					\
                  (int)gid,						\
		  strerror (errno));			}

/* first goes to euid-root and then sets the egid and euid, to make sure
 * this succeeds */
#define NEVER_FAILS_root_set_euid_egid(uid,gid) \
	{ NEVER_FAILS_seteuid (0); \
	  NEVER_FAILS_setegid (gid); \
	  if (uid != 0) { NEVER_FAILS_seteuid (uid); } }


/* like fopen with "w" but unlinks and uses O_EXCL */
FILE *         gdm_safe_fopen_w  (const char *file,
				  mode_t      perm);
/* like fopen with "a+" and uses O_EXCL and O_NOFOLLOW */
FILE *         gdm_safe_fopen_ap (const char *file,
				  mode_t      perm);


typedef struct {
	gboolean not_found; /* hostname below set to fallback,
			       as gethostbyaddr/name failed */
	char *hostname; /* never a bogus dot, if
			   invalid/unknown, then set to the
			   ip address in string form */

	struct sockaddr_storage *addrs;
	int addr_count;
} GdmHostent;

GdmHostent * gdm_gethostbyname (const char *name);

GdmHostent *gdm_gethostbyaddr (struct sockaddr_storage *ia);
GdmHostent * gdm_hostent_copy (GdmHostent *he);
void gdm_hostent_free (GdmHostent *he);

/* This is for race free forks */
void	gdm_sigchld_block_push (void);
void	gdm_sigchld_block_pop (void);
void	gdm_sigterm_block_push (void);
void	gdm_sigterm_block_pop (void);
void	gdm_sigusr2_block_push (void);
void	gdm_sigusr2_block_pop (void);

void gdm_fdprintf  (int fd, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
int gdm_fdgetc     (int fd);
char *gdm_fdgets   (int fd);

void gdm_signal_ignore (int signal);
void gdm_signal_default (int signal);

void gdm_close_all_descriptors (int from, int except, int except2);

int gdm_open_dev_null (mode_t mode);

/* somewhat like g_build_filename, but does somet hing like
 * <dir> "/" <name> <extension>
 */
char *         gdm_make_filename (const char *dir,
				  const char *name,
				  const char *extension);

void           gdm_fd_set_close_on_exec  (int fd);

void           ve_clearenv (void);
char *	       ve_first_word (const char *s);

/* Gets the first existing command out of a list separated by semicolons */
char *	       ve_get_first_working_command (const char *list,
					     gboolean only_existance);

/* These two functions will ALWAYS return a non-NULL string,
 * if there is an error, they return the unconverted string */
char *         ve_locale_to_utf8 (const char *str);
char *         ve_locale_from_utf8 (const char *str);

/* These two functions will ALWAYS return a non-NULL string,
 * if there is an error, they return the unconverted string */
char *         ve_filename_to_utf8 (const char *str);
char *         ve_filename_from_utf8 (const char *str);

/* function which doesn't stop on signals */
pid_t          ve_waitpid_no_signal (pid_t pid, int *status, int options);

/* Testing for existance of a certain locale */
gboolean       ve_locale_exists (const char *loc);

G_END_DECLS

#endif /* _GDM_COMMON_H */
