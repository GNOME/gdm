/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 Sun Microsystems Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <glib.h>
#include <gmodule.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <vicious.h>
#include <gnome.h>

#include <config.h>

/*
 * Note that CONFIGFILE will have to be changed to something more generic
 * if this module is ever moved outside of gdm.
 */

#define CONFIGFILE EXPANDED_SYSCONFDIR "/gdm/modules/AccessKeyMouseEvents"
#define	iseol(ch)	((ch) == '\r' || (ch) == '\f' || (ch) == '\0' || \
			(ch) == '\n')

typedef enum
{
	GESTURE_TYPE_KEY	= 1 << 0,
	GESTURE_TYPE_MOUSE	= 1 << 1
} GestureType;

typedef struct {
	guint keysym;
	GdkModifierType state;
	guint keycode;
} Key;

typedef struct {
	guint number;
	GdkModifierType state;
} Button;

union Input {
	Key key;
	Button button;
};

typedef struct {
	GestureType type;
	union Input input;
	char *gesture_str;
	GSList *actions;
	guint n_times;
	guint duration;
	guint timeout;
} Gesture;

static int lineno = 0;
static GSList *gesture_list = NULL;
extern char **environ;

static gchar * screen_exec_display_string (GdkScreen *screen, const char *old);
static void create_event_watcher ();
static void load_gestures(gchar *path);
static gchar ** get_exec_environment (XEvent *xevent);
static Gesture * parse_line(gchar *buf);
static gboolean gesture_already_used (Gesture *gesture);
static GdkFilterReturn	
gestures_filter (GdkXEvent *gdk_xevent, GdkEvent *event, gpointer data);
static gint is_mouseX (const gchar *string);

static void
free_gesture (Gesture *gesture)
{
	if (gesture == NULL)
		return;
	g_slist_foreach (gesture->actions, (GFunc)g_free, NULL);
	g_slist_free (gesture->actions);
	g_free (gesture->gesture_str);
	g_free (gesture);
}
			
static gchar *
screen_exec_display_string (GdkScreen *screen, const char *old)
{
#ifdef HAVE_GTK_MULTIHEAD
	GString    *str;
	const gchar *old_display;
	gchar       *retval;
	gchar       *p;
  
	g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

	old_display = gdk_display_get_name (gdk_screen_get_display (screen));

	str = g_string_new ("DISPLAY=");
	g_string_append (str, old_display);

	p = strrchr (str->str, '.');
	if (p && p >  strchr (str->str, ':'))
		g_string_truncate (str, p - str->str);

	g_string_append_printf (str, ".%d", gdk_screen_get_number (screen));

	retval = str->str;

	g_string_free (str, FALSE);

	return retval;
#else
	if (old)
		return g_strdup (old);
	else
		return g_strdup ("DISPLAY=:0.0");
#endif
}

static void create_event_watcher ()
{
	GdkDisplay *display;

	display = gdk_display_get_default();
	if (!display) {
		return;
	}

	/*
	 * Switch off keyboard autorepeat
	 */
	load_gestures(CONFIGFILE);
	gdk_window_add_filter (NULL, gestures_filter, NULL);

	return;
}


static void
load_gestures(gchar *path)
{
	FILE *fp;
	Gesture *tmp_gesture;
	gchar buf[1024];

	fp = fopen (path, "r");
	if (fp == NULL) {
		/* TODO - I18n */
		g_warning ("Cannot open gestures file: %s\n", path);
		return;
	}

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		tmp_gesture = (Gesture *)parse_line(buf);
		if (tmp_gesture) {
		/*
		 * Is the key already associated with an existing gesture?
		 */
			if (strcmp (tmp_gesture->gesture_str, "<Add>") == 0) {
				/*
				 * Add another action to the last gesture
				 */
				Gesture *last_gesture;
				GSList *last_item = g_slist_last(gesture_list);
				/*
				 * If there is no last_item to add onto ignore the entry
				 */
				if (last_item) {
					last_gesture = (Gesture *)last_item->data;
					/* Add the action to the last gesture's actions list */
					last_gesture->actions = 
						g_slist_append(last_gesture->actions,
						g_strdup((gchar *)tmp_gesture->actions->data));
				}
				free_gesture (tmp_gesture);
				/* Ignore duplicate gestures */
			} else if ( ! gesture_already_used (tmp_gesture))
				gesture_list = g_slist_append (gesture_list, tmp_gesture);
			else
				free_gesture (tmp_gesture);
		}
	}
	fclose(fp);
}


