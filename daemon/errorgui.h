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

#ifndef GDM_ERRORGUI_H
#define GDM_ERRORGUI_H

#include "gdm.h"

/* this includes the actual gtk interface */
void		gdm_run_errorgui	(const char *error,
					 const char *dialog_type,
					 int screenx,
					 int screeny,
					 int screenwidth,
					 int screenheight);
/* this spawns self with the argument to run the above */
void		gdm_error_box		(GdmDisplay *d,
					 const char *dialog_type,
					 const char *error);

char *		gdm_run_failsafe_question(const char *question,
					  gboolean echo,
					  int screenx,
					  int screeny,
					  int screenwidth,
					  int screenheight);
/* this spawns self with the argument to run the above */
char *		gdm_failsafe_question	(GdmDisplay *d,
					 const char *question,
					 gboolean echo);

gboolean	gdm_run_failsafe_yesno	(const char *question,
					 int screenx,
					 int screeny,
					 int screenwidth,
					 int screenheight);
/* this spawns self with the argument to run the above */
gboolean	gdm_failsafe_yesno	(GdmDisplay *d,
					 const char *question);

#endif /* GDM_ERRORGUI_H */

/* EOF */

