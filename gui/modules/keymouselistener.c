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

#ifdef HAVE_XINPUT
#include <X11/extensions/XInput.h>
#endif


/*
 * Note that CONFIGFILE will have to be changed to something more generic
 * if this module is ever moved outside of gdm.
 */

#undef DEBUG_GESTURES

#define CONFIGFILE EXPANDED_SYSCONFDIR "/gdm/modules/AccessKeyMouseEvents"
#define	iseol(ch)	((ch) == '\r' || (ch) == '\f' || (ch) == '\0' || \
			(ch) == '\n')

#define N_INPUT_TYPES 40

typedef enum
{
	GESTURE_TYPE_KEY	= 1 << 0,
	GESTURE_TYPE_MOUSE	= 1 << 1,
	GESTURE_TYPE_BUTTON	= 1 << 2
} GestureType;

typedef enum {
  XINPUT_TYPE_MOTION = 0,
  XINPUT_TYPE_BUTTON_PRESS   = 1,
  XINPUT_TYPE_BUTTON_RELEASE = 2,
  XINPUT_TYPE_KEY_PRESS      = 3,
  XINPUT_TYPE_KEY_RELEASE    = 4
} XInputEventType;

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
	gint  start_time;
	gint  seq_count;
} Gesture;

static int xinput_types[N_INPUT_TYPES];

static int lineno = 0;
static GSList *gesture_list = NULL;
extern char **environ;

static gchar * screen_exec_display_string (GdkScreen *screen, const char *old);
static void create_event_watcher (void);
static void load_gestures(gchar *path);
static gchar ** get_exec_environment (XEvent *xevent);
static Gesture * parse_line(gchar *buf);
static GdkFilterReturn gestures_filter (GdkXEvent *gdk_xevent, GdkEvent *event, gpointer data);
static gint is_mouseX (const gchar *string);
static gint is_switchX (const gchar *string);

#define gesture_list_get_matches(a, b, c) (g_slist_find_custom (a, b, c))

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
}

static void
init_xinput (GdkDisplay *display, GdkWindow *root)
{
#ifdef HAVE_XINPUT
	XEventClass      event_list[40];
	int              i, j, number = 0, num_devices; 
	XDeviceInfo  *devices = NULL;
	XDevice      *device = NULL;

	devices = XListInputDevices (GDK_DISPLAY_XDISPLAY (display), &num_devices);

#ifdef DEBUG_GESTURES
	g_message ("checking %d input devices...", num_devices);
#endif
	for (i = 0; i < num_devices; i++) {
		if (devices[i].use == IsXExtensionDevice)
		{
			device = XOpenDevice (GDK_DISPLAY_XDISPLAY (display), devices[i].id);
			for (j = 0; j < device->num_classes && number < 40; j++) {
				switch (device->classes[j].input_class) 
				{
				case KeyClass:
					DeviceKeyPress(device, 
						       xinput_types[XINPUT_TYPE_KEY_PRESS], 
						       event_list[number]); number++;
					DeviceKeyRelease(device, 
							 xinput_types[XINPUT_TYPE_KEY_RELEASE], 
							 event_list[number]); number++;
					break;      
				case ButtonClass:
					DeviceButtonPress(device, 
							  xinput_types[XINPUT_TYPE_BUTTON_PRESS], 
							  event_list[number]); number++;
					DeviceButtonRelease(device, 
							    xinput_types[XINPUT_TYPE_BUTTON_RELEASE], 
							    event_list[number]); number++;
					break;
				case ValuatorClass:
					DeviceMotionNotify(device, 
							   xinput_types[XINPUT_TYPE_MOTION], 
							   event_list[number]); number++;
				}
			}
		}
	}
#ifdef DEBUG_GESTURES
	g_message ("%d event types available\n", number);
#endif	
	if (XSelectExtensionEvent(GDK_WINDOW_XDISPLAY (root), 
				  GDK_WINDOW_XWINDOW (root),
				  event_list, number)) 
	{
		g_warning ("Can't select input device events!");
	}
#endif
}

