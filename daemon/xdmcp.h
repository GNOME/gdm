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

#ifndef GDM_XDMCP_H
#define GDM_XDMCP_H

#ifdef HAVE_LIBXDMCP
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xauth.h>
#include <X11/Xdmcp.h>
#include "gdm.h"
#endif /* HAVE_LIBXDMCP */

/* Note that these are defined as empty stubs if there is no XDMCP support */
gboolean	gdm_xdmcp_init	(void);
void		gdm_xdmcp_run	(void);
void		gdm_xdmcp_close	(void);

#ifdef HAVE_LIBXDMCP
/* Fix broken X includes */
int XdmcpReallocARRAY8 (ARRAY8Ptr array, int length);
#endif /* HAVE_LIBXDMCP */

#endif /* GDM_XDMCP_H */

/* EOF */
