/* GDM - The Gnome Display Manager
 * Copyright (C) 2002 Queen of England
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

#ifndef GETVT_H
#define GETVT_H

/* gets an argument we should pass to the X server, on
 * linux for example we get the first empty vt (higher
 * then or equal to GdmFirstVT) and then return vt<number>
 * (e.g. "vt7") as a newly allocated string.
 * Can return NULL if we can't figure out what to do
 * or if GdmVTAllocation is false. */
/* fd is opened so that we are saying we have opened this
 * vt.  This should be closed after the server has started.
 * This is to avoid race with other stuff openning this vt.
 * It can be set to -1 if nothing could be opened. */
char * gdm_get_empty_vt_argument (int *fd, int *vt);

#endif /* GETVT_H */
