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

/* gdmlogin is a stripped down greeter for environments where:
 *
 *  1. Users are not trusted (i.e. gdmlogin is less vulnerable to DoS
 *  attacks )
 *
 *  2. Usernames should not be exposed 
 */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <gdk/gdkx.h>
#include <pwd.h>
#include <sys/utsname.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";

extern gboolean gdm_file_check(gchar *caller, uid_t user, gchar *dir, gchar *file, gboolean absentok);

gint  GdmDebug;
gint  GdmIconify;
gint  GdmShutdownMenu;
gint  GdmVerboseAuth;
gint  GdmQuiver;
gint  GdmChooserMenu;
gint  GdmRelaxPerms;
gint  GdmUserMaxFile;
gchar *GdmLogoFilename;
gchar *GdmMessageText;
gchar *GdmMessageFont;
gchar *GdmConfigFilename=GDM_CONFIG_FILE;
gchar *GdmMessageFont;
gchar *GdmGtkRC;
gchar *GdmIconFile;
gchar *GdmSessionDir;
gchar *GdmLocaleFile;
gchar *GdmDefaultLocale;

GtkWidget *login;
GtkWidget *label;
GtkWidget *entry;
GtkWidget *msg;
GtkWidget *win;
GtkWidget *sessmenu;
GtkWidget *langmenu;
GdkWindow *rootwin;

/* Eew. Loads of global vars. It's hard to be event controlled while maintaining state */
GSList *sessions=NULL;
GSList *languages=NULL;

gchar *defsess=NULL;
gchar *cursess=NULL;
gchar *curlang=NULL;
gchar *curuser=NULL;
gchar *lastsess=NULL;
gchar *lastlang=NULL;
gchar *session=NULL;
gchar *language=NULL;

gboolean savesess;
gboolean savelang;


/* Log debug messages */
void 
static gdm_debug(const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if(GdmDebug) {
	va_start(args, format);
	s=g_strdup_vprintf(format, args);
	va_end(args);
    
	syslog(LOG_DEBUG, s);
    
	g_free(s);
    }
}


static void
gdm_login_done(void)
{
    closelog();
    gtk_main_quit();
    exit(DISPLAY_SUCCESS);
}


typedef struct _cursoroffset {gint x,y;} CursorOffset;


