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

#ifndef GDM_AUTH_H
#define GDM_AUTH_H

#include "gdm-display.h"

G_BEGIN_DECLS

gboolean gdm_auth_add_entry_for_display (int            display_num,
                                         GString       *cookie,
                                         GSList       **authlist,
                                         FILE          *af);
gboolean gdm_auth_add_entry (int            display_num,
                             GString       *binary_cookie,
                             GSList       **authlist,
                             FILE          *af,
                             unsigned short family,
                             const char    *addr,
                             int            addrlen);

gboolean gdm_auth_user_add       (GdmDisplay *d,
                                  uid_t       user,
                                  const char *homedir);
void     gdm_auth_user_remove    (GdmDisplay *d,
                                  uid_t       user);

/* Call XSetAuthorization */
void	 gdm_auth_set_local_auth (GdmDisplay *d);

void     gdm_auth_free_auth_list (GSList     *list);

G_END_DECLS

#endif /* GDM_AUTH_H */
