/*
 *  GDM - THe GNOME Display Manager
 *  Copyright (C) 2001 Queen of England, (c)2002 George Lebl
 *    
 *  GDMcommunication routines
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *   
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#ifndef GDMCONFIG_H
#define GDMCONFIG_H

#include "ve-misc.h"
#include "ve-miscui.h"

void		gdm_openlog				(const char *ident,
							 int logopt,
							 int facility);

gchar *		gdm_config_get_string			(gchar *key);
gchar *		gdm_config_get_translated_string	(gchar *key);
gint		gdm_config_get_int     			(gchar *key);
gboolean	gdm_config_get_bool			(gchar *key);
gboolean	gdm_config_reload_string		(gchar *key);
gboolean	gdm_config_reload_int			(gchar *key);
gboolean	gdm_config_reload_bool			(gchar *key);

void		gdm_set_servauth			(gchar *file,
							 gchar *key,
							 gchar *id);
char *		gdm_get_theme_greeter			(gchar *file,
							 const char *fallback);
GSList *	gdm_config_get_xservers			(gboolean flexible);

#endif /* GDMCONFIG_H */
