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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
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
#ifdef HAVE_LIBXINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "gdmlogin.h"
#include "gdm.h"
#include "gdmwm.h"
#include "gdmlanguages.h"
#include "misc.h"

static const gchar RCSid[]="$Id$";

/* Some strings that are in other files that we may want to
 * translate.  This is not actually used anywhere, it's just
 * to have gettext know about these strings. */
const char *session_titles[] = {
	N_("AnotherLevel"),
	N_("Default"),
	N_("Failsafe"),
	N_("Gnome"),
	N_("KDE"),
	N_("XSession"),
	N_("Gnome Chooser"),
	N_("Last")
};

#define LAST_SESSION "Last"
#define LAST_LANGUAGE "Last"
#define SESSION_NAME "SessionName"

static gboolean GdmAllowRoot;
static gboolean GdmAllowRemoteRoot;
static gboolean GdmBrowser;
static gboolean GdmDebug;
static gint  GdmIconMaxHeight;
static gint  GdmIconMaxWidth;
static gboolean GdmQuiver;
static gboolean GdmSystemMenu;
static gboolean GdmConfigAvailable;
static gint GdmXineramaScreen;
static gchar *GdmLogo;
static gchar *GdmWelcome;
static gchar *GdmBackgroundProg;
static gchar *GdmBackgroundImage;
static gchar *GdmBackgroundColor;
static gboolean GdmBackgroundScaleToFit;
static gboolean GdmBackgroundRemoteOnlyColor;
static int GdmBackgroundType;
enum {
	GDM_BACKGROUND_NONE = 0,
	GDM_BACKGROUND_IMAGE = 1,
	GDM_BACKGROUND_COLOR = 2
};
static gchar *GdmFont;
static gchar *GdmGtkRC;
static gchar *GdmIcon;
static gchar *GdmSessionDir;
static gchar *GdmLocaleFile;
static gchar *GdmDefaultLocale;
static gchar *GdmExclude;
static gchar *GdmGlobalFaceDir;
static gchar *GdmDefaultFace;
static gboolean GdmTimedLoginEnable;
static gchar *GdmTimedLogin;
static gint GdmTimedLoginDelay;
static gboolean GdmLockPosition;
static gboolean GdmSetPosition;
static gint GdmPositionX;
static gint GdmPositionY;
static gboolean GdmTitleBar;

static GtkWidget *login;
static GtkWidget *label;
static GtkWidget *clock_label = NULL;
static GtkWidget *entry;
static GtkWidget *msg;
static gboolean require_quarter = FALSE;
static GtkWidget *icon_win = NULL;
static GtkWidget *sessmenu;
static GtkWidget *langmenu;
static GdkRectangle *allscreens;
static int screens = 0;
static GtkTooltips *tooltips;

static gboolean login_is_local = FALSE;

GdkRectangle screen; /* this is an extern since it's used in gdmwm as well */

static GnomeIconList *browser;
static GdkImlibImage *defface;

/* Eew. Loads of global vars. It's hard to be event controlled while maintaining state */
static GSList *sessions = NULL;
static GSList *languages = NULL;
static GList *users = NULL;
static gint number_of_users = 0;

static gchar *defsess = NULL;
static const gchar *cursess = NULL;
static const gchar *curlang = NULL;
static gchar *curuser = NULL;
static gchar *session = NULL;
static gchar *language = NULL;
static gint curdelay = 0;

/* this is true if the prompt is for a login name */
static gboolean login_entry = FALSE;

static gboolean savesess = FALSE;
static gboolean savelang = FALSE;
static gint maxwidth = 0;
static gint maxheight = 0;

static pid_t backgroundpid = 0;

static guint timed_handler_id = 0;

/* This is true if session dir doesn't exist or is whacked out
 * in some way or another */
static gboolean session_dir_whacked_out = FALSE;

/*
 * Timed Login: Timer
 */

static gboolean
gdm_timer (gpointer data)
{
	curdelay --;
	if ( curdelay <= 0 ) {
		login_entry = FALSE; /* no matter where we are,
					this is no longer a login_entry */
		/* timed interruption */
		g_print ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
	} else if ((curdelay % 5) == 0) {
		gchar *autologin_msg = 
			g_strdup_printf (_("User %s will login in %d seconds"),
					 GdmTimedLogin, curdelay);
		gtk_label_set (GTK_LABEL (msg), autologin_msg);
		gtk_widget_show (GTK_WIDGET (msg));
		g_free (autologin_msg);
	}
	return TRUE;
}

/*
 * Timed Login: On GTK events, increase delay to
 * at least 30 seconds. Or the TimedLoginDelay,
 * whichever is higher
 */

static gboolean
gdm_timer_up_delay (GtkObject *object,
		    guint signal_id,
		    guint n_params,
		    GtkArg *params,
		    gpointer data)
{
	if (curdelay < 30)
		curdelay = 30;
	if (curdelay < GdmTimedLoginDelay)
		curdelay = GdmTimedLoginDelay;
	return TRUE;
}      

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

static void
setup_cursor (GdkCursorType type)
{
	GdkCursor *cursor = gdk_cursor_new (type);
	gdk_window_set_cursor (GDK_ROOT_PARENT (), cursor);
	gdk_cursor_destroy (cursor);
}

static void
gdm_greeter_chld (int sig)
{
	if (backgroundpid != 0 &&
	    waitpid (backgroundpid, NULL, WNOHANG) > 0) {
		backgroundpid = 0;
	}
}

static void
kill_thingies (void)
{
	pid_t pid = backgroundpid;

	backgroundpid = 0;
	if (pid != 0) {
		kill (pid, SIGTERM);
	}
}

static void
gdm_login_done (int sig)
{
    kill_thingies ();
    _exit (DISPLAY_SUCCESS);
}

static void
set_screen_pos (GtkWidget *widget, int x, int y)
{
	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (widget));

	if (x < screen.x)
		x = screen.x;
	if (y < screen.y)
		y = screen.y;
	if (x > screen.x + screen.width - widget->allocation.width)
		x = screen.x + screen.width - widget->allocation.width;
	if (y > screen.y + screen.height - widget->allocation.height)
		y = screen.y + screen.height - widget->allocation.height;

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

	return TRUE;
}

static gboolean
gdm_login_icon_pressed (GtkWidget *widget, GdkEventButton *event)
{
    CursorOffset *p;
    GdkCursor *fleur_cursor;

    if (event->type == GDK_2BUTTON_PRESS) {
	    gdm_login_icon_released (widget);
	    gtk_widget_destroy (GTK_WIDGET (icon_win));
	    icon_win = NULL;
	    gtk_widget_show_now (login);
	    gdk_window_raise (login->window);
	    gtk_widget_grab_focus (entry);	
	    gtk_window_set_focus (GTK_WINDOW (login), entry);	
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
    gdk_pointer_grab (widget->window, TRUE,
		      GDK_BUTTON_RELEASE_MASK |
		      GDK_BUTTON_MOTION_MASK |
		      GDK_POINTER_MOTION_HINT_MASK,
		      NULL,
		      fleur_cursor,
		      GDK_CURRENT_TIME);
    gdk_cursor_destroy (fleur_cursor);
    gdk_flush ();

    return TRUE;
}

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
	if ( ! within_rect (&screen, x, y)) {
		int i;
		/* If not within screen boundaries,
		 * maybe we want to switch xinerama
		 * screen */
		for (i = 0; i < screens; i++) {
			if (within_rect (&allscreens[i], x, y)) {
				GdmXineramaScreen = i;
				screen = allscreens[i];
				break;
			}
		}
	}
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

	gdk_window_get_pointer (GDK_ROOT_PARENT (), &xp, &yp, &mask);

	set_screen_to_pos (xp, yp);

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
    icon_win = gtk_window_new (GTK_WINDOW_POPUP);

    gtk_widget_set_events (icon_win, 
			   gtk_widget_get_events (GTK_WIDGET (icon_win)) | 
			   GDK_BUTTON_PRESS_MASK |
			   GDK_BUTTON_MOTION_MASK |
			   GDK_POINTER_MOTION_HINT_MASK);

    gtk_widget_realize (GTK_WIDGET (icon_win));

    fixed = gtk_fixed_new();
    gtk_container_add (GTK_CONTAINER (icon_win), fixed);
    gtk_widget_show (fixed);

    icon = gnome_pixmap_new_from_file (GdmIcon);
    if (icon != NULL) {
	    gdk_window_get_size ((GdkWindow *) GNOME_PIXMAP (icon)->pixmap,
				 &iw, &ih);
    } else {
	    /* sanity fallback */
	    icon = gtk_event_box_new ();
	    gtk_widget_set_usize (icon, 64, 64);
	    iw = ih = 64;
    }
    gtk_tooltips_set_tip (tooltips, GTK_WIDGET (icon_win),
			  _("Doubleclick here to un-iconify the login "
			    "window, so that you may log in."),
			  NULL);
    
    gtk_fixed_put(GTK_FIXED (fixed), GTK_WIDGET (icon), 0, 0);
    gtk_widget_show(GTK_WIDGET (icon));

    gtk_signal_connect (GTK_OBJECT (icon_win), "button_press_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_pressed),NULL);
    gtk_signal_connect (GTK_OBJECT (icon_win), "button_release_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_released),NULL);
    gtk_signal_connect (GTK_OBJECT (icon_win), "motion_notify_event",
			GTK_SIGNAL_FUNC (gdm_login_icon_motion),NULL);

    gtk_widget_show (GTK_WIDGET (icon_win));

    rw = screen.width;
    rh = screen.height;

    set_screen_pos (GTK_WIDGET (icon_win), rw-iw, rh-ih);
}


static void
gdm_login_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (!format) {
	kill_thingies ();
	exit (DISPLAY_ABORT);
    }

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, s);
    closelog();

    kill_thingies ();
    exit (DISPLAY_ABORT);
}


