/*
 *    GDMcommunication routines
 *    (c)2001 Queen of England, (c)2002 George Lebl
 *    
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *   
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *   
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#ifndef GDMCOMM_H
#define GDMCOMM_H

void		gdmcomm_set_debug (gboolean enable);
char *		gdmcomm_call_gdm (const char *command,
				  const char *auth_cookie,
				  const char *min_version,
				  int tries);
const char *	gdmcomm_get_display (void);
/* This just gets a cookie of MIT-MAGIC-COOKIE-1 type */
char *		gdmcomm_get_a_cookie (gboolean binary);
/* get the gdm auth cookie */
char *		gdmcomm_get_auth_cookie (void);

gboolean	gdmcomm_check (gboolean gui_bitching);
const char *	gdmcomm_get_error_message (const char *ret,
					   gboolean use_xnest);

#endif /* GDMCOMM_H */