static void
gdm_login_icon_pressed(GtkWidget *widget, GdkEventButton *event)
{
  CursorOffset *p;

  if(event->type == GDK_2BUTTON_PRESS) {
      gtk_widget_destroy(win);
      gdk_window_show(login->window);
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
gdm_login_icon_released(GtkWidget *widget)
{
  gtk_grab_remove(widget);
  gdk_pointer_ungrab(0);
}


static void
gdm_login_icon_motion(GtkWidget *widget, GdkEventMotion *event)
{
  gint xp, yp;
  CursorOffset *p;
  GdkModifierType mask;

  p=gtk_object_get_user_data(GTK_OBJECT (widget));
  gdk_window_get_pointer(rootwin, &xp, &yp, &mask);
  gtk_widget_set_uposition(widget, xp-p->x, yp-p->y);
}


static gboolean
gdm_login_iconify_handler(GtkWidget *widget, gpointer data)
{
    GtkWidget *fixed;
    GtkWidget *icon;
    GdkGC *gc;
    GtkStyle *style;
    CursorOffset *icon_pos;
    gint rw, rh, iw, ih;

    gdk_window_hide(login->window);
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
		       GTK_SIGNAL_FUNC (gdm_login_icon_pressed),NULL);
    gtk_signal_connect(GTK_OBJECT (win), "button_release_event",
		       GTK_SIGNAL_FUNC (gdm_login_icon_released),NULL);
    gtk_signal_connect(GTK_OBJECT (win), "motion_notify_event",
		       GTK_SIGNAL_FUNC (gdm_login_icon_motion),NULL);

    icon_pos=g_new(CursorOffset, 1);
    gtk_object_set_user_data(GTK_OBJECT(win), icon_pos);

    gtk_widget_show(win);

    rw=gdk_screen_width();
    rh=gdk_screen_height();

    gtk_widget_set_uposition(win, rw-iw, rh-ih);

    return(TRUE);
}


static void
gdm_login_abort(const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start(args, format);
    s=g_strdup_vprintf(format, args);
    va_end(args);
    
    syslog(LOG_ERR, s);
    closelog();
    exit(DISPLAY_ABORT);
}


static gchar *
gdm_parse_enriched_string(gchar *s)
{
    gchar cmd, *buffer, *start;
    gchar hostbuf[256];
    gchar *hostname, *temp1, *temp2, *display;
    struct utsname name;

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

    uname(&name);

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

	    case 's':
	        memcpy(buffer, name.sysname, strlen(name.sysname));
		buffer+=strlen(name.sysname);
		break;
		
	    case 'r':
	        memcpy(buffer, name.release, strlen(name.release));
	        buffer+=strlen(name.release);
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
gdm_login_about(void)
{
    const gchar *authors[]={"Martin Kasper Petersen <mkp@mkp.net>", NULL};
    
    GtkWidget *about;
    
    about=gnome_about_new("Gnome Display Manager", 
			  "" VERSION "",
			  "Copyright Martin K. Petersen (C) 1998, 1999",
			  authors,
			  _("gdm manages local and remote displays and provides the user with a login window."),
			  NULL);
    
    gtk_widget_show(about);                
}


static gboolean
gdm_login_query(gchar *msg)
{
    GtkWidget *req;

    req=gnome_message_box_new(msg,
			      GNOME_MESSAGE_BOX_QUESTION,
			      GNOME_STOCK_BUTTON_YES,
			      GNOME_STOCK_BUTTON_NO,
			      NULL);
	    
    gtk_window_set_modal(GTK_WINDOW(req), TRUE);
    gtk_window_set_position(GTK_WINDOW(req), GTK_WIN_POS_CENTER);

    return(!gnome_dialog_run(GNOME_DIALOG(req)));
}


static gboolean
gdm_login_reboot_handler(void)
{
    if(gdm_login_query(_("Are you sure you want to reboot the machine?"))) {
	closelog();
	exit(DISPLAY_REBOOT);
    }

    return(TRUE);
}


static gboolean
gdm_login_halt_handler(void)
{
    if(gdm_login_query(_("Are you sure you want to halt the machine?"))) {
	closelog();
	exit(DISPLAY_HALT);
    }

    return(TRUE);
}


static void 
gdm_login_parse_config(void)
{
    gchar *display;
    struct stat unused;
	
    if(stat(GDM_CONFIG_FILE, &unused) == -1)
	gdm_login_abort(_("gdm_login_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmLogoFilename=gnome_config_get_string("appearance/logoimage");
    GdmMessageFont=gnome_config_get_string("appearance/msgfont=-adobe-helvetica-bold-r-normal-*-*-180-*-*-*-*-*-*");
    GdmGtkRC=gnome_config_get_string("appearance/gtkrc");
    GdmIconify=gnome_config_get_int("appearance/iconify=1");
    GdmIconFile=gnome_config_get_string("appearance/iconfile=gdm.xpm");
    GdmQuiver=gnome_config_get_int("appearance/quiver=1");

    GdmChooserMenu=gnome_config_get_int("system/choosermenu=0");
    GdmShutdownMenu=gnome_config_get_int("system/shutdownmenu=0");
    GdmUserMaxFile=gnome_config_get_int("system/UserFileCutoffSize=65536");
    GdmRelaxPerms=gnome_config_get_int("system/relaxpermissions=0");

    GdmLocaleFile=g_strdup(gnome_config_get_string("system/LocaleFile"));
    GdmDefaultLocale=g_strdup(gnome_config_get_string("system/DefaultLocale=english"));
    GdmSessionDir=gnome_config_get_string("daemon/sessiondir");

    GdmMessageText=gnome_config_get_string(_("messages/welcome=Welcome to %h"));

    GdmDebug=gnome_config_get_int("debug/enable=0");

    gnome_config_pop_prefix ();

    if(stat(GdmLocaleFile, &unused) == -1)
	gdm_login_abort("gdm_login_parse_config: Could not open locale file %s. Aborting!", GdmLocaleFile);

    /* Disable System menu on non-local displays */
    display=getenv("DISPLAY");

    if(!display)
	gdm_login_abort("gdm_login_parse_config: DISPLAY variable not set!");

    if(strncmp(display, ":", 1))
	GdmShutdownMenu=0;
}


static gboolean 
gdm_login_list_lookup(GSList *l, gchar *data)
{
    GSList *list=l;

    if(!list || !data)
	return(FALSE);

    while(list) {

	if(!strcasecmp(list->data, data))
	    return(TRUE);
	
	list=list->next;
    }

    return(FALSE);
}


static void
gdm_login_sesslang_lookup(void)	/* Input validation sucks */
{
    struct passwd *pwent;
    gboolean fileok;
    gchar msg[1024];
    gchar *dir=NULL, *cfg=NULL;
    gchar *usrlang=NULL, *usrsess=NULL;

    gtk_widget_set_sensitive(sessmenu, FALSE);
    gtk_widget_set_sensitive(langmenu, FALSE);

    /* Lookup verified user */
    pwent=getpwnam(curuser);

    dir=g_strconcat(pwent->pw_dir, "/.gnome", NULL);
    fileok=gdm_file_check("gdm_login_sesslang_lookup", pwent->pw_uid, dir, "gdm", TRUE);
    g_free(dir);

    if(!fileok) { /* User's settings can't be retrieved */
	
	/* If cursess==last use system default session */
	if(!strcasecmp(cursess, lastsess))
	    session=defsess;
	else
	    session=cursess;

	/* If curlang==last use default */
	if(!strcasecmp(curlang, lastlang))
	    language=GdmDefaultLocale;
	else
	    language=curlang;

	savesess=FALSE;
	savelang=FALSE;

	return;
    }

    /* Find user's last session and language if available */
    cfg=g_strconcat("=", pwent->pw_dir, "/.gnome/gdm=/session/last", NULL);
    usrsess=gnome_config_get_string(cfg);
    g_free(cfg);

    cfg=g_strconcat("=", pwent->pw_dir, "/.gnome/gdm=/session/lang", NULL);
    usrlang=gnome_config_get_string(cfg);
    g_free(cfg);

    /* If ``Last'' session is selected */
    if(!strcasecmp(cursess, lastsess)) { 

	/* User has no saved session. Use default. */
	if(!usrsess) {
	    session=defsess;
	    savesess=TRUE;
	}
	/* User's saved session exists on this box */
	else if(gdm_login_list_lookup(sessions, usrsess)) {
	    session=usrsess;
	    savesess=FALSE;
	}
	/* User's saved session type unknown */
	else {
	    session=defsess;
	    g_snprintf(msg, 1023, 
		       _("Your preferred session type %s is not installed on this machine.\n" \
			 "Do you wish to make %s the default for future sessions?"),
		       usrsess, defsess);	    
	    savesess=gdm_login_query(msg);
	}
    }
    /* One of the other available session types is selected */
    else { 
	session=cursess;

	/* User has no saved session type. Use current */
	if(!usrsess)
	    savesess=TRUE;
	/* User's saved session is also the chosen one */
	else if(!strcasecmp(usrsess, cursess)) 
	    savesess=FALSE;
	/* User selected a new session type */
	else {
	    g_snprintf(msg, 1023, 
		       _("You have chosen %s for this session, but your default setting is %s.\n" \
			 "Do you wish to make %s the default for future sessions?"),
		       cursess, usrsess, cursess);
	    savesess=gdm_login_query(msg);
	}
    }

    /* If ``Last'' language is selected */
    if(!strcasecmp(curlang, lastlang)) { 

	/* User has no saved language. Use default. */
	if(!usrlang) {
	    language=GdmDefaultLocale;
	    savelang=TRUE;
	}
	else {
	    language=usrlang;
	    savelang=FALSE;
	}
    }
    /* One of the available languages is selected */
    else { 
	language=curlang;

	/* User has no saved language. Use current */
	if(!usrlang)
	    savelang=TRUE;
	/* User selected a new language */
	else {
	    g_snprintf(msg, 1023, 
		       _("You have chosen %s for this session, but your default setting is %s.\n" \
			 "Do you wish to make %s the default for future sessions?"),
		       curlang, usrlang, curlang);
	    savelang=gdm_login_query(msg);
	}
    }

    return;
}


static gboolean
gdm_login_entry_handler (GtkWidget *widget, GdkEventKey *event)
{
    if(!event)
	return(TRUE);

    switch(event->keyval) {

    case GDK_Return:
	gtk_widget_set_sensitive(entry, FALSE);

	/* Save login. I'm making the assumption that login is always
	 * the first thing entered. This might not be true for all PAM
	 * setups. Needs thinking! 
	 */

	if(!curuser)
	    curuser=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

	g_print("%s\n", gtk_entry_get_text(GTK_ENTRY(entry)));
	break;

    case GDK_Up:
    case GDK_Down:
    case GDK_Tab:
	gtk_signal_emit_stop_by_name(GTK_OBJECT(entry), "key_press_event");
	break;

	break;

    case GDK_F1:
	gdm_login_about();
	break;

    default:
	break;
    }

    return(TRUE);
}


static void 
gdm_login_session_handler (GtkWidget *widget) 
{
    gchar *s;

    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &cursess);
    s=g_strdup_printf(_("%s session selected"), cursess);
    gtk_label_set(GTK_LABEL(msg), s);
    g_free(s);
}


static void 
gdm_login_session_init(GtkWidget *menu)
{
    GSList *sessgrp=NULL;
    GtkWidget *item;
    DIR *sessdir;
    struct dirent *dent;
    struct stat statbuf;
    gint linklen;

    lastsess=_("Last");

    cursess=lastsess;
    item = gtk_radio_menu_item_new_with_label(NULL, lastsess);
    sessgrp = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_signal_connect(GTK_OBJECT(item), "activate", gdm_login_session_handler, NULL);
    gtk_widget_show(item);

    item = gtk_menu_item_new();
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);

    /* Check that session dir is readable */
    if (access(GdmSessionDir, R_OK|X_OK))
	gdm_login_abort(_("gdm_login_session_init: Session script directory not found!"));

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
		item=gtk_radio_menu_item_new_with_label(sessgrp, dent->d_name);
		sessgrp=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM (item));
		sessions=g_slist_append(sessions, dent->d_name);
		gtk_menu_append(GTK_MENU(menu), item);
		gtk_signal_connect(GTK_OBJECT (item), "activate",
				   GTK_SIGNAL_FUNC (gdm_login_session_handler), NULL);
		gtk_widget_show(item);
	    }
	    else 
		syslog(LOG_ERR, "Wrong permissions on %s/%s. Should be readable/executable for all.", 
		       GdmSessionDir, dent->d_name);

	}

	dent=readdir(sessdir);
	g_free(s);
    }

    if(!g_slist_length(sessgrp)) 
	gdm_login_abort(_("No session scripts found. Aborting!"));

    if(!defsess) {
	gtk_label_get(GTK_LABEL(GTK_BIN(g_slist_nth_data(sessgrp, 0))->child), &defsess);
	syslog(LOG_WARNING, _("No default session link found. Using %s.\n"), defsess);
    }
}


