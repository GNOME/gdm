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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <gnome.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <syslog.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <pwd.h>
#include <sys/utsname.h>

#include "gdmlogin.h"
#include "gdm.h"
#include "filecheck.h"

static const gchar RCSid[]="$Id$";

static gboolean GdmBrowser;
static gboolean GdmDebug;
static gint  GdmIconMaxHeight;
static gint  GdmIconMaxWidth;
static gboolean GdmQuiver;
static gint  GdmRelaxPerms;
static gboolean GdmSystemMenu;
static gint  GdmUserMaxFile;
static gchar *GdmLogo;
static gchar *GdmWelcome;
static gchar *GdmBackgroundProg;
static gchar *GdmFont;
static gchar *GdmGtkRC;
static gchar *GdmIcon;
static gchar *GdmSessionDir;
static gchar *GdmLocaleFile;
static gchar *GdmDefaultLocale;
static gchar *GdmExclude;
static gchar *GdmGlobalFaceDir;
static gchar *GdmDefaultFace;
static gboolean GdmLockPosition;
static gboolean GdmSetPosition;
static int GdmPositionX;
static int GdmPositionY;

static GtkWidget *login;
static GtkWidget *label;
static GtkWidget *entry;
static GtkWidget *msg;
static gboolean first_message = TRUE;
static GtkWidget *win;
static GtkWidget *sessmenu;
static GtkWidget *langmenu;
static GdkWindow *rootwin;
static GdkWindow *rootwin_overlay = NULL;

static GnomeIconList *browser;
static GdkImlibImage *defface;

/* Eew. Loads of global vars. It's hard to be event controlled while maintaining state */
static GSList *sessions = NULL;
static GSList *languages = NULL;
static GList *users = NULL;
static GSList *exclude = NULL;

static gchar *defsess = NULL;
static gchar *cursess = NULL;
static gchar *curlang = NULL;
static gchar *curuser = NULL;
static gchar *lastsess = NULL;
static gchar *lastlang = NULL;
static gchar *session = NULL;
static gchar *language = NULL;

static gboolean savesess;
static gboolean savelang;
static gint maxwidth;

static pid_t backgroundpid = 0;

static void
gdm_greeter_chld (int sig)
{
	if (backgroundpid != 0 &&
	    waitpid (backgroundpid, NULL, WNOHANG) > 0) {
		backgroundpid = 0;
	}
}

static void
kill_background (void)
{
	if (backgroundpid != 0) {
		kill (backgroundpid, SIGTERM);
		backgroundpid = 0;
	}
}

static void
gdm_login_done (int sig)
{
    kill_background ();
    _exit (DISPLAY_SUCCESS);
}

static void
set_screen_pos (GtkWidget *widget, int x, int y)
{
	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (widget));

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x > gdk_screen_width () - widget->allocation.width)
		x = gdk_screen_width () - widget->allocation.width;
	if (y > gdk_screen_height () - widget->allocation.height)
		y = gdk_screen_height () - widget->allocation.height;

	gtk_widget_set_uposition (widget, x, y);
}

typedef struct _CursorOffset {
	int x;
	int y;
} CursorOffset;

static gboolean
gdm_login_icon_released (GtkWidget *widget)
{
	gtk_grab_remove (widget);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);

	gtk_object_remove_data (GTK_OBJECT (widget), "offset");

	/* no longer send events from this window to widget */
	gdk_window_set_user_data (rootwin_overlay, NULL);

	return TRUE;
}

static gboolean
gdm_login_icon_pressed (GtkWidget *widget, GdkEventButton *event)
{
    CursorOffset *p;
    GdkCursor *fleur_cursor;

    if (event->type == GDK_2BUTTON_PRESS) {
	    gdm_login_icon_released (widget);
	    gtk_widget_destroy (GTK_WIDGET (win));
	    gtk_widget_show (login);
	    gtk_widget_grab_focus (entry);	
	    return TRUE;
    }
    
    if (event->type != GDK_BUTTON_PRESS)
	return FALSE;
    

    p = g_new0 (CursorOffset, 1);
    gtk_object_set_data_full (GTK_OBJECT (widget), "offset", p,
			      (GtkDestroyNotify)g_free);
    p->x = (gint) event->x;
    p->y = (gint) event->y;

    gtk_grab_add (widget);
    fleur_cursor = gdk_cursor_new (GDK_FLEUR);
    gdk_pointer_grab (rootwin_overlay, TRUE,
		      GDK_BUTTON_RELEASE_MASK |
		      GDK_BUTTON_MOTION_MASK |
		      GDK_POINTER_MOTION_HINT_MASK,
		      NULL,
		      fleur_cursor,
		      GDK_CURRENT_TIME);
    gdk_cursor_destroy (fleur_cursor);
    /* send events from this window to widget */
    gdk_window_set_user_data (rootwin_overlay, widget);
    gdk_flush ();

    return TRUE;
}

static gboolean
gdm_login_icon_motion (GtkWidget *widget, GdkEventMotion *event)
{
	gint xp, yp;
	CursorOffset *p;
	GdkModifierType mask;

	p = gtk_object_get_data (GTK_OBJECT (widget), "offset");

	if (p == NULL)
		return FALSE;

	gdk_window_get_pointer (rootwin, &xp, &yp, &mask);
	set_screen_pos (GTK_WIDGET (widget), xp-p->x, yp-p->y);

	return TRUE;
}


