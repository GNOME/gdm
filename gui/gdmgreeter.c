/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

/* gdmgreeter is the graphical login part of gdm, The Gnome Display
 * Manager. The greeter runs as a dedicated gdm user and communicates
 * with gdm through a pipe. Thus, all files used by the program must
 * be accessible for `gdm' (or whatever you set the userid to in
 * gdm.conf).  
 */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <pwd.h>
#include <syslog.h>
#include <ctype.h>
#include <signal.h>

#include "gdmgreeter.h"

static const gchar RCSid[]="$Id$";

extern gboolean gdm_file_check(gchar *caller, uid_t user, gchar *dir, gchar *file, gboolean absentok);

gchar *gdm_greeter_session_lookup(gchar *session);
gchar *gdm_greeter_language_lookup(gchar *language);
static gdmUserType *gdm_greeter_user_alloc (gchar *logname, gint uid, gchar *homedir);


gint  GdmCompletion;
gint  GdmDebug;
gint  GdmDisplayBrowser;
gint  GdmDisplayLogo;
gint  GdmIconMaxHeight;
gint  GdmIconMaxWidth;
gint  GdmIconify;
gint  GdmNumberOfCols;
gint  GdmQuiver;
gint  GdmShutdownMenu;
gint  GdmUserMaxFile;
gint  GdmRelaxPerms=0;
gchar *GdmSuspend;
gchar *GdmGlobalImageDir;
gchar *GdmLogoFilename;
gchar *GdmMessageText;
gchar *GdmNofaceImageFile;
gchar *GdmConfigFilename=GDM_CONFIG_FILE;
gchar *GdmSessionDir;
gchar *GdmIconFile;
gchar *GdmMessageFont;
gchar *GdmLocaleFile;
gchar *GdmDefaultLocale;
gchar *GdmPidFile;		/* Hack, not used in greeter */
gchar *GdmDefaultPath;		/* Hack Part II */
gchar *GdmGtkRC;

GtkWidget *greeterframe;
GtkWidget *gdmMain;
GtkWidget *logobox;
GtkWidget *msgbox;
GtkWidget *msglabel;
GtkWidget *loginentry;
GtkWidget *passwdentry;
GtkWidget *greeter;
GtkWidget *buttonpane;
GtkWidget *entrypane;
GdkWindow *rootwin;
GtkWidget *win;
GtkWidget *optionmenu;
GtkWidget *sessmenu;
GtkWidget *langmenu;
GtkWidget *loginbutton;
GtkWidget *optionbutton;
GtkWidget *browser_hbox;

gchar *login, *passwd;
gchar *defsess = NULL;
gchar *deflang = NULL;
gchar *cursess = NULL;
gchar *curlang = NULL;
gchar *usrsess = NULL;
gchar *usrlang = NULL;

gboolean sessmatch=FALSE;
gboolean langmatch=TRUE;
gboolean sessmod=FALSE;
gboolean langmod=TRUE;

GCompletion *cmpl;
GList *result;
GList *users;
GSList *languages=NULL;
GSList *sessions=NULL;
GdkImlibImage *nofaceimg;
GnomeIconList *browser;



/* Normal program termination */
static void
gdm_greeter_done(void)
{
    closelog();
    gtk_main_quit();
}


/* Log formatted error message and exit */
static void
gdm_greeter_abort(const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start(args, format);
    s=g_strdup_vprintf(format, args);
    va_end(args);

    syslog(LOG_ERR, s);

    closelog();
    g_print("%cA", 0x2);
    exit(EXIT_FAILURE);
}


/* Show about dialog */
static void 
gdm_greeter_about (void)
{
    const gchar *authors[] = {"Martin Kasper Petersen <mkp@mkp.net>", NULL};

    GtkWidget *about = gnome_about_new ( _("Gnome Display Manager"), 
					 "" VERSION "",
					 _("Copyright Martin K. Petersen (C) 1998, 1999"),
					 authors,
					 _("gdm manages local and remote displays and provides the user with a login window."),
					 NULL);
    gtk_widget_show (about);                
}


static gchar *
gdm_parse_enriched_string(gchar *s)
{
    gchar cmd, *buffer, *start;
    gchar hostbuf[256];
    gchar *hostname, *temp1, *temp2, *display;

    display=getenv("DISPLAY");

    if(!display)
	return(NULL);

    temp1=strchr(display, '.');
    temp2=strchr(display, ':');

    if(temp1)
	*temp1='\0';
    else if(temp2)
	*temp2='\0';
    else
	return(NULL);		/* Display isn't */

    gethostname(hostbuf, 255);
    hostname=g_strdup(hostbuf);

    if(!hostname) 
	hostname=g_strdup("Gnome");

    if(strlen(s) > 1023) {
	syslog(LOG_ERR, _("gdm_parse_enriched_string: String too long!"));
	return(g_strconcat(_("Welcome to "), hostname, NULL));
    }

    if(!(buffer = g_malloc(4096))) {
	syslog(LOG_ERR, _("gdm_parse_enriched_string: Could not malloc temporary buffer!"));
	return(NULL);
    }

    start = buffer;

    while(*s) {

	if(*s=='%' && (cmd = s[1]) != 0) {
	    s+=2;

	    switch(cmd) {

	    case 'h': 
		memcpy(buffer, hostname, strlen(hostname));
		buffer+=strlen(hostname);
		break;
		
	    case 'd': 
		memcpy(buffer, display, strlen(display));
		buffer+=strlen(display);
		break;

	    case '%':
		*buffer++='%';
		break;
		
	    default:
		break;
	    };
	}
	else
	    *buffer++=*s++;
    }

    *buffer=0;

    return(g_strdup(start));
}


static void 
gdm_greeter_message_init(void)
{
    GtkStyle *style;
    gchar *msg=NULL;

    msg=gdm_parse_enriched_string(GdmMessageText);

    msgbox = gtk_vbox_new(FALSE, 0);

    if(msg) {	
	style = gtk_style_new();
	gdk_font_unref(style->font);
	style->font = gdk_font_load (GdmMessageFont);
	gtk_widget_push_style(style);

	msglabel = gtk_label_new(msg);
	gtk_box_pack_start(GTK_BOX (msgbox), msglabel, TRUE, TRUE, 0);
	gtk_widget_show(msglabel);
	gtk_widget_pop_style();
    }

    gtk_widget_show_all(msgbox);
}


