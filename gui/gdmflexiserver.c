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
#include <gdk/gdkx.h>
#include <X11/Xauth.h>

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

#include <viciousui.h>

#include "gdm.h"

static GSList *xservers = NULL;
static gboolean got_standard = FALSE;
static gboolean use_xnest = FALSE;
static gboolean authenticate = FALSE;
static gboolean no_lock = FALSE;
static const char *send_command = NULL;
static const char *server = NULL;
static const char *chosen_server = NULL;
static gboolean debug = FALSE;


static char *
do_command (int fd, const char *command, gboolean get_response)
{
	GString *str;
	char buf[1];
	char *cstr;
	int ret;
#ifndef MSG_NOSIGNAL
	void (*old_handler)(int);
#endif

	if (debug)
		g_print ("Sending command: '%s'\n", command);

	cstr = g_strdup_printf ("%s\n", command);

#ifdef MSG_NOSIGNAL
	ret = send (fd, cstr, strlen (cstr), MSG_NOSIGNAL);
#else
	old_handler = signal (SIGPIPE, SIG_IGN);
	ret = send (fd, cstr, strlen (cstr), 0);
	signal (SIGPIPE, old_handler);
#endif

	if (ret < 0)
		return NULL;

	if ( ! get_response)
		return NULL;

	str = g_string_new (NULL);
	while (read (fd, buf, 1) == 1 &&
	       buf[0] != '\n') {
		g_string_append_c (str, buf[0]);
	}

	if (debug)
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
call_gdm (const char *command, const char * auth_cookie, const char *min_version, int tries)
{
	struct sockaddr_un addr;
	int fd;
	char *ret;

	if (tries <= 0)
		return NULL;

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return call_gdm (command, auth_cookie, min_version, tries - 1);
	}

	strcpy (addr.sun_path, GDM_SUP_SOCKET);
	addr.sun_family = AF_UNIX;

	if (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		close (fd);
		return call_gdm (command, auth_cookie, min_version, tries - 1);
	}

	/* Version check first */
	ret = do_command (fd, GDM_SUP_VERSION, TRUE /* get_response */);
	if (ret == NULL) {
		close (fd);
		return call_gdm (command, auth_cookie, min_version, tries - 1);
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
	g_free (ret);

	/* require authentication */
	if (auth_cookie != NULL)  {
		char *auth_cmd = g_strdup_printf
			(GDM_SUP_AUTH_LOCAL " %s", auth_cookie);
		ret = do_command (fd, auth_cmd, TRUE /* get_response */);
		g_free (auth_cmd);
		if (ret == NULL) {
			close (fd);
			return call_gdm (command, auth_cookie,
					 min_version, tries - 1);
		}
		/* not auth'ed */
		if (strcmp (ret, "OK") != 0) {
			do_command (fd, GDM_SUP_CLOSE,
				    FALSE /* get_response */);
			close (fd);
			/* returns the error */
			return ret;
		}
		g_free (ret);
	}

	ret = do_command (fd, command, TRUE /* get_response */);
	if (ret == NULL) {
		close (fd);
		return call_gdm (command, auth_cookie, min_version, tries - 1);
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
		display = g_strdup (gdk_display_name);
		if (display == NULL) {
			display = g_strdup (g_getenv ("DISPLAY"));
			if (display == NULL) /*eek!*/ {
				display = g_strdup (":0");
			}
		}
	}

	return display;
}

static char *
get_dispnum (void)
{
	static char *number = NULL;

	if (number == NULL) {
		char *p;
		number = g_strdup (get_display ());

		/* whee! handles even DECnet crap */
		number = strchr (number, ':');
		if (number != NULL) {
			while (*number == ':') {
				number++;
			}
			p = strchr (number, '.');
			if (p != NULL)
				*p = '\0';
		} else {
			number = "0";
		}
	}

	return number;
}

/* This just gets a cookie of MIT-MAGIC-COOKIE-1 type */
static char *
get_a_cookie (void)
{
	FILE *fp;
	char *number;
	char *cookie = NULL;
	Xauth *xau;

	fp = fopen (XauFileName (), "r");
	if (fp == NULL) {
		return NULL;
	}

	number = get_dispnum ();

	cookie = NULL;

	while ((xau = XauReadAuth (fp)) != NULL) {
		int i;
		GString *str;

		/* Just find the FIRST magic cookie, that's what gdm uses */
		if (xau->number_length != strlen (number) ||
		    strncmp (xau->number, number, xau->number_length) != 0 ||
		    /* gdm sends MIT-MAGIC-COOKIE-1 cookies of length 16,
		     * so just do those */
		    xau->name_length != strlen ("MIT-MAGIC-COOKIE-1") ||
		    strncmp (xau->name, "MIT-MAGIC-COOKIE-1",
			     xau->name_length) != 0) {
			XauDisposeAuth (xau);
			continue;
		}

		str = g_string_new (NULL);

		for (i = 0; i < xau->data_length; i++) {
			g_string_sprintfa (str, "%02x",
					   (guint)(guchar)xau->data[i]);
		}

		XauDisposeAuth (xau);

		cookie = str->str;
		g_string_free (str, FALSE);
		break;
	}
	fclose (fp);

	return cookie;
}

static char *
get_auth_cookie (void)
{
	FILE *fp;
	char *number;
	static gboolean tried = FALSE;
	static char *cookie = NULL;
	Xauth *xau;

	if (tried)
		return cookie;

	fp = fopen (XauFileName (), "r");
	if (fp == NULL) {
		cookie = NULL;
		tried = TRUE;
		return NULL;
	}

	number = get_dispnum ();

	cookie = NULL;

	while ((xau = XauReadAuth (fp)) != NULL) {
		char *cmd;
		char *ret;
		int i;
		char buffer[40 /* 2*16 == 32, so 40 is enough */];

		/* Only Family local things are considered, all console
		 * logins DO have this family (and even some local xdmcp
		 * logins, though those will not pass by gdm itself of
		 * course) */
		if (xau->family != FamilyLocal ||
		    xau->number_length != strlen (number) ||
		    strncmp (xau->number, number, xau->number_length) != 0 ||
		    /* gdm sends MIT-MAGIC-COOKIE-1 cookies of length 16,
		     * so just do those */
		    xau->name_length != strlen ("MIT-MAGIC-COOKIE-1") ||
		    strncmp (xau->name, "MIT-MAGIC-COOKIE-1",
			     xau->name_length) != 0 ||
		    xau->data_length != 16) {
			XauDisposeAuth (xau);
			continue;
		}

		buffer[0] = '\0';
		for (i = 0; i < 16; i++) {
			char sub[3];
			g_snprintf (sub, sizeof (sub), "%02x",
				    (guint)(guchar)xau->data[i]);
			strcat (buffer, sub);
		}

		XauDisposeAuth (xau);

		cmd = g_strdup_printf (GDM_SUP_AUTH_LOCAL " %s", buffer);
		ret = call_gdm (cmd, NULL /* auth cookie */, "2.2.4.0", 5);
		if (ret != NULL &&
		    strcmp (ret, "OK") == 0) {
			g_free (ret);
			cookie = g_strdup (buffer);
			break;
		}
		g_free (ret);
	}
	fclose (fp);

	tried = TRUE;
	return cookie;
}

static void
read_servers (void)
{
	gpointer iter;
	char *k;

	/* Find server definitions */
	iter = gnome_config_init_iterator_sections ("=" GDM_CONFIG_FILE "=/");
	iter = gnome_config_iterator_next (iter, &k, NULL);

	while (iter) {
		if (strncmp (k, "server-", strlen ("server-")) == 0) {
			char *section;
			GdmXServer *svr;

			section = g_strdup_printf ("=" GDM_CONFIG_FILE "=/%s/", k);
			gnome_config_push_prefix (section);

			if ( ! gnome_config_get_bool
			     (GDM_KEY_SERVER_FLEXIBLE)) {
				gnome_config_pop_prefix ();
				g_free (section);
				g_free (k);
				iter = gnome_config_iterator_next (iter,
								   &k, NULL);
				continue;
			}

			svr = g_new0 (GdmXServer, 1);

			svr->id = g_strdup (k + strlen ("server-"));
			svr->name = gnome_config_get_string
				(GDM_KEY_SERVER_NAME);
			svr->command = gnome_config_get_string
				(GDM_KEY_SERVER_COMMAND);
			svr->flexible = TRUE;
			svr->choosable = gnome_config_get_bool
				(GDM_KEY_SERVER_CHOOSABLE);

			g_free (section);
			gnome_config_pop_prefix ();

			if (strcmp (svr->id, GDM_STANDARD) == 0)
				got_standard = TRUE;

			if (server != NULL &&
			    strcmp (svr->id, server) == 0)
				chosen_server = g_strdup (svr->id);

			xservers = g_slist_append (xservers, svr);
		}

		g_free (k);

		iter = gnome_config_iterator_next (iter, &k, NULL);
	}
}

static char *
choose_server (void)
{
	GtkWidget *dialog, *vbox;
	GtkWidget *w;
	GSList *group = NULL;
	GSList *li;

	if (chosen_server != NULL)
		return g_strdup (chosen_server);

	if (xservers == NULL)
		return NULL;

	if (xservers->next == NULL &&
	    got_standard)
		return g_strdup (GDM_STANDARD);

	dialog = gnome_dialog_new (_("Choose server"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	vbox = GNOME_DIALOG (dialog)->vbox;

	w = gtk_label_new (_("Choose the X server to start"));
	gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

	group = NULL;
	if ( ! got_standard) {
		w = gtk_radio_button_new_with_label (group,
						     _("Standard server"));
		gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (w));
	}

	for (li = xservers; li != NULL; li = li->next) {
		GdmXServer *svr = li->data;
		w = gtk_radio_button_new_with_label
			(group, svr->name ? svr->name : svr->id);
		gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
		if (got_standard &&
		    strcmp (svr->id, GDM_STANDARD) == 0)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
						      TRUE);
		gtk_object_set_user_data (GTK_OBJECT (w), svr->id);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (w));
	}

	gtk_widget_show_all (dialog);

	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);

	switch (gnome_dialog_run_and_close (GNOME_DIALOG (dialog))) {
	case 0:	
		/* OK */
		break;
	default:
		gtk_widget_destroy (dialog);
		/* cancel, or close */
		exit (0);
		break;
	}

	for (li = group; li != NULL; li = li->next) {
		GtkWidget *w = li->data;
		char *name = gtk_object_get_user_data (GTK_OBJECT (w));
		if (GTK_TOGGLE_BUTTON (w)->active) {
			gtk_widget_destroy (dialog);
			return g_strdup (name);
		}
	}

	gtk_widget_destroy (dialog);

	/* should never get here really */
	return NULL;
}

