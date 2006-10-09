/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <locale.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#include "vicious.h"

#include "gdm.h"
#include "gdmuser.h"
#include "gdmcomm.h"
#include "gdmcommon.h"
#include "gdmsession.h"
#include "gdmwm.h"
#include "gdmlanguages.h"
#include "gdmconfig.h"
#include "misc.h"

/* set the DOING_GDM_DEVELOPMENT env variable if you aren't running
 * within the protocol */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;
static gboolean browser_ok = TRUE;
static gboolean disable_sys_config_chooser_buttons = FALSE;
static gboolean GdmLockPosition = FALSE;
static gboolean GdmSetPosition = FALSE;
static gint GdmPositionX;
static gint GdmPositionY;

#define LAST_LANGUAGE "Last"
#define DEFAULT_LANGUAGE "Default"
#define GTK_KEY "gtk-2.0"

enum {
	GREETER_ULIST_ICON_COLUMN = 0,
	GREETER_ULIST_LABEL_COLUMN,
	GREETER_ULIST_LOGIN_COLUMN
};

enum {
	GDM_BACKGROUND_NONE = 0,
	GDM_BACKGROUND_IMAGE_AND_COLOR = 1,
	GDM_BACKGROUND_COLOR = 2,
	GDM_BACKGROUND_IMAGE = 3,
};

static GtkWidget *login;
static GtkWidget *logo_frame = NULL;
static GtkWidget *logo_image = NULL;
static GtkWidget *table = NULL;
static GtkWidget *welcome;
static GtkWidget *label;
static GtkWidget *icon_button = NULL;
static GtkWidget *title_box = NULL;
static GtkWidget *clock_label = NULL;
static GtkWidget *entry;
static GtkWidget *ok_button;
static GtkWidget *cancel_button;
static GtkWidget *msg;
static GtkWidget *auto_timed_msg;
static GtkWidget *err_box;
static guint err_box_clear_handler = 0;
static gboolean require_quarter = FALSE;
static GtkWidget *icon_win = NULL;
static GtkWidget *sessmenu;
static GtkWidget *langmenu;

static gboolean login_is_local = FALSE;

static GtkWidget *browser;
static GtkTreeModel *browser_model;
static GdkPixbuf *defface;

/* Eew. Loads of global vars. It's hard to be event controlled while maintaining state */
static GSList *languages = NULL;
static GList *users = NULL;
static GList *users_string = NULL;
static gint size_of_users = 0;

static const gchar *curlang = NULL;
static gchar *curuser = NULL;
static gchar *session = NULL;
static gchar *language = NULL;

static gint savelang = GTK_RESPONSE_NO;

/* back_prog_timeout_event_id: event of the timer.
 * back_prog_watcher_event_id: event of the background program watcher.
 * back_prog_pid: 	       process ID of the background program.
 * back_prog_has_run:	       true if the background program has run
 *  			       at least once.
 * back_prog_watching_events:  true if we are watching for user events.
 * back_prog_delayed: 	       true if the execution of the program has
 *                             been delayed.
 */
static gint back_prog_timeout_event_id = -1;
static gint back_prog_watcher_event_id = -1;
static gint back_prog_pid = -1;
static gboolean back_prog_has_run = FALSE;
static gboolean back_prog_watching_events = FALSE;
static gboolean back_prog_delayed = FALSE;

static guint timed_handler_id = 0;

#if FIXME
static char *selected_browser_user = NULL;
#endif
static gboolean  selecting_user = TRUE;
static gchar    *selected_user = NULL;

extern GList *sessions;
extern GHashTable *sessnames;
extern gchar *default_session;
extern const gchar *current_session;
extern gboolean session_dir_whacked_out;
extern gint gdm_timed_delay;

static void login_window_resize (gboolean force);

/* Background program logic */
static void back_prog_on_exit (GPid pid, gint status, gpointer data);
static gboolean back_prog_on_timeout (gpointer data);
static gboolean back_prog_delay_timeout (GSignalInvocationHint *ihint, 
					 guint n_param_values, 
					 const GValue *param_values, 
					 gpointer data);
static void back_prog_watch_events (void);
static gchar * back_prog_get_path (void);
static void back_prog_launch_after_timeout (void);
static void back_prog_run (void);
static void back_prog_stop (void);

static void process_operation (guchar op_code, const gchar *args);

/* 
 * This function is called when the background program exits.
 * It will add a timer to restart the program after the
 * restart delay has elapsed, if this is enabled.
 */
static void 
back_prog_on_exit (GPid pid, gint status, gpointer data)
{
	g_assert (back_prog_timeout_event_id == -1);
	
	back_prog_watcher_event_id = -1;
	back_prog_pid = -1;
	
	back_prog_launch_after_timeout ();
}

/* 
 * This function starts the background program (if any) when
 * the background program timer triggers, unless the execution
 * has been delayed.
 */
static gboolean 
back_prog_on_timeout (gpointer data)
{
	g_assert (back_prog_watcher_event_id == -1);
	g_assert (back_prog_pid == -1);
	
	back_prog_timeout_event_id = -1;
	
	if (back_prog_delayed) {
	 	back_prog_launch_after_timeout ();
	} else {
		back_prog_run ();
	}
	
	return FALSE;
}

/*
 * This function is called to delay the execution of the background
 * program when the user is doing something (when we detect an event).
 */
static gboolean
back_prog_delay_timeout (GSignalInvocationHint *ihint,
	       		 guint n_param_values,
	       		 const GValue *param_values,
	       		 gpointer data)
{
	back_prog_delayed = TRUE;
	return TRUE;
}

/*
 * This function creates signal listeners to catch user events.
 * That allows us to avoid spawning the background program
 * when the user is doing something.
 */
static void
back_prog_watch_events (void)
	{
	guint sid;
	
	if (back_prog_watching_events)
		return;
	
	back_prog_watching_events = TRUE;
	
	sid = g_signal_lookup ("activate", GTK_TYPE_MENU_ITEM);
	g_signal_add_emission_hook (sid, 0, back_prog_delay_timeout, 
				    NULL, NULL);

	sid = g_signal_lookup ("key_press_event", GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid, 0, back_prog_delay_timeout, 
				    NULL, NULL);

	sid = g_signal_lookup ("button_press_event", GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sid, 0, back_prog_delay_timeout, 
				    NULL, NULL);
	}

/*
 * This function returns the path of the background program
 * if there is one. Otherwise, NULL is returned.
 */
static gchar *
back_prog_get_path (void)
{
	gchar *backprog = gdm_config_get_string (GDM_KEY_BACKGROUND_PROGRAM);

	if ((gdm_config_get_int (GDM_KEY_BACKGROUND_TYPE) == GDM_BACKGROUND_NONE ||
	     gdm_config_get_bool (GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS)) &&
	    ! ve_string_empty (backprog)) {
		return backprog;
	} else 
		return NULL;
}

/* 
 * This function creates a timer to start the background 
 * program after the requested delay (in seconds) has elapsed.
 */ 
static void 
back_prog_launch_after_timeout ()
{
	int timeout;
	
	g_assert (back_prog_timeout_event_id == -1);
	g_assert (back_prog_watcher_event_id == -1);
	g_assert (back_prog_pid == -1);
	
	/* No program to run. */
	if (! back_prog_get_path ())
		return;
	
	/* First time. */
	if (! back_prog_has_run) {
		timeout = gdm_config_get_int (GDM_KEY_BACKGROUND_PROGRAM_INITIAL_DELAY);
		
	/* Already run, but we are allowed to restart it. */
	} else if (gdm_config_get_bool (GDM_KEY_RESTART_BACKGROUND_PROGRAM)) {
		timeout = gdm_config_get_int (GDM_KEY_BACKGROUND_PROGRAM_RESTART_DELAY);
	
	/* Already run, but we are not allowed to restart it. */
	} else {
		return;
	}
	
	back_prog_delayed = FALSE;
	back_prog_watch_events ();
	back_prog_timeout_event_id = g_timeout_add (timeout * 1000,
						    back_prog_on_timeout,
						    NULL);
}

/* 
 * This function starts the background program (if any).
 */
static void
back_prog_run (void)
{
	GPid pid = -1;
	GError *error = NULL;
	gchar *command = NULL;
	gchar **back_prog_argv = NULL;
	
	g_assert (back_prog_timeout_event_id == -1);
	g_assert (back_prog_watcher_event_id == -1);
	g_assert (back_prog_pid == -1);
	
	command = back_prog_get_path ();
	if (! command)
		return;

        gdm_common_debug ("Running background program <%s>", command);
	
	/* Focus new windows. We want to give focus to the background program. */
	gdm_wm_focus_new_windows (TRUE);
		
	back_prog_argv = ve_split (command);	
	
	/* Don't reap child automatically: we want to catch the event. */
	if (! g_spawn_async (".", 
			     back_prog_argv, 
			     NULL, 
			     (GSpawnFlags) (G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), 
			     NULL, 
			     NULL, 
			     &pid, 
			     &error)) {
			    
		GtkWidget *dialog;
		gchar *msg;
		
                gdm_common_debug ("Cannot run background program %s : %s", command, error->message);
		msg = g_strdup_printf (_("Cannot run command '%s': %s."),
		                       command,
		                       error->message);
					    
		dialog = ve_hig_dialog_new (NULL,
					    GTK_DIALOG_MODAL,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_OK,
					    _("Cannot start background application"),
					    msg);
		g_free (msg);
		
		gtk_widget_show_all (dialog);
		gdm_wm_center_window (GTK_WINDOW (dialog));

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		g_error_free (error);
		g_strfreev (back_prog_argv);
		
		return;
	}
	
	g_strfreev (back_prog_argv);
	back_prog_watcher_event_id = g_child_watch_add (pid, 
							back_prog_on_exit,
							NULL);
	back_prog_pid = pid;
	back_prog_has_run = TRUE;
}

/*
 * This function stops the background program if it is running,
 * and removes any associated timer or watcher.
 */
static void 
back_prog_stop (void)
{
	if (back_prog_timeout_event_id != -1) {
		GSource *source = g_main_context_find_source_by_id
					(NULL, back_prog_timeout_event_id);
		if (source != NULL)
			g_source_destroy (source);
			
		back_prog_timeout_event_id = -1;
	}
	
	if (back_prog_watcher_event_id != -1) {
		GSource *source = g_main_context_find_source_by_id
					(NULL, back_prog_watcher_event_id);
		if (source != NULL)
			g_source_destroy (source);
			
		back_prog_watcher_event_id = -1;
	}
	
	if (back_prog_pid != -1) {		
		if (kill (back_prog_pid, SIGTERM) == 0) {
			waitpid (back_prog_pid, NULL, 0);
		}

		back_prog_pid = -1;
	}
}

/*
 * Timed Login: Timer
 */
static gboolean
gdm_timer (gpointer data)
{
	if (gdm_timed_delay <= 0) {
		/* timed interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
		fflush (stdout);
	} else {
		gchar *autologin_msg;

		/* Note that this message is not handled the same way as in
		 * the greeter, we don't parse it through the enriched text.
		 */
		autologin_msg = gdm_common_expand_text (
			_("User %u will login in %t"));
		gtk_label_set_text (GTK_LABEL (auto_timed_msg), autologin_msg);
		gtk_widget_show (GTK_WIDGET (auto_timed_msg));
		g_free (autologin_msg);
		login_window_resize (FALSE /* force */);
	}

	gdm_timed_delay--;
	return TRUE;
}

/*
 * Timed Login: On GTK events, increase delay to at least 30
 * seconds, or the GDM_KEY_TIMED_LOGIN_DELAY, whichever is higher
 */
static gboolean
gdm_timer_up_delay (GSignalInvocationHint *ihint,
		    guint	           n_param_values,
		    const GValue	  *param_values,
		    gpointer		   data)
{
	if (gdm_timed_delay < 30)
		gdm_timed_delay = 30;
	if (gdm_timed_delay < gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY))
		gdm_timed_delay = gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);
	return TRUE;
}

/* The reaping stuff */
static time_t last_reap_delay = 0;

static gboolean
delay_reaping (GSignalInvocationHint *ihint,
	       guint	           n_param_values,
	       const GValue	  *param_values,
	       gpointer		   data)
{
	last_reap_delay = time (NULL);
	return TRUE;
}      

static void
gdm_kill_thingies (void)
{
	back_prog_stop ();
}

static gboolean
reap_flexiserver (gpointer data)
{
	int reapminutes = gdm_config_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES);

	if (reapminutes > 0 &&
	    ((time (NULL) - last_reap_delay) / 60) > reapminutes) {
		gdm_kill_thingies ();
		_exit (DISPLAY_REMANAGE);
	}
	return TRUE;
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

	/* Support Ctrl-U for blanking the username/password entry */
	if (event->type == GDK_KEY_PRESS &&
	    (event->key.state & GDK_CONTROL_MASK) &&
	    (event->key.keyval == GDK_u ||
	     event->key.keyval == GDK_U)) {

		gtk_entry_set_text (GTK_ENTRY (entry), "");
	}

	return TRUE;
}      

static void
gdm_login_done (int sig)
{
	gdm_kill_thingies ();
	_exit (EXIT_SUCCESS);
}

static void
set_screen_pos (GtkWidget *widget, int x, int y)
{
	int width, height;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (widget));

	gtk_window_get_size (GTK_WINDOW (widget), &width, &height);

	/* allow negative values, to be like standard X geometry ones */
	if (x < 0)
		x = gdm_wm_screen.width + x - width;
	if (y < 0)
		y = gdm_wm_screen.height + y - height;

	if (x < gdm_wm_screen.x)
		x = gdm_wm_screen.x;
	if (y < gdm_wm_screen.y)
		y = gdm_wm_screen.y;
	if (x > gdm_wm_screen.x + gdm_wm_screen.width - width)
		x = gdm_wm_screen.x + gdm_wm_screen.width - width;
	if (y > gdm_wm_screen.y + gdm_wm_screen.height - height)
		y = gdm_wm_screen.y + gdm_wm_screen.height - height;

	gtk_window_move (GTK_WINDOW (widget), x, y);
}