static void
gdm_login_iconify_handler (GtkWidget *widget, gpointer data)
{
    GtkWidget *fixed;
    GtkWidget *icon;
    GdkGC *gc;
    GtkStyle *style;
    gint rw, rh, iw, ih;

    gtk_widget_hide (login);
    style = gtk_widget_get_default_style();
    gc = style->black_gc; 
    win = gtk_window_new (GTK_WINDOW_POPUP);

    gtk_widget_set_events (win, 
			   gtk_widget_get_events (GTK_WIDGET (win)) | 
			   GDK_BUTTON_PRESS_MASK |
			   GDK_BUTTON_MOTION_MASK |
			   GDK_POINTER_MOTION_HINT_MASK);

    gtk_widget_realize (GTK_WIDGET (win));

    fixed = gtk_fixed_new();
    gtk_container_add (GTK_CONTAINER (win), fixed);
    gtk_widget_show (fixed);

    icon = gnome_pixmap_new_from_file (GdmIcon);
    gdk_window_get_size ((GdkWindow *) GNOME_PIXMAP (icon)->pixmap, &iw, &ih);
    
    gtk_fixed_put(GTK_FIXED (fixed), GTK_WIDGET (icon), 0, 0);
    gtk_widget_show(GTK_WIDGET (icon));

    gtk_signal_connect (GTK_OBJECT (win), "button_press_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_pressed),NULL);
    gtk_signal_connect (GTK_OBJECT (win), "button_release_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_released),NULL);
    gtk_signal_connect (GTK_OBJECT (win), "motion_notify_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_motion),NULL);

    gtk_widget_show (GTK_WIDGET (win));

    rw = gdk_screen_width();
    rh = gdk_screen_height();

    set_screen_pos (GTK_WIDGET (win), rw-iw, rh-ih);
}


static void
gdm_login_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	kill_background ();
	exit (DISPLAY_ABORT);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, s);
    closelog();

    kill_background ();
    exit (DISPLAY_ABORT);
}


/* I *really* need to rewrite this crap */
static gchar *
gdm_parse_enriched_string (const gchar *s)
{
    gchar cmd, *buffer;
    gchar hostbuf[256];
    gchar *hostname, *temp1, *temp2, *display;
    struct utsname name;
    GString *str;

    if (s == NULL)
	return(NULL);

    display = g_strdup (g_getenv ("DISPLAY"));

    if (display == NULL)
	return(NULL);

    temp1 = strchr (display, '.');
    temp2 = strchr (display, ':');

    if (temp1 != NULL) {
	    *temp1 = '\0';
    } else if (temp2 != NULL) {
	    *temp2 = '\0';
    } else {
	    g_free (display);
	    return (NULL);
    }

    gethostname (hostbuf, 255);
    hostname = g_strdup (hostbuf);
    
    if (hostname != NULL) 
	hostname = g_strdup ("Gnome");

    uname (&name);

    if (strlen (s) > 1023) {
	syslog (LOG_ERR, _("gdm_parse_enriched_string: String too long!"));
	g_free (display);
	return (g_strdup_printf (_("Welcome to %s"), hostname));
    }

    str = g_string_new (NULL);

    while (*s != '\0') {

	if (*s == '%' && s[1] != 0) {
		cmd = s[1];
		s+=2;

		switch (cmd) {

		case 'h': 
			g_string_append (str, hostname);
			break;

		case 'n':
			g_string_append (str, name.nodename);
			break;

		case 'd': 
			g_string_append (str, display);
			break;

		case 's':
			g_string_append (str, name.sysname);
			break;

		case 'r':
			g_string_append (str, name.release);
			break;

		case 'm':
			g_string_append (str, name.machine);
			break;

		case '%':
			g_string_append_c (str, '%');
			break;

		default:
			break;
		};
	} else {
		g_string_append_c (str, *s);
	}
	s++;
    }

    buffer = str->str;
    g_string_free (str, FALSE);

    g_free (display);
    g_free (hostname);

    return buffer;
}


static gboolean
gdm_login_query (const gchar *msg)
{
    GtkWidget *req;

    req = gnome_message_box_new (msg,
				 GNOME_MESSAGE_BOX_QUESTION,
				 GNOME_STOCK_BUTTON_YES,
				 GNOME_STOCK_BUTTON_NO,
				 NULL);
	    
    gtk_window_set_modal (GTK_WINDOW (req), TRUE);
    gtk_window_set_position (GTK_WINDOW (req), GTK_WIN_POS_CENTER);

    return (!gnome_dialog_run (GNOME_DIALOG(req)));
}


static gboolean
gdm_login_reboot_handler (void)
{
    if (gdm_login_query (_("Are you sure you want to reboot the machine?"))) {
	closelog();

        kill_background ();
	exit (DISPLAY_REBOOT);
    }

    return (TRUE);
}


static gboolean
gdm_login_halt_handler (void)
{
    if (gdm_login_query (_("Are you sure you want to halt the machine?"))) {
	closelog();

        kill_background ();
	exit (DISPLAY_HALT);
    }

    return (TRUE);
}


static void 
gdm_login_parse_config (void)
{
    gchar *display;
    struct stat unused;
	
    if (stat (GDM_CONFIG_FILE, &unused) == -1)
	gdm_login_abort (_("gdm_login_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmBrowser = gnome_config_get_bool (GDM_KEY_BROWSER);
    GdmLogo = gnome_config_get_string (GDM_KEY_LOGO);
    GdmFont = gnome_config_get_string (GDM_KEY_FONT);
    GdmIcon = gnome_config_get_string (GDM_KEY_ICON);
    GdmQuiver = gnome_config_get_bool (GDM_KEY_QUIVER);
    GdmSystemMenu = gnome_config_get_bool (GDM_KEY_SYSMENU);
    GdmUserMaxFile = gnome_config_get_int (GDM_KEY_MAXFILE);
    GdmRelaxPerms = gnome_config_get_int (GDM_KEY_RELAXPERM);
    GdmLocaleFile = gnome_config_get_string (GDM_KEY_LOCFILE);
    GdmDefaultLocale = gnome_config_get_string (GDM_KEY_LOCALE);
    GdmSessionDir = gnome_config_get_string (GDM_KEY_SESSDIR);
    GdmWelcome = gnome_config_get_translated_string (GDM_KEY_WELCOME);
    GdmBackgroundProg = gnome_config_get_string (GDM_KEY_BACKGROUNDPROG);
    GdmGtkRC = gnome_config_get_string (GDM_KEY_GTKRC);
    GdmExclude = gnome_config_get_string (GDM_KEY_EXCLUDE);
    GdmGlobalFaceDir = gnome_config_get_string (GDM_KEY_FACEDIR);
    GdmDefaultFace = gnome_config_get_string (GDM_KEY_FACE);
    GdmDebug = gnome_config_get_bool (GDM_KEY_DEBUG);
    GdmIconMaxWidth = gnome_config_get_int (GDM_KEY_ICONWIDTH);
    GdmIconMaxHeight = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
    GdmLockPosition = gnome_config_get_bool (GDM_KEY_LOCK_POSITION);
    GdmSetPosition = gnome_config_get_bool (GDM_KEY_SET_POSITION);
    GdmPositionX = gnome_config_get_int (GDM_KEY_POSITIONX);
    GdmPositionY = gnome_config_get_int (GDM_KEY_POSITIONY);

    gnome_config_pop_prefix();

    if (stat (GdmLocaleFile, &unused) == -1)
	gdm_login_abort ("gdm_login_parse_config: Could not open locale file %s. Aborting!", GdmLocaleFile);


    /* Disable System menu on non-local displays */
    display = g_getenv ("DISPLAY");

    if (!display)
	gdm_login_abort ("gdm_login_parse_config: DISPLAY variable not set!");

    if (strncmp (display, ":", 1))
	GdmSystemMenu = FALSE;
}


static gboolean 
gdm_login_list_lookup (GSList *l, const gchar *data)
{
    GSList *list = l;

    if (!list || !data)
	return(FALSE);

    while (list) {

	if (g_strcasecmp (list->data, data) == 0)
	    return (TRUE);
	
	list = list->next;
    }

    return (FALSE);
}


static void
gdm_login_session_lookup (const gchar* savedsess)
{
    if (curuser == NULL)
	gdm_login_abort("gdm_login_session_lookup: curuser==NULL. Mail <mkp@mkp.net> with " \
			"information on your PAM and user database setup");

    /* Don't save session unless told otherwise */
    savesess = FALSE;

    gtk_widget_set_sensitive (GTK_WIDGET (sessmenu), FALSE);

    /* Previously saved session not found in ~user/.gnome/gdm */
    if ( ! (savedsess != NULL &&
	    strcmp ("(null)", savedsess) != 0 &&
	    savedsess[0] != '\0')) {
	    /* If "Last" is chosen run Default,
	     * else run user's current selection */
	    g_free (session);
	    if (g_strcasecmp (cursess, lastsess) == 0)
		    session = g_strdup (defsess);
	    else
		    session = g_strdup (cursess);

	    savesess = TRUE;
	    return;
    }

    /* If "Last" session is selected */
    if (g_strcasecmp (cursess, lastsess) == 0) { 
	g_free (session);
	session = g_strdup (savedsess);

	/* Check if user's saved session exists on this box */
	if (!gdm_login_list_lookup (sessions, session)) {
	    gchar *msg;

	    g_free (session);
	    session = g_strdup (defsess);
	    msg = g_strdup_printf (_("Your preferred session type %s is not installed on this machine.\n" \
				     "Do you wish to make %s the default for future sessions?"),
				   savedsess, defsess);	    
	    savesess = gdm_login_query (msg);
	    g_free (msg);
	}
    }
    /* One of the other available session types is selected */
    else { 
	g_free (session);
	session = g_strdup (cursess);

	/* User's saved session is not the chosen one */
	if (g_strcasecmp (savedsess, session) != 0) {
	    gchar *msg;

	    msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is " \
				     "%s.\nDo you wish to make %s the default for future sessions?"),
				   cursess, savedsess, cursess);
	    savesess = gdm_login_query (msg);
	    g_free (msg);
	}
    }
}


static void
gdm_login_language_lookup (const gchar* savedlang)
{
    if (curuser == NULL)
	gdm_login_abort("gdm_login_language_lookup: curuser==NULL. Mail <mkp@mkp.net> with " \
			"information on your PAM and user database setup");

    /* Don't save language unless told otherwise */
    savelang = FALSE;

    gtk_widget_set_sensitive (GTK_WIDGET (langmenu), FALSE);

    /* Previously saved language not found in ~user/.gnome/gdm */
    if (savedlang && ! strlen (savedlang)) {
	/* If "Last" is chosen use Default, else use current selection */
	g_free (language);
	if (g_strcasecmp (curlang, lastlang) == 0)
	    language = g_strdup (GdmDefaultLocale);
	else
	    language = g_strdup (curlang);

	/* Now this is utterly ugly, but I suppose it works */
	language[0] = tolower (language[0]);

	savelang = TRUE;
	return;
    }

    /* If a different language is selected */
    if (g_strcasecmp (curlang, lastlang) != 0) {
        g_free (language);
	language = g_strdup (curlang);

	/* User's saved language is not the chosen one */
	if (g_strcasecmp (savedlang, language) != 0) {
	    gchar *msg;

	    msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is " \
				     "%s.\nDo you wish to make %s the default for future sessions?"),
				   curlang, savedlang, curlang);
	    savelang = gdm_login_query (msg);
	    g_free (msg);
	}
    } else {
	g_free (language);
	language = g_strdup (savedlang);
    }

    /* Now this is utterly ugly, but I suppose it works */
    language[0] = tolower (language[0]);
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
		width = gdk_screen_width ();
	if (height == -1)
		height = gdk_screen_height ();

	if (login == NULL ||
	    login->window == NULL) {
		dance_handler = 0;
		return FALSE;
	}

	xm = cos (1.01 * t1);
	ym = sin (0.90 * t2);

	t1 += 0.03 + (rand () % 10) / 500.0;
	t2 += 0.03 + (rand () % 10) / 500.0;

	x = (width / 2) + (width / 5) * xm;
	y = (height / 2) + (height / 5) * ym;

	set_screen_pos (login,
			x - login->allocation.width / 2,
			y - login->allocation.height / 2);

	return TRUE;
}

static void
evil (const char *user)
{
	if (dance_handler == 0 &&
	    strcmp (user, "Start Dancing") == 0) {
		dance_handler = gtk_timeout_add (50, dance, NULL);
		GdmLockPosition = TRUE;
	}
}

static gboolean
gdm_login_entry_handler (GtkWidget *widget, GdkEventKey *event)
{
    if (!event)
	return(TRUE);

    switch (event->keyval) {

    case GDK_Return:
	gtk_widget_set_sensitive (entry, FALSE);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);

	/* Save login. I'm making the assumption that login is always
	 * the first thing entered. This might not be true for all PAM
	 * setups. Needs thinking! 
	 */

	if (curuser == NULL) {
	    curuser = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

	    /* evilness */
	    evil (curuser);
	}

	g_print ("%c%s\n", STX, gtk_entry_get_text (GTK_ENTRY (entry)));
	break;

    case GDK_Up:
    case GDK_Down:
    case GDK_Tab:
	gtk_signal_emit_stop_by_name (GTK_OBJECT (entry), "key_press_event");
	break;

    default:
	break;
    }

    return (TRUE);
}


