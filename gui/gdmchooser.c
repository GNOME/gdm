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

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xdmcp.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <pwd.h>
#include <syslog.h>
#include <ctype.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "gdmchooser.h"

static const gchar RCSid[]="$Id$";

static void gdm_chooser_decode_packet(void);
static GdmChooserHost *gdm_chooser_host_alloc (gchar *hostname, gchar *description);
static void gdm_chooser_abort(const gchar *format, ...);
static void gdm_chooser_browser_update(void);
static void gdm_chooser_xdmcp_init(void);
static void gdm_chooser_host_dispose(GdmChooserHost *host);

int XdmcpReallocARRAY8 (ARRAY8Ptr array, int length);


typedef struct _XdmAuth {
    ARRAY8  authentication;
    ARRAY8  authorization;
} XdmAuthRec, *XdmAuthPtr;

static XdmAuthRec authlist = { 
    { (CARD16)  0, (CARD8 *) 0 },
    { (CARD16)  0, (CARD8 *) 0 }
};


gint sockfd;
static XdmcpBuffer querybuf;
GSList *bcaddr;

gint  GdmIconMaxHeight;
gint  GdmIconMaxWidth;
gint  GdmDebug;
gint  GdmRescanTime;
gint  GdmDeadTime;
gchar *GdmHostIconDir;
gchar *GdmHostDefaultIcon;
gchar *GdmGtkRC;

GtkWidget *chooser;
GtkWidget *manage;
GtkWidget *rescan;
GtkWidget *cancel;

gint maxwidth=0;
guint tid;
GIOChannel *channel;

GList *hosts=NULL;
GdkImlibImage *nohostimg;
GnomeIconList *browser;

GdmChooserHost *curhost;


static gint 
gdm_chooser_sort_func(gpointer d1, gpointer d2)
{
    GdmChooserHost *a=d1;
    GdmChooserHost *b=d2;

    return strcmp(a->name, b->name);
}


static void
gdm_chooser_decode_packet(void)
{
    struct sockaddr_in clnt_sa;
    gint sa_len=sizeof(clnt_sa);
    static XdmcpBuffer buf;
    XdmcpHeader header;
    struct hostent *he;
    gchar *hostname, *status=NULL;
    ARRAY8 auth, host, stat;

    if(!XdmcpFill(sockfd, &buf, &clnt_sa, &sa_len))
	return;

    if(!XdmcpReadHeader(&buf, &header))
	return;
    
    if(header.version != XDM_PROTOCOL_VERSION)
	return;

    if(header.opcode == WILLING) {
	if(!XdmcpReadARRAY8(&buf, &auth))
	    goto done;

	if(!XdmcpReadARRAY8(&buf, &host))
	    goto done;

	if(!XdmcpReadARRAY8(&buf, &stat))
	    goto done;

	status=g_strndup(stat.data, stat.length);

	he=gethostbyaddr((gchar *) &clnt_sa.sin_addr, 
			 sizeof(struct in_addr), 
			 AF_INET);

	hostname=(he && he->h_name) ? he->h_name : inet_ntoa(clnt_sa.sin_addr);

	hosts=g_list_insert_sorted(hosts, 
				   gdm_chooser_host_alloc(hostname, (gchar *) status),
				   (GCompareFunc) gdm_chooser_sort_func);

    done:
	XdmcpDisposeARRAY8(&auth);
	XdmcpDisposeARRAY8(&host);
	XdmcpDisposeARRAY8(&stat);

	g_free(status);

	return;
    }

}


/* Find broadcast address for all active, non pointopoint interfaces */
static void
gdm_chooser_find_bcaddr(void)
{
    gint i=0;
    struct ifconf ifcfg;
    struct ifreq ifr[MAXIF];
    struct in_addr *ia;

    ifcfg.ifc_buf=(gchar *)&ifr;
    ifcfg.ifc_len=sizeof(ifr);

    memset(ifr, 0, sizeof(ifr));

    if(ioctl(sockfd, SIOCGIFCONF, &ifcfg) < 0) 
	gdm_chooser_abort("Could not get SIOCIFCONF");

    for(i=0 ; i < MAXIF ; i++) 
	if(strlen(ifr[i].ifr_name)) {
	    struct ifreq *ifreq;
	    struct sockaddr_in *ba;

	    ifreq=g_new0(struct ifreq, 1);

	    strncpy(ifreq->ifr_name, ifr[i].ifr_name, sizeof(ifr[i].ifr_name));

	    if(ioctl(sockfd, SIOCGIFFLAGS, ifreq) < 0) 
		gdm_chooser_abort("Could not get SIOCGIFFLAGS for %s", ifr[i].ifr_name);

	    if((ifreq->ifr_flags & IFF_UP) == 0)
		goto done;

	    if((ifreq->ifr_flags & IFF_BROADCAST) == 0)
		goto done;

	    if(ioctl(sockfd, SIOCGIFBRDADDR, ifreq) < 0) {
		goto done;
	    }

	    ba=(struct sockaddr_in *)&ifreq->ifr_broadaddr;

	    ia=g_new0(struct in_addr, 1);

	    ia->s_addr = ba->sin_addr.s_addr;

	    bcaddr=g_slist_append(bcaddr, ia);

	done:
	    g_free(ifreq);
	}
}


