/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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

#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/utsname.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "gdmcomm.h"
#include "gdmconfig.h"

gint gdm_timed_delay = 0;

/*
 * Some slaves want to send output to syslog and others (such as 
 * gdmflexiserver and gdmdynamic send error messages to stdout.  
 * Calling gdm_common_openlog to open the syslog sets the
 * using_syslog flag so that calls to gdm_common_fail, 
 * gdm_common_info, gdm_common_error, and gdm_common_debug sends 
 * output to the syslog if the syslog has been opened, otherwise
 * send to stdout.
 */
static gboolean using_syslog = FALSE;

void
gdm_common_openlog (const char *ident, int logopt, int facility)
{
   openlog (ident, logopt, facility);
   using_syslog = TRUE;
}

void
gdm_common_fail_exit (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	_exit (EXIT_FAILURE);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    if (using_syslog) {
        syslog (LOG_ERR, "%s", s);
        closelog ();
    } else
        g_printf ("%s\n", s);

    g_free (s);

    _exit (EXIT_FAILURE);
}

void
gdm_common_fail_greeter (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	_exit (DISPLAY_GREETERFAILED);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    if (using_syslog) {
        syslog (LOG_ERR, "%s", s);
        closelog ();
    } else
        g_printf ("%s\n", s);

    g_free (s);

    _exit (DISPLAY_GREETERFAILED);
}

void
gdm_common_info (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    if (using_syslog)
        syslog (LOG_INFO, "%s", s);
    else
        g_printf ("%s\n", s);
 
    g_free (s);
}

void
gdm_common_error (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    if (using_syslog)
        syslog (LOG_ERR, "%s", s);
    else
        g_printf ("%s\n", s);
    
    g_free (s);
}

void
gdm_common_warning (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    if (using_syslog)
        syslog (LOG_WARNING, "%s", s);
    else
        g_printf ("%s\n", s);
    
    g_free (s);
}

void
gdm_common_debug (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if G_LIKELY (! gdm_config_get_bool (GDM_KEY_DEBUG))
        return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    syslog (LOG_ERR, "%s", s);
    closelog ();
    g_free (s);
}

void
gdm_common_setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (gdk_get_default_root_window (), cursor);
	gdk_cursor_unref (cursor);
}

void
gdm_common_login_sound (const gchar *GdmSoundProgram,
			const gchar *GdmSoundOnLoginReadyFile,
			gboolean     GdmSoundOnLoginReady)
{
	if ( ! GdmSoundOnLoginReady)
		return;

	if (ve_string_empty (g_getenv ("GDM_IS_LOCAL")) ||
	    ve_string_empty (GdmSoundProgram) ||
	    ve_string_empty (GdmSoundOnLoginReadyFile) ||
	    g_access (GdmSoundProgram, F_OK) != 0 ||
	    g_access (GdmSoundOnLoginReadyFile, F_OK) != 0) {
		gdk_beep ();
	} else {
		/* login sound interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_LOGIN_SOUND);
		fflush (stdout);
	}
}

typedef struct {
	GtkWidget *entry;
	gboolean   blink;
} EntryBlink;

static GSList *entries = NULL;
static guint noblink_timeout = 0;

#define NOBLINK_TIMEOUT (20*1000)

static void
setup_blink (gboolean blink)
{
	GSList *li;
	for (li = entries; li != NULL; li = li->next) {
		EntryBlink *eb = li->data;
		if (eb->blink) {
			GtkSettings *settings
				= gtk_widget_get_settings (eb->entry);
			g_object_set (settings,
				      "gtk-cursor-blink", blink, NULL);
			gtk_widget_queue_resize (eb->entry);
		}
	}
}

static gboolean
no_blink (gpointer data)
{
	noblink_timeout = 0;
	setup_blink (FALSE);
	return FALSE;
}

static gboolean
delay_noblink (GSignalInvocationHint *ihint,
	       guint	           n_param_values,
	       const GValue	  *param_values,
	       gpointer		   data)
{
	setup_blink (TRUE);
	if (noblink_timeout > 0)
		g_source_remove (noblink_timeout);
	noblink_timeout
		= g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
	return TRUE;
}      


void
gdm_common_setup_blinking (void)
{
	guint sid;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	sid = g_signal_lookup ("activate",
			       GTK_TYPE_MENU_ITEM);
	if (sid != NULL) {
	   g_signal_add_emission_hook (sid,
				       0 /* detail */,
				       delay_noblink,
				       NULL /* data */,
				       NULL /* destroy_notify */);
	}

	sid = g_signal_lookup ("key_press_event",
			       GTK_TYPE_WIDGET);
	if (sid != NULL) {
	   g_signal_add_emission_hook (sid,
				       0 /* detail */,
				       delay_noblink,
				       NULL /* data */,
				       NULL /* destroy_notify */);
	}

	sid = g_signal_lookup ("button_press_event",
			       GTK_TYPE_WIDGET);
	if (sid != NULL) {
	   g_signal_add_emission_hook (sid,
				       0 /* detail */,
				       delay_noblink,
				       NULL /* data */,
				       NULL /* destroy_notify */);
	}

	noblink_timeout = g_timeout_add (NOBLINK_TIMEOUT, no_blink, NULL);
}