static void 
gdm_login_session_handler (GtkWidget *widget) 
{
    gchar *s;

    gtk_label_get (GTK_LABEL (GTK_BIN (widget)->child), &cursess);
    s = g_strdup_printf (_("%s session selected"), cursess);
    gtk_label_set (GTK_LABEL (msg), s);
    g_free (s);
}


static void 
gdm_login_session_init (GtkWidget *menu)
{
    GSList *sessgrp = NULL;
    GtkWidget *item;
    DIR *sessdir;
    struct dirent *dent;
    struct stat statbuf;
    gint linklen;

    lastsess=_("Last");

    cursess = lastsess;
    item = gtk_radio_menu_item_new_with_label (NULL, lastsess);
    sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (gdm_login_session_handler),
			NULL);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new();
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    /* Check that session dir is readable */
    if (access (GdmSessionDir, R_OK|X_OK))
	gdm_login_abort (_("gdm_login_session_init: Session script directory not found!"));

    /* Read directory entries in session dir */
    sessdir = opendir (GdmSessionDir);
    dent = readdir (sessdir);

    while (dent != NULL) {
	gchar *s;

	/* Ignore backups and rpmsave files */
	if ((strstr (dent->d_name, "~")) || (strstr (dent->d_name, ".rpmsave"))) {
	    dent = readdir (sessdir);
	    continue;
	}

	s = g_strconcat (GdmSessionDir, "/", dent->d_name, NULL);
	lstat (s, &statbuf);

	/* If default session link exists, find out what it points to */
	if (S_ISLNK (statbuf.st_mode) &&
	    g_strcasecmp (dent->d_name, "default") == 0) {
	    gchar t[_POSIX_PATH_MAX];
	    
	    linklen = readlink (s, t, _POSIX_PATH_MAX);
	    t[linklen] = 0;
	    defsess = g_strdup (t);
	}

	/* If session script is readable/executable add it to the list */
	if (S_ISREG (statbuf.st_mode)) {

	    if ((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
		(statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
		(statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) 
	    {
		item = gtk_radio_menu_item_new_with_label (sessgrp, dent->d_name);
		sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
		sessions = g_slist_append (sessions, dent->d_name);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (gdm_login_session_handler),
				    NULL);
		gtk_widget_show (GTK_WIDGET (item));
	    }
	    else 
		syslog (LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
			GdmSessionDir, dent->d_name);

	}

	dent = readdir (sessdir);
	g_free (s);
    }

    if (!g_slist_length (sessgrp)) 
	gdm_login_abort (_("No session scripts found. Aborting!"));

    if (!defsess) {
	gtk_label_get (GTK_LABEL (GTK_BIN (g_slist_nth_data (sessgrp, 0))->child), &defsess);
	syslog (LOG_WARNING, _("No default session link found. Using %s.\n"), defsess);
    }
}


static void 
gdm_login_language_handler (GtkWidget *widget) 
{
    gchar *s;

    if (!widget)
	return;

    gtk_label_get (GTK_LABEL (GTK_BIN (widget)->child), &curlang);
    s = g_strdup_printf (_("%s language selected"), curlang);
    gtk_label_set (GTK_LABEL (msg), s);
    g_free (s);
}


static void
gdm_login_language_init (GtkWidget *menu)
{
    GtkWidget *item, *ammenu, *nzmenu, *omenu;
    FILE *langlist;
    char curline[256];
    char *ctmp, *ctmp1, *ctmp2;
    int has_other_locale = FALSE;
    GtkWidget *other_menu;

    if (!menu)
	return;

    lastlang = _("Last");
    curlang = lastlang;

    item = gtk_radio_menu_item_new_with_label (NULL, lastlang);
    languages = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_append (GTK_MENU(menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate", 
			GTK_SIGNAL_FUNC (gdm_login_language_handler), 
			NULL);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new();
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new_with_label (_("A-M"));
    ammenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (ammenu));
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new_with_label (_("N-Z"));
    nzmenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), nzmenu);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show(GTK_WIDGET (item));

    other_menu = item = gtk_menu_item_new_with_label (_("Other"));
    omenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), omenu);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    langlist = fopen (GdmLocaleFile, "r");

    if (!langlist)
	return;
        
    while (fgets (curline, sizeof (curline), langlist)) {

	if (!isalpha (curline[0])) 
	    continue;
	
	ctmp1 = strchr (curline, ' ');
	ctmp2 = strchr (curline, '\t');
	ctmp = curline + strlen (curline) - 1;

	if (ctmp1 && (ctmp1 < ctmp))
	    ctmp = ctmp1;

	if (ctmp2 && (ctmp2 < ctmp))
	    ctmp = ctmp2;

	*ctmp = '\0';
	curline[0] = toupper (curline[0]);
	
	item = gtk_radio_menu_item_new_with_label (languages, curline);
	languages = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));

	if (curline[0] >= 'A' && curline[0] <= 'M')
	    gtk_menu_append (GTK_MENU (ammenu), item);
	else if (curline[0] >= 'N' && curline[0] <= 'Z')
	    gtk_menu_append (GTK_MENU (nzmenu), item);
	else {
	    gtk_menu_append (GTK_MENU (omenu), item);
	    has_other_locale = 1;
	}

	gtk_signal_connect (GTK_OBJECT (item), "activate", 
			    GTK_SIGNAL_FUNC (gdm_login_language_handler), 
			    NULL);
	gtk_widget_show (GTK_WIDGET (item));
    }
    if (!has_other_locale) 
        gtk_widget_destroy(other_menu);
    
    fclose (langlist);
}