/* I *really* need to rewrite this crap */
static gchar *
gdm_parse_enriched_string (const gchar *s)
{
    gchar cmd, *buffer;
    gchar hostbuf[256];
    gchar *hostname, *display;
    struct utsname name;
    GString *str;

    if (s == NULL)
	return(NULL);

    display = g_strdup (g_getenv ("DISPLAY"));

    if (display == NULL)
	return(NULL);

    gethostname (hostbuf, 255);
    hostname = g_strdup (hostbuf);
    
    if (hostname == NULL) 
	hostname = g_strdup ("Gnome");

    uname (&name);

    /* HAAAAAAAAAAAAAAAAAAAAAAAACK!, do not translate the next line!,
     * Since this is the default string we might as well use the gettext
     * translation as we will likely have better translations there.
     * Yes ugly as fuck, but oh well, unfortunately two standard defaults
     * are in circulation */
    if (strcmp (s, "Welcome to %h") == 0) {
	g_free (display);
	buffer = g_strdup_printf (_("Welcome to %s"), hostname);
	g_free (hostname);
	return buffer;
    } else if (strcmp (s, "Welcome to %n") == 0) {
	g_free (display);
	g_free (hostname);
	buffer = g_strdup_printf (_("Welcome to %s"), name.nodename);
	return buffer;
    }

    if (strlen (s) > 1023) {
	syslog (LOG_ERR, _("gdm_parse_enriched_string: String too long!"));
	g_free (display);
	buffer = g_strdup_printf (_("Welcome to %s"), hostname);
	g_free (hostname);
	return buffer;
    }

    str = g_string_new (NULL);

    while (s[0] != '\0') {

	if (s[0] == '%' && s[1] != 0) {
		cmd = s[1];
		s++;

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

static void
gdm_center_window (GtkWindow *cw) 
{
	GtkRequisition req;
        gint x, y;

	gtk_widget_size_request (GTK_WIDGET (cw), &req);

	x = screen.x + (screen.width - req.width)/2;
	y = screen.y + (screen.height - req.height)/2;	

	if (x < screen.x)
		x = screen.x;
	if (y < screen.y)
		y = screen.y;

 	gtk_widget_set_uposition (GTK_WIDGET (cw), x, y);	
}

static gboolean
gdm_login_query (const gchar *msg)
{
	int ret;
	static GtkWidget *req = NULL;

	if (req != NULL)
		gtk_widget_destroy (req);

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	req = gnome_message_box_new (msg,
				     GNOME_MESSAGE_BOX_QUESTION,
				     GNOME_STOCK_BUTTON_YES,
				     GNOME_STOCK_BUTTON_NO,
				     NULL);
	gtk_signal_connect (GTK_OBJECT (req), "destroy",
			    GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			    &req);

	gtk_window_set_modal (GTK_WINDOW (req), TRUE);
	gdm_center_window (GTK_WINDOW (req));

	gdm_wm_no_login_focus_push ();
	ret = gnome_dialog_run (GNOME_DIALOG (req));
	gdm_wm_no_login_focus_pop ();

	if (ret == 0)
		return TRUE;
	else /* this includes -1 which is "destroyed" */
		return FALSE;
}

static pid_t
gdm_run_command (const char *command)
{
	pid_t pid;
	char **argv;

	pid = fork ();

	if (pid == -1) {
		/* We can't fork, that means we're pretty much up shit creek
		 * without a paddle. */
		gnome_error_dialog (_("Could not fork a new procss!\n\n"
				      "You likely won't be able to log "
				      "in either."));
	} else if (pid == 0) {
		int i;

		/* close everything */

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close(i);

		/* No error checking here - if it's messed the best
		 * response is to ignore & try to continue */
		open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open("/dev/null", O_RDWR); /* open stderr - fd 2 */

		argv = g_strsplit (command, " ", MAX_ARGS);	
		execv (argv[0], argv);
		/*ingore errors, this is irrelevant */
		_exit (0);
	}

	return pid;
}

static void
gdm_run_gdmconfig (GtkWidget *w, gpointer data)
{
	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	/* configure interruption */
	login_entry = FALSE; /* no matter where we are,
				this is no longer a login_entry */
	/* timed interruption */
	g_print ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CONFIGURE);
}

static gboolean
gdm_login_reboot_handler (void)
{
    if (gdm_login_query (_("Are you sure you want to reboot the machine?"))) {
	closelog();

        kill_thingies ();
	exit (DISPLAY_REBOOT);
    }

    return (TRUE);
}


static gboolean
gdm_login_halt_handler (void)
{
    if (gdm_login_query (_("Are you sure you want to halt the machine?"))) {
	closelog();

        kill_thingies ();
	exit (DISPLAY_HALT);
    }

    return (TRUE);
}


static void 
gdm_login_parse_config (void)
{
    struct stat unused;
	
    if (stat (GDM_CONFIG_FILE, &unused) == -1)
	gdm_login_abort (_("gdm_login_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmAllowRoot = gnome_config_get_bool (GDM_KEY_ALLOWROOT);
    GdmAllowRemoteRoot = gnome_config_get_bool (GDM_KEY_ALLOWREMOTEROOT);
    GdmBrowser = gnome_config_get_bool (GDM_KEY_BROWSER);
    GdmLogo = gnome_config_get_string (GDM_KEY_LOGO);
    GdmFont = gnome_config_get_string (GDM_KEY_FONT);
    GdmIcon = gnome_config_get_string (GDM_KEY_ICON);
    GdmQuiver = gnome_config_get_bool (GDM_KEY_QUIVER);
    GdmSystemMenu = gnome_config_get_bool (GDM_KEY_SYSMENU);
    GdmConfigAvailable = gnome_config_get_bool (GDM_KEY_CONFIG_AVAILABLE);
    GdmTitleBar = gnome_config_get_bool (GDM_KEY_TITLE_BAR);
    GdmLocaleFile = gnome_config_get_string (GDM_KEY_LOCFILE);
    GdmDefaultLocale = gnome_config_get_string (GDM_KEY_LOCALE);
    GdmSessionDir = gnome_config_get_string (GDM_KEY_SESSDIR);
    GdmWelcome = gnome_config_get_translated_string (GDM_KEY_WELCOME);
    GdmBackgroundProg = gnome_config_get_string (GDM_KEY_BACKGROUNDPROG);
    GdmBackgroundImage = gnome_config_get_string (GDM_KEY_BACKGROUNDIMAGE);
    GdmBackgroundColor = gnome_config_get_string (GDM_KEY_BACKGROUNDCOLOR);
    GdmBackgroundType = gnome_config_get_int (GDM_KEY_BACKGROUNDTYPE);
    GdmBackgroundScaleToFit = gnome_config_get_bool (GDM_KEY_BACKGROUNDSCALETOFIT);
    GdmBackgroundRemoteOnlyColor = gnome_config_get_bool (GDM_KEY_BACKGROUNDREMOTEONLYCOLOR);
    GdmGtkRC = gnome_config_get_string (GDM_KEY_GTKRC);
    GdmExclude = gnome_config_get_string (GDM_KEY_EXCLUDE);
    GdmGlobalFaceDir = gnome_config_get_string (GDM_KEY_FACEDIR);
    GdmDefaultFace = gnome_config_get_string (GDM_KEY_FACE);
    GdmDebug = gnome_config_get_bool (GDM_KEY_DEBUG);
    GdmIconMaxWidth = gnome_config_get_int (GDM_KEY_ICONWIDTH);
    GdmIconMaxHeight = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
    GdmXineramaScreen = gnome_config_get_int (GDM_KEY_XINERAMASCREEN);
    GdmLockPosition = gnome_config_get_bool (GDM_KEY_LOCK_POSITION);
    GdmSetPosition = gnome_config_get_bool (GDM_KEY_SET_POSITION);
    GdmPositionX = gnome_config_get_int (GDM_KEY_POSITIONX);
    GdmPositionY = gnome_config_get_int (GDM_KEY_POSITIONY);

    GdmTimedLoginEnable = gnome_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE);
    if (GdmTimedLoginEnable) {
	    GdmTimedLogin = gnome_config_get_string (GDM_KEY_TIMED_LOGIN);
	    GdmTimedLoginDelay =
		    gnome_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);
	    if (GdmTimedLoginDelay < 10) {
		    syslog (LOG_WARNING,
			    _("TimedLoginDelay was less then 10.  I'll just use 10."));
		    GdmTimedLoginDelay = 10;
	    }
    } else {
	    GdmTimedLogin = NULL;
	    GdmTimedLoginDelay = 10;
    }

    gnome_config_pop_prefix();

    if (GdmIconMaxWidth < 0) GdmIconMaxWidth = 128;
    if (GdmIconMaxHeight < 0) GdmIconMaxHeight = 128;
    if (GdmXineramaScreen < 0) GdmXineramaScreen = 0;

    /* Disable System menu on non-local displays */
    if (gdm_string_empty (g_getenv ("GDM_IS_LOCAL"))) {
	    GdmSystemMenu = FALSE;
	    GdmConfigAvailable = FALSE;
	    if (GdmBackgroundRemoteOnlyColor &&
		GdmBackgroundType == GDM_BACKGROUND_IMAGE)
		    GdmBackgroundType = GDM_BACKGROUND_COLOR;
	    if (GdmBackgroundRemoteOnlyColor &&
		! gdm_string_empty (GdmBackgroundProg)) {
		    g_free (GdmBackgroundProg);
		    GdmBackgroundProg = NULL;
	    }
	    login_is_local = FALSE;
    } else {
	    login_is_local = TRUE;
    }

    /* Disable timed login stuff if it's not ok for this display */
    if (gdm_string_empty (g_getenv ("GDM_TIMED_LOGIN_OK"))) {
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
    }
}


static gboolean 
gdm_login_list_lookup (GSList *l, const gchar *data)
{
    GSList *list = l;

    if (!list || !data)
	return(FALSE);

    while (list) {

	if (strcmp (list->data, data) == 0)
	    return (TRUE);
	
	list = list->next;
    }

    return (FALSE);
}

static const char *
translate_session (const char *name)
{
	/* eek */
	if (name == NULL)
		return "(null)";

	if (strcmp (name, GDM_SESSION_FAILSAFE_GNOME) == 0)
		return _("Failsafe Gnome");
	else if (strcmp (name, GDM_SESSION_FAILSAFE_XTERM) == 0)
		return _("Failsafe xterm");
	else
		return _(name);
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
	    if (cursess == NULL || strcmp (cursess, LAST_SESSION) == 0)
		    session = g_strdup (defsess);
	    else
		    session = g_strdup (cursess);

	    savesess = TRUE;
	    return;
    }

    /* If "Last" session is selected */
    if (cursess == NULL ||
	strcmp (cursess, LAST_SESSION) == 0) { 
	g_free (session);
	session = g_strdup (savedsess);

	/* Check if user's saved session exists on this box */
	if (!gdm_login_list_lookup (sessions, session)) {
	    gchar *msg;

	    g_free (session);
	    session = g_strdup (defsess);
	    msg = g_strdup_printf (_("Your preferred session type %s is not installed on this machine.\n" \
				     "Do you wish to make %s the default for future sessions?"),
				   translate_session (savedsess),
				   translate_session (defsess));	    
	    savesess = gdm_login_query (msg);
	    g_free (msg);
	}
    }
    /* One of the other available session types is selected */
    else { 
	g_free (session);
	session = g_strdup (cursess);

	/* User's saved session is not the chosen one */
	if (strcmp (savedsess, session) != 0) {
	    gchar *msg;

	    msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is " \
				     "%s.\nDo you wish to make %s the default for future sessions?"),
				   translate_session (cursess),
				   translate_session (savedsess),
				   translate_session (cursess));
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
    if (gdm_string_empty (savedlang)) {
	/* If "Last" is chosen use Default, which is the current language,
	 * or the GdmDefaultLocale if that's not set or is "C"
	 * else use current selection */
	g_free (language);
	if (curlang == NULL ||
	    strcmp (curlang, LAST_LANGUAGE) == 0) {
		char *lang = g_getenv ("LANG");
		if (lang == NULL ||
		    lang[0] == '\0' ||
		    strcasecmp_no_locale (lang, "C") == 0) {
			language = g_strdup (GdmDefaultLocale);
		} else {
			language = g_strdup (lang);
		}
	} else {
		language = g_strdup (curlang);
	}

	if (gdm_string_empty (language)) {
		g_free (language);
		language = g_strdup ("C");
	}

	savelang = TRUE;
	return;
    }

    /* If a different language is selected */
    if (curlang != NULL && strcmp (curlang, LAST_LANGUAGE) != 0) {
        g_free (language);
	language = g_strdup (curlang);

	/* User's saved language is not the chosen one */
	if (strcmp (savedlang, language) != 0) {
	    gchar *msg;
	    char *curname, *savedname;

	    curname = gdm_lang_name (NULL, curlang, FALSE, TRUE);
	    savedname = gdm_lang_name (NULL, savedlang, FALSE, TRUE);

	    msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is "
				     "%s.\nDo you wish to make %s the default for future sessions?"),
				   curname, savedname, curname);
	    g_free (curname);
	    g_free (savedname);

	    savelang = gdm_login_query (msg);
	    g_free (msg);
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
		width = screen.width;
	if (height == -1)
		height = screen.height;

	if (login == NULL ||
	    login->window == NULL) {
		dance_handler = 0;
		return FALSE;
	}

	xm = cos (2.31 * t1);
	ym = sin (1.03 * t2);

	t1 += 0.03 + (rand () % 10) / 500.0;
	t2 += 0.03 + (rand () % 10) / 500.0;

	x = screen.x + (width / 2) + (width / 5) * xm;
	y = screen.y + (height / 2) + (height / 5) * ym;

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
		setup_cursor (GDK_UMBRELLA);
		dance_handler = gtk_timeout_add (50, dance, NULL);
		old_lock = GdmLockPosition;
		GdmLockPosition = TRUE;
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
	} else if (dance_handler != 0 &&
		   /* do not translate */
		   strcmp (user, "Stop Dancing") == 0) {
		setup_cursor (GDK_LEFT_PTR);
		gtk_timeout_remove (dance_handler);
		dance_handler = 0;
		GdmLockPosition = old_lock;
		gdm_center_window (GTK_WINDOW (login));
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
				 /* do not translate */
	} else if (strcmp (user, "Gimme Random Cursor") == 0) {
		setup_cursor (((rand () >> 3) % (GDK_LAST_CURSOR/2)) * 2);
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

static gboolean
gdm_login_entry_handler (GtkWidget *widget, GdkEventKey *event)
{
    static gboolean first_return = TRUE;
    static gchar *login;

    if (!event)
	return(TRUE);

    switch (event->keyval) {

    case GDK_Return:
	gtk_widget_set_sensitive (entry, FALSE);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);

	login = gtk_entry_get_text (GTK_ENTRY (entry));

	/* If in timed login mode, and if this is the login
	 * entry.  Then an enter by itself is sort of like I want to
	 * log in as the timed user "damn it".  */
	if (gdm_string_empty (login) &&
	    timed_handler_id != 0 &&
	    login_entry) {
		login_entry = FALSE;
		/* timed interruption */
		g_print ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
		return TRUE;
	}

	/* Save login. I'm making the assumption that login is always
	 * the first thing entered. This might not be true for all PAM
	 * setups. Needs thinking! 
	 */

	if (login_entry) {
		g_free (curuser);
		curuser = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

		/* evilness */
		if (evil (curuser)) {
			g_free (curuser);
			curuser = NULL;
			gtk_widget_set_sensitive (entry, TRUE);
			gtk_widget_grab_focus (entry);	
			gtk_window_set_focus (GTK_WINDOW (login), entry);	
			return TRUE;
		}
	}

	/* somewhat ugly thing to clear the initial message */
	if (first_return) {
	       first_return = FALSE;
	       gtk_label_set (GTK_LABEL (msg), "");
	}

	login_entry = FALSE;
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

    cursess = gtk_object_get_data (GTK_OBJECT (widget), SESSION_NAME);

    s = g_strdup_printf (_("%s session selected"), translate_session (cursess));

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
    gboolean got_default_link = FALSE;

    cursess = LAST_SESSION;
    item = gtk_radio_menu_item_new_with_label (NULL, _(LAST_SESSION));
    gtk_object_set_data (GTK_OBJECT (item),
			 "SessionName",
			 LAST_SESSION);
    sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (gdm_login_session_handler),
			NULL);
    gtk_widget_show (GTK_WIDGET (item));
    gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
			  _("Log in using the session that you have used "
			    "last time you logged in"),
			  NULL);

    item = gtk_menu_item_new();
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    /* Check that session dir is readable */
    if (GdmSessionDir == NULL ||
	access (GdmSessionDir, R_OK|X_OK)) {
	syslog (LOG_ERR, _("gdm_login_session_init: Session script directory not found!"));
	session_dir_whacked_out = TRUE;
    }

    /* Read directory entries in session dir */
    if (GdmSessionDir == NULL)
	    sessdir = NULL;
    else
	    sessdir = opendir (GdmSessionDir);

    if (sessdir != NULL)
	    dent = readdir (sessdir);
    else
	    dent = NULL;

    while (dent != NULL) {
	gchar *s;

	/* Ignore backups and rpmsave files */
	if ((strstr (dent->d_name, "~")) ||
	    (strstr (dent->d_name, ".rpmsave")) ||
	    (strstr (dent->d_name, ".dpkg-old")) ||
	    (strstr (dent->d_name, ".deleted")) ||
	    (strstr (dent->d_name, ".desc")) /* description file */ ||
	    (strstr (dent->d_name, ".orig"))) {
	    dent = readdir (sessdir);
	    continue;
	}

	s = g_strconcat (GdmSessionDir, "/", dent->d_name, NULL);
	lstat (s, &statbuf);

	/* If default session link exists, find out what it points to */
	if (S_ISLNK (statbuf.st_mode) &&
	    strcasecmp_no_locale (dent->d_name, "default") == 0) {
	    gchar t[_POSIX_PATH_MAX];
	    
	    linklen = readlink (s, t, _POSIX_PATH_MAX);
	    t[linklen] = 0;
	    g_free (defsess);
	    defsess = g_strdup (t);

	    got_default_link = TRUE;
	}

	/* If session script is readable/executable add it to the list */
	if (S_ISREG (statbuf.st_mode)) {

	    if ((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
		(statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
		(statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) 
	    {
		item = gtk_radio_menu_item_new_with_label (sessgrp, _(dent->d_name));
		gtk_object_set_data_full (GTK_OBJECT (item),
					  "SessionName",
					  g_strdup (dent->d_name),
					  (GtkDestroyNotify) g_free);

		sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
		sessions = g_slist_append (sessions, dent->d_name);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (gdm_login_session_handler),
				    NULL);
		gtk_widget_show (GTK_WIDGET (item));

		/* if there is a session called Default, use that as default
		 * if no link has been made */
		if ( ! got_default_link &&
		    strcasecmp_no_locale (dent->d_name, "Default") == 0) {
			g_free (defsess);
			defsess = g_strdup (dent->d_name);
		}	

		if (strcasecmp_no_locale (dent->d_name, "Gnome") == 0) {
			/* Just in case there is no Default session and
			 * no default link, make Gnome the default */
			if (defsess == NULL)
				defsess = g_strdup (dent->d_name);
			
			/* FIXME: when we get descriptions in session files,
			 * take this out */
			gtk_tooltips_set_tip
				(tooltips, GTK_WIDGET (item),
				 _("This session will log you directly into "
				   "GNOME, into your current session."),
				 NULL);

			/* Add the chooser session, this one doesn't have a script
			 * really, it's a fake, it runs the Gnome script */
			/* For translators:  This is the login that lets users choose the
			 * specific gnome session they want to use */
			item = gtk_radio_menu_item_new_with_label (sessgrp, _("Gnome Chooser"));
			gtk_tooltips_set_tip
				(tooltips, GTK_WIDGET (item),
				 _("This session will log you into "
				   "GNOME and it will let you choose which "
				   "one of the GNOME sessions you want to "
				   "use."),
				 NULL);
			gtk_object_set_data (GTK_OBJECT (item),
					     "SessionName",
					     GDM_SESSION_GNOME_CHOOSER);

			sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
			sessions = g_slist_append (sessions, GDM_SESSION_GNOME_CHOOSER);
			gtk_menu_append (GTK_MENU (menu), item);
			gtk_signal_connect (GTK_OBJECT (item), "activate",
					    GTK_SIGNAL_FUNC (gdm_login_session_handler),
					    NULL);
			gtk_widget_show (GTK_WIDGET (item));
		}
			
	    }
	    else 
		syslog (LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
			GdmSessionDir, dent->d_name);

	}

	dent = readdir (sessdir);
	g_free (s);
    }

    if (sessdir != NULL)
	    closedir (sessdir);

    if (sessions == NULL) {
	    syslog (LOG_WARNING, _("Yaikes, nothing found in the session directory."));
	    session_dir_whacked_out = TRUE;

	    defsess = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
    }

    /* For translators:  This is the failsafe login when the user
     * can't login otherwise */
    item = gtk_radio_menu_item_new_with_label (sessgrp,
					       _("Failsafe Gnome"));
    gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
		          _("This is a failsafe session that will log you "
			    "into GNOME.  No startup scripts will be read "
			    "and it is only to be used when you can't log "
			    "in otherwise.  GNOME will use the 'Default' "
			    "session."),
			  NULL);
    gtk_object_set_data (GTK_OBJECT (item),
			 "SessionName", GDM_SESSION_FAILSAFE_GNOME);

    sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    sessions = g_slist_append (sessions, GDM_SESSION_FAILSAFE_GNOME);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (gdm_login_session_handler),
			NULL);
    gtk_widget_show (GTK_WIDGET (item));

    /* For translators:  This is the failsafe login when the user
     * can't login otherwise */
    item = gtk_radio_menu_item_new_with_label (sessgrp,
					       _("Failsafe xterm"));
    gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
		          _("This is a failsafe session that will log you "
			    "into a terminal.  No startup scripts will be read "
			    "and it is only to be used when you can't log "
			    "in otherwise.  To exit the terminal, "
			    "type 'exit'."),
			  NULL);
    gtk_object_set_data (GTK_OBJECT (item),
			 "SessionName", GDM_SESSION_FAILSAFE_XTERM);

    sessgrp = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    sessions = g_slist_append (sessions, GDM_SESSION_FAILSAFE_XTERM);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (gdm_login_session_handler),
			NULL);
    gtk_widget_show (GTK_WIDGET (item));

    if (defsess == NULL) {
	    defsess = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	    syslog (LOG_WARNING, _("No default session link found. Using Failsafe GNOME.\n"));
    }

}