static void 
gdm_greeter_logo_init (void)
{
    GtkWidget *logo;
    GtkWidget *frame;

    if(access(GdmLogoFilename, R_OK)) {
	syslog(LOG_WARNING, _("Logo not found. No image will be displayed!"));
    }
    else {
	logo = gnome_pixmap_new_from_file (GdmLogoFilename);
	gtk_widget_show(logo);
	
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame), logo);
	gtk_widget_show(frame);
	
	logobox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX (logobox), 
			   frame, FALSE, FALSE, 0);
	gtk_widget_show(logobox);
    }
}


static void 
gdm_greeter_parse_config(void)
{
    struct stat unused;
    gchar *display;
	
    if(stat(GDM_CONFIG_FILE, &unused) == -1)
	gdm_greeter_abort(_("gdm_greeter_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmCompletion=gnome_config_get_int("appearance/Completion=1");
    GdmDisplayBrowser=gnome_config_get_int("appearance/Browser=1");
    GdmDisplayLogo=gnome_config_get_int("appearance/Logo=1");
    GdmLogoFilename=gnome_config_get_string("appearance/LogoImage");
    GdmQuiver=gnome_config_get_int("appearance/Quiver=1");
    GdmIconify=gnome_config_get_int("appearance/iconify=1");
    GdmIconFile=gnome_config_get_string("appearance/iconfile=gdm.xpm");
    GdmNofaceImageFile= gnome_config_get_string("appearance/NoFaceImage");
    GdmGlobalImageDir=gnome_config_get_string("appearance/GlobalImageDir");
    GdmMessageFont=gnome_config_get_string("appearance/msgfont=-adobe-helvetica-bold-r-normal-*-*-180-*-*-*-*-*-*");
    GdmGtkRC=gnome_config_get_string("appearance/gtkrc");

    GdmShutdownMenu=gnome_config_get_int("system/ShutdownMenu=0");
    GdmUserMaxFile=gnome_config_get_int("system/UserFileCutoffSize=65536");
    GdmIconMaxWidth=gnome_config_get_int("system/UserIconMaxWidth=128");
    GdmIconMaxHeight=gnome_config_get_int("system/UserIconMaxHeight=128");
    GdmSuspend=g_strdup(gnome_config_get_string("system/SuspendCommand"));
    GdmLocaleFile=g_strdup(gnome_config_get_string("system/LocaleFile=/usr/share/locale/locale.alias"));
    GdmDefaultLocale=g_strdup(gnome_config_get_string("system/DefaultLocale=english"));

    GdmMessageText=gnome_config_get_string(_("messages/welcome=Welcome to %h"));

    GdmSessionDir=gnome_config_get_string("daemon/sessiondir");

    GdmDebug=gnome_config_get_int("debug/enable=0");

    gnome_config_pop_prefix ();

    if(stat(GdmLocaleFile, &unused) == -1)
	gdm_greeter_abort("gdm_greeter_parse_config: Could not open locale file %s. Aborting!", GdmLocaleFile);

    /* Disable System menu on non-local displays */
    display=getenv("DISPLAY");

    if(!display)
	gdm_greeter_abort("gdm_greeter_parse_config: DISPLAY variable not set!");

    if(strncmp(display, ":", 1))
	GdmShutdownMenu=0;
}


static gboolean
gdm_greeter_query(gchar *msg)
{
    GtkWidget *req;

    req=gnome_message_box_new(msg,
			      GNOME_MESSAGE_BOX_QUESTION,
			      GNOME_STOCK_BUTTON_YES,
			      GNOME_STOCK_BUTTON_NO,
			      NULL);
	    
    gtk_window_set_modal(GTK_WINDOW(req), TRUE);
    return(!gnome_dialog_run(GNOME_DIALOG(req)));
}


static void
gdm_greeter_info_handler(GtkWidget *widget, GtkWidget *dialog)
{
    GdkCursor *cursor;

    cursor=gdk_cursor_new(GDK_LEFT_PTR);
    gdk_window_set_cursor (gdmMain->window, cursor);
    gdk_cursor_destroy(cursor);
    gtk_widget_set_sensitive (gdmMain, TRUE);
}


static void
gdm_greeter_info(gchar *msg)
{
    GtkWidget *req;

    req=gnome_message_box_new(msg,
                              GNOME_MESSAGE_BOX_INFO,
                              GNOME_STOCK_BUTTON_OK,
                              NULL);

    gnome_dialog_button_connect(GNOME_DIALOG(req),
				0,
				gdm_greeter_info_handler,
				NULL);

    gtk_window_set_modal(GTK_WINDOW(req), TRUE);
    gtk_widget_show_all(req);
}


static gboolean
gdm_greeter_login (GtkWidget *widget, gpointer data) 
{
    GdkCursor *cursor;

    if(!strlen(gtk_entry_get_text(GTK_ENTRY(loginentry))))
	return(TRUE);

    cursor=gdk_cursor_new(GDK_WATCH);
    gdk_window_set_cursor(gdmMain->window, cursor);
    gdk_cursor_destroy(cursor);
    gtk_widget_set_sensitive(gdmMain, FALSE);
    gtk_widget_show(gdmMain);

    g_print("%cU%s\n%s\n", 0x2, gtk_entry_get_text(GTK_ENTRY (loginentry)), 
	    gtk_entry_get_text(GTK_ENTRY (passwdentry)));

    return(TRUE);
}


static void
gdm_greeter_sesslang_send(void)
{
    gboolean savesess=FALSE, savelang=FALSE;
    static gchar msg[1024];

    if(strcasecmp(cursess, usrsess)) {

	if(sessmatch && sessmod) {
	    g_snprintf(msg, 1023, 
		       _("You have chosen %s for this session, but your default setting is %s.\n" \
			 "Do you wish to make %s the default for future sessions?"),
		       cursess, usrsess, cursess);
	    savesess=gdm_greeter_query(msg);
	}

	if(!sessmatch && sessmod) {
	    g_snprintf(msg, 1023, 
		       _("Your previous session type %s is not installed on this machine.\n" \
			 "Do you wish to make %s the default for future sessions?"),
		       usrsess, cursess);	    
	    savesess=gdm_greeter_query(msg);
	}

	if(!sessmatch && !sessmod) {
	    g_snprintf(msg, 1023, 
		       _("Your previous session type %s is not installed on this machine.\n" \
			 "You will be logged in using the %s environment."),
		       usrsess, cursess);
	    gdm_greeter_info(msg);
	}
    }
    else
	savesess=TRUE;

    if(strcasecmp(curlang, usrlang)) {

	if(langmatch && langmod) {
	    g_snprintf(msg, 1023, 
		       _("You have chosen the language %s for this session, but your default setting is %s.\n" \
			 "Do you wish to make %s the default language for future sessions?"),
		       curlang, usrlang, curlang);
	    savelang=gdm_greeter_query(msg);
	}

	if(!langmatch && langmod) {
	    g_snprintf(msg, 1023, 
		       _("Your previous language %s is not installed on this machine.\n" \
			 "Do you wish to make %s the default language for future sessions?"),
		       usrlang, curlang);	    
	    savelang=gdm_greeter_query(msg);
	}

	if(!langmatch && !langmod) {
	    g_snprintf(msg, 1023, 
		       _("Your previous language %s is not installed on this machine.\n" \
			 "Your environment will be %s."),
		       usrlang, curlang);
	    gdm_greeter_info(msg);
	}
    }
    else
	savelang=TRUE;

    g_print("%c%s\n%d\n%s\n%d\n", 0x3, cursess, savesess, curlang, savelang);
}


static void
gdm_greeter_invalid_passwd_req (void) 
{
    gint i;
    gint x,y;

    /* FIXME: esound! */
    if(GdmQuiver) {
	gdk_window_get_position(greeter->window, &x, &y);

	for (i=32 ; i > 0 ; i=i/2) {
	    gdk_window_move(greeter->window, i+x, y);
	    gdk_window_move(greeter->window, x, y);
	    gdk_window_move(greeter->window, -i+x, y);
	    gdk_window_move(greeter->window, x, y);
	}
    }

    gdm_greeter_info(_("Invalid username or password!"));

    gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);
}


static void
gdm_greeter_reboot(gint reply, gpointer data)
{
    if (reply == GNOME_YES) {
	closelog();
	g_print("%cR", 0x2);
	gtk_main_quit();
    }

    gtk_widget_set_sensitive(gdmMain, TRUE);
    gtk_window_set_focus(GTK_WINDOW(greeter), passwdentry);
}


static gboolean
gdm_greeter_reboot_handler(GtkWidget *widget, gpointer data)
{
    static GtkWidget *box=NULL;

    gtk_widget_set_sensitive(gdmMain, FALSE);
    box=gnome_question_dialog_modal(_("Are you sure you want to reboot the machine?"),
				    GTK_SIGNAL_FUNC (gdm_greeter_reboot),
				    NULL);
    gtk_widget_show(box);

    return(TRUE);
}


static void
gdm_greeter_halt(gint reply, gpointer data)
{
    if (reply == GNOME_YES) {
	closelog();
	g_print("%cH", 0x2);
	gtk_main_quit();
    }

    gtk_widget_set_sensitive(gdmMain, TRUE);
    gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);
}