static guint set_pos_id = 0;

static gboolean
set_pos_idle (gpointer data)
{
	if (GdmSetPosition) {
		set_screen_pos (login, GdmPositionX, GdmPositionY);
	} else {
		gdm_wm_center_window (GTK_WINDOW (login));
	}
	set_pos_id = 0;
	return FALSE;
}

static void
login_window_resize (gboolean force)
{
	/* allow opt out if we don't really need
	 * a resize */
	if ( ! force) {
		GtkRequisition req;
		int width, height;

		gtk_window_get_size (GTK_WINDOW (login), &width, &height);
		gtk_widget_size_request (login, &req);

		if (req.width <= width && req.height <= height)
			return;
	}

	GTK_WINDOW (login)->need_default_size = TRUE;
	gtk_container_check_resize (GTK_CONTAINER (login));

	if (set_pos_id == 0)
		set_pos_id = g_idle_add (set_pos_idle, NULL);
}


typedef struct _CursorOffset {
	int x;
	int y;
} CursorOffset;

static gboolean
within_rect (GdkRectangle *rect, int x, int y)
{
	return
		x >= rect->x &&
		x <= rect->x + rect->width &&
		y >= rect->y &&
		y <= rect->y + rect->height;
}

/* switch to the xinerama screen where x,y are */
static void
set_screen_to_pos (int x, int y)
{
	if ( ! within_rect (&gdm_wm_screen, x, y)) {
		int i;
		/* If not within gdm_wm_screen boundaries,
		 * maybe we want to switch xinerama
		 * screen */
		for (i = 0; i < gdm_wm_screens; i++) {
			if (within_rect (&gdm_wm_allscreens[i], x, y)) {
				gdm_wm_set_screen (i);
				break;
			}
		}
	}
}

static void
gdm_run_gdmconfig (GtkWidget *w, gpointer data)
{
	gtk_widget_set_sensitive (browser, FALSE);

	/* Make sure to unselect the user */
	if (selected_user != NULL)
		g_free (selected_user);
	selected_user = NULL;

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	/* configure interruption */
	printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CONFIGURE);
	fflush (stdout);
}

static void
gdm_login_restart_handler (void)
{
	if (gdm_wm_warn_dialog (
	    _("Are you sure you want to restart the computer?"), "",
	    _("_Restart"), NULL, TRUE) == GTK_RESPONSE_YES) {

		closelog ();

		gdm_kill_thingies ();
		_exit (DISPLAY_REBOOT);
	}
}


static void
gdm_login_halt_handler (void)
{
	if (gdm_wm_warn_dialog (
	    _("Are you sure you want to Shut Down the computer?"), "",
	    _("Shut _Down"), NULL, TRUE) == GTK_RESPONSE_YES) {

		closelog ();

		gdm_kill_thingies ();
		_exit (DISPLAY_HALT);
	}
}

static void
gdm_login_use_chooser_handler (void)
{
	closelog ();

	gdm_kill_thingies ();
	_exit (DISPLAY_RUN_CHOOSER);
}

static void
gdm_login_suspend_handler (void)
{
	if (gdm_wm_warn_dialog (
	    _("Are you sure you want to suspend the computer?"), "",
	    _("_Suspend"), NULL, TRUE) == GTK_RESPONSE_YES) {

		/* suspend interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_SUSPEND);
		fflush (stdout);
	}
}

static void
gdm_theme_handler (GtkWidget *widget, gpointer data)
{
    const char *theme_name = (const char *)data;

    printf ("%c%c%c%s\n", STX, BEL, GDM_INTERRUPT_THEME, theme_name);
  
    fflush (stdout);

    gdm_set_theme (theme_name);

    login_window_resize (FALSE);
    gdm_wm_center_window (GTK_WINDOW (login));
}

static void
gdm_login_language_lookup (const gchar* savedlang)
{
    /* Don't save language unless told otherwise */
    savelang = GTK_RESPONSE_NO;

    if (savedlang == NULL)
	    savedlang = "";

    /* If a different language is selected */
    if (curlang != NULL && strcmp (curlang, LAST_LANGUAGE) != 0) {
        g_free (language);
	if (strcmp (curlang, DEFAULT_LANGUAGE) == 0)
		language = g_strdup ("");
	else
		language = g_strdup (curlang);

	/* User's saved language is not the chosen one */
	if (strcmp (savedlang, language) != 0) {
	    gchar *firstmsg;
    	    gchar *secondmsg;
	    char *curname, *savedname;

	    if (strcmp (curlang, DEFAULT_LANGUAGE) == 0) {
		    curname = g_strdup (_("System Default"));
	    } else {
		    curname = gdm_lang_name (curlang,
					     FALSE /* never_encoding */,
					     TRUE /* no_group */,
					     TRUE /* untranslated */,
					     TRUE /* markup */);
	    }
	    if (strcmp (savedlang, "") == 0) {
		    savedname = g_strdup (_("System Default"));
	    } else {
		    savedname = gdm_lang_name (savedlang,
					       FALSE /* never_encoding */,
					       TRUE /* no_group */,
					       TRUE /* untranslated */,
					       TRUE /* markup */);
	    }

	    firstmsg = g_strdup_printf (_("Do you wish to make %s the default for future sessions?"),
	                                curname);
	    secondmsg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is %s."),
	                                 curname, savedname);
	    g_free (curname);
	    g_free (savedname);

	    savelang = gdm_wm_query_dialog (firstmsg, secondmsg,
		_("Make _Default"), _("Just For _This Session"), TRUE);
	    g_free (firstmsg);
	    g_free (secondmsg);
	}
    } else {
	g_free (language);
	language = g_strdup (savedlang);
    }
}

static int dance_handler = 0;

static gboolean
dance (gpointer data)
{
	static double t1 = 0.0, t2 = 0.0;
	double xm, ym;
	int x, y;
	static int width = -1;
	static int height = -1;

	if (width == -1)
		width = gdm_wm_screen.width;
	if (height == -1)
		height = gdm_wm_screen.height;

	if (login == NULL ||
	    login->window == NULL) {
		dance_handler = 0;
		return FALSE;
	}

	xm = cos (2.31 * t1);
	ym = sin (1.03 * t2);

	t1 += 0.03 + (rand () % 10) / 500.0;
	t2 += 0.03 + (rand () % 10) / 500.0;

	x = gdm_wm_screen.x + (width / 2) + (width / 5) * xm;
	y = gdm_wm_screen.y + (height / 2) + (height / 5) * ym;

	set_screen_pos (login,
			x - login->allocation.width / 2,
			y - login->allocation.height / 2);

	return TRUE;
}

static gboolean
evil (const char *user)
{
	static gboolean old_lock;

	if (dance_handler == 0 &&
	    /* do not translate */
	    strcmp (user, "Start Dancing") == 0) {
		gdm_common_setup_cursor (GDK_UMBRELLA);
		dance_handler = g_timeout_add (50, dance, NULL);
		old_lock = GdmLockPosition;
		GdmLockPosition = TRUE;
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
	} else if (dance_handler != 0 &&
		   /* do not translate */
		   strcmp (user, "Stop Dancing") == 0) {
		gdm_common_setup_cursor (GDK_LEFT_PTR);
		g_source_remove (dance_handler);
		dance_handler = 0;
		GdmLockPosition = old_lock;
		gdm_wm_center_window (GTK_WINDOW (login));
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
				 /* do not translate */
	} else if (strcmp (user, "Gimme Random Cursor") == 0) {
		gdm_common_setup_cursor (((rand () >> 3) % (GDK_LAST_CURSOR/2)) * 2);
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
				 /* do not translate */
	} else if (strcmp (user, "Require Quater") == 0 ||
		   strcmp (user, "Require Quarter") == 0) {
		/* btw, note that I misspelled quarter before and
		 * thus this checks for Quater as well as Quarter to
		 * keep compatibility which is obviously important for
		 * something like this */
		require_quarter = TRUE;
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
	}

	return FALSE;
}

static void
gdm_login_enter (GtkWidget *entry)
{
	const char *login_string;
	const char *str;
	char *tmp;

	if (entry == NULL)
		return;

	gtk_widget_set_sensitive (entry, FALSE);
	gtk_widget_set_sensitive (ok_button, FALSE);
	gtk_widget_set_sensitive (cancel_button, FALSE);

	login_string = gtk_entry_get_text (GTK_ENTRY (entry));

	str = gtk_label_get_text (GTK_LABEL (label));
	if (str != NULL &&
	    (strcmp (str, _("Username:")) == 0 ||
	     strcmp (str, _("_Username:")) == 0) &&
	    /* If in timed login mode, and if this is the login
	     * entry.  Then an enter by itself is sort of like I want to
	     * log in as the timed user, really.  */
	    ve_string_empty (login_string) &&
	    timed_handler_id != 0) {
		/* timed interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
		fflush (stdout);
		return;
	}

	if (str != NULL &&
	    (strcmp (str, _("Username:")) == 0 ||
	     strcmp (str, _("_Username:")) == 0) &&
	    /* evilness */
	    evil (login_string)) {
		/* obviously being 100% reliable is not an issue for
		   this test */
		gtk_widget_set_sensitive (entry, TRUE);
		gtk_widget_set_sensitive (ok_button, TRUE);
		gtk_widget_set_sensitive (cancel_button, TRUE);
		gtk_widget_grab_focus (entry);	
		gtk_window_set_focus (GTK_WINDOW (login), entry);	
		return;
	}

	/* clear the err_box */
	if (err_box_clear_handler > 0)
		g_source_remove (err_box_clear_handler);
	err_box_clear_handler = 0;
	gtk_label_set_text (GTK_LABEL (err_box), "");

	tmp = ve_locale_from_utf8 (gtk_entry_get_text (GTK_ENTRY (entry)));
	printf ("%c%s\n", STX, tmp);
	fflush (stdout);
	g_free (tmp);
}

static void
gdm_login_ok_button_press (GtkButton *button, GtkWidget *entry)
{
	gdm_login_enter (entry);
}

static void
gdm_login_cancel_button_press (GtkButton *button, GtkWidget *entry)
{
	GtkTreeSelection *selection;

	if (selected_user != NULL)
		g_free (selected_user);
	selected_user = NULL;

	if (browser != NULL) {
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
		gtk_tree_selection_unselect_all (selection);
	}

	printf ("%c%c%c\n", STX, BEL,
		GDM_INTERRUPT_CANCEL);
	fflush (stdout);
}

static gboolean
gdm_login_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
	if (title_box != NULL)
		gtk_widget_set_state (title_box, GTK_STATE_SELECTED);

	if (icon_button != NULL)
		gtk_widget_set_state (icon_button, GTK_STATE_NORMAL);

	return FALSE;
}

static gboolean
gdm_login_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	if (title_box != NULL)
		gtk_widget_set_state (title_box, GTK_STATE_NORMAL);

	return FALSE;
}

static void 
gdm_login_session_handler (GtkWidget *widget) 
{
    gchar *s;

    current_session = g_object_get_data (G_OBJECT (widget), SESSION_NAME);

    s = g_strdup_printf (_("%s session selected"), gdm_session_name (current_session));

    gtk_label_set_text (GTK_LABEL (msg), s);
    g_free (s);

    login_window_resize (FALSE /* force */);
}

static void 
gdm_login_session_init (GtkWidget *menu)
{
    GSList *sessgrp = NULL;
    GList *tmp;
    GtkWidget *item;
    int num = 1;
    char *label;

    current_session = NULL;
    
    if (gdm_config_get_bool (GDM_KEY_SHOW_LAST_SESSION)) {
            current_session = LAST_SESSION;
            item = gtk_radio_menu_item_new_with_mnemonic (NULL, _("_Last"));
            g_object_set_data (G_OBJECT (item),
			       SESSION_NAME,
			       LAST_SESSION);
            sessgrp = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
            g_signal_connect (G_OBJECT (item), "activate",
			      G_CALLBACK (gdm_login_session_handler),
			      NULL);
            gtk_widget_show (GTK_WIDGET (item));
            item = gtk_menu_item_new ();
            gtk_widget_set_sensitive (item, FALSE);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
            gtk_widget_show (GTK_WIDGET (item));
    }

    gdm_session_list_init ();

    for (tmp = sessions; tmp != NULL; tmp = tmp->next) {
	    GdmSession *session;
	    char *file;

	    file = (char *) tmp->data;
	    session = g_hash_table_lookup (sessnames, file);

	    if (num < 10 && 
	       (strcmp (file, GDM_SESSION_FAILSAFE_GNOME) != 0) &&
	       (strcmp (file, GDM_SESSION_FAILSAFE_XTERM) != 0))
		    label = g_strdup_printf ("_%d. %s", num, session->name);
	    else
		    label = g_strdup (session->name);
	    num++;

	    item = gtk_radio_menu_item_new_with_mnemonic (sessgrp, label);
	    g_free (label);
	    g_object_set_data_full (G_OBJECT (item), SESSION_NAME,
		 g_strdup (file), (GDestroyNotify) g_free);

	    sessgrp = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
	    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	    g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (gdm_login_session_handler), NULL);
	    gtk_widget_show (GTK_WIDGET (item));
    }

    /* Select the proper session */
    {
            GSList *tmp;
            
            tmp = sessgrp;
            while (tmp != NULL) {
                    GtkWidget *w = tmp->data;
                    const char *n;

                    n = g_object_get_data (G_OBJECT (w), SESSION_NAME);
                    
                    if (n && strcmp (n, current_session) == 0) {
                            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (w),
                                                            TRUE);
                            break;
                    }
                    
                    tmp = tmp->next;
            }
    }
}


static void 
gdm_login_language_handler (GtkWidget *widget) 
{
    gchar *s;
    char *name;

    if (!widget)
	return;

    curlang = g_object_get_data (G_OBJECT (widget), "Language");
    name = gdm_lang_name (curlang,
			  FALSE /* never_encoding */,
			  TRUE /* no_group */,
			  TRUE /* untranslated */,
			  TRUE /* makrup */);
    s = g_strdup_printf (_("%s language selected"), name);
    g_free (name);
    gtk_label_set_markup (GTK_LABEL (msg), s);
    g_free (s);

    login_window_resize (FALSE /* force */);
}