static void 
gdm_login_language_handler (GtkWidget *widget) 
{
    gchar *s;
    char *name;

    if (!widget)
	return;

    curlang = gtk_object_get_data (GTK_OBJECT (widget), "Language");
    name = gdm_lang_name (NULL, curlang, FALSE, TRUE);
    s = g_strdup_printf (_("%s language selected"), name);
    g_free (name);
    gtk_label_set (GTK_LABEL (msg), s);
    g_free (s);
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

    langlist = gdm_lang_read_locale_file (GdmLocaleFile);

    if (langlist == NULL)
	    return NULL;

    menu = gtk_menu_new ();

    curlang = LAST_LANGUAGE;

    item = gtk_radio_menu_item_new_with_label (NULL, _(LAST_LANGUAGE));
    languages = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_append (GTK_MENU(menu), item);
    gtk_signal_connect (GTK_OBJECT (item), "activate", 
			GTK_SIGNAL_FUNC (gdm_login_language_handler), 
			NULL);
    gtk_widget_show (GTK_WIDGET (item));
    gtk_object_set_data (GTK_OBJECT (item),
			 "Language",
			 LAST_LANGUAGE);
    gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
			  _("Log in using the language that you have used "
			    "last time you logged in"),
			  NULL);

    item = gtk_menu_item_new();
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new_with_label (gdm_lang_group1 ());
    ammenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (ammenu));
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    item = gtk_menu_item_new_with_label (gdm_lang_group2 ());
    nzmenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), nzmenu);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show(GTK_WIDGET (item));

    other_menu = item = gtk_menu_item_new_with_label (_("Other"));
    omenu = gtk_menu_new();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), omenu);
    gtk_menu_append (GTK_MENU (menu), item);
    gtk_widget_show (GTK_WIDGET (item));

    g1 = gdm_lang_group1 ();
    g2 = gdm_lang_group2 ();

    for (li = langlist; li != NULL; li = li->next) {
	    char *lang = li->data;
	    char *name;
	    char *group;
	    char *p;

	    li->data = NULL;

	    group = name = gdm_lang_name (NULL, lang, FALSE, FALSE);
	    if (name == NULL) {
		    g_free (lang);
		    continue;
	    }

	    p = strchr (name, '|');
	    if (p != NULL) {
		    *p = '\0';
		    name = p+1;
	    }

	    item = gtk_radio_menu_item_new_with_label (languages, name);
	    languages = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
	    gtk_object_set_data_full (GTK_OBJECT (item),
				      "Language",
				      g_strdup (lang),
				      (GtkDestroyNotify) g_free);

	    if (strcmp (group, g1) == 0) {
		    gtk_menu_append (GTK_MENU (ammenu), item);
	    } else if (strcmp (group, g2) == 0) {
		    gtk_menu_append (GTK_MENU (nzmenu), item);
	    } else {
		    gtk_menu_append (GTK_MENU (omenu), item);
		    has_other_locale = TRUE;
	    }

	    gtk_signal_connect (GTK_OBJECT (item), "activate", 
				GTK_SIGNAL_FUNC (gdm_login_language_handler), 
				NULL);
	    gtk_widget_show (GTK_WIDGET (item));

	    g_free (lang);
	    g_free (group);
    }
    if ( ! has_other_locale) 
	    gtk_widget_destroy (other_menu);

    g_list_free (langlist);

    return menu;
}