static gboolean
gdm_greeter_halt_handler(GtkWidget *widget, gpointer data)
{
    static GtkWidget *box=NULL;

    gtk_widget_set_sensitive (gdmMain, FALSE);
    box=gnome_question_dialog_modal(_("Are you sure you want to halt the machine?"),
				    GTK_SIGNAL_FUNC (gdm_greeter_halt), 
				    NULL);	
    gtk_widget_show(box);

    return(TRUE);
}


static gboolean
gdm_greeter_suspend_handler(GtkWidget *widget, int button, gpointer data)
{
    g_print("%cS", 0x2);

    return(TRUE);
}


typedef struct _cursoroffset {gint x,y;} CursorOffset;


static void
gdm_greeter_icon_pressed(GtkWidget *widget, GdkEventButton *event)
{
  CursorOffset *p;

  if(event->type == GDK_2BUTTON_PRESS) {
      gtk_widget_destroy(win);
      gdk_window_show(greeter->window);
      return;
  }

  if (event->type != GDK_BUTTON_PRESS)
      return;

  p=gtk_object_get_user_data(GTK_OBJECT (widget));
  p->x=(gint)event->x;
  p->y=(gint)event->y;

  gtk_grab_add(widget);
  gdk_pointer_grab(widget->window, TRUE,
		   GDK_BUTTON_RELEASE_MASK |
		   GDK_BUTTON_MOTION_MASK |
		   GDK_POINTER_MOTION_HINT_MASK,
		   NULL, NULL, 0);
}


static void
gdm_greeter_icon_released(GtkWidget *widget)
{
  gtk_grab_remove(widget);
  gdk_pointer_ungrab(0);
}


static void
gdm_greeter_icon_motion(GtkWidget *widget, GdkEventMotion *event)
{
  gint xp, yp;
  CursorOffset *p;
  GdkModifierType mask;

  p=gtk_object_get_user_data(GTK_OBJECT (widget));
  gdk_window_get_pointer(rootwin, &xp, &yp, &mask);
  gtk_widget_set_uposition(widget, xp-p->x, yp-p->y);
}


static gboolean
gdm_greeter_iconify_handler(GtkWidget *widget, gpointer data)
{
    GtkWidget *fixed;
    GtkWidget *icon;
    GdkGC *gc;
    GtkStyle *style;
    CursorOffset *icon_pos;
    gint rw, rh, iw, ih;

    gdk_window_hide(greeter->window);
    style=gtk_widget_get_default_style();
    gc=style->black_gc; 
    win=gtk_window_new(GTK_WINDOW_POPUP);

    gtk_widget_set_events(win, 
			  gtk_widget_get_events(win) | 
			  GDK_BUTTON_PRESS_MASK |
			  GDK_BUTTON_MOTION_MASK |
			  GDK_POINTER_MOTION_HINT_MASK);

    gtk_widget_realize(win);

    fixed=gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER (win), fixed);
    gtk_widget_show(fixed);

    icon=gnome_pixmap_new_from_file (GdmIconFile);
    gdk_window_get_size ((GdkWindow *) GNOME_PIXMAP (icon)->pixmap, &iw, &ih);
    
    gtk_fixed_put(GTK_FIXED (fixed), icon, 0, 0);
    gtk_widget_show(icon);

    gtk_signal_connect(GTK_OBJECT (win), "button_press_event",
		       GTK_SIGNAL_FUNC (gdm_greeter_icon_pressed),NULL);
    gtk_signal_connect(GTK_OBJECT (win), "button_release_event",
		       GTK_SIGNAL_FUNC (gdm_greeter_icon_released),NULL);
    gtk_signal_connect(GTK_OBJECT (win), "motion_notify_event",
		       GTK_SIGNAL_FUNC (gdm_greeter_icon_motion),NULL);

    icon_pos=g_new(CursorOffset, 1);
    gtk_object_set_user_data(GTK_OBJECT(win), icon_pos);

    gtk_widget_show(win);

    rw=gdk_screen_width();
    rh=gdk_screen_height();

    gtk_widget_set_uposition(win, rw-iw, rh-ih);

    return(TRUE);
}