static GtkWidget *
gdm_login_language_menu_new (void)
{
    GtkWidget *menu;
    GtkWidget *item, *ammenu, *nzmenu, *omenu;
    GList *langlist, *li;
    gboolean has_other_locale = FALSE;
    GtkWidget *other_menu;
    const char *g1;
    const char *g2;
    char *menulabel;
    /* Start numbering with 3 since 1-2 is used for toplevel menu */
    int g1_num = 3;
    int g2_num = 3;
    int other_num = 3;
    int num;

    langlist = gdm_lang_read_locale_file (gdm_config_get_string (GDM_KEY_LOCALE_FILE));

    if (langlist == NULL)
	    return NULL;

    menu = gtk_menu_new ();

    curlang = LAST_LANGUAGE;

    item = gtk_radio_menu_item_new_with_mnemonic (NULL, _("_Last"));
    languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    g_signal_connect (G_OBJECT (item), "activate", 
		      G_CALLBACK (gdm_login_language_handler), 
		      NULL);
    gtk_widget_show (GTK_WIDGET (item));
    g_object_set_data (G_OBJECT (item),
		       "Language",
		       LAST_LANGUAGE);

    item = gtk_radio_menu_item_new_with_mnemonic (languages, _("_System Default"));
    languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    g_signal_connect (G_OBJECT (item), "activate", 
		      G_CALLBACK (gdm_login_language_handler), 
		      NULL);
    gtk_widget_show (GTK_WIDGET (item));
    g_object_set_data (G_OBJECT (item),
		       "Language",
		       DEFAULT_LANGUAGE);

    item = gtk_menu_item_new ();
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    menulabel = g_strdup_printf ("_1. %s", gdm_lang_group1());
    item = gtk_menu_item_new_with_mnemonic (menulabel);
    g_free (menulabel);
    ammenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (ammenu));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    menulabel = g_strdup_printf ("_2. %s", gdm_lang_group2());
    item = gtk_menu_item_new_with_mnemonic (menulabel);
    g_free (menulabel);
    nzmenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), nzmenu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    other_menu = item = gtk_menu_item_new_with_mnemonic (_("_Other"));
    omenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), omenu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    g1 = gdm_lang_group1 ();
    g2 = gdm_lang_group2 ();

    for (li = langlist; li != NULL; li = li->next) {
	    char *lang = li->data;
	    char *name;
	    char *untranslated;
	    char *group;
	    char *p;
	    GtkWidget *box, *l;

	    li->data = NULL;

	    group = name = gdm_lang_name (lang,
					  FALSE /* never_encoding */,
					  FALSE /* no_group */,
					  FALSE /* untranslated */,
					  FALSE /* markup */);
	    if (name == NULL) {
		    g_free (lang);
		    continue;
	    }

	    untranslated = gdm_lang_untranslated_name (lang,
						       TRUE /* markup */);

	    p = strchr (name, '|');
	    if (p != NULL) {
		    *p = '\0';
		    name = p+1;
	    }

	    box = gtk_hbox_new (FALSE, 5);
	    gtk_widget_show (box);

	    if (strcmp (group, g1) == 0)
		num = g1_num++;
	    else if (strcmp (group, g2) == 0)
                num = g2_num++;
	    else
		num = other_num++;

	    if (num < 10)
		menulabel = g_strdup_printf ("_%d. %s", num, name);
	    else if ((num -10) + (int)'a' <= (int)'z')
		menulabel = g_strdup_printf ("_%c. %s",
                                                  (char)(num-10)+'a',
                                                  name);
	    else
		menulabel = g_strdup (name);

	    l = gtk_label_new_with_mnemonic (menulabel);
	    if ( ! gdm_lang_name_translated (lang))
		    gtk_widget_set_direction (l, GTK_TEXT_DIR_LTR);
	    gtk_widget_show (l);
	    gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);

	    if (untranslated != NULL) {
		    l = gtk_label_new (untranslated);
		    /* we really wantd LTR here for the widget */
		    gtk_widget_set_direction (l, GTK_TEXT_DIR_LTR);
		    gtk_label_set_use_markup (GTK_LABEL (l), TRUE);
		    gtk_widget_show (l);
		    gtk_box_pack_end (GTK_BOX (box), l, FALSE, FALSE, 0);
	    }

	    item = gtk_radio_menu_item_new (languages);
	    gtk_container_add (GTK_CONTAINER (item), box);
	    languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
	    g_object_set_data_full (G_OBJECT (item),
				    "Language",
				    g_strdup (lang),
				    (GDestroyNotify) g_free);

	    if (strcmp (group, g1) == 0) {
		    gtk_menu_shell_append (GTK_MENU_SHELL (ammenu), item);
	    } else if (strcmp (group, g2) == 0) {
		    gtk_menu_shell_append (GTK_MENU_SHELL (nzmenu), item);
	    } else {
		    gtk_menu_shell_append (GTK_MENU_SHELL (omenu), item);
		    has_other_locale = TRUE;
	    }

	    g_signal_connect (G_OBJECT (item), "activate", 
			      G_CALLBACK (gdm_login_language_handler), 
			      NULL);
	    gtk_widget_show (GTK_WIDGET (item));

	    g_free (lang);
	    g_free (group);
	    g_free (untranslated);
    }
    if ( ! has_other_locale) 
	    gtk_widget_destroy (other_menu);

    g_list_free (langlist);

    return menu;
}

static gboolean
theme_allowed (const char *theme)
{
	gchar * themestoallow = gdm_config_get_string (GDM_KEY_GTK_THEMES_TO_ALLOW);
	char **vec;
	int i;

	if (ve_string_empty (themestoallow) ||
	    g_ascii_strcasecmp (themestoallow, "all") == 0)
		return TRUE;

	vec = g_strsplit (themestoallow, ",", 0);
	if (vec == NULL || vec[0] == NULL)
		return TRUE;

	for (i = 0; vec[i] != NULL; i++) {
		if (strcmp (vec[i], theme) == 0) {
			g_strfreev (vec);
			return TRUE;
		}
	}

	g_strfreev (vec);

	return FALSE;
}

static GSList *
build_theme_list (void)
{
    DIR *dir;
    struct dirent *de;
    gchar *theme_dir;
    GSList *theme_list = NULL;

    theme_dir = gtk_rc_get_theme_dir ();
    dir = opendir (theme_dir);

    while ((de = readdir (dir))) {
	char *name;
	if (de->d_name[0] == '.')
		continue;
	if ( ! theme_allowed (de->d_name))
		continue;
	name = g_build_filename (theme_dir, de->d_name, GTK_KEY, NULL);
	if (g_file_test (name, G_FILE_TEST_IS_DIR))
		theme_list = g_slist_append (theme_list, g_strdup (de->d_name));
	g_free (name);
    }
    g_free (theme_dir);
    closedir (dir);

    return theme_list;
}

static GtkWidget *
gdm_login_theme_menu_new (void)
{
    GSList *theme_list;
    GtkWidget *item;
    GtkWidget *menu;
    int num = 1;

    if ( ! gdm_config_get_bool (GDM_KEY_ALLOW_GTK_THEME_CHANGE))
	    return NULL;

    menu = gtk_menu_new ();
    
    for (theme_list = build_theme_list ();
	 theme_list != NULL;
	 theme_list = theme_list->next) {
        char *menu_item_name;
        char *theme_name = theme_list->data;
	theme_list->data = NULL;

	if (num < 10)
		menu_item_name = g_strdup_printf ("_%d. %s", num, _(theme_name));
	else if ((num -10) + (int)'a' <= (int)'z')
		menu_item_name = g_strdup_printf ("_%c. %s",
						  (char)(num-10)+'a',
						  _(theme_name));
	else
		menu_item_name = g_strdup (theme_name);
	num++;

	item = gtk_menu_item_new_with_mnemonic (menu_item_name);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (GTK_WIDGET (item));
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gdm_theme_handler), theme_name);
	g_free (menu_item_name);
    }
    g_slist_free (theme_list);
    return menu;
}

static gboolean
err_box_clear (gpointer data)
{
	if (err_box != NULL)
		gtk_label_set_text (GTK_LABEL (err_box), "");

	err_box_clear_handler = 0;
	return FALSE;
}

static void
browser_set_user (const char *user)
{
  gboolean old_selecting_user = selecting_user;
  GtkTreeSelection *selection;
  GtkTreeIter iter = {0};
  GtkTreeModel *tm = NULL;

  if (browser == NULL)
    return;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
  gtk_tree_selection_unselect_all (selection);

  if (ve_string_empty (user))
    return;

  selecting_user = FALSE;

  tm = gtk_tree_view_get_model (GTK_TREE_VIEW (browser));

  if (gtk_tree_model_get_iter_first (tm, &iter))
    {
      do
        {
          char *login;
	  gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			      &login, -1);
	  if (login != NULL && strcmp (user, login) == 0)
	    {
	      GtkTreePath *path = gtk_tree_model_get_path (tm, &iter);
	      gtk_tree_selection_select_iter (selection, &iter);
	      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (browser),
					    path, NULL,
					    FALSE, 0.0, 0.0);
	      gtk_tree_path_free (path);
	      break;
	    }
	  
        }
      while (gtk_tree_model_iter_next (tm, &iter));
    }
  selecting_user = old_selecting_user;
}

static Display *
get_parent_display (void)
{
  static gboolean tested = FALSE;
  static Display *dsp = NULL;

  if (tested)
    return dsp;

  tested = TRUE;

  if (g_getenv ("GDM_PARENT_DISPLAY") != NULL)
    {
      char *old_xauth = g_strdup (g_getenv ("XAUTHORITY"));
      if (g_getenv ("GDM_PARENT_XAUTHORITY") != NULL)
        {
	  g_setenv ("XAUTHORITY",
		     g_getenv ("GDM_PARENT_XAUTHORITY"), TRUE);
	}
      dsp = XOpenDisplay (g_getenv ("GDM_PARENT_DISPLAY"));
      if (old_xauth != NULL)
        g_setenv ("XAUTHORITY", old_xauth, TRUE);
      else
        g_unsetenv ("XAUTHORITY");
      g_free (old_xauth);
    }

  return dsp;
}

static gboolean
greeter_is_capslock_on (void)
{
  XkbStateRec states;
  Display *dsp;

  /* HACK! incredible hack, if GDM_PARENT_DISPLAY is set we get
   * indicator state from the parent display, since we must be inside an
   * Xnest */
  dsp = get_parent_display ();
  if (dsp == NULL)
    dsp = GDK_DISPLAY ();

  if (XkbGetState (dsp, XkbUseCoreKbd, &states) != Success)
      return FALSE;

  return (states.locked_mods & LockMask) != 0;
}

static void
face_browser_select_user (gchar *login)
{
        printf ("%c%c%c%s\n", STX, BEL,
                GDM_INTERRUPT_SELECT_USER, login);

        fflush (stdout);
}

static gboolean
gdm_login_ctrl_handler (GIOChannel *source, GIOCondition cond, gint fd)
{
    gchar buf[PIPE_SIZE];
    gchar *p;
    gsize len;

    /* If this is not incoming i/o then return */
    if (cond != G_IO_IN) 
	return (TRUE);

    /* Read random garbage from i/o channel until STX is found */
    do {
	g_io_channel_read_chars (source, buf, 1, &len, NULL);

	if (len != 1)
	    return (TRUE);
    } while (buf[0] && buf[0] != STX);

    memset (buf, '\0', sizeof (buf));
    if (g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &len, NULL) !=
	G_IO_STATUS_NORMAL)
      return TRUE;

    p = memchr (buf, STX, len);
    if (p != NULL) {
      len = p - buf;
      g_io_channel_seek_position (source, -((sizeof (buf) - 1) - len), G_SEEK_CUR, NULL);
      memset (buf + len, '\0', (sizeof (buf) - 1) - len);
    }
    buf[len - 1] = '\0';  
 
    process_operation ((guchar) buf[0], buf + 1);

    return TRUE;
}