static void
toggle_sensitize (GtkWidget *w, gpointer data)
{
	gtk_widget_set_sensitive (GTK_WIDGET (data),
				  GTK_TOGGLE_BUTTON (w)->active);
}
static void
toggle_insensitize (GtkWidget *w, gpointer data)
{
	gtk_widget_set_sensitive (GTK_WIDGET (data),
				  ! GTK_TOGGLE_BUTTON (w)->active);
}

static gboolean
selector_delete_event (GtkWidget *dlg, GdkEvent *event, gpointer data)
{
	gnome_dialog_close (GNOME_DIALOG (dlg));
	return TRUE;
}

static void
clist_double_click_closes (GtkCList *clist,
			   int row,
			   int column,
			   GdkEvent *event,
			   gpointer data)
{
	if (event != NULL &&
	    event->type == GDK_2BUTTON_PRESS)
		gnome_dialog_close (GNOME_DIALOG (data));
}

static char *
get_gnome_session (const char *sess_string)
{
	GtkWidget *d;
	GtkWidget *clist, *sw;
	GtkWidget *hbox;
	GtkWidget *entry;
	GtkWidget *newcb;
	char *retval;
	char **sessions;
	char *selected;
	gboolean got_default;
	int i;

	/* the first one is the selected one, and it will also come
	 * again later, so we should just note it */
	sessions = g_strsplit (sess_string, "\n", 0);
	if (sessions != NULL &&
	    sessions[0] != NULL)
		selected = sessions[0];
	else
		selected = "Default";

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	d = gnome_dialog_new (_("Select GNOME session"),
			      GNOME_STOCK_BUTTON_OK,
			      NULL);
	gnome_dialog_close_hides (GNOME_DIALOG (d), TRUE);
	gtk_signal_connect (GTK_OBJECT (d), "delete_event",
			    GTK_SIGNAL_FUNC (selector_delete_event),
			    NULL);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox),
			    gtk_label_new (_("Select GNOME session")),
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox),
			    gtk_hseparator_new (),
			    FALSE, FALSE, 0);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox), sw,
			    TRUE, TRUE, 0);
	clist = gtk_clist_new (1);
	gtk_clist_set_column_auto_resize (GTK_CLIST (clist), 0, TRUE);
	gtk_container_add (GTK_CONTAINER (sw), clist);
	gtk_widget_set_usize (sw, 120, 180);

	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (clist_double_click_closes),
			    d);

	got_default = FALSE;
	if (sessions != NULL &&
	    sessions[0] != NULL) {
		for (i = 1; sessions[i] != NULL; i++) {
			int row;
			char *text[1];
			if (strcmp (sessions[i], "Default") == 0) {
				got_default = TRUE;
				/* default is nicely translated */
				/* Translators: default GNOME session */
				text[0] = _("Default");
			} else {
				text[0] = sessions[i];
			}
			row = gtk_clist_append (GTK_CLIST (clist),
						text);
			gtk_clist_set_row_data_full (GTK_CLIST (clist),
						     row,
						     g_strdup (sessions[i]),
						     (GtkDestroyNotify) g_free);
			if (strcmp (sessions[i], selected) == 0) {
				gtk_clist_select_row (GTK_CLIST (clist),
						      row, 0);
			}
		}
	}

	if ( ! got_default) {
		int row;
		char *text[1];
		/* default is nicely translated */
		/* Translators: default GNOME session */
		text[0] = _("Default");
		row = gtk_clist_append (GTK_CLIST (clist),
					text);
		gtk_clist_set_row_data_full (GTK_CLIST (clist),
					     row, g_strdup ("Default"),
					     (GtkDestroyNotify) g_free);
		if (strcmp ("Default", selected) == 0) {
			gtk_clist_select_row (GTK_CLIST (clist),
					      row, 0);
		}
	}

	g_strfreev (sessions);

	newcb = gtk_check_button_new_with_label (_("Create new session"));
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox), newcb,
			    FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox), hbox,
			    FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (hbox),
			    gtk_label_new (_("Name: ")),
			    FALSE, FALSE, 0);

	entry = gtk_entry_new ();
	gnome_dialog_editable_enters (GNOME_DIALOG (d), GTK_EDITABLE (entry));
	gtk_box_pack_start (GTK_BOX (hbox),
			    entry,
			    TRUE, TRUE, 0);

	gtk_widget_set_sensitive (hbox, FALSE);

	gtk_signal_connect (GTK_OBJECT (newcb), "toggled",
			    GTK_SIGNAL_FUNC (toggle_sensitize),
			    hbox);
	gtk_signal_connect (GTK_OBJECT (newcb), "toggled",
			    GTK_SIGNAL_FUNC (toggle_insensitize),
			    clist);

	gtk_window_set_modal (GTK_WINDOW (d), TRUE);

	gtk_widget_show_all (d);

	gdm_center_window (GTK_WINDOW (d));
	gdm_wm_no_login_focus_push ();
	gnome_dialog_run (GNOME_DIALOG (d));
	gdm_wm_no_login_focus_pop ();

	/* we've set the just_hide to TRUE, so we can still access the
	 * window */
	if (GTK_TOGGLE_BUTTON (newcb)->active) {
		retval = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	} else if (GTK_CLIST (clist)->selection != NULL) {
		int selected_row =
			GPOINTER_TO_INT (GTK_CLIST (clist)->selection->data);
		char *session = gtk_clist_get_row_data (GTK_CLIST (clist),
							selected_row);
		retval = g_strdup (session);
	} else {
		retval = g_strdup ("");
	}

	/* finally destroy window */
	gtk_widget_destroy (d);

	return retval;
}

