/*
 *    GDMXnestChooser - run X nest with a chooser using xdmcp
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
#include <gnome.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <viciousui.h>

#include "gdm.h"

static int
get_free_display (void)
{
	int sock;
	int i;
	struct sockaddr_in serv_addr = {0}; 

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	for (i = 20; i < 3000; i ++) {
		struct stat s;
		char buf[256];
		sock = socket (AF_INET, SOCK_STREAM, 0);

		serv_addr.sin_port = htons (6000 + i);

		errno = 0;
		if (connect (sock, &serv_addr, sizeof (serv_addr)) >= 0 ||
		    errno != ECONNREFUSED) {
			close (sock);
			continue;
		}

		g_snprintf (buf, sizeof (buf), "/tmp/.X11-unix/X%d", i);
		if (stat (buf, &s) == 0 &&
		    s.st_uid != getuid ()) {
			close (sock);
			continue;
		}

		close (sock);
		return i;
	}
	fprintf (stderr, "ERROR! Can't find free display!\n");

	return -1;
}

static const char *host = "localhost";

static const struct poptOption options[] = {
	{ NULL } 
};

int
main (int argc, char *argv[])
{
	gboolean xdmcp_enabled;
	gboolean honor_indirect;
	int display;
	char *command, *socket;
	char *pidfile;
	poptContext ctx;
	const char **args;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init_with_popt_table ("gdmXnestchooser", VERSION,
				    argc, argv, options, 0, &ctx);

	args = poptGetArgs (ctx);
	if (args != NULL && args[0] != NULL)
		host = args[0];

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	xdmcp_enabled = gnome_config_get_bool (GDM_KEY_XDMCP);
	honor_indirect = gnome_config_get_bool (GDM_KEY_INDIRECT);
	pidfile = gnome_config_get_string (GDM_KEY_PIDFILE);
	gnome_config_pop_prefix ();

	/* Leak, yeah yeah yeah, piss off */
	if (gnome_is_program_in_path ("Xnest") == NULL) {
		GtkWidget *d;
		d = gnome_warning_dialog (_("Xnest doesn't exist.\n"
					    "Please ask your system "
					    "administrator\n"
					    "to install it."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
		return 0;
	}

	if (strcmp (host, "localhost") == 0) {
		FILE *fp = NULL;
		long pid;
		
		if ( ! xdmcp_enabled ||
		     ! honor_indirect) {
			GtkWidget *d;
			d = gnome_warning_dialog
				(_("Indirect XDMCP is not enabled,\n"
				   "please ask your "
				   "system administrator to enable "
				   "it\nin the GDM configurator "
				   "program."));
			gnome_dialog_run_and_close (GNOME_DIALOG (d));
			return 0;
		}

		pid = 0;
		if (pidfile != NULL)
			fp = fopen (pidfile, "r");
		if (fp != NULL) {
			fscanf (fp, "%ld", &pid);
			fclose (fp);
		}

		errno = 0;
		if (pid <= 1 ||
		    (kill (pid, 0) < 0 &&
		     errno != EPERM)) {
			GtkWidget *d;
			d = gnome_warning_dialog
				(_("GDM is not running.\n"
				   "Please ask your "
				   "system administrator to start it."));
			gnome_dialog_run_and_close (GNOME_DIALOG (d));
			return 0;
		}
	}

	display = get_free_display ();
	if (display < 0) {
		GtkWidget *d;
		d = gnome_warning_dialog (_("Could not find a free "
					    "display number"));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
		return 0;
	}

	command = g_strdup_printf ("Xnest :%d -indirect %s",
				   display, host);

	system (command);

	socket = g_strdup_printf ("/tmp/.X11-unix/X%d", display);

	unlink (socket);

	return 0;
}
