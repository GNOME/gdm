/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K, Petersen <mkp@mkp.net>
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

/* gdmchooser discovers hosts running XDMCP on the local network(s),
 * presents a list of them and allows the user to choose one. The
 * selected hostname will be printed on stdout. */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <gdk/gdkx.h>
#include <X11/Xmd.h>
#include <X11/Xdmcp.h>
#include <syslog.h>
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
#include "gdm.h"

static const gchar RCSid[]="$Id$";

static GdmChooserHost *gdm_chooser_host_alloc (gchar *hostname, gchar *description);
static void gdm_chooser_decode_packet (void);
static void gdm_chooser_abort (const gchar *format, ...);
static void gdm_chooser_browser_update (void);
static void gdm_chooser_xdmcp_init (gchar **hosts);
static void gdm_chooser_host_dispose (GdmChooserHost *host);
static void gdm_chooser_choose_host (gchar *hostname);
static void gdm_chooser_add_hosts (gchar **hosts);

/* Fixetyfix */
int XdmcpReallocARRAY8 (ARRAY8Ptr array, int length);


typedef struct _XdmAuth {
    ARRAY8  authentication;
    ARRAY8  authorization;
} XdmAuthRec, *XdmAuthPtr;

static XdmAuthRec authlist = { 
    { (CARD16)  0, (CARD8 *) 0 },
    { (CARD16)  0, (CARD8 *) 0 }
};


static gint sockfd;
static XdmcpBuffer bcbuf;
static XdmcpBuffer querybuf;
static GSList *bcaddr;
static GSList *queryaddr;

static gint  GdmIconMaxHeight;
static gint  GdmIconMaxWidth;
static gboolean  GdmDebug;
static gint  GdmScanTime;
static gchar *GdmHostIconDir;
static gchar *GdmHostDefaultIcon;
static gchar *GdmGtkRC;

static GtkWidget *chooser;
static GtkWidget *manage;
static GtkWidget *rescan;
static GtkWidget *cancel;

static gint maxwidth = 0;
static GIOChannel *channel;
static GList *hosts = NULL;
static GdkImlibImage *defhostimg;
static GnomeIconList *browser;
static GdmChooserHost *curhost;


static gint 
gdm_chooser_sort_func (gpointer d1, gpointer d2)
{
    GdmChooserHost *a = d1;
    GdmChooserHost *b = d2;

    if (!d1 || !d2)
	return 0;

    return strcmp (a->name, b->name);
}


static void
gdm_chooser_decode_packet (void)
{
    struct sockaddr_in clnt_sa;
    gint sa_len = sizeof (clnt_sa);
    static XdmcpBuffer buf;
    XdmcpHeader header;
    struct hostent *he;
    gchar *hostname = NULL;
    gchar *status = NULL;
    ARRAY8 auth, host, stat;
    GdmChooserHost *new_host;

    if (! XdmcpFill (sockfd, &buf, (XdmcpNetaddr) &clnt_sa, &sa_len))
	return;

    if (! XdmcpReadHeader (&buf, &header))
	return;
    
    if (header.version != XDM_PROTOCOL_VERSION)
	return;

    if (header.opcode != WILLING)
	return;
    
    if (! XdmcpReadARRAY8 (&buf, &auth))
	goto done;
    
    if (! XdmcpReadARRAY8 (&buf, &host))
	goto done;
    
    if (! XdmcpReadARRAY8 (&buf, &stat))
	goto done;
    
    status = g_strndup (stat.data, stat.length);
    
    he = gethostbyaddr ((gchar *) &clnt_sa.sin_addr, 
			sizeof (struct in_addr), 
			AF_INET);
    
    hostname = (he && he->h_name) ? he->h_name : inet_ntoa (clnt_sa.sin_addr);
    
    /* We can't pipe hostnames larger than this */
    if (strlen (hostname)+1 > PIPE_BUF)
	goto done;

    new_host = gdm_chooser_host_alloc (hostname, (gchar *) status);
    if (g_list_find_custom (hosts,
			    new_host,
			    (GCompareFunc) gdm_chooser_sort_func)) {
	    gdm_chooser_host_dispose (new_host);
	    goto done;
    }
    
    hosts = g_list_insert_sorted (hosts, 
				  new_host,
				  (GCompareFunc) gdm_chooser_sort_func);

    gdm_chooser_browser_update ();
    
 done:
    XdmcpDisposeARRAY8 (&auth);
    XdmcpDisposeARRAY8 (&host);
    XdmcpDisposeARRAY8 (&stat);
    
    g_free (status);
    
    return;
}


