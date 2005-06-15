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

#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <math.h>

#include <glib.h>
#include <gmodule.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <vicious.h>
#include <gnome.h>

#include <X11/Xlib.h>
#include <config.h>
 
#ifdef HAVE_XINPUT
#include <X11/extensions/XInput.h>
#endif
 
/*
 * Note that CONFIGFILE will have to be changed to something more generic
 * if this module is ever moved outside of gdm.
 */

#define CONFIGFILE EXPANDED_SYSCONFDIR "/gdm/modules/AccessDwellMouseEvents"
#define iseol(ch)       ((ch) == '\r' || (ch) == '\f' || (ch) == '\0' || \
                        (ch) == '\n')

typedef enum
{
        BINDING_DWELL_BORDER_TOP        = 1 << 0,
        BINDING_DWELL_BORDER_BOTTOM     = 1 << 1,
        BINDING_DWELL_BORDER_RIGHT      = 1 << 2,
        BINDING_DWELL_BORDER_LEFT       = 1 << 3,
        BINDING_DWELL_BORDER_ERROR      = 1 << 4
} BindingType;

typedef enum
{
        BINDING_DWELL_DIRECTION_IN        = 1 << 0,
        BINDING_DWELL_DIRECTION_OUT       = 1 << 1,
        BINDING_DWELL_DIRECTION_ERROR     = 1 << 2
} BindingDirection;

typedef struct {
      int num_gestures;
      BindingType *gesture; 
      BindingDirection start_direction;
} Dwell;

typedef struct {
        Dwell input;
        char *binding_str;
        GSList *actions;
        guint timeout;
} Binding;

typedef struct {
         BindingType type;
         BindingDirection direction;
         guint32 time;
} Crossings;

static int lineno = 0;
static GSList *binding_list = NULL;
extern char **environ;
static guint enter_signal_id = 0;
static guint leave_signal_id = 0;
static int xinput_type_motion = 0;

static Crossings *crossings = NULL;
static int crossings_position = 0;
static guint max_crossings = 0;
static XID *ext_input_devices = NULL;
static gint ext_device_count = 0;
static gboolean latch_core_pointer = TRUE;
 
static void create_event_watcher (void);
static void load_bindings(gchar *path);
static gchar * screen_exec_display_string (GdkScreen *screen, const char *old);
static gchar ** get_exec_environment (GdkScreen *screen);
static Binding * parse_line(gchar *buf);
static gboolean binding_already_used (Binding *binding);
static gboolean debug_gestures = FALSE;

BindingType get_binding_type(char c);
BindingDirection get_binding_direction(char c);

static gboolean
is_ext_device (XID id)
{
	gint i;
	for (i=0; i < ext_device_count; i++)
		if (id == ext_input_devices[i])
			return TRUE;

	if (debug_gestures)
		syslog (LOG_WARNING, "is-ext-device failed for %d", id);

	return FALSE;
}

