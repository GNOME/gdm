/* GDM - The GNOME Display Manager
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

#include <vicious.h>
#include "gdm.h"
#include "gdmsession.h"
extern gchar *GdmDefaultSession;
extern GHashTable *sessnames;

/* Note: A lot of the session setup logic is identical in gdmlogin and
   gdmgreeter.  This is a start at getting the two to use common logic,
   but more needs to be done.
 */

gint
gdm_session_sort_func (const char *a, const char *b)
{
        /* Put default and GNOME sessions at the top */
        if (strcmp (a, ve_sure_string (GdmDefaultSession)) == 0)
                return -1;

        if (strcmp (b, ve_sure_string (GdmDefaultSession)) == 0)
                return 1;

        if (strcmp (a, "default.desktop") == 0)
                return -1;

        if (strcmp (b, "default.desktop") == 0)
                return 1;

        if (strcmp (a, "gnome.desktop") == 0)
                return -1;

        if (strcmp (b, "gnome.desktop") == 0)
                return 1;

        /* put failsafe sessions on the bottom */
        if (strcmp (b, GDM_SESSION_FAILSAFE_XTERM) == 0)
                return -1;

        if (strcmp (a, GDM_SESSION_FAILSAFE_XTERM) == 0)
                return 1;

        if (strcmp (b, GDM_SESSION_FAILSAFE_GNOME) == 0)
                return -1;

        if (strcmp (a, GDM_SESSION_FAILSAFE_GNOME) == 0)
                return 1;

        /* put everything else in the middle in alphabetical order */
                return strcmp (a, b);
}

const char *
gdm_session_name (const char *name)
{
        GdmSession *session;

        /* eek */
        if G_UNLIKELY (name == NULL)
                return "(null)";

        session = g_hash_table_lookup (sessnames, name);
        if (session != NULL && !ve_string_empty (session->name))
                return session->name;
        else
		return name;
}           

void
gdm_session_list_from_hash_table_func (const char *key, const char *value,
         GList **sessions)
{
        *sessions = g_list_prepend (*sessions, g_strdup (key));
}