static void
process_operation (guchar       op_code,
		   const gchar *args)
{
    char *tmp;
    gint i, x, y;
    GtkWidget *dlg;
    static gboolean replace_msg = TRUE;
    static gboolean messages_to_give = FALSE;
    gboolean greeter_probably_login_prompt = FALSE;

    /* Parse opcode */
    switch (op_code) {
    case GDM_SETLOGIN:
	/* somebody is trying to fool us this is the user that
	 * wants to log in, and well, we are the gullible kind */
	g_free (curuser);
	curuser = g_strdup (args);
	if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER))
		browser_set_user (curuser);
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_PROMPT:
	tmp = ve_locale_to_utf8 (args);
	if (tmp != NULL && strcmp (tmp, _("Username:")) == 0) {
		gdm_common_login_sound (gdm_config_get_string (GDM_KEY_SOUND_PROGRAM),
					gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE),
					gdm_config_get_bool   (GDM_KEY_SOUND_ON_LOGIN));
		gtk_label_set_text_with_mnemonic (GTK_LABEL (label), _("_Username:"));
		greeter_probably_login_prompt = TRUE;
	} else {
		if (tmp != NULL)
			gtk_label_set_text (GTK_LABEL (label), tmp);
		greeter_probably_login_prompt = FALSE;
	}
	g_free (tmp);

	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_max_length (GTK_ENTRY (entry), PW_ENTRY_SIZE);
	gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_set_sensitive (ok_button, TRUE);
	gtk_widget_set_sensitive (cancel_button, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_window_set_focus (GTK_WINDOW (login), entry);	
	gtk_widget_show (entry);

	/* replace rather then append next message string */
	replace_msg = TRUE;

	/* the user has seen messages */
	messages_to_give = FALSE;

	login_window_resize (FALSE /* force */);

        if (greeter_probably_login_prompt == TRUE && selected_user != NULL)
		face_browser_select_user (selected_user);
	break;

    case GDM_NOECHO:
	tmp = ve_locale_to_utf8 (args);
	if (tmp != NULL && strcmp (tmp, _("Password:")) == 0) {
		gtk_label_set_text_with_mnemonic (GTK_LABEL (label), _("_Password:"));
	} else {
		if (tmp != NULL)
			gtk_label_set_text (GTK_LABEL (label), tmp);
	}
	g_free (tmp);

	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_max_length (GTK_ENTRY (entry), PW_ENTRY_SIZE);
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_set_sensitive (ok_button, TRUE);
	gtk_widget_set_sensitive (cancel_button, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_window_set_focus (GTK_WINDOW (login), entry);	
	gtk_widget_show (entry);

	/* replace rather then append next message string */
	replace_msg = TRUE;

	/* the user has seen messages */
	messages_to_give = FALSE;

	login_window_resize (FALSE /* force */);
	break;

    case GDM_MSG:
	/* the user has not yet seen messages */
	messages_to_give = TRUE;

	/* HAAAAAAACK.  Sometimes pam sends many many messages, SO
	 * we try to collect them until the next prompt or reset or
	 * whatnot */
	if ( ! replace_msg &&
	   /* empty message is for clearing */
	   ! ve_string_empty (args)) {
		const char *oldtext;
		oldtext = gtk_label_get_text (GTK_LABEL (msg));
		if ( ! ve_string_empty (oldtext)) {
			char *newtext;
			tmp = ve_locale_to_utf8 (args);
			newtext = g_strdup_printf ("%s\n%s", oldtext, tmp);
			g_free (tmp);
			gtk_label_set_text (GTK_LABEL (msg), newtext);
			g_free (newtext);
		} else {
			tmp = ve_locale_to_utf8 (args);
			gtk_label_set_text (GTK_LABEL (msg), tmp);
			g_free (tmp);
		}
	} else {
		tmp = ve_locale_to_utf8 (args);
		gtk_label_set_text (GTK_LABEL (msg), tmp);
		g_free (tmp);
	}
	replace_msg = FALSE;

	gtk_widget_show (GTK_WIDGET (msg));
	printf ("%c\n", STX);
	fflush (stdout);

	login_window_resize (FALSE /* force */);

	break;

    case GDM_ERRBOX:
	tmp = ve_locale_to_utf8 (args);
	gtk_label_set_text (GTK_LABEL (err_box), tmp);
	g_free (tmp);
	if (err_box_clear_handler > 0)
		g_source_remove (err_box_clear_handler);
	if (ve_string_empty (args))
		err_box_clear_handler = 0;
	else
		err_box_clear_handler = g_timeout_add (30000,
						       err_box_clear,
						       NULL);
	printf ("%c\n", STX);
	fflush (stdout);

	login_window_resize (FALSE /* force */);
	break;

    case GDM_ERRDLG:
	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	tmp = ve_locale_to_utf8 (args);
	dlg = ve_hig_dialog_new (NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 tmp,
				 "");
	g_free (tmp);

	gdm_wm_center_window (GTK_WINDOW (dlg));

	gdm_wm_no_login_focus_push ();
	gtk_dialog_run (GTK_DIALOG (dlg));
	gtk_widget_destroy (dlg);
	gdm_wm_no_login_focus_pop ();

	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_SESS:
	tmp = ve_locale_to_utf8 (args);
	session = gdm_session_lookup (tmp);
	g_free (tmp);
	if (gdm_get_save_session () == GTK_RESPONSE_CANCEL) {
	    printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
	} else {
	    tmp = ve_locale_from_utf8 (session);
	    printf ("%c%s\n", STX, tmp);
	    g_free (tmp);
	}
	fflush (stdout);
	break;

    case GDM_LANG:
	gdm_login_language_lookup (args);
	if (savelang == GTK_RESPONSE_CANCEL)
	    printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
	else
	    printf ("%c%s\n", STX, language);
	fflush (stdout);
	break;

    case GDM_SSESS:
	if (gdm_get_save_session () == GTK_RESPONSE_YES)
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);
	
	break;

    case GDM_SLANG:
	if (savelang == GTK_RESPONSE_YES)
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);

	break;

    case GDM_RESET:
	if (gdm_config_get_bool (GDM_KEY_QUIVER) &&
	    login->window != NULL &&
	    icon_win == NULL &&
	    GTK_WIDGET_VISIBLE (login)) {
		Window lw = GDK_WINDOW_XWINDOW (login->window);

		gdm_wm_get_window_pos (lw, &x, &y);

		for (i = 32 ; i > 0 ; i = i/4) {
			gdm_wm_move_window_now (lw, i+x, y);
			usleep (200);
			gdm_wm_move_window_now (lw, x, y);
			usleep (200);
			gdm_wm_move_window_now (lw, -i+x, y);
			usleep (200);
			gdm_wm_move_window_now (lw, x, y);
			usleep (200);
		}
	}
	/* fall thru to reset */

    case GDM_RESETOK:
	if (curuser != NULL) {
	    g_free (curuser);
	    curuser = NULL;
	}

	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_set_sensitive (ok_button, TRUE);
	gtk_widget_set_sensitive (cancel_button, TRUE);

	if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER))
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);

	tmp = ve_locale_to_utf8 (args);
	gtk_label_set_text (GTK_LABEL (msg), tmp);
	g_free (tmp);
	gtk_widget_show (GTK_WIDGET (msg));

	printf ("%c\n", STX);
	fflush (stdout);

	login_window_resize (FALSE /* force */);
	break;

    case GDM_QUIT:
	if (timed_handler_id != 0) {
		g_source_remove (timed_handler_id);
		timed_handler_id = 0;
	}

	if (require_quarter) {
		/* we should be now fine for focusing new windows */
		gdm_wm_focus_new_windows (TRUE);

		dlg = ve_hig_dialog_new (NULL /* parent */,
					 GTK_DIALOG_MODAL /* flags */,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_OK,
					 /* translators:  This is a nice and evil eggie text, translate
					  * to your favourite currency */
					 _("Please insert 25 cents "
					   "to log in."),
					 "");
		gdm_wm_center_window (GTK_WINDOW (dlg));

		gdm_wm_no_login_focus_push ();
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		gdm_wm_no_login_focus_pop ();
	}

	/* Hide the login window now */
	gtk_widget_hide (login);

	if (messages_to_give) {
		const char *oldtext;
		oldtext = gtk_label_get_text (GTK_LABEL (msg));

		if ( ! ve_string_empty (oldtext)) {
			/* we should be now fine for focusing new windows */
			gdm_wm_focus_new_windows (TRUE);

			dlg = ve_hig_dialog_new (NULL /* parent */,
						 GTK_DIALOG_MODAL /* flags */,
						 GTK_MESSAGE_INFO,
						 GTK_BUTTONS_OK,
						 oldtext,
						 "");
			gtk_window_set_modal (GTK_WINDOW (dlg), TRUE);
			gdm_wm_center_window (GTK_WINDOW (dlg));

			gdm_wm_no_login_focus_push ();
			gtk_dialog_run (GTK_DIALOG (dlg));
			gtk_widget_destroy (dlg);
			gdm_wm_no_login_focus_pop ();
		}
		messages_to_give = FALSE;
	}

	gdm_kill_thingies ();

	gdk_flush ();

	printf ("%c\n", STX);
	fflush (stdout);

	/* screw gtk_main_quit, we want to make sure we definately die */
	_exit (EXIT_SUCCESS);
	break;

    case GDM_STARTTIMER:
	/*
	 * Timed Login: Start Timer Loop
	 */

	if (timed_handler_id == 0 &&
	    gdm_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE) &&
	    ! ve_string_empty (gdm_config_get_string (GDM_KEY_TIMED_LOGIN)) &&
	    gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY) > 0) {
		gdm_timed_delay = gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);
		timed_handler_id  = g_timeout_add (1000, gdm_timer, NULL);
	}
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_STOPTIMER:
	/*
	 * Timed Login: Stop Timer Loop
	 */

	if (timed_handler_id != 0) {
		g_source_remove (timed_handler_id);
		timed_handler_id = 0;
	}
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_DISABLE:
	if (clock_label != NULL)
		GTK_WIDGET_SET_FLAGS (clock_label->parent, GTK_SENSITIVE);
	gtk_widget_set_sensitive (login, FALSE);
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_ENABLE:
	gtk_widget_set_sensitive (login, TRUE);
	if (clock_label != NULL)
		GTK_WIDGET_UNSET_FLAGS (clock_label->parent, GTK_SENSITIVE);
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    /* These are handled separately so ignore them here and send
     * back a NULL response so that the daemon quits sending them */
    case GDM_NEEDPIC:
    case GDM_READPIC:
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_NOFOCUS:
	gdm_wm_no_login_focus_push ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_FOCUS:
	gdm_wm_no_login_focus_pop ();
	
	printf ("%c\n", STX);
	fflush (stdout);
	break;

    case GDM_SAVEDIE:
	/* Set busy cursor */
	gdm_common_setup_cursor (GDK_WATCH);

	gdm_wm_save_wm_order ();

	gdm_kill_thingies ();
	gdk_flush ();

	_exit (EXIT_SUCCESS);

    case GDM_QUERY_CAPSLOCK:
	if (greeter_is_capslock_on ())
	    printf ("%cY\n", STX);
	else
	    printf ("%c\n", STX);
	fflush (stdout);

	break;
	
    default:
	gdm_kill_thingies ();
	gdm_common_fail_greeter ("Unexpected greeter command received: '%c'", op_code);
	break;
    }
}


static void
gdm_login_browser_populate (void)
{
    GList *li;

    for (li = users; li != NULL; li = li->next) {
	    GdmUser *usr = li->data;
	    GtkTreeIter iter = {0};
	    char *label;
	    char *login, *gecos;

	    login = g_markup_escape_text (usr->login, -1);
	    gecos = g_markup_escape_text (usr->gecos, -1);

	    label = g_strdup_printf ("<b>%s</b>\n%s",
				     login,
				     gecos);

	    g_free (login);
	    g_free (gecos);
	    gtk_list_store_append (GTK_LIST_STORE (browser_model), &iter);
	    gtk_list_store_set (GTK_LIST_STORE (browser_model), &iter,
				GREETER_ULIST_ICON_COLUMN, usr->picture,
				GREETER_ULIST_LOGIN_COLUMN, usr->login,
				GREETER_ULIST_LABEL_COLUMN, label,
				-1);
	    g_free (label);
    }
}

static void
user_selected (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};

#ifdef FIXME
  g_free (selected_browser_user);
  selected_browser_user = NULL;
#endif

  if (gtk_tree_selection_get_selected (selection, &tm, &iter)) {
	char *login;
	gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			      &login, -1);
	if (login != NULL) {
		const char *str = gtk_label_get_text (GTK_LABEL (label));

		if (selecting_user &&
		    str != NULL &&
		    (strcmp (str, _("Username:")) == 0 ||
		     strcmp (str, _("_Username:")) == 0)) {
			gtk_entry_set_text (GTK_ENTRY (entry), login);
		}
#ifdef FIXME
		selected_browser_user = g_strdup (login);
#endif
		if (selecting_user) {
			face_browser_select_user (login);
			if (selected_user != NULL)
				g_free (selected_user);
			selected_user = g_strdup (login);
  		}
  	}
  }
}

static void
browser_change_focus (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    gtk_widget_grab_focus (entry);	
}

static gboolean
gdm_login_handle_pressed (GtkWidget *widget, GdkEventButton *event)
{
    gint xp, yp;
    GdkModifierType mask;
    CursorOffset *p;
    GdkCursor *fleur_cursor;

    if (login == NULL ||
	login->window == NULL ||
	event->type != GDK_BUTTON_PRESS ||
	GdmLockPosition)
	    return FALSE;

    gdk_window_raise (login->window);

    p = g_new0 (CursorOffset, 1);
    g_object_set_data_full (G_OBJECT (widget), "offset", p,
			    (GDestroyNotify)g_free);
    
    gdk_window_get_pointer (login->window, &xp, &yp, &mask);
    p->x = xp;
    p->y = yp;

    gtk_grab_add (widget);
    fleur_cursor = gdk_cursor_new (GDK_FLEUR);
    gdk_pointer_grab (widget->window, TRUE,
		      GDK_BUTTON_RELEASE_MASK |
		      GDK_BUTTON_MOTION_MASK |
		      GDK_POINTER_MOTION_HINT_MASK,
		      NULL,
		      fleur_cursor,
		      GDK_CURRENT_TIME);
    gdk_cursor_unref (fleur_cursor);
    gdk_flush ();
    
    return TRUE;
}

static gboolean
gdm_login_handle_released (GtkWidget *widget, GdkEventButton *event)
{
	gtk_grab_remove (widget);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);

	g_object_set_data (G_OBJECT (widget), "offset", NULL);

	return TRUE;
}


static gboolean
gdm_login_handle_motion (GtkWidget *widget, GdkEventMotion *event)
{
    int xp, yp;
    CursorOffset *p;
    GdkModifierType mask;

    p = g_object_get_data (G_OBJECT (widget), "offset");

    if (p == NULL)
	    return FALSE;

    gdk_window_get_pointer (gdk_get_default_root_window (), &xp, &yp, &mask);

    set_screen_to_pos (xp, yp);

    GdmSetPosition = TRUE;
    GdmPositionX = xp - p->x;
    GdmPositionY = yp - p->y;

    if (GdmPositionX < 0)
	    GdmPositionX = 0;
    if (GdmPositionY < 0)
	    GdmPositionY = 0;

    set_screen_pos (GTK_WIDGET (login), GdmPositionX, GdmPositionY);

    return TRUE;
}