void
gdm_common_setup_blinking_entry (GtkWidget *entry)
{
	EntryBlink *eb;
	GtkSettings *settings;

	if ( ! ve_string_empty (g_getenv ("GDM_IS_LOCAL")) &&
	    strncmp (ve_sure_string (g_getenv ("DISPLAY")), ":0", 2) == 0)
		return;

	eb = g_new0 (EntryBlink, 1);

	eb->entry = entry;
	settings = gtk_widget_get_settings (eb->entry);
	g_object_get (settings, "gtk-cursor-blink", &(eb->blink), NULL);

	entries = g_slist_prepend (entries, eb);
}

GdkPixbuf *
gdm_common_get_face (const char *filename,
		     const char *fallback_filename,
		     guint       max_width,
		     guint       max_height)
{
	GdkPixbuf *pixbuf    = NULL;

	/* If we don't have a filename then try the fallback */
	if (! filename) {
		GtkIconTheme *theme;
		int           icon_size = 48;

		/* If we don't have a fallback then return NULL */
		if (! fallback_filename)
			return NULL;

		/* Try to load an icon from the theme before the fallback */
		theme = gtk_icon_theme_get_default ();
		pixbuf = gtk_icon_theme_load_icon (theme, "stock_person", icon_size, 0, NULL);
		if (! pixbuf)
			pixbuf = gdk_pixbuf_new_from_file (fallback_filename, NULL);
	} else {
		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	}

	if (pixbuf) {
		guint w, h;
	
		w = gdk_pixbuf_get_width (pixbuf);
		h = gdk_pixbuf_get_height (pixbuf);

		if (w > h && w > max_width) {
			h = h * ((gfloat) max_width / w);
			w = max_width;
		} else if (h > max_height) {
			w = w * ((gfloat) max_height / h);
			h = max_height;
		}

		if (w != gdk_pixbuf_get_width (pixbuf) ||
		    h != gdk_pixbuf_get_height (pixbuf)) {
			GdkPixbuf *img;

			img = gdk_pixbuf_scale_simple (pixbuf, w, h, GDK_INTERP_BILINEAR);
			g_object_unref (pixbuf);
			pixbuf = img;
		}
        }

	return pixbuf;
}

gchar * 
gdm_common_get_config_file (void)
{
	gchar *result;
	gchar *config_file;

	/* Get config file */
	result = gdmcomm_call_gdm ("GET_CONFIG_FILE", NULL /* auth cookie */, "2.8.0.2", 5);
	if (! result)
		return NULL;

	if (ve_string_empty (result) ||
		strncmp (result, "OK ", 3) != 0) {
		g_free (result);
		return NULL;
	}

	/* skip the "OK " */
	config_file = g_strdup (result + 3);

	g_free (result);

	return config_file;
}

gchar * 
gdm_common_get_custom_config_file (void)
{
	gchar *result;
	gchar *config_file;

	/* Get config file */
	result = gdmcomm_call_gdm ("GET_CUSTOM_CONFIG_FILE", NULL /* auth cookie */, "2.8.0.2", 5);
	if (! result)
		return NULL;

	if (ve_string_empty (result) ||
		strncmp (result, "OK ", 3) != 0) {
		g_free (result);
		return NULL;
	}

	/* skip the "OK " */
	config_file = g_strdup (result + 3);

	g_free (result);

	return config_file;
}

