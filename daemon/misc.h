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

#include "config.h"
#include "gdm.h"

void gdm_fail   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_info   (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_error  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
void gdm_debug  (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

/* clear environment, but keep the i18n ones (LANG, LC_ALL, etc...),
 * note that this leak memory so only use before exec */
void gdm_clearenv_no_lang (void);

int gdm_get_free_display (int start, uid_t server_uid);

gboolean gdm_text_message_dialog (const char *msg);
gboolean gdm_text_yesno_dialog (const char *msg, gboolean *ret);
int	gdm_exec_wait (char * const *argv, gboolean no_display);

/* This is a race free fork, that is the pid will
 * be set before sigchld is sent.  This is achieved by
 * blocking sigchld for the moment */
void	gdm_safe_fork (pid_t *pid);

#endif /* GDM_MISC_H */

/* EOF */
