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
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "gdm.h"
#include "misc.h"

#include <vicious.h>

#include "errorgui.h"

/* set in the main function */
extern char **stored_argv;
extern int stored_argc;
extern char *stored_path;

/* Configuration option variables */
extern gchar *GdmUser;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;

static int screenx = 0;
static int screeny = 0;
static int screenwidth = 0;
static int screenheight = 0;

static void
setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

static gboolean
gdm_event (GSignalInvocationHint *ihint,
	   guint		n_param_values,
	   const GValue	       *param_values,
	   gpointer		data)
{
	GdkEvent *event;

	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	if (n_param_values != 2 ||
	    !G_VALUE_HOLDS (&param_values[1], GDK_TYPE_EVENT))
	  return FALSE;
	
	event = g_value_get_boxed (&param_values[1]);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}

static void
get_screen_size (GdmDisplay *d)
{
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
}

static void
center_window (GtkWidget *window)
{
	GtkRequisition req;

	gtk_widget_size_request (window, &req);

	gtk_window_move (GTK_WINDOW (window),
			 screenx +
			 (screenwidth / 2) -
			 (req.width / 2),
			 screeny +
			 (screenheight / 2) -
			 (req.height / 2));
}

static void
show_errors (GtkWidget *button, gpointer data)
{
	const char *details = data;
	GtkWidget *sw;
	GtkWidget *label;
	GtkWidget *dlg = gtk_widget_get_toplevel (button);
	GtkWidget *parent = button->parent;

	gtk_widget_destroy (button);

	sw = gtk_scrolled_window_new (NULL, NULL);
	if (gdk_screen_width () >= 800)
		gtk_widget_set_size_request (sw, 500, 150);
	else
		gtk_widget_set_size_request (sw, 200, 150);
	gtk_widget_show (sw);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_ALWAYS);

	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_IN);

	label = gtk_label_new (ve_sure_string (details));
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (sw), label);
	gtk_widget_show (label);

	gtk_box_pack_start (GTK_BOX (parent),
			    sw, TRUE, TRUE, 0);

	center_window (dlg);
}

gboolean g_file_get_contents (const gchar  *filename,
                              gchar       **contents,
                              gsize        *length,    
                              GError      **error);



void
gdm_error_box_full (GdmDisplay *d, GtkMessageType type, const char *error,
		    const char *details_label, const char *details_file)
{
	pid_t pid;

	pid = gdm_fork_extra ();

	if (pid == 0) {
		guint sid;
		int argc = 1;
		char **argv;
		GtkWidget *dlg;
		GtkWidget *button;
		char *loc;
		char *display;
		char *xauthority;
		char *details;
		
		/* First read the details if they exist */
		if (details_file) {
			FILE *fp;
			struct stat s;
			GString *gs = g_string_new (NULL);

			fp = NULL;
			if (stat (details_file, &s) == 0) {
				if (S_ISREG (s.st_mode))
					fp = fopen (details_file, "r");
				else
					g_string_printf (gs, _("%s not a regular file!\n"), details_file);
			}
			if (fp != NULL) {
				char buf[256];
				int lines = 0;
				while (fgets (buf, sizeof (buf), fp)) {
					g_string_append (gs, buf);
					/* cap the lines at 500, that's already
					   a possibility of 128k of crap */
					if (lines ++ > 500) {
						g_string_append (gs, _("\n... File too long to display ...\n"));
						break;
					}
				}
				fclose (fp);
			} else {
				g_string_append_printf (gs, _("%s could not be opened"), details_file);
			}

			details = g_string_free (gs, FALSE);
		} else {
			details = NULL;
		}

		closelog ();

		gdm_close_all_descriptors (0 /* from */, -1 /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		setgid (GdmGroupId);
		initgroups (GdmUser, GdmGroupId);
		setuid (GdmUserId);

		gdm_desetuid ();

		display = g_strdup (g_getenv ("DISPLAY"));
		xauthority = g_strdup (g_getenv ("XAUTHORITY"));

		/* restore initial environment */
		gdm_restoreenv ();

		if (display != NULL)
			gnome_setenv ("DISPLAY", display, TRUE);
		if (xauthority != NULL)
			gnome_setenv ("XAUTHORITY", xauthority, TRUE);
		/* sanity env stuff */
		gnome_setenv ("SHELL", "/bin/sh", TRUE);
		gnome_setenv ("HOME", "/tmp", TRUE);

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-error-box";

		gtk_init (&argc, &argv);

		get_screen_size (d);

		loc = gdm_locale_to_utf8 (error);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      type,
					      GTK_BUTTONS_NONE,
					      "%s",
					      loc);
		g_free (loc);
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

		if (details_label != NULL) {
			loc = gdm_locale_to_utf8 (details_label);
			button = gtk_button_new_with_label (loc);
			g_free (loc);

			gtk_widget_show (button);
			g_signal_connect (button, "clicked",
					  G_CALLBACK (show_errors),
					  /* leak? who cares we exit right
					   * away */
					  details);

			gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
					    button, FALSE, FALSE, 3);
		}

		button = gtk_dialog_add_button (GTK_DIALOG (dlg),
						GTK_STOCK_OK,
						GTK_RESPONSE_OK);
		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		center_window (dlg);

		gtk_widget_grab_focus (button);

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

		setup_cursor (GDK_LEFT_PTR);

		gtk_dialog_run (GTK_DIALOG (dlg));

		XSetInputFocus (GDK_DISPLAY (),
				PointerRoot,
				RevertToPointerRoot,
				CurrentTime);

		_exit (0);
	} else if (pid > 0) {
		gdm_wait_for_extra (NULL);
	} else {
		gdm_error (_("%s: Cannot fork to display error/info box"),
			   "gdm_error_box");
	}
}