static GtkWidget *
create_handle (void)
{
	GtkWidget *hbox, *w;

	title_box = gtk_event_box_new ();
	g_signal_connect (G_OBJECT (title_box), "button_press_event",
			  G_CALLBACK (gdm_login_handle_pressed),
			  NULL);
	g_signal_connect (G_OBJECT (title_box), "button_release_event",
			  G_CALLBACK (gdm_login_handle_released),
			  NULL);
	g_signal_connect (G_OBJECT (title_box), "motion_notify_event",
			  G_CALLBACK (gdm_login_handle_motion),
			  NULL);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (title_box), hbox);
	w = gtk_label_new (_("GNOME Desktop Manager"));
	gtk_misc_set_padding (GTK_MISC (w), 4, 4);
	gtk_box_pack_start (GTK_BOX (hbox), w,
			    TRUE, TRUE, 4);
	
	gtk_widget_show_all (title_box);

	return title_box;
}

static gboolean
update_clock (void)
{
        struct tm *the_tm;
	gchar *str;
        gint time_til_next_min;

	if (clock_label == NULL)
		return FALSE;

	str = gdm_common_get_clock (&the_tm);
	gtk_label_set_text (GTK_LABEL (clock_label), str);
	g_free (str);

	/* account for leap seconds */
	time_til_next_min = 60 - the_tm->tm_sec;
	time_til_next_min = (time_til_next_min>=0?time_til_next_min:0);

	g_timeout_add (time_til_next_min*1000, update_clock, NULL);
	return FALSE;
}

/* doesn't check for executability, just for existence */
static gboolean
bin_exists (const char *command)
{
	char *bin;

	if (ve_string_empty (command))
		return FALSE;

	/* Note, check only for existence, not for executability */
	bin = ve_first_word (command);
	if (bin != NULL &&
	    g_access (bin, F_OK) == 0) {
		g_free (bin);
		return TRUE;
	} else {
		g_free (bin);
		return FALSE;
	}
}

static gboolean
window_browser_event (GtkWidget *window, GdkEvent *event, gpointer data)
{
	switch (event->type) {
		/* FIXME: Fix fingering cuz it's cool */
#ifdef FIXME
	case GDK_KEY_PRESS:
		if ((event->key.state & GDK_CONTROL_MASK) &&
		    (event->key.keyval == GDK_f ||
		     event->key.keyval == GDK_F) &&
		    selected_browser_user != NULL) {
			GtkWidget *d, *less;
			char *command;
			d = gtk_dialog_new_with_buttons (_("Finger"),
							 NULL /* parent */,
							 0 /* flags */,
							 GTK_STOCK_OK,
							 GTK_RESPONSE_OK,
							 NULL);
			gtk_dialog_set_has_separator (GTK_DIALOG (d), FALSE);
			less = gnome_less_new ();
			gtk_widget_show (less);
			gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d)->vbox),
					    less,
					    TRUE,
					    TRUE,
					    0);

			/* hack to make this be the size of a terminal */
			gnome_less_set_fixed_font (GNOME_LESS (less), TRUE);
			{
				int i;
				char buf[82];
				GtkWidget *text = GTK_WIDGET (GNOME_LESS (less)->text);
				GdkFont *font = GNOME_LESS (less)->font;
				for (i = 0; i < 81; i++)
					buf[i] = 'X';
				buf[i] = '\0';
				gtk_widget_set_size_request
					(text,
					 gdk_string_width (font, buf) + 30,
					 gdk_string_height (font, buf)*24+30);
			}

			command = g_strconcat ("finger ",
					       selected_browser_user,
					       NULL);
			gnome_less_show_command (GNOME_LESS (less), command);

			gtk_widget_grab_focus (GTK_WIDGET (less));

			gtk_window_set_modal (GTK_WINDOW (d), TRUE);
			gdm_wm_center_window (GTK_WINDOW (d));

			gdm_wm_no_login_focus_push ();
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
			gdm_wm_no_login_focus_pop ();
		}
		break;
#endif
	default:
		break;
	}

	return FALSE;
}

static gboolean
key_press_event (GtkWidget *entry, GdkEventKey *event, gpointer data)
{
	if ((event->keyval == GDK_Tab ||
	     event->keyval == GDK_KP_Tab) &&
	    (event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) == 0) {
		gdm_login_enter (entry);
		return TRUE;
	}

	return FALSE;
}

static void
gdm_set_welcomemsg (void)
{
	gchar *greeting;
	gchar *welcomemsg     = gdm_common_get_welcomemsg ();
	gchar *fullwelcomemsg = g_strdup_printf (
		"<big><big><big>%s</big></big></big>", welcomemsg);

	greeting = gdm_common_expand_text (fullwelcomemsg);
	gtk_label_set_markup (GTK_LABEL (welcome), greeting);

	g_free (fullwelcomemsg);
	g_free (welcomemsg);
	g_free (greeting);
}

static void
gdm_login_gui_init (void)
{
    GtkWidget *frame1, *frame2, *ebox;
    GtkWidget *mbox, *menu, *menubar, *item;
    GtkWidget *stack, *hline1, *hline2, *handle;
    GtkWidget *bbox = NULL;
    GtkWidget /**help_button,*/ *button_box;
    gint rows;
    GdkPixbuf *pb;
    GtkWidget *frame;
    int lw, lh;
    gboolean have_logo = FALSE;
    GtkWidget *thememenu;
    const gchar *theme_name;

    theme_name = g_getenv ("GDM_GTK_THEME");
    if (ve_string_empty (theme_name))
	    theme_name = gdm_config_get_string (GDM_KEY_GTK_THEME);

    if ( ! ve_string_empty (gdm_config_get_string (GDM_KEY_GTKRC)))
	    gtk_rc_parse (gdm_config_get_string (GDM_KEY_GTKRC));

    if ( ! ve_string_empty (theme_name)) {
	    gdm_set_theme (theme_name);
    }

    login = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_object_ref (login);
    g_object_set_data_full (G_OBJECT (login), "login", login,
			    (GDestroyNotify) g_object_unref);

    gtk_widget_set_events (login, GDK_ALL_EVENTS_MASK);

    gtk_window_set_title (GTK_WINDOW (login), _("GDM Login"));
    /* connect for fingering */
    if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER))
	    g_signal_connect (G_OBJECT (login), "event",
			      G_CALLBACK (window_browser_event),
			      NULL);

    frame1 = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame1), GTK_SHADOW_OUT);
    gtk_container_set_border_width (GTK_CONTAINER (frame1), 0);
    gtk_container_add (GTK_CONTAINER (login), frame1);
    g_object_set_data_full (G_OBJECT (login), "frame1", frame1,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_ref (GTK_WIDGET (frame1));
    gtk_widget_show (GTK_WIDGET (frame1));

    frame2 = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame2), GTK_SHADOW_IN);
    gtk_container_set_border_width (GTK_CONTAINER (frame2), 2);
    gtk_container_add (GTK_CONTAINER (frame1), frame2);
    g_object_set_data_full (G_OBJECT (login), "frame2", frame2,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_ref (GTK_WIDGET (frame2));
    gtk_widget_show (GTK_WIDGET (frame2));

    mbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_ref (mbox);
    g_object_set_data_full (G_OBJECT (login), "mbox", mbox,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (mbox);
    gtk_container_add (GTK_CONTAINER (frame2), mbox);

    if (gdm_config_get_bool (GDM_KEY_TITLE_BAR)) {
	    handle = create_handle ();
	    gtk_box_pack_start (GTK_BOX (mbox), handle, FALSE, FALSE, 0);
    }

    menubar = gtk_menu_bar_new ();
    gtk_widget_ref (GTK_WIDGET (menubar));
    gtk_box_pack_start (GTK_BOX (mbox), menubar, FALSE, FALSE, 0);

    menu = gtk_menu_new ();
    gdm_login_session_init (menu);
    sessmenu = gtk_menu_item_new_with_mnemonic (_("_Session"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menubar), sessmenu);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (sessmenu), menu);
    gtk_widget_show (GTK_WIDGET (sessmenu));

    menu = gdm_login_language_menu_new ();
    if (menu != NULL) {
	langmenu = gtk_menu_item_new_with_mnemonic (_("_Language"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menubar), langmenu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (langmenu), menu);
	gtk_widget_show (GTK_WIDGET (langmenu));
    }

    if (disable_sys_config_chooser_buttons == FALSE &&
        gdm_config_get_bool (GDM_KEY_SYSTEM_MENU)) {

        gboolean got_anything = FALSE;

	menu = gtk_menu_new ();

	if (gdm_config_get_bool (GDM_KEY_CHOOSER_BUTTON)) {
		item = gtk_menu_item_new_with_mnemonic (_("Remote Login via _XDMCP..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gdm_login_use_chooser_handler),
				  NULL);
		gtk_widget_show (item);
		got_anything = TRUE;
	}

	if (gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) &&
	    bin_exists (gdm_config_get_string (GDM_KEY_CONFIGURATOR))) {
		item = gtk_menu_item_new_with_mnemonic (_("_Configure Login Manager..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gdm_run_gdmconfig),
				  NULL);
		gtk_widget_show (item);
		got_anything = TRUE;
	}

	if (gdm_working_command_exists (gdm_config_get_string (GDM_KEY_REBOOT))) {
		item = gtk_menu_item_new_with_mnemonic (_("_Restart"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gdm_login_restart_handler), 
				  NULL);
		gtk_widget_show (GTK_WIDGET (item));
		got_anything = TRUE;
	}
	
	if (gdm_working_command_exists (gdm_config_get_string (GDM_KEY_HALT))) {
		item = gtk_menu_item_new_with_mnemonic (_("Shut _Down"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gdm_login_halt_handler), 
				  NULL);
		gtk_widget_show (GTK_WIDGET (item));
		got_anything = TRUE;
	}

	if (gdm_working_command_exists (gdm_config_get_string (GDM_KEY_SUSPEND))) {
		item = gtk_menu_item_new_with_mnemonic (_("_Suspend"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gdm_login_suspend_handler), 
				  NULL);
		gtk_widget_show (GTK_WIDGET (item));
		got_anything = TRUE;
	}
	
	if (got_anything) {
		item = gtk_menu_item_new_with_mnemonic (_("_Actions"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menubar), item);
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
		gtk_widget_show (GTK_WIDGET (item));
	}
    }

    menu = gdm_login_theme_menu_new ();
    if (menu != NULL) {
	thememenu = gtk_menu_item_new_with_mnemonic (_("_Theme"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menubar), thememenu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (thememenu), menu);
	gtk_widget_show (GTK_WIDGET (thememenu));
    }

    /* Add a quit/disconnect item when in xdmcp mode or flexi mode */
    /* Do note that the order is important, we always want "Quit" for
     * flexi, even if not local (non-local xnest).  and Disconnect
     * only for xdmcp */
    if ( ! ve_string_empty (g_getenv ("GDM_FLEXI_SERVER"))) {
	    item = gtk_menu_item_new_with_mnemonic (_("_Quit"));
    } else if (ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
	    item = gtk_menu_item_new_with_mnemonic (_("D_isconnect"));
    } else {
	    item = NULL;
    }
    if (item != NULL) {
	    gtk_menu_shell_append (GTK_MENU_SHELL (menubar), item);
	    gtk_widget_show (GTK_WIDGET (item));
	    g_signal_connect (G_OBJECT (item), "activate",
			      G_CALLBACK (gtk_main_quit), NULL);
    }

    /* The clock */
    clock_label = gtk_label_new ("");
    gtk_widget_show (clock_label);
    item = gtk_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (item), clock_label);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (menubar), item);
    gtk_menu_item_set_right_justified (GTK_MENU_ITEM (item), TRUE);
    GTK_WIDGET_UNSET_FLAGS (item, GTK_SENSITIVE);

    g_signal_connect (G_OBJECT (clock_label), "destroy",
		      G_CALLBACK (gtk_widget_destroyed),
		      &clock_label);

    update_clock (); 

    if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER))
	rows = 2;
    else
	rows = 1;

    table = gtk_table_new (rows, 2, FALSE);
    gtk_widget_ref (table);
    g_object_set_data_full (G_OBJECT (login), "table", table,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (table);
    gtk_box_pack_start (GTK_BOX (mbox), table, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (table), 10);
    gtk_table_set_row_spacings (GTK_TABLE (table), 10);
    gtk_table_set_col_spacings (GTK_TABLE (table), 10);

    if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER)) {
	    int height;
	    GtkTreeSelection *selection;
	    GtkTreeViewColumn *column;

	    browser = gtk_tree_view_new ();
	    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (browser), FALSE);
	    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (browser),
					       FALSE);
	    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
	    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	    g_signal_connect (selection, "changed",
			      G_CALLBACK (user_selected),
			      NULL);

	    g_signal_connect (browser, "button_release_event",
			      G_CALLBACK (browser_change_focus),
			      NULL);

	    browser_model = (GtkTreeModel *)gtk_list_store_new (3,
								GDK_TYPE_PIXBUF,
								G_TYPE_STRING,
								G_TYPE_STRING);
	    gtk_tree_view_set_model (GTK_TREE_VIEW (browser), browser_model);
	    column = gtk_tree_view_column_new_with_attributes
		    (_("Icon"),
		     gtk_cell_renderer_pixbuf_new (),
		     "pixbuf", GREETER_ULIST_ICON_COLUMN,
		     NULL);
	    gtk_tree_view_append_column (GTK_TREE_VIEW (browser), column);
      
	    column = gtk_tree_view_column_new_with_attributes
		    (_("Username"),
		     gtk_cell_renderer_text_new (),
		     "markup", GREETER_ULIST_LABEL_COLUMN,
		     NULL);
	    gtk_tree_view_append_column (GTK_TREE_VIEW (browser), column);

	    bbox = gtk_scrolled_window_new (NULL, NULL);
	    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (bbox),
						 GTK_SHADOW_IN);
	    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (bbox),
					    GTK_POLICY_NEVER,
					    GTK_POLICY_AUTOMATIC);
	    gtk_container_add (GTK_CONTAINER (bbox), browser);
	
	    height = size_of_users + 4 /* some padding */;
	    if (height > gdm_wm_screen.height * 0.25)
		    height = gdm_wm_screen.height * 0.25;

	    gtk_widget_set_size_request (GTK_WIDGET (bbox), -1, height);
    }

    if (gdm_config_get_string (GDM_KEY_LOGO) != NULL) {
	    pb = gdk_pixbuf_new_from_file (gdm_config_get_string (GDM_KEY_LOGO), NULL);
    } else {
	    pb = NULL;
    }

    if (pb != NULL) {
	    have_logo = TRUE;
	    logo_image = gtk_image_new_from_pixbuf (pb);
	    lw = gdk_pixbuf_get_width (pb);
	    lh = gdk_pixbuf_get_height (pb);
	    g_object_unref (G_OBJECT (pb));
    } else {
	    logo_image = gtk_image_new ();
	    lw = lh = 100;
    }

    /* this will make the logo always left justified */
    logo_frame = gtk_alignment_new (0, 0.10, 0, 0);
    gtk_widget_show (logo_frame);

    frame = gtk_frame_new (NULL);
    gtk_widget_show (frame);
    gtk_frame_set_shadow_type (GTK_FRAME (frame),
			       GTK_SHADOW_IN);

    ebox = gtk_event_box_new ();
    gtk_widget_show (ebox);
    gtk_container_add (GTK_CONTAINER (ebox), logo_image);
    gtk_container_add (GTK_CONTAINER (frame), ebox);
    gtk_container_add (GTK_CONTAINER (logo_frame), frame);

    if (lw > gdm_wm_screen.width / 2)
	    lw = gdm_wm_screen.width / 2;
    else
	    lw = -1;
    if (lh > (2 * gdm_wm_screen.height) / 3)
	    lh = (2 * gdm_wm_screen.height) / 3;
    else
	    lh = -1;
    if (lw > -1 || lh > -1)
	    gtk_widget_set_size_request (logo_image, lw, lh);
    gtk_widget_show (GTK_WIDGET (logo_image));

    stack = gtk_table_new (7, 1, FALSE);
    gtk_widget_ref (stack);
    g_object_set_data_full (G_OBJECT (login), "stack", stack,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (stack);

    /* Welcome msg */
    welcome = gtk_label_new (NULL);
    gdm_set_welcomemsg ();
    gtk_widget_set_name (welcome, _("Welcome"));
    gtk_widget_ref (welcome);
    g_object_set_data_full (G_OBJECT (login), "welcome", welcome,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (welcome);
    gtk_table_attach (GTK_TABLE (stack), welcome, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 15);

    /* Put in error box here */
    err_box = gtk_label_new (NULL);
    gtk_widget_set_name (err_box, "Error box");
    gtk_widget_set_size_request (err_box, -1, 40);
    g_signal_connect (G_OBJECT (err_box), "destroy",
		      G_CALLBACK (gtk_widget_destroyed),
		      &err_box);
    gtk_label_set_line_wrap (GTK_LABEL (err_box), TRUE);
    gtk_table_attach (GTK_TABLE (stack), err_box, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);


    hline1 = gtk_hseparator_new ();
    gtk_widget_ref (hline1);
    g_object_set_data_full (G_OBJECT (login), "hline1", hline1,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hline1);
    gtk_table_attach (GTK_TABLE (stack), hline1, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 6);
    
    label = gtk_label_new_with_mnemonic (_("_Username:"));
    gtk_widget_ref (label);
    g_object_set_data_full (G_OBJECT (login), "label", label,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (stack), label, 0, 1, 3, 4,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (label), 10, 5);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    
    entry = gtk_entry_new ();
    g_signal_connect (G_OBJECT (entry), "key_press_event",
		      G_CALLBACK (key_press_event), NULL);
    if (gdm_config_get_bool (GDM_KEY_ENTRY_INVISIBLE))
	    gtk_entry_set_invisible_char (GTK_ENTRY (entry), 0);
    else if (gdm_config_get_bool (GDM_KEY_ENTRY_CIRCLES))
	    gtk_entry_set_invisible_char (GTK_ENTRY (entry), 0x25cf);
    gtk_entry_set_max_length (GTK_ENTRY (entry), PW_ENTRY_SIZE);
    gtk_widget_set_size_request (entry, 250, -1);
    gtk_widget_ref (entry);
    g_object_set_data_full (G_OBJECT (login), "entry", entry,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_entry_set_text (GTK_ENTRY (entry), "");
    gtk_widget_show (entry);
    gtk_table_attach (GTK_TABLE (stack), entry, 0, 1, 4, 5,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 10, 0);
    g_signal_connect (G_OBJECT(entry), "activate", 
		      G_CALLBACK (gdm_login_enter),
		      NULL);

    /* cursor blinking is evil on remote displays, don't do it forever */
    gdm_common_setup_blinking ();
    gdm_common_setup_blinking_entry (entry);
    
    hline2 = gtk_hseparator_new ();
    gtk_widget_ref (hline2);
    g_object_set_data_full (G_OBJECT (login), "hline2", hline2,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hline2);
    gtk_table_attach (GTK_TABLE (stack), hline2, 0, 1, 5, 6,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 10);

    msg = gtk_label_new (_("Please enter your username"));
    gtk_widget_set_name (msg, "Message");
    gtk_label_set_line_wrap (GTK_LABEL (msg), TRUE);
    gtk_label_set_justify (GTK_LABEL (msg), GTK_JUSTIFY_LEFT);
    gtk_table_attach (GTK_TABLE (stack), msg, 0, 1, 6, 7,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 10);
    gtk_widget_set_size_request (msg, -1, 30);

    gtk_widget_ref (msg);
    g_object_set_data_full (G_OBJECT (login), "msg", msg,
			    (GDestroyNotify) gtk_widget_unref);
    gtk_widget_show (msg);

    auto_timed_msg = gtk_label_new ("");
    gtk_widget_set_name (auto_timed_msg, "Message");
    gtk_label_set_line_wrap (GTK_LABEL (auto_timed_msg), TRUE);
    gtk_label_set_justify (GTK_LABEL (auto_timed_msg), GTK_JUSTIFY_LEFT);
    gtk_table_attach (GTK_TABLE (stack), auto_timed_msg, 0, 1, 7, 8,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 10);
    gtk_widget_set_size_request (auto_timed_msg, -1, 15);
    
    gtk_widget_ref (auto_timed_msg);
    gtk_widget_show (auto_timed_msg);

    /* FIXME: No Documentation yet.... */
    /*help_button = gtk_button_new_from_stock (GTK_STOCK_OK);
    GTK_WIDGET_UNSET_FLAGS (help_button, GTK_CAN_FOCUS);
    gtk_widget_show (help_button);*/

    ok_button = gtk_button_new_from_stock (GTK_STOCK_OK);
    GTK_WIDGET_UNSET_FLAGS (ok_button, GTK_CAN_FOCUS);
    g_signal_connect (G_OBJECT (ok_button), "clicked",
		      G_CALLBACK (gdm_login_ok_button_press),
		      entry);
    gtk_widget_show (ok_button);

    cancel_button = gtk_button_new_with_mnemonic (_("_Start Again"));
    GTK_WIDGET_UNSET_FLAGS (cancel_button, GTK_CAN_FOCUS);
    g_signal_connect (G_OBJECT (cancel_button), "clicked",
		      G_CALLBACK (gdm_login_cancel_button_press),
		      entry);
    gtk_widget_show (cancel_button);

    button_box = gtk_hbutton_box_new ();
    gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box),
                               GTK_BUTTONBOX_END);
    gtk_box_set_spacing (GTK_BOX (button_box), 10);

#if 0
    gtk_box_pack_end (GTK_BOX (button_box), GTK_WIDGET (help_button),
                      FALSE, TRUE, 0);
    gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (button_box), help_button, TRUE);