static gboolean 
gdm_chooser_xdmcp_discover(void)
{
    struct sockaddr_in sock;
    GSList *bl=bcaddr;
    struct in_addr *ia;
    GList *hl=hosts;

    gtk_widget_set_sensitive (chooser, FALSE);
    gnome_icon_list_freeze(browser);
    gnome_icon_list_clear(browser);

    while(hl) {
	gdm_chooser_host_dispose((GdmChooserHost *) hl->data);
	hl=hl->next;
    }

    g_list_free(hosts);

    hosts=NULL;

    sock.sin_family = AF_INET;
    sock.sin_port = htons (XDM_UDP_PORT);

    while(bl) {
	ia = (struct in_addr *)bl->data;

	sock.sin_addr.s_addr = ia->s_addr; 
	XdmcpFlush (sockfd, &querybuf, &sock, sizeof(struct sockaddr_in));
	bl=bl->next;
    }

    tid=g_timeout_add(GdmRescanTime*1000, 
		      (GSourceFunc) gdm_chooser_browser_update, NULL);

    return(TRUE);
}


static void
gdm_chooser_xdmcp_init(void)
{
    static XdmcpHeader header;
    gint sockopts=1;

    /* Open socket for communication */
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	gdm_chooser_abort("Could not create socket()!");

    if(setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&sockopts, sizeof (sockopts)) < 0)
	gdm_chooser_abort("Could not set socket options!");

    gdm_chooser_find_bcaddr();

    /* Assemble XDMCP BROADCAST_QUERY packet in static buffer */
    header.opcode  = (CARD16) BROADCAST_QUERY;
    header.length  = 1;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader(&querybuf, &header);
    XdmcpWriteARRAY8(&querybuf, &authlist.authentication);

    channel = g_io_channel_unix_new(sockfd);
    g_io_add_watch_full(channel, G_PRIORITY_DEFAULT,
			G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
			(GIOFunc) gdm_chooser_decode_packet,
			GINT_TO_POINTER(sockfd), NULL);
    g_io_channel_unref(channel);

    gdm_chooser_xdmcp_discover();
}


static gboolean
gdm_chooser_cancel(void)
{
    closelog();
    gtk_main_quit();

    return(TRUE);
}


static gboolean
gdm_chooser_manage(void)
{
    if(curhost)
	g_print("%s\n", curhost->name);

    closelog();
    gtk_main_quit();

    return(TRUE);
}


static void
gdm_chooser_abort(const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start(args, format);
    s=g_strdup_vprintf(format, args);
    va_end(args);

    syslog(LOG_ERR, s);

    closelog();
    exit(EXIT_FAILURE);
}