static void 
gdm_login_language_handler (GtkWidget *widget) 
{
    gchar *s;

    gtk_label_get(GTK_LABEL(GTK_BIN(widget)->child), &curlang);
    s=g_strdup_printf(_("%s language selected"), curlang);
    gtk_label_set(GTK_LABEL(msg), s);
    g_free(s);
}


static void
gdm_login_language_init(GtkWidget *menu)
{
    GtkWidget *item, *ammenu, *nzmenu, *omenu;
    FILE *langlist;
    char curline[256];
    char *ctmp, *ctmp1, *ctmp2;

    lastlang=_("Last");
    curlang=lastlang;

    item=gtk_radio_menu_item_new_with_label(NULL, lastlang);
    languages=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_signal_connect(GTK_OBJECT(item), "activate", gdm_login_language_handler, NULL);
    gtk_widget_show(item);

    item=gtk_menu_item_new();
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);

    item=gtk_menu_item_new_with_label(_("A-M"));
    ammenu=gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), ammenu);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);

    item=gtk_menu_item_new_with_label(_("N-Z"));
    nzmenu=gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), nzmenu);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);

    item=gtk_menu_item_new_with_label(_("Other"));
    omenu=gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), omenu);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);

    langlist = fopen(GdmLocaleFile, "r");

    if(!langlist)
	return;
        
    while(fgets(curline, sizeof(curline), langlist)) {

	if(!isalpha(curline[0])) 
	    continue;
	
	ctmp1 = strchr(curline, ' ');
	ctmp2 = strchr(curline, '\t');
	ctmp = curline + strlen(curline) - 1;

	if(ctmp1 && (ctmp1 < ctmp))
	    ctmp = ctmp1;

	if(ctmp2 && (ctmp2 < ctmp))
	    ctmp = ctmp2;

	*ctmp = '\0';
	curline[0] = toupper(curline[0]);
	
	item=gtk_radio_menu_item_new_with_label(languages, curline);
	languages=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));

	if(curline[0] >= 'A' && curline[0] <= 'M')
	    gtk_menu_append(GTK_MENU(ammenu), item);
	else if(curline[0] >= 'N' && curline[0] <= 'Z')
	    gtk_menu_append(GTK_MENU(nzmenu), item);
	else
	    gtk_menu_append(GTK_MENU(omenu), item);

	gtk_signal_connect(GTK_OBJECT(item), "activate", gdm_login_language_handler, NULL);
	gtk_widget_show(item);
    }
    
    fclose(langlist);
}