static gboolean
gdm_login_ctrl_handler (GIOChannel *source, GIOCondition cond, gint fd)
{
    gchar buf[PIPE_SIZE];
    gint len;
    gint i, x, y;

    /* If this is not incoming i/o then return */
    if (cond != G_IO_IN) 
	return (TRUE);

    /* Read random garbage from i/o channel until STX is found */
    do {
	g_io_channel_read (source, buf, 1, &len);

	if (len != 1)
	    return (TRUE);

    } 
    while (buf[0] && buf[0] != STX);

    /* Read opcode */
    g_io_channel_read (source, buf, 1, &len);

    /* If opcode couldn't be read */
    if (len != 1)
	return (TRUE);

    /* Parse opcode */
    switch (buf[0]) {
    case GDM_PROMPT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	/* Turn off the message whenever the prompt changes,
	   this is sort of a hack. Also, don't turn it off
	   the first time. Yeah I know.  */
	if (first_message)
	  first_message = FALSE;
	else
	  gtk_label_set (GTK_LABEL(msg), "");

	gtk_label_set (GTK_LABEL (label), buf);
	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_widget_show (entry);
	break;

    case GDM_NOECHO:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	/* Turn off the message whenever the prompt changes,
	   this is sort of a hack. Also, don't turn it off
	   the first time. Yeah I know.  */
	if (first_message)
	  first_message = FALSE;
	else
	  gtk_label_set (GTK_LABEL(msg), "");

	gtk_label_set (GTK_LABEL(label), buf);
	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_widget_show (entry);
	break;

    case GDM_MSGERR:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';
	gtk_label_set (GTK_LABEL (msg), buf);
	gtk_widget_show (GTK_WIDGET (msg));
	g_print ("%c\n", STX);

	break;

    case GDM_SESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	buf[len-1] = '\0';
	gdm_login_session_lookup (buf);
	g_print ("%c%s\n", STX, session);
	break;

    case GDM_LANG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	buf[len-1] = '\0';
	gdm_login_language_lookup (buf);
	g_print ("%c%s\n", STX, language);
	break;

    case GDM_SSESS:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	if (savesess)
	    g_print ("%cY\n", STX);
	else
	    g_print ("%c\n", STX);
	
	break;

    case GDM_SLANG:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	if (savelang)
	    g_print ("%cY\n", STX);
	else
	    g_print ("%c\n", STX);

	break;

    case GDM_RESET:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	if (GdmQuiver) {
	    gdk_window_get_position (login->window, &x, &y);
	    
	    for (i=32 ; i > 0 ; i=i/4) {
		gdk_window_move (login->window, i+x, y);
		gdk_flush ();
		usleep (200);
		gdk_window_move (login->window, x, y);
		gdk_flush ();
		usleep (200);
		gdk_window_move (login->window, -i+x, y);
		gdk_flush ();
		usleep (200);
		gdk_window_move (login->window, x, y);
		gdk_flush ();
		usleep (200);
	    }
	}

	if (curuser != NULL) {
	    g_free (curuser);
	    curuser = NULL;
	}

	gtk_widget_set_sensitive (entry, TRUE);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);

	g_print ("%c\n", STX);
	break;

    case GDM_QUIT:
        kill_background ();
	exit (EXIT_SUCCESS);
	break;
	
    default:
	break;
    }

    return (TRUE);
}