gboolean
gdm_common_select_time_format (void)
{
	gchar *val = gdm_config_get_string (GDM_KEY_USE_24_CLOCK);

	if (val != NULL &&
	    (val[0] == 'T' ||
	     val[0] == 't' ||
	     val[0] == 'Y' ||
	     val[0] == 'y' ||
	     atoi (val) != 0)) {
		return TRUE;
	} else if (val != NULL &&
	    (val[0] == 'F' ||
	     val[0] == 'f' ||
	     val[0] == 'N' ||
	     val[0] == 'n')) {
		return FALSE;
	} else {
		/* Value is "auto" (default), thus select according to
		   "locale" settings. */
		char outstr[20];
		time_t t;
		struct tm *tmp;

		t = time(NULL);
		tmp = localtime (&t);

		/* if the locale does not have an AM/PM string, use 24h time */
		return (strftime (outstr, sizeof(outstr), "%p", tmp) == 0);
	}
	/* NOTREACHED */
	return TRUE;
}

/* Not to look too shaby on Xinerama setups */
void
gdm_common_setup_background_color (gchar *bg_color)
{
  GdkColormap *colormap;
  GdkColor color;

  if (bg_color == NULL ||
      bg_color[0] == '\0' ||
      ! gdk_color_parse (bg_color, &color))
    {
      gdk_color_parse ("#007777", &color);
    }

  g_free (bg_color);

  colormap = gdk_drawable_get_colormap
          (gdk_get_default_root_window ());
  /* paranoia */
  if (colormap != NULL)
    {
      gboolean success;
      gdk_error_trap_push ();

      gdk_colormap_alloc_colors (colormap, &color, 1,
                                 FALSE, TRUE, &success);
      gdk_window_set_background (gdk_get_default_root_window (), &color);
      gdk_window_clear (gdk_get_default_root_window ());

      gdk_flush ();
      gdk_error_trap_pop ();
    }
}

gchar *
gdm_common_get_welcomemsg (void)
{
        gchar *welcomemsg;
	gchar *tempstr;

	/*
	 * Translate the welcome msg in the client program since it is running as the
	 * user and therefore has the appropriate language environment set.
	 */
        if (ve_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
                if (gdm_config_get_bool (GDM_KEY_DEFAULT_REMOTE_WELCOME))
                        welcomemsg = g_strdup (_(GDM_DEFAULT_REMOTE_WELCOME_MSG));
                else {
			tempstr = gdm_config_get_translated_string (GDM_KEY_REMOTE_WELCOME);

			if (tempstr == NULL ||
			    strcmp (ve_sure_string (tempstr), GDM_DEFAULT_REMOTE_WELCOME_MSG) == 0)
				welcomemsg = g_strdup (_(GDM_DEFAULT_REMOTE_WELCOME_MSG));
			else
				welcomemsg = g_strdup (tempstr);
		}
        } else {
                if (gdm_config_get_bool (GDM_KEY_DEFAULT_WELCOME))
                        welcomemsg = g_strdup (_(GDM_DEFAULT_WELCOME_MSG));
                else {
                        tempstr = gdm_config_get_translated_string (GDM_KEY_WELCOME);

			if (tempstr == NULL ||
			    strcmp (ve_sure_string (tempstr), GDM_DEFAULT_WELCOME_MSG) == 0)
				welcomemsg = g_strdup (_(GDM_DEFAULT_WELCOME_MSG));
			else
				welcomemsg = g_strdup (tempstr);
		}
        }

	return welcomemsg;
}

static gchar *
pre_fetch_prog_get_path (void)
{
	gchar *prefetchprog;

	prefetchprog = gdm_config_get_string (GDM_KEY_PRE_FETCH_PROGRAM);
	if  (! ve_string_empty (prefetchprog)) {
		return prefetchprog;
	} else
		return NULL;
}

static gboolean
pre_fetch_run (gpointer data)
{
	GPid pid = -1;
	GError *error = NULL;
	char *command = NULL;
	gchar **pre_fetch_prog_argv =  NULL;

	command = pre_fetch_prog_get_path ();

	if (! command)
		return FALSE;

	pre_fetch_prog_argv = ve_split (command);

	g_spawn_async (".",
		       pre_fetch_prog_argv,
		       NULL,
		       (GSpawnFlags) (G_SPAWN_SEARCH_PATH),
		       NULL,
		       NULL,
		       &pid,
		       &error);

	return FALSE;
}

void
gdm_common_pre_fetch_launch (void)
{
	if (! pre_fetch_prog_get_path ())
		return;

	g_idle_add (pre_fetch_run, NULL);
}