static void
press_ok (GtkWidget *entry, gpointer data)
{
	GtkWidget *dlg = data;
	gtk_dialog_response (GTK_DIALOG (dlg), GTK_RESPONSE_OK);
}

void
gdm_error_box (GdmDisplay *d, GtkMessageType type, const char *error)
{
	gdm_error_box_full (d, type, error, NULL, NULL);
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
		int argc = 1;
		char **argv;
		GtkWidget *dlg, *label, *entry;
		char *loc;
		char *display;
		char *xauthority;

		closelog ();

		gdm_close_all_descriptors (0 /* from */, p[1] /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		setgid (GdmGroupId);
		initgroups (GdmUser, GdmGroupId);
		setuid (GdmUserId);

		gdm_desetuid ();

		display = g_strdup (g_getenv ("DISPLAY"));
		xauthority = g_strdup (g_getenv ("XAUTHORITY"));

		/* restore initial environment */
		gdm_restoreenv ();

		if (display != NULL)
			gnome_setenv ("DISPLAY", display, TRUE);
		if (xauthority != NULL)
			gnome_setenv ("XAUTHORITY", xauthority, TRUE);
		/* sanity env stuff */
		gnome_setenv ("SHELL", "/bin/sh", TRUE);
		gnome_setenv ("HOME", "/tmp", TRUE);

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-failsafe-question";

		gtk_init (&argc, &argv);

		get_screen_size (d);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_dialog_new_with_buttons (loc,
						   NULL /* parent */,
						   0 /* flags */,
						   GTK_STOCK_OK,
						   GTK_RESPONSE_OK,
						   NULL);
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
		g_signal_connect (G_OBJECT (dlg), "delete_event",
				  G_CALLBACK (gtk_true), NULL);

		label = gtk_label_new (loc);
		gtk_widget_show_all (label);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
				    label, FALSE, FALSE, 0);
		entry = gtk_entry_new ();
		gtk_widget_show_all (entry);
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

		center_window (dlg);

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

		setup_cursor (GDK_LEFT_PTR);

		gtk_dialog_run (GTK_DIALOG (dlg));

		loc = gdm_locale_from_utf8 (ve_sure_string (gtk_entry_get_text (GTK_ENTRY (entry))));

		gdm_fdprintf (p[1], "%s", ve_sure_string (loc));

		XSetInputFocus (GDK_DISPLAY (),
				PointerRoot,
				RevertToPointerRoot,
				CurrentTime);

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
		gdm_error (_("%s: Cannot fork to display error/info box"),
			   "gdm_failsafe_question");
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
		int argc = 1;
		char **argv;
		GtkWidget *dlg;
		char *loc;
		char *display;
		char *xauthority;

		closelog ();

		gdm_close_all_descriptors (0 /* from */, p[1] /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		setgid (GdmGroupId);
		initgroups (GdmUser, GdmGroupId);
		setuid (GdmUserId);

		gdm_desetuid ();

		display = g_strdup (g_getenv ("DISPLAY"));
		xauthority = g_strdup (g_getenv ("XAUTHORITY"));

		/* restore initial environment */
		gdm_restoreenv ();

		if (display != NULL)
			gnome_setenv ("DISPLAY", display, TRUE);
		if (xauthority != NULL)
			gnome_setenv ("XAUTHORITY", xauthority, TRUE);
		/* sanity env stuff */
		gnome_setenv ("SHELL", "/bin/sh", TRUE);
		gnome_setenv ("HOME", "/tmp", TRUE);

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-failsafe-yesno";

		gtk_init (&argc, &argv);

		get_screen_size (d);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      GTK_MESSAGE_QUESTION,
					      GTK_BUTTONS_YES_NO,
					      "%s",
					      loc);
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		center_window (dlg);

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

		setup_cursor (GDK_LEFT_PTR);

		if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_YES)
			gdm_fdprintf (p[1], "yes\n");
		else
			gdm_fdprintf (p[1], "no\n");

		XSetInputFocus (GDK_DISPLAY (),
				PointerRoot,
				RevertToPointerRoot,
				CurrentTime);

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
		gdm_error (_("%s: Cannot fork to display error/info box"),
			   "gdm_failsafe_yesno");
	}
	return FALSE;
}