static gboolean
gdm_login_ctrl_handler (GIOChannel *source, GIOCondition cond, gint fd)
{
    gchar buf[PIPE_SIZE];
    gint len;
    gint i, x, y;
    static gboolean replace_msg = TRUE;

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
    case GDM_SETLOGIN:
	/* somebody is trying to fool us this is the user that
	 * wants to log in, and well, we are the gullible kind */
        g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';
	g_free (curuser);
	curuser = g_strdup (buf);
	g_print ("%c\n", STX);
	break;
    case GDM_LOGIN:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	gtk_label_set (GTK_LABEL (label), buf);
	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_max_length (GTK_ENTRY (entry), 32);
	gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_window_set_focus (GTK_WINDOW (login), entry);	
	gtk_widget_show (entry);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);

	/* replace rapther then append next message string */
	replace_msg = TRUE;

	/* this is a login prompt */
	login_entry = TRUE;
	break;

    case GDM_PROMPT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	gtk_label_set (GTK_LABEL (label), buf);
	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_max_length (GTK_ENTRY (entry), 128);
	gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_window_set_focus (GTK_WINDOW (login), entry);	
	gtk_widget_show (entry);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);

	/* replace rapther then append next message string */
	replace_msg = TRUE;

	/* this is not a login prompt */
	login_entry = FALSE;
	break;

    case GDM_NOECHO:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	gtk_label_set (GTK_LABEL(label), buf);
	gtk_widget_show (GTK_WIDGET (label));
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_max_length (GTK_ENTRY (entry), 128);
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_widget_set_sensitive (entry, TRUE);
	gtk_widget_grab_focus (entry);	
	gtk_window_set_focus (GTK_WINDOW (login), entry);	
	gtk_widget_show (entry);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);

	/* replace rapther then append next message string */
	replace_msg = TRUE;

	/* this is not a login prompt */
	login_entry = FALSE;
	break;

    case GDM_MSGERR:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	/* HAAAAAAACK.  Sometimes pam send many many messages, SO
	 * we try to collect them until the next prompt or reset or
	 * whatnot */
	if ( ! replace_msg) {
		char *oldtext;
		gtk_label_get (GTK_LABEL (msg), &oldtext);
		if (oldtext != NULL && oldtext[0] != '\0') {
			char *newtext;
			newtext = g_strdup_printf ("%s\n%s", oldtext, buf);
			gtk_label_set (GTK_LABEL (msg), newtext);
		} else {
			gtk_label_set (GTK_LABEL (msg), buf);
		}
	} else {
		gtk_label_set (GTK_LABEL (msg), buf);
	}
	replace_msg = FALSE;

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

    case GDM_RESETOK:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
	buf[len-1] = '\0';

	if (curuser != NULL) {
	    g_free (curuser);
	    curuser = NULL;
	}

	gtk_widget_set_sensitive (entry, TRUE);

	if (GdmBrowser)
	    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);

	gtk_label_set (GTK_LABEL (msg), buf);
	gtk_widget_show (GTK_WIDGET (msg));

	g_print ("%c\n", STX);
	break;

    case GDM_QUIT:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	if (timed_handler_id != 0) {
		gtk_timeout_remove (timed_handler_id);
		timed_handler_id = 0;
	}

	if (require_quarter) {
		GtkWidget *d;

		/* we should be now fine for focusing new windows */
		gdm_wm_focus_new_windows (TRUE);

		/* translators:  This is a nice and evil eggie text, translate
		 * to your favourite currency */
		d = gnome_message_box_new (_("Please insert 25 cents "
					     "to log in."),
					   GNOME_MESSAGE_BOX_INFO,
					   GNOME_STOCK_BUTTON_OK,
					   NULL);
		gtk_window_set_modal (GTK_WINDOW (d), TRUE);
		gdm_center_window (GTK_WINDOW (d));

		gdm_wm_no_login_focus_push ();
		gnome_dialog_run (GNOME_DIALOG (d));
		gdm_wm_no_login_focus_pop ();
	}

	kill_thingies ();

	g_print ("%c\n", STX);

	gtk_main_quit ();
	break;

    case GDM_GNOMESESS:
	{
		char *sess;
		GString *str = g_string_new (NULL);

		do {
			g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
			buf[len-1] = '\0';
			g_string_append (str, buf);
		} while (len == PIPE_SIZE-1);


		sess = get_gnome_session (str->str);

		g_string_free (str, TRUE);

		g_print ("%c%s\n", STX, sess);

		g_free (sess);
	}
	break;

    case GDM_STARTTIMER:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	/*
	 * Timed Login: Start Timer Loop
	 */

	if (timed_handler_id == 0 &&
	    ! gdm_string_empty (GdmTimedLogin) &&
	    GdmTimedLoginDelay > 0) {
		curdelay = GdmTimedLoginDelay;
		timed_handler_id = gtk_timeout_add (1000,
						    gdm_timer, NULL);
	}
	g_print ("%c\n", STX);
	break;

    case GDM_STOPTIMER:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */

	/*
	 * Timed Login: Stop Timer Loop
	 */

	if (timed_handler_id != 0) {
		gtk_timeout_remove (timed_handler_id);
		timed_handler_id = 0;
	}
	g_print ("%c\n", STX);
	break;

    case GDM_DISABLE:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	gtk_widget_set_sensitive (login, FALSE);
	g_print ("%c\n", STX);
	break;

    case GDM_ENABLE:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	gtk_widget_set_sensitive (login, TRUE);
	g_print ("%c\n", STX);
	break;

    /* These are handled separately so ignore them here and send
     * back a NULL response so that the daemon quits sending them */
    case GDM_NEEDPIC:
    case GDM_READPIC:
	g_io_channel_read (source, buf, PIPE_SIZE-1, &len); /* Empty */
	g_print ("%c\n", STX);
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

    /* eek, this shouldn't get here, but just in case */
    if ( ! login_entry)
	    return TRUE;

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
	login_entry = FALSE;
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

    gdk_window_raise (login->window);

    p = g_new0 (CursorOffset, 1);
    gtk_object_set_data_full (GTK_OBJECT (widget), "offset", p,
			      (GtkDestroyNotify)g_free);
    
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
    gdk_cursor_destroy (fleur_cursor);
    gdk_flush ();
    
    return TRUE;
}

