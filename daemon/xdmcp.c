/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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


#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include "display.h"
#include "gdm-daemon-config.h"
#include "gdm-log.h"

#include "gdm-xdmcp-manager.h"
#include "xdmcp.h"

#ifdef HAVE_LIBXDMCP

static GdmXdmcpManager *xdmcp_manager = NULL;

gboolean
gdm_xdmcp_init (void)
{
	xdmcp_manager = gdm_xdmcp_manager_new ();
	return TRUE;
}

void
gdm_xdmcp_run (void)
{
	if (xdmcp_manager != NULL) {
		gdm_xdmcp_manager_start (xdmcp_manager, NULL);
	}
}

void
gdm_xdmcp_close (void)
{
	g_object_unref (xdmcp_manager);
}

static void
reconnect_to_parent (GdmDisplay *to)
{
       GError *error;
       gchar *command_argv[10];
       const gchar *proxyreconnect = gdm_daemon_config_get_value_string (GDM_KEY_XDMCP_PROXY_RECONNECT);

       command_argv[0] = (char *)proxyreconnect;
       command_argv[1] = "--display";
       command_argv[2] = to->parent_disp;
       command_argv[3] = "--display-authfile";
       command_argv[4] = to->parent_auth_file;
       command_argv[5] = "--to";
       command_argv[6] = to->name;
       command_argv[7] = "--to-authfile";
       command_argv[8] = to->authfile;
       command_argv[9] = NULL;

       gdm_debug ("XDMCP: migrating display by running "
                  "'%s --display %s --display-authfile %s --to %s --to-authfile %s'",
                  proxyreconnect,
                  to->parent_disp, to->parent_auth_file,
                  to->name, to->authfile);

       error = NULL;
       if (!g_spawn_async (NULL, command_argv, NULL, 0, NULL, NULL, NULL, &error)) {
               gdm_error (_("%s: Failed to run "
                            "'%s --display %s --display-authfile %s --to %s --to-authfile %s': %s"),
                          "gdm_xdmcp_migrate",
                          proxyreconnect,
                          to->parent_disp, to->parent_auth_file,
                          to->name, to->authfile,
                          error->message);
               g_error_free (error);
       }
}

void
gdm_xdmcp_migrate (GdmDisplay *from, GdmDisplay *to)
{
       if (from->type != TYPE_XDMCP_PROXY ||
           to->type   != TYPE_XDMCP_PROXY)
               return;
       g_free (to->parent_disp);
       to->parent_disp = from->parent_disp;
       from->parent_disp = NULL;

       g_free (to->parent_auth_file);
       to->parent_auth_file = from->parent_auth_file;
       from->parent_auth_file = NULL;

       reconnect_to_parent (to);
}

#else /* HAVE_LIBXDMCP */

/* Here come some empty stubs for no XDMCP support */
int
gdm_xdmcp_init  (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_init");
	return FALSE;
}

void
gdm_xdmcp_run (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_run");
}

void
gdm_xdmcp_close (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_close");
}

void
gdm_xdmcp_migrate (GdmDisplay *from, GdmDisplay *to)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_migrate");
}

#endif /* HAVE_LIBXDMCP */
