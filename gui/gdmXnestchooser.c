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
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <X11/Xauth.h>

#include <viciousui.h>

#include "gdm.h"
#include "gdmcomm.h"

static pid_t xnest_pid = 0;

static void
term_handler (int sig)
{
	if (xnest_pid != 0)
		kill (xnest_pid, SIGTERM);
	else
		exit (0);
}

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

/* options for Xnest only mode */
static const struct poptOption xnest_only_options[] = {
	{ "xnest", 'x', POPT_ARG_STRING, &xnest_binary, 0, N_("Xnest command line"), N_("STRING") },
	{ "xnest-extra-options", 'o', POPT_ARG_STRING, &xnest_options, 0, N_("Extra options for Xnest"), N_("OPTIONS") },
	{ "background", 'b', POPT_ARG_NONE, &background, 0, N_("Run in background"), NULL },
	{ NULL } 
};

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

#if 0
/* Perhaps this may be useful sometime */
static gboolean
test_opt (const char *cmd, const char *help, const char *option)
{
	char *q = g_shell_quote (cmd);
	char *full;
	char buf[1024];
	FILE *fp;

	full = g_strdup_printf ("%s %s 2>&1", q, help);
	g_free (q);

	fp = popen (full, "r");
	g_free (full);

	if (fp == NULL)
		return FALSE;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p = strstr (buf, option);
		char end;
		if (p == NULL)
			continue;
		/* must be a full word */
		end = *(p + strlen (option));
		if ((end >= 'a' && end <= 'z') ||
		    (end >= 'A' && end <= 'Z') ||
		    (end >= '0' && end <= '9') ||
		    end == '_')
			continue;

		fclose (fp);
		return TRUE;
	}
	fclose (fp);
	return FALSE;
}
#endif

static char *
get_font_path (const char *display)
{
	Display *disp;
	char **font_path;
	int n_fonts;
	int i;
	GString *gs;

	disp = XOpenDisplay (display);
	if (disp == NULL)
		return NULL;

	font_path = XGetFontPath (disp, &n_fonts);
	if (font_path == NULL) {
		XCloseDisplay (disp);
		return NULL;
	}

	gs = g_string_new (NULL);
	for (i = 0; i < n_fonts; i++) {
		if (i != 0)
			g_string_append_c (gs, ',');
		g_string_append (gs, font_path[i]);
	}

	XFreeFontPath (font_path);

	XCloseDisplay (disp);

	return g_string_free (gs, FALSE);
}

