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

pid_t	gdm_fork_extra (void);
void	gdm_wait_for_extra (pid_t pid, int *status);

gboolean gdm_setup_gids (const char *login, gid_t gid);

void gdm_desetuid (void);

gboolean gdm_test_opt (const char *cmd, const char *help, const char *option);

void gdm_unset_signals (void);

void gdm_saveenv (void);
const char * gdm_saved_getenv (const char *var);
/* leaks */
void gdm_restoreenv (void);

/* first must get initial limits before attempting to ever reset those
   limits */
void gdm_get_initial_limits (void);
void gdm_reset_limits (void);
void gdm_reset_locale (void);

const char *gdm_root_user (void);

void gdm_sleep_no_signal (int secs);

char * gdm_ensure_extension (const char *name, const char *extension);
char * gdm_strip_extension (const char *name, const char *extension);

void gdm_twiddle_pointer (GdmDisplay *disp);

char * gdm_get_last_info (const char *username);

gboolean gdm_ok_console_language (void);
const char * gdm_console_translate (const char *str);
/* Use with C_(N_("foo")) to make gettext work it out right */
#define C_(x) (gdm_console_translate(x))

gchar * gdm_read_default (gchar *key);

#endif /* GDM_MISC_H */
