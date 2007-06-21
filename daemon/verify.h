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

#ifndef GDM_VERIFY_H
#define GDM_VERIFY_H

#include "gdm.h"
#include "display.h"

/* If username is NULL, we ask, if local is FALSE, don't start
 * the timed login timer */
gchar *gdm_verify_user			 (GdmDisplay *d,
					  const char *username,
					  gboolean allow_retry);
void   gdm_verify_cleanup		 (GdmDisplay *d);
void   gdm_verify_check			 (void);
void   gdm_verify_select_user		 (const char *user);

/* used in pam */
gboolean gdm_verify_setup_env  (GdmDisplay *d);
gboolean gdm_verify_setup_user (GdmDisplay *d,
				const gchar *login,
				char **new_login);

#endif /* GDM_VERIFY_H */

/* EOF */
