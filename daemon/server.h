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

#ifndef GDM_SERVER_H
#define GDM_SERVER_H

#include "gdm.h"

/* Wipe cookie files */
void		gdm_server_wipe_cookies	(GdmDisplay *disp);

/* Wipe X server's butt */
void		gdm_server_whack_lockfile (GdmDisplay *disp);

gboolean	gdm_server_start	(GdmDisplay *d,
					 gboolean try_again_if_busy,
					 gboolean treat_as_flexi,
					 int min_flexi_disp,
					 int flexi_retries);
void		gdm_server_stop		(GdmDisplay *d);
gboolean	gdm_server_reinit	(GdmDisplay *d);
GdmDisplay *	gdm_server_alloc	(gint id,
					 const gchar *command);
void		gdm_server_whack_clients (GdmDisplay *disp);
void		gdm_server_checklog	(GdmDisplay *disp);

char **		gdm_server_resolve_command_line (GdmDisplay *disp,
						 gboolean resolve_flags,
						 const char *vtarg);
GdmXServer *	gdm_server_resolve	(GdmDisplay *disp);



#endif /* GDM_SERVER_H */

/* EOF */