static void 
gdm_chooser_parse_config(void)
{
    struct stat unused;
	
    if(stat(GDM_CONFIG_FILE, &unused) == -1)
	gdm_chooser_abort(_("gdm_chooser_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmGtkRC=gnome_config_get_string("appearance/gtkrc");

    GdmRescanTime=gnome_config_get_int("chooser/rescantime=3");
    GdmDeadTime=gnome_config_get_int("chooser/deadtime=3");
    GdmHostDefaultIcon=gnome_config_get_string("chooser/defaultimage=nohost.xpm");
    GdmHostIconDir=gnome_config_get_string("chooser/imagedir");

    GdmIconMaxWidth=gnome_config_get_int("system/UserIconMaxWidth=128");
    GdmIconMaxHeight=gnome_config_get_int("system/UserIconMaxHeight=128");

    GdmDebug=gnome_config_get_int("debug/enable=0");

    gnome_config_pop_prefix ();
}


static gboolean 
gdm_chooser_browser_select(GtkWidget *widget, gint selected, GdkEvent *event)
{
    if(!event)
	return(TRUE);
    else
	switch(event->type) {
	    
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	    curhost=g_list_nth_data(hosts, selected);
	    gtk_widget_set_sensitive (manage, TRUE);
	    break;
	    
	default: 
	    break;
	}

    return(TRUE);
}


static gboolean
gdm_chooser_browser_unselect(GtkWidget *widget, gint selected, GdkEvent *event)
{
    if(event) {
	switch(event->type) {
	    
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	    curhost=NULL;
	    gtk_widget_set_sensitive (manage, FALSE);
	    break;

	default:
 	    break;
	}
    }

    return(TRUE);
}


static void
gdm_chooser_browser_update(void)
{
    GList *list=hosts;

    g_source_remove(tid);

    while(list) {
	GdmChooserHost *host;
	gchar *temp;

	host=(GdmChooserHost *)list->data;

	temp=g_strconcat(host->name, "\n", host->desc, NULL);
	gnome_icon_list_append_imlib(browser, host->picture, temp);
	g_free(temp);

	list=list->next;
    }

    gnome_icon_list_thaw(browser);

    gtk_widget_set_sensitive(chooser, TRUE);
    gtk_widget_set_sensitive(manage, FALSE);
    gtk_widget_show_all(chooser);
}


static void 
gdm_chooser_gui_init (void)
{
    GdkWindow *rootwin;
    GtkWidget *frame;
    GtkWidget *vbox;
    GtkWidget *bbox;
    GtkWidget *buttonpane;
    GtkWidget *scrollbar;
    GtkWidget *bframe;
    GtkAdjustment *adj;
    GtkStyle  *style;
    GdkColor  bbg = { 0, 0xFFFF, 0xFFFF, 0xFFFF };
    struct    stat statbuf;
 
    /* Enable theme */
    if(GdmGtkRC)
	gtk_rc_parse(GdmGtkRC);

    /* Load default host image */
    if(stat(GdmHostDefaultIcon, &statbuf))
	gdm_chooser_abort(_("Can't open default host icon: %s"), GdmHostDefaultIcon);
    else {
	nohostimg=gdk_imlib_load_image(GdmHostDefaultIcon);
	maxwidth=nohostimg->rgb_width;
    }

    /* Root Window */
    rootwin=gdk_window_foreign_new (GDK_ROOT_WINDOW ());

    /* Main window */
    chooser = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_border_width (GTK_CONTAINER (chooser), 0);

    /* 3D frame for main window */
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_OUT);
    gtk_container_border_width (GTK_CONTAINER (frame), 0);
    gtk_container_add(GTK_CONTAINER (chooser), frame);

    /* Vertical box containing browser box and button pane */
    vbox = gtk_vbox_new(FALSE, 10);
    gtk_container_border_width (GTK_CONTAINER (vbox), 10);
    gtk_container_add(GTK_CONTAINER (frame), vbox);

    /* Find background style for browser */
    style = gtk_style_copy (chooser->style);
    style->bg[GTK_STATE_NORMAL] = bbg;
    gtk_widget_push_style(style);

    /* Icon list */
    if(maxwidth < GdmIconMaxWidth/2)
	maxwidth=(gint) GdmIconMaxWidth/2;

    browser = GNOME_ICON_LIST (gnome_icon_list_new (maxwidth+20, NULL, FALSE));
    gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
    gnome_icon_list_set_separators (browser, " /-_.");
    gnome_icon_list_set_row_spacing (browser, 2);
    gnome_icon_list_set_col_spacing (browser, 2);
    gnome_icon_list_set_icon_border (browser, 2);
    gnome_icon_list_set_text_spacing (browser, 2);
    gnome_icon_list_set_selection_mode (browser, GTK_SELECTION_SINGLE);
    gtk_signal_connect (GTK_OBJECT (browser), "select_icon",
			GTK_SIGNAL_FUNC (gdm_chooser_browser_select), NULL);
    gtk_signal_connect (GTK_OBJECT (browser), "unselect_icon",
			GTK_SIGNAL_FUNC (gdm_chooser_browser_unselect), NULL);
    gtk_widget_pop_style();

    /* Browser 3D frame */
    bframe = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (bframe), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(bframe), GTK_WIDGET(browser));

    /* Browser scroll bar */
    adj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    scrollbar = gtk_vscrollbar_new (adj);
    gnome_icon_list_set_vadjustment (browser, adj);

    /* Box containing all browser functionality */
    bbox = gtk_hbox_new (0, 0);
    gtk_box_pack_start (GTK_BOX (bbox), GTK_WIDGET (bframe), 1, 1, 0);
    gtk_box_pack_start (GTK_BOX (bbox), scrollbar, 0, 0, 0);
    gtk_widget_show_all (GTK_WIDGET (bbox));

    /* Put browser box in main window */
    gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (bbox), TRUE, TRUE, 0);

    /* Buttons */
    manage = gtk_button_new_with_label(_("Connect"));
    gtk_signal_connect(GTK_OBJECT (manage), "clicked",
		       GTK_SIGNAL_FUNC (gdm_chooser_manage), NULL);
    GTK_WIDGET_SET_FLAGS(manage, GTK_CAN_DEFAULT);
    gtk_widget_set_sensitive (manage, FALSE);
    gtk_widget_show(manage);

    rescan = gtk_button_new_with_label(_("Rescan"));
    gtk_signal_connect(GTK_OBJECT (rescan), "clicked",
		       GTK_SIGNAL_FUNC (gdm_chooser_xdmcp_discover), NULL);
    GTK_WIDGET_SET_FLAGS(rescan, GTK_CAN_DEFAULT);
    gtk_widget_show(rescan);

    cancel = gtk_button_new_with_label(_("Cancel"));
    gtk_signal_connect(GTK_OBJECT (cancel), "clicked",
		       GTK_SIGNAL_FUNC (gdm_chooser_cancel), NULL);
    GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
    gtk_widget_show(cancel);

    /* Button pane */
    buttonpane = gtk_hbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER (buttonpane), 0);
    gtk_box_pack_start(GTK_BOX (buttonpane), 
		       manage, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX (buttonpane), 
		       rescan, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX (buttonpane), 
		       cancel, TRUE, TRUE, 0);
    gtk_window_set_default(GTK_WINDOW(chooser), GTK_WIDGET(manage));
    gtk_widget_show_all(buttonpane);

    /* Put button pane in main window */
    gtk_box_pack_end(GTK_BOX (vbox), 
		     buttonpane, FALSE, FALSE, 0);

    gtk_widget_show(vbox);
    gtk_widget_show(frame);

    gtk_window_set_policy(GTK_WINDOW (chooser), 1, 1, 1);
    gtk_window_set_focus(GTK_WINDOW (chooser), manage);	

    /* Geometry fun */
    gtk_widget_show_all(GTK_WIDGET(browser));
    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));
    gtk_widget_set_usize(GTK_WIDGET (chooser), 
			 (gint) gdk_screen_width() * 0.4, 
			 (gint) gdk_screen_height() * 0.6);

    gtk_window_position(GTK_WINDOW (chooser), GTK_WIN_POS_CENTER);
    gtk_widget_show_all(chooser);
}


