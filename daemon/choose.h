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

#ifndef CHOOSE_H
#define CHOOSE_H

#include "gdm.h"

GdmIndirectDisplay *	gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa);
GdmIndirectDisplay *	gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa);
GdmIndirectDisplay *	gdm_choose_indirect_lookup_by_chosen (struct in_addr *chosen,
							      struct in_addr *origin);
void			gdm_choose_indirect_dispose (GdmIndirectDisplay *id);

/* dispose of indirect display of id, if no host is set */
void			gdm_choose_indirect_dispose_empty_id (guint id);

gboolean		gdm_choose_data (const char *data);

#endif /* CHOOSE_H */

/* EOF */