static gboolean
gdm_login_handle_released (GtkWidget *widget, GdkEventButton *event)
{
	gtk_grab_remove (widget);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);

	gtk_object_remove_data (GTK_OBJECT (widget), "offset");

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

    gdk_window_get_pointer (GDK_ROOT_PARENT (), &xp, &yp, &mask);

    set_screen_to_pos (xp, yp);

    set_screen_pos (GTK_WIDGET (login), xp-p->x, yp-p->y);

    GdmSetPosition = TRUE;
    GdmPositionX = xp - p->x;
    GdmPositionY = yp - p->y;

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
			gtk_button_set_relief (GTK_BUTTON (w), GTK_RELIEF_NONE);
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
			gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
					      _("Iconify the login window"),
					      NULL);
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
login_realized (GtkWidget *w)
{
	/* In case we're out of bounds, after realization we'll have correct
	 * limits to make the window be on screen */
	if (GdmSetPosition) {
		set_screen_pos (login, GdmPositionX, GdmPositionY);
	}
}

static gboolean
update_clock (gpointer data)
{
	struct tm *the_tm;
	time_t the_time;
	char str[256];

	if (clock_label == NULL)
		return FALSE;

	time (&the_time);
	the_tm = localtime (&the_time);

	if (strftime (str, sizeof (str), _("%a %b %d, %X"), the_tm) == 0) {
		/* according to docs, if the string does not fit, the
		 * contents of str are undefined, thus just use
		 * ??? */
		strcpy (str, "???");
	}
	str [sizeof(str)-1] = '\0'; /* just for sanity */

	gtk_label_set (GTK_LABEL (clock_label), str);

	return TRUE;
}

static void
clock_state_changed (GtkWidget *clock)
{
	clock->state = login->state;
}