/* Find broadcast address for all active, non pointopoint interfaces */
static void
gdm_chooser_find_bcaddr (void)
{
    gint i = 0;
    struct ifconf ifcfg;
    struct ifreq ifr[MAXIF];
    struct in_addr *ia;

    ifcfg.ifc_buf = (gchar *) &ifr;
    ifcfg.ifc_len = sizeof (ifr);

    memset (ifr, 0, sizeof (ifr));

    if (ioctl (sockfd, SIOCGIFCONF, &ifcfg) < 0) 
	gdm_chooser_abort ("Could not get SIOCIFCONF");

    for (i=0 ; i < MAXIF ; i++) 
	if (strlen (ifr[i].ifr_name)) {
	    struct ifreq *ifreq;
	    struct sockaddr_in *ba;

	    ifreq = g_new0 (struct ifreq, 1);

	    strncpy (ifreq->ifr_name, ifr[i].ifr_name, sizeof (ifr[i].ifr_name));

	    if (ioctl (sockfd, SIOCGIFFLAGS, ifreq) < 0) 
		gdm_chooser_abort ("Could not get SIOCGIFFLAGS for %s", ifr[i].ifr_name);

	    if ((ifreq->ifr_flags & IFF_UP) == 0)
		goto done;

	    if ((ifreq->ifr_flags & IFF_BROADCAST) == 0)
		goto done;

	    if (ioctl (sockfd, SIOCGIFBRDADDR, ifreq) < 0)
		goto done;

	    ba = (struct sockaddr_in *) &ifreq->ifr_broadaddr;

	    ia = g_new0 (struct in_addr, 1);

	    ia->s_addr = ba->sin_addr.s_addr;

	    bcaddr = g_slist_append (bcaddr, ia);

	done:
	    g_free (ifreq);
	}
}


static gboolean 
gdm_chooser_xdmcp_discover (void)
{
    struct sockaddr_in sock;
    GSList *bl = bcaddr;
    GSList *ql = queryaddr;
    struct in_addr *ia;
    GList *hl = hosts;

    gtk_widget_set_sensitive (GTK_WIDGET (chooser), FALSE);

    while (hl) {
	gdm_chooser_host_dispose ((GdmChooserHost *) hl->data);
	hl = hl->next;
    }

    g_list_free (hosts);

    hosts = NULL;

    sock.sin_family = AF_INET;
    sock.sin_port = htons (XDM_UDP_PORT);

    while (bl) {
	ia = (struct in_addr *) bl->data;
	sock.sin_addr.s_addr = ia->s_addr; 
	XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock, sizeof (struct sockaddr_in));
	bl = bl->next;
    }

    /*
    tid = g_timeout_add (GdmScanTime * 1000, 
			 (GSourceFunc) gdm_chooser_browser_update, NULL);
     */
    while (ql != NULL) {
	    ia = (struct in_addr *) ql->data;
	    sock.sin_addr.s_addr = ia->s_addr;
	    XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock, sizeof (struct sockaddr_in));
	    ql = ql->next;
    }

    return TRUE;
}


static void
gdm_chooser_xdmcp_init (char **hosts)
{
    static XdmcpHeader header;
    gint sockopts = 1;

    /* Open socket for communication */
    if ((sockfd = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
	gdm_chooser_abort ("Could not create socket()!");

    if (setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, (char *) &sockopts, sizeof (sockopts)) < 0)
	gdm_chooser_abort ("Could not set socket options!");

    /* Assemble XDMCP BROADCAST_QUERY packet in static buffer */
    header.opcode  = (CARD16) BROADCAST_QUERY;
    header.length  = 1;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&bcbuf, &header);
    XdmcpWriteARRAY8 (&bcbuf, &authlist.authentication);

    /* Assemble XDMCP QUERY packet in static buffer */
    header.opcode  = (CARD16) QUERY;
    header.length  = 1;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&querybuf, &header);
    XdmcpWriteARRAY8 (&querybuf, &authlist.authentication);

    gdm_chooser_add_hosts (hosts);

    channel = g_io_channel_unix_new (sockfd);
    g_io_add_watch_full (channel, G_PRIORITY_DEFAULT,
			G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
			(GIOFunc) gdm_chooser_decode_packet,
			GINT_TO_POINTER (sockfd), NULL);
    g_io_channel_unref (channel);

    gdm_chooser_xdmcp_discover();
}