static void
gdm_login_browser_update (void)
{
    GList *list = users;

    gnome_icon_list_clear (GNOME_ICON_LIST (browser));

    while (list) {
	GdmLoginUser *user = list->data;

	gnome_icon_list_append_imlib (GNOME_ICON_LIST (browser), user->picture, user->login);
	list = list->next;
    }

    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));
}


static gboolean 
gdm_login_browser_select (GtkWidget *widget, gint selected, GdkEvent *event)
{
    GdmLoginUser *user;

    if (!widget || !event)
	return (TRUE);

    switch (event->type) {
	    
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
	user = g_list_nth_data (users, selected);

	if (user && user->login)
	    gtk_entry_set_text (GTK_ENTRY (entry), user->login);

	break;

    case GDK_2BUTTON_PRESS:
	user = g_list_nth_data (users, selected);

	if (user && user->login)
	    gtk_entry_set_text (GTK_ENTRY (entry), user->login);

	if (curuser == NULL)
	    curuser = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

	gtk_widget_set_sensitive (entry, FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);
	g_print ("%c%s\n", STX, gtk_entry_get_text (GTK_ENTRY (entry)));
	break;
	
    default: 
	break;
    }
    
    return (TRUE);
}


static gboolean
gdm_login_browser_unselect (GtkWidget *widget, gint selected, GdkEvent *event)
{
    if (!widget || !event)
	return (TRUE);

    switch (event->type) {
	    
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	break;
	
    default:
	break;
    }

    return (TRUE);
}

static GdkFilterReturn
root_keys_filter (GdkXEvent *gdk_xevent,
		  GdkEvent *event,
		  gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;
	XEvent new_xevent;

	if (xevent->type != KeyPress &&
	    xevent->type != KeyRelease)
		return GDK_FILTER_CONTINUE;

	if (entry->window == NULL)
		return GDK_FILTER_CONTINUE;

	/* EVIIIIIIIIIL, but works */
	/* -George */
	new_xevent = *xevent;
	new_xevent.xany.window = GDK_WINDOW_XWINDOW (entry->window);
	XSendEvent (GDK_DISPLAY (),
		    GDK_WINDOW_XWINDOW (entry->window),
		    True, NoEventMask, &new_xevent);

	return GDK_FILTER_CONTINUE;
}

static void
gdm_init_root_window_overlay (void)
{
	GdkWindowAttr attributes;
	gint attributes_mask;

	attributes.window_type = GDK_WINDOW_TEMP;
	attributes.x = 0;
	attributes.y = 0;
	attributes.width = gdk_screen_width ();
	attributes.height = gdk_screen_height ();
	attributes.wclass = GDK_INPUT_ONLY;
	attributes.event_mask = (GDK_BUTTON_PRESS_MASK |
				 GDK_BUTTON_RELEASE_MASK |
				 GDK_POINTER_MOTION_MASK |
				 GDK_POINTER_MOTION_HINT_MASK |
				 GDK_KEY_PRESS_MASK |
				 GDK_KEY_RELEASE_MASK |
				 GDK_ENTER_NOTIFY_MASK |
				 GDK_LEAVE_NOTIFY_MASK);
	attributes_mask = GDK_WA_X | GDK_WA_Y;

	rootwin_overlay = gdk_window_new (NULL,
					  &attributes,
					  attributes_mask);

	gdk_window_show (rootwin_overlay);

	gdk_window_add_filter (rootwin_overlay, root_keys_filter, NULL);
}

static gboolean
handle_expose (GtkWidget *handle, GdkEventExpose *event, gpointer data)
{
	if (handle->window != NULL)
		gtk_paint_handle (handle->style,
				  handle->window,
				  handle->state,
				  GTK_SHADOW_NONE,
				  &event->area,
				  handle,
				  "gdm-handle",
				  0, 0,
				  handle->allocation.width,
				  handle->allocation.height,
				  GTK_ORIENTATION_HORIZONTAL);
	return TRUE;
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

    p = g_new0 (CursorOffset, 1);
    gtk_object_set_data_full (GTK_OBJECT (widget), "offset", p,
			      (GtkDestroyNotify)g_free);
    
    gdk_window_get_pointer (login->window, &xp, &yp, &mask);
    p->x = xp;
    p->y = yp;

    gtk_grab_add (widget);
    fleur_cursor = gdk_cursor_new (GDK_FLEUR);
    gdk_pointer_grab (rootwin_overlay, TRUE,
		      GDK_BUTTON_RELEASE_MASK |
		      GDK_BUTTON_MOTION_MASK |
		      GDK_POINTER_MOTION_HINT_MASK,
		      NULL,
		      fleur_cursor,
		      GDK_CURRENT_TIME);
    gdk_cursor_destroy (fleur_cursor);
    /* send events from this window to widget */
    gdk_window_set_user_data (rootwin_overlay, widget);
    gdk_flush ();
    
    return TRUE;
}

static gboolean
gdm_login_handle_released (GtkWidget *widget, GdkEventButton *event)
{
	gtk_grab_remove (widget);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);

	gtk_object_remove_data (GTK_OBJECT (widget), "offset");

	/* no longer send events from this window to widget */
	gdk_window_set_user_data (rootwin_overlay, NULL);

	return TRUE;
}


static gboolean
gdm_login_handle_motion (GtkWidget *widget, GdkEventMotion *event)
{
    gint xp, yp;
    CursorOffset *p;
    GdkModifierType mask;

    p = gtk_object_get_data (GTK_OBJECT (widget), "offset");

    if (p == NULL)
	    return FALSE;

    gdk_window_get_pointer (rootwin, &xp, &yp, &mask);
    set_screen_pos (GTK_WIDGET (login), xp-p->x, yp-p->y);

    GdmSetPosition = TRUE;
    GdmPositionX = xp - p->x;
    GdmPositionY = yp - p->y;
    gtk_window_position (GTK_WINDOW (login), GTK_WIN_POS_NONE);

    return TRUE;
}


