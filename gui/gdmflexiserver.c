/*
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
#include <gnome.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>

#include <viciousui.h>

#include "gdm.h"

static char *
do_command (int fd, const char *command, gboolean get_response)
{
	GString *str;
	char buf[1];
	char *cstr;

	g_print ("Sending command: '%s'\n", command);

	cstr = g_strdup_printf ("%s\n", command);
	if (send (fd, cstr, strlen (cstr), MSG_NOSIGNAL) < 0)
		return NULL;

	if ( ! get_response)
		return NULL;

	str = g_string_new (NULL);
	while (read (fd, buf, 1) == 1 &&
	       buf[0] != '\n') {
		g_string_append_c (str, buf[0]);
	}

	g_print ("  Got response: '%s'\n", str->str);

	cstr = str->str;
	g_string_free (str, FALSE);
	return cstr;
}

static gboolean
version_ok_p (const char *version, const char *min_version)
{
	int a = 0, b = 0, c = 0, d = 0;
	int mina = 0, minb = 0, minc = 0, mind = 0;

	/* Note that if some fields in the version don't exist, then
	 * we don't mind, they are zero */
	sscanf (version, "%d.%d.%d.%d", &a, &b, &c, &d);
	sscanf (min_version, "%d.%d.%d.%d", &mina, &minb, &minc, &mind);

	if ((a > mina) ||
	    (a == mina && b > minb) ||
	    (a == mina && b == minb && c > minc) ||
	    (a == mina && b == minb && c == minc && d >= mind))
		return TRUE;
	else
		return FALSE;
}

static char *
call_gdm (const char *command, const char *min_version, int tries)
{
	struct sockaddr_un addr;
	int fd;
	char *ret;

	if (tries <= 0)
		return NULL;

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return call_gdm (command, min_version, tries - 1);
	}

	strcpy (addr.sun_path, GDM_SUP_SOCKET);
	addr.sun_family = AF_UNIX;

	if (connect (fd, &addr, sizeof (addr)) < 0) {
		close (fd);
		return call_gdm (command, min_version, tries - 1);
	}

	/* Version check first */
	ret = do_command (fd, GDM_SUP_VERSION, TRUE /* get_response */);
	if (ret == NULL) {
		close (fd);
		return call_gdm (command, min_version, tries - 1);
	}
	if (strncmp (ret, "GDM ", strlen ("GDM ")) != 0) {
		g_free (ret);
		close (fd);
		return NULL;
	}
	if ( ! version_ok_p (&ret[4], min_version)) {
		g_free (ret);
		do_command (fd, GDM_SUP_CLOSE, FALSE /* get_response */);
		close (fd);
		return NULL;
	}

	ret = do_command (fd, command, TRUE /* get_response */);
	if (ret == NULL) {
		close (fd);
		return call_gdm (command, min_version, tries - 1);
	}

	do_command (fd, GDM_SUP_CLOSE, FALSE /* get_response */);

	close (fd);

	return ret;
}

static char *
get_display (void)
{
	static char *display = NULL;

	if (display == NULL) {
		display = g_strdup (g_getenv ("DISPLAY"));
		if (display == NULL) /*eek!*/
			display = g_strdup (":0");
	}

	return display;
}

static char *
get_xauth_string (void)
{
	char *command;
	FILE *fp;
	char buf[1024];
	char *p;

	command = g_strdup_printf ("xauth nextract - %s", get_display ());
	fp = popen (command, "r");
	g_free (command);

	if (fp == NULL) {
		return NULL;
	}
	if (fgets (buf, sizeof (buf), fp) == NULL) {
		fclose (fp);
		return NULL;
	}
	fclose (fp);

	p = strchr (buf, '\n');
	if (p != NULL)
		*p = '\0';

	return g_strdup (buf);
}

static gboolean use_xnest = FALSE;

struct poptOption options [] = {
	{ "xnest", 'n', POPT_ARG_NONE, &use_xnest, 0, N_("Xnest mode"), NULL },
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0}
};


int
main (int argc, char *argv[])
{
	FILE *fp = NULL;
	long pid;
	char *pidfile;
	char *command;
	char *version;
	char *ret;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init_with_popt_table ("gdmflexiserver", VERSION, argc, argv,
				    options, 0, NULL);

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	pidfile = gnome_config_get_string (GDM_KEY_PIDFILE);
	gnome_config_pop_prefix ();

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

	if (access (GDM_SUP_SOCKET, R_OK|W_OK)) {
		GtkWidget *d;
		d = gnome_warning_dialog
			(_("Cannot communicate with gdm, perhaps "
			   "you have an old version running."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
		return 0;
	}

	if (use_xnest) {
		char *xauth_string = get_xauth_string ();

		if (xauth_string != NULL) {
			command = g_strdup_printf (GDM_SUP_FLEXI_XNEST
						   " %s %s",
						   get_display (),
						   xauth_string);
		} else {
			command = g_strdup_printf (GDM_SUP_FLEXI_XNEST
						   " %s",
						   get_display ());
		}
		version = "2.2.3.2";
	} else {
		command = g_strdup (GDM_SUP_FLEXI_XSERVER);
		version = "2.2.3.2";
	}

	ret = call_gdm (command, version, 5);
	if (ret == NULL) {
		GtkWidget *d;
		d = gnome_warning_dialog
			(_("Cannot communicate with gdm, perhaps "
			   "you have an old version running."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
		return 0;
	} else if (strncmp (ret, "ERROR 0 ", strlen ("ERROR 0 ")) == 0) {
		GtkWidget *d;
		g_warning ("Command not implemented");
		d = gnome_warning_dialog
			(_("Cannot communicate with gdm, perhaps "
			   "you have an old version running."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
	} else if (strncmp (ret, "OK ", 3) != 0) {
		GtkWidget *d;
		d = gnome_warning_dialog (_("Unknown error occured."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
	}

	return 0;
}

/* Used for torture testing the socket */
#if 0
static void
torture (void)
{
	struct sockaddr_un addr;
	int fd;
	int i;
	int times;

	srand (getpid () * time (NULL));

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}

	strcpy (addr.sun_path, "/tmp/.gdm_socket");
	addr.sun_family = AF_UNIX;

	if (connect (fd, &addr, sizeof (addr)) < 0) {
		close (fd);
		return;
	}

	g_print ("OPEN ");

	times = rand () % 500;
	for (i = 0; i < rand () % 500; i++) {
		int len = rand () % 5000;
		char *buf = g_new (char, len);
		int ii;
		for (ii = 0; ii < len; ii ++)
			buf[ii] = rand () % 256;
		write (fd, buf, len); 
		g_free (buf);
		g_print ("SENT(%d) ", len);
	}

	close (fd);
}

static void
torture_test (void)
{
	int i;

	srand (getpid () * time (NULL));

	for (i = 0; i < 500; i ++) {
		if (fork () == 0) {
			torture ();
			_exit (0);
		}
		usleep (1000);
	}
}
#endif