int
gdm_failsafe_ask_buttons (GdmDisplay *d,
			  const char *question,
			  char **but)
{
	pid_t pid;
	int p[2];

	if (pipe (p) < 0)
		return -1;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		int i;
		guint sid;
		int argc = 1;
		char **argv;
		GtkWidget *dlg;
		char *loc;
		char *display;
		char *xauthority;

		closelog ();

		gdm_close_all_descriptors (0 /* from */, p[1] /* except */);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		setgid (GdmGroupId);
		initgroups (GdmUser, GdmGroupId);
		setuid (GdmUserId);

		gdm_desetuid ();

		display = g_strdup (g_getenv ("DISPLAY"));
		xauthority = g_strdup (g_getenv ("XAUTHORITY"));

		/* restore initial environment */
		gdm_restoreenv ();

		if (display != NULL)
			gnome_setenv ("DISPLAY", display, TRUE);
		if (xauthority != NULL)
			gnome_setenv ("XAUTHORITY", xauthority, TRUE);
		/* sanity env stuff */
		gnome_setenv ("SHELL", "/bin/sh", TRUE);
		gnome_setenv ("HOME", "/tmp", TRUE);

		openlog ("gdm", LOG_PID, LOG_DAEMON);

		argv = g_new0 (char *, 2);
		argv[0] = "gtk-failsafe-ask-buttons";

		gtk_init (&argc, &argv);

		get_screen_size (d);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      GTK_MESSAGE_QUESTION,
					      GTK_BUTTONS_NONE,
					      "%s",
					      loc);
		g_free (loc);
		for (i = 0; but[i] != NULL; i++) {
			loc = gdm_locale_to_utf8 (but[i]);
			gtk_dialog_add_button (GTK_DIALOG (dlg),
					       loc, i);
			g_free (loc);

		}
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

		sid = g_signal_lookup ("event",
				       GTK_TYPE_WIDGET);
		g_signal_add_emission_hook (sid,
					    0 /* detail */,
					    gdm_event,
					    NULL /* data */,
					    NULL /* destroy_notify */);

		center_window (dlg);

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

		setup_cursor (GDK_LEFT_PTR);

		i = gtk_dialog_run (GTK_DIALOG (dlg));
		gdm_fdprintf (p[1], "%d\n", i);

		XSetInputFocus (GDK_DISPLAY (),
				PointerRoot,
				RevertToPointerRoot,
				CurrentTime);

		_exit (0);
	} else if (pid > 0) {
		char buf[BUFSIZ];
		int bytes;

		close (p[1]);

		gdm_wait_for_extra (NULL);

		bytes = read (p[0], buf, BUFSIZ-1);
		if (bytes > 0) {
			int i;
			close (p[0]);
			buf[bytes] = '\0';
			if (sscanf (buf, "%d", &i) == 1)
				return i;
			else
				return -1;
		} 
		close (p[0]);
	} else {
		gdm_error (_("%s: Cannot fork to display error/info box"),
			   "gdm_failsafe_ask_buttons");
	}
	return -1;
}

/* EOF */