/*
 * Returns the string version of the time that the user
 * will need to free.  Requires the user pass in the
 * the_tm structure to be used.  This way the caller
 * has access to the time data as well.
 */
gchar *
gdm_common_get_clock (struct tm **the_tm)
{
        char *str;
        time_t the_time;

        time (&the_time);
        *the_tm = localtime (&the_time);

        if (gdm_common_select_time_format ()) {
                str = ve_strftime (*the_tm, _("%a %b %d, %H:%M"));
        } else {
                /* Translators: You should translate time part as
                   %H:%M if your language does not have AM and PM
                   equivalent.  Note: %l is a strftime option for
                   12-hour clock format */
                str = ve_strftime (*the_tm, _("%a %b %d, %l:%M %p"));
        }

        return str;
}

char *
gdm_common_expand_text (const gchar *text)
{
  GString *str;
  const char *p;
  gchar *clock, *display;
  int r, i, n_chars;
  gboolean underline = FALSE;
  gchar buf[256];
  struct utsname name;
  struct tm *the_tm;

  str = g_string_sized_new (strlen (text));

  p = text;
  n_chars = g_utf8_strlen (text, -1);
  i = 0;
  
  while (i < n_chars)
    {
      gunichar ch;

      ch = g_utf8_get_char (p);

      /* Backslash commands */
      if (ch == '\\')
        {
	  p = g_utf8_next_char (p);
	  i++;
	  ch = g_utf8_get_char (p);

	  if (i >= n_chars || ch == '\0')
	    {
	      g_warning ("Unescaped \\ at end of text\n");
	      goto bail;
	    }
	  else if (ch == 'n')
	    g_string_append_unichar (str, '\n');
	  else
	    g_string_append_unichar (str, ch);
	}
      else if (ch == '%')
	{
	  p = g_utf8_next_char (p);
	  i++;
	  ch = g_utf8_get_char (p);

	  if (i >= n_chars || ch == '\0')
	    {
	      g_warning ("Unescaped %% at end of text\n");
	      goto bail;
	    }

	  switch (ch)
	    {
	    case '%':
	      g_string_append (str, "%");
	      break;
	    case 'c':
	      clock = gdm_common_get_clock (&the_tm);
	      g_string_append (str, clock);
	      g_free (clock);
	      break;
	    case 'd':
	      display = g_strdup (g_getenv ("DISPLAY"));
	      g_string_append (str, display);
	      break;
	    case 'h':
	      buf[sizeof (buf) - 1] = '\0';
	      r = gethostname (buf, sizeof (buf) - 1);
	      if (r)
		g_string_append (str, "localhost");
	      else
		g_string_append (str, buf);
	      break;
	    case 'm':
	      uname (&name);
	      g_string_append (str, name.machine);
	      break;
	    case 'n':
	      uname (&name);
	      g_string_append (str, name.nodename);
	      break;
	    case 'o':
	      buf[sizeof (buf) - 1] = '\0';
	      r = getdomainname (buf, sizeof (buf) - 1);
	      if (r)
		g_string_append (str, "localdomain");
	      else
		g_string_append (str, buf);
	      break;
	    case 'r':
	      uname (&name);
	      g_string_append (str, name.release);
	      break;
	    case 's':
	      uname (&name);
	      g_string_append (str, name.sysname);
	      break;
	    case 't':
	      g_string_append_printf (str, ngettext("%d second", "%d seconds", gdm_timed_delay),
				      gdm_timed_delay);
	      break;
	    case 'u':
	      g_string_append (str, ve_sure_string (g_getenv("GDM_TIMED_LOGIN_OK")));
	      break;
	    default:
	      if (ch < 127)
	        g_warning ("unknown escape code %%%c in text\n", (char)ch);
	      else
		g_warning ("unknown escape code %%(U%x) in text\n", (int)ch);
	    }
	}
      else if (ch == '_')
        {
          /*
           * Could be true if an underscore was put right before a special
           * character like % or /
           */
          if (underline == FALSE) {
            underline = TRUE;
	    g_string_append (str, "<u>");
	  }
	}
      else
	{
	  g_string_append_unichar (str, ch);
	  if (underline)
	    {
	      underline = FALSE;
	      g_string_append (str, "</u>");
	    }
	}
      p = g_utf8_next_char (p);
      i++;
    }
  
 bail:

  if (underline)
    g_string_append (str, "</u>");

  return g_string_free (str, FALSE);
}