/**
 * get_exec_environment:
 *
 * Description: Modifies the current program environment to
 * ensure that $DISPLAY is set such that a launched application
 * inheriting this environment would appear on screen.
 *
 * Returns: a newly-allocated %NULL-terminated array of strings or
 * %NULL on error. Use g_strfreev() to free it.
 *
 * mainly ripped from egg_screen_exec_display_string in 
 * gnome-panel/egg-screen-exec.c
 **/
static gchar **
get_exec_environment (XEvent *xevent)
{
	gchar **retval = NULL;
	gint    i;
	gint    display_index = -1;

	GdkScreen  *screen = NULL;
  	GdkWindow *window = gdk_xid_table_lookup (xevent->xkey.root);
  	if (window)
    	screen = gdk_drawable_get_screen (GDK_DRAWABLE (window));
   
	g_assert (GDK_IS_SCREEN (screen));

	for (i = 0; environ [i]; i++)
		if (!strncmp (environ [i], "DISPLAY", 7))
  			display_index = i;

	if (display_index == -1)
		display_index = i++;

	retval = g_new0 (char *, i + 1);

	for (i = 0; environ [i]; i++)
 		if (i == display_index)
  			retval [i] = screen_exec_display_string (screen, environ[i]);
	else
		retval [i] = g_strdup (environ [i]);

	retval [i] = NULL;

	return retval;
}

static Gesture *
parse_line (gchar *buf)
{
	gchar *keystring;
	gchar *keyservice;
	gint button = 0;
	Gesture *tmp_gesture = NULL;
	static GdkDisplay *display = NULL;
	
	if(!display) {
		if ((display = gdk_display_get_default()) == NULL)
			return NULL;
	}
	lineno++;

	if ((*buf == '#') || (iseol(*buf)) || (buf == NULL))
		return NULL;
	
	/*
	 * Find the key name
	 */
	keystring = strtok (buf, " \t\n\r\f");
	if (keystring == NULL) {
		/* TODO - Error messages */
		return NULL;
	}

	tmp_gesture = g_new0 (Gesture, 1);
	tmp_gesture->gesture_str = g_strdup (keystring);

	if (strcmp (tmp_gesture->gesture_str, "<Add>") != 0) {
		guint n, duration, timeout;
		gchar *tmp_string;

		button = is_mouseX (tmp_gesture->gesture_str);
		if (button > 0) {
			tmp_gesture->type = GESTURE_TYPE_MOUSE;
			tmp_gesture->input.button.number = button;
		} else {
			tmp_gesture->type = GESTURE_TYPE_KEY;
			gtk_accelerator_parse(tmp_gesture->gesture_str, 
	 			&(tmp_gesture->input.key.keysym), 
	 			&(tmp_gesture->input.key.state));
			if (tmp_gesture->input.key.keysym == 0 &&
			    tmp_gesture->input.key.state == 0) {
				/* TODO - Error messages here */
				free_gesture (tmp_gesture);
				return NULL;
			}
			tmp_gesture->input.key.keycode = 
				XKeysymToKeycode(GDK_DISPLAY_XDISPLAY (display), tmp_gesture->input.key.keysym);
		}

		if (tmp_gesture->type == 0) {
			/* TODO - Error messages here */
			free_gesture (tmp_gesture);
			return NULL;
		}
		/* [TODO] Need to clean up here. */
		 
		/*
		 * Find the repetition number
		 */
		tmp_string = strtok (NULL, " \t\n\r\f");
		if (tmp_string == NULL) {
			/* TODO - Error messages */
			free_gesture (tmp_gesture);
			return NULL;
		}

		/* TODO - the above doesn't check for the string to
		   be all digits */

		if ((n=atoi(tmp_string)) <= 0) {
			/* Add an error message */
			free_gesture (tmp_gesture);
			return NULL;
		}
		tmp_gesture->n_times = n;

		/*
		 * Find the key press duration (in ms)
		 */
		tmp_string = strtok (NULL, " \t\n\r\f");
		if (tmp_string == NULL) {
			/* TODO - Error messages */
			free_gesture (tmp_gesture);
			return NULL;
		}
		/* TODO - the above doesn't check for the string to
		   be all digits */

		duration = atoi (tmp_string);
		if (duration < 0) {
			/* Add an error message */
			free_gesture (tmp_gesture);
			return NULL;
		}
		tmp_gesture->duration = duration;

		/*
		 * Find the timeout duration (in ms). Timeout value is the 
		 * time within which consecutive keypress actions must be performed
		 * by the user before the sequence is discarded.
		 */
		tmp_string = strtok (NULL, " \t\n\r\f");
		if (tmp_string == NULL) {
			/* TODO - Error messages */
			free_gesture (tmp_gesture);
			return NULL;
		}

		/*
		 * A gesture with an n_times value greater than 1 and a
		 * non-positive timeout can never be triggered, so do not
		 * accept such gestures.  The value of timeout is not used
		 * if n_times is 1, so don't bother setting the timeout in
		 * this case.
		 */
		tmp_gesture->timeout = 0;
		if (tmp_gesture->n_times > 1) {
			if ((timeout=atoi(tmp_string)) <= 0) {
				/* Add an error message */;
				free_gesture (tmp_gesture);
				return NULL;
			}
			tmp_gesture->timeout = timeout;
		}
	}

	/*
	 * Find servcice. Permit blank space so arguments can be supplied.
	 */
	keyservice = strtok (NULL, "\n\r\f");
	if (keyservice == NULL) {
		/* TODO - Error messages */
		free_gesture (tmp_gesture);
		return NULL;
	}
	/* skip over initial whitespace */
	while (*keyservice && isspace (*keyservice))
		keyservice++;
	tmp_gesture->actions = g_slist_append(tmp_gesture->actions, g_strdup(keyservice));
	return tmp_gesture;
}

