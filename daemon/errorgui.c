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
#include <gnome.h>
#include <gdk/gdkx.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "gdm.h"
#include "misc.h"

#include "errorgui.h"

/* set in the main function */
extern char **stored_argv;
extern int stored_argc;
extern char *stored_path;
extern pid_t extra_process;

static gboolean
gdm_event (GtkObject *object,
	   guint signal_id,
	   guint n_params,
	   GtkArg *params,
	   gpointer data)
{
	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	GdkEvent *event = GTK_VALUE_POINTER(params[0]);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}      

void
gdm_run_errorgui (const char *error,
		  const char *dialog_type,
		  int screenx,
		  int screeny,
		  int screenwidth,
		  int screenheight)
{
	GtkWidget *dialog;
	GtkRequisition req;
	guint sid;
	char **argv = g_new0 (char *, 2);
	argv[0] = "gdm-error-box";

	/* Avoid creating ~gdm/.gnome stuff */
	gnome_do_not_create_directories = TRUE;

	gnome_init ("gdm-error-box", VERSION, 1, argv);

	sid = gtk_signal_lookup ("event",
				 GTK_TYPE_WIDGET);
	gtk_signal_add_emission_hook (sid,
				      gdm_event,
				      NULL);

	dialog = gnome_message_box_new (error,
					dialog_type,
					GNOME_STOCK_BUTTON_OK,
					NULL);
	gtk_widget_show (dialog);

	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC(gtk_main_quit),
			    NULL);

	gtk_widget_size_request (dialog, &req);

	if (screenwidth <= 0)
		screenwidth = gdk_screen_width ();
	if (screenheight <= 0)
		screenheight = gdk_screen_height ();

	gtk_widget_set_uposition (dialog,
				  screenx +
				  (screenwidth / 2) -
				  (req.width / 2),
				  screeny +
				  (screenheight / 2) -
				  (req.height / 2));

	gtk_widget_show_now (dialog);

	if (dialog->window != NULL) {
		gdk_error_trap_push ();
		XSetInputFocus (GDK_DISPLAY (),
				GDK_WINDOW_XWINDOW (dialog->window),
				RevertToPointerRoot,
				CurrentTime);
		gdk_flush ();
		gdk_error_trap_pop ();
	}

	gtk_main ();
}

void
gdm_error_box (GdmDisplay *d, const char *dialog_type, const char *error)
{
	pid_t pid;

	extra_process = pid = fork ();
	if (pid == 0) {
		char *geom;
		int i;

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best response
		* is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */
	       
		if (d != NULL)
			geom = g_strdup_printf ("%d:%d:%d:%d",
						d->screenx,
						d->screeny,
						d->screenwidth,
						d->screenheight);
		else
			geom = "0:0:0:0";

		if (stored_path != NULL)
			putenv (stored_path);
		execlp (stored_argv[0],
			stored_argv[0],
			"--run-error-dialog",
			error,
			dialog_type,
			geom,
			NULL);
		gdm_error (_("gdm_error_box: Failed to execute self"));
		_exit (1);
	} else if (pid > 0) {
		waitpid (pid, 0, 0);
		extra_process = -1;
	} else {
		gdm_error (_("gdm_error_box: Cannot fork to display error/info box"));
	}
}

/* EOF */