static void
gdm_login_gui_init (void)
{
    GtkWidget *frame1, *frame2, *ebox;
    GtkWidget *mbox, *menu, *menubar, *item, *welcome;
    GtkWidget *table, *stack, *hline1, *hline2, *handle;
    GtkWidget *bbox = NULL;
    GtkWidget *logoframe = NULL;
    GtkAccelGroup *accel;
    gchar *greeting;
    gint cols, rows;
    struct stat statbuf;

    if(*GdmGtkRC)
	gtk_rc_parse (GdmGtkRC);

    login = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_ref (login);
    gtk_object_set_data_full (GTK_OBJECT (login), "login", login,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_window_set_title (GTK_WINDOW (login), _("GDM Login"));

    accel = gtk_accel_group_new ();
    gtk_window_add_accel_group (GTK_WINDOW (login), accel);

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

    if (GdmTitleBar) {
	    handle = create_handle ();
	    gtk_box_pack_start (GTK_BOX (mbox), handle, FALSE, FALSE, 0);
    }

    menubar = gtk_menu_bar_new();
    gtk_widget_ref (GTK_WIDGET (menubar));
    gtk_box_pack_start (GTK_BOX (mbox), menubar, FALSE, FALSE, 0);

    menu = gtk_menu_new();
    gdm_login_session_init (menu);
    sessmenu = gtk_menu_item_new_with_label (_("Session"));
    gtk_menu_bar_append (GTK_MENU_BAR(menubar), sessmenu);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (sessmenu), menu);
    gtk_widget_add_accelerator (sessmenu, "activate_item", accel,
				GDK_Escape, 0, 0);
    gtk_widget_add_accelerator (sessmenu, "activate_item", accel,
				GDK_s, GDK_MOD1_MASK, 0);
    gtk_widget_show (GTK_WIDGET (sessmenu));

    menu = gdm_login_language_menu_new ();
    if (menu != NULL) {
	langmenu = gtk_menu_item_new_with_label (_("Language"));
	gtk_menu_bar_append (GTK_MENU_BAR (menubar), langmenu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (langmenu), menu);
	gtk_widget_add_accelerator (langmenu, "activate_item", accel,
				    GDK_l, GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (langmenu));
    }

    if (GdmSystemMenu) {
	menu = gtk_menu_new();
        if (GdmConfigAvailable) {
	   item = gtk_menu_item_new_with_label (_("Configure..."));
	   gtk_menu_append (GTK_MENU (menu), item);
	   gtk_signal_connect (GTK_OBJECT (item), "activate",
			       GTK_SIGNAL_FUNC (gdm_run_gdmconfig),
			       NULL);
	   gtk_widget_show (item);
	   gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
				 _("Configure GDM (this login manager). "
				   "This will require the root password."),
				 NULL);
	}

	item = gtk_menu_item_new_with_label (_("Reboot..."));
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (gdm_login_reboot_handler), 
			    NULL);
	gtk_widget_show (GTK_WIDGET (item));
	gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
			      _("Reboot your computer"),
			      NULL);
	
	item = gtk_menu_item_new_with_label (_("Halt..."));
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			   GTK_SIGNAL_FUNC (gdm_login_halt_handler), 
			    NULL);
	gtk_widget_show (GTK_WIDGET (item));
	gtk_tooltips_set_tip (tooltips, GTK_WIDGET (item),
			      _("Shut down your computer so that "
				"you may turn it off."),
			      NULL);
	
	item = gtk_menu_item_new_with_label (_("System"));
	gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
        gtk_widget_add_accelerator (item, "activate_item", accel,
				    GDK_y, GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (item));
    }

    /* The clock */
    ebox = gtk_event_box_new ();
    gtk_widget_show (ebox);
    clock_label = gtk_label_new ("");
    gtk_widget_show (clock_label);
    gtk_container_add (GTK_CONTAINER (ebox), clock_label);
    item = gtk_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (item), ebox);
    gtk_widget_show (item);
    gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);
    gtk_menu_item_right_justify (GTK_MENU_ITEM (item));
    gtk_widget_set_sensitive (item, FALSE);

    /* hack */
    clock_label->state = GTK_STATE_NORMAL;
    gtk_signal_connect (GTK_OBJECT (clock_label), "state_changed",
			GTK_SIGNAL_FUNC (clock_state_changed),
			NULL);

    gtk_signal_connect (GTK_OBJECT (clock_label), "destroy",
			GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			&clock_label);

    gtk_timeout_add (1000, update_clock, NULL);


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
	int width;
	int height;

	/* Find background style for browser */
	style = gtk_style_copy (login->style);
	style->bg[GTK_STATE_NORMAL] = bbg;
	gtk_widget_push_style (style);
	
	/* Icon list */
	if (maxwidth < GdmIconMaxWidth/2)
	    maxwidth = (gint) GdmIconMaxWidth/2;
	if (maxheight < GdmIconMaxHeight/2)
	    maxheight = (gint) GdmIconMaxHeight/2;
	
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

	/* FIXME: do smarter sizing here */
	width = maxwidth + (maxwidth + 20) * (number_of_users < 5 ?
					      number_of_users : 5);
	if (width > screen.width * 0.5)
		width = screen.width * 0.5;

	height = (maxheight * 1.9 ) *
		(1 + (number_of_users - 1) / 5);
	if (height > screen.height * 0.25)
		height = screen.height * 0.25;

	gtk_widget_set_usize (GTK_WIDGET (bbox), width, height);
    }

    if (GdmLogo &&
	access (GdmLogo, R_OK) == 0) {
	GtkWidget *logo;

	logo = gnome_pixmap_new_from_file (GdmLogo);

	if (logo != NULL) {
		GtkWidget *ebox;
		GtkWidget *frame;
		int lw, lh;

		/* this will make the logo always left justified */
		logoframe = gtk_alignment_new (0, 0.5, 0, 0);
		gtk_widget_show (logoframe);

		frame = gtk_frame_new (NULL);
		gtk_widget_show (frame);
		gtk_frame_set_shadow_type (GTK_FRAME (frame),
					   GTK_SHADOW_IN);

		ebox = gtk_event_box_new ();
		gtk_widget_show (ebox);
		gtk_container_add (GTK_CONTAINER (ebox), logo);
		gtk_container_add (GTK_CONTAINER (frame), ebox);
		gtk_container_add (GTK_CONTAINER (logoframe), frame);

		gdk_window_get_size ((GdkWindow *) GNOME_PIXMAP (logo)->pixmap,
				     &lw, &lh);
		if (lw > screen.width / 2)
			lw = screen.width / 2;
		else
			lw = -1;
		if (lh > (2 * screen.height) / 3)
			lh = (2 * screen.height) / 3;
		else
			lh = -1;
		if (lw > -1 || lh > -1)
			gtk_widget_set_usize (logo, lw, lh);
		gtk_widget_show (GTK_WIDGET (logo));
	}
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
    gtk_widget_set_usize (entry, 120, -1);
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
    gtk_widget_set_name(msg, "Message");
    gtk_widget_ref (msg);
    gtk_object_set_data_full (GTK_OBJECT (login), "msg", msg,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (msg);
    gtk_table_attach (GTK_TABLE (stack), msg, 0, 1, 5, 6,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 10, 10);

    /* Put it nicely together */

    if (bbox != NULL && logoframe != NULL) {
	    gtk_table_attach (GTK_TABLE (table), bbox, 0, 2, 0, 1,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), logoframe, 0, 1, 1, 2,
			      (GtkAttachOptions) (GTK_FILL),
			      (GtkAttachOptions) (0), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 1, 2,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    } else if (bbox != NULL) {
	    gtk_table_attach (GTK_TABLE (table), bbox, 0, 1, 0, 1,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), stack, 0, 1, 1, 2,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    } else if (logoframe != NULL) {
	    gtk_table_attach (GTK_TABLE (table), logoframe, 0, 1, 0, 1,
			      (GtkAttachOptions) (0),
			      (GtkAttachOptions) (0), 0, 0);
	    gtk_table_attach (GTK_TABLE (table), stack, 1, 2, 0, 1,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    } else {
	    gtk_table_attach (GTK_TABLE (table), stack, 0, 1, 0, 1,
			      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			      (GtkAttachOptions) (GTK_FILL), 0, 0);
    }
    
    gtk_widget_grab_focus (entry);	
    gtk_window_set_focus (GTK_WINDOW (login), entry);	
    gtk_window_set_policy (GTK_WINDOW (login), 1, 1, 1);
    
    if (GdmSetPosition) {
	    set_screen_pos (login, GdmPositionX, GdmPositionY);
	    gtk_signal_connect (GTK_OBJECT (login), "realize",
				GTK_SIGNAL_FUNC (login_realized),
				NULL);
    } else {
	    gdm_center_window (GTK_WINDOW (login));
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
gdm_login_user_alloc (const gchar *logname, uid_t uid, const gchar *homedir)
{
	GdmLoginUser *user;
	GdkImlibImage *img = NULL;
	gchar buf[PIPE_SIZE];
	size_t size;

	user = g_new0 (GdmLoginUser, 1);

	user->uid = uid;
	user->login = g_strdup (logname);
	user->homedir = g_strdup (homedir);
	user->picture = defface;

	/* read initial request */
	do {
		while (read (STDIN_FILENO, buf, 1) == 1)
			if (buf[0] == STX)
				break;
		size = read (STDIN_FILENO, buf, sizeof (buf));
		if (size <= 0)
			return user;
	} while (buf[0] != GDM_NEEDPIC);

	g_print ("%c%s\n", STX, logname);

	do {
		while (read (STDIN_FILENO, buf, 1) == 1)
			if (buf[0] == STX)
				break;
		size = read (STDIN_FILENO, buf, sizeof (buf));
		if (size <= 0)
			return user;
	} while (buf[0] != GDM_READPIC);

	/* both nul terminate and wipe the trailing \n */
	buf[size-1] = '\0';

	if (size < 2 ||
	    access (&buf[1], R_OK) != 0) {
		g_print ("%c\n", STX);
		return user;
	}

	img = gdk_imlib_load_image (&buf[1]);

	/* the daemon is now free to go on */
	g_print ("%c\n", STX);

	if (img != NULL) {
		gint w, h;

		w = img->rgb_width;
		h = img->rgb_height;

		if (w > h && w > GdmIconMaxWidth) {
			h = h * ((gfloat) GdmIconMaxWidth/w);
			w = GdmIconMaxWidth;
		} else if (h > GdmIconMaxHeight) {
			w = w * ((gfloat) GdmIconMaxHeight/h);
			h = GdmIconMaxHeight;
		}

		maxwidth = MAX (maxwidth, w);
		maxheight = MAX (maxheight, h);
		user->picture = gdk_imlib_clone_scaled_image (img, w, h);
		gdk_imlib_destroy_image (img);
	}

	return user;
}


static gboolean
gdm_login_check_exclude (struct passwd *pwent)
{
	const char * const lockout_passes[] = { "*", "!!", NULL };
	gint i;

	if ( ! GdmAllowRoot && pwent->pw_uid == 0)
		return TRUE;

	if ( ! GdmAllowRemoteRoot && ! login_is_local && pwent->pw_uid == 0)
		return TRUE;

	for (i=0 ; lockout_passes[i] != NULL ; i++)  {
		if (strcmp (lockout_passes[i], pwent->pw_passwd) == 0) {
			return TRUE;
		}
	}

	if (GdmExclude != NULL &&
	    GdmExclude[0] != '\0') {
		char **excludes;
		excludes = g_strsplit (GdmExclude, ",", 0);

		for (i=0 ; excludes[i] != NULL ; i++)  {
			if (strcasecmp_no_locale (excludes[i],
						  pwent->pw_name) == 0) {
				g_strfreev (excludes);
				return TRUE;
			}
		}
		g_strfreev (excludes);
	}

	return FALSE;
}


static gboolean
gdm_login_check_shell (const gchar *usersh)
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
	    syslog (LOG_WARNING,
		    _("Can't open DefaultImage: %s. Suspending face browser!"),
		    GdmDefaultFace);
	    GdmBrowser = FALSE;
	    return;
    } else  {
	    defface = gdk_imlib_load_image (GdmDefaultFace);
    }

    setpwent ();

    pwent = getpwent();
	
    while (pwent != NULL) {
	
	if (pwent->pw_shell && 
	    gdm_login_check_shell (pwent->pw_shell) &&
	    !gdm_login_check_exclude (pwent)) {

	    user = gdm_login_user_alloc(pwent->pw_name,
					pwent->pw_uid,
					pwent->pw_dir);

	    if ((user) &&
		(! g_list_find_custom (users, user, (GCompareFunc) gdm_login_sort_func))) {
		users = g_list_insert_sorted(users, user,
					     (GCompareFunc) gdm_login_sort_func);
		number_of_users ++;
	    }
	}
	
	pwent = getpwent();
    }

    endpwent ();
}


static void 
gdm_screen_init (void) 
{
#ifdef HAVE_LIBXINERAMA
	gboolean have_xinerama = FALSE;

	gdk_flush ();
	gdk_error_trap_push ();
	have_xinerama = XineramaIsActive (GDK_DISPLAY ());
	gdk_flush ();
	if (gdk_error_trap_pop () != 0)
		have_xinerama = FALSE;

	if (have_xinerama) {
		int screen_num;
		XineramaScreenInfo *xscreens =
			XineramaQueryScreens (GDK_DISPLAY (),
					      &screen_num);


		if (screen_num <= 0) 
			gdm_login_abort ("Xinerama active, but <= 0 screens?");

		if (screen_num <= GdmXineramaScreen)
			GdmXineramaScreen = 0;

		allscreens = g_new0 (GdkRectangle, screen_num);
		screens = screen_num;

		for (i = 0; i < screen_num; i++) {
			allscreens[i].x = xscreens[i].x_org;
			allscreens[i].y = xscreens[i].y_org;
			allscreens[i].width = xscreens[i].width;
			allscreens[i].height = xscreens[i].height;

			if (GdmXineramaScreen == i)
				screen = allscreens[i];
		}

		XFree (xscreens);
	} else
#endif
	{
		screen.x = 0;
		screen.y = 0;
		screen.width = gdk_screen_width ();
		screen.height = gdk_screen_height ();

		allscreens = g_new0 (GdkRectangle, 1);
		allscreens[0] = screen;
		screens = 1;
	}
#if 0
	/* for testing Xinerama support on non-xinerama setups */
	{
		screen.x = 100;
		screen.y = 100;
		screen.width = gdk_screen_width () / 2 - 100;
		screen.height = gdk_screen_height () / 2 - 100;

		allscreens = g_new0 (GdkRectangle, 2);
		allscreens[0] = screen;
		allscreens[1].x = gdk_screen_width () / 2;
		allscreens[1].y = gdk_screen_height () / 2;
		allscreens[1].width = gdk_screen_width () / 2;
		allscreens[1].height = gdk_screen_height () / 2;
		screens = 2;
	}
#endif
}

static void
set_root (GdkPixbuf *pb)
{
	GdkPixmap *pm;

	g_return_if_fail (pb != NULL);

	gdk_pixbuf_render_pixmap_and_mask (pb,
					   &pm,
					   NULL /* mask_return */,
					   0 /* alpha_threshold */);

	/* paranoia */
	if (pm == NULL)
		return;

	gdk_error_trap_push ();

	gdk_window_set_back_pixmap (GDK_ROOT_PARENT (),
				    pm,
				    FALSE /* parent_relative */);

	gdk_pixmap_unref (pm);

	gdk_window_clear (GDK_ROOT_PARENT ());

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

	for (i = 0; i < screens; i++) {
		gdk_pixbuf_scale (pb, back,
				  allscreens[i].x,
				  allscreens[i].y,
				  allscreens[i].width,
				  allscreens[i].height,
				  allscreens[i].x /* offset_x */,
				  allscreens[i].y /* offset_y */,
				  (double) allscreens[i].width / width,
				  (double) allscreens[i].height / height,
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

/* Load the background stuff, the image and program */
static void
run_backgrounds (void)
{
	GdkColor color;
	/* Load background image */
	if (GdmBackgroundType == GDM_BACKGROUND_IMAGE &&
	    GdmBackgroundImage != NULL &&
	    GdmBackgroundImage[0] != '\0') {
		GdkPixbuf *pb = gdk_pixbuf_new_from_file (GdmBackgroundImage);
		if (pb != NULL) {
			if (gdk_pixbuf_get_has_alpha (pb)) {
				if (GdmBackgroundColor == NULL ||
				    GdmBackgroundColor[0] == '\0' ||
				    ! gdk_color_parse (GdmBackgroundColor,
						       &color)) {
					gdk_color_parse ("#007777", &color);
				}
				add_color_to_pb (pb, &color);
			}
			if (GdmBackgroundScaleToFit) {
				GdkPixbuf *spb = render_scaled_back (pb);
				gdk_pixbuf_unref (pb);
				pb = spb;
			}

			/* paranoia */
			if (pb != NULL) {
				set_root (pb);
				gdk_pixbuf_unref (pb);
			}
		}
	/* Load background color */
	} else if (GdmBackgroundType == GDM_BACKGROUND_COLOR) {
		GdkColormap *colormap;

		if (GdmBackgroundColor == NULL ||
		    GdmBackgroundColor[0] == '\0' ||
		    ! gdk_color_parse (GdmBackgroundColor, &color)) {
			gdk_color_parse ("#007777", &color);
		}

		colormap = gdk_window_get_colormap (GDK_ROOT_PARENT ());
		/* paranoia */
		if (colormap != NULL) {
			gdk_error_trap_push ();

			gdk_color_alloc (colormap, &color);
			gdk_window_set_background (GDK_ROOT_PARENT (), &color);
			gdk_window_clear (GDK_ROOT_PARENT ());

			gdk_flush ();
			gdk_error_trap_pop ();
		}
	}

	/* Launch a background program if one exists */
	if (GdmBackgroundProg != NULL &&
	    GdmBackgroundProg[0] != '\0') {
		backgroundpid = gdm_run_command (GdmBackgroundProg);
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

    gdm_login_parse_config ();

    /* no language set, use the GdmDefaultLocale */
    if ( ! gdm_string_empty (GdmDefaultLocale) &&
	g_getenv ("LANG") == NULL &&
	g_getenv ("LC_ALL") == NULL) {
	    setlocale (LC_ALL, GdmDefaultLocale);
    } else {
	    setlocale (LC_ALL, "");
    }

    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    setup_cursor (GDK_LEFT_PTR);

    tooltips = gtk_tooltips_new ();

    gdm_screen_init ();

    if (GdmBrowser)
	gdm_login_users_init ();

    gdm_login_gui_init ();

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

    run_backgrounds ();

    ctrlch = g_io_channel_unix_new (STDIN_FILENO);
    g_io_channel_init (ctrlch);
    g_io_add_watch (ctrlch, 
		    G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		    (GIOFunc) gdm_login_ctrl_handler,
		    NULL);
    g_io_channel_unref (ctrlch);

    /* if in timed mode, delay timeout on keyboard or menu
     * activity */
    if ( ! gdm_string_empty (GdmTimedLogin)) {
	    guint sid = gtk_signal_lookup ("activate",
					   GTK_TYPE_MENU_ITEM);
	    gtk_signal_add_emission_hook (sid,
					  gdm_timer_up_delay,
					  NULL);

	    sid = gtk_signal_lookup ("key_press_event",
				     GTK_TYPE_ENTRY);
	    gtk_signal_add_emission_hook (sid,
					  gdm_timer_up_delay,
					  NULL);
    }

    if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
	    guint sid = gtk_signal_lookup ("event",
					   GTK_TYPE_WIDGET);
	    gtk_signal_add_emission_hook (sid,
					  gdm_event,
					  NULL);
    }

    gtk_widget_show_now (login);

    /* can it ever happen that it'd be NULL here ??? */
    if (login->window != NULL) {
	    gdm_wm_init (GDK_WINDOW_XWINDOW (login->window));

	    /* Run the focus, note that this will work no matter what
	     * since gdm_wm_init will set the display to the gdk one
	     * if it fails */
	    gdm_wm_focus_window (GDK_WINDOW_XWINDOW (login->window));
    }

    if (session_dir_whacked_out) {
	    GtkWidget *dialog;

	    gdm_wm_focus_new_windows (TRUE);

	    dialog = gnome_error_dialog
		    (_("Your session directory is missing or empty!\n\n"
		       "There are two available sessions you can use, but\n"
		       "you should log in and correct the gdm configuration."));
	    gtk_widget_show_all (dialog);
	    gdm_center_window (GTK_WINDOW (dialog));
	    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	    gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
    }

    gtk_main ();

    kill_thingies ();

    return EXIT_SUCCESS;
}

/* EOF */