static gboolean
gdm_greeter_opt_button_handler (GtkWidget *widget, GdkEvent *event)
{
    if (event->type == GDK_BUTTON_PRESS) {
	GdkEventButton *bevent = (GdkEventButton *) event;
	gtk_menu_popup(GTK_MENU(widget), NULL, NULL, NULL, NULL,
		       bevent->button, bevent->time);
	return(TRUE);
    }

    /* This code will probably never be reached */
    if (event->type == GDK_KEY_PRESS) {
	GdkEventKey *kevent = (GdkEventKey *) event;
	switch(kevent->keyval) {

	case GDK_Left:
	    gtk_window_set_focus(GTK_WINDOW (greeter), loginbutton);
	    return(TRUE);

	case GDK_Right:
	    return(TRUE);

	case GDK_Up:
	case GDK_Tab:
	    gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);	
	    return(TRUE);
	    
	case GDK_Down:
	    gtk_window_set_focus(GTK_WINDOW (greeter), loginentry);	
	    return(TRUE);
	    
	default:
	    break;
	}
    }
	
    return(FALSE);
}


static gboolean
gdm_greeter_login_key_handler (GtkWidget *widget, GdkEventKey *event)
{
    switch(event->keyval) {

    case GDK_Return:
	gdm_greeter_login(NULL, NULL);
	break;

    case GDK_Up:
    case GDK_Tab:
    case GDK_Left:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (loginbutton), "key_press_event");
	gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);	
	break;

    case GDK_Down:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (loginbutton), "key_press_event");
	gtk_window_set_focus(GTK_WINDOW (greeter), loginentry);	
	break;

    case GDK_Right:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (loginbutton), "key_press_event");
	break;

    default:
	break;
    }

    return(TRUE);
}


gchar *
gdm_greeter_session_lookup(gchar *session)
{
    GSList *sess=sessions;
    gchar *temp;

    if(!session) {
	sessmatch=TRUE;
	return(defsess);
    }

    while(sess) {
	gtk_label_get(GTK_LABEL(GTK_BIN(sess->data)->child), &temp);
	
	if(!strcmp(temp, session)) {
	    sessmatch=TRUE;
	    return(g_strdup(session));
	}
	
	sess=sess->next;
    }

    sessmatch=FALSE;
    return(defsess);
}


static void
gdm_greeter_session_select(GtkWidget *widget, gchar *sess)
{
    gchar *temp;

    if(!widget || !sess) 
	return;

    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &temp);

    if(!strcasecmp(temp, sess))
        gtk_check_menu_item_set_state (GTK_CHECK_MENU_ITEM (widget), TRUE);
}


static void 
gdm_greeter_session_handler (GtkWidget *widget) 
{
    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &cursess);
    sessmod=TRUE;
}


static void 
gdm_greeter_session_init(GtkWidget *parent)
{
    DIR *sessdir;
    struct dirent *dent;
    struct stat statbuf;
    gint linklen;
    GtkWidget *submenu, *item;

    /* Check that session dir is readable */
    if (access(GdmSessionDir, R_OK|X_OK))
	gdm_greeter_abort(_("gdm_greeter_session_init: Session script directory not found!"));

    /* Create submenu */
    submenu=gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), submenu);

    /* Read directory entries in session dir */
    sessdir=opendir(GdmSessionDir);
    dent=readdir(sessdir);

    while(dent != NULL) {
	gchar *s;

	s=g_strconcat(GdmSessionDir, "/", dent->d_name, NULL);
	lstat(s, &statbuf);

	/* If default session link exists, find out what it points to */
	if(S_ISLNK(statbuf.st_mode) && !strcasecmp(dent->d_name, "default")) {
	    gchar t[_POSIX_PATH_MAX];
	    
	    linklen=readlink(s, t, _POSIX_PATH_MAX);
	    t[linklen]=0;
	    defsess=g_strdup(t);
	}

	/* If session script is readable/executable add it to the list */
	if(S_ISREG(statbuf.st_mode)) {

	    if((statbuf.st_mode & (S_IRUSR|S_IXUSR)) == (S_IRUSR|S_IXUSR) &&
	       (statbuf.st_mode & (S_IRGRP|S_IXGRP)) == (S_IRGRP|S_IXGRP) &&
	       (statbuf.st_mode & (S_IROTH|S_IXOTH)) == (S_IROTH|S_IXOTH)) 
	    {
		item=gtk_radio_menu_item_new_with_label(sessions, dent->d_name);
		sessions=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM (item));
		gtk_check_menu_item_set_show_toggle(GTK_CHECK_MENU_ITEM (item), TRUE);
		gtk_menu_append(GTK_MENU (submenu), item);
		
		gtk_signal_connect(GTK_OBJECT (item), "activate",
				   GTK_SIGNAL_FUNC (gdm_greeter_session_handler), NULL);

		gtk_widget_show(item);
	    }
	    else 
		syslog(LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
		       GdmSessionDir, dent->d_name);

	}

	dent=readdir(sessdir);
	g_free(s);
    }

    if(!g_slist_length(sessions)) 
	gdm_greeter_abort(_("No session scripts found. Aborting!"));

    if(!defsess) {
	gtk_label_get(GTK_LABEL(GTK_BIN(g_slist_nth_data(sessions, 0))->child), &defsess);
	syslog(LOG_WARNING, _("No default session link found. Using %s.\n"), defsess);
    }

}


gchar *
gdm_greeter_language_lookup(gchar *language)
{
    GSList *lang=languages;
    gchar *temp;

    if(!language) {
	langmatch=TRUE;
	return(deflang);
    }

    while(lang) {
	gtk_label_get(GTK_LABEL(GTK_BIN(lang->data)->child), &temp);
	
	if(!strcasecmp(temp, language)) {
	    langmatch=TRUE;
	    return(g_strdup(language));
	}
	
	lang=lang->next;
    }

    langmatch=FALSE;
    return(deflang);
}


static void
gdm_greeter_language_select(GtkWidget *widget, gchar *lang)
{
    gchar *temp;

    if(!widget || !lang)
	return;

    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &temp);

    if(!strcasecmp(temp, lang))
        gtk_check_menu_item_set_state (GTK_CHECK_MENU_ITEM (widget), TRUE);
}


static void 
gdm_greeter_language_handler (GtkWidget *widget) 
{
    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &curlang);
    langmod=TRUE;
}