static void create_event_watcher (void)
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

	init_xinput (display, 
		     gdk_screen_get_root_window (
			 gdk_display_get_default_screen (display)));

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
		g_warning (_("Cannot open gestures file: %s\n)"), path);
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
				/* We must be able to deal with multiple unambiguous gestures attached to one switch/button */
			} else
				gesture_list = g_slist_append (gesture_list, tmp_gesture);
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
	/* TODO: revisit this fallback, it's suspect since Xi events might not have xkey.root set */
	else
		screen = gdk_display_get_default_screen (gdk_display_get_default ());   

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

		tmp_gesture->start_time = 0;
		tmp_gesture->seq_count = 0;

		button = is_mouseX (tmp_gesture->gesture_str);
		if (button > 0) {
			tmp_gesture->type = GESTURE_TYPE_MOUSE;
			tmp_gesture->input.button.number = button;
		} else if ((button = is_switchX (tmp_gesture->gesture_str)) == TRUE) {
			tmp_gesture->type = GESTURE_TYPE_BUTTON;
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

#ifdef DEBUG_GESTURES
	g_message ("gesture parsed for %s button %d", 
		   (tmp_gesture->type == GESTURE_TYPE_MOUSE) ? "mouse" : ((tmp_gesture->type == GESTURE_TYPE_BUTTON) ? "switch" : "key"), 
		   tmp_gesture->input.button.number);
#endif

	return tmp_gesture;
}

/* 
 * These modifiers are ignored because they make no sense.
 * .eg <NumLock>x 
 * FIXME: [sadly, NumLock isn't always mapped to the same modifier, so the logic below is faulty - bill]
 */
#define IGNORED_MODS (GDK_LOCK_MASK | GDK_MOD2_MASK | GDK_MOD3_MASK)
#define USED_MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | \
	   GDK_MOD4_MASK | GDK_MOD5_MASK)

static gboolean
change_cursor_back (gpointer data)
{
	GdkCursor *cursor = gdk_cursor_new (GDK_LEFT_PTR);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);

	return FALSE;
}

static gint
event_time (XEvent *ev)
{
	if ((ev->type == KeyPress) ||
	    (ev->type == KeyRelease))
		return ((XKeyEvent *) ev)->time;
	else if ((ev->type == ButtonPress) ||
		 (ev->type == ButtonRelease))
		return ((XButtonEvent *) ev)->time;
	else if ((ev->type == xinput_types[XINPUT_TYPE_KEY_PRESS]) ||
		 (ev->type == xinput_types[XINPUT_TYPE_KEY_RELEASE]))
		return ((XDeviceKeyEvent *) ev)->time;
	else if ((ev->type == xinput_types[XINPUT_TYPE_BUTTON_PRESS]) ||
		 (ev->type == xinput_types[XINPUT_TYPE_BUTTON_RELEASE]))
		return ((XDeviceButtonEvent *) ev)->time;
	else
		return 0;
}

static gint
elapsed_time (XEvent *ev1, XEvent *ev2)
{
	return event_time (ev2) - event_time (ev1);
}

static gboolean
keycodes_equal (XEvent *ev1, XEvent *ev2)
{
	if (ev1->type == ev2->type)
	{
		if (ev1->type == KeyPress || ev1->type == KeyRelease)
		{
			return (((XKeyEvent *) ev1)->keycode == ((XKeyEvent *) ev2)->keycode);
		}
		else if (ev1->type == xinput_types[XINPUT_TYPE_KEY_PRESS] || ev1->type == xinput_types[XINPUT_TYPE_KEY_RELEASE])
		{
			return (((XDeviceKeyEvent *) ev1)->keycode == ((XDeviceKeyEvent *) ev2)->keycode);
		}
	}
	return FALSE;
}

