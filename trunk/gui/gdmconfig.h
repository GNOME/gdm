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

#ifndef _GDMCONFIG_H
#define _GDMCONFIG_H

#include "glib.h"

void		gdm_config_never_cache			(gboolean never_cache);
void		gdm_config_set_comm_retries		(int tries);
gchar *		gdm_config_get_string			(const gchar *key);
gchar *		gdm_config_get_translated_string	(const gchar *key);
gint		gdm_config_get_int     			(const gchar *key);
gboolean	gdm_config_get_bool			(const gchar *key);
gboolean	gdm_config_reload_string		(const gchar *key);
gboolean	gdm_config_reload_int			(const gchar *key);
gboolean	gdm_config_reload_bool			(const gchar *key);
GSList *	gdm_config_get_xservers			(gboolean flexible);

void		gdm_save_customlist_data		(const gchar *file,
							 const gchar *key,
							 const gchar *id);
char *		gdm_get_theme_greeter			(const gchar *file,
							 const char *fallback);

#endif /* _GDMCONFIG_H */
