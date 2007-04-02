/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef GDM_MISC_H
#define GDM_MISC_H

#include <stdio.h>
#include <sys/types.h>

#include "gdm.h"
#include "display.h"

void gdm_fdprintf  (int fd, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
int gdm_fdgetc     (int fd);
char *gdm_fdgets   (int fd);

/* clear environment, but keep the i18n ones (LANG, LC_ALL, etc...),
 * note that this leak memory so only use before exec */
void gdm_clearenv_no_lang (void);

int gdm_get_free_display (int start, uid_t server_uid);

gboolean gdm_text_message_dialog (const char *msg);
gboolean gdm_text_yesno_dialog (const char *msg, gboolean *ret);
int	gdm_exec_wait (char * const *argv, gboolean no_display,
		       gboolean de_setuid);

/* done before each login.  This can do so sanity ensuring,
 * one of the things it does now is make sure /tmp/.ICE-unix
 * exists and has the correct permissions */
void	gdm_ensure_sanity	(void);

/* This is for race free forks */
void	gdm_sigchld_block_push (void);
void	gdm_sigchld_block_pop (void);
void	gdm_sigterm_block_push (void);
void	gdm_sigterm_block_pop (void);
void	gdm_sigusr2_block_push (void);
void	gdm_sigusr2_block_pop (void);

pid_t	gdm_fork_extra (void);
void	gdm_wait_for_extra (pid_t pid, int *status);

const GList * gdm_address_peek_local_list (void);
gboolean      gdm_address_is_local        (struct sockaddr_storage *sa);

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

gboolean gdm_setup_gids (const char *login, gid_t gid);

void gdm_desetuid (void);

gboolean gdm_test_opt (const char *cmd, const char *help, const char *option);

void gdm_close_all_descriptors (int from, int except, int except2);

int gdm_open_dev_null (mode_t mode);

void gdm_unset_signals (void);

void gdm_saveenv (void);
const char * gdm_saved_getenv (const char *var);
/* leaks */
void gdm_restoreenv (void);

/* like fopen with "w" but unlinks and uses O_EXCL */
FILE * gdm_safe_fopen_w (const char *file);
/* like fopen with "a+" and uses O_EXCL and O_NOFOLLOW */
FILE * gdm_safe_fopen_ap (const char *file);

/* first must get initial limits before attempting to ever reset those
   limits */
void gdm_get_initial_limits (void);
void gdm_reset_limits (void);
void gdm_reset_locale (void);

const char *gdm_root_user (void);

#include <setjmp.h>

/* stolen from xdm sources */
#if defined(X_NOT_POSIX) || defined(__EMX__) || defined(__NetBSD__) && defined(__sparc__)
#define Setjmp(e)	setjmp(e)
#define Longjmp(e,v)	longjmp(e,v)
#define Jmp_buf		jmp_buf
#else
#define Setjmp(e)   sigsetjmp(e,1)
#define Longjmp(e,v)	siglongjmp(e,v)
#define Jmp_buf		sigjmp_buf
#endif

void gdm_signal_ignore (int signal);
void gdm_signal_default (int signal);

void gdm_sleep_no_signal (int secs);

/* somewhat like g_build_filename, but does somet hing like
 * <dir> "/" <name> <extension>
 */
char * gdm_make_filename (const char *dir, const char *name, const char *extension);
char * gdm_ensure_extension (const char *name, const char *extension);
char * gdm_strip_extension (const char *name, const char *extension);

void gdm_twiddle_pointer (GdmDisplay *disp);

char * gdm_get_last_info (const char *username);

gboolean gdm_ok_console_language (void);
const char * gdm_console_translate (const char *str);
/* Use with C_(N_("foo")) to make gettext work it out right */
#define C_(x) (gdm_console_translate(x))

gchar * gdm_read_default (gchar *key);

#define NEVER_FAILS_seteuid(uid) \
	{ int r = 0; \
	  if (geteuid () != uid) \
	    r = seteuid (uid); \
	  if G_UNLIKELY (r != 0) \
        gdm_fail ("GDM file %s: line %d (%s): Cannot run seteuid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  __PRETTY_FUNCTION__,					\
                  (int)uid,						\
		  strerror (errno));			}
#define NEVER_FAILS_setegid(gid) \
	{ int r = 0; \
	  if (getegid () != gid) \
	    r = setegid (gid); \
	  if G_UNLIKELY (r != 0) \
        gdm_fail ("GDM file %s: line %d (%s): Cannot run setegid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  __PRETTY_FUNCTION__,					\
                  (int)gid,						\
		  strerror (errno));			}

/* first goes to euid-root and then sets the egid and euid, to make sure
 * this succeeds */
#define NEVER_FAILS_root_set_euid_egid(uid,gid) \
	{ NEVER_FAILS_seteuid (0); \
	  NEVER_FAILS_setegid (gid); \
	  if (uid != 0) { NEVER_FAILS_seteuid (uid); } }

#endif /* GDM_MISC_H */