static gint
key_gesture_compare_func (gconstpointer a, gconstpointer b)
{
	const Gesture *gesture = a;
	const XEvent *xev = b;

	if (gesture->type == GESTURE_TYPE_KEY) 
	{
	    if (((xev->type == KeyPress) || (xev->type == KeyRelease)) &&
		(xev->xkey.keycode == gesture->input.key.keycode) &&
		((xev->xkey.state & USED_MODS) == gesture->input.key.state))
		return 0;
	    else if (((xev->type == xinput_types[XINPUT_TYPE_KEY_PRESS]) || (xev->type == xinput_types[XINPUT_TYPE_KEY_RELEASE])) &&
		     (xev->xkey.keycode == gesture->input.key.keycode) &&
		     ((xev->xkey.state & USED_MODS) == gesture->input.key.state))
		return 0;
	    else
		return 1;
	}
	else if ((gesture->type == GESTURE_TYPE_MOUSE) &&
		 ((xev->type == ButtonPress) || (xev->type == ButtonRelease)) &&
		 (xev->xbutton.button == gesture->input.button.number))
		return 0;
	else if ((gesture->type == GESTURE_TYPE_BUTTON) &&
		 ((xev->type == xinput_types[XINPUT_TYPE_BUTTON_PRESS]) || (xev->type == xinput_types[XINPUT_TYPE_BUTTON_RELEASE])) &&
		 ((XDeviceButtonEvent *) xev)->button == gesture->input.button.number)
		return 0;
	else
		return 1;
}

#define event_is_gesture_type(xevent) (xevent->type == KeyPress ||\
	    xevent->type == KeyRelease ||\
	    xevent->type == ButtonPress ||\
	    xevent->type == ButtonRelease ||\
	    xevent->type == xinput_types[XINPUT_TYPE_KEY_PRESS] ||\
	    xevent->type == xinput_types[XINPUT_TYPE_KEY_RELEASE] ||\
	    xevent->type == xinput_types[XINPUT_TYPE_BUTTON_PRESS] ||\
	    xevent->type == xinput_types[XINPUT_TYPE_BUTTON_RELEASE])