static char **
make_us_an_exec_vector (const char *xnest)
{
	char **vector;
	int i, ii;
	int argc;
	char **xnest_vec;
	char **options_vec = NULL;
	gboolean got_font_path = FALSE;
	char *font_path = NULL;

	if( ! ve_string_empty (xnest_binary))
		xnest = xnest_binary;

	if (ve_string_empty (xnest))
		xnest = "Xnest";

	xnest_vec = ve_split (xnest);
	if (xnest_options != NULL)
		options_vec = ve_split (xnest_options);
	else
		options_vec = NULL;

	argc = ve_vector_len (xnest_vec) +
		1 +
		ve_vector_len (options_vec) +
		3 +
		2;

	vector = g_new0 (char *, argc);

	ii = 0;

	/* lots of leaks follow */

	vector[ii++] = xnest_vec[0];
	vector[ii++] = display_num;

	for (i = 1; xnest_vec[i] != NULL; i++) {
		vector[ii++] = xnest_vec[i];
		if (strcmp (xnest_vec[i], "-fp") == 0)
			got_font_path = TRUE;
	}

	if (options_vec != NULL) {
		for (i = 0; options_vec[i] != NULL; i++) {
			vector[ii++] = options_vec[i];
			if (strcmp (options_vec[i], "-fp") == 0)
				got_font_path = TRUE;
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

	if ( ! got_font_path)
		font_path = get_font_path (NULL);

	if (font_path != NULL) {
		vector[ii++] = "-fp";
		vector[ii++] = font_path;
	}

	return vector;
}

static const char *
base (const char *s)
{
	const char *p = strchr (s, '/');
	if (p == NULL)
		return s;
	return p+1;
}

static const char *
xauth_path (void)
{
	static char *xauth_path = NULL;
	if (xauth_path == NULL)
		xauth_path = g_find_program_in_path ("xauth");
	return xauth_path;
}

static void
whack_cookie (int display)
{
	char *command;

	if (xauth_path () == NULL) {
		return;
	}

	command = g_strdup_printf ("%s remove :%d",
				   xauth_path (),
				   display);
	system (command);
	g_free (command);
}

static Xauth *
get_auth_entry (int disp, char *cookie)
{
	Xauth *xa;
	gchar *dispnum;
	char hostname [1024];

	xa = malloc (sizeof (Xauth));

	if (xa == NULL)
		return NULL;

	hostname[1023] = '\0';
	if (gethostname (hostname, 1023) != 0) {
		strcpy (hostname, "localhost");
	}

	xa->family = FamilyLocal;
	xa->address = malloc (strlen (hostname) + 1);
	if (xa->address == NULL) {
		free (xa);
		return FALSE;
	}

	strcpy (xa->address, hostname);
	xa->address_length = strlen (hostname);;

	dispnum = g_strdup_printf ("%d", disp);
	xa->number = strdup (dispnum);
	xa->number_length = strlen (dispnum);
	g_free (dispnum);

	xa->name = strdup ("MIT-MAGIC-COOKIE-1");
	xa->name_length = strlen ("MIT-MAGIC-COOKIE-1");
	xa->data = malloc (16);
	if (xa->data == NULL) {
		free (xa->number);
		free (xa->name);
		free (xa->address);
		free (xa);
		return FALSE;
	}
	memcpy (xa->data, cookie, 16);
	xa->data_length = 16;

	return xa;
}

static void
setup_cookie (int disp)
{
    char *cookie;
    FILE *af;
    Xauth *xa;
    const char *filename = XauFileName ();
    if (filename == NULL)
	    return;

    if (XauLockAuth (filename, 3, 3, 0) != LOCK_SUCCESS)
	    return;

    cookie = gdmcomm_get_a_cookie (TRUE /* binary */);
    if (cookie == NULL) {
	    XauUnlockAuth (filename);
	    return;
    }

    af = fopen (filename, "a+");
    if (af == NULL) {
	    XauUnlockAuth (filename);
	    g_free (cookie);
	    return;
    }

    xa = get_auth_entry (disp, cookie);
    if (xa == NULL) {
	    g_free (cookie);
	    fclose (af);
	    XauUnlockAuth (filename);
	    return;
    }

    g_free (cookie);

    XauWriteAuth (af, xa);

    XauDisposeAuth (xa);

    fclose (af);

    XauUnlockAuth (filename);
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
	struct sigaction term;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (strcmp (base (argv[0]), "gdmXnest") == 0) {
		GnomeProgram *program = gnome_program_init
			("gdmXnest", VERSION, 
			 LIBGNOMEUI_MODULE /* module_info */,
			 argc, argv,
			 GNOME_PARAM_POPT_TABLE, xnest_only_options,
			 NULL);
		g_object_get (G_OBJECT (program),
			      GNOME_PARAM_POPT_CONTEXT, &ctx,
			      NULL);	
		no_query = TRUE;
		no_gdm_check = TRUE;
	} else {
		GnomeProgram *program = gnome_program_init
			("gdmXnestchooser", VERSION, 
			 LIBGNOMEUI_MODULE /* module_info */,
			 argc, argv,
			 GNOME_PARAM_POPT_TABLE, options,
			 NULL);
		g_object_get (G_OBJECT (program),
			      GNOME_PARAM_POPT_CONTEXT, &ctx,
			      NULL);	
	}

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
		d = gtk_message_dialog_new (NULL /* parent */,
					    GTK_DIALOG_MODAL /* flags */,
					    GTK_MESSAGE_WARNING,
					    GTK_BUTTONS_OK,
					    _("Xnest doesn't exist.\n"
					    "Please ask your system "
					    "administrator\n"
					    "to install it."));
		gtk_widget_show_all (d);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
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
			d = gtk_message_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 _("Indirect XDMCP is not enabled,\n"
				   "please ask your "
				   "system administrator to enable "
				   "it\nin the GDM configurator "
				   "program."));
			gtk_widget_show_all (d);
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
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
			d = gtk_message_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 _("GDM is not running.\n"
				   "Please ask your "
				   "system administrator to start it."));
			gtk_widget_show_all (d);
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
			return 0;
		}
	}

	display = get_free_display ();
	if (display < 0) {
		GtkWidget *d;
		d = gtk_message_dialog_new (NULL /* parent */,
					    GTK_DIALOG_MODAL /* flags */,
					    GTK_MESSAGE_WARNING,
					    GTK_BUTTONS_OK,
					    _("Could not find a free "
					      "display number"));
		gtk_widget_show_all (d);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
		return 0;
	}

	printf ("DISPLAY=:%d\n", display);
	fflush (stdout);

	g_snprintf (display_num, sizeof (display_num), ":%d", display);
	g_snprintf (indirect_host, sizeof (indirect_host), "%s", host);

	if (no_query) {
		whack_cookie (display);
		setup_cookie (display);
	}

	if (background) {
		if (fork () > 0) {
			_exit (0);
		}
		close (0);
		close (1);
		close (2);
		open ("/dev/null", O_RDWR);
		open ("/dev/null", O_RDONLY);
		open ("/dev/null", O_RDONLY);
	}

	term.sa_handler = term_handler;
	term.sa_flags = SA_RESTART;
	sigemptyset (&term.sa_mask);

	sigaction (SIGTERM, &term, NULL);
	sigaction (SIGINT, &term, NULL);
	sigaction (SIGHUP, &term, NULL);

	xnest_pid = fork ();
	if (xnest_pid == 0) {
		execvp (execvec[0], execvec);
		g_warning ("Can't exec, trying Xnest");
		execvec[0] = "Xnest";
		execvp (execvec[0], execvec);
		g_warning ("Can't exec that either, giving up");
		/* FIXME: this should be handled in the GUI */
		_exit (1);
	} else if (xnest_pid < 0) {
		/* eeeek */
		g_warning ("Can't fork");
		_exit (1);
	}

	ve_waitpid_no_signal (xnest_pid, 0, 0);
	xnest_pid = 0;

	socket = g_strdup_printf ("/tmp/.X11-unix/X%d", display);
	unlink (socket);
	g_free (socket);

	if (no_query)
		whack_cookie (display);

	return 0;
}