static void
gdm_greeter_language_init(GtkWidget *parent)
{
    GtkWidget *submenu, *item;
    FILE *langlist;
    char curline[256];
    char *ctmp, *ctmp1, *ctmp2;

    deflang=GdmDefaultLocale;
    
    submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), submenu);

    langlist = fopen(GdmLocaleFile, "r");
    
    item = gtk_radio_menu_item_new_with_label(NULL, "English");
    languages = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
    gtk_widget_show(item);
    
    gtk_container_add(GTK_CONTAINER(submenu), item);
    gtk_signal_connect(GTK_OBJECT(item), "activate", gdm_greeter_language_handler, NULL);
    
    while(fgets(curline, sizeof(curline), langlist)) {
	if(!isalpha(curline[0])) continue;
	
	ctmp1 = strchr(curline, ' ');
	ctmp2 = strchr(curline, '\t');
	ctmp = curline + strlen(curline) - 1;
	if(ctmp1 && (ctmp1 < ctmp))
	    ctmp = ctmp1;
	if(ctmp2 && (ctmp2 < ctmp))
	    ctmp = ctmp2;
	*ctmp = '\0';
	curline[0] = toupper(curline[0]);
	
	item = gtk_radio_menu_item_new_with_label(languages, curline);
	languages = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
	gtk_widget_show(item);
	
	gtk_container_add(GTK_CONTAINER(submenu), item);
	gtk_signal_connect(GTK_OBJECT(item), "activate", gdm_greeter_language_handler, NULL);
    }
    
    fclose(langlist);
}


static void 
gdm_greeter_sl_update(gdmUserType *user)
{
    gboolean fileok;
    gchar *dir=NULL, *cfg=NULL;

    /* Check if ~user/.gnome/gdm passes sanity check */
    dir=g_strconcat(user->homedir, "/.gnome", NULL);
    fileok=gdm_file_check("gdm_greeter_sl_update", user->uid, dir, "gdm", TRUE);
    g_free(dir);
 
    /* Find user's last session and language if available */
    if(fileok) {
	cfg=g_strconcat("=", user->homedir, "/.gnome/gdm=/session/last", NULL);
	usrsess=gnome_config_get_string(cfg);
	g_free(cfg);
	
	cfg=g_strconcat("=", user->homedir, "/.gnome/gdm=/session/lang", NULL);
	usrlang=gnome_config_get_string(cfg);
	g_free(cfg);
    }

    sessmatch=FALSE;
    sessmod=FALSE;
    cursess=gdm_greeter_session_lookup(usrsess);

    if(!usrsess)
	usrsess=g_strdup(cursess);

    g_slist_foreach(sessions, (GFunc) gdm_greeter_session_select, cursess);
    gtk_widget_set_sensitive (sessmenu, TRUE);

    langmatch=FALSE;
    langmod=FALSE;
    curlang=gdm_greeter_language_lookup(usrlang);

    if(!usrlang)
	if(curlang)
	    usrlang=g_strdup(curlang);
	else
	    usrlang=gdm_greeter_language_lookup("english");

    g_slist_foreach(languages, (GFunc) gdm_greeter_language_select, curlang);
    gtk_widget_set_sensitive (langmenu, TRUE);

    /* Widget magic */
    gtk_entry_set_text (GTK_ENTRY (loginentry), user->login);
    gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);
}


static void 
gdm_greeter_buttons_init(void)
{
    GtkWidget *submenu, *item;

    /* Login button */
    loginbutton = gtk_button_new_with_label(_("Login"));
    gtk_signal_connect(GTK_OBJECT (loginbutton), "clicked",
		       GTK_SIGNAL_FUNC (gdm_greeter_login), NULL);
    gtk_signal_connect(GTK_OBJECT (loginbutton), "key_press_event",
		       GTK_SIGNAL_FUNC (gdm_greeter_login_key_handler), NULL);
    GTK_WIDGET_SET_FLAGS(loginbutton, GTK_CAN_DEFAULT);
    gtk_widget_show(loginbutton);

    /* Option button drop down menu */
    optionmenu = gtk_menu_new();

    /* Init sessions menu */
    sessmenu = gtk_menu_item_new_with_label(_("Sessions"));
    gtk_menu_append(GTK_MENU (optionmenu), sessmenu);
    gdm_greeter_session_init(sessmenu);
    gtk_widget_show(sessmenu);
    gtk_widget_set_sensitive (sessmenu, FALSE);

    /* Language selection support */
    langmenu = gtk_menu_item_new_with_label (_("Languages"));
    gtk_menu_append(GTK_MENU (optionmenu), langmenu);
    gdm_greeter_language_init(langmenu);
    gtk_widget_show(langmenu);
    gtk_widget_set_sensitive (langmenu, FALSE);

    /* If sysadmin enabled shutdown menu */
    if (GdmShutdownMenu) {
	submenu = gtk_menu_new();

	item = gtk_menu_item_new_with_label (_("Reboot..."));
	gtk_menu_append(GTK_MENU (submenu), item);
	gtk_signal_connect(GTK_OBJECT (item), "activate",
			   GTK_SIGNAL_FUNC (gdm_greeter_reboot_handler), NULL);
	gtk_widget_show(item);

	item = gtk_menu_item_new_with_label (_("Halt..."));
	gtk_menu_append(GTK_MENU (submenu), item);
	gtk_signal_connect(GTK_OBJECT (item), "activate",
			   GTK_SIGNAL_FUNC (gdm_greeter_halt_handler), NULL);
	gtk_widget_show(item);

	if(GdmSuspend) {
	    item = gtk_menu_item_new_with_label (_("Suspend"));
	    gtk_menu_append(GTK_MENU (submenu), item);
	    gtk_signal_connect(GTK_OBJECT (item), "activate",
			       GTK_SIGNAL_FUNC (gdm_greeter_suspend_handler), NULL);
	    gtk_widget_show(item);
	}

	item = gtk_menu_item_new_with_label (_("System"));
	gtk_menu_append(GTK_MENU (optionmenu), item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_widget_show(item);
    }

    if(GdmIconify) {
	if(access(GdmIconFile, R_OK)) {
	    syslog(LOG_WARNING, _("Can't open icon file: %s. Suspending iconify feature!"), GdmIconFile);
	    GdmIconify=0;
	}
	else {
	    item = gtk_menu_item_new_with_label (_("Iconify"));
	    gtk_menu_append(GTK_MENU (optionmenu), item);
	    gtk_signal_connect(GTK_OBJECT (item), "activate",
			       GTK_SIGNAL_FUNC (gdm_greeter_iconify_handler), NULL);
	    gtk_widget_show(item);
	}
    }

    /* Option button */
    optionbutton = gtk_button_new_with_label(_("Options..."));
    gtk_signal_connect_object(GTK_OBJECT (optionbutton), "event", 
			      GTK_SIGNAL_FUNC (gdm_greeter_opt_button_handler),
			      GTK_OBJECT(optionmenu));
    GTK_WIDGET_SET_FLAGS(optionbutton, GTK_CAN_DEFAULT);
    gtk_widget_show(optionbutton);

    buttonpane = gtk_hbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER (buttonpane), 0);

    gtk_box_pack_start(GTK_BOX (buttonpane), 
		       loginbutton, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX (buttonpane), 
		       optionbutton, TRUE, TRUE, 5);

    gtk_window_set_default(GTK_WINDOW(greeter), GTK_WIDGET(loginbutton));
    gtk_widget_show_all(buttonpane);
}


