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
#include <glade/glade.h>
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

#include <viciousui.h>

#include "gdmchooser.h"
#include "gdm.h"
#include "misc.h"
#include "gdmwm.h"

/* set the DOING_GDM_DEVELOPMENT env variable if you want to
 * search for the glade file in the current dir and not the system
 * install dir, better then something you have to change
 * in the source and recompile */
static gboolean DOING_GDM_DEVELOPMENT = FALSE;

static gboolean RUNNING_UNDER_GDM = FALSE;

static const gchar *scanning_message = N_("Please wait: scanning local network for XDMCP-enabled hosts...");
static const gchar *empty_network = N_("No serving hosts were found.");
static const gchar *active_network = N_("Choose a host to connect to from the selection below.");
static gchar *glade_filename = NULL;

static GdmChooserHost * gdm_chooser_host_alloc (const char *hostname,
						const char *description,
						struct in_addr *ia,
						gboolean willing);

static gboolean gdm_chooser_decode_packet (GIOChannel   *source,
					   GIOCondition  condition,
					   gpointer      data);

static void gdm_chooser_abort (const gchar *format, ...) G_GNUC_PRINTF (1, 2);
static void gdm_chooser_browser_update (void);
static void gdm_chooser_xdmcp_init (char **hosts);
static void gdm_chooser_host_dispose (GdmChooserHost *host);
static void gdm_chooser_choose_host (const gchar *hostname);
static void gdm_chooser_add_hosts (char **hosts);

static guint scan_time_handler = 0;

#define PING_TIMEOUT 2000
#define PING_TRIES 3
static int ping_tries = PING_TRIES;
static guint ping_try_handler = 0;

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

static gint  GdmXineramaScreen;
static gint  GdmIconMaxHeight;
static gint  GdmIconMaxWidth;
static gboolean  GdmDebug;
static gint  GdmScanTime;
static gchar *GdmHostIconDir;
static gchar *GdmHostDefaultIcon;
static gchar *GdmGtkRC;
static gchar *GdmHosts;
static gboolean GdmBroadcast;
static gchar *GdmBackgroundColor;
static int GdmBackgroundType;
enum {
	GDM_BACKGROUND_NONE = 0,
	GDM_BACKGROUND_IMAGE = 1,
	GDM_BACKGROUND_COLOR = 2
};

static GladeXML *chooser_app;
static GtkWidget *chooser, *manage, *rescan, *cancel;
static GtkWidget *status_label;

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

