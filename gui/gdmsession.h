/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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

#ifndef GDM_SESSION_H
#define GDM_SESSION_H

#include <vicious.h>
#include "misc.h"

typedef struct {
        char *name;
        char *comment;
} GdmSession;

gint		gdm_session_sort_func		(const char *a, const char *b);
const char *	gdm_session_name 		(const char *name);
void		gdm_session_list_from_hash_table_func (const char *key,
						const char *value,
						GList **sessions);


#endif /* GDM_SESSION_H */
