/* GDM - The GNOME Display Manager
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
#include <unistd.h>
#include <syslog.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/types.h>
#include <signal.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "gdm.h"
#include "misc.h"
#include "auth.h"
#include "gdmconfig.h"
#include "errorgui.h"

/* set in the main function */
extern char **stored_argv;
extern int stored_argc;
extern char *stored_path;

static int screenx = 0;
static int screeny = 0;
static int screenwidth = 0;
static int screenheight = 0;

static gboolean inhibit_gtk_modules = FALSE;
static gboolean inhibit_gtk_themes = FALSE;

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
	int w, h;

	/* sanity, should never happen */
	if (window == NULL)
		return;

	gtk_window_get_size (GTK_WINDOW (window), &w, &h);

	gtk_window_move (GTK_WINDOW (window),
			 screenx +
			 (screenwidth / 2) -
			 (w / 2),
			 screeny +
			 (screenheight / 2) -
			 (h / 2));
}

static void
show_errors (GtkWidget *button, gpointer data)
{
	GtkRequisition req;
	GtkWidget *textsw = data;
	GtkWidget *dlg = g_object_get_data (G_OBJECT (button), "dlg");

	if (GTK_TOGGLE_BUTTON (button)->active) {
		gtk_widget_show (textsw);
	} else {
		gtk_widget_hide (textsw);
	}

	/* keep window at the size request size */
	gtk_widget_size_request (dlg, &req);
	gtk_window_resize (GTK_WINDOW (dlg), req.width, req.height);
}

static GtkWidget *
get_error_text_view (const char *details)
{
	GtkWidget *sw;
	GtkWidget *tv;
	GtkTextBuffer *buf;
	GtkTextIter iter;

	tv = gtk_text_view_new ();
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_buffer_create_tag (buf, "foo",
				    "editable", FALSE,
				    "family", "monospace",
				    NULL);
	gtk_text_buffer_get_iter_at_offset (buf, &iter, 0);

	gtk_text_buffer_insert_with_tags_by_name
		(buf, &iter,
		 ve_sure_string (details), -1,
		 "foo", NULL);

	sw = gtk_scrolled_window_new (NULL, NULL);
	if (gdk_screen_width () >= 800)
		gtk_widget_set_size_request (sw, 500, 150);
	else
		gtk_widget_set_size_request (sw, 200, 150);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_ALWAYS);

	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_IN);

	gtk_container_add (GTK_CONTAINER (sw), tv);
	gtk_widget_show (tv);

	return sw;
}

static void
setup_dialog (GdmDisplay *d, const char *name, int closefdexcept, gboolean set_gdm_ids, uid_t uid)
{
	int argc = 1;
	char **argv;
	struct passwd *pw;

	closelog ();

	gdm_close_all_descriptors (0 /* from */, closefdexcept /* except */, -1 /* except2 */);

	/* No error checking here - if it's messed the best response
	 * is to ignore & try to continue */
	gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
	gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	if (set_gdm_ids) {
		setgid (gdm_get_gdmgid());
		initgroups (gdm_get_value_string (GDM_KEY_USER), gdm_get_gdmgid());
		setuid (gdm_get_gdmuid());
		pw = NULL;
	} else {
		pw = getpwuid (uid);
	}

	gdm_desetuid ();

	/* restore initial environment */
	gdm_restoreenv ();

	openlog ("gdm", LOG_PID, LOG_DAEMON);

	g_setenv ("LOGNAME", gdm_get_value_string (GDM_KEY_USER), TRUE);
	g_setenv ("USER", gdm_get_value_string (GDM_KEY_USER), TRUE);
	g_setenv ("USERNAME", gdm_get_value_string (GDM_KEY_USER), TRUE);

	g_setenv ("DISPLAY", d->name, TRUE);
	g_unsetenv ("XAUTHORITY");

	gdm_auth_set_local_auth (d);

	/* sanity env stuff */
	g_setenv ("SHELL", "/bin/sh", TRUE);
	/* set HOME to /, we don't need no stinking HOME anyway */
	if (pw == NULL ||
	    ve_string_empty (pw->pw_dir))
		g_setenv ("HOME", ve_sure_string (gdm_get_value_string (GDM_KEY_SERV_AUTHDIR)), TRUE);
	else
		g_setenv ("HOME", pw->pw_dir, TRUE);

	argv = g_new0 (char *, 3);
	argv[0] = (char *)name;
	argc = 1;

	if ( ! inhibit_gtk_modules &&
	    gdm_get_value_bool (GDM_KEY_ADD_GTK_MODULES) &&
	     ! ve_string_empty (gdm_get_value_string (GDM_KEY_GTK_MODULES_LIST))) {
		argv[1] = g_strdup_printf ("--gtk-module=%s", gdm_get_value_string (GDM_KEY_GTK_MODULES_LIST));
		argc = 2;
	}

	if (inhibit_gtk_modules) {
		g_unsetenv ("GTK_MODULES");
	}

	gtk_init (&argc, &argv);

	if ( ! inhibit_gtk_themes) {
		const char *theme_name;
                gchar *gtkrc = gdm_get_value_string (GDM_KEY_GTKRC);

		if ( ! ve_string_empty (gtkrc) &&
		     access (gtkrc, R_OK) == 0)
			gtk_rc_parse (gtkrc);

		theme_name = d->theme_name;
		if (ve_string_empty (theme_name))
			theme_name = gdm_get_value_string (GDM_KEY_GTK_THEME);
		if ( ! ve_string_empty (theme_name)) {
			gchar *theme_dir = gtk_rc_get_theme_dir ();
			char *theme = g_strdup_printf ("%s/%s/gtk-2.0/gtkrc", theme_dir, theme_name);
			g_free (theme_dir);

			if ( ! ve_string_empty (theme) &&
			     access (theme, R_OK) == 0)
				gtk_rc_parse (theme);

			g_free (theme);
		}
	}

	get_screen_size (d);
}