static gboolean
gdm_host_known (struct in_addr *ia)
{
	GList *li;

	for (li = hosts; li != NULL; li = li->next) {
		GdmChooserHost *host = li->data;
		if (memcmp (&host->ia, ia, sizeof (struct in_addr)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
is_loopback_addr (struct in_addr *ia)
{
	const char lo[] = {127,0,0,1};

	if (ia->s_addr == INADDR_LOOPBACK ||
	    memcmp (&ia->s_addr, lo, 4) == 0) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static gboolean
gdm_chooser_decode_packet (GIOChannel   *source,
			   GIOCondition  condition,
			   gpointer      data)
{
    struct sockaddr_in clnt_sa;
    gint sa_len = sizeof (clnt_sa);
    static XdmcpBuffer buf;
    XdmcpHeader header;
    struct hostent *he;
    gchar *hostname = NULL;
    gchar *status = NULL;
    ARRAY8 auth, host, stat;

    if (! XdmcpFill (sockfd, &buf, (XdmcpNetaddr) &clnt_sa, &sa_len))
	return TRUE;

    if (! XdmcpReadHeader (&buf, &header))
	return TRUE;
    
    if (header.version != XDM_PROTOCOL_VERSION)
	return TRUE;

    if (header.opcode == WILLING) {
	    if (! XdmcpReadARRAY8 (&buf, &auth))
		    goto done;

	    if (! XdmcpReadARRAY8 (&buf, &host))
		    goto done;

	    if (! XdmcpReadARRAY8 (&buf, &stat))
		    goto done;

	    status = g_strndup (stat.data, stat.length);
    } else if (header.opcode == UNWILLING) {
	    /* immaterial, will not be shown */
	    status = NULL;
    } else {
	    return TRUE;
    }

    if ( ! is_loopback_addr (&clnt_sa.sin_addr)) {
	    he = gethostbyaddr ((gchar *) &clnt_sa.sin_addr,
				sizeof (struct in_addr),
				AF_INET);

	    hostname = (he && he->h_name) ? he->h_name : inet_ntoa (clnt_sa.sin_addr);

	    /* We can't pipe hostnames larger than this */
	    if (strlen (hostname)+1 > PIPE_BUF)
		    goto done;

	    hostname = g_strdup (hostname);
    } else {
	    hostname = g_new0 (char, 1024);
	    if (gethostname (hostname, 1023) != 0) {
		    g_free (hostname);
		    goto done;
	    }
    }

    gdm_chooser_host_alloc (hostname,
			    status,
			    &clnt_sa.sin_addr,
			    header.opcode == WILLING);

    g_free (hostname);

    gdm_chooser_browser_update ();
    
 done:
    if (header.opcode == WILLING) {
	    XdmcpDisposeARRAY8 (&auth);
	    XdmcpDisposeARRAY8 (&host);
	    XdmcpDisposeARRAY8 (&stat);
    }
    
    g_free (status);
    
    return TRUE;
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
chooser_scan_time_update (gpointer data)
{
	scan_time_handler = 0;
	gdm_chooser_browser_update ();
	return FALSE;
}

static void
do_ping (gboolean full)
{
    struct sockaddr_in sock;
    GSList *bl = bcaddr;
    GSList *ql = queryaddr;
    struct in_addr *ia;

    sock.sin_family = AF_INET;
    sock.sin_port = htons (XDM_UDP_PORT);

    while (bl) {
	    ia = (struct in_addr *) bl->data;
	    sock.sin_addr.s_addr = ia->s_addr; 
	    XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
	    bl = bl->next;
    }

    while (ql != NULL) {
	    ia = (struct in_addr *) ql->data;
	    if (full ||
		! gdm_host_known (ia)) {
		    sock.sin_addr.s_addr = ia->s_addr;
		    XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
	    }
	    ql = ql->next;
    }
}

static gboolean
ping_try (gpointer data)
{
	do_ping (FALSE);

	ping_tries --;
	if (ping_tries <= 0)
		return FALSE;
	else
		return TRUE;
}

gboolean 
gdm_chooser_xdmcp_discover (void)
{
    GList *hl = hosts;

    gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
    gnome_icon_list_clear (GNOME_ICON_LIST (browser));
    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);
    gtk_label_set (GTK_LABEL (status_label),
		   _(scanning_message));

    while (hl) {
	gdm_chooser_host_dispose ((GdmChooserHost *) hl->data);
	hl = hl->next;
    }

    g_list_free (hosts);

    hosts = NULL;

    do_ping (TRUE);

    if (scan_time_handler > 0)
	    g_source_remove (scan_time_handler);
    scan_time_handler = g_timeout_add (GdmScanTime * 1000, 
				       chooser_scan_time_update, NULL);

    /* Note we already used up one try */
    ping_tries = PING_TRIES - 1;
    if (ping_try_handler > 0)
	    g_source_remove (ping_try_handler);
    ping_try_handler = g_timeout_add (PING_TIMEOUT, ping_try, NULL);

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
			gdm_chooser_decode_packet,
			GINT_TO_POINTER (sockfd), NULL);
    g_io_channel_unref (channel);

    gdm_chooser_xdmcp_discover();
}


gboolean
gdm_chooser_cancel (void)
{
    closelog();
    gtk_main_quit();

    return TRUE;
}


gboolean
gdm_chooser_manage (GtkButton *button, gpointer data)
{
    if (scan_time_handler > 0) {
	    g_source_remove (scan_time_handler);
	    scan_time_handler = 0;
    }

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

    syslog (LOG_ERR, "%s", s);
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

    GdmXineramaScreen = gnome_config_get_int (GDM_KEY_XINERAMASCREEN);
    GdmGtkRC = gnome_config_get_string (GDM_KEY_GTKRC);
    GdmScanTime = gnome_config_get_int (GDM_KEY_SCAN);
    GdmHostDefaultIcon = gnome_config_get_string (GDM_KEY_HOST);
    GdmHostIconDir = gnome_config_get_string (GDM_KEY_HOSTDIR);
    GdmIconMaxWidth = gnome_config_get_int (GDM_KEY_ICONWIDTH);
    GdmIconMaxHeight = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
    GdmDebug = gnome_config_get_bool (GDM_KEY_DEBUG);

    GdmBackgroundColor = gnome_config_get_string (GDM_KEY_BACKGROUNDCOLOR);
    GdmBackgroundType = gnome_config_get_int (GDM_KEY_BACKGROUNDTYPE);

    /* note that command line arguments will prevail over these */
    GdmHosts = gnome_config_get_string (GDM_KEY_HOSTS);
    GdmBroadcast = gnome_config_get_bool (GDM_KEY_BROADCAST);
    /* if broadcasting, then append BROADCAST to hosts */
    if (GdmBroadcast) {
	    if (ve_string_empty (GdmHosts)) {
		    g_free (GdmHosts);
		    GdmHosts = "BROADCAST";
	    } else {
		    char *tmp = g_strconcat (GdmHosts, ",BROADCAST", NULL);
		    g_free (GdmHosts);
		    GdmHosts = tmp;
	    }
    }

    if (GdmScanTime < 1) GdmScanTime = 1;
    if (GdmIconMaxWidth < 0) GdmIconMaxWidth = 128;
    if (GdmIconMaxHeight < 0) GdmIconMaxHeight = 128;

    gnome_config_pop_prefix();
}

static GdmChooserHost *
gdm_nth_willing_host (int n)
{
	GList *li;
	int i;

	i = 0;
	for (li = hosts; li != NULL; li = li->next) {
		GdmChooserHost *host = li->data;
		if (host->willing) {
			if (i == n)
				return host;
			i++;
		}
	}
	return NULL;
}


gboolean 
gdm_chooser_browser_select (GtkWidget *widget, gint selected, GdkEvent *event)
{
    if (!widget || !event)
	return TRUE;

    switch (event->type) {
	
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
	curhost = gdm_nth_willing_host (selected);
	gtk_widget_set_sensitive (manage, TRUE);
	break;

    case GDK_2BUTTON_PRESS:
	curhost = gdm_nth_willing_host (selected);
	gdm_chooser_manage (NULL, NULL);
	break;
	
    default: 
	break;
    }
    
    return TRUE;
}


gboolean
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
    GList *li;
    gboolean any;

    gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
    gnome_icon_list_clear (GNOME_ICON_LIST (browser));

    any = FALSE;
    for (li = hosts; li != NULL; li = li->next) {
	    GdmChooserHost *host = (GdmChooserHost *) li->data;

	    if (host->willing) {
		    /* FIXME: the \n doesn't actually propagate
		     * since the icon list is a broken piece of horsedung */
		    char *temp = g_strconcat (host->name, " \n",
					      host->desc, NULL);
		    gnome_icon_list_append_imlib (GNOME_ICON_LIST (browser),
						  host->picture, temp);
		    g_free (temp);
		    any = TRUE;
	    }
    }

    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));

    if (any) {
      gtk_label_set (GTK_LABEL (glade_xml_get_widget (chooser_app, "status_label")),
		     _(active_network));
    } else {
      gtk_label_set (GTK_LABEL (glade_xml_get_widget (chooser_app, "status_label")),
		     _(empty_network));
    }
    gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (rescan), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);
}