struct poptOption options [] = {
	{ "command", 'c', POPT_ARG_STRING, &send_command, 0, N_("Send the specified protocol command to gdm"), N_("COMMAND") },
	{ "xnest", 'n', POPT_ARG_NONE, &use_xnest, 0, N_("Xnest mode"), NULL },
	{ "no-lock", 'l', POPT_ARG_NONE, &no_lock, 0, N_("Do not lock current screen"), NULL },
	{ "debug", 'd', POPT_ARG_NONE, &debug, 0, N_("Debugging output"), NULL },
	{ "authenticate", 'a', POPT_ARG_NONE, &authenticate, 0, N_("Authenticate before running --command"), NULL },
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0}
};


int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	FILE *fp = NULL;
	long pid;
	char *pidfile;
	char *command;
	char *version;
	char *ret;
	char *message;
	char *auth_cookie = NULL;
	poptContext ctx;
	const char **args;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init_with_popt_table ("gdmflexiserver", VERSION, argc, argv,
				    options, 0, &ctx);

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	pidfile = gnome_config_get_string (GDM_KEY_PIDFILE);
	gnome_config_pop_prefix ();

	args = poptGetArgs (ctx);
	if (args != NULL && args[0] != NULL)
		server = args[0];

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
		dialog = gnome_warning_dialog
			(_("GDM is not running.\n"
			   "Please ask your "
			   "system administrator to start it."));
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		return 1;
	}

	if (access (GDM_SUP_SOCKET, R_OK|W_OK)) {
		dialog = gnome_warning_dialog
			(_("Cannot communicate with gdm, perhaps "
			   "you have an old version running."));
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		return 1;
	}

	if (send_command != NULL) {
		if (authenticate)
			auth_cookie = get_auth_cookie ();
		ret = call_gdm (send_command, auth_cookie,
				"2.2.4.0", 5);
		if (ret != NULL) {
			g_print ("%s\n", ret);
			return 0;
		} else {
			dialog = gnome_warning_dialog
				(_("Cannot communicate with gdm, perhaps "
				   "you have an old version running."));
			gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
			return 1;
		}
	}

	if (use_xnest) {
		char *cookie = get_a_cookie ();
		if (cookie == NULL) {
			dialog = gnome_warning_dialog
				(_("You do not seem to have authentication "
				   "needed be for this operation.  Perhaps "
				   "your .Xauthority file is not set up "
				   "correctly."));
			gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
			return 1;
		}
		command = g_strdup_printf (GDM_SUP_FLEXI_XNEST " %s %s %s",
					   get_display (),
					   cookie,
					   XauFileName ());
		g_free (cookie);
		version = "2.2.4.2";
		auth_cookie = NULL;
	} else {
		auth_cookie = get_auth_cookie ();

		if (auth_cookie == NULL) {
			dialog = gnome_warning_dialog
				(_("You do not seem to be logged in on the "
				   "console.  Starting a new login only "
				   "works correctly on the console."));
			gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
			return 1;
		}

		read_servers ();
		server = choose_server ();
		if (server == NULL)
			command = g_strdup (GDM_SUP_FLEXI_XSERVER);
		else
			command = g_strdup_printf (GDM_SUP_FLEXI_XSERVER " %s",
						   server);
		version = "2.2.4.0";
	}

	ret = call_gdm (command, auth_cookie, version, 5);
	if (ret != NULL &&
	    strncmp (ret, "OK ", 3) == 0) {

		/* if we switched to a different screen as a result of this,
		 * lock the current screen */
		if ( ! no_lock && ! use_xnest) {
			char *argv[3] = {"xscreensaver-command", "-lock", NULL};
			if (gnome_execute_async (g_get_home_dir (), 2, argv) < 0)
				g_warning (_("Can't lock screen"));
			argv[1] = "-throttle";
			if (gnome_execute_async (g_get_home_dir (), 2, argv) < 0)
				g_warning (_("Can't disable xscreensaver display hacks"));
		}

		/* all fine and dandy */
		return 0;
	}

	/* These need a bit more refinement */
	if (ret == NULL) {
		message = _("Cannot communicate with gdm, perhaps "
			    "you have an old version running.");
	} else if (strncmp (ret, "ERROR 0 ", strlen ("ERROR 0 ")) == 0) {
		message = _("Cannot communicate with gdm, perhaps "
			    "you have an old version running.");
	} else if (strncmp (ret, "ERROR 1 ", strlen ("ERROR 1 ")) == 0) {
		message = _("The allowed limit of flexible X servers reached.");
	} else if (strncmp (ret, "ERROR 2 ", strlen ("ERROR 2 ")) == 0) {
		message = _("There were errors trying to start the X server.");
	} else if (strncmp (ret, "ERROR 3 ", strlen ("ERROR 3 ")) == 0) {
		message = _("The X server failed.  Perhaps it is not "
			    "configured well.");
	} else if (strncmp (ret, "ERROR 4 ", strlen ("ERROR 4 ")) == 0) {
		message = _("Too many X sessions running.");
	} else if (strncmp (ret, "ERROR 5 ", strlen ("ERROR 5 ")) == 0) {
		message = _("The nested X server (Xnest) cannot connect to "
			    "your current X server.  You may be missing an "
			    "X authorization file.");
	} else if (strncmp (ret, "ERROR 6 ", strlen ("ERROR 6 ")) == 0) {
		if (use_xnest)
			message = _("The nested X server (Xnest) is not "
				    "available, or gdm is badly configured.\n"
				    "Please install the Xnest package in "
				    "order to use the nested login.");
		else
			message = _("The X server is not available, "
				    "it is likely that gdm is badly "
				    "configured.");
	} else if (strncmp (ret, "ERROR 100 ", strlen ("ERROR 100 ")) == 0) {
		message = _("You do not seem to have authentication needed "
			    "be for this operation.  Perhaps your .Xauthority "
			    "file is not set up correctly.");
	} else {
		message = _("Unknown error occured.");
	}

	dialog = gnome_warning_dialog (message);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	return 1;
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
