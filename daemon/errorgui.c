/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* This is the gdm slave process. gdmslave runs the chooser, greeter
 * and the user's session scripts. */

#include <config.h>
#include <libgnome/libgnome.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "gdm.h"
#include "misc.h"

#include <vicious.h>

#include "errorgui.h"

/* set in the main function */
extern char **stored_argv;
extern int stored_argc;
extern char *stored_path;

static gboolean
gdm_event (GSignalInvocationHint *ihint,
	   guint		n_param_values,
	   const GValue	       *param_values,
	   gpointer		data)
{
	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	GdkEvent *event = g_value_get_pointer ((GValue *)param_values);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}      

void
gdm_error_box (GdmDisplay *d, GtkMessageType type, const char *error)
{
	pid_t pid;

	pid = gdm_fork_extra ();

	if (pid == 0) {
		guint sid;
		int i;
		int argc = 1;
		char **argv;
		GtkWidget *dlg;
		GtkRequisition req;
		int screenx = 0;
		int screeny = 0;
		int screenwidth = 0;
		int screenheight = 0;
		char *loc;

		closelog ();

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		seteuid (getuid ());
		setegid (getgid ());

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-error-box";

		gtk_init (&argc, &argv);

		if (d != NULL) {
			screenx = d->screenx;
			screeny = d->screeny;
			screenwidth = d->screenwidth;
			screenheight = d->screenheight;
		}

		if (screenwidth <= 0)
			screenwidth = gdk_screen_width ();
		if (screenheight <= 0)
			screenheight = gdk_screen_height ();

		loc = g_locale_to_utf8 (error, -1, NULL, NULL, NULL);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      type,
					      GTK_BUTTONS_OK,
					      "%s",
					      loc);

		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		gtk_widget_size_request (dlg, &req);

		gtk_window_move (GTK_WINDOW (dlg),
				 screenx +
				 (screenwidth / 2) -
				 (req.width / 2),
				 screeny +
				 (screenheight / 2) -
				 (req.height / 2));

		gtk_widget_show_now (dlg);

		if (dlg->window != NULL) {
			gdk_error_trap_push ();
			XSetInputFocus (GDK_DISPLAY (),
					GDK_WINDOW_XWINDOW (dlg->window),
					RevertToPointerRoot,
					CurrentTime);
			gdk_flush ();
			gdk_error_trap_pop ();
		}

		gtk_dialog_run (GTK_DIALOG (dlg));

		_exit (0);
	} else if (pid > 0) {
		gdm_wait_for_extra (NULL);
	} else {
		gdm_error (_("gdm_error_box: Cannot fork to display error/info box"));
	}
}

static void
press_ok (GtkWidget *entry, gpointer data)
{
	GtkWidget *dlg = data;
	gtk_dialog_response (GTK_DIALOG (dlg), GTK_RESPONSE_OK);
}

