/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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

#include "config.h"

#include <string.h>

#ifdef HAVE_CHKAUTHATTR
#include <auth_attr.h>
#include <secdb.h>
#endif

#include "gdmgreeter.h"
#include <fontconfig/fontconfig.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gdm.h"
#include "gdm-common.h"
#include "gdmconfig.h"
#include "gdm-daemon-config-keys.h"

/*
 * This file is for functions that are only used by gdmlogin,
 * gdmgreeter, and gdmsetup
 */

gboolean
gdm_common_is_action_available (gchar *action)
{
	gchar **allowsyscmd = NULL;
	const gchar *allowsyscmdval;
	gboolean ret = FALSE;
	int i;

	allowsyscmdval = gdm_config_get_string (GDM_KEY_SYSTEM_COMMANDS_IN_MENU);
	if (allowsyscmdval)
		allowsyscmd = g_strsplit (allowsyscmdval, ";", 0);

	if (allowsyscmd) {
		for (i = 0; allowsyscmd[i] != NULL; i++) {
			if (strcmp (allowsyscmd[i], action) == 0) {
				ret = TRUE;
				break;
			}
		}
	}

#ifdef HAVE_CHKAUTHATTR
	if (ret == TRUE) {
		gchar **rbackeys = NULL;
		const gchar *rbackeysval;
		const char *gdmuser;

		gdmuser     = gdm_config_get_string (GDM_KEY_USER);
		rbackeysval = gdm_config_get_string (GDM_KEY_RBAC_SYSTEM_COMMAND_KEYS);
		if (rbackeysval)
			rbackeys = g_strsplit (rbackeysval, ";", 0);

		if (rbackeys) {
			for (i = 0; rbackeys[i] != NULL; i++) {
				gchar **rbackey = g_strsplit (rbackeys[i], ":", 2);

				if (gdm_vector_len (rbackey) == 2 &&
				    ! ve_string_empty (rbackey[0]) &&
				    ! ve_string_empty (rbackey[1]) &&
				    strcmp (rbackey[0], action) == 0) {

					if (!chkauthattr (rbackey[1], gdmuser)) {
						g_strfreev (rbackey);
						ret = FALSE;
						break;
					}
				}
				g_strfreev (rbackey);
			}
		}
		g_strfreev (rbackeys);
	}
#endif
	g_strfreev (allowsyscmd);

	return ret;
}