static gboolean
gdm_login_ctrl_handler(GIOChannel *source, GIOCondition cond, gint fd)
{
    gchar buf[PIPE_SIZE];
    gint len;
    gint i, x, y;

    if(cond != G_IO_IN) 
	return(TRUE);

    g_io_channel_read(source, buf, 1, &len);

    if(len!=1)
	return(TRUE);

    switch(buf[0]) {
    case GDM_PROMPT:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len);
	buf[len-1]='\0';
	gtk_label_set(GTK_LABEL(label), buf);
	gtk_widget_show(label);
	gtk_entry_set_text(GTK_ENTRY(entry), "");
	gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);
	gtk_widget_set_sensitive(entry, TRUE);
	gtk_widget_grab_focus(entry);	
	gtk_widget_show(entry);
	break;

    case GDM_NOECHO:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len);
	buf[len-1]='\0';
	gtk_label_set(GTK_LABEL(label), buf);
	gtk_widget_show(label);
	gtk_entry_set_text(GTK_ENTRY(entry), "");
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	gtk_widget_set_sensitive(entry, TRUE);
	gtk_widget_grab_focus(entry);	
	gtk_widget_show(entry);
	break;

    case GDM_MSGERR:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len);
	buf[len-1]='\0';
	gtk_label_set(GTK_LABEL(msg), buf);
	gtk_widget_show(msg);
	g_print("\n");
	break;

    case GDM_SESS:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len); /* Empty */
	gdm_login_sesslang_lookup(); /* Lookup session and language */
	g_print("%s\n", session);
	break;

    case GDM_LANG:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len); /* Empty */
	g_print("%s\n", language);
	break;

    case GDM_SSESS:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len); /* Empty */

	if(savesess)
	    g_print("Y\n");
	else
	    g_print("\n");
	
	break;

    case GDM_SLANG:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len); /* Empty */

	if(savelang)
	    g_print("Y\n");
	else
	    g_print("\n");

	break;

    case GDM_RESET:
	g_io_channel_read(source, buf, PIPE_SIZE-1, &len);
	buf[len-1]='\0';

	if(GdmQuiver) {
	    gdk_window_get_position(login->window, &x, &y);
	    
	    for (i=32 ; i > 0 ; i=i/2) {
		gdk_window_move(login->window, i+x, y);
		gdk_window_move(login->window, x, y);
		gdk_window_move(login->window, -i+x, y);
		gdk_window_move(login->window, x, y);
	    }
	}

	if(curuser) {
	    g_free(curuser);
	    curuser=NULL;
	}

	g_print("\n");
	break;

    case GDM_QUIT:
	exit(EXIT_SUCCESS);
	break;
	
    default:
	break;
    }

    return(TRUE);
}