static void
init_xinput (GdkDisplay *display, GdkWindow *root)
{
#ifdef HAVE_XINPUT
	XEventClass      event_list[40];
	int              i, j, number = 0, num_devices; 
	XDeviceInfo  *devices = NULL;
	XDevice      *device = NULL;

	devices = XListInputDevices (GDK_DISPLAY_XDISPLAY (display),
		&num_devices);

	if (debug_gestures)
		syslog (LOG_WARNING, "checking %d input devices...",
			num_devices);

	for (i=0; i < num_devices; i++) {
		if (devices[i].use == IsXExtensionDevice) {
			device = XOpenDevice (GDK_DISPLAY_XDISPLAY (display),
				devices[i].id);
			for (j=0; j < device->num_classes && number < 40; j++) {
				switch (device->classes[j].input_class) 
				{
				case ValuatorClass:
					DeviceMotionNotify (device, 
							    xinput_type_motion, 
							    event_list[number]);
					number++;
				default:
					break;
				}
			}
			++ext_device_count;

			if (ext_input_devices) {
				ext_input_devices = g_realloc (ext_input_devices,
					sizeof (XID *) * ext_device_count);
			} else {
				ext_input_devices = g_malloc (sizeof (XID *));
			}
			ext_input_devices[ext_device_count - 1] = devices[i].id;
		}
	}

	if (debug_gestures)
		syslog (LOG_WARNING, "%d event types available", number);

	if (XSelectExtensionEvent (GDK_WINDOW_XDISPLAY (root), 
				   GDK_WINDOW_XWINDOW (root),
				   event_list, number)) {
		if (debug_gestures)
			syslog (LOG_WARNING,
				"Can't select input device events!");
	}
#endif
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
get_exec_environment (GdkScreen *screen)
{
	gchar **retval = NULL;
	gint    i;
	gint    display_index = -1;

	g_assert (GDK_IS_SCREEN (screen));

	for (i=0; environ [i]; i++)
		if (strncmp (environ [i], "DISPLAY", 7) == 0)
			display_index = i;

	if (display_index == -1)
		display_index = i++;

	retval = g_new0 (char *, i + 1);

	for (i=0; environ [i]; i++)
		if (i == display_index)
			retval [i] = screen_exec_display_string (screen,
				environ[i]);
		else
			retval [i] = g_strdup (environ [i]);

	retval [i] = NULL;

	return retval;
}

BindingType
get_binding_type(char c)
{
	BindingType rc;

	if (c == toupper ('T'))
		rc = BINDING_DWELL_BORDER_TOP;
	else if (c == toupper ('B'))
		rc = BINDING_DWELL_BORDER_BOTTOM;
	else if (c == toupper ('R'))
		rc = BINDING_DWELL_BORDER_RIGHT;
	else if (c == toupper ('L'))
		rc = BINDING_DWELL_BORDER_LEFT;
	else
		rc = BINDING_DWELL_BORDER_ERROR;

	return rc;
}

BindingDirection
get_binding_direction(char c)
{
	BindingDirection rc;

	if (c == toupper ('I'))
		rc = BINDING_DWELL_DIRECTION_IN;
	else if (c == toupper ('O'))
		rc = BINDING_DWELL_DIRECTION_OUT;
	else
		rc = BINDING_DWELL_DIRECTION_ERROR;

	return rc;
}

static void
free_binding (Binding *binding)
{
	if (binding == NULL)
		return;

	g_slist_foreach (binding->actions, (GFunc)g_free, NULL);
	g_slist_free (binding->actions);
	g_free (binding->binding_str);
	g_free (binding->input.gesture);
	g_free (binding);
}

static Binding *
parse_line(gchar *buf)
{
	gchar *keystring, *keyservice;
	Binding *tmp_binding = NULL;
	static GdkDisplay *display = NULL;

	lineno++;
  
	if (!display) {
		if ((display = gdk_display_get_default ()) == NULL)
			return NULL;
	}

	if ((*buf == '#') || (iseol (*buf)) || (buf == NULL))
		return NULL;

	/* Find the binding name */
	keystring = strtok (buf, " \t\n\r\f");
	if (keystring == NULL) {
		/* TODO - Add an error message */
		return NULL;
	}

	tmp_binding = g_new0 (Binding, 1);
	tmp_binding->binding_str = g_strdup (keystring);

	if (strcmp (tmp_binding->binding_str, "<Add>") != 0) {
		BindingType bt;
		BindingDirection bd;
		guint timeout;
		gchar *tmp_string;
		int i, j;

		tmp_binding->input.gesture = g_new0 (BindingType,
			strlen (tmp_binding->binding_str));

		j=0;
		for (i=0; i < strlen(tmp_binding->binding_str); i++) {
			bt = get_binding_type (tmp_binding->binding_str[i]);

			if (bt == BINDING_DWELL_BORDER_ERROR) {
				if (debug_gestures)
					syslog (LOG_WARNING,
						"Invalid value in binding %s\n",
						tmp_binding->binding_str);

				continue;
			}

			tmp_binding->input.gesture[j++] = bt;
		}
		tmp_binding->input.num_gestures = j;

		if (j > max_crossings)
			max_crossings = j;

		/* [TODO] Need to clean up here. */
		tmp_string = strtok (NULL, " \t\n\r\f");
		if (tmp_string == NULL) {
			/* TODO - Add an error message */
			free_binding (tmp_binding);
			return NULL;
		}

		bd = get_binding_direction (tmp_string[0]);

		if (bd == BINDING_DWELL_DIRECTION_ERROR)
			if (debug_gestures) {
				syslog (LOG_WARNING, "Invalid value in binding %s",
					tmp_binding->binding_str);
			} else {
				tmp_binding->input.start_direction = bd;
			}

		/*
		 * Find the timeout duration (in ms). Timeout value is the 
		 * time within which consecutive keypress actions must be
		 * performed by the user before the sequence is discarded.
		 */

		tmp_string = strtok (NULL, " \t\n\r\f");
		if (tmp_string == NULL) {
			/* TODO - Add an error message */
			free_binding (tmp_binding);
			return NULL;
		}

		timeout = atoi (tmp_string);
		if (timeout <= 0) {
			/* TODO - Add an error message */;
			free_binding (tmp_binding);
			return NULL;
		}
		tmp_binding->timeout = timeout;
	}

	/* Find servcice. Permit blank space so arguments can be supplied. */
	keyservice = strtok (NULL, "\n\r\f");
	if (keyservice == NULL) {
		/* TODO - Add an error message */
		free_binding (tmp_binding);
		return NULL;
	 }

	/* skip over initial whitespace */
	while (*keyservice && isspace (*keyservice))
		keyservice++;

	tmp_binding->actions = g_slist_append (tmp_binding->actions, g_strdup (keyservice));

	return tmp_binding;
}

static gboolean
binding_already_used (Binding *binding)
{
	GSList *li;

	for (li=binding_list; li != NULL; li = li->next) {
		Binding *tmp_binding = (Binding*) li->data;

		if (tmp_binding != binding &&
		    tmp_binding->input.start_direction == binding->input.start_direction)
		{
			int i;

			for (i=0; i < tmp_binding->input.num_gestures; i++) {
				if (tmp_binding->input.gesture != binding->input.gesture)
					break;
			}

			if (i == tmp_binding->input.num_gestures)
				return TRUE;
		}
	}
	return FALSE;
}

static void
load_bindings(gchar *path)
{
	FILE *fp;
	Binding *tmp_binding;
	gchar buf[1024];

	fp = fopen (path, "r");
	if (fp == NULL) {
		/* TODO - I18n */
		if (debug_gestures)
			syslog (LOG_WARNING, "Cannot open bindings file: %s", path);
		return;
	}

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		tmp_binding = (Binding *)parse_line (buf);

		if (tmp_binding) {
			/* Is the key already associated with an existing binding? */
			if (strcmp (tmp_binding->binding_str, "<Add>") == 0) {
				/* Add another action to the last binding */
				Binding *last_binding;
				GSList *last_item = g_slist_last (binding_list);

				/* If there is no last_item to add onto ignore the entry */
				if (last_item) {
					last_binding = (Binding *)last_item->data;

					/* Add the action to the last binding's actions list */
					last_binding->actions = g_slist_append (last_binding->actions,
						g_strdup ((gchar *)tmp_binding->actions->data));
				}
				free_binding (tmp_binding);
				/* Ignore duplicate bindings */
			} else if (!binding_already_used (tmp_binding))
				binding_list = g_slist_append (binding_list, tmp_binding);
			else
				free_binding (tmp_binding);
		}
	}
	fclose (fp);
}