static gboolean 
gdm_greeter_browser_select(GtkWidget *widget, gint selected, GdkEvent *event)
{
    gdmUserType *user;
 
    if(!event)
	return(TRUE);
    else
	switch(event->type) {
	    
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	    user=g_list_nth_data(result, selected);
	    gdm_greeter_sl_update(user);
	    break;
	    
	default: 
	    break;
	}

    return(TRUE);
}


static gboolean
gdm_greeter_browser_unselect(GtkWidget *widget, gint selected, GdkEvent *event)
{
    if(event) {
	switch(event->type) {
	    
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	    gnome_icon_list_unselect_all((GnomeIconList *) widget, NULL, NULL);
	    gtk_entry_set_text (GTK_ENTRY (loginentry), "");
	    gtk_window_set_focus(GTK_WINDOW (greeter), loginentry);
	    gtk_widget_set_sensitive (sessmenu, FALSE);
	    gtk_widget_set_sensitive (langmenu, FALSE);
	    break;

	default:
 	    break;
	}
    }

    return(TRUE);
}


static void 
gdm_greeter_browser_foreach (gdmUserType *user)
{
    if(browser)
	gnome_icon_list_append_imlib (browser, user->picture, user->login);
}


static void 
gdm_greeter_browser_init (void)
{
    GtkWidget *scrollbar;
    gchar *lmatch=NULL;
    GtkWidget *bframe;
    GtkStyle *style;
    GdkColor bbg = { 0, 0xFFFF, 0xFFFF, 0xFFFF };

    /*
     * Create icon list and scrollbar
     */

    style = gtk_style_copy (greeter->style);
    style->bg[GTK_STATE_NORMAL] = bbg;
    gtk_widget_push_style(style);

    browser = GNOME_ICON_LIST (gnome_icon_list_new (GdmIconMaxWidth+20, NULL, FALSE));
    gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
    gtk_widget_pop_style();

    scrollbar = gtk_vscrollbar_new (browser->adj);
    bframe = gtk_frame_new(NULL);

    gtk_frame_set_shadow_type(GTK_FRAME (bframe), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(bframe), GTK_WIDGET(browser));

    browser_hbox = gtk_hbox_new (0, 0);
    gtk_box_pack_start (GTK_BOX (browser_hbox), GTK_WIDGET (bframe), 1, 1, 0);
    gtk_box_pack_start (GTK_BOX (browser_hbox), scrollbar, 0, 0, 0);
    
    gnome_icon_list_set_separators (browser, " /-_.");
    gnome_icon_list_set_row_spacing (browser, 2);
    gnome_icon_list_set_col_spacing (browser, 2);
    gnome_icon_list_set_icon_border (browser, 2);
    gnome_icon_list_set_text_spacing (browser, 2);
    gnome_icon_list_set_selection_mode (browser, GTK_SELECTION_SINGLE);

    gtk_signal_connect (GTK_OBJECT (browser), "select_icon",
			GTK_SIGNAL_FUNC (gdm_greeter_browser_select), NULL);

    gtk_signal_connect (GTK_OBJECT (browser), "unselect_icon",
			GTK_SIGNAL_FUNC (gdm_greeter_browser_unselect), NULL);

    result = g_completion_complete(cmpl, "", &lmatch);

    if(lmatch)
	gtk_entry_set_text(GTK_ENTRY (loginentry), lmatch);

    gnome_icon_list_clear (browser);
    g_list_foreach(users, (GFunc) gdm_greeter_browser_foreach, NULL);
    gnome_icon_list_thaw (browser);
    gtk_widget_show_all(GTK_WIDGET(browser_hbox));
}


static gboolean
gdm_greeter_login_entry_handler (GtkWidget *widget, GdkEventKey *event)
{
    gchar *lmatch=NULL;
    gchar *entry=NULL;
    gint i;
    gdmUserType *user=NULL;

    gtk_widget_set_sensitive (sessmenu, FALSE);
    gtk_widget_set_sensitive (langmenu, FALSE);

    switch(event->keyval) {

    case GDK_Return:
    case GDK_Down:
    case GDK_Tab:		/* Where the nightmare begins */

	gtk_signal_emit_stop_by_name(GTK_OBJECT (loginentry), "key_press_event");
	
	entry=gtk_entry_get_text(GTK_ENTRY (loginentry));
	
	if(!strlen(entry)) 
	    entry=g_strdup("");
	
	result = g_completion_complete(cmpl, entry, &lmatch);
	
	/* Unambiguous match => Move to passwd field and enable
	 * session selection 
	 */
	if(g_list_length(result) == 1) {
	    gtk_window_set_focus(GTK_WINDOW (greeter), passwdentry);
	    gtk_widget_set_sensitive (sessmenu, TRUE);
	    gtk_widget_set_sensitive (langmenu, TRUE);
	}
	
	/* One or no completions => Show all users */
	if(g_list_length(result) <= 1) 
	    result = g_completion_complete(cmpl, "", NULL);
	
	/* If partial match write greatest common string in login
	 * field
	 */
	if(lmatch)
	    gtk_entry_set_text(GTK_ENTRY (loginentry), lmatch);
	
	if(GdmDisplayBrowser) {
	    gnome_icon_list_freeze(browser);
	    gnome_icon_list_clear(browser);
	    g_list_foreach(result, (GFunc) gdm_greeter_browser_foreach, NULL);
	    gnome_icon_list_thaw (browser);
	}
	
	for (i=0 ; i<g_list_length(result) ; i++) {
	    user=g_list_nth_data(result, i);
	    
	    if(!strcasecmp(gtk_entry_get_text(GTK_ENTRY(loginentry)), user->login)) {
		
		if(GdmDisplayBrowser) {
		    gnome_icon_list_moveto(browser, i, 0.5);
		    gnome_icon_list_select_icon(browser, i);
		}
		
		gdm_greeter_sl_update(user);
	    }
	}
	
	break;

    case GDK_Up:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (loginentry), "key_press_event");
	break;

    case GDK_F1:
	gdm_greeter_about();
	break;

    default:
	break;
    }

    return(TRUE);
}


