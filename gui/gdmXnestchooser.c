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
#include <sys/wait.h>
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
		if (connect (sock, (struct sockaddr *)&serv_addr,
			     sizeof (serv_addr)) >= 0 ||
		    errno != ECONNREFUSED) {
			close (sock);
			continue;
		}

		close (sock);

		g_snprintf (buf, sizeof (buf), "/tmp/.X11-unix/X%d", i);
		if (stat (buf, &s) == 0 &&
		    s.st_uid != getuid ()) {
			continue;
		}

		g_snprintf (buf, sizeof (buf), "/tmp/.X%d-lock", i);
		if (stat (buf, &s) == 0 &&
		    s.st_uid != getuid ()) {
			continue;
		}

		return i;
	}
	fprintf (stderr, "ERROR! Can't find free display!\n");

	return -1;
}

static const char *host = "localhost";
static char *xnest_binary = NULL;
static char *xnest_options = NULL;
static gboolean no_query = FALSE;
static gboolean background = FALSE;
static gboolean do_direct = FALSE;
static gboolean do_broadcast = FALSE;
static gboolean no_gdm_check = FALSE;

static char display_num[BUFSIZ] = "";
static char indirect_host[BUFSIZ] = "";

static const struct poptOption options[] = {
	{ "xnest", 'x', POPT_ARG_STRING, &xnest_binary, 0, N_("Xnest command line"), N_("STRING") },
	{ "xnest-extra-options", 'o', POPT_ARG_STRING, &xnest_options, 0, N_("Extra options for Xnest"), N_("OPTIONS") },
	{ "no-query", 'n', POPT_ARG_NONE, &no_query, 0, N_("Just run Xnest, no query (no chooser)"), NULL },
	{ "direct", 'd', POPT_ARG_NONE, &do_direct, 0, N_("Do direct query instead of indirect (chooser)"), NULL },
	{ "broadcast", 'B', POPT_ARG_NONE, &do_broadcast, 0, N_("Run broadcast instead of indirect (chooser)"), NULL },
	{ "background", 'b', POPT_ARG_NONE, &background, 0, N_("Run in background"), NULL },
	{ "no-gdm-check", '\0', POPT_ARG_NONE, &no_gdm_check, 0, N_("Don't check for running gdm"), NULL },
	{ NULL } 
};

static char **
make_us_an_exec_vector (const char *xnest)
{
	char **vector;
	int i, ii;
	int argc;
	char **xnest_vec;
	char **options_vec = NULL;

	if( ! ve_string_empty (xnest_binary))
		xnest = xnest_binary;

	if (ve_string_empty (xnest))
		xnest = "Xnest";

	if (xnest[0] == '/' &&
	    /* leak */
	    access (ve_sure_string (ve_first_word (xnest)), X_OK) != 0) {
		xnest = "Xnest";
	}

	/* leak */
	if (gnome_is_program_in_path (xnest) == NULL) {
		xnest = "Xnest";
		/* leak */
		if (gnome_is_program_in_path (xnest) == NULL) {
			return NULL;
		}
	}

	xnest_vec = ve_split (xnest);
	if (xnest_options != NULL)
		options_vec = ve_split (xnest_options);
	else
		options_vec = NULL;

	argc = ve_vector_len (xnest_vec) +
		1 +
		ve_vector_len (options_vec) +
		3;

	vector = g_new0 (char *, argc);

	ii = 0;

	/* lots of leaks follow */

	vector[ii++] = xnest_vec[0];
	vector[ii++] = display_num;

	for (i = 1; xnest_vec[i] != NULL; i++) {
		vector[ii++] = xnest_vec[i];
	}

	if (options_vec != NULL) {
		for (i = 0; options_vec[i] != NULL; i++) {
			vector[ii++] = options_vec[i];
		}
	}

	if ( ! no_query) {
		if (do_broadcast) {
			vector[ii++] = "-broadcast";
		} else if (do_direct) {
			vector[ii++] = "-query";
			vector[ii++] = indirect_host;
		} else {
			vector[ii++] = "-indirect";
			vector[ii++] = indirect_host;
		}
	}

	return vector;
}

int
main (int argc, char *argv[])
{
	gboolean xdmcp_enabled;
	gboolean honor_indirect;
	int display;
	char *socket;
	char *pidfile;
	poptContext ctx;
	const char **args;
	char *xnest;
	char **execvec;
	pid_t pid;

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
	xnest = gnome_config_get_string (GDM_KEY_XNEST);
	gnome_config_pop_prefix ();

	/* complex and wonderous way to get the exec vector */
	execvec = make_us_an_exec_vector (xnest);

	if (execvec == NULL) {
		GtkWidget *d;
		d = gnome_warning_dialog (_("Xnest doesn't exist.\n"
					    "Please ask your system "
					    "administrator\n"
					    "to install it."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
		return 0;
	}

	if ( ! no_query &&
	     ! do_broadcast &&
	     ! no_gdm_check &&
	    strcmp (host, "localhost") == 0) {
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

	g_print ("DISPLAY=:%d\n", display);

	g_snprintf (display_num, sizeof (display_num), ":%d", display);
	g_snprintf (indirect_host, sizeof (indirect_host), "%s", host);

	pid = fork ();
	if (pid == 0) {
		execvp (execvec[0], execvec);
		g_warning ("Can't exec");
		_exit (1);
	} else if (pid < 0) {
		/* eeeek */
		g_warning ("Can't fork");
		_exit (1);
	}

	if (background) {
		if (fork () > 0) {
			_exit (0);
		}
	}

	waitpid (pid, 0, 0);

	socket = g_strdup_printf ("/tmp/.X11-unix/X%d", display);

	unlink (socket);

	return 0;
}