static GtkWidget *
create_handle (void)
{
	GtkWidget *ebox, *hbox, *frame, *w;

	ebox = gtk_event_box_new ();
	gtk_signal_connect (GTK_OBJECT (ebox), "button_press_event",
			    GTK_SIGNAL_FUNC (gdm_login_handle_pressed),
			    NULL);
	gtk_signal_connect (GTK_OBJECT (ebox), "button_release_event",
			    GTK_SIGNAL_FUNC (gdm_login_handle_released),
			    NULL);
	gtk_signal_connect (GTK_OBJECT (ebox), "motion_notify_event",
			    GTK_SIGNAL_FUNC (gdm_login_handle_motion),
			    NULL);

	frame = gtk_frame_new (NULL);
	gtk_container_add (GTK_CONTAINER (ebox), frame);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame), hbox);

	w = gtk_drawing_area_new ();
	gtk_signal_connect (GTK_OBJECT (w), "expose_event",
			    GTK_SIGNAL_FUNC (handle_expose),
			    NULL);
	gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);

	w = gtk_label_new (_("GNOME Desktop Manager"));
	gtk_misc_set_padding (GTK_MISC (w), GNOME_PAD_SMALL, GNOME_PAD_SMALL);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, GNOME_PAD_SMALL);
	
	w = gtk_drawing_area_new ();
	gtk_signal_connect (GTK_OBJECT (w), "expose_event",
			    GTK_SIGNAL_FUNC (handle_expose),
			    NULL);
	gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);

	if (GdmIcon != NULL) {
		if (access (GdmIcon, R_OK)) {
			syslog (LOG_WARNING, _("Can't open icon file: %s. Suspending iconify feature!"), GdmIcon);
		} else {
			w = gtk_button_new ();
			gtk_container_add (GTK_CONTAINER (w),
					   gtk_arrow_new (GTK_ARROW_DOWN,
							  GTK_SHADOW_OUT));
			gtk_signal_connect
				(GTK_OBJECT (w), "clicked",
				 GTK_SIGNAL_FUNC (gdm_login_iconify_handler), 
				 NULL);
			GTK_WIDGET_UNSET_FLAGS (w, GTK_CAN_FOCUS);
			GTK_WIDGET_UNSET_FLAGS (w, GTK_CAN_DEFAULT);
			gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
		}
	}

	gtk_widget_show_all (ebox);

	return ebox;
}

static void
gdm_login_set_font (GtkWidget *widget, const char *font_name)
{
	GdkFont *font;
	GtkStyle *new_style;
	
	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (widget));
	g_return_if_fail (font_name != NULL);

	font = gdk_fontset_load (font_name);

	if (font == NULL)
		return;
	
	gtk_widget_set_rc_style (widget);
	new_style = gtk_style_copy (gtk_widget_get_style (widget));

	gdk_font_unref (new_style->font);
	new_style->font = font;
	
	gtk_widget_set_style (widget, new_style);
	gtk_style_unref (new_style);
}