static gboolean
gdm_greeter_passwd_entry_handler (GtkWidget *widget, GdkEventKey *event)
{
    switch(event->keyval) {

    case GDK_Return:
	gdm_greeter_login(NULL, NULL);
	break;

    case GDK_Up:
    case GDK_Tab:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (passwdentry), "key_press_event");
	gtk_window_set_focus(GTK_WINDOW (greeter), loginentry);	
	break;

    case GDK_Down:
	gtk_signal_emit_stop_by_name(GTK_OBJECT (passwdentry), "key_press_event");
	break;

    default:
	break;
    }

    return(TRUE);
}


static void 
gdm_greeter_entry_init(void)
{
    GtkWidget *topsep;
    GtkWidget *botsep;
    GtkWidget *labelvbox;
    GtkWidget *loginlabel;
    GtkWidget *passwdlabel;
    GtkWidget *entryvbox;
    GtkWidget *gdminputhbox;

    entrypane = gtk_vbox_new(FALSE, 0);
    gdminputhbox = gtk_hbox_new(FALSE, 10);
    labelvbox = gtk_vbox_new(TRUE, 0);
    entryvbox = gtk_vbox_new(TRUE, 0);

    topsep = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX (entrypane), topsep, 
		       FALSE, FALSE, 10);
    gtk_widget_show (topsep);

    loginlabel = gtk_label_new(_("Login:"));
    gtk_misc_set_alignment (GTK_MISC (loginlabel), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX (labelvbox), loginlabel, 
		       FALSE, FALSE, 5);
    gtk_widget_show(loginlabel);

    passwdlabel = gtk_label_new(_("Password:"));
    gtk_misc_set_alignment (GTK_MISC (passwdlabel), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX (labelvbox), passwdlabel, 
		       FALSE, FALSE, 0);
    gtk_widget_show(passwdlabel);

    loginentry = gtk_entry_new_with_max_length (32);
    gtk_entry_set_text (GTK_ENTRY (loginentry), "");
    gtk_entry_select_region (GTK_ENTRY (loginentry), 0, 
			     GTK_ENTRY (loginentry)->text_length);
    gtk_signal_connect_object(GTK_OBJECT (loginentry), 
			      "key_press_event", 
			      GTK_SIGNAL_FUNC (gdm_greeter_login_entry_handler),
			      NULL);

    gtk_box_pack_start(GTK_BOX (entryvbox), loginentry, 
		       FALSE, TRUE, 5);
    gtk_widget_show(loginentry);

    passwdentry = gtk_entry_new_with_max_length (32);
    gtk_entry_set_text (GTK_ENTRY (passwdentry), "");
    gtk_entry_set_visibility(GTK_ENTRY (passwdentry), FALSE);
    gtk_signal_connect_object(GTK_OBJECT (passwdentry), 
			      "key_press_event", 
			      GTK_SIGNAL_FUNC (gdm_greeter_passwd_entry_handler),
			      NULL);
    gtk_box_pack_start(GTK_BOX (entryvbox), passwdentry, 
		       FALSE, TRUE, 5);
    gtk_widget_show(passwdentry);

    gtk_box_pack_start(GTK_BOX (gdminputhbox), labelvbox, 
		       FALSE, FALSE, 10);
    gtk_widget_show (labelvbox);

    gtk_box_pack_start(GTK_BOX (gdminputhbox), entryvbox, 
		       TRUE, TRUE, 10);
    gtk_widget_show (entryvbox);

    gtk_box_pack_start(GTK_BOX (entrypane), gdminputhbox, 
		       FALSE, FALSE, 0);
    gtk_widget_show (gdminputhbox);

    botsep = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX (entrypane), botsep, 
		       FALSE, FALSE, 10);
    gtk_widget_show(botsep);

    gtk_widget_show(entrypane);
}


gdmUserType * 
gdm_greeter_user_alloc (gchar *logname, gint uid, gchar *homedir)
{
    gdmUserType *user;
    gchar *dir=NULL, *img=NULL;
    gboolean fileok;
    GdkImlibImage *imlibimg=NULL;
    gint w, h;

    /* Allocate mem for user info struct */
    user=g_malloc(sizeof(gdmUserType));

    if(!user)
	return(NULL);

    user->uid=uid;
    user->login=g_strdup(logname);
    user->homedir=g_strdup(homedir);

    /* If browser enabled choose an appropriate picture */
    if(GdmDisplayBrowser) {
	
	/* Check if ~user/.gnome/photo passes sanity check */
	dir=g_strconcat(homedir, "/.gnome", NULL);
	fileok=gdm_file_check("gdm_greeter_user_alloc", uid, dir, "photo", FALSE);
	g_free(dir);

	/* If user's picture passed sanity check/size cutoff value load it */
	if(fileok) {
	    img=g_strconcat(homedir, "/.gnome/photo", NULL);
	    imlibimg=gdk_imlib_load_image(img);
	    g_free(img);
	}
	else {
	    img=g_strconcat(GdmGlobalImageDir, "/", logname, NULL);
	    if (!access(img, R_OK))
		imlibimg=gdk_imlib_load_image(img);
	    g_free(img);
	}

	if(imlibimg) {
	    w=imlibimg->rgb_width;
	    h=imlibimg->rgb_height;
	    
	    if(w>h && w>GdmIconMaxWidth) {
		h=h*((gfloat) GdmIconMaxWidth/w);
		w=GdmIconMaxWidth;
	    } 
	    else if(h>GdmIconMaxHeight) {
		w=w*((gfloat) GdmIconMaxHeight/h);
		h=GdmIconMaxHeight;
	    }

	    user->picture=gdk_imlib_clone_scaled_image(imlibimg, w, h);
	    
	    gdk_imlib_destroy_image(imlibimg);
	}
	else
	    user->picture=nofaceimg;

    }

    return (user);
}


static gint 
gdm_greeter_sort_func(gpointer d1, gpointer d2)
{
    gdmUserType *a=d1;
    gdmUserType *b=d2;

    return strcmp(a->login, b->login);
}


static gchar *
gdm_greeter_complete_func(gpointer d1)
{
    gdmUserType *a=d1;

    return (a->login);
}


static gint
gdm_greeter_check_shell (gchar *usersh)
{
    gint found=0;
    gchar *csh;

    setusershell();

    while((csh=getusershell()) != NULL)
	if(!strcmp(csh, usersh))
	    found=1;

    endusershell();

    return(found);
}