static gboolean
gdm_chooser_cancel (void)
{
    closelog();
    gtk_main_quit();

    return TRUE;
}


static gboolean
gdm_chooser_manage (void)
{
	if (curhost)
		gdm_chooser_choose_host (curhost->name);

	closelog();
	gtk_main_quit();

	return TRUE;
}


static void
gdm_chooser_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    syslog (LOG_ERR, s);
    closelog ();

    exit (EXIT_FAILURE);
}


static void 
gdm_chooser_parse_config (void)
{
    struct stat unused;
	
    if (stat (GDM_CONFIG_FILE, &unused) == -1)
	gdm_chooser_abort (_("gdm_chooser_parse_config: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmGtkRC = gnome_config_get_string (GDM_KEY_GTKRC);
    GdmScanTime = gnome_config_get_int (GDM_KEY_SCAN);
    GdmHostDefaultIcon = gnome_config_get_string (GDM_KEY_HOST);
    GdmHostIconDir = gnome_config_get_string (GDM_KEY_HOSTDIR);
    GdmIconMaxWidth = gnome_config_get_int (GDM_KEY_ICONWIDTH);
    GdmIconMaxHeight = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
    GdmDebug = gnome_config_get_bool (GDM_KEY_DEBUG);

    if (GdmScanTime < 1) GdmScanTime = 1;
    if (GdmIconMaxWidth < 0) GdmIconMaxWidth = 128;
    if (GdmIconMaxHeight < 0) GdmIconMaxHeight = 128;

    gnome_config_pop_prefix();
}


static gboolean 
gdm_chooser_browser_select (GtkWidget *widget, gint selected, GdkEvent *event)
{
    if (!widget || !event)
	return TRUE;

    switch (event->type) {
	
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
	curhost = g_list_nth_data (hosts, selected);
	gtk_widget_set_sensitive (manage, TRUE);
	break;
	
    default: 
	break;
    }
    
    return TRUE;
}


static gboolean
gdm_chooser_browser_unselect (GtkWidget *widget, gint selected, GdkEvent *event)
{
    if (!event) 
	return TRUE;

    switch (event->type) {
	
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
	curhost = NULL;
	gtk_widget_set_sensitive (manage, FALSE);
	break;
	
    default:
	break;
    }

    return TRUE;
}


static void
gdm_chooser_browser_update (void)
{
    GList *list = hosts;

    gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
    gnome_icon_list_clear (GNOME_ICON_LIST (browser));

    while (list) {
	GdmChooserHost *host;
	gchar *temp;

	host = (GdmChooserHost *) list->data;

	temp = g_strconcat (host->name, "\n", host->desc, NULL);
	gnome_icon_list_append_imlib (GNOME_ICON_LIST (browser), host->picture, temp);
	g_free (temp);

	list = list->next;
    }

    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));

    gtk_widget_set_sensitive (GTK_WIDGET (chooser), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
    gtk_widget_show_all (GTK_WIDGET (chooser));
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
    if (GdmGtkRC)
	gtk_rc_parse (GdmGtkRC);

    /* Load default host image */
    if (stat (GdmHostDefaultIcon, &statbuf))
	gdm_chooser_abort (_("Can't open default host icon: %s"), GdmHostDefaultIcon);
    else {
	defhostimg = gdk_imlib_load_image (GdmHostDefaultIcon);
	maxwidth = defhostimg->rgb_width;
    }

    /* Root Window */
    rootwin = gdk_window_foreign_new (GDK_ROOT_WINDOW ());

    /* Main window */
    chooser = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_border_width (GTK_CONTAINER (chooser), 0);

    /* 3D frame for main window */
    frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
    gtk_container_border_width (GTK_CONTAINER (frame), 0);
    gtk_container_add (GTK_CONTAINER (chooser), frame);

    /* Vertical box containing browser box and button pane */
    vbox = gtk_vbox_new (FALSE, 10);
    gtk_container_border_width (GTK_CONTAINER (vbox), 10);
    gtk_container_add (GTK_CONTAINER (frame), vbox);

    /* Find background style for browser */
    style = gtk_style_copy (chooser->style);
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
			GTK_SIGNAL_FUNC (gdm_chooser_browser_select), NULL);
    gtk_signal_connect (GTK_OBJECT (browser), "unselect_icon",
			GTK_SIGNAL_FUNC (gdm_chooser_browser_unselect), NULL);
    gtk_widget_pop_style();

    /* Browser 3D frame */
    bframe = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (bframe), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER(bframe), GTK_WIDGET (browser));

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
    manage = gtk_button_new_with_label (_("Connect"));
    gtk_signal_connect (GTK_OBJECT (manage), "clicked",
			GTK_SIGNAL_FUNC (gdm_chooser_manage), NULL);
    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (manage), GTK_CAN_DEFAULT);
    gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
    gtk_widget_show (GTK_WIDGET (manage));

    rescan = gtk_button_new_with_label (_("Rescan"));
    gtk_signal_connect(GTK_OBJECT (rescan), "clicked",
		       GTK_SIGNAL_FUNC (gdm_chooser_xdmcp_discover), NULL);
    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (rescan), GTK_CAN_DEFAULT);
    gtk_widget_show (GTK_WIDGET (rescan));

    cancel = gtk_button_new_with_label (_("Cancel"));
    gtk_signal_connect(GTK_OBJECT (cancel), "clicked",
		       GTK_SIGNAL_FUNC (gdm_chooser_cancel), NULL);
    GTK_WIDGET_SET_FLAGS(GTK_WIDGET (cancel), GTK_CAN_DEFAULT);
    gtk_widget_show (GTK_WIDGET (cancel));

    /* Button pane */
    buttonpane = gtk_hbox_new(TRUE, 0);
    gtk_container_set_border_width ( GTK_CONTAINER (buttonpane), 0);
    gtk_box_pack_start (GTK_BOX (buttonpane), 
			GTK_WIDGET (manage), TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (buttonpane), 
			GTK_WIDGET (rescan), TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (buttonpane), 
			GTK_WIDGET (cancel), TRUE, TRUE, 0);
    gtk_window_set_default (GTK_WINDOW (chooser), GTK_WIDGET (manage));
    gtk_widget_show_all (GTK_WIDGET (buttonpane));

    /* Put button pane in main window */
    gtk_box_pack_end (GTK_BOX (vbox), 
		      GTK_WIDGET (buttonpane), FALSE, FALSE, 0);

    gtk_widget_show (GTK_WIDGET (vbox));
    gtk_widget_show (GTK_WIDGET (frame));

    gtk_window_set_policy (GTK_WINDOW (chooser), 1, 1, 1);
    gtk_window_set_focus (GTK_WINDOW (chooser), GTK_WIDGET (manage));	

    /* Geometry fun */
    gtk_widget_show_all (GTK_WIDGET (browser));
    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));
    gtk_widget_set_usize (GTK_WIDGET (chooser), 
			  (gint) gdk_screen_width() * 0.4, 
			  (gint) gdk_screen_height() * 0.6);

    gtk_window_position (GTK_WINDOW (chooser), GTK_WIN_POS_CENTER);
    gtk_widget_show_all (GTK_WIDGET (chooser));
}