static void
gdm_login_gui_init (void)
{
    GtkWidget *frame1, *frame2;
    GtkWidget *mbox, *menu, *menubar, *item, *welcome;
    GtkWidget *table, *stack, *hline1, *hline2, *handle;
    GtkWidget *bbox = NULL;
    GtkWidget *logoframe = NULL;
    gchar *greeting;
    gint cols, rows;
    struct stat statbuf;

    if(*GdmGtkRC)
	gtk_rc_parse (GdmGtkRC);

    rootwin = gdk_window_foreign_new (GDK_ROOT_WINDOW ());

    gdm_init_root_window_overlay ();

    login = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_ref (login);
    gtk_object_set_data_full (GTK_OBJECT (login), "login", login,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_window_set_title (GTK_WINDOW (login), "GDM Login");

    frame1 = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame1), GTK_SHADOW_OUT);
    gtk_container_border_width (GTK_CONTAINER (frame1), 0);
    gtk_container_add (GTK_CONTAINER (login), frame1);
    gtk_object_set_data_full (GTK_OBJECT (login), "frame1", frame1,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_ref (GTK_WIDGET (frame1));
    gtk_widget_show (GTK_WIDGET (frame1));

    frame2 = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame2), GTK_SHADOW_IN);
    gtk_container_border_width (GTK_CONTAINER (frame2), 2);
    gtk_container_add (GTK_CONTAINER (frame1), frame2);
    gtk_object_set_data_full (GTK_OBJECT (login), "frame2", frame2,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_ref (GTK_WIDGET (frame2));
    gtk_widget_show (GTK_WIDGET (frame2));

    mbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_ref (mbox);
    gtk_object_set_data_full (GTK_OBJECT (login), "mbox", mbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (mbox);
    gtk_container_add (GTK_CONTAINER (frame2), mbox);

    handle = create_handle ();
    gtk_box_pack_start (GTK_BOX (mbox), handle, FALSE, FALSE, 0);

    menubar = gtk_menu_bar_new();
    gtk_widget_ref (GTK_WIDGET (menubar));
    gtk_box_pack_start (GTK_BOX (mbox), menubar, FALSE, FALSE, 0);

    menu = gtk_menu_new();
    gdm_login_session_init (menu);
    sessmenu = gtk_menu_item_new_with_label (_("Session"));
    gtk_menu_bar_append (GTK_MENU_BAR(menubar), sessmenu);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (sessmenu), menu);
    gtk_widget_show (GTK_WIDGET (sessmenu));

    menu = gtk_menu_new();
    gdm_login_language_init (menu);
    langmenu = gtk_menu_item_new_with_label (_("Language"));
    gtk_menu_bar_append (GTK_MENU_BAR (menubar), langmenu);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (langmenu), menu);
    gtk_widget_show (GTK_WIDGET (langmenu));

    if (GdmSystemMenu) {
	menu = gtk_menu_new();
	item = gtk_menu_item_new_with_label (_("Reboot..."));
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (gdm_login_reboot_handler), 
			    NULL);
	gtk_widget_show (GTK_WIDGET (item));
	
	item = gtk_menu_item_new_with_label (_("Halt..."));
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			   GTK_SIGNAL_FUNC (gdm_login_halt_handler), 
			    NULL);
	gtk_widget_show (GTK_WIDGET (item));
	
	item = gtk_menu_item_new_with_label (_("System"));
	gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
	gtk_widget_show (GTK_WIDGET (item));
    }

    if (GdmBrowser)
	rows = 2;
    else
	rows = 1;

    if (GdmLogo && ! stat (GdmLogo, &statbuf))
	cols = 2;
    else 
	cols = 1;

    table = gtk_table_new (rows, cols, FALSE);
    gtk_widget_ref (table);
    gtk_object_set_data_full (GTK_OBJECT (login), "table", table,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (table);
    gtk_box_pack_start (GTK_BOX (mbox), table, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (table), 10);
    gtk_table_set_row_spacings (GTK_TABLE (table), 10);
    gtk_table_set_col_spacings (GTK_TABLE (table), 10);

    if (GdmBrowser) {
	GtkStyle *style;
	GdkColor  bbg = { 0, 0xFFFF, 0xFFFF, 0xFFFF };
	GtkWidget *bframe;
	GtkWidget *scrollbar;

	/* Find background style for browser */
	style = gtk_style_copy (login->style);
	style->bg[GTK_STATE_NORMAL] = bbg;
	gtk_widget_push_style (style);
	
	/* Icon list */
	if (maxwidth < GdmIconMaxWidth/2)
	    maxwidth = (gint) GdmIconMaxWidth/2;
	
	browser = GNOME_ICON_LIST (gnome_icon_list_new (maxwidth+20, NULL, FALSE));
	gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
	gnome_icon_list_set_separators (GNOME_ICON_LIST (browser), " /-_.");
	gnome_icon_list_set_row_spacing (GNOME_ICON_LIST (browser), 2);
	gnome_icon_list_set_col_spacing (GNOME_ICON_LIST (browser), 2);
	gnome_icon_list_set_icon_border (GNOME_ICON_LIST (browser), 2);
	gnome_icon_list_set_text_spacing (GNOME_ICON_LIST (browser), 2);
	gnome_icon_list_set_selection_mode (GNOME_ICON_LIST (browser), GTK_SELECTION_SINGLE);
	gtk_signal_connect (GTK_OBJECT (browser), "select_icon",
			    GTK_SIGNAL_FUNC (gdm_login_browser_select), NULL);
	gtk_signal_connect (GTK_OBJECT (browser), "unselect_icon",
			    GTK_SIGNAL_FUNC (gdm_login_browser_unselect), NULL);
	gtk_widget_pop_style();
	
	/* Browser 3D frame */
	bframe = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (bframe), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER(bframe), GTK_WIDGET (browser));
	
	/* Browser scroll bar */
	scrollbar = gtk_vscrollbar_new (browser->adj);
	
	/* Box containing all browser functionality */
	bbox = gtk_hbox_new (0, 0);
	gtk_box_pack_start (GTK_BOX (bbox), GTK_WIDGET (bframe), 1, 1, 0);
	gtk_box_pack_start (GTK_BOX (bbox), GTK_WIDGET (scrollbar), 0, 0, 0);
	gtk_widget_show_all (GTK_WIDGET (bbox));

	/* FIXME */
	gtk_widget_set_usize (GTK_WIDGET (bbox),
			      (gint) gdk_screen_width () * 0.5,
			      (gint) gdk_screen_height () * 0.25);
    }

    if (GdmLogo && !access (GdmLogo, R_OK)) {
	GtkWidget *logo;

	logoframe = gtk_frame_new (NULL);
	gtk_widget_ref (logoframe);
	gtk_object_set_data_full (GTK_OBJECT (login), "logoframe", logoframe,
				  (GtkDestroyNotify) gtk_widget_unref);
	gtk_widget_show (logoframe);
	gtk_frame_set_shadow_type (GTK_FRAME (logoframe), GTK_SHADOW_IN);

	logo = gnome_pixmap_new_from_file (GdmLogo);
	gtk_container_add (GTK_CONTAINER (logoframe), logo);
	gtk_widget_show (GTK_WIDGET (logo));
    }

    stack = gtk_table_new (6, 1, FALSE);
    gtk_widget_ref (stack);
    gtk_object_set_data_full (GTK_OBJECT (login), "stack", stack,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (stack);

    greeting = gdm_parse_enriched_string (GdmWelcome);    
    welcome = gtk_label_new (greeting);
    gtk_widget_set_name (welcome, "Welcome");
    g_free (greeting);
    gtk_widget_ref (welcome);
    gtk_object_set_data_full (GTK_OBJECT (login), "welcome", welcome,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (welcome);
    gtk_table_attach (GTK_TABLE (stack), welcome, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    if (GdmFont != NULL)
	    gdm_login_set_font (welcome, GdmFont);

    hline1 = gtk_hseparator_new ();
    gtk_widget_ref (hline1);
    gtk_object_set_data_full (GTK_OBJECT (login), "hline1", hline1,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hline1);
    gtk_table_attach (GTK_TABLE (stack), hline1, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 10);
    
    label = gtk_label_new (_("Login:"));
    gtk_widget_ref (label);
    gtk_object_set_data_full (GTK_OBJECT (login), "label", label,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (stack), label, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (label), 10, 5);
    
    entry = gtk_entry_new_with_max_length (32);
    gtk_widget_ref (entry);
    gtk_object_set_data_full (GTK_OBJECT (login), "entry", entry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_entry_set_text (GTK_ENTRY (entry), "");
    gtk_widget_show (entry);
    gtk_table_attach (GTK_TABLE (stack), entry, 0, 1, 3, 4,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 10, 0);
    gtk_signal_connect_object (GTK_OBJECT(entry), 
			       "key_press_event", 
			       GTK_SIGNAL_FUNC (gdm_login_entry_handler),
			       NULL);
    
    hline2 = gtk_hseparator_new ();
    gtk_widget_ref (hline2);
    gtk_object_set_data_full (GTK_OBJECT (login), "hline2", hline2,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hline2);
    gtk_table_attach (GTK_TABLE (stack), hline2, 0, 1, 4, 5,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 10);
        
    msg = gtk_label_new (_("Please enter your login"));
    first_message = TRUE;
    gtk_widget_set_name(msg, "Message");
    gtk_widget_ref (msg);
    gtk_object_set_data_full (GTK_OBJECT (login), "msg", msg,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (msg);
    gtk_table_attach (GTK_TABLE (stack), msg, 0, 1, 5, 6,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 10, 10);

    /* Put it nicely together */

    if (GdmBrowser && GdmLogo) {
	gtk_table_attach (GTK_TABLE (table), bbox, 0, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (table), logoframe, 0, 1, 1, 2,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
    }
    else if (GdmBrowser) {
	gtk_table_attach (GTK_TABLE (table), bbox, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (table), stack, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
    }
    else if (GdmLogo) {
	gtk_table_attach (GTK_TABLE (table), logoframe, 0, 1, 0, 1,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
    }
    else
	gtk_table_attach (GTK_TABLE (table), stack, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    gtk_widget_grab_focus (entry);	
    gtk_window_set_focus (GTK_WINDOW (login), entry);	
    gtk_window_set_policy (GTK_WINDOW (login), 1, 1, 1);
    
    if (GdmSetPosition) {
	    set_screen_pos (login, GdmPositionX, GdmPositionY);
    } else {
	    gtk_window_position (GTK_WINDOW (login), GTK_WIN_POS_CENTER);
    }

    gtk_widget_show_all (GTK_WIDGET (login));
}


static gint 
gdm_login_sort_func (gpointer d1, gpointer d2)
{
    GdmLoginUser *a = d1;
    GdmLoginUser *b = d2;

    if (!d1 || !d2)
	return (0);

    return (strcmp (a->login, b->login));
}


static GdmLoginUser * 
gdm_login_user_alloc (gchar *logname, uid_t uid, gchar *homedir)
{
    GdmLoginUser *user;
    gboolean fileok;
    gchar *gnomedir = NULL;
    GdkImlibImage *img = NULL;

    user = g_new0 (GdmLoginUser, 1);

    if (!user)
	return (NULL);

    user->uid = uid;
    user->login = g_strdup (logname);
    user->homedir = g_strdup (homedir);

    gnomedir = g_strconcat (homedir, "/.gnome", NULL);

    fileok = gdm_file_check ("gdm_login_user_alloc", uid, gnomedir, "photo", 
			     FALSE, GdmUserMaxFile, GdmRelaxPerms);
    
    if (fileok) {
	gchar *filename;
	
	filename = g_strconcat (gnomedir, "/photo", NULL);
	img = gdk_imlib_load_image (filename);
	g_free (filename);
    }
    else {
	gchar *filename;
	
	filename = g_strconcat (GdmGlobalFaceDir, "/", logname, NULL);
	
	if (access (filename, R_OK) == 0)
	    img = gdk_imlib_load_image (filename);
	
	g_free (filename);
    }
    
    g_free (gnomedir);
    
    if(img) {
	gint w, h;
	
	w = img->rgb_width;
	h = img->rgb_height;
	
	if (w>h && w > GdmIconMaxWidth) {
	    h = h * ((gfloat) GdmIconMaxWidth/w);
	    w = GdmIconMaxWidth;
	} 
	else if (h>GdmIconMaxHeight) {
	    w = w * ((gfloat) GdmIconMaxHeight/h);
	    h = GdmIconMaxHeight;
	}
	
	maxwidth = MAX (maxwidth, w);
	user->picture = gdk_imlib_clone_scaled_image (img, w, h);
	gdk_imlib_destroy_image (img);
    }
    else
	user->picture=defface;

    return (user);
}


static gint
gdm_login_check_exclude (struct passwd *pwent)
{
    const char * const lockout_passes[] = { "*", "!!", NULL };
    GSList *list = exclude;
    gint i;

    for (i=0 ; lockout_passes[i] ; i++) 
	if(strcmp (lockout_passes[i], pwent->pw_passwd) == 0)
	    return (TRUE);
 
     while (list && list->data) {
	 if (g_strcasecmp (pwent->pw_name, (gchar *) list->data) == 0)
	     return (TRUE);

	 list = list->next;
     }

     return (FALSE);
}


static gint
gdm_login_check_shell (gchar *usersh)
{
    gint found = 0;
    gchar *csh;

    setusershell ();

    while ((csh = getusershell ()) != NULL)
	if (! strcmp (csh, usersh))
	    found = 1;

    endusershell ();

    return (found);
}


static void 
gdm_login_users_init (void)
{
    GdmLoginUser *user;
    struct passwd *pwent;

    if (access (GdmDefaultFace, R_OK)) {
	syslog (LOG_WARNING, _("Can't open DefaultImage: %s. Suspending face browser!"), GdmDefaultFace);
	GdmBrowser = FALSE;
	return;
    }
    else 
	defface = gdk_imlib_load_image (GdmDefaultFace);

    if (GdmExclude) {
        gchar *s = strtok (GdmExclude, ",");
        exclude = g_slist_append (exclude, g_strdup (s));

        while ((s = strtok (NULL, ","))) 
	    exclude = g_slist_append (exclude, g_strdup (s));
    }

    pwent = getpwent();
	
    while (pwent != NULL) {
	
	if (pwent->pw_shell && 
	    gdm_login_check_shell (pwent->pw_shell) &&
	    !gdm_login_check_exclude (pwent)) {

	    user = gdm_login_user_alloc(pwent->pw_name,
					pwent->pw_uid,
					pwent->pw_dir);

	    if ((user) &&
		(! g_list_find_custom (users, user, (GCompareFunc) gdm_login_sort_func)))
		users = g_list_insert_sorted(users, user,
					     (GCompareFunc) gdm_login_sort_func);
	}
	
	pwent = getpwent();
    }    
}


int 
main (int argc, char *argv[])
{
    gchar **fixedargv;
    gint fixedargc, i;
    struct sigaction hup;
    struct sigaction chld;
    sigset_t mask;
    GIOChannel *ctrlch;

    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog ("gdmlogin", LOG_PID, LOG_DAEMON);

    fixedargc = argc + 1;
    fixedargv = g_new0 (gchar *, fixedargc);

    for (i=0; i < argc; i++)
	fixedargv[i] = argv[i];
    
    fixedargv[fixedargc-1] = "--disable-sound";
    gnome_init ("gdmlogin", VERSION, fixedargc, fixedargv);
    g_free (fixedargv);

    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    gnome_preferences_set_dialog_position (GTK_WIN_POS_CENTER);
    
    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    gdm_login_parse_config();

    if (GdmBrowser)
	gdm_login_users_init ();

    gdm_login_gui_init();

    if (GdmBrowser)
	gdm_login_browser_update();

    hup.sa_handler = gdm_login_done;
    hup.sa_flags = 0;
    sigemptyset(&hup.sa_mask);
    sigaddset (&hup.sa_mask, SIGCHLD);

    if (sigaction (SIGHUP, &hup, NULL) < 0) 
        gdm_login_abort (_("main: Error setting up HUP signal handler"));

    if (sigaction (SIGINT, &hup, NULL) < 0) 
        gdm_login_abort (_("main: Error setting up INT signal handler"));

    if (sigaction (SIGTERM, &hup, NULL) < 0) 
        gdm_login_abort (_("main: Error setting up TERM signal handler"));

    chld.sa_handler = gdm_greeter_chld;
    chld.sa_flags = SA_RESTART;
    sigemptyset(&chld.sa_mask);
    sigaddset (&chld.sa_mask, SIGCHLD);

    if (sigaction (SIGCHLD, &chld, NULL) < 0) 
        gdm_login_abort (_("main: Error setting up CHLD signal handler"));

    sigfillset (&mask);
    sigdelset (&mask, SIGTERM);
    sigdelset (&mask, SIGHUP);
    sigdelset (&mask, SIGINT);
    sigdelset (&mask, SIGCHLD);
    
    if (sigprocmask (SIG_SETMASK, &mask, NULL) == -1) 
	gdm_login_abort (_("Could not set signal mask!"));

    /* Launch a background program if one exists */
    if (GdmBackgroundProg != NULL &&
	GdmBackgroundProg[0] != '\0') {
	    backgroundpid = fork ();

	    if (backgroundpid == -1) {
		    /*ingore errors, this is irrelevant */
		    backgroundpid = 0;
	    } else if (backgroundpid == 0) {
		    char **argv;
		    argv = g_strsplit (GdmBackgroundProg, " ", MAX_ARGS);
		    execv (argv[0], argv);
		    /*ingore errors, this is irrelevant */
		    _exit (0);
	    }
    }

    ctrlch = g_io_channel_unix_new (STDIN_FILENO);
    g_io_channel_init (ctrlch);
    g_io_add_watch (ctrlch, 
		    G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		    (GIOFunc) gdm_login_ctrl_handler,
		    NULL);
    g_io_channel_unref (ctrlch);

    gtk_main();

    kill_background ();
    exit(EXIT_SUCCESS);
}

/* EOF */