static void 
gdm_greeter_users_init (void)
{
    gdmUserType *user;
    struct passwd *pwent;

    if(GdmDisplayBrowser) {
	if(access(GdmNofaceImageFile, R_OK)) {
	    syslog(LOG_WARNING, _("Can't open NofaceImageFile: %s. Suspending face browser!"), GdmNofaceImageFile);
	    GdmDisplayBrowser=0;
	}
	else 
	    nofaceimg=gdk_imlib_load_image(GdmNofaceImageFile);
    }

    pwent = getpwent();
	
    while (pwent != NULL) {
	
	if (pwent->pw_shell && gdm_greeter_check_shell(pwent->pw_shell)) {

	    user=gdm_greeter_user_alloc(pwent->pw_name,
					pwent->pw_uid,
					pwent->pw_dir);
	    if(user)
		users=g_list_insert_sorted(users, 
					   user,
					   (GCompareFunc) gdm_greeter_sort_func);
	}
	
	pwent = getpwent();
    }
    
    cmpl = g_completion_new ((GCompletionFunc) gdm_greeter_complete_func);
    
    /* FIXME: Gross hack */
    users = g_list_reverse(users);
    g_completion_add_items (cmpl, users);
    users = g_list_reverse(users);
}


int 
main (int argc, char *argv[])
{
    struct sigaction usr1, usr2, hup;
    sigset_t mask;
    GtkWidget *gdmLogoCtrlHbox;
    GtkWidget *gdmCtrlVbox;

    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog("gdmgreeter", LOG_PID, LOG_DAEMON);

    gnome_init("gdmgreeter", VERSION, argc, argv);
    gnome_sound_shutdown ();
    
    gdm_greeter_parse_config();

    /* If completion is disabled => Disable browser */
    /* if(!GdmCompletion)
	GdmDisplayBrowser=FALSE; */

    /* If browser is enabled => Force completion */
    /* if(GdmDisplayBrowser) */

    /* Force completion until PAM conversation function is implemented */
    GdmCompletion=TRUE;

    if(GdmCompletion)
	gdm_greeter_users_init();

    if(GdmGtkRC)
	gtk_rc_parse(GdmGtkRC);

    greeter = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_container_border_width (GTK_CONTAINER (greeter), 0);

    greeterframe = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (greeterframe), GTK_SHADOW_OUT);
    gtk_container_border_width (GTK_CONTAINER (greeterframe), 0);
    gtk_container_add(GTK_CONTAINER (greeter), greeterframe);

    gdmMain = gtk_vbox_new(FALSE, 20);
    gtk_container_border_width (GTK_CONTAINER (gdmMain), 0);
    gtk_container_add(GTK_CONTAINER (greeterframe), gdmMain);

    if(GdmDisplayBrowser) {
	gdm_greeter_browser_init();
	gtk_box_pack_start(GTK_BOX (gdmMain), 
			   GTK_WIDGET(browser_hbox), TRUE, TRUE, 0);
    }

    gdmLogoCtrlHbox = gtk_hbox_new(FALSE, 10);
    gtk_container_border_width (GTK_CONTAINER (gdmMain), 20);
    gtk_box_pack_end(GTK_BOX (gdmMain), 
                       gdmLogoCtrlHbox, FALSE, FALSE, 0);

    if ((GdmDisplayLogo) && (GdmLogoFilename)) {
	gdm_greeter_logo_init();
	gtk_box_pack_start(GTK_BOX (gdmLogoCtrlHbox), 
			   logobox, FALSE, FALSE, 0);
    }

    gdmCtrlVbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER (gdmLogoCtrlHbox), gdmCtrlVbox);

    gdm_greeter_message_init();
    gtk_box_pack_start(GTK_BOX (gdmCtrlVbox), 
                       msgbox, TRUE, FALSE, 0);

    gdm_greeter_buttons_init();
    gtk_box_pack_end(GTK_BOX (gdmCtrlVbox), 
                       buttonpane, FALSE, FALSE, 0);

    gdm_greeter_entry_init();
    gtk_box_pack_end(GTK_BOX (gdmCtrlVbox), 
                       entrypane, FALSE, FALSE, 0);

    gtk_widget_show(gdmCtrlVbox);
    gtk_widget_show(gdmLogoCtrlHbox);
    gtk_widget_show(gdmMain);
    gtk_widget_show(greeterframe);

    gtk_window_set_policy(GTK_WINDOW (greeter), 1, 1, 1);
    gtk_window_set_focus(GTK_WINDOW (greeter), loginentry);	
    gtk_window_activate_focus(GTK_WINDOW (greeter));	
    gtk_widget_grab_focus(loginentry);	

    rootwin=gdk_window_foreign_new (GDK_ROOT_WINDOW ());

    if(GdmDisplayBrowser) {
	gtk_widget_show_all(GTK_WIDGET(browser));
	
	gtk_widget_set_usize(GTK_WIDGET (greeter), 
			     (gint) gdk_screen_width() * 0.6, 
			     (gint) gdk_screen_height() * 0.8);
    }

    gtk_window_position(GTK_WINDOW (greeter), GTK_WIN_POS_CENTER);
    gtk_widget_show_all(greeter);

    hup.sa_handler = (void *) gdm_greeter_done;
    hup.sa_flags = 0;
    sigemptyset(&hup.sa_mask);

    if(sigaction(SIGHUP, &hup, NULL) < 0) 
        gdm_greeter_abort(_("main: Error setting up HUP signal handler"));

    if(sigaction(SIGINT, &hup, NULL) < 0) 
        gdm_greeter_abort(_("main: Error setting up INT signal handler"));

    if(sigaction(SIGTERM, &hup, NULL) < 0) 
        gdm_greeter_abort(_("main: Error setting up TERM signal handler"));

    usr1.sa_handler = (void *) gdm_greeter_invalid_passwd_req;
    usr1.sa_flags = SA_RESTART;
    sigemptyset(&usr1.sa_mask);
    sigaddset(&usr2.sa_mask, SIGUSR2);

    if(sigaction(SIGUSR1, &usr1, NULL) < 0) 
        gdm_greeter_abort(_("main: Error setting up USR1 signal handler"));

    usr2.sa_handler = (void *) gdm_greeter_sesslang_send;
    usr2.sa_flags = SA_RESTART;
    sigemptyset(&usr2.sa_mask);
    sigaddset(&usr2.sa_mask, SIGUSR1);

    if(sigaction(SIGUSR2, &usr2, NULL) < 0)
        gdm_greeter_abort(_("main: Error setting up USR2 signal handler"));

    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGUSR2);
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGHUP);
    sigdelset(&mask, SIGINT);
    
    if(sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
	syslog(LOG_ERR, "Could not set signal mask!");
	exit(EXIT_FAILURE);
    }

    gtk_main();

    return 0;
}

/* EOF */