static gboolean
change_cursor_back (gpointer data)
{
	GdkCursor *cursor = gdk_cursor_new (GDK_LEFT_PTR);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);

	return FALSE;
}


static gboolean
leave_enter_emission_hook (GSignalInvocationHint        *ihint,
                           guint                        n_param_values,
                           const GValue                 *param_values,
                           gpointer                     data)
{
	GObject *object;
	GdkEventCrossing *event;
	int i;

	object = g_value_get_object (param_values + 0);
	event  = g_value_get_boxed (param_values + 1);

	if (event->detail != GDK_NOTIFY_INFERIOR && 
	    GTK_IS_WINDOW (object) && GTK_WIDGET_TOPLEVEL (object)) {

		GtkWidget *widget = GTK_WIDGET (object);
		GtkWindow *window = GTK_WINDOW (object);
		GdkRectangle rect;
		GSList *li, *act_li;
		double mid_x, mid_y;

		gdk_window_get_frame_extents (widget->window, &rect);

		mid_x = rect.x + (rect.width  / 2);
		mid_y = rect.y + (rect.height / 2);

		/* avoid division by 0 */
		if (fabs (event->x_root - mid_x) <= 0.001) {
			if (event->x_root < mid_x)
				crossings[crossings_position].type = BINDING_DWELL_BORDER_LEFT;
			else
				crossings[crossings_position].type = BINDING_DWELL_BORDER_RIGHT;
		} else {
			double slope = (event->y_root - mid_y) / (event->x_root - mid_x);

			if (event->y_root < mid_y) {
				if (slope > 1 || slope < -1)
					crossings[crossings_position].type = BINDING_DWELL_BORDER_TOP;
				else if (slope >= 0)
					crossings[crossings_position].type = BINDING_DWELL_BORDER_LEFT;
				else
					crossings[crossings_position].type = BINDING_DWELL_BORDER_RIGHT;
			} else {
				if (slope > 1 || slope < -1)
					crossings[crossings_position].type = BINDING_DWELL_BORDER_BOTTOM;
				else if (slope >= 0)
					crossings[crossings_position].type = BINDING_DWELL_BORDER_RIGHT;
				else
					crossings[crossings_position].type = BINDING_DWELL_BORDER_LEFT;
			}
		}

		if (ihint->signal_id == enter_signal_id)
			crossings[crossings_position].direction = BINDING_DWELL_DIRECTION_IN;
		else if (ihint->signal_id == leave_signal_id)
			crossings[crossings_position].direction = BINDING_DWELL_DIRECTION_OUT;

		crossings[crossings_position].time = event->time;

		/* Check to see if a gesture has been completed */
		for (li=binding_list; li != NULL; li = li->next) {
			Binding *curr_binding = (Binding *) li->data;
			int start_position = (crossings_position - curr_binding->input.num_gestures + 1 +
				max_crossings) % max_crossings;

			/* being anal here */
			if (start_position < 0)
				start_position = 0;

			/* check initial crossing direction */
			if (curr_binding->input.start_direction == crossings[start_position].direction) {
				/* check borders */
				for (i=0; i < curr_binding->input.num_gestures; i++) {
					if (curr_binding->input.gesture[i] !=
					    crossings[(start_position + i) % max_crossings].type)
						break; 
				}

				/* check timeout values */
				if (i == curr_binding->input.num_gestures) {
					for (i=1; i < curr_binding->input.num_gestures; i++) {
						int cur_pos  = (start_position + i) % max_crossings;
						int prev_pos = (start_position + i - 1) % max_crossings; 
						guint32 diff_time = crossings[cur_pos].time -
							crossings[prev_pos].time;
	    
						if (curr_binding->timeout != 0 &&
						    curr_binding->timeout < diff_time)
							break; 
					}
				}

				/* If this is true, then gesture was recognized */
				if (i == curr_binding->input.num_gestures) {
					gboolean retval;
					gchar **argv = NULL;
					gchar **envp = NULL;

					for (act_li=curr_binding->actions; act_li != NULL; act_li = act_li->next) {
						gchar *action = (gchar *)act_li->data;
						g_return_val_if_fail (action != NULL, TRUE);

						if (!g_shell_parse_argv (action, NULL, &argv, NULL))
							continue;

						envp = get_exec_environment (gtk_window_get_screen(window));

						retval = g_spawn_async (NULL, argv, envp,
							G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

						g_strfreev (argv);
						g_strfreev (envp);

						if ( ! retval) {
							GtkWidget *dialog =
								gtk_message_dialog_new (NULL,
									0, GTK_MESSAGE_ERROR,
									GTK_BUTTONS_OK,
									_("Error while trying to run (%s)\n"\
									"which is linked to (%s)"),
									action, curr_binding->binding_str);
							gtk_dialog_set_has_separator (GTK_DIALOG (dialog),
								FALSE);
							g_signal_connect (dialog, "response",
								G_CALLBACK (gtk_widget_destroy), NULL);
							gtk_widget_show (dialog);
						} else {
							GdkCursor *cursor = gdk_cursor_new (GDK_WATCH);
							gdk_window_set_cursor (gdk_get_default_root_window (),
								cursor);
							gdk_cursor_unref (cursor);
							g_timeout_add (2000, change_cursor_back, NULL);
							latch_core_pointer = FALSE;
							/* once we've recognized a gesture, we need to
							   leave the pointer alone */
						}
					}
				}
			}
		}

		crossings_position = (crossings_position + 1) % max_crossings;
	}

	return TRUE;
}

static GdkFilterReturn
gestures_filter (GdkXEvent *gdk_xevent,
		 GdkEvent *event,
		 gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;

	if (xevent->type == xinput_type_motion) {
		XDeviceMotionEvent *motion = (XDeviceMotionEvent *) xevent;
		if ((motion->axes_count < 2) || !is_ext_device (motion->deviceid))
			return GDK_FILTER_CONTINUE;
		if (latch_core_pointer)
		    XWarpPointer (motion->display, None, 
			      motion->root,
			      0, 0, 0, 0, motion->axis_data[0], motion->axis_data[1]);
	}
       	return GDK_FILTER_CONTINUE;
}

static void
create_event_watcher (void)
{
	GdkDisplay *display;
	gint i;

	display = gdk_display_get_default ();
	if (!display)
		return;

	load_bindings(CONFIGFILE);

	crossings = g_new0(Crossings, max_crossings);

	for (i=0; i < max_crossings; i++) {
		crossings[i].type      = BINDING_DWELL_BORDER_ERROR;
		crossings[i].direction = BINDING_DWELL_DIRECTION_ERROR;
		crossings[i].time      = 0;
	}

	init_xinput (display, 
		gdk_screen_get_root_window (
		gdk_display_get_default_screen (display)));

	gdk_window_add_filter (NULL, gestures_filter, NULL);

	/* set up emission hook */
	gtk_type_class (GTK_TYPE_WIDGET);
	enter_signal_id = g_signal_lookup ("enter-notify-event", GTK_TYPE_WIDGET);
	leave_signal_id = g_signal_lookup ("leave-notify-event", GTK_TYPE_WIDGET);

	g_signal_add_emission_hook (enter_signal_id, 0, 
		leave_enter_emission_hook, NULL, (GDestroyNotify) NULL); 
	g_signal_add_emission_hook (leave_signal_id, 0,
		leave_enter_emission_hook, NULL, (GDestroyNotify) NULL); 
}

/* The init function for this gtk module */
G_MODULE_EXPORT void gtk_module_init (int *argc, char* argv[]);

void gtk_module_init (int *argc, char* argv[])
{
	if (g_getenv ("GDM_DEBUG_GESTURES") != NULL);
		debug_gestures = TRUE;

	if (debug_gestures)
		syslog (LOG_WARNING, "dwellmouselistener loaded.");

	create_event_watcher ();
}

/* EOF */