char *
gdm_failsafe_question (GdmDisplay *d,
		       const char *question,
		       gboolean echo)
{
	pid_t pid;
	int p[2];

	if (pipe (p) < 0)
		return NULL;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		guint sid;
		int i;
		int argc = 1;
		char **argv;
		GtkWidget *dlg, *label, *entry;
		GtkRequisition req;
		int screenx = 0;
		int screeny = 0;
		int screenwidth = 0;
		int screenheight = 0;
		char *loc;

		closelog ();

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		seteuid (getuid ());
		setegid (getgid ());

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-failsafe-question";

		gtk_init (&argc, &argv);

		if (d != NULL) {
			screenx = d->screenx;
			screeny = d->screeny;
			screenwidth = d->screenwidth;
			screenheight = d->screenheight;
		}

		if (screenwidth <= 0)
			screenwidth = gdk_screen_width ();
		if (screenheight <= 0)
			screenheight = gdk_screen_height ();

		loc = g_locale_to_utf8 (question, -1, NULL, NULL, NULL);

		dlg = gtk_dialog_new_with_buttons (loc,
						   NULL /* parent */,
						   0 /* flags */,
						   GTK_STOCK_OK,
						   GTK_RESPONSE_OK,
						   NULL);
		g_signal_connect (G_OBJECT (dlg), "delete_event",
				  G_CALLBACK (gtk_true), NULL);

		label = gtk_label_new (loc);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
				    label, FALSE, FALSE, 0);
		entry = gtk_entry_new ();
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
				    entry, FALSE, FALSE, 0);
		if ( ! echo)
			gtk_entry_set_visibility (GTK_ENTRY (entry),
						  FALSE /* visible */);
		g_signal_connect (G_OBJECT (entry), "activate",
				  G_CALLBACK (press_ok), dlg);

		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		gtk_widget_size_request (dlg, &req);

		gtk_window_move (GTK_WINDOW (dlg),
				 screenx +
				 (screenwidth / 2) -
				 (req.width / 2),
				 screeny +
				 (screenheight / 2) -
				 (req.height / 2));

		gtk_widget_show_now (dlg);

		if (dlg->window != NULL) {
			gdk_error_trap_push ();
			XSetInputFocus (GDK_DISPLAY (),
					GDK_WINDOW_XWINDOW (dlg->window),
					RevertToPointerRoot,
					CurrentTime);
			gdk_flush ();
			gdk_error_trap_pop ();
		}

		gtk_widget_grab_focus (entry);

		gtk_dialog_run (GTK_DIALOG (dlg));

		loc = g_locale_from_utf8 (ve_sure_string (gtk_entry_get_text (GTK_ENTRY (entry))),
					  -1, NULL, NULL, NULL);

		g_print ("%s", ve_sure_string (loc));

		_exit (0);
	} else if (pid > 0) {
		char buf[BUFSIZ];
		int bytes;

		close (p[1]);

		gdm_wait_for_extra (NULL);

		bytes = read (p[0], buf, BUFSIZ-1);
		if (bytes > 0) {
			close (p[0]);
			buf[bytes] = '\0';
			return g_strdup (buf);
		} 
		close (p[0]);
	} else {
		gdm_error (_("gdm_failsafe_question: Cannot fork to display error/info box"));
	}
	return NULL;
}

gboolean
gdm_failsafe_yesno (GdmDisplay *d,
		    const char *question)
{
	pid_t pid;
	int p[2];

	if (pipe (p) < 0)
		return FALSE;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		guint sid;
		int i;
		int argc = 1;
		char **argv;
		GtkWidget *dlg;
		GtkRequisition req;
		int screenx = 0;
		int screeny = 0;
		int screenwidth = 0;
		int screenheight = 0;
		char *loc;

		closelog ();

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++) {
			if (p[1] != i)
				close(i);
		}

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		seteuid (getuid ());
		setegid (getgid ());

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-failsafe-yesno";

		gtk_init (&argc, &argv);

		if (d != NULL) {
			screenx = d->screenx;
			screeny = d->screeny;
			screenwidth = d->screenwidth;
			screenheight = d->screenheight;
		}

		if (screenwidth <= 0)
			screenwidth = gdk_screen_width ();
		if (screenheight <= 0)
			screenheight = gdk_screen_height ();

		loc = g_locale_to_utf8 (question, -1, NULL, NULL, NULL);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      GTK_MESSAGE_QUESTION,
					      GTK_BUTTONS_YES_NO,
					      "%s",
					      loc);

		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		gtk_widget_size_request (dlg, &req);

		gtk_window_move (GTK_WINDOW (dlg),
				 screenx +
				 (screenwidth / 2) -
				 (req.width / 2),
				 screeny +
				 (screenheight / 2) -
				 (req.height / 2));

		gtk_widget_show_now (dlg);

		if (dlg->window != NULL) {
			gdk_error_trap_push ();
			XSetInputFocus (GDK_DISPLAY (),
					GDK_WINDOW_XWINDOW (dlg->window),
					RevertToPointerRoot,
					CurrentTime);
			gdk_flush ();
			gdk_error_trap_pop ();
		}

		if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_YES)
			g_print ("yes\n");
		else
			g_print ("no\n");

		_exit (0);
	} else if (pid > 0) {
		char buf[BUFSIZ];
		int bytes;

		close (p[1]);

		gdm_wait_for_extra (NULL);

		bytes = read (p[0], buf, BUFSIZ-1);
		if (bytes > 0) {
			close (p[0]);
			if (buf[0] == 'y')
				return TRUE;
			else
				return FALSE;
		} 
		close (p[0]);
	} else {
		gdm_error (_("gdm_failsafe_question: Cannot fork to display error/info box"));
	}
	return FALSE;
}

/* EOF */