static void
gdm_chooser_signals_init (void)
{
    struct sigaction hup;
    sigset_t mask;

    hup.sa_handler = (void *) gdm_chooser_cancel;
    hup.sa_flags = 0;
    sigemptyset (&hup.sa_mask);

    if (sigaction (SIGHUP, &hup, NULL) < 0) 
        gdm_chooser_abort (_("gdm_signals_init: Error setting up HUP signal handler"));

    if (sigaction (SIGINT, &hup, NULL) < 0) 
        gdm_chooser_abort (_("gdm_signals_init: Error setting up INT signal handler"));

    if (sigaction (SIGTERM, &hup, NULL) < 0) 
        gdm_chooser_abort (_("gdm_signals_init: Error setting up TERM signal handler"));

    sigfillset (&mask);
    sigdelset (&mask, SIGTERM);
    sigdelset (&mask, SIGHUP);
    sigdelset (&mask, SIGINT);
    
    if (sigprocmask (SIG_SETMASK, &mask, NULL) == -1) 
	gdm_chooser_abort (_("Could not set signal mask!"));
}

static GdmChooserHost * 
gdm_chooser_host_alloc (gchar *hostname, gchar *description)
{
    GdmChooserHost *host;
    GdkImlibImage *imlibimg;
    gchar *hostimg;
    struct stat statbuf;

    host = g_malloc (sizeof (GdmChooserHost));
    host->name = g_strdup (hostname);
    host->desc = g_strdup (description);

    hostimg = g_strconcat (GdmHostIconDir, "/", hostname, NULL);

    if (!stat (hostimg, &statbuf) && (imlibimg = gdk_imlib_load_image (hostimg))) {
	gint w, h;

	w = imlibimg->rgb_width;
	h = imlibimg->rgb_height;
	
	if (w>h && w>GdmIconMaxWidth) {
	    h = h * ((gfloat) GdmIconMaxWidth/w);
	    w = GdmIconMaxWidth;
	} 
	else if (h>GdmIconMaxHeight) {
	    w = w * ((gfloat) GdmIconMaxHeight/h);
	    h = GdmIconMaxHeight;
	}

	maxwidth = MAX (maxwidth, w);

	host->picture = gdk_imlib_clone_scaled_image (imlibimg, w, h);
	
	gdk_imlib_destroy_image (imlibimg);
    }
    else
	host->picture = defhostimg;

    g_free (hostimg);

    return host;
}