/* 
 * These modifiers are ignored because they make no sense.
 * .eg <NumLock>x
 */
#define IGNORED_MODS (GDK_LOCK_MASK  | \
       GDK_MOD3_MASK | GDK_MOD5_MASK) 
#define USED_MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | \
	   GDK_MOD2_MASK | GDK_MOD4_MASK)

#define N_BITS 32

static gboolean 
gesture_already_used (Gesture *gesture)
{
	GSList *li;

	for (li = gesture_list; li != NULL; li = li->next) {
		Gesture *tmp_gesture =  (Gesture*) li->data;

		if (tmp_gesture != gesture && tmp_gesture->type == gesture->type) {
			switch (tmp_gesture->type) {
		  	case (GESTURE_TYPE_KEY):
				if (tmp_gesture->input.key.keycode == gesture->input.key.keycode &&
			    	tmp_gesture->input.key.state == gesture->input.key.state)
					return TRUE;
		  	case (GESTURE_TYPE_MOUSE):
				if (tmp_gesture->input.button.number == gesture->input.button.number)
					return TRUE;
		  	default:
		  		break;
			}
		}
	}
	return FALSE;
}

static gboolean
change_cursor_back (gpointer data)
{
	GdkCursor *cursor = gdk_cursor_new (GDK_LEFT_PTR);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);

	return FALSE;
}

