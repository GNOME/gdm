/* GDM - The Gnome Display Manager
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

#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ "N/A"
#endif

#include <sys/types.h>

#include "gdm.h"

void gdm_fail   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_info   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_error  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_debug  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

#define gdm_assert(expr)		G_STMT_START{			\
     if G_LIKELY(expr) { } else 					\
        gdm_fail ("GDM file %s: line %d (%s): assertion failed: (%s)",	\
		  __FILE__,						\
		  __LINE__,						\
		  __PRETTY_FUNCTION__,					\
                  #expr);			}G_STMT_END

#define gdm_assert_not_reached()	G_STMT_START{			\
     gdm_fail ("GDM file %s: line %d (%s): should not be reached",	\
	       __FILE__,						\
	       __LINE__,						\
	       __PRETTY_FUNCTION__);	}G_STMT_END

void gdm_fdprintf  (int fd, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
int gdm_fdgetc     (int fd);
char *gdm_fdgets   (int fd);

/* Note that these can actually clear environment without killing
 * the LD_* env vars if --preserve-ld-vars was passed to the
 * main daemon */
/* clear environment, but keep the i18n ones (LANG, LC_ALL, etc...),
 * note that this leak memory so only use before exec */
void gdm_clearenv_no_lang (void);
void gdm_clearenv (void);

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
void	gdm_wait_for_extra (int *status);

const GList * gdm_peek_local_address_list (void);
gboolean gdm_is_local_addr (struct in_addr *ia);
gboolean gdm_is_loopback_addr (struct in_addr *ia);

#ifdef ENABLE_IPV6
gboolean gdm_is_local_addr6 (struct in6_addr* ia);
gboolean gdm_is_loopback_addr6 (struct in6_addr *ia);
#endif

typedef struct {
	gboolean not_found; /* hostname below set to fallback,
			       as gethostbyaddr/name failed */
	char *hostname; /* never a bogus dot, if
			   invalid/unknown, then set to the
			   ip address in string form */
#ifdef ENABLE_IPV6
	struct sockaddr_storage *addrs;
#else
	struct in_addr *addrs; /* array */
#endif
	int addr_count;
} GdmHostent;

GdmHostent * gdm_gethostbyname (const char *name);
#ifdef ENABLE_IPV6
GdmHostent *gdm_gethostbyaddr (struct sockaddr_storage *ia);
#else
GdmHostent * gdm_gethostbyaddr (struct sockaddr_in *ia);
#endif
GdmHostent * gdm_hostent_copy (GdmHostent *he);
void gdm_hostent_free (GdmHostent *he);

gboolean gdm_setup_gids (const char *login, gid_t gid);

void gdm_desetuid (void);

gboolean gdm_test_opt (const char *cmd, const char *help, const char *option);

void gdm_close_all_descriptors (int from, int except, int except2);

int gdm_open_dev_null (mode_t mode);

void gdm_unset_signals (void);

char * gdm_locale_to_utf8 (const char *text);
char * gdm_locale_from_utf8 (const char *text);

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

#endif /* GDM_MISC_H */

/* EOF */