static void
gdm_chooser_host_dispose (GdmChooserHost *host)
{
    if (!host)
	return;

    if (host->picture != defhostimg)
	gdk_imlib_destroy_image (host->picture);

    g_free (host->name);
    g_free (host->desc);
    g_free (host);
}

static gchar *xdm_address = NULL;
static gchar *client_address = NULL;
static gint connection_type = 0;

struct poptOption xdm_options [] = {
  { "xdmaddress", '\0', POPT_ARG_STRING|POPT_ARGFLAG_ONEDASH, &xdm_address, 0,
    "setting socket for xdm communication", "xdm response socket" },
  { "clientaddress", '\0', POPT_ARG_STRING|POPT_ARGFLAG_ONEDASH, &client_address, 0, "setting client address to return in response to xdm", "client address" },
  { "connectionType", '\0', POPT_ARG_INT|POPT_ARGFLAG_ONEDASH, &connection_type, 0, "setting connection type to return in response to xdm", "connection type" },
  POPT_AUTOHELP
  { NULL, 0, 0, NULL, 0}
};

#ifndef ishexdigit
#define ishexdigit(c) (isdigit(c) || ('a' <= (c) && (c) <= 'f'))
#endif
#define HexChar(c)  ('0' <= (c) && (c) <= '9' ? (c) - '0' : (c) - 'a' + 10)

static int
from_hex (gchar *s, gchar *d, int len)
{
  int t;
  while (len >= 2)
    {
      if (!ishexdigit(*s))
 return 1;
      t = HexChar (*s) << 4;
      s++;
      if (!ishexdigit(*s))
 return 1;
      t += HexChar (*s);
      s++;
      *d++ = t;
      len -= 2;
    }
  return len;
}