#endif

    gtk_box_pack_end (GTK_BOX (button_box), GTK_WIDGET (cancel_button),
		      FALSE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (button_box), GTK_WIDGET (ok_button),
		      FALSE, TRUE, 0);
    gtk_widget_show (button_box);
    
    gtk_table_attach (GTK_TABLE (stack), button_box, 0, 1, 8, 9,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 10, 10);

    /* Put it nicely together */

    if (bbox != NULL) {
	    gtk_table_attach (GTK_TABLE (table), bbox, 0, 2, 0, 1,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), logo_frame, 0, 1, 1, 2,
			      (GtkAttachOptions) (GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 1, 2,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    } else {
	    gtk_table_attach (GTK_TABLE (table), logo_frame, 0, 1, 0, 1,
			      (GtkAttachOptions) (0),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 0, 1,
			      (GtkAttachOptions) (0),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    }
    
    gtk_widget_grab_focus (entry);	
    gtk_window_set_focus (GTK_WINDOW (login), entry);	
    g_object_set (G_OBJECT (login),
		  "allow_grow", TRUE,
		  "allow_shrink", TRUE,
		  "resizable", TRUE,
		  NULL);
    
    /* do it now, and we'll also do it later */
    if (GdmSetPosition) {
	    set_screen_pos (login, GdmPositionX, GdmPositionY);
    } else {
	    gdm_wm_center_window (GTK_WINDOW (login));
    }

    g_signal_connect (G_OBJECT (login), "focus_in_event", 
		      G_CALLBACK (gdm_login_focus_in),
		      NULL);
    g_signal_connect (G_OBJECT (login), "focus_out_event", 
		      G_CALLBACK (gdm_login_focus_out),
		      NULL);

    gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

    /* normally disable the prompt first */
    if ( ! DOING_GDM_DEVELOPMENT) {
	    gtk_widget_set_sensitive (entry, FALSE);
	    gtk_widget_set_sensitive (ok_button, FALSE);
	    gtk_widget_set_sensitive (cancel_button, FALSE);
    }

    gtk_widget_show_all (GTK_WIDGET (login));
    if ( ! have_logo) {
	    gtk_table_set_col_spacings (GTK_TABLE (table), 0);
	    gtk_widget_hide (logo_frame);
    }
}

static void
set_root (GdkPixbuf *pb)
{
	GdkPixmap *pm;
	gint width, height;

	g_return_if_fail (pb != NULL);

	gdk_drawable_get_size (gdk_get_default_root_window (), &width, &height);
	pm = gdk_pixmap_new (gdk_get_default_root_window (), 
			width, height, -1);


	/* paranoia */
	if (pm == NULL)
		return;

	gdk_draw_pixbuf (pm, NULL, pb, 0, 0, 0, 0, -1, -1, 
			GDK_RGB_DITHER_MAX, 0, 0);

	gdk_error_trap_push ();

	gdk_window_set_back_pixmap (gdk_get_default_root_window (),
				    pm,
				    FALSE /* parent_relative */);

	g_object_unref (G_OBJECT (pm));

	gdk_window_clear (gdk_get_default_root_window ());

	gdk_flush ();
	gdk_error_trap_pop ();
}

static GdkPixbuf *
render_scaled_back (const GdkPixbuf *pb)
{
	int i;
	int width, height;

	GdkPixbuf *back = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
					  gdk_pixbuf_get_has_alpha (pb),
					  8,
					  gdk_screen_width (),
					  gdk_screen_height ());

	width = gdk_pixbuf_get_width (pb);
	height = gdk_pixbuf_get_height (pb);

	for (i = 0; i < gdm_wm_screens; i++) {
		gdk_pixbuf_scale (pb, back,
				  gdm_wm_allscreens[i].x,
				  gdm_wm_allscreens[i].y,
				  gdm_wm_allscreens[i].width,
				  gdm_wm_allscreens[i].height,
				  gdm_wm_allscreens[i].x /* offset_x */,
				  gdm_wm_allscreens[i].y /* offset_y */,
				  (double) gdm_wm_allscreens[i].width / width,
				  (double) gdm_wm_allscreens[i].height / height,
				  GDK_INTERP_BILINEAR);
	}

	return back;
}

static void
add_color_to_pb (GdkPixbuf *pb, GdkColor *color)
{
	int width = gdk_pixbuf_get_width (pb);
	int height = gdk_pixbuf_get_height (pb);
	int rowstride = gdk_pixbuf_get_rowstride (pb);
	guchar *pixels = gdk_pixbuf_get_pixels (pb);
	gboolean has_alpha = gdk_pixbuf_get_has_alpha (pb);
	int i;
	int cr = color->red >> 8;
	int cg = color->green >> 8;
	int cb = color->blue >> 8;

	if ( ! has_alpha)
		return;

	for (i = 0; i < height; i++) {
		int ii;
		guchar *p = pixels + (rowstride * i);
		for (ii = 0; ii < width; ii++) {
			int r = p[0];
			int g = p[1];
			int b = p[2];
			int a = p[3];

			p[0] = (r * a + cr * (255 - a)) >> 8;
			p[1] = (g * a + cg * (255 - a)) >> 8;
			p[2] = (b * a + cb * (255 - a)) >> 8;
			p[3] = 255;

			p += 4;
		}
	}
}

/* setup background color/image */
static void
setup_background (void)
{
	GdkColor color;
	GdkPixbuf *pb = NULL;
	gchar *bg_color = gdm_config_get_string (GDM_KEY_BACKGROUND_COLOR);
	gchar *bg_image = gdm_config_get_string (GDM_KEY_BACKGROUND_IMAGE);
	gint   bg_type  = gdm_config_get_int    (GDM_KEY_BACKGROUND_TYPE); 

	if ((bg_type == GDM_BACKGROUND_IMAGE ||
	     bg_type == GDM_BACKGROUND_IMAGE_AND_COLOR) &&
	    ! ve_string_empty (bg_image))
		pb = gdk_pixbuf_new_from_file (bg_image, NULL);

	/* Load background image */
	if (pb != NULL) {
		if (gdk_pixbuf_get_has_alpha (pb)) {
			if (bg_type == GDM_BACKGROUND_IMAGE_AND_COLOR) {
				if (bg_color == NULL ||
				    bg_color[0] == '\0' ||
				    ! gdk_color_parse (bg_color,
					       &color)) {
					gdk_color_parse ("#000000", &color);
				}
				add_color_to_pb (pb, &color);
			}
		}
		if (gdm_config_get_bool (GDM_KEY_BACKGROUND_SCALE_TO_FIT)) {
			GdkPixbuf *spb = render_scaled_back (pb);
			g_object_unref (G_OBJECT (pb));
			pb = spb;
		}

		/* paranoia */
		if (pb != NULL) {
			set_root (pb);
			g_object_unref (G_OBJECT (pb));
		}
	/* Load background color */
	} else if (bg_type != GDM_BACKGROUND_NONE &&
	           bg_type != GDM_BACKGROUND_IMAGE) {
		gdm_common_setup_background_color (bg_color);
	/* Load default background */
	} else {
		gchar *blank_color = g_strdup ("#000000");
		gdm_common_setup_background_color (blank_color);
	}
}

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