static void
gdm_login_gui_init(void)
{
    GtkStyle *style;
    GtkWidget *frame, *frame2, *hbox, *hline, *item, *lebox, *logo, *logobox;
    GtkWidget *logoframe, *mbox, *menu, *menubar, *vbox, *welcome;
    gchar *greeting;

    if(GdmGtkRC)
	gtk_rc_parse(GdmGtkRC);

    rootwin=gdk_window_foreign_new (GDK_ROOT_WINDOW ());

    login=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_border_width(GTK_CONTAINER(login), 0);

    frame=gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
    gtk_container_border_width(GTK_CONTAINER(frame), 0);
    gtk_container_add(GTK_CONTAINER(login), frame);
    gtk_widget_show(frame);

    frame2=gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame2), GTK_SHADOW_IN);
    gtk_container_border_width(GTK_CONTAINER(frame2), 2);
    gtk_container_add(GTK_CONTAINER(frame), frame2);
    gtk_widget_show(frame2);

    mbox=gtk_vbox_new(FALSE,0);
    gtk_container_add(GTK_CONTAINER(frame2), mbox);
    gtk_widget_show(mbox);

    menubar=gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(mbox), menubar, FALSE, FALSE, 0);

    menu=gtk_menu_new();
    gdm_login_session_init(menu);
    sessmenu=gtk_menu_item_new_with_label(_("Session"));
    gtk_menu_bar_append(GTK_MENU_BAR(menubar), sessmenu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(sessmenu), menu);
    gtk_widget_show(sessmenu);

    menu=gtk_menu_new();
    gdm_login_language_init(menu);
    langmenu=gtk_menu_item_new_with_label(_("Language"));
    gtk_menu_bar_append(GTK_MENU_BAR(menubar), langmenu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(langmenu), menu);
    gtk_widget_show(langmenu);

    if(GdmShutdownMenu) {
	menu=gtk_menu_new();
	item=gtk_menu_item_new_with_label(_("Reboot..."));
	gtk_menu_append(GTK_MENU(menu), item);
	gtk_signal_connect(GTK_OBJECT (item), "activate",
			   GTK_SIGNAL_FUNC(gdm_login_reboot_handler), NULL);
	gtk_widget_show(item);
	
	item=gtk_menu_item_new_with_label(_("Halt..."));
	gtk_menu_append(GTK_MENU(menu), item);
	gtk_signal_connect(GTK_OBJECT(item), "activate",
			   GTK_SIGNAL_FUNC(gdm_login_halt_handler), NULL);
	gtk_widget_show(item);
	
	item=gtk_menu_item_new_with_label(_("System"));
	gtk_menu_bar_append(GTK_MENU_BAR(menubar), item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
	gtk_widget_show(item);
    }

    if(GdmChooserMenu) {
	item=gtk_menu_item_new_with_label(_("Host chooser"));
	gtk_menu_bar_append(GTK_MENU_BAR(menubar), item);
	gtk_widget_show(item);
    }

    if(GdmIconify) {
	if(access(GdmIconFile, R_OK)) {
	    syslog(LOG_WARNING, _("Can't open icon file: %s. Suspending iconify feature!"), GdmIconFile);
	    GdmIconify=0;
	}
	else {
	    item=gtk_menu_item_new_with_label(_("Iconify"));
	    gtk_menu_bar_append(GTK_MENU_BAR(menubar), item);
	    gtk_signal_connect(GTK_OBJECT(item), "activate",
			       GTK_SIGNAL_FUNC(gdm_login_iconify_handler), NULL);
	    gtk_widget_show(item);
	}
    }

    hbox=gtk_hbox_new(FALSE, 0);
    gtk_container_border_width(GTK_CONTAINER(hbox), 10);
    gtk_box_pack_end(GTK_BOX(mbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    if (GdmLogoFilename && !access(GdmLogoFilename, R_OK)) {
	logobox=gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), logobox, FALSE, FALSE, 0);

	logoframe=gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(logoframe), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(logobox), logoframe, FALSE, FALSE, 0);
	gtk_widget_show(logoframe);

	logo=gnome_pixmap_new_from_file(GdmLogoFilename);
	gtk_container_add(GTK_CONTAINER(logoframe), logo);
	gtk_widget_show(logo);

	gtk_widget_show(logobox);
    }

    vbox=gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    gtk_container_border_width(GTK_CONTAINER(vbox), 10);
    gtk_widget_show(vbox);

    style = gtk_style_new();
    gdk_font_unref(style->font);
    style->font = gdk_font_load (GdmMessageFont);
    gtk_widget_push_style(style);
    greeting=gdm_parse_enriched_string(GdmMessageText);
    welcome=gtk_label_new(greeting);
    g_free(greeting);
    gtk_box_pack_start(GTK_BOX(vbox), welcome, TRUE, FALSE, 10);
    gtk_widget_show(welcome);
    gtk_widget_pop_style();

    msg=gtk_label_new("Please enter your login");
    gtk_box_pack_end(GTK_BOX(vbox), msg, FALSE, TRUE, 0);
    gtk_widget_show(msg);

    hline=gtk_hseparator_new();
    gtk_box_pack_end(GTK_BOX(vbox), hline, FALSE, FALSE, 10);
    gtk_widget_show(hline);

    lebox=gtk_vbox_new(FALSE, 0);
    gtk_container_border_width(GTK_CONTAINER(lebox), 5);
    gtk_box_pack_end(GTK_BOX(vbox), lebox, FALSE, FALSE, 5);    
    gtk_widget_show(lebox);

    label=gtk_label_new("Login:");
    gtk_box_pack_start(GTK_BOX(lebox), label, FALSE, FALSE, 5);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);

    entry=gtk_entry_new_with_max_length(32);
    gtk_box_pack_start(GTK_BOX(lebox), entry, FALSE, FALSE, 5);
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    gtk_signal_connect_object(GTK_OBJECT(entry), 
			      "key_press_event", 
			      GTK_SIGNAL_FUNC(gdm_login_entry_handler),
			      NULL);
    gtk_widget_show(entry);

    hline=gtk_hseparator_new();
    gtk_box_pack_end(GTK_BOX(vbox), hline, FALSE, FALSE, 10);
    gtk_widget_show(hline);

    gtk_widget_grab_focus(entry);	
    gtk_window_set_focus(GTK_WINDOW (login), entry);	
    gtk_window_set_policy(GTK_WINDOW (login), 1, 1, 1);
    gtk_window_position(GTK_WINDOW (login), GTK_WIN_POS_CENTER);

    gtk_widget_set_usize(GTK_WIDGET(login), 
			 (gint)gdk_screen_width() * 0.5, 
			 0);

    gtk_widget_show_all(login);
}