static void
gdm_chooser_signals_init(void)
{
    struct sigaction hup;
    sigset_t mask;

    hup.sa_handler = (void *) gdm_chooser_cancel;
    hup.sa_flags = 0;
    sigemptyset(&hup.sa_mask);

    if(sigaction(SIGHUP, &hup, NULL) < 0) 
        gdm_chooser_abort(_("main: Error setting up HUP signal handler"));

    if(sigaction(SIGINT, &hup, NULL) < 0) 
        gdm_chooser_abort(_("main: Error setting up INT signal handler"));

    if(sigaction(SIGTERM, &hup, NULL) < 0) 
        gdm_chooser_abort(_("main: Error setting up TERM signal handler"));

    sigfillset(&mask);
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGHUP);
    sigdelset(&mask, SIGINT);
    
    if(sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
	syslog(LOG_ERR, "Could not set signal mask!");
	exit(EXIT_FAILURE);
    }
}

static GdmChooserHost * 
gdm_chooser_host_alloc (gchar *hostname, gchar *description)
{
    GdmChooserHost *host;
    GdkImlibImage *imlibimg;
    gint w, h;
    gchar *hostimg;
    struct stat statbuf;

    host=g_malloc(sizeof(GdmChooserHost));
    host->name=g_strdup(hostname);
    host->desc=g_strdup(description);

    hostimg=g_strconcat(GdmHostIconDir, "/", hostname, NULL);

    if (!stat(hostimg, &statbuf) && (imlibimg=gdk_imlib_load_image(hostimg))) {

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

	maxwidth=MAX(maxwidth, w);

	host->picture=gdk_imlib_clone_scaled_image(imlibimg, w, h);
	
	gdk_imlib_destroy_image(imlibimg);
    }
    else
	host->picture=nohostimg;

    g_free(hostimg);

    return (host);
}


static void
gdm_chooser_host_dispose(GdmChooserHost *host)
{
    if(!host)
	return;

    if(host->picture != nohostimg)
	gdk_imlib_destroy_image(host->picture);

    g_free(host->name);
    g_free(host->desc);
    g_free(host);
}


int 
main (int argc, char *argv[])
{
    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog("gdmchooser", LOG_PID, LOG_DAEMON);

    gnome_init("gdmchooser", VERSION, argc, argv);
    gnome_sound_shutdown();
    
    gdm_chooser_parse_config();
    gdm_chooser_gui_init();
    gdm_chooser_signals_init();

    gdm_chooser_xdmcp_init();

    gtk_main();

    exit(EXIT_SUCCESS);
}

/* EOF */