void
display_chooser_information (void)
{
   glade_xml_new (glade_filename, "gdmchooser_help_dialog");
}

static void 
gdm_chooser_gui_init (void)
{
    GtkStyle  *style;
    GdkColor  bbg = { 0, 0xFFFF, 0xFFFF, 0xFFFF };
 
   
        /* Look for the glade file in $(datadir)/gdmconfig or, failing that,
     * look in the current directory.
	 * Except when doing development, we want the app to use the glade file
	 * in the same directory, so we can actually make changes easily.
     */
	
    if ( ! DOING_GDM_DEVELOPMENT) {
	    if (g_file_exists (GDM_GLADE_DIR "/gdmchooser.glade")) {
		    glade_filename = g_strdup (GDM_GLADE_DIR
					       "/gdmchooser.glade");
	    } else {
		    glade_filename = gnome_datadir_file("gdm/gdmchooser.glade");
		    if (glade_filename == NULL) {	  
			    glade_filename = g_strdup("gdmchooser.glade");
		    }
	    }
    } else {
	    glade_filename = g_strdup("gdmchooser.glade");
    }
    /* Enable theme */
    if (GdmGtkRC)
	gtk_rc_parse (GdmGtkRC);

    /* Load default host image */
    if (access (GdmHostDefaultIcon, R_OK) != 0)
	gdm_chooser_abort (_("Can't open default host icon: %s"), GdmHostDefaultIcon);
    else
	defhostimg = gdk_imlib_load_image (GdmHostDefaultIcon);

    /* Main window */
    g_assert (glade_filename != NULL);
    chooser_app = glade_xml_new (glade_filename, "gdmchooser_main");
    if (chooser_app == NULL) {
	    GtkWidget *fatal_error = 
		    gnome_error_dialog(_("Cannot find the glade interface description\n"
					 "file, cannot run gdmchooser.\n"
					 "Please check your installation and the\n"
					 "location of the gdmchooser.glade file."));
	    gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
	    exit(EXIT_FAILURE);
    }
    glade_xml_signal_autoconnect (chooser_app);
   
    chooser = glade_xml_get_widget (chooser_app, "gdmchooser_main");
    manage = glade_xml_get_widget (chooser_app, "connect_button");
    rescan = glade_xml_get_widget (chooser_app, "rescan_button");
    cancel = glade_xml_get_widget (chooser_app, "quit_button");
    status_label = glade_xml_get_widget (chooser_app, "status_label");

    if (chooser == NULL ||
	manage == NULL ||
	rescan == NULL ||
	cancel == NULL ||
	status_label == NULL) {
	    GtkWidget *fatal_error = 
		    gnome_error_dialog(_("The glade interface description file\n"
					 "appears to be corrupted.\n"
					 "Please check your installation."));
	    gnome_dialog_run_and_close(GNOME_DIALOG(fatal_error));
	    exit(EXIT_FAILURE);
	}
   
    /* Find background style for browser */
    style = gtk_style_copy (chooser->style);
   
    /* FIXME: Forcing the background to be white seems a bit off seeing as
     * the user might like having a dark theme.
     */
    style->bg[GTK_STATE_NORMAL] = bbg;
    gtk_widget_push_style (style);

    browser = (GnomeIconList *) glade_xml_get_widget (chooser_app, 
						     "chooser_iconlist");
    gnome_icon_list_freeze (GNOME_ICON_LIST (browser));
    gnome_icon_list_set_separators (GNOME_ICON_LIST (browser), " /-_.");
    gnome_icon_list_set_icon_width (GNOME_ICON_LIST (browser), GdmIconMaxWidth + 20);
    gnome_icon_list_set_icon_border (GNOME_ICON_LIST (browser), 2);
    gtk_widget_pop_style();
    gnome_icon_list_thaw (GNOME_ICON_LIST (browser));

    gtk_widget_set_usize (GTK_WIDGET (chooser), 
			  (gint) gdk_screen_width() * 0.4, 
			  (gint) gdk_screen_height() * 0.6);

    gdm_wm_center_window (GTK_WINDOW (chooser));
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
gdm_chooser_host_alloc (const char *hostname,
			const char *description,
			struct in_addr *ia,
			gboolean willing)
{
    GdmChooserHost *host;
    GdkImlibImage *imlibimg;
    gchar *hostimg;
    GList *hostl;

    host = g_malloc (sizeof (GdmChooserHost));
    host->name = g_strdup (hostname);
    host->desc = g_strdup (description);
    memcpy (&host->ia, ia, sizeof (struct in_addr));
    host->willing = willing;

    hostl = g_list_find_custom (hosts,
				host,
				(GCompareFunc) gdm_chooser_sort_func);
    /* replace */
    if (hostl != NULL) {
	    GdmChooserHost *old = hostl->data;
	    hostl->data = host;
	    gdm_chooser_host_dispose (old);
    } else {
	    hosts = g_list_insert_sorted (hosts, 
					  host,
					  (GCompareFunc) gdm_chooser_sort_func);
    }
    
    if ( ! willing)
	    return host;

    hostimg = g_strconcat (GdmHostIconDir, "/", hostname, NULL);

    if (access (hostimg, R_OK) == 0 &&
	(imlibimg = gdk_imlib_load_image (hostimg))) {
	gint w, h;

	w = imlibimg->rgb_width;
	h = imlibimg->rgb_height;
	
	if (w>h && w>GdmIconMaxWidth) {
	    h = h * ((gfloat) GdmIconMaxWidth/w);
	    w = GdmIconMaxWidth;
	} else if (h>GdmIconMaxHeight) {
	    w = w * ((gfloat) GdmIconMaxHeight/h);
	    h = GdmIconMaxHeight;
	}

	host->picture = gdk_imlib_clone_scaled_image (imlibimg, w, h);
	
	gdk_imlib_destroy_image (imlibimg);
    } else {
	host->picture = defhostimg;
    }

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
    host->picture = NULL;

    g_free (host->name);
    host->name = NULL;
    g_free (host->desc);
    host->desc = NULL;
    g_free (host);
}

static gchar *xdm_address = NULL;
static gchar *client_address = NULL;
static gint connection_type = 0;

struct poptOption xdm_options [] = {
  { "xdmaddress", '\0', POPT_ARG_STRING|POPT_ARGFLAG_ONEDASH, &xdm_address, 0,
    N_("Socket for xdm communication"), N_("SOCKET") }, { "clientaddress", '\0', POPT_ARG_STRING|POPT_ARGFLAG_ONEDASH, &client_address, 0, N_("Client address to return in response to xdm"), N_("ADDRESS") },
  { "connectionType", '\0', POPT_ARG_INT|POPT_ARGFLAG_ONEDASH, &connection_type, 0, N_("Connection type to return in response to xdm"), N_("TYPE") },
  POPT_AUTOHELP
  { NULL, 0, 0, NULL, 0}
};

#ifndef ishexdigit
#define ishexdigit(c) (isdigit(c) || ('a' <= (c) && (c) <= 'f'))
#endif
#define HexChar(c)  ('0' <= (c) && (c) <= '9' ? (c) - '0' : (c) - 'a' + 10)

static int
from_hex (const char *s, char *d, int len)
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
gdm_chooser_choose_host (const char *hostname)
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
      if (strlen (xdm_address) > 64 ||
	  from_hex (xdm_address, xdm_addr, strlen (xdm_address)) != 0)
	      gdm_chooser_abort ("gdm_chooser_chooser_host: Invalid xdm address.");

      family = (xdm_addr[0] << 8) | xdm_addr[1];
      port = (xdm_addr[2] << 8) | xdm_addr[3];
      addr = (xdm_addr[4] << 24) | (xdm_addr[5] << 16) | (xdm_addr[6] << 8) | xdm_addr[7];
      in_addr.sin_family = AF_INET;
      in_addr.sin_port = htons (port);
      in_addr.sin_addr.s_addr = htonl (addr);
      if ((fd = socket (PF_INET, SOCK_STREAM, 0)) == -1)
	      gdm_chooser_abort ("gdm_chooser_chooser_host: Couldn't create response socket.");

      if (connect (fd, (struct sockaddr *) &in_addr, sizeof (in_addr)) == -1)
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
gdm_chooser_add_hosts (char **hosts)
{
	struct hostent *hostent;
	struct sockaddr_in qa;
	struct in_addr *ia;
	int i;

	for (i = 0; hosts != NULL && hosts[i] != NULL; i++) {
		const char *name = hosts[i];

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

static void
set_background (void)
{
	if (GdmBackgroundType != GDM_BACKGROUND_NONE) {
		GdkColor color;
		GdkColormap *colormap;

		if (ve_string_empty (GdmBackgroundColor) ||
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
}

int 
main (int argc, char *argv[])
{
    char **fixedargv;
    int fixedargc, i;
    char **hosts;
    poptContext ctx;
    const char *gdm_version;

    if (g_getenv ("DOING_GDM_DEVELOPMENT") != NULL)
	    DOING_GDM_DEVELOPMENT = TRUE;
    if (g_getenv ("RUNNING_UNDER_GDM") != NULL)
	    RUNNING_UNDER_GDM = TRUE;

    /* Avoid creating ~gdm/.gnome stuff */
    gnome_do_not_create_directories = TRUE;

    openlog ("gdmchooser", LOG_PID, LOG_DAEMON);

    fixedargc = argc + 1;
    fixedargv = g_new0 (gchar *, fixedargc);

    for (i=0; i < argc; i++)
	fixedargv[i] = argv[i];
    
    fixedargv[fixedargc-1] = "--disable-sound";
    gnome_init_with_popt_table ("gdmchooser", VERSION, fixedargc, fixedargv, xdm_options, 0, &ctx);
    glade_gnome_init();
    g_free (fixedargv);

    bindtextdomain (PACKAGE, GNOMELOCALEDIR);
    textdomain (PACKAGE);

    gdm_wm_screen_init (GdmXineramaScreen);

    gdm_version = g_getenv ("GDM_VERSION");

    if (gdm_version != NULL &&
	strcmp (gdm_version, VERSION) != 0) {
	    char *msg;
	    GtkWidget *dialog;

	    gdm_wm_init (0);

	    gdm_wm_focus_new_windows (TRUE);

	    msg = g_strdup_printf
		    (_("The chooser version (%s) does not match the daemon "
		       "version (%s).\n"
		       "You have probably just upgraded gdm.\n"
		       "Please restart the gdm daemon or reboot the computer."),
		     VERSION, gdm_version);
	    dialog = gnome_error_dialog (msg);
	    g_free (msg);

	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));
	    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	    return EXIT_SUCCESS;
    }
    
    gdm_chooser_parse_config();
    gdm_chooser_gui_init();
    gdm_chooser_signals_init();

    set_background ();

    hosts = (char **)poptGetArgs (ctx);
    /* when no hosts on the command line, take them from the config */
    if (hosts == NULL ||
	hosts[0] == NULL) {
	    int i;
	    hosts = g_strsplit (GdmHosts, ",", -1);
	    for (i = 0; hosts != NULL && hosts[i] != NULL; i++) {
		    g_strstrip (hosts[i]);
	    }
    }
    gdm_chooser_xdmcp_init (hosts);
    poptFreeContext (ctx);

    gtk_widget_show_now (chooser);

    /* can it ever happen that it'd be NULL here ??? */
    if (chooser->window != NULL) {
	    gdm_wm_init (GDK_WINDOW_XWINDOW (chooser->window));

	    /* Run the focus, note that this will work no matter what
	     * since gdm_wm_init will set the display to the gdk one
	     * if it fails */
	    gdm_wm_focus_window (GDK_WINDOW_XWINDOW (chooser->window));
    }

    gtk_main();

    exit (EXIT_SUCCESS);
}


/* EOF */