static gboolean
dialog_failed (int status)
{
	if (WIFSIGNALED (status) &&
	    (WTERMSIG (status) == SIGTERM ||
	     WTERMSIG (status) == SIGINT ||
	     WTERMSIG (status) == SIGQUIT ||
	     WTERMSIG (status) == SIGHUP)) {
		return FALSE;
	} else if (WIFEXITED (status) &&
		   WEXITSTATUS (status) == 0) {
		return FALSE;
	} else {
		gdm_error ("failsafe dialog failed (inhibitions: %d %d)",
			   inhibit_gtk_modules, inhibit_gtk_themes);
		return TRUE;
	}
}

void
gdm_error_box_full (GdmDisplay *d, GtkMessageType type, const char *error,
		    const char *details_label, const char *details_file,
		    uid_t uid, gid_t gid)
{
	pid_t pid;

	pid = gdm_fork_extra ();

	if (pid == 0) {
		guint sid;
		GtkWidget *dlg;
		GtkWidget *button;
		char *loc;
		char *details;

		if (uid != 0) {
			gid_t groups[1] = { gid };

			/* if we for some reason fail here
			   don't allow the file */
			if G_UNLIKELY (setgid (gid) != 0)
				details_file = NULL;
			if G_UNLIKELY (setgroups (1, groups) != 0)
				details_file = NULL;
			if G_UNLIKELY (setuid (uid) != 0)
				details_file = NULL;

			gdm_desetuid ();
		}

		/* First read the details if they exist */
		if (details_label != NULL && details_file != NULL) {
			FILE *fp;
			struct stat s;
			int r;
			gboolean valid_utf8 = TRUE;
			GString *gs = g_string_new (NULL);

			fp = NULL;
			VE_IGNORE_EINTR (r = lstat (details_file, &s));
			if (r == 0) {
				if (S_ISREG (s.st_mode)) {
					VE_IGNORE_EINTR (fp = fopen (details_file, "r"));
				} else {
					loc = gdm_locale_to_utf8 (_("%s not a regular file!\n"));
					g_string_printf (gs, loc, details_file);
					g_free (loc);
				}
			}
			if (fp != NULL) {
				char buf[256];
				int lines = 0;
				char *getsret;
				VE_IGNORE_EINTR (getsret = fgets (buf, sizeof (buf), fp));
				while (getsret != NULL) {
					if ( ! g_utf8_validate (buf, -1, NULL))
						valid_utf8 = FALSE;
					g_string_append (gs, buf);
					/* cap the lines at 500, that's already
					   a possibility of 128k of data */
					if (lines ++ > 500) {
						loc = gdm_locale_to_utf8 (_("\n... File too long to display ...\n"));
						g_string_append (gs, loc);
						g_free (loc);
						break;
					}
					VE_IGNORE_EINTR (getsret = fgets (buf, sizeof (buf), fp));
				}
				VE_IGNORE_EINTR (fclose (fp));
			} else {
				loc = gdm_locale_to_utf8 (_("%s could not be opened"));
				g_string_append_printf (gs, loc, details_file);
				g_free (loc);
			}

			details = g_string_free (gs, FALSE);

			if ( ! valid_utf8) {
				char *tmp = gdm_locale_to_utf8 (details);
				g_free (details);
				details = tmp;
			}
		} else {
			details = NULL;
		}

		setup_dialog (d, "gtk-error-box", -1,
			      (uid == 0 || uid == gdm_get_gdmuid()) /* set_gdm_ids */,
			      uid);

		loc = gdm_locale_to_utf8 (error);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      type,
					      GTK_BUTTONS_NONE,
					      "%s",
					      loc);
		g_free (loc);
		gtk_widget_set_events (dlg, GDK_ALL_EVENTS_MASK);
		gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

		if (details_label != NULL) {
			GtkWidget *text = get_error_text_view (details);

			loc = gdm_locale_to_utf8 (details_label);
			button = gtk_check_button_new_with_label (loc);
			g_free (loc);

			gtk_widget_show (button);
			g_object_set_data (G_OBJECT (button), "dlg", dlg);
			g_signal_connect (button, "toggled",
					  G_CALLBACK (show_errors),
					  text);

			gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
					    button, FALSE, FALSE, 6);
			gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
					    text, FALSE, FALSE, 6);

			g_signal_connect_after (dlg, "size_allocate",
						G_CALLBACK (center_window),
						NULL);
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
		int status;
		gdm_wait_for_extra (&status);

		if (dialog_failed (status)) {
			if ( ! inhibit_gtk_themes) {
				/* on failure try again, this time without any themes
				   which may be causing a crash */
				inhibit_gtk_themes = TRUE;
				gdm_error_box_full (d, type, error, details_label, details_file, uid, gid);
				inhibit_gtk_themes = FALSE;
			} else if ( ! inhibit_gtk_modules) {
				/* on failure try again, this time without any modules
				   which may be causing a crash */
				inhibit_gtk_modules = TRUE;
				gdm_error_box_full (d, type, error, details_label, details_file, uid, gid);
				inhibit_gtk_modules = FALSE;
			}
		}
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
	gdm_error_box_full (d, type, error, NULL, NULL,
			    /* zero for uid/gid doesn't mean root, but
			       it means to use the gdm user/group */
			    0, 0);
}

