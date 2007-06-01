/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *    GDMflexiserver - run a flexible server
 *    (c)2001 Queen of England
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gdm.h"
#include "gdmcommon.h"

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-socket-protocol.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

static gboolean got_standard     = FALSE;
static gboolean use_xnest        = FALSE;
static gboolean debug_in         = FALSE;
static gboolean authenticate     = FALSE;
static gboolean no_lock          = FALSE;
static gboolean startnew         = FALSE;
static gchar **args_remaining    = NULL;

GOptionEntry options [] = {
	{ "xnest", 'n', 0, G_OPTION_ARG_NONE, &use_xnest, N_("Xnest mode"), NULL },
	{ "no-lock", 'l', 0, G_OPTION_ARG_NONE, &no_lock, N_("Do not lock current screen"), NULL },
	{ "debug", 'd', 0, G_OPTION_ARG_NONE, &debug_in, N_("Debugging output"), NULL },
	{ "authenticate", 'a', 0, G_OPTION_ARG_NONE, &authenticate, N_("Authenticate before running --command"), NULL },
	{ "startnew", 's', 0, G_OPTION_ARG_NONE, &startnew, N_("Start new flexible session; do not show popup"), NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args_remaining, NULL, NULL },
	{ NULL }
};

static gboolean
is_program_in_path (const char *program)
{
	char *tmp = g_find_program_in_path (program);
	if (tmp != NULL) {
		g_free (tmp);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
maybe_lock_screen (void)
{
	gboolean   use_gscreensaver = FALSE;
	GError    *error            = NULL;
	char      *command;
	GdkScreen *screen;

	if (is_program_in_path ("gnome-screensaver-command"))
		use_gscreensaver = TRUE;
	else if (! is_program_in_path ("xscreensaver-command"))
		return;

	if (use_gscreensaver) {
		command = g_strdup ("gnome-screensaver-command --lock");
	} else {
		command = g_strdup ("xscreensaver-command -lock");
	}

	screen = gdk_screen_get_default ();

	if (! gdk_spawn_command_line_on_screen (screen, command, &error)) {
		g_warning ("Cannot lock screen: %s", error->message);
		g_error_free (error);
	}

	g_free (command);

	if (! use_gscreensaver) {
		command = g_strdup ("xscreensaver-command -throttle");
		if (! gdk_spawn_command_line_on_screen (screen, command, &error)) {
			g_warning ("Cannot disable screensaver engines: %s", error->message);
			g_error_free (error);
		}

		g_free (command);
	}
}

int
main (int argc, char *argv[])
{
	gboolean    initialized;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	initialized = gtk_init_with_args (&argc,
					  &argv,
					  "- New gdm login",
					  options,
					  GETTEXT_PACKAGE,
					  NULL);

	gdm_log_init ();
	gdm_log_set_debug (debug_in);

	if (! initialized) {
		g_warning ("Unable to open a display");
		exit (1);
	}

	return 1;
}

