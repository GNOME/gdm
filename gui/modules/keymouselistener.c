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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <glib.h>
#include <gmodule.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
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
	BINDING_TYPE_KEY	= 1 << 0,
	BINDING_TYPE_MOUSE	= 1 << 1
} BindingType;

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
	BindingType type;
	union Input input;
	char *binding_str;
	GSList *actions;
	guint n_times;
	guint duration;
	guint timeout;
} Binding;

static int lineno = 0;
static GSList *binding_list = NULL;
extern char **environ;

static gchar * screen_exec_display_string (GdkScreen *screen);
static void create_event_watcher ();
static void load_bindings(gchar *path);
static gchar ** get_exec_environment (XEvent *xevent);
static Binding * parse_line(gchar *buf);
static gboolean binding_already_used (Binding *binding);
static GdkFilterReturn	
bindings_filter (GdkXEvent *gdk_xevent, GdkEvent *event, gpointer data);
static gint is_mouseX (const gchar *string);
			
static gchar *
screen_exec_display_string (GdkScreen *screen)
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
	return g_strdup ("DISPLAY=:0.0");
#endif
}

static void create_event_watcher ()
{
	GSList *li;
	GdkDisplay *display;

	display = gdk_display_get_default();
	if (!display) {
		return;
	}

	/*
	 * Switch off keyboard autorepeat
	 */
	XAutoRepeatOff(GDK_DISPLAY_XDISPLAY(display));
	load_bindings(CONFIGFILE);
	gdk_window_add_filter (NULL, bindings_filter, NULL);

	return;
}