char *
gdm_failsafe_question (GdmDisplay *d,
		       const char *question,
		       gboolean echo)
{
	pid_t pid;
	int p[2];

	if G_UNLIKELY (pipe (p) < 0)
		return NULL;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		guint sid;
		GtkWidget *dlg, *label, *entry;
		char *loc;

		setup_dialog (d, "gtk-failsafe-question", p[1], TRUE /* set_gdm_ids */, 0);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_dialog_new_with_buttons (loc,
						   NULL /* parent */,
						   0 /* flags */,
						   GTK_STOCK_OK,
						   GTK_RESPONSE_OK,
						   NULL);
		gtk_widget_set_events (dlg, GDK_ALL_EVENTS_MASK);
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
		int status;
		char buf[BUFSIZ];
		int bytes;

		VE_IGNORE_EINTR (close (p[1]));

		gdm_wait_for_extra (&status);

		if (dialog_failed (status)) {
			char *ret = NULL;
			VE_IGNORE_EINTR (close (p[0]));
			if ( ! inhibit_gtk_themes) {
				/* on failure try again, this time without any themes
				   which may be causing a crash */
				inhibit_gtk_themes = TRUE;
				ret = gdm_failsafe_question (d, question, echo);
				inhibit_gtk_themes = FALSE;
			} else if ( ! inhibit_gtk_modules) {
				/* on failure try again, this time without any modules
				   which may be causing a crash */
				inhibit_gtk_modules = TRUE;
				ret = gdm_failsafe_question (d, question, echo);
				inhibit_gtk_modules = FALSE;
			}
			return ret;
		}

		VE_IGNORE_EINTR (bytes = read (p[0], buf, BUFSIZ-1));
		if (bytes > 0) {
			VE_IGNORE_EINTR (close (p[0]));
			buf[bytes] = '\0';
			return g_strdup (buf);
		} 
		VE_IGNORE_EINTR (close (p[0]));
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

	if G_UNLIKELY (pipe (p) < 0)
		return FALSE;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		guint sid;
		GtkWidget *dlg;
		char *loc;

		setup_dialog (d, "gtk-failsafe-yesno", p[1], TRUE /* set_gdm_ids */, 0);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      GTK_MESSAGE_QUESTION,
					      GTK_BUTTONS_YES_NO,
					      "%s",
					      loc);
		gtk_widget_set_events (dlg, GDK_ALL_EVENTS_MASK);
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
		int status;
		char buf[BUFSIZ];
		int bytes;

		VE_IGNORE_EINTR (close (p[1]));

		gdm_wait_for_extra (&status);

		if (dialog_failed (status)) {
			gboolean ret = FALSE;
			VE_IGNORE_EINTR (close (p[0]));
			if ( ! inhibit_gtk_themes) {
				/* on failure try again, this time without any themes
				   which may be causing a crash */
				inhibit_gtk_themes = TRUE;
				ret = gdm_failsafe_yesno (d, question);
				inhibit_gtk_themes = FALSE;
			} else if ( ! inhibit_gtk_modules) {
				/* on failure try again, this time without any modules
				   which may be causing a crash */
				inhibit_gtk_modules = TRUE;
				ret = gdm_failsafe_yesno (d, question);
				inhibit_gtk_modules = FALSE;
			}
			return ret;
		}

		VE_IGNORE_EINTR (bytes = read (p[0], buf, BUFSIZ-1));
		if (bytes > 0) {
			VE_IGNORE_EINTR (close (p[0]));
			if (buf[0] == 'y')
				return TRUE;
			else
				return FALSE;
		} 
		VE_IGNORE_EINTR (close (p[0]));
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

	if G_UNLIKELY (pipe (p) < 0)
		return -1;

	pid = gdm_fork_extra ();
	if (pid == 0) {
		int i;
		guint sid;
		GtkWidget *dlg;
		char *loc;

		setup_dialog (d, "gtk-failsafe-ask-buttons", p[1], TRUE /* set_gdm_ids */, 0);

		loc = gdm_locale_to_utf8 (question);

		dlg = gtk_message_dialog_new (NULL /* parent */,
					      0 /* flags */,
					      GTK_MESSAGE_QUESTION,
					      GTK_BUTTONS_NONE,
					      "%s",
					      loc);
		g_free (loc);
		gtk_widget_set_events (dlg, GDK_ALL_EVENTS_MASK);
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
		int status;
		char buf[BUFSIZ];
		int bytes;

		VE_IGNORE_EINTR (close (p[1]));

		gdm_wait_for_extra (&status);

		if (dialog_failed (status)) {
			int ret = -1;
			VE_IGNORE_EINTR (close (p[0]));
			if ( ! inhibit_gtk_themes) {
				/* on failure try again, this time without any themes
				   which may be causing a crash */
				inhibit_gtk_themes = TRUE;
				ret = gdm_failsafe_ask_buttons (d, question, but);
				inhibit_gtk_themes = FALSE;
			} else if ( ! inhibit_gtk_modules) {
				/* on failure try again, this time without any modules
				   which may be causing a crash */
				inhibit_gtk_modules = TRUE;
				ret = gdm_failsafe_ask_buttons (d, question, but);
				inhibit_gtk_modules = FALSE;
			}
			return ret;
		}

		VE_IGNORE_EINTR (bytes = read (p[0], buf, BUFSIZ-1));
		if (bytes > 0) {
			int i;
			VE_IGNORE_EINTR (close (p[0]));
			buf[bytes] = '\0';
			if (sscanf (buf, "%d", &i) == 1)
				return i;
			else
				return -1;
		} 
		VE_IGNORE_EINTR (close (p[0]));
	} else {
		gdm_error (_("%s: Cannot fork to display error/info box"),
			   "gdm_failsafe_ask_buttons");
	}
	return -1;
}

/* EOF */
