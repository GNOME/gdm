/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <X11/Xlib.h>
#include <X11/extensions/dmxext.h>

#include <ve-misc.h>

static char *to_display = NULL;
static char *backend_display = NULL;
static char *to_authfile = NULL;
static char *backend_authfile = NULL;

static GOptionEntry options[] = {
	{
		"to", 0, 0, G_OPTION_ARG_STRING, &to_display,
		N_("DMX display to migrate to"),
		N_("DISPLAY")
	},
	{
		"display", 0, 0, G_OPTION_ARG_STRING, &backend_display,
		N_("Backend display name"),
		N_("DISPLAY")
	},
	{
		"to-authfile", 0, 0, G_OPTION_ARG_STRING, &to_authfile,
		N_("Xauthority file for destination display"),
		N_("AUTHFILE")
	},
	{
		"display-authfile", 0, 0, G_OPTION_ARG_STRING, &backend_authfile,
		N_("Xauthority file for backend display"),
		N_("AUTHFILE")
	},
	{ NULL }
};

static Display *
get_dmx_display (const char *display_name,
		 const char *authfile)
{
	Display *display;
	int event_base, error_base;
	const char *old_authfile;

	old_authfile = getenv ("XAUTHORITY");
	ve_setenv ("XAUTHORITY", authfile, TRUE);

	if ((display = XOpenDisplay (display_name)) == NULL)
		g_printerr (_("Failed to open display \"%s\"\n"), display_name);

	if (display != NULL &&
	    !DMXQueryExtension (display, &event_base, &error_base)) {
		g_printerr (_("DMX extension not present on \"%s\"\n"), display_name);
		XCloseDisplay (display);
		display = NULL;
	}

	ve_setenv ("XAUTHORITY", old_authfile, TRUE);

	return display;
}

int
main (int argc, char **argv)
{
	GOptionContext *options_context;
	Display *display;
	DMXScreenAttributes attr;
	guint mask;
	int screen;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	options_context = g_option_context_new (_("- migrate a backend display from one DMX display to another"));
	g_option_context_add_main_entries (options_context, options, GETTEXT_PACKAGE);
	g_option_context_parse (options_context, &argc, &argv, NULL);
	g_option_context_free (options_context);

	if (to_display == NULL) {
		g_printerr (_("You must specify a destination DMX display using %s\n"), "--to");
		return 1;
	}

	if (backend_display == NULL) {
		g_printerr (_("You must specify a backend display by using %s\n"), "--display");
		return 1;
	}

	if ((display = get_dmx_display (to_display, to_authfile)) == NULL)
		return 1;

	/* Note, we have no way yet of using backend_authfile to ensure
	 * that the DMX server can authenticate against the backend Xserver.
	 * For now, we must disable access control on the backend server.
	 */

	mask = 0;
	screen = 0;
	if (!DMXAddScreen (display, backend_display, mask, &attr, &screen)) {
		g_printerr (_("DMXAddScreen \"%s\" failed on \"%s\"\n"),
			    backend_display, to_display);
		XCloseDisplay (display);
		return 1;
	}

	XCloseDisplay (display);

	return 0;
}
