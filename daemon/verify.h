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

#ifndef GDM_VERIFY_H
#define GDM_VERIFY_H

#include "gdm.h"

gchar *gdm_verify_user    (const gchar *display);
void   gdm_verify_cleanup (void);
void   gdm_verify_check   (void);
/* used in pam */
void   gdm_verify_env_setup (void);
void   gdm_verify_setup_user (const gchar *login, const gchar *display) ;

#endif /* GDM_VERIFY_H */

/* EOF */
