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

#include <sys/types.h>

#include "gdm.h"

void gdm_fail   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_info   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_error  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_debug  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

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

gboolean gdm_setup_gids (const char *login, gid_t gid);

void gdm_desetuid (void);

gboolean gdm_test_opt (const char *cmd, const char *help, const char *option);

void gdm_close_all_descriptors (int from, int except);

gboolean gdm_is_a_no_password_user (const char *user);

int gdm_open_dev_null (mode_t mode);

void gdm_unset_signals (void);

char * gdm_locale_to_utf8 (const char *text);
char * gdm_locale_from_utf8 (const char *text);

void gdm_saveenv (void);
/* leaks */
void gdm_restoreenv (void);

#endif /* GDM_MISC_H */

/* EOF */