/* 
 * If new configuration keys are added to this program, make sure to add the
 * key to the gdm_read_config and gdm_reread_config functions.  Note if the
 * number of configuration values used by gdmlogin is greater than 
 * GDM_SUP_MAX_MESSAGES defined in daemon/gdm.h (currently defined to be 80),
 * consider bumping that number so that all the config can be read in one
 * socket connection.
 */
static void
gdm_read_config (void)
{
	/* Read config data in bulk */
	gdmcomm_comm_bulk_start ();

	gdmcomm_set_debug (gdm_config_get_bool (GDM_KEY_DEBUG));

	/*
	 * Read all the keys at once and close sockets connection so we do
	 * not have to keep the socket open. 
	 */
	gdm_config_get_string (GDM_KEY_BACKGROUND_COLOR);
	gdm_config_get_string (GDM_KEY_BACKGROUND_IMAGE);
	gdm_config_get_string (GDM_KEY_BACKGROUND_PROGRAM);
	gdm_config_get_string (GDM_KEY_CONFIGURATOR);
	gdm_config_get_string (GDM_KEY_DEFAULT_FACE);
	gdm_config_get_string (GDM_KEY_DEFAULT_SESSION);
	gdm_config_get_string (GDM_KEY_EXCLUDE);
	gdm_config_get_string (GDM_KEY_GTK_THEME);
	gdm_config_get_string (GDM_KEY_GTK_THEMES_TO_ALLOW);
	gdm_config_get_string (GDM_KEY_GTKRC);
	gdm_config_get_string (GDM_KEY_HALT);
	gdm_config_get_string (GDM_KEY_INCLUDE);
	gdm_config_get_string (GDM_KEY_INFO_MSG_FILE);
	gdm_config_get_string (GDM_KEY_INFO_MSG_FONT);
	gdm_config_get_string (GDM_KEY_LOCALE_FILE);
	gdm_config_get_string (GDM_KEY_LOGO);
	gdm_config_get_string (GDM_KEY_REBOOT);
	gdm_config_get_string (GDM_KEY_REMOTE_WELCOME);
	gdm_config_get_string (GDM_KEY_SESSION_DESKTOP_DIR);
	gdm_config_get_string (GDM_KEY_SOUND_PROGRAM);
	gdm_config_get_string (GDM_KEY_SOUND_ON_LOGIN_FILE);
	gdm_config_get_string (GDM_KEY_SUSPEND);
	gdm_config_get_string (GDM_KEY_TIMED_LOGIN);
	gdm_config_get_string (GDM_KEY_USE_24_CLOCK);
	gdm_config_get_string (GDM_KEY_WELCOME);

	gdm_config_get_int    (GDM_KEY_BACKGROUND_TYPE);
	gdm_config_get_int    (GDM_KEY_BACKGROUND_PROGRAM_INITIAL_DELAY);
	gdm_config_get_int    (GDM_KEY_BACKGROUND_PROGRAM_RESTART_DELAY);
	gdm_config_get_int    (GDM_KEY_FLEXI_REAP_DELAY_MINUTES);
	gdm_config_get_int    (GDM_KEY_MAX_ICON_HEIGHT);
	gdm_config_get_int    (GDM_KEY_MAX_ICON_WIDTH);
	gdm_config_get_int    (GDM_KEY_MINIMAL_UID);
	gdm_config_get_int    (GDM_KEY_TIMED_LOGIN_DELAY);
	gdm_config_get_int    (GDM_KEY_XINERAMA_SCREEN);

	gdm_config_get_bool   (GDM_KEY_ALLOW_GTK_THEME_CHANGE);
	gdm_config_get_bool   (GDM_KEY_ALLOW_REMOTE_ROOT);
	gdm_config_get_bool   (GDM_KEY_ALLOW_ROOT);
	gdm_config_get_bool   (GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR);
	gdm_config_get_bool   (GDM_KEY_BACKGROUND_SCALE_TO_FIT);
	gdm_config_get_bool   (GDM_KEY_BROWSER);
	gdm_config_get_bool   (GDM_KEY_CHOOSER_BUTTON);
	gdm_config_get_bool   (GDM_KEY_CONFIG_AVAILABLE);
	gdm_config_get_bool   (GDM_KEY_DEFAULT_REMOTE_WELCOME);
	gdm_config_get_bool   (GDM_KEY_DEFAULT_WELCOME);
	gdm_config_get_bool   (GDM_KEY_ENTRY_CIRCLES);
	gdm_config_get_bool   (GDM_KEY_ENTRY_INVISIBLE);
	gdm_config_get_bool   (GDM_KEY_INCLUDE_ALL);
	gdm_config_get_bool   (GDM_KEY_QUIVER);
	gdm_config_get_bool   (GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS);
	gdm_config_get_bool   (GDM_KEY_RESTART_BACKGROUND_PROGRAM);
	gdm_config_get_bool   (GDM_KEY_SHOW_GNOME_FAILSAFE);
	gdm_config_get_bool   (GDM_KEY_SHOW_LAST_SESSION);
	gdm_config_get_bool   (GDM_KEY_SHOW_XTERM_FAILSAFE);
	gdm_config_get_bool   (GDM_KEY_SOUND_ON_LOGIN);
	gdm_config_get_bool   (GDM_KEY_SYSTEM_MENU);
	gdm_config_get_bool   (GDM_KEY_TIMED_LOGIN_ENABLE);
	gdm_config_get_bool   (GDM_KEY_TITLE_BAR);
	gdm_config_get_bool   (GDM_KEY_ADD_GTK_MODULES);

	/* Keys not to include in reread_config */
	gdm_config_get_bool   (GDM_KEY_LOCK_POSITION);
	gdm_config_get_string (GDM_KEY_PID_FILE);
	gdm_config_get_int    (GDM_KEY_POSITION_X);
	gdm_config_get_int    (GDM_KEY_POSITION_Y);
	gdm_config_get_string (GDM_KEY_PRE_FETCH_PROGRAM);
	gdm_config_get_bool   (GDM_KEY_SET_POSITION);

	gdmcomm_comm_bulk_stop ();
}

static gboolean
gdm_reread_config (int sig, gpointer data)
{
	gboolean resize = FALSE;

	/* Read config data in bulk */
	gdmcomm_comm_bulk_start ();

	if (gdm_config_reload_bool (GDM_KEY_DEBUG))
		gdmcomm_set_debug (gdm_config_get_bool (GDM_KEY_DEBUG));

	/* reparse config stuff here.  At least the ones we care about */
	/*
	 * We don't want to reload GDM_KEY_POSITION_X, GDM_KEY_POSITION_Y
	 * GDM_KEY_LOCK_POSITION, and GDM_KEY_SET_POSITION since we don't
	 * want to move the window or lock its position just because the
	 * config default changed.  It would be too confusing to have the
	 * window location move around.  These changes can wait until the
	 * next time gdmlogin is launched.
	 */

	/* FIXME: We should update these on the fly rather than just
         * restarting */
	/* Also we may not need to check ALL those keys but just a few */

	if (gdm_config_reload_string (GDM_KEY_BACKGROUND_PROGRAM) ||
	    gdm_config_reload_string (GDM_KEY_CONFIGURATOR) ||
	    gdm_config_reload_string (GDM_KEY_DEFAULT_FACE) ||
	    gdm_config_reload_string (GDM_KEY_DEFAULT_SESSION) ||
	    gdm_config_reload_string (GDM_KEY_EXCLUDE) ||
	    gdm_config_reload_string (GDM_KEY_GTKRC) ||
	    gdm_config_reload_string (GDM_KEY_GTK_THEME) ||
	    gdm_config_reload_string (GDM_KEY_GTK_THEMES_TO_ALLOW) ||
	    gdm_config_reload_string (GDM_KEY_HALT) ||
	    gdm_config_reload_string (GDM_KEY_INCLUDE) ||
	    gdm_config_reload_string (GDM_KEY_INFO_MSG_FILE) ||
	    gdm_config_reload_string (GDM_KEY_INFO_MSG_FONT) ||
	    gdm_config_reload_string (GDM_KEY_LOCALE_FILE) ||
	    gdm_config_reload_string (GDM_KEY_REBOOT) ||
	    gdm_config_reload_string (GDM_KEY_SESSION_DESKTOP_DIR) ||
	    gdm_config_reload_string (GDM_KEY_SUSPEND) ||
	    gdm_config_reload_string (GDM_KEY_TIMED_LOGIN) ||

	    gdm_config_reload_int    (GDM_KEY_BACKGROUND_PROGRAM_INITIAL_DELAY) ||
	    gdm_config_reload_int    (GDM_KEY_BACKGROUND_PROGRAM_RESTART_DELAY) ||
	    gdm_config_reload_int    (GDM_KEY_MAX_ICON_WIDTH) ||
	    gdm_config_reload_int    (GDM_KEY_MAX_ICON_HEIGHT) ||
	    gdm_config_reload_int    (GDM_KEY_MINIMAL_UID) ||
	    gdm_config_reload_int    (GDM_KEY_TIMED_LOGIN_DELAY) ||
	    gdm_config_reload_int    (GDM_KEY_XINERAMA_SCREEN) ||

	    gdm_config_reload_bool   (GDM_KEY_ALLOW_GTK_THEME_CHANGE) ||
	    gdm_config_reload_bool   (GDM_KEY_ALLOW_ROOT) ||
	    gdm_config_reload_bool   (GDM_KEY_ALLOW_REMOTE_ROOT) ||
	    gdm_config_reload_bool   (GDM_KEY_BROWSER) ||
	    gdm_config_reload_bool   (GDM_KEY_CHOOSER_BUTTON) ||
	    gdm_config_reload_bool   (GDM_KEY_CONFIG_AVAILABLE) ||
	    gdm_config_reload_bool   (GDM_KEY_ENTRY_CIRCLES) ||
	    gdm_config_reload_bool   (GDM_KEY_ENTRY_INVISIBLE) ||
	    gdm_config_reload_bool   (GDM_KEY_INCLUDE_ALL) ||
	    gdm_config_reload_bool   (GDM_KEY_QUIVER) ||
	    gdm_config_reload_bool   (GDM_KEY_RESTART_BACKGROUND_PROGRAM) ||
	    gdm_config_reload_bool   (GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS) ||
	    gdm_config_reload_bool   (GDM_KEY_SHOW_GNOME_FAILSAFE) ||
	    gdm_config_reload_bool   (GDM_KEY_SHOW_LAST_SESSION) ||
	    gdm_config_reload_bool   (GDM_KEY_SHOW_XTERM_FAILSAFE) ||
	    gdm_config_reload_bool   (GDM_KEY_SYSTEM_MENU) ||
	    gdm_config_reload_bool   (GDM_KEY_TIMED_LOGIN_ENABLE) ||
	    gdm_config_reload_bool   (GDM_KEY_TITLE_BAR) ||
	    gdm_config_reload_bool   (GDM_KEY_ADD_GTK_MODULES)) {

		/* Set busy cursor */
		gdm_common_setup_cursor (GDK_WATCH);

		gdm_wm_save_wm_order ();
		gdm_kill_thingies ();
		gdmcomm_comm_bulk_stop ();

		_exit (DISPLAY_RESTARTGREETER);
		return TRUE;
	}

	if (gdm_config_reload_string (GDM_KEY_BACKGROUND_IMAGE) ||
	    gdm_config_reload_string (GDM_KEY_BACKGROUND_COLOR) ||
	    gdm_config_reload_int    (GDM_KEY_BACKGROUND_TYPE) ||
	    gdm_config_reload_bool   (GDM_KEY_BACKGROUND_SCALE_TO_FIT) ||
	    gdm_config_reload_bool   (GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR)) {

		gdm_kill_thingies ();
		setup_background ();
		back_prog_launch_after_timeout ();
	}

	gdm_config_reload_string (GDM_KEY_SOUND_PROGRAM);
	gdm_config_reload_bool   (GDM_KEY_SOUND_ON_LOGIN);
	gdm_config_reload_string (GDM_KEY_SOUND_ON_LOGIN_FILE);
	gdm_config_reload_string (GDM_KEY_USE_24_CLOCK);
	update_clock ();

	if (gdm_config_reload_string (GDM_KEY_LOGO)) {
		GdkPixbuf *pb;
		gboolean have_logo = FALSE;
		gchar *gdmlogo;
		int lw, lh;

		gdmlogo = gdm_config_get_string (GDM_KEY_LOGO);

		if (gdmlogo != NULL) {
			pb = gdk_pixbuf_new_from_file (gdmlogo, NULL);
		} else {
			pb = NULL;
		}

		if (pb != NULL) {
			have_logo = TRUE;
			gtk_image_set_from_pixbuf (GTK_IMAGE (logo_image), pb);
			lw = gdk_pixbuf_get_width (pb);
			lh = gdk_pixbuf_get_height (pb);
			g_object_unref (G_OBJECT (pb));
		} else {
			lw = lh = 100;
		}

		if (lw > gdm_wm_screen.width / 2)
			lw = gdm_wm_screen.width / 2;
		else
			lw = -1;
		if (lh > (2 * gdm_wm_screen.height) / 3)
			lh = (2 * gdm_wm_screen.height) / 3;
		else
			lh = -1;
		if (lw > -1 || lh > -1)
			gtk_widget_set_size_request (logo_image, lw, lh);

		if (have_logo) {
			gtk_table_set_col_spacings (GTK_TABLE (table), 10);
			gtk_widget_show (logo_frame);
		} else {
			gtk_table_set_col_spacings (GTK_TABLE (table), 0);
			gtk_widget_hide (logo_frame);
		}

		resize = TRUE;
	}

	if (gdm_config_reload_string (GDM_KEY_WELCOME) ||
            gdm_config_reload_bool   (GDM_KEY_DEFAULT_WELCOME) ||
            gdm_config_reload_string (GDM_KEY_REMOTE_WELCOME) ||
            gdm_config_reload_bool   (GDM_KEY_DEFAULT_REMOTE_WELCOME)) {

		gdm_set_welcomemsg ();
	}

	if (resize)
		login_window_resize (TRUE /* force */);

	gdmcomm_comm_bulk_stop ();

	return TRUE;
}