static void
gdm_chooser_choose_host (gchar *hostname)
{
  ARRAY8 tmparr;
  struct hostent *hentry;

  g_print ("%s\n", curhost->name);
  if (xdm_address)
    {
      struct sockaddr_in in_addr;
      char xdm_addr[32];
      char client_addr[32];
      int fd;
      char buf[1024];
      XdmcpBuffer buffer;
      long family, port, addr;
      if (strlen (xdm_address) > 64 || from_hex (xdm_address, xdm_addr, strlen (xdm_address)) != 0)
 gdm_chooser_abort ("gdm_chooser_chooser_host: Invalid xdm address.");

      family = (xdm_addr[0] << 8) | xdm_addr[1];
      port = (xdm_addr[2] << 8) | xdm_addr[3];
      addr = (xdm_addr[4] << 24) | (xdm_addr[5] << 16) | (xdm_addr[6] << 8) | xdm_addr[7];
      in_addr.sin_family = AF_INET;
      in_addr.sin_port = htons (port);
      in_addr.sin_addr.s_addr = htonl (addr);
      if ((fd = socket (PF_INET, SOCK_STREAM, 0)) == -1)
 gdm_chooser_abort ("gdm_chooser_chooser_host: Couldn't create response socket.");

      if (connect (fd, (struct sockaddr_in *) &in_addr, sizeof (in_addr)) == -1)
 gdm_chooser_abort ("gdm_chooser_chooser_host: Couldn't connect to xdm.");
      buffer.data = (BYTE *) buf;
      buffer.size = sizeof (buf);
      buffer.pointer = 0;
      buffer.count = 0;

      if (strlen (client_address) > 64 || from_hex (client_address, client_addr, strlen (client_address)) != 0)
 gdm_chooser_abort ("gdm_chooser_chooser_host: Invalid client address.");
      tmparr.data = (BYTE *) client_addr;
      tmparr.length = strlen (client_address) / 2;
      XdmcpWriteARRAY8 (&buffer, &tmparr);
      XdmcpWriteCARD16 (&buffer, (CARD16) connection_type);
      hentry = gethostbyname (hostname);
      if (!hentry)
 gdm_chooser_abort ("gdm_chooser_chooser_host: Couldn't get host entry for %s", hostname);

      tmparr.data = (BYTE *) hentry->h_addr_list[0]; /* XXX */
      tmparr.length = 4;

      XdmcpWriteARRAY8 (&buffer, &tmparr);
      write (fd, (char *) buffer.data, buffer.pointer);
      close (fd);
    }
}

static void
gdm_chooser_add_hosts (gchar **hosts)
{
	struct hostent *hostent;
	struct sockaddr_in qa;
	struct in_addr *ia;
	int i;

	for (i = 0; hosts != NULL && hosts[i] != NULL; i++) {
		char *name = hosts[i];

		if (strcmp (name, "BROADCAST") == 0) {
			gdm_chooser_find_bcaddr ();
			continue;
		}
		if (strlen (name) == 8 &&
		    from_hex (name, (char *) &qa.sin_addr, strlen (name)) == 0)
			qa.sin_family = AF_INET;
		else if ((qa.sin_addr.s_addr = inet_addr (name)) != -1)
			qa.sin_family = AF_INET;
		else if ((hostent = gethostbyname (name)) != NULL
			 && hostent->h_addrtype == AF_INET
			 && hostent->h_length == 4) {
			qa.sin_family = AF_INET;
			memmove (&qa.sin_addr, hostent->h_addr, 4);
		} else {
			continue; /* not a valid address */
		}

		ia = g_new0 (struct in_addr, 1);
		ia->s_addr = qa.sin_addr.s_addr;
		queryaddr = g_slist_append (queryaddr, ia);
	}

	if (bcaddr == NULL &&
	    queryaddr == NULL)
		gdm_chooser_find_bcaddr ();
}

int 
main (int argc, char *argv[])
{
    gchar **fixedargv;
    gint fixedargc, i;
    gchar **hosts;
    poptContext ctx;

    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog ("gdmchooser", LOG_PID, LOG_DAEMON);

    fixedargc = argc + 1;
    fixedargv = g_new0 (gchar *, fixedargc);

    for (i=0; i < argc; i++)
	fixedargv[i] = argv[i];
    
    fixedargv[fixedargc-1] = "--disable-sound";
    gnome_init_with_popt_table ("gdmchooser", VERSION, fixedargc, fixedargv, xdm_options, 0, &ctx);
    g_free (fixedargv);

    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    gnome_preferences_set_dialog_position(GTK_WIN_POS_CENTER);
    
    gdm_chooser_parse_config();
    gdm_chooser_gui_init();
    gdm_chooser_signals_init();

    hosts = (gchar **) poptGetArgs (ctx);
    gdm_chooser_xdmcp_init (hosts);
    poptFreeContext (ctx);

    gtk_main();

    exit (EXIT_SUCCESS);
}


/* EOF */
