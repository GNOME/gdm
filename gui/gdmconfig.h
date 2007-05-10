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

typedef struct _GdmXserver GdmXserver;
struct _GdmXserver
{
	char    *id;
	char    *name;
	char    *command;
	gboolean flexible;
	gboolean choosable; /* not implemented yet */
	gboolean chooser; /* instead of greeter, run chooser */
	gboolean handled;
	int      number;
	int      priority;
};

#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_SUSPEND 17	/* Suspend (don't use, use the interrupt) */
#define DISPLAY_CHOSEN 20	/* successful chooser session,
				   restart display */
#define DISPLAY_RUN_CHOOSER 30	/* Run chooser */
#define DISPLAY_XFAILED 64	/* X failed */
#define DISPLAY_GREETERFAILED 65 /* greeter failed (crashed) */
#define DISPLAY_RESTARTGREETER 127 /* Restart greeter */
#define DISPLAY_RESTARTGDM 128	/* Restart GDM */

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
