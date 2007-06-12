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

#include <glib.h>

#define LAST_SESSION "Last"
#define SESSION_NAME "SessionName"

typedef struct {
        char *name;
        char *clearname;
        char *comment;
} GdmSession;

enum {
	SESSION_LOOKUP_SUCCESS,
	SESSION_LOOKUP_PREFERRED_MISSING,
	SESSION_LOOKUP_DEFAULT_MISMATCH,
	SESSION_LOOKUP_USE_SWITCHDESK
};

#define GDM_SESSION_FAILSAFE_GNOME "GDM_Failsafe.GNOME"
#define GDM_SESSION_FAILSAFE_XTERM "GDM_Failsafe.XTERM"

/* FIXME: will support these builtin types later */
#define GDM_SESSION_DEFAULT "default"
#define GDM_SESSION_CUSTOM "custom"
#define GDM_SESSION_FAILSAFE "failsafe"


void		gdm_session_list_init		(void);
void		_gdm_session_list_init		(GHashTable **sessnames,
						 GList **sessions,
						 gchar **default_session, 
						 const gchar **current_session);
gint		gdm_session_sort_func		(const char *a, const char *b);
const char *	gdm_session_name 		(const char *name);
void		gdm_session_list_from_hash_table_func (const char *key,
						const char *value,
						GList **sessions);
gint		gdm_session_sort_func		(const char *a,
						 const char *b);
char *		gdm_session_lookup 		(const char *saved_session, gint *lookup_status);

gint		gdm_get_save_session 		(void);

void		gdm_set_save_session 		(const gint session);

const char *    gdm_get_default_session         (void);

#endif /* GDM_SESSION_H */