int 
main (int argc, char *argv[])
{
    struct sigaction hup;
    struct sigaction term;
    sigset_t mask;
    GIOChannel *ctrlch;
    const char *gdm_version;
    const char *gdm_protocol_version;
    guint sid;

    if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
	    DOING_GDM_DEVELOPMENT = TRUE;

    gdm_common_openlog ("gdmlogin", LOG_PID, LOG_DAEMON);

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    /*
     * gdm_common_atspi_launch () needs gdk initialized.
     * We cannot start gtk before the registry is running 
     * because the atk-bridge will crash.
     */
    gdk_init (&argc, &argv);
    if ( ! DOING_GDM_DEVELOPMENT) {
       gdm_common_atspi_launch ();
    }

    gtk_init (&argc, &argv);

    if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
	disable_sys_config_chooser_buttons = TRUE;

    /* Read all configuration at once, so the values get cached */
    gdm_read_config ();

    GdmLockPosition = gdm_config_get_bool (GDM_KEY_LOCK_POSITION);
    GdmSetPosition  = gdm_config_get_bool (GDM_KEY_SET_POSITION);
    GdmPositionX    = gdm_config_get_int  (GDM_KEY_POSITION_X);
    GdmPositionY    = gdm_config_get_int  (GDM_KEY_POSITION_Y);
    setlocale (LC_ALL, "");

    gdm_wm_screen_init (gdm_config_get_int (GDM_KEY_XINERAMA_SCREEN));

    gdm_version = g_getenv ("GDM_VERSION");
    gdm_protocol_version = g_getenv ("GDM_GREETER_PROTOCOL_VERSION");

    /* Load the background as early as possible so GDM does not leave  */
    /* the background unfilled.   The cursor should be a watch already */
    /* but just in case */
    setup_background ();
    gdm_common_setup_cursor (GDK_WATCH);

    if ( ! DOING_GDM_DEVELOPMENT &&
	 ((gdm_protocol_version != NULL &&
	   strcmp (gdm_protocol_version, GDM_GREETER_PROTOCOL_VERSION) != 0) ||
	  (gdm_protocol_version == NULL &&
	   (gdm_version == NULL ||
	    strcmp (gdm_version, VERSION) != 0))) &&
	        ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
	    GtkWidget *dialog;
	    gchar *msg;

	    gdm_wm_init (0);

	    gdm_wm_focus_new_windows (TRUE);
	    
	    msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
				     "version. "
				     "You have probably just upgraded GDM. "
				     "Please restart the GDM daemon or the computer."),
				   VERSION);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_OK,
					_("Cannot start the greeter"),
					msg);
	    g_free (msg);

	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    gtk_dialog_run (GTK_DIALOG (dialog));

	    return EXIT_SUCCESS;
    }

    if ( ! DOING_GDM_DEVELOPMENT &&
	gdm_protocol_version == NULL &&
	gdm_version == NULL) {
	    GtkWidget *dialog;
	    gchar *msg;

	    gdm_wm_init (0);

	    gdm_wm_focus_new_windows (TRUE);
	    
	    msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
	                             "version. "
	                             "You have probably just upgraded GDM. "
	                             "Please restart the GDM daemon or the computer."),
	                           VERSION);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_WARNING,
					GTK_BUTTONS_NONE,
					_("Cannot start the greeter"),
					msg);	
	    g_free (msg);

	    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				    _("Restart"),
				    RESPONSE_REBOOT,
				    GTK_STOCK_CLOSE,
				    RESPONSE_CLOSE,
				    NULL);

	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	    case RESPONSE_REBOOT:
		    gtk_widget_destroy (dialog);
		    return DISPLAY_REBOOT;
	    default:
		    gtk_widget_destroy (dialog);
		    return DISPLAY_ABORT;
	    }
    }

    if ( ! DOING_GDM_DEVELOPMENT &&
	 ((gdm_protocol_version != NULL &&
	   strcmp (gdm_protocol_version, GDM_GREETER_PROTOCOL_VERSION) != 0) ||
	  (gdm_protocol_version == NULL &&
	   strcmp (gdm_version, VERSION) != 0))) {
	    GtkWidget *dialog;
	    gchar *msg;

	    gdm_wm_init (0);

	    gdm_wm_focus_new_windows (TRUE);
	    
	    msg = g_strdup_printf (_("The greeter version (%s) does not match the daemon "
	                             "version (%s).  "
	                             "You have probably just upgraded GDM.  "
	                             "Please restart the GDM daemon or the computer."),
	                           VERSION, gdm_version);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_WARNING,
					GTK_BUTTONS_NONE,
					_("Cannot start the greeter"),
					msg);
	    g_free (msg);

	    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				    _("Restart GDM"),
				    RESPONSE_RESTART,
				    _("Restart computer"),
				    RESPONSE_REBOOT,
				    GTK_STOCK_CLOSE,
				    RESPONSE_CLOSE,
				    NULL);


	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gtk_dialog_set_default_response (GTK_DIALOG (dialog), RESPONSE_RESTART);

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	    case RESPONSE_RESTART:
		    gtk_widget_destroy (dialog);
		    return DISPLAY_RESTARTGDM;
	    case RESPONSE_REBOOT:
		    gtk_widget_destroy (dialog);
		    return DISPLAY_REBOOT;
	    default:
		    gtk_widget_destroy (dialog);
		    return DISPLAY_ABORT;
	    }
    }

    if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER)) {
	    defface = gdm_common_get_face (NULL,
					   gdm_config_get_string (GDM_KEY_DEFAULT_FACE),
					   gdm_config_get_int (GDM_KEY_MAX_ICON_WIDTH),
					   gdm_config_get_int (GDM_KEY_MAX_ICON_HEIGHT));

	    if (! defface) {
		    gdm_common_warning ("Could not open DefaultImage: %s.  Suspending face browser!",
			    gdm_config_get_string (GDM_KEY_DEFAULT_FACE));
		    browser_ok = FALSE;
	    } else  {
		    gdm_users_init (&users, &users_string, NULL, defface,
				    &size_of_users, login_is_local, !DOING_GDM_DEVELOPMENT);
	    }
    }

    gdm_login_gui_init ();

    if (browser_ok && gdm_config_get_bool (GDM_KEY_BROWSER))
	gdm_login_browser_populate ();

    ve_signal_add (SIGHUP, gdm_reread_config, NULL);

    hup.sa_handler = ve_signal_notify;
    hup.sa_flags = 0;
    sigemptyset (&hup.sa_mask);
    sigaddset (&hup.sa_mask, SIGCHLD);

    if G_UNLIKELY (sigaction (SIGHUP, &hup, NULL) < 0) {
	    gdm_kill_thingies ();
	    gdm_common_fail_greeter (_("%s: Error setting up %s signal handler: %s"), "main",
		"HUP", strerror (errno));
    }

    term.sa_handler = gdm_login_done;
    term.sa_flags = 0;
    sigemptyset (&term.sa_mask);
    sigaddset (&term.sa_mask, SIGCHLD);

    if G_UNLIKELY (sigaction (SIGINT, &term, NULL) < 0) {
	    gdm_kill_thingies ();
	    gdm_common_fail_greeter (_("%s: Error setting up %s signal handler: %s"), "main",
		"INT", strerror (errno));
    }

    if G_UNLIKELY (sigaction (SIGTERM, &term, NULL) < 0) {
	    gdm_kill_thingies ();
	    gdm_common_fail_greeter (_("%s: Error setting up %s signal handler: %s"), "main",
		"TERM", strerror (errno));
    }

    sigemptyset (&mask);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGHUP);
    sigaddset (&mask, SIGINT);
    
    if G_UNLIKELY (sigprocmask (SIG_UNBLOCK, &mask, NULL) == -1) {
	    gdm_kill_thingies ();
	    gdm_common_fail_greeter (_("Could not set signal mask!"));
    }

    g_atexit (gdm_kill_thingies);
    back_prog_launch_after_timeout ();

    if G_LIKELY ( ! DOING_GDM_DEVELOPMENT) {
	    ctrlch = g_io_channel_unix_new (STDIN_FILENO);
	    g_io_channel_set_encoding (ctrlch, NULL, NULL);
	    g_io_channel_set_buffered (ctrlch, TRUE);
	    g_io_channel_set_flags (ctrlch, 
				    g_io_channel_get_flags (ctrlch) | G_IO_FLAG_NONBLOCK,
				    NULL);
	    g_io_add_watch (ctrlch, 
			    G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			    (GIOFunc) gdm_login_ctrl_handler,
			    NULL);
	    g_io_channel_unref (ctrlch);
    }

    /* if in timed mode, delay timeout on keyboard or menu
     * activity */
    if (gdm_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE) &&
        ! ve_string_empty (gdm_config_get_string (GDM_KEY_TIMED_LOGIN))) {
	    sid = g_signal_lookup ("activate",
				   GTK_TYPE_MENU_ITEM);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					gdm_timer_up_delay,
					NULL /* data */,
					NULL /* destroy_notify */);

	    sid = g_signal_lookup ("key_press_event",
				   GTK_TYPE_WIDGET);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					gdm_timer_up_delay,
					NULL /* data */,
					NULL /* destroy_notify */);

	    sid = g_signal_lookup ("button_press_event",
				   GTK_TYPE_WIDGET);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					gdm_timer_up_delay,
					NULL /* data */,
					NULL /* destroy_notify */);
    }

    /* if a flexiserver, reap self after some time */
    if (gdm_config_get_int (GDM_KEY_FLEXI_REAP_DELAY_MINUTES) > 0 &&
	! ve_string_empty (g_getenv ("GDM_FLEXI_SERVER")) &&
	/* but don't reap Xnest flexis */
	ve_string_empty (g_getenv ("GDM_PARENT_DISPLAY"))) {
	    sid = g_signal_lookup ("activate",
				   GTK_TYPE_MENU_ITEM);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					delay_reaping,
					NULL /* data */,
					NULL /* destroy_notify */);

	    sid = g_signal_lookup ("key_press_event",
				   GTK_TYPE_WIDGET);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					delay_reaping,
					NULL /* data */,
					NULL /* destroy_notify */);

	    sid = g_signal_lookup ("button_press_event",
				   GTK_TYPE_WIDGET);
	    g_signal_add_emission_hook (sid,
					0 /* detail */,
					delay_reaping,
					NULL /* data */,
					NULL /* destroy_notify */);

	    last_reap_delay = time (NULL);
	    g_timeout_add (60*1000, reap_flexiserver, NULL);
    }

    sid = g_signal_lookup ("event",
                           GTK_TYPE_WIDGET);
    g_signal_add_emission_hook (sid,
				0 /* detail */,
				gdm_event,
				NULL /* data */,
				NULL /* destroy_notify */);

    gtk_widget_queue_resize (login);
    gtk_widget_show_now (login);

    if (GdmSetPosition) {
	    set_screen_pos (login, GdmPositionX, GdmPositionY);
    } else {
	    gdm_wm_center_window (GTK_WINDOW (login));
    }

    /* can it ever happen that it'd be NULL here ??? */
    if G_UNLIKELY (login->window != NULL) {
	    gdm_wm_init (GDK_WINDOW_XWINDOW (login->window));

	    /* Run the focus, note that this will work no matter what
	     * since gdm_wm_init will set the display to the gdk one
	     * if it fails */
	    gdm_wm_focus_window (GDK_WINDOW_XWINDOW (login->window));
    }

    if G_UNLIKELY (session_dir_whacked_out) {
	    GtkWidget *dialog;

	    gdm_wm_focus_new_windows (TRUE);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_OK,
					_("Session directory is missing"),
					_("Your session directory is missing or empty!  "
					  "There are two available sessions you can use, but "
					  "you should log in and correct the GDM configuration."));
	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    gdm_wm_no_login_focus_push ();
	    gtk_dialog_run (GTK_DIALOG (dialog));
	    gtk_widget_destroy (dialog);
	    gdm_wm_no_login_focus_pop ();
    }

    if G_UNLIKELY (g_getenv ("GDM_WHACKED_GREETER_CONFIG") != NULL) {
	    GtkWidget *dialog;

	    gdm_wm_focus_new_windows (TRUE);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_OK,
					_("Configuration is not correct"),
					_("The configuration file contains an invalid command "
					  "line for the login dialog, so running the "
					  "default command.  Please fix your configuration."));
	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    gdm_wm_no_login_focus_push ();
	    gtk_dialog_run (GTK_DIALOG (dialog));
	    gtk_widget_destroy (dialog);
	    gdm_wm_no_login_focus_pop ();
    }

    gdm_wm_restore_wm_order ();

    gdm_wm_show_info_msg_dialog (gdm_config_get_string (GDM_KEY_INFO_MSG_FILE),
       gdm_config_get_string (GDM_KEY_INFO_MSG_FONT));

    /* Only setup the cursor now since it will be a WATCH from before */
    gdm_common_setup_cursor (GDK_LEFT_PTR);

    gdm_common_pre_fetch_launch ();
    gtk_main ();

    gdm_kill_thingies ();

    return EXIT_SUCCESS;
}

/* EOF */