static GdkFilterReturn
gestures_filter (GdkXEvent *gdk_xevent,
		 GdkEvent *event,
		 gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;
	GSList *li, *act_li;
	Gesture *curr_gesture = NULL;
	XID xinput_device = None;
	
	static XEvent *last_event = NULL;
	static gint seq_count = 0;

	if (!event_is_gesture_type (xevent)) 
		return GDK_FILTER_CONTINUE;

	if (!last_event)
		last_event = g_new0(XEvent, 1);

	if ((xevent->type == KeyPress) || (xevent->type == xinput_types[XINPUT_TYPE_KEY_PRESS]))
	{
#ifdef DEBUG_GESTURES
	    g_message ("key press");
#endif
		if (last_event->type == KeyPress &&
		    last_event->xkey.keycode == xevent->xkey.keycode) {
			/* these come from auto key-repeat */
#ifdef DEBUG_GESTURES
		    g_message ("rejecting repeat");
#endif
			return GDK_FILTER_CONTINUE;
		}

		if (seq_count > 0 &&
		    last_event->type != KeyRelease) {
#ifdef DEBUG_GESTURES
		    g_message ("last event wasn't a release, resetting seq");
#endif
			seq_count = 0;
		}
		else if (seq_count > 0 &&
		    keycodes_equal (last_event, xevent)) {
#ifdef DEBUG_GESTURES
		    g_message ("keycode doesn't match last event, resetting seq");
#endif
			seq_count = 0;
		}

		/*
		 * Find the associated gesture for this keycode & state
		 */
		li = gesture_list_get_matches (gesture_list, xevent, key_gesture_compare_func);
		if (li) {
			curr_gesture = li->data;
#ifdef DEBUG_GESTURES
		    g_message ("found a press match [%s]", curr_gesture->gesture_str);
#endif
			if (curr_gesture->timeout > 0 && seq_count > 0 && 
			    /* xevent time values are in milliseconds. The config file spec is in ms */
			    elapsed_time (last_event, xevent) > curr_gesture->timeout) {
#ifdef DEBUG_GESTURES
			    g_message ("timeout exceeded: reset seq and gesture");
#endif
				seq_count = 0; /* The timeout has been exceeded. Reset the sequence. */
				curr_gesture = NULL;
			}
		}
	}
	else if ((xevent->type == KeyRelease) || (xevent->type == xinput_types[XINPUT_TYPE_KEY_RELEASE]))
	{
#ifdef DEBUG_GESTURES
	    g_message ("key release");
#endif
		if (seq_count > 0 &&
		    ((last_event->type != KeyPress && last_event->type != xinput_types[XINPUT_TYPE_KEY_PRESS]) ||
		     !keycodes_equal (last_event, xevent))) {
#ifdef DEBUG_GESTURES
		    g_message ("either last event not a keypress, or keycodes don't match. Resetting seq.");
#endif
			seq_count = 0;
		}
		
		/*
		 * Find the associated gesture for this keycode & state
		 *
		 * Note that here we check the state against the last_event,
		 * otherwise key gestures based on modifier keys such as
		 * Control_R won't work.
		 */
		li = gesture_list_get_matches (gesture_list, xevent, key_gesture_compare_func);
	        if (li) {
			curr_gesture = li->data;
#ifdef DEBUG_GESTURES
		    g_message ("found a release match [%s]", curr_gesture->gesture_str);
#endif
			if ((curr_gesture->duration > 0) &&
			    (elapsed_time (last_event, xevent) < curr_gesture->duration)) {
				seq_count = 0;
				curr_gesture = NULL;
#ifdef DEBUG_GESTURES
				g_message ("setting current gesture to NULL");
#endif
			} else {
				seq_count++;
#ifdef DEBUG_GESTURES
				g_message ("incrementing seq_count");
#endif
			}	
		}
	}
	else if ((xevent->type == ButtonPress) ||  (xevent->type == xinput_types[XINPUT_TYPE_BUTTON_PRESS]))
	{
		gint button = 0;
		gint time = 0;


		if (xevent->type == ButtonPress) {
			button = xevent->xbutton.button;
			time = xevent->xbutton.time;
#ifdef DEBUG_GESTURES
			g_message ("button press: %d", button);
#endif
			if (seq_count > 0 && (last_event->type != ButtonRelease))
				seq_count = 0;
			else if (seq_count > 0 && last_event->xbutton.button != button) {
				seq_count = 0;
			}
		}
#ifdef HAVE_XINPUT
		else {
			button = ((XDeviceButtonEvent *) xevent)->button;
			time =  ((XDeviceButtonEvent *) xevent)->time;
#ifdef DEBUG_GESTURES
			g_message ("xinput button press: %d", button);
#endif
			if (seq_count > 0 && last_event->type != xinput_types[XINPUT_TYPE_BUTTON_RELEASE]) {
				seq_count = 0;
			}
			else if (seq_count > 0 && ((XDeviceButtonEvent *) last_event)->button != button) {
				seq_count = 0;
			}
		}
#endif
		
		/*
		 * Find the associated gesture for this button.
		 */
		li = gesture_list_get_matches (gesture_list, xevent, key_gesture_compare_func);
		if (li) {
#ifdef DEBUG_GESTURES
			g_message ("found match for press");
#endif
			curr_gesture = li->data;
			if (curr_gesture->timeout > 0 && seq_count > 0) {
				/* xevent time values are in milliseconds. The config file spec is in ms */
				if (curr_gesture->type == 
				    elapsed_time (last_event, xevent) > curr_gesture->timeout) {
					seq_count = 0; /* Timeout has elapsed. Reset the sequence. */
					curr_gesture = NULL;
#ifdef DEBUG_GESTURES
					g_message ("gesture timed out.");
#endif
				}
			}
		}
#ifdef DEBUG_GESTURES
		else g_message ("no match for press %d", button);
#endif
	}
	else if ((xevent->type == ButtonRelease) || (xevent->type == xinput_types[XINPUT_TYPE_BUTTON_RELEASE]))
	{
		gint button = 0;
		gint time = 0;
		
		if (xevent->type == ButtonRelease) 
		{
			button = xevent->xbutton.button;
			time = xevent->xbutton.time;
			if (seq_count > 0 &&
			    (last_event->type != ButtonPress ||
			     last_event->xbutton.button != button)) {
#ifdef DEBUG_GESTURES
				g_message ("resetting count to zero, based on failure to match last event.");
#endif
				seq_count = 0;
			}
		}
#ifdef HAVE_XINPUT
		else
		{
			button = ((XDeviceButtonEvent *) (xevent))->button;
			time = ((XDeviceButtonEvent *)(xevent))->time;
			xinput_device = ((XDeviceButtonEvent *)(xevent))->deviceid;
			if (seq_count > 0 &&
			    (last_event->type != xinput_types[XINPUT_TYPE_BUTTON_PRESS] ||
			     ((XDeviceButtonEvent *) last_event)->button != button)) {
#ifdef DEBUG_GESTURES
				g_message ("resetting count to zero, based on failure to match last input event.");
#endif
				seq_count = 0;
			}
		}
#endif

		li = gesture_list_get_matches (gesture_list, xevent, key_gesture_compare_func);
		if (li) {
#ifdef DEBUG_GESTURES
			g_message ("found match for release");
#endif
			curr_gesture = li->data;
			if ((curr_gesture->duration > 0) &&
			    (elapsed_time (last_event, xevent) < curr_gesture->duration)) {
				seq_count = 0;
				curr_gesture = NULL;
#ifdef DEBUG_GESTURES
				g_message ("insufficient duration.");
#endif
			} else {
#ifdef DEBUG_GESTURES
				g_message ("duration OK");
#endif
				seq_count++;
			}
		}
#ifdef DEBUG_GESTURES
		else g_message ("no match for release - button %d", button);
#endif
	}

	/*
	 * Did this event complete any gesture sequences?
	 */

	last_event = memcpy(last_event, xevent, sizeof(XEvent));
	if (curr_gesture) {
		if (seq_count != curr_gesture->n_times) {
#ifdef DEBUG_GESTURES
			g_message ("waiting for %d more repetitions...", curr_gesture->n_times - seq_count);
#endif
			return GDK_FILTER_CONTINUE;
		} else {
			gboolean retval;
			gchar **argv = NULL;
			gchar **envp = NULL; 

#ifdef DEBUG_GESTURES
			g_message ("gesture complete!");
#endif
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

static gint
is_switchX (const gchar *string)
{
	if ((string[0] == '<') &&
	  (string[1] == 's' || string[1] == 'S') &&
	  (string[2] == 'w' || string[2] == 'W') &&
	  (string[3] == 'i' || string[3] == 'I') &&
	  (string[4] == 't' || string[4] == 'T') &&
	  (string[5] == 'c' || string[5] == 'C') &&
	  (string[6] == 'h' || string[6] == 'H') &&
	  (isdigit(string[7]) && 
	  	(atoi(&string[7]) > 0) && 
		(atoi(&string[7]) < 6)) &&
	  (string[8] == '>'))
		return atoi(&string[7]);
	else
		return 0;
}

/* The init function for this gtk module */
G_MODULE_EXPORT void gtk_module_init(int *argc, char* argv[]);

void gtk_module_init(int *argc, char* argv[])
{
#ifdef DEBUG_GESTURES
    g_message ("keymouselistener loaded.");
#endif
    create_event_watcher();
}