int 
main (int argc, char *argv[])
{
    struct sigaction hup;
    sigset_t mask;
    GIOChannel *ctrlch;

    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog("gdmlogin", LOG_PID, LOG_DAEMON);

    gnome_init("gdmlogin", VERSION, argc, argv);
    gnome_sound_shutdown ();
    
    gdm_login_parse_config();
    gdm_login_gui_init();

    hup.sa_handler = (void *) gdm_login_done;
    hup.sa_flags = 0;
    sigemptyset(&hup.sa_mask);

    if(sigaction(SIGHUP, &hup, NULL) < 0) 
        gdm_login_abort(_("main: Error setting up HUP signal handler"));

    if(sigaction(SIGINT, &hup, NULL) < 0) 
        gdm_login_abort(_("main: Error setting up INT signal handler"));

    if(sigaction(SIGTERM, &hup, NULL) < 0) 
        gdm_login_abort(_("main: Error setting up TERM signal handler"));

    sigfillset(&mask);
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGHUP);
    sigdelset(&mask, SIGINT);
    
    if(sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
	syslog(LOG_ERR, "Could not set signal mask!");
	exit(EXIT_FAILURE);
    }

    ctrlch = g_io_channel_unix_new(STDIN_FILENO);
    g_io_channel_init(ctrlch);
    g_io_add_watch(ctrlch, 
		   G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		   (GIOFunc) gdm_login_ctrl_handler,
		   NULL);
    g_io_channel_unref(ctrlch);

    gtk_main();

    exit(EXIT_SUCCESS);
}

/* EOF */