GdkFilterReturn
gestures_filter (GdkXEvent *gdk_xevent,
		    GdkEvent *event,
		    gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;
	GSList *li, *act_li;
	Gesture *curr_gesture = NULL;
	
	static XEvent *last_event = NULL;
	static gint seq_count = 0;

	if (xevent->type != KeyPress &&
	    xevent->type != KeyRelease &&
	    xevent->type != ButtonPress &&
	    xevent->type != ButtonRelease)
	 	return GDK_FILTER_CONTINUE;

	if (!last_event)
		last_event = g_new0(XEvent, 1);

	switch (xevent->type) {

	case KeyPress:
		if (last_event->type == KeyPress &&
		    last_event->xkey.keycode == xevent->xkey.keycode) {
			/* they comes from auto key-repeat */
			return GDK_FILTER_CONTINUE;
		}

		if (seq_count > 0 &&
		    last_event->type != KeyRelease) {
			seq_count = 0;
			break;
		}

		if (seq_count > 0 &&
		    last_event->xkey.keycode != xevent->xkey.keycode) {
			seq_count = 0;
			break;
		}

		/*
		 * Find the associated gesture for this keycode & state
		 * TODO: write a custom g_slist_find function.
		 */
		for (li = gesture_list; li != NULL; li = li->next) {
			Gesture *gesture = (Gesture *) li->data;
			if (gesture->type == GESTURE_TYPE_KEY &&
			    xevent->xkey.keycode == gesture->input.key.keycode &&
			    (xevent->xkey.state & USED_MODS) == gesture->input.key.state) {
				/* 
				 * OK Found the gesture.
				 * Now check if it has a timeout value > 0;
				 */
				curr_gesture = gesture;
				if (gesture->timeout > 0 && seq_count > 0 &&
				    /* xevent time values are in milliseconds. The config file spec is in ms */
				    (xevent->xkey.time - last_event->xkey.time) > gesture->timeout) {
					seq_count = 0; /* The timeout has been exceeded. Reset the sequence. */
					curr_gesture = NULL;
				}
				break;
			}
		}
		break;

	case KeyRelease:
		if (seq_count > 0 &&
		    (last_event->type != KeyPress ||
		     last_event->xkey.keycode != xevent->xkey.keycode)) {
			seq_count = 0;
			break;
		}

		/*
		 * Find the associated gesture for this keycode & state
		 * TODO: write a custom g_slist_find function.
		 *
		 * Note that here we check the state against the last_event,
		 * otherwise key gestures based on modifier keys such as
		 * Control_R won't work.
		 */
		for (li = gesture_list; li != NULL; li = li->next) {
			Gesture *gesture = (Gesture *) li->data;
			if (gesture->type == GESTURE_TYPE_KEY &&
			    xevent->xkey.keycode == gesture->input.key.keycode &&
			    last_event->xkey.state == gesture->input.key.state) {
				/* 
				 * OK Found the gesture.
				 * Now check if it has a duration value > 0.
				 */
				curr_gesture = gesture;
				if ((gesture->duration > 0) &&
				    ((xevent->xkey.time - last_event->xkey.time) < gesture->duration)) {
					seq_count = 0;
					curr_gesture = NULL;
				} else {
					seq_count++;
				}
				break;
			}
		}
		break;

	case ButtonPress:
		if (seq_count > 0 && last_event->type != ButtonRelease) {
			seq_count = 0;
			break;
		}

		if (seq_count > 0 && last_event->xbutton.button != xevent->xbutton.button) {
			seq_count = 0;
			break;
		}

		/*
		 * Find the associated gesture for this button.
		 * TODO: write a custom g_slist_find function
		 */
		for (li = gesture_list; li != NULL; li = li->next) {
			Gesture *gesture = (Gesture *) li->data;
			if (gesture->type == GESTURE_TYPE_MOUSE &&
			    xevent->xbutton.button == gesture->input.button.number) { /* TODO: Support state? */
				/*
				 * Ok Found the gesture.
				 * Now check if it has a timeout value > 0;
				 */
				curr_gesture = gesture;
				if (gesture->timeout > 0 && seq_count > 0 &&
				    /* xevent time values are in milliseconds. The config file spec is in ms */
				    (xevent->xbutton.time - last_event->xbutton.time) > gesture->timeout) {
					seq_count = 0; /* Timeout has elapsed. Reset the sequence. */
					curr_gesture = NULL;
				}
				break;
			}
		}
		break;

	case ButtonRelease:
		if (seq_count > 0 &&
		    (last_event->type != ButtonPress ||
		     last_event->xbutton.button != xevent->xbutton.button)) {
			seq_count = 0;
			break;
		}

		/*
		 * Find the associated gesture for this button.
		 * TODO: write a custom g_slist_find function
		 */
		for (li = gesture_list; li != NULL; li = li->next) {
			Gesture *gesture = (Gesture *) li->data;
			if (gesture->type == GESTURE_TYPE_MOUSE &&
			    xevent->xbutton.button == gesture->input.button.number) { /* TODO: Support state? */
				/*
				 * OK Found the gesture.
				 * Now check if it has a duration value > 0.
				 */
				curr_gesture = gesture;
				if ((gesture->duration > 0) &&
				    ((xevent->xbutton.time - last_event->xbutton.time) < gesture->duration)) {
					seq_count = 0;
					curr_gesture = NULL;
				} else {
					seq_count++;
				}
				break;
			}
		}
		break;

	default:
		break;
	}

	/*
	 * Did this event complete any gesture sequences?
	 */
	last_event = memcpy(last_event, xevent, sizeof(XEvent));
	if (curr_gesture) {
		if (seq_count != curr_gesture->n_times) {
			return GDK_FILTER_CONTINUE;
		} else {
			gboolean retval;
			gchar **argv = NULL;
			gchar **envp = NULL; 

			seq_count = 0;
			for (act_li = curr_gesture->actions; act_li != NULL; act_li = act_li->next) {
				gchar *action = (gchar *)act_li->data;
				char *oldpath, *newpath;
				g_return_val_if_fail (action != NULL, GDK_FILTER_CONTINUE);
				if (!g_shell_parse_argv (action, NULL, &argv, NULL))
					continue;

				envp = get_exec_environment (xevent);

				/* add our BINDIR to the path */
				oldpath = g_strdup (g_getenv ("PATH"));
				if (ve_string_empty (oldpath))
					newpath = g_strdup (EXPANDED_BINDIR);
				else
					newpath = g_strconcat (oldpath,
							       ":",
							       EXPANDED_BINDIR,
							       NULL);
				ve_setenv ("PATH", newpath, TRUE);
				g_free (newpath);

				retval = g_spawn_async (NULL,
							argv,
							envp,
							G_SPAWN_SEARCH_PATH,
							NULL,
							NULL,
							NULL,
							NULL);

				if (ve_string_empty (oldpath))
					ve_unsetenv ("PATH");
				else
					ve_setenv ("PATH", oldpath, TRUE);
				g_free (oldpath);

				g_strfreev (argv);
				g_strfreev (envp); 

				if ( ! retval) {
					GtkWidget *dialog = 
						gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
							GTK_BUTTONS_OK,
							_("Error while trying to run (%s)\n"\
							"which is linked to (%s)"),
							action,
							curr_gesture->gesture_str);
					gtk_dialog_set_has_separator (GTK_DIALOG (dialog),
								      FALSE);
					g_signal_connect (dialog, "response",
						G_CALLBACK (gtk_widget_destroy),
						NULL);
					gtk_widget_show (dialog);
				} else {
					GdkCursor *cursor = gdk_cursor_new (GDK_WATCH);
					gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
					gdk_cursor_unref (cursor);
					g_timeout_add (2000,
						       change_cursor_back,
						       NULL);
				}
			}
   			return GDK_FILTER_CONTINUE;
		}
	}
	return GDK_FILTER_CONTINUE;
}

			
static gint
is_mouseX (const gchar *string)
{
	if ((string[0] == '<') &&
	  (string[1] == 'm' || string[1] == 'M') &&
	  (string[2] == 'o' || string[2] == 'O') &&
	  (string[3] == 'u' || string[3] == 'U') &&
	  (string[4] == 's' || string[4] == 'S') &&
	  (string[5] == 'e' || string[5] == 'E') &&
	  (isdigit(string[6]) && 
	  	(atoi(&string[6]) > 0) && 
		(atoi(&string[6]) < 6)) &&
	  (string[7] == '>'))
		return atoi(&string[6]);
	else
		return 0;
}

/* The init function for this gtk module */
G_MODULE_EXPORT void gtk_module_init(int *argc, char* argv[]);

void gtk_module_init(int *argc, char* argv[])
{
	create_event_watcher();
}

/* EOF */