static void
load_bindings(gchar *path)
{
	FILE *fp;
	Binding *tmp_binding;
	gchar buf[1024];

	if ((fp = fopen(path, "r")) == NULL) {
		/* TODO - I18n */
		printf("Cannot open bindings file: %s\n", path);
		return;
	}

	while (((fgets(buf, 1024, fp)) != NULL) && ((feof(fp)) == 0)) {
		tmp_binding = (Binding *)parse_line(buf);
		if (tmp_binding) {
		/*
		 * Is the key already associated with an existing binding?
		 */
			if (!strcmp(tmp_binding->binding_str, "<Add>")) {
				/*
				 * Add another action to the last binding
				 */
				Binding *last_binding;
				GSList *last_item = g_slist_last(binding_list);
				/*
				 * If there is no last_item to add onto ignore the entry
				 */
				if (last_item) {
					last_binding = (Binding *)last_item->data;
					/* Add the action to the last binding's actions list */
					last_binding->actions = 
						g_slist_append(last_binding->actions,
						g_strdup((gchar *)tmp_binding->actions->data));
				}
				/* Ignore duplicate bindings */
			} else if (!binding_already_used (tmp_binding))
				binding_list = g_slist_append(binding_list, tmp_binding);
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

	retval = g_new (char *, i + 1);

	for (i = 0; environ [i]; i++)
 		if (i == display_index)
  			retval [i] = screen_exec_display_string (screen);
	else
		retval [i] = g_strdup (environ [i]);

	retval [i] = NULL;

	return retval;
}

static Binding *
parse_line(gchar *buf)
{
	gchar *c;
	gchar *keystring;
	gchar *keyservice;
	gint button = 0;
	Binding *tmp_binding = NULL;
	static GdkDisplay *display = NULL;
	
	if(!display) {
		if ((display = gdk_display_get_default()) == NULL)
			return NULL;
	}
	lineno++;

	if ((*buf == '#') || (iseol(*buf)) || (buf == NULL))
		return NULL;
	
	tmp_binding = g_new0 (Binding, 1);
	keystring = c = buf;

	/*
	 * Find the key name
	 */
	while(!(isspace(*c))) {
		if(iseol(*c)) {
		/* TODO - Error messages */
			return NULL;
		}
		c++;
	}

	*c++ = '\0';
	tmp_binding->binding_str = (gchar *)g_malloc(strlen(keystring) + 1);
	strncpy(tmp_binding->binding_str, keystring, strlen(keystring)+1);

	if(strcmp(tmp_binding->binding_str, "<Add>")) {
		guint n, duration, timeout;
		gchar *tmp_string;

		if ((button = is_mouseX(tmp_binding->binding_str)) > 0) {
			tmp_binding->type = BINDING_TYPE_MOUSE;
			tmp_binding->input.button.number = button;
		} else {
			tmp_binding->type = BINDING_TYPE_KEY;
			gtk_accelerator_parse(tmp_binding->binding_str, 
	 			&(tmp_binding->input.key.keysym), 
	 			&(tmp_binding->input.key.state));
			tmp_binding->input.key.keycode = 
				XKeysymToKeycode(GDK_DISPLAY_XDISPLAY (display), tmp_binding->input.key.keysym);
		}
		/* [TODO] Need to clean up here. */
		 
		/*
	 	 * Skip over white space
	 	 */
		do {
			if (iseol(*c)) {
				/* Add an error message */
				return NULL;
			}
		} while (isspace(*c) && (c++));

		/*
		 * Find the repetition number
		 */
		tmp_string = c;
		while(!(isspace(*c))) {
			if(!isdigit(*c)) {
				/* Add an error message */
				return NULL;
			}
			c++;
		}

		*c++ = '\0';
		if ((n=atoi(tmp_string)) <= 0) {
				/* Add an error message */
			return NULL;
		}
		tmp_binding->n_times = n;

		/*
	 	 * Skip over white space
	 	 */
	 	 do {
			if (iseol(*c)) {
				/* Add an error message */
				return NULL;
			}
		} while (isspace(*c) && (c++));

		/*
		 * Find the key press duration (in seconds)
		 */
		tmp_string = c;
		while(!(isspace(*c))) {
			if(!isdigit(*c)) {
				/* Add an error message */
				return NULL;
			}
			c++;
		}

		*c++ = '\0';
		if ((duration=atoi(tmp_string)) < 0) {
				/* Add an error message */
			return NULL;
		}
		tmp_binding->duration = duration;

		/*
	 	 * Skip over white space
	 	 */
	 	 do {
			if (iseol(*c)) {
				/* Add an error message */
				return NULL;
			}
		} while (isspace(*c) && (c++));

		/*
		 * Find the timeout duration (in seconds). Timeout value is the 
		 * time within which consecutive keypress actions must be performed
		 * by the user before the sequence is discarded.
		 */

		tmp_string = c;
		while(!(isspace(*c))) {
			if(!isdigit(*c)) {
				/* Add an error message */
				return NULL;
			}
			c++;
		}

		*c++ = '\0';
		if ((timeout=atoi(tmp_string)) <= 0) {
				/* Add an error message */;
			return NULL;
		}
		tmp_binding->timeout = timeout;
	}

	/*
	 * Find servcice. Permit blank space so arguments can be supplied.
	 */
	do {
		if (iseol(*c)) {
				/* Add an error message */
			return NULL;
		}
	} while (isspace(*c) && (c++));

	keyservice = c;
	for (; !iseol(*c); c++);
	*c = '\0';
	tmp_binding->actions = g_slist_append(tmp_binding->actions, g_strdup(keyservice));
	return tmp_binding;
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
binding_already_used (Binding *binding)
{
	GSList *li;

	for (li = binding_list; li != NULL; li = li->next) {
		Binding *tmp_binding =  (Binding*) li->data;

		if (tmp_binding != binding && tmp_binding->type == binding->type) {
			switch (tmp_binding->type) {
		  	case (BINDING_TYPE_KEY):
				if (tmp_binding->input.key.keycode == binding->input.key.keycode &&
			    	tmp_binding->input.key.state == binding->input.key.state)
					return TRUE;
		  	case (BINDING_TYPE_MOUSE):
				if (tmp_binding->input.button.number == binding->input.button.number)
					return TRUE;
		  	default:
		  		break;
			}
		}
    }
	return FALSE;
}

GdkFilterReturn
bindings_filter (GdkXEvent *gdk_xevent,
		    GdkEvent *event,
		    gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;
	GSList *li, *act_li;
	KeySym sym;
	
	static XEvent *last_event = NULL;
	static Binding *curr_binding = NULL;
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
		if (last_event->type != KeyRelease) {
			seq_count = 0;
		} else {
			if (last_event->xkey.keycode != xevent->xkey.keycode ||
				last_event->xkey.state != xevent->xkey.state
				) {
				seq_count = 0;
			} else {
				/*
				 * Find the associated binding for this keycode & state
				 * TODO: write a custom g_slist_find function.
				 */
				 for (li = binding_list; li != NULL; li = li->next) {
				 	Binding *binding = (Binding *) li->data;
					if (binding->type == BINDING_TYPE_KEY &&
						xevent->xkey.keycode == binding->input.key.keycode &&
						(xevent->xkey.state & USED_MODS) == binding->input.key.state) {
						/* 
						 * OK Found the binding.
						 * Now check if it has a timeout value > 0;
						 */
						curr_binding = binding;
						if (binding->timeout > 0) {
							 /* xevent time values are in milliseconds. The config file spec is in seconds */
							if ((xevent->xkey.time - last_event->xkey.time) > binding->timeout)	
								seq_count = 0; /* The timeout has been exceeded. Reset the sequence. */
						}
					}
				}
			}
		}
		break;

	case KeyRelease:
		if ((last_event->type != KeyPress) ||
			last_event->xkey.keycode != xevent->xkey.keycode ||
			last_event->xkey.state != xevent->xkey.state
			)
			seq_count = 0;
		else {
			/*
			 * Find the associated binding for this keycode &state
			 * TODO: write a custom g_slist_find function.
			 */
			for (li = binding_list; li != NULL; li = li->next) {
				Binding *binding = (Binding *) li->data;
				if (binding->type == BINDING_TYPE_KEY &&
					xevent->xkey.keycode == binding->input.key.keycode &&
					(xevent->xkey.state & USED_MODS) == binding->input.key.state) {
					/* 
					 * OK Found the binding.
					 * Now check if it has a duration value > 0.
					 */
					curr_binding = binding;
					if ((binding->duration > 0) &&
						((xevent->xkey.time - last_event->xkey.time) < binding->duration))
						seq_count = 0;
					else
						seq_count++;
				}
			}
		}
		break;

	case ButtonPress:
		if(last_event->type != ButtonRelease)
			seq_count = 0;
		else {
			if (last_event->xbutton.button != xevent->xbutton.button)
				seq_count = 0;
			else {
				/*
				 * Find the associated binding for this button.
				 * TODO: write a custom g_slist_find function
				 */
				for (li = binding_list; li != NULL; li = li->next) {
					Binding *binding = (Binding *) li->data;
					if (binding->type == BINDING_TYPE_MOUSE &&
						xevent->xbutton.button == binding->input.button.number) { /* TODO: Support state? */
						/*
						 * Ok Found the binding.
						 * Now check if it has a timeout value > 0;
						 */
						curr_binding = binding;
						if (binding->timeout > 0 ) {
							/* xevent time values are in milliseconds. The config file spec is in seconds */
							if ((xevent->xbutton.time - last_event->xbutton.time) > binding->timeout)
								seq_count = 0; /* Timeout has elapsed. Reset the sequence. */
						}
					}
				}
			}
		}
		break;

	case ButtonRelease:
		if (last_event->type != ButtonPress ||
			last_event->xbutton.button != xevent->xbutton.button)
			seq_count = 0;
		else {
			/*
			 * Find the associated binding for this button.
			 * TODO: write a custom g_slist_find function
			 */
			for (li = binding_list; li != NULL; li = li->next) {
				Binding *binding = (Binding *) li->data;
				if (binding->type == BINDING_TYPE_MOUSE &&
					xevent->xbutton.button == binding->input.button.number) { /* TODO: Support state? */
					/*
					 * OK Found the binding.
					 * Now check if it has a duration value > 0.
					 */
					curr_binding = binding;
					if ((binding->duration > 0) &&
						((xevent->xbutton.time - last_event->xbutton.time) < binding->duration))
						seq_count = 0;
					else
						seq_count++;
				}
			}
		}
		break;

	default:
		break;
	}

	/*
	 * Did this event complete any binding sequences?
	 */
	last_event = memcpy(last_event, xevent, sizeof(XEvent));
	if (curr_binding) {
		if (seq_count != curr_binding->n_times) 
			return GDK_FILTER_REMOVE;
		else {
			GError* error = NULL;
			gboolean retval;
			gchar **argv = NULL;
			gchar **envp = NULL; 

			seq_count = 0;
			for (act_li = curr_binding->actions; act_li != NULL; act_li = act_li->next) {
				gchar *action = (gchar *)act_li->data;
				g_return_val_if_fail (action != NULL, GDK_FILTER_CONTINUE);
				if (!g_shell_parse_argv (action, NULL, &argv, &error))
					return GDK_FILTER_CONTINUE;

				envp = get_exec_environment (xevent);

				retval = g_spawn_async (NULL,
					argv,
					envp,
					G_SPAWN_SEARCH_PATH,
					NULL,
					NULL,
					NULL,
					&error);
				g_strfreev (argv);
				g_strfreev (envp); 

				if (!retval) {
					GtkWidget *dialog = 
						gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_WARNING,
							GTK_BUTTONS_CLOSE,
							_("Error while trying to run (%s)\n"\
							"which is linked to (%s)"),
							action,
							curr_binding->binding_str);
					g_signal_connect (dialog, "response",
						G_CALLBACK (gtk_widget_destroy),
						NULL);
					gtk_widget_show (dialog);
				}
			}
   			return GDK_FILTER_REMOVE;
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

