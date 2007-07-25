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

#include <glib.h>
#include "gdm-address.h"

G_BEGIN_DECLS

gboolean gdm_auth_add_entry_for_display (int         display_num,
                                         GdmAddress *address,
                                         GString    *cookie,
                                         FILE       *af,
                                         GSList    **authlist);

gboolean gdm_auth_add_entry             (int         display_num,
                                         GdmAddress *address,
                                         GString    *binary_cookie,
                                         FILE       *af,
                                         GSList    **authlist);

gboolean gdm_auth_user_add              (int         display_num,
                                         GdmAddress *address,
                                         const char *cookie,
                                         const char *username,
                                         char      **filenamep);

void     gdm_auth_free_auth_list (GSList     *list);

G_END_DECLS

#endif /* GDM_AUTH_H */
