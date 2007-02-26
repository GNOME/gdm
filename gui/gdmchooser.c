/* GDM - The GNOME Display Manager
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

/* gdmchooser discovers hosts running XDMCP on the local network (s),
 * presents a list of them and allows the user to choose one. The
 * selected hostname will be printed on stdout. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#include <X11/Xmd.h>
#include <X11/Xdmcp.h>

#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <glade/glade.h>

#include "gdm.h"
#include "misc.h"
#include "gdmwm.h"
#include "gdmcomm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"

#include "viciousui.h"

static gboolean RUNNING_UNDER_GDM = FALSE;

enum {
	CHOOSER_LIST_ICON_COLUMN = 0,
	CHOOSER_LIST_LABEL_COLUMN,
	CHOOSER_LIST_HOST_COLUMN
};

typedef struct _GdmChooserHost GdmChooserHost;
struct _GdmChooserHost {
    gchar *name;
    gchar *desc;
#ifdef ENABLE_IPV6
    struct in6_addr ia6;
#endif
    struct in_addr ia;
    gint addrtype;    /* Address stored is IPv4 or IPv6 */
    GdkPixbuf *picture;
    gboolean willing;
};


static const gchar *scanning_message = N_("Please wait: scanning local network...");
static const gchar *empty_network = N_("No serving hosts were found.");
static const gchar *active_network = N_("Choose a ho_st to connect to:");
static void gdm_chooser_cancel (/*void*/);

/* XDM chooser style stuff */
static gchar *xdm_address = NULL;
static gchar *client_address = NULL;
static gint connection_type = 0;

/* Exported for glade */
void gdm_chooser_add_host (void);
void gdm_chooser_add_entry_changed (void);
void gdm_chooser_manage (GtkButton *button, gpointer data);
void gdm_chooser_browser_select (GtkWidget *widget,
				 gint selected,
				 GdkEvent *event);
void gdm_chooser_browser_unselect (GtkWidget *widget,
				   gint selected,
				   GdkEvent *event);
void gdm_chooser_xdmcp_discover (void);
void display_chooser_information (void);

#define ADD_TIMEOUT 3000
static guint add_check_handler = 0;

/* if this is received, select that host automatically */
#ifdef ENABLE_IPV6 
static struct in6_addr *added6_addr = NULL;
#endif
static struct in_addr *added_addr = NULL;
static char *added_host = NULL;

static guint scan_time_handler = 0;

#define PING_TIMEOUT 2000
#define PING_TRIES 3
static int ping_tries = PING_TRIES;
static guint ping_try_handler = 0;

/* set in the main function */
static char **stored_argv = NULL;
static int stored_argc = 0;

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

enum {
	GDM_BACKGROUND_NONE = 0,
	GDM_BACKGROUND_IMAGE = 1,
	GDM_BACKGROUND_COLOR = 2
};

static GladeXML *chooser_app;
static GtkWidget *chooser, *manage, *rescan, *cancel, *add_entry, *add_button;
static GtkWidget *status_label;

static GIOChannel *channel;
static GList *chooser_hosts = NULL;
static GdkPixbuf *defhostimg;
static GtkWidget *browser;
static GtkTreeModel *browser_model;
static GdmChooserHost *curhost;

static gboolean have_ipv6;      /* Socket is IPv4 or IPv6 */

static gboolean
find_host_in_list (GdmChooserHost *host, GtkTreeIter *iter)
{
	if (gtk_tree_model_get_iter_first (browser_model, iter)) {
		do {
			GdmChooserHost *lhost;
			gtk_tree_model_get (browser_model, iter,
					    CHOOSER_LIST_HOST_COLUMN, &lhost,
					    -1);
			if (lhost == host)
				return TRUE;
		} while (gtk_tree_model_iter_next (browser_model, iter));
	}
	return FALSE;
}

static void
gdm_chooser_host_dispose (GdmChooserHost *host)
{
    if (!host)
	return;

    if (host->picture != NULL)
	    g_object_unref (G_OBJECT (host->picture));
    host->picture = NULL;

    g_free (host->name);
    host->name = NULL;
    g_free (host->desc);
    host->desc = NULL;
    g_free (host);
}

static GdmChooserHost * 
gdm_chooser_host_alloc (const char *hostname,
			const char *description,
			char *ia,
			int family,
			gboolean willing)
{
    GdmChooserHost *host;
    GdkPixbuf *img;
    gchar *hostimg;
    gchar *hostimgdir;

    host = g_new0 (GdmChooserHost, 1);
    host->name = g_strdup (hostname);
    host->desc = g_strdup (description);
    host->willing = willing;
#ifdef ENABLE_IPV6
    if (family == AF_INET6)
	    memcpy (&host->ia6, (struct in6_addr *)ia, sizeof (struct in6_addr));
    else
#endif
	    memcpy (&host->ia, (struct in_addr *)ia, sizeof (struct in_addr));

    host->addrtype = family;
    chooser_hosts = g_list_prepend (chooser_hosts, host);
    
    if ( ! willing)
	    return host;

    hostimgdir = gdm_config_get_string (GDM_KEY_HOST_IMAGE_DIR); 
    hostimg    = g_strconcat (hostimgdir, "/", hostname, NULL);
    if (g_access (hostimg, R_OK) != 0) {
	    g_free (hostimg);
	    hostimg = g_strconcat (hostimgdir, "/", hostname, ".png", NULL);
    }

    if (g_access (hostimg, R_OK) == 0 &&
	(img = gdk_pixbuf_new_from_file (hostimg, NULL)) != NULL) {
	gint w, h, maxw, maxh;

	w = gdk_pixbuf_get_width (img);
	h = gdk_pixbuf_get_height (img);
	
	maxw = gdm_config_get_int (GDM_KEY_MAX_ICON_WIDTH);
	maxh = gdm_config_get_int (GDM_KEY_MAX_ICON_HEIGHT);

	if (w > h && w > maxw) {
	    h = h * ((gfloat) maxw / w);
	    w = maxw;
	} else if (h > maxh) {
	    w = w * ((gfloat) maxh / h);
	    h = maxh;
	}


	if (w != gdk_pixbuf_get_width (img) ||
	    h != gdk_pixbuf_get_height (img))
		host->picture = gdk_pixbuf_scale_simple (img, w, h,
							 GDK_INTERP_BILINEAR);
	else
		host->picture = g_object_ref (G_OBJECT (img));
	
	g_object_unref (G_OBJECT (img));
    } else if (defhostimg != NULL) {
	    host->picture = (GdkPixbuf *)g_object_ref (G_OBJECT (defhostimg));
    }

    g_free (hostimg);

    return host;
}

static void
gdm_chooser_browser_add_host (GdmChooserHost *host)
{
    gboolean add_this_host = FALSE;

    if (host->willing) {
	    GtkTreeIter iter = {0};
	    const char *addr;
	    char *label;
	    char *name, *desc;
#ifdef ENABLE_IPV6
	    if (host->addrtype == AF_INET6) {   /* IPv6 address */
		static char buffer6[INET6_ADDRSTRLEN];

		addr = inet_ntop (AF_INET6, host->ia6.s6_addr, buffer6, INET6_ADDRSTRLEN);
	    }
	    else /* IPv4 address */
#endif
	    {
		addr = inet_ntoa (host->ia);
	    }

	    name = g_markup_escape_text (host->name, -1);
	    desc = g_markup_escape_text (host->desc, -1);

	    if (strcmp (addr, host->name) == 0)
		    label = g_strdup_printf ("<b>%s</b>\n%s",
					     name,
					     desc);
	    else
		    label = g_strdup_printf ("<b>%s</b> (%s)\n%s",
					     name,
					     addr,
					     desc);

	    g_free (name);
	    g_free (desc);

	    gtk_list_store_append (GTK_LIST_STORE (browser_model), &iter);
	    gtk_list_store_set (GTK_LIST_STORE (browser_model), &iter,
				CHOOSER_LIST_ICON_COLUMN, host->picture,
				CHOOSER_LIST_LABEL_COLUMN, label,
				CHOOSER_LIST_HOST_COLUMN, host,
				-1);
	    g_free (label);

#ifdef ENABLE_IPV6
	    if (added6_addr != NULL && memcmp (&host->ia6, added6_addr,
                                   sizeof (struct in6_addr)) == 0) {
		added6_addr = NULL;
		add_this_host = TRUE;
	    }
#else
	    if (added_addr != NULL &&
		memcmp (&host->ia, added_addr,
			sizeof (struct in_addr)) == 0) {
		added_addr = NULL;
		add_this_host = TRUE;
	    }
#endif
	    if (add_this_host) {
		    GtkTreeSelection *selection;
		    GtkTreePath *path = gtk_tree_model_get_path (browser_model, &iter);
		    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
		    gtk_tree_selection_select_iter (selection, &iter);
		    gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (browser),
						  path, NULL,
						  FALSE, 0.0, 0.0);
		    gtk_tree_path_free (path);
		    gtk_widget_grab_focus (manage);
	    }
	    g_free (added_host);
	    added_host = NULL;
	    if (add_check_handler > 0)
		    g_source_remove (add_check_handler);
	    add_check_handler = 0;
    }

    gtk_widget_set_sensitive (GTK_WIDGET (browser), TRUE);
}

static GdmChooserHost *
gdm_host_known (char *ia, gint family)
{
	GList *li;

	for (li = chooser_hosts; li != NULL; li = li->next) {
		GdmChooserHost *host = li->data;
#ifdef ENABLE_IPV6
		if (family == AF_INET6) {
			if (host->addrtype != AF_INET6)
				continue;
			if ( ! memcmp (&host->ia6, (struct in6_addr *)ia, sizeof (struct in6_addr)))
				return host;
		}
		else
#endif
		if (family == AF_INET) {
			if (host->addrtype != AF_INET)
				continue;
			if ( ! memcmp (&host->ia, (struct in_addr *)ia, sizeof (struct in_addr)))
				return host;
		}
	}
	return NULL;
}

static gboolean
is_loopback_addr (char *ia, gint family)
{
	const char lo[] = {127,0,0,1};

#ifdef ENABLE_IPV6
	if (family == AF_INET6 && IN6_IS_ADDR_LOOPBACK ((struct in6_addr *)ia)) {
	    return TRUE;
	} else
#endif
	if (((family == AF_INET) && (((struct in_addr *) ia)->s_addr == INADDR_LOOPBACK)) || memcmp (&((struct in_addr *)ia)->s_addr, lo, 4) == 0) {
	    return TRUE;
	}
	else {
	    return FALSE;
	}
}

static gboolean
gdm_addr_known (char *ia, gint family)
{
	GSList *li;

	for (li = queryaddr; li != NULL; li = li->next) {
		struct sockaddr *sa = li->data;

#ifdef ENABLE_IPV6
		if (sa->sa_family == AF_INET6) {
			if (family != AF_INET6)
				continue;

			if (!memcmp (&((struct sockaddr_in6 *)sa)->sin6_addr, (struct in6_addr *)ia , sizeof (struct in6_addr)))
				return TRUE;
		}
		else if (sa->sa_family == AF_INET) {
			if (family != AF_INET)
				continue;

			if (!memcmp (&((struct sockaddr_in *)sa)->sin_addr, (struct in_addr *)ia, sizeof (struct in_addr)))
				return TRUE;

		}
#else
		if (memcmp (&((struct sockaddr_in *)sa)->sin_addr, (struct in_addr *)ia, sizeof (struct in_addr)) == 0)
			return TRUE;
#endif
	}
	return FALSE;
}

static gboolean
gdm_chooser_decode_packet (GIOChannel   *source,
			   GIOCondition  condition,
			   gpointer      data)
{
#ifdef ENABLE_IPV6
    char hbuf[NI_MAXHOST];
    struct sockaddr_in6 clnt6_sa;
#endif
    struct sockaddr_in clnt_sa;
    gint sa_len;
    static XdmcpBuffer buf;
    XdmcpHeader header;
    struct hostent *he;
    gchar *hostname = NULL;
    gchar *status = NULL;
    ARRAY8 auth = {0}, host = {0}, stat = {0};
    GdmChooserHost *gh;
    int pipe_buf;
    gboolean host_not_willing = FALSE;

#ifdef PIPE_BUF
    pipe_buf = PIPE_BUF;
#else
    /* apparently Hurd doesn't have PIPE_BUF */
    pipe_buf = fpathconf (1 /*stdout*/, _PC_PIPE_BUF);
    /* could return -1 if no limit */
#endif

    if ( ! (condition & G_IO_IN)) 
        return TRUE;

#ifdef ENABLE_IPV6
    if (have_ipv6) {
	    sa_len = sizeof (struct sockaddr_in6);
	    if (! XdmcpFill (sockfd, &buf, (XdmcpNetaddr) &clnt6_sa, &sa_len))
		    return TRUE;
    } else
#endif
    {
	    sa_len = sizeof (struct sockaddr_in);
	    if (! XdmcpFill (sockfd, &buf, (XdmcpNetaddr) &clnt_sa, &sa_len))
		    return TRUE;
    }

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

	    status = g_strndup ((char *) stat.data, MIN (stat.length, 256));
    } else if (header.opcode == UNWILLING) {
	    /* immaterial, will not be shown */
	    status = NULL;
    } else {
	    return TRUE;
    }
#ifdef ENABLE_IPV6
    /*Since, IPv4 addresses will get extracted as V4 mapped IPv6 address*/

    if (have_ipv6 &&
	IN6_IS_ADDR_V4MAPPED (&(clnt6_sa.sin6_addr))) {
        memset (&clnt_sa, 0, sizeof (clnt_sa));
        memcpy (&(clnt_sa.sin_addr), &(clnt6_sa.sin6_addr.s6_addr[12]), 4);
        clnt_sa.sin_family = AF_INET;
        clnt_sa.sin_port = clnt6_sa.sin6_port;
        clnt6_sa.sin6_family = AF_INET;
    }

    if (have_ipv6 &&
	((struct sockaddr *) &clnt6_sa)->sa_family == AF_INET6) {
        if ( ! is_loopback_addr ((gchar *) &clnt6_sa.sin6_addr, AF_INET6)) {
	    clnt6_sa.sin6_scope_id = 0;
	
	    getnameinfo ((struct sockaddr *)&clnt6_sa, sizeof (struct sockaddr_in6), hbuf, sizeof (hbuf), NULL, 0, 0);

	    hostname = hbuf;

	    if (strlen (hostname) + 1 > pipe_buf)
		goto done;
	    hostname = g_strdup (hostname);

        } else {
	    hostname = g_new0 (char, 1024);
	
	    if (gethostname (hostname, 1023) != 0) {
		g_free (hostname);
		goto done;
	    }
	    added6_addr = NULL;
	    gh = gdm_host_known ((char *)&clnt6_sa.sin6_addr, AF_INET6);
        }
    } else
#endif
    {
        if ( !is_loopback_addr ((char *)&clnt_sa.sin_addr, AF_INET)) {
	    he = gethostbyaddr ((gchar *) &clnt_sa.sin_addr,
				sizeof (struct in_addr),
				AF_INET);

	    hostname = (he && he->h_name) ? he->h_name : inet_ntoa (clnt_sa.sin_addr);
	    if (strlen (hostname) + 1 > pipe_buf)
                   goto done;

	    hostname = g_strdup (hostname);
        } else {
	    hostname = g_new0 (char, 1024);
	    if (gethostname (hostname, 1023) != 0) {
		    g_free (hostname);
		    goto done;
	    }
        }
    }

    /* We can't pipe hostnames larger than this */
    if (pipe_buf > 0 && strlen (hostname)+1 > pipe_buf) {
	    g_free (hostname);
	    goto done;
    }

#ifdef ENABLE_IPV6
    if (have_ipv6 && ((struct sockaddr *) &clnt6_sa)->sa_family == AF_INET6) {
	    gh = gdm_host_known ((char *)&clnt6_sa.sin6_addr, AF_INET6);
	    if (gh == NULL) {
		gh = gdm_chooser_host_alloc (hostname, status, (char *)&clnt6_sa.sin6_addr, AF_INET6, header.opcode == WILLING);
		gdm_chooser_browser_add_host (gh);
	    }
    } else
#endif
    {
	    gh = gdm_host_known ((char *)&clnt_sa.sin_addr, AF_INET);
	    if (gh == NULL) {
		gh = gdm_chooser_host_alloc (hostname, status, (char *)&clnt_sa.sin_addr, AF_INET, header.opcode == WILLING);
		gdm_chooser_browser_add_host (gh);
	    }
    }
    if (gh != NULL) {
      /* server changed it's mind */
	    if (header.opcode == WILLING &&
		! gh->willing) {
		    gh->willing = TRUE;
		    gdm_chooser_browser_add_host (gh);
	    }
	    /* hmmm what about the other change, just ignore
	       for now, it's kind of confusing to just remove
	       servers really */
    }
#ifdef ENABLE_IPV6
    if (have_ipv6 &&
	((struct sockaddr *) &clnt6_sa)->sa_family == AF_INET6 &&
        ! gh->willing &&
	added6_addr != NULL &&
        memcmp (&gh->ia6, added6_addr, sizeof (struct in6_addr)) == 0) {

	    added6_addr = NULL;
	    host_not_willing = TRUE;
    }
    else
#endif
    if (clnt_sa.sin_family == AF_INET &&
	! gh->willing &&
	added_addr != NULL &&
	memcmp (&gh->ia, added_addr, sizeof (struct in_addr)) == 0) {

	    added_addr = NULL;
	    host_not_willing = TRUE;
    }

    if (host_not_willing) {
	    GtkWidget *dialog;
	    gchar *msg;

	    if (add_check_handler > 0)
		    g_source_remove (add_check_handler);
	    add_check_handler = 0;

	    msg = g_strdup_printf (_("The host \"%s\" is not willing "
	                             "to support a login session right now.  "
	                             "Please try again later."),
	                           added_host);

	    dialog = ve_hig_dialog_new
		    (GTK_WINDOW (chooser) /* parent */,
		     GTK_DIALOG_MODAL /* flags */,
		     GTK_MESSAGE_ERROR,
		     GTK_BUTTONS_OK,
		     _("Cannot connect to remote server"),
		     msg);

	    g_free (msg);

	    if (RUNNING_UNDER_GDM)
		    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_wm_no_login_focus_push ();
	    gtk_dialog_run (GTK_DIALOG (dialog));
	    gtk_widget_destroy (dialog);
	    gdm_wm_no_login_focus_pop ();

	    g_free (added_host);
	    added_host = NULL;
    }

    g_free (hostname);
    
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
	int i = 0, num;
	int sock;
	struct ifconf ifc;
	char *buf;
	struct ifreq *ifr;

	sock = socket (AF_INET, SOCK_DGRAM, 0);
#ifdef SIOCGIFNUM
	if (ioctl (sock, SIOCGIFNUM, &num) < 0) {
		num = 64;
	}
#else
	num = 64;
#endif

	ifc.ifc_len = sizeof (struct ifreq) * num;
	ifc.ifc_buf = buf = g_malloc0 (ifc.ifc_len);
	if (ioctl (sock, SIOCGIFCONF, &ifc) < 0) {
		g_free (buf);
		gdm_common_error ("Could not get local addresses!");
		close (sock);
		return;
	}

	ifr = ifc.ifc_req;
	num = ifc.ifc_len / sizeof (struct ifreq);
	for (i = 0 ; i < num ; i++) {
		if ( ! ve_string_empty (ifr[i].ifr_name)) {
			struct ifreq ifreq;
			struct sockaddr_in *ba = NULL;
			struct sockaddr_in *sin = NULL;

			memset (&ifreq, 0, sizeof (ifreq));

			strncpy (ifreq.ifr_name, ifr[i].ifr_name,
				 sizeof (ifreq.ifr_name));
			/* paranoia */
			ifreq.ifr_name[sizeof (ifreq.ifr_name) - 1] = '\0';

			if (ioctl (sock, SIOCGIFFLAGS, &ifreq) < 0) 
				gdm_common_error ("Could not get SIOCGIFFLAGS for %s", ifr[i].ifr_name);

			if ((ifreq.ifr_flags & IFF_UP) == 0 ||
			    (ifreq.ifr_flags & IFF_BROADCAST) == 0 ||
			    ioctl (sock, SIOCGIFBRDADDR, &ifreq) < 0)
				continue;

			ba = (struct sockaddr_in *) &ifreq.ifr_broadaddr;

			sin = g_new0 (struct sockaddr_in, 1);

			sin->sin_family = AF_INET;
			memcpy (&sin->sin_addr, &ba->sin_addr, sizeof (ba->sin_addr));
			bcaddr =  g_slist_append (bcaddr, sin);
		}
	}

	g_free (buf);
}

/* Append multicast address into the list */
#ifdef ENABLE_IPV6
static void
gdm_chooser_find_mcaddr (void)
{
	struct sockaddr_in6 *sin6;
	int sock;       /* Temporary socket for getting information about available interfaces */
	u_char loop = 0; /* Disable multicast for loopback interface */
	int i, num;
	char *buf;
	/* For interfaces' list */
	struct ifconf ifc;
	struct ifreq *ifr = NULL;

	sock = socket (AF_INET, SOCK_DGRAM, 0);
#ifdef SIOCGIFNUM
	if (ioctl (sock, SIOCGIFNUM, &num) < 0) {
		num = 64;
	}
#else
	num = 64;
#endif
	ifc.ifc_len = sizeof (struct ifreq) * num;
	ifc.ifc_buf = buf = malloc (ifc.ifc_len);

	if (setsockopt (sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof (loop)) < 0)
		gdm_common_error ("setsockopt: Could not disable loopback interface for multicasting\n");

	if (ioctl (sock, SIOCGIFCONF, &ifc) >= 0)
		ifr = ifc.ifc_req;
		num = ifc.ifc_len / sizeof (struct ifreq); /* No of interfaces */
		for (i = 0 ; i < num ; i++) {
			struct ifreq ifreq;
			int ifindex;
                                                            
			memset (&ifreq, 0, sizeof (ifreq));
			strncpy (ifreq.ifr_name, ifr[i].ifr_name, sizeof (ifreq.ifr_name));
			ifreq.ifr_name[sizeof (ifreq.ifr_name) - 1] = '\0';

			if (ioctl (sock, SIOCGIFFLAGS, &ifreq) < 0)
				gdm_common_error ("Could not get interface flags for %s\n", ifr[i].ifr_name); 
			ifindex = if_nametoindex (ifr[i].ifr_name);
                                                            
			if ((!(ifreq.ifr_flags & IFF_UP) || (!(ifreq.ifr_flags & IFF_MULTICAST))) || (ifindex == 0 )) {
			/* Not a valid interface or Not up */
				continue;
			}

			sin6 = g_new0 (struct sockaddr_in6, 1);
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = htons (XDM_UDP_PORT);
			sin6->sin6_scope_id = ifindex;
			inet_pton (AF_INET6, gdm_config_get_string (GDM_KEY_MULTICAST_ADDR),
				&sin6->sin6_addr);

			/* bcaddr is also serving for multicast address for IPv6 */
			bcaddr = g_slist_append (bcaddr, sin6);
		}
}
#endif

static gboolean
chooser_scan_time_update (gpointer data)
{
	GList *li;
	scan_time_handler = 0;
	for (li = chooser_hosts; li != NULL; li = li->next) {
		GdmChooserHost *host = (GdmChooserHost *) li->data;
		if (host->willing)
			break;
	}
	if (li != NULL /* something was found */) {
		gtk_label_set_label (GTK_LABEL (status_label), _(active_network));
	} else {
		gtk_label_set_label (GTK_LABEL (status_label), _(empty_network));
	}
	gtk_widget_set_sensitive (GTK_WIDGET (rescan), TRUE);
	return FALSE;
}

static void
do_ping (gboolean full)
{
    struct sockaddr_in sock;
    GSList *bl = bcaddr;
    GSList *ql = queryaddr;
    struct sockaddr *ia;
#ifdef ENABLE_IPV6
    struct sockaddr_in6 sock6;

    memset (&sock6, 0, sizeof (sock6));
    sock6.sin6_family = AF_INET6;
    sock6.sin6_port = htons (XDM_UDP_PORT);
#endif

    sock.sin_family = AF_INET;
    sock.sin_port = htons (XDM_UDP_PORT);

    while (bl) {
	    ia = (struct sockaddr *) bl->data;
#ifdef ENABLE_IPV6
            if (have_ipv6) {    /* Convert the IPv4 broadcast address to v4 mapped v6 address.*/
		if (ia->sa_family == AF_INET) {
		    char tmpaddr[30];
		    struct in6_addr in6;

		    sprintf (tmpaddr, "::ffff:%s", inet_ntoa (((struct sockaddr_in *)(ia))->sin_addr));
		    inet_pton (AF_INET6, tmpaddr, &in6);
		    memcpy (sock6.sin6_addr.s6_addr, in6.s6_addr, sizeof (struct in6_addr));
		    XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock6, (int) sizeof (struct sockaddr_in6));
		}

		else if (ia->sa_family == AF_INET6) {
		    memcpy (sock6.sin6_addr.s6_addr, ((struct sockaddr_in6 *)ia)->sin6_addr.s6_addr, sizeof (struct in6_addr));
		    XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock6, (int) sizeof (struct sockaddr_in6));
		}
	    }
	    else
#endif
	    {
		if (ia->sa_family == AF_INET) {
		    sock.sin_addr.s_addr = ((struct sockaddr_in *)ia)->sin_addr.s_addr;
		    XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
		}
	    }
	    bl = bl->next;
    }

    while (ql != NULL) {
	    ia = (struct sockaddr *) ql->data;

#ifdef ENABLE_IPV6
	    if (have_ipv6) {
		if (ia->sa_family == AF_INET) {
		    char tmpaddr[30];
		    struct in6_addr in6;

		    sprintf (tmpaddr, "::ffff:%s", inet_ntoa (((struct sockaddr_in *)(ia))->sin_addr));
		    inet_pton (AF_INET6, tmpaddr, &in6);

		    if (full || ! gdm_host_known ((char *)&((struct sockaddr_in6 *)ia)->sin6_addr, AF_INET6)) {
			memcpy (sock6.sin6_addr.s6_addr, in6.s6_addr, sizeof (struct in6_addr));
			XdmcpFlush (sockfd, &bcbuf, (XdmcpNetaddr) &sock6, (int) sizeof (struct sockaddr_in6));
		    }
		}

		if (ia->sa_family == AF_INET6) {
		    if (full || ! gdm_host_known ((char *)&((struct sockaddr_in6 *)ia)->sin6_addr, AF_INET6)) {
			memcpy (&sock6.sin6_addr, &((struct sockaddr_in6 *)ia)->sin6_addr, sizeof (struct in6_addr));
			XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock6, (int) sizeof (struct sockaddr_in6));
		    }
		}
	    }
	    else
#endif
	    {
		if (full || ! gdm_host_known ((char *)&((struct sockaddr_in *)ia)->sin_addr, AF_INET)) {
		    sock.sin_addr.s_addr = ((struct sockaddr_in *)ia)->sin_addr.s_addr;
		    XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
		}
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

void
gdm_chooser_xdmcp_discover (void)
{
    GList *hl = chooser_hosts;

    g_free (added_host);
    added_host = NULL;
#ifdef ENABLE_IPV6
    added6_addr = NULL;
#endif
    added_addr = NULL;
    if (add_check_handler > 0)
	    g_source_remove (add_check_handler);
    add_check_handler = 0;

    gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (rescan), FALSE);
    gtk_list_store_clear (GTK_LIST_STORE (browser_model));
    gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);
    gtk_label_set_label (GTK_LABEL (status_label),
			 _(scanning_message));

    while (hl) {
	gdm_chooser_host_dispose ((GdmChooserHost *) hl->data);
	hl = hl->next;
    }

    g_list_free (chooser_hosts);
    chooser_hosts = NULL;

    do_ping (TRUE);

    if (scan_time_handler > 0)
	    g_source_remove (scan_time_handler);
    scan_time_handler = g_timeout_add (gdm_config_get_int (GDM_KEY_SCAN_TIME) * 1000, 
				       chooser_scan_time_update, NULL);

    /* Note we already used up one try */
    ping_tries = PING_TRIES - 1;
    if (ping_try_handler > 0)
	    g_source_remove (ping_try_handler);
    ping_try_handler = g_timeout_add (PING_TIMEOUT, ping_try, NULL);
}

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
      t = HexChar(*s) << 4;
      s++;
      if (!ishexdigit(*s))
 return 1;
      t += HexChar(*s);
      s++;
      *d++ = t;
      len -= 2;
    }
  return len;
}

static void
gdm_chooser_add_hosts (char **hosts)
{
	struct hostent *hostent;
#ifdef ENABLE_IPV6
	struct sockaddr_in6 *qa6 = NULL;
	struct addrinfo hints, *result, *res;
#endif
	struct sockaddr_in* qa = NULL;
	int used_addr = 0;
	int i;

	for (i = 0; hosts != NULL && hosts[i] != NULL; i++) {
		const char *name = hosts[i];

		if (strcmp (name, "BROADCAST") == 0) {
			gdm_chooser_find_bcaddr ();
			continue;
		}
#ifdef ENABLE_IPV6
		if (strcmp (name, "MULTICAST") == 0) {
			gdm_chooser_find_mcaddr ();
			continue;
		}
#endif
		if (used_addr == AF_INET || !qa) {
			qa = g_new0 (struct sockaddr_in, 1);
			memset (qa, 0, sizeof (*qa));
			qa->sin_family = AF_INET;
		}
#ifdef ENABLE_IPV6
		if (used_addr == AF_INET6 || !qa6) {
			 qa6 = g_new0 (struct sockaddr_in6, 1);
			 memset (qa6, 0, sizeof (qa6));
			 qa6->sin6_family = AF_INET6;
		}

		result = NULL;
		memset (&hints, 0, sizeof (hints));
		hints.ai_socktype = SOCK_STREAM;

		if ((strlen (name) == 32) && from_hex (name, (char *) &qa6->sin6_addr, strlen (name)) == 0) {
			queryaddr = g_slist_append (queryaddr, qa6);
			g_free (qa);
			qa = NULL;
			used_addr = AF_INET6;
		}
		else
#endif
		if ((strlen (name) == 8) && (from_hex (name, (char *) &qa->sin_addr, strlen (name)) == 0)) {
			queryaddr = g_slist_append (queryaddr, qa);
#ifdef ENABLE_IPV6
			g_free (qa6);
			qa6 = NULL;
#endif
			used_addr = AF_INET;
		}
		else
#ifdef ENABLE_IPV6
		if (inet_pton (AF_INET6, name, &qa6->sin6_addr) > 0) {
			queryaddr = g_slist_append (queryaddr, qa6);
			g_free (qa);
			qa = NULL;
			used_addr = AF_INET6;
		}
		else
#endif
		if ((qa->sin_addr.s_addr = inet_addr (name)) != -1) {
			queryaddr = g_slist_append (queryaddr, qa);
#ifdef ENABLE_IPV6
			g_free (qa6);
			qa6 = NULL;
#endif
			used_addr = AF_INET;
		}
		else
#ifdef ENABLE_IPV6
		if (getaddrinfo (name, NULL, &hints, &result) == 0) {
			for (res = result; res; res = res->ai_next) {
				if (res && res->ai_family == AF_INET6) {
					memmove (qa6, res->ai_addr, res->ai_addrlen);
					queryaddr = g_slist_append (queryaddr, qa6);
					g_free (qa);
					qa = NULL;
					used_addr = AF_INET6;
                           }
				if (res && res->ai_family == AF_INET) {
					memmove (qa, res->ai_addr, res->ai_addrlen);
					queryaddr = g_slist_append (queryaddr, qa);
					g_free (qa6);
					qa6 = NULL;
					used_addr = AF_INET;
				}
			}
		} else
#endif
		if ((hostent = gethostbyname (name)) != NULL
			 && hostent->h_addrtype == AF_INET
			 && hostent->h_length == 4) {
			qa->sin_family = AF_INET;
			memmove (&qa->sin_addr, hostent->h_addr, 4);
			queryaddr = g_slist_append (queryaddr, qa);
#ifdef ENABLE_IPV6
			g_free (qa6);
			qa6 = NULL;
#endif
			used_addr = AF_INET;
		} else {
			continue; /* not a valid address */
		}
	}

	if (bcaddr == NULL &&
	    queryaddr == NULL)
		gdm_chooser_find_bcaddr ();
}

static void
gdm_chooser_xdmcp_init (char **hosts)
{
    static XdmcpHeader header;
    gint sockopts = 1;

    /* Open socket for communication */
#ifdef ENABLE_IPV6
    if ((sockfd = socket (AF_INET6, SOCK_DGRAM, 0)) == -1)
	have_ipv6 = FALSE;
    else
	have_ipv6 = TRUE;
#endif
    if ( ! have_ipv6) {
	if ((sockfd = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
	    gdm_common_fail_exit ("Could not create socket!");
	}
    }

    if (setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST,
        (char *) &sockopts, sizeof (sockopts)) < 0) {
	gdm_common_fail_exit ("Could not set socket options!");
    }

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
    g_io_channel_set_encoding (channel, NULL, NULL);
    g_io_channel_set_buffered (channel, FALSE);
    g_io_add_watch_full (channel, G_PRIORITY_DEFAULT,
			G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
			gdm_chooser_decode_packet,
			GINT_TO_POINTER (sockfd), NULL);
    g_io_channel_unref (channel);

    gdm_chooser_xdmcp_discover ();
}

static void
gdm_chooser_choose_host (const char *hostname)
{
  ARRAY8 tmparr;
#ifndef ENABLE_IPV6
  struct hostent *hentry;
#endif

  printf ("\n%s\n", curhost->name);
  fflush (stdout);
  if (xdm_address != NULL) {
#ifdef ENABLE_IPV6
      int status;
      struct sockaddr_in6 in6_addr;
      struct addrinfo hints, *result;
#endif
      struct sockaddr_in in_addr;
      char xdm_addr[32];
      char client_addr[32];
      int fd;
      char buf[1024];
      XdmcpBuffer buffer;
      long family, port, addr;

      if (strlen (xdm_address) > 64 ||
	  from_hex (xdm_address, xdm_addr, strlen (xdm_address)) != 0) {
	      gdm_common_fail_exit ("gdm_chooser_chooser_host: Invalid xdm address.");
      }

      family = (xdm_addr[0] << 8) | xdm_addr[1];
      port = (xdm_addr[2] << 8) | xdm_addr[3];

#ifdef ENABLE_IPV6
      if (family == AF_INET6) {
	  memset (&in6_addr, 0, sizeof (in6_addr));

	  in6_addr.sin6_port   = htons (port);
	  in6_addr.sin6_family = AF_INET6;

	  memcpy (&in6_addr.sin6_addr, &xdm_address[4], 16);

	  if ((fd = socket (PF_INET6, SOCK_STREAM, 0)) == -1) {
	      gdm_common_fail_exit ("gdm_chooser_choose_host: Could not create response socket.");
	  }

	  if (connect (fd, (struct sockaddr *) &in6_addr,
              sizeof (in6_addr)) == -1) {

	      gdm_common_fail_exit ("gdm_chooser_chooser_host: Could not connect to xdm.");
	  }
      } else
#endif
      {
	  addr = (xdm_addr[4] << 24) | (xdm_addr[5] << 16) |
                 (xdm_addr[6] << 8)  | xdm_addr[7];

	  in_addr.sin_family      = AF_INET;
	  in_addr.sin_port        = htons (port);
	  in_addr.sin_addr.s_addr = htonl (addr);

	  if ((fd = socket (PF_INET, SOCK_STREAM, 0)) == -1) {
	      gdm_common_fail_exit ("gdm_chooser_chooser_host: Could not create response socket.");
	  }

	  if (connect (fd, (struct sockaddr *) &in_addr,
              sizeof (in_addr)) == -1) {

	      gdm_common_fail_exit ("gdm_chooser_chooser_host: Could not connect to xdm.");
	  }
      }

      buffer.data = (BYTE *) buf;
      buffer.size = sizeof (buf);
      buffer.pointer = 0;
      buffer.count = 0;

      if (strlen (client_address) > 64 || from_hex (client_address,
          client_addr, strlen (client_address)) != 0) {

	   gdm_common_fail_exit ("gdm_chooser_chooser_host: Invalid client address.");
      }

      tmparr.data   = (BYTE *) client_addr;
      tmparr.length = strlen (client_address) / 2;

      XdmcpWriteARRAY8 (&buffer, &tmparr);
      XdmcpWriteCARD16 (&buffer, (CARD16) connection_type);

#ifdef ENABLE_IPV6
      result = NULL;
      memset (&hints, 0, sizeof (hints));
      hints.ai_socktype = SOCK_STREAM;

      status = getaddrinfo (hostname, NULL, &hints, &result);

      if (status != 0) {
	   gdm_common_fail_exit ("gdm_chooser_chooser_host: Could not get host entry for %s",
	       hostname);
      }

      if (result->ai_family == AF_INET6)
	  tmparr.length = 16;
      if (result->ai_family == AF_INET)
	  tmparr.length = 4;
      tmparr.data = (BYTE *) result->ai_addr;
#else
      hentry = gethostbyname (hostname);

      if (!hentry) {
	  gdm_common_fail_exit ("gdm_chooser_chooser_host: Could not get host entry for %s",
	     hostname);
      }

      tmparr.data   = (BYTE *) hentry->h_addr_list[0]; /* XXX */
      tmparr.length = 4;

#endif
      XdmcpWriteARRAY8 (&buffer, &tmparr);
      write (fd, (char *) buffer.data, buffer.pointer);
      close (fd);
  }
}

static gboolean
add_check (gpointer data)
{
	gboolean check = FALSE;

#ifdef ENABLE_IPV6
	if (have_ipv6 && added6_addr != NULL)
		check = TRUE;
	else
#endif
	if ((! have_ipv6) && added_addr != NULL)
		check = TRUE;

	if (check) {
		GtkWidget *dialog;
		gchar *msg;
		
		msg = g_strdup_printf (_("Did not receive any response from host \"%s\" "
		                         "in %d seconds.  Perhaps the host is not "
		                         "turned on, or is not willing to support a "
		                         "login session right now.  Please try again "
		                         "later."),
		                       added_host,
		                       ADD_TIMEOUT / 1000);

		dialog = ve_hig_dialog_new
			(GTK_WINDOW (chooser) /* parent */,
			 GTK_DIALOG_MODAL /* flags */,
			 GTK_MESSAGE_ERROR,
			 GTK_BUTTONS_OK,
			 _("Did not receive response from server"),
			 msg);

		g_free (msg);

		if (RUNNING_UNDER_GDM)
			gdm_wm_center_window (GTK_WINDOW (dialog));

		gdm_wm_no_login_focus_push ();
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		gdm_wm_no_login_focus_pop ();
	}
	add_check_handler = 0;
	return FALSE;
}

void
gdm_chooser_add_host (void)
{
	struct hostent *hostent;
	struct sockaddr_in *qa;
	GdmChooserHost *host = NULL;
	struct sockaddr_in sock;
	gboolean status  = FALSE;
	const char *name;
#ifdef ENABLE_IPV6
	struct sockaddr_in6 *qa6;
	struct sockaddr_in6 sock6;
	struct addrinfo hints, *result;

	result = NULL;
	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_DGRAM;
#endif

	name = gtk_entry_get_text (GTK_ENTRY (add_entry));
	if (ve_string_empty (name))
		return;

	qa = g_new0 (struct sockaddr_in, 1);
	qa->sin_family = AF_INET;
#ifdef ENABLE_IPV6
	qa6 = g_new0 (struct sockaddr_in6, 1);
	qa6->sin6_family = AF_INET6;

	if (have_ipv6 && strlen (name) == 32 && 
	    from_hex (name, (char *) &qa6->sin6_addr, strlen (name)) == 0) ;

	else
#endif
	if (strlen (name) == 8 &&
	    from_hex (name, (char *) &qa->sin_addr, strlen (name)) == 0) {
#ifdef ENABLE_IPV6
		if (have_ipv6) {
			char tmpaddr[30];

			sprintf (tmpaddr, "::ffff:%s", inet_ntoa (qa->sin_addr));
			inet_pton (AF_INET6, tmpaddr, &qa6->sin6_addr);
		}
#endif
	}
	else
#ifdef ENABLE_IPV6
	if (have_ipv6 && inet_pton (AF_INET6, name, &qa6->sin6_addr) > 0) ;
	else
#endif
	if (inet_aton (name, &(qa->sin_addr))) {
#ifdef ENABLE_IPV6
		if (have_ipv6) {
			char tmpaddr[30];

			sprintf (tmpaddr, "::ffff:%s", inet_ntoa (qa->sin_addr));
			inet_pton (AF_INET6, tmpaddr, &qa6->sin6_addr);
		}
#endif
	}
	else
#ifdef ENABLE_IPV6
	if (getaddrinfo (name, NULL, &hints, &result) == 0) {
		if (result->ai_family == AF_INET6) {
			memcpy (qa6, (struct sockaddr_in6 *)result->ai_addr, result->ai_addrlen);
		}
		else if (result->ai_family == AF_INET) {
			if (have_ipv6) {
				char tmpaddr [30];

				sprintf (tmpaddr, "::ffff:%s",
				  inet_ntoa (((struct sockaddr_in *)result->ai_addr)->sin_addr));
				inet_pton (AF_INET6, tmpaddr, &qa6->sin6_addr);
			}
		}
	}
	else
#endif
	if ((hostent = gethostbyname (name)) != NULL &&
	     hostent->h_addrtype == AF_INET && hostent->h_length == 4) {
		memmove (&qa->sin_addr, hostent->h_addr, 4);
	} else {
		GtkWidget *dialog;
		gchar *msg;

		msg = g_strdup_printf (_("Cannot find the host \"%s\". "
		                         "Perhaps you have mistyped it."),
		                         name);

		dialog = ve_hig_dialog_new
		(GTK_WINDOW (chooser) /* parent */,
		 GTK_DIALOG_MODAL /* flags */,
		 GTK_MESSAGE_ERROR,
		 GTK_BUTTONS_OK,
		 _("Cannot find host"),
		 msg);
		 
		g_free (msg);

		if (RUNNING_UNDER_GDM)
			gdm_wm_center_window (GTK_WINDOW (dialog));

		gdm_wm_no_login_focus_push ();
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		gdm_wm_no_login_focus_pop ();
		g_free (qa);
#ifdef ENABLE_IPV6
		g_free (qa6);
#endif
		return; /* not a valid address */
	}

#ifdef ENABLE_IPV6
	if (have_ipv6) {
		memset (&sock6, 0, sizeof (struct sockaddr_in6));
		sock6.sin6_family = AF_INET6;
		sock6.sin6_port = htons (XDM_UDP_PORT);
		status = gdm_addr_known ((char *)&qa6->sin6_addr, AF_INET6);
		if ( ! status) {
			queryaddr = g_slist_append (queryaddr, qa6);
		}
		if (IN6_IS_ADDR_V4MAPPED (&qa6->sin6_addr)) {
			memcpy (&qa->sin_addr, &(qa6->sin6_addr.s6_addr[12]), 4);
			host = gdm_host_known ((char *) &qa->sin_addr, AF_INET);
		}
		else
			host = gdm_host_known ((char *) &qa6->sin6_addr, AF_INET6);
	} else
#endif
	{
		memset (&sock, 0, sizeof (struct sockaddr_in));
		sock.sin_family = AF_INET;
		sock.sin_port = htons (XDM_UDP_PORT);
		status = gdm_addr_known ((char *)&qa->sin_addr, AF_INET);
		if ( ! status) {
			queryaddr = g_slist_append (queryaddr, qa);
		}
		host = gdm_host_known ((char *) &qa->sin_addr, AF_INET);
	}

	if (host != NULL) {
		GtkTreeIter iter = {0};
		if (find_host_in_list (host, &iter)) {
			GtkTreeSelection *selection;
			GtkTreePath *path = gtk_tree_model_get_path (browser_model, &iter);
			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
			gtk_tree_selection_select_iter (selection, &iter);
			gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (browser),
						      path, NULL,
						      FALSE, 0.0, 0.0);
			gtk_tree_path_free (path);
			gtk_widget_grab_focus (manage);
		} else {
			/* hmm, probably not willing, ping the host then for
			   good measure */
#ifdef ENABLE_IPV6
			if (have_ipv6) {
				memcpy (&sock6.sin6_addr, &qa6->sin6_addr, sizeof (qa6->sin6_addr));
				XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock6, (int) sizeof (struct sockaddr_in6));
				added6_addr = &qa6->sin6_addr;
				if (IN6_IS_ADDR_V4MAPPED (added6_addr))
					added_addr = (struct in_addr *)&(qa6->sin6_addr.s6_addr[12]);
			} else
#endif
			{
				sock.sin_addr.s_addr = qa->sin_addr.s_addr;
				XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
				added_addr = &qa->sin_addr;
			}
			g_free (added_host);
			added_host = g_strdup (name);

			if (add_check_handler > 0)
				g_source_remove (add_check_handler);
			add_check_handler = g_timeout_add (ADD_TIMEOUT,
							   add_check, NULL);
		}

		/* empty the text entry to indicate success */
		gtk_entry_set_text (GTK_ENTRY (add_entry), "");
		g_free (qa);
#ifdef ENABLE_IPV6
		g_free (qa6);
#endif
		return;
	}
#ifdef ENABLE_IPV6
	if (have_ipv6) {
		added6_addr = &qa6->sin6_addr;

		if (IN6_IS_ADDR_V4MAPPED (added6_addr))
			added_addr = (struct in_addr *)&(qa6->sin6_addr.s6_addr[12]);

		memcpy (&sock6.sin6_addr, &qa6->sin6_addr, sizeof (struct in6_addr));
		XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock6, (int)sizeof (struct sockaddr_in6));
	} else
#endif
	{
		added_addr = &qa->sin_addr;

		/* and send out the query */
		sock.sin_addr.s_addr = qa->sin_addr.s_addr;
		XdmcpFlush (sockfd, &querybuf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in));
	}
	g_free (added_host);
	added_host = g_strdup (name);
	if (add_check_handler > 0)
		g_source_remove (add_check_handler);
	add_check_handler = g_timeout_add (ADD_TIMEOUT,
					   add_check, NULL);

	/* empty the text entry to indicate success */
	gtk_entry_set_text (GTK_ENTRY (add_entry), "");

	g_free (qa);
#ifdef ENABLE_IPV6
	g_free (qa6);
#endif
}

void
gdm_chooser_add_entry_changed (void)
{
	const char *name;

	name = gtk_entry_get_text (GTK_ENTRY (add_entry));
	gtk_widget_set_sensitive (add_button, ! ve_string_empty (name));
}

static void
gdm_chooser_cancel (int sig)
{
    if (scan_time_handler > 0) {
	    g_source_remove (scan_time_handler);
	    scan_time_handler = 0;
    }

    closelog ();
    /* exit rather gtk_main_quit, it's just safer this way we don't
       have to worry about random whackiness happening */
    exit (EXIT_SUCCESS);
}


void
gdm_chooser_manage (GtkButton *button, gpointer data)
{
    if (scan_time_handler > 0) {
	    g_source_remove (scan_time_handler);
	    scan_time_handler = 0;
    }

    if (curhost)
      gdm_chooser_choose_host (curhost->name);
   
    closelog ();

    /* exit rather gtk_main_quit, it's just safer this way we don't
       have to worry about random whackiness happening */
    exit (EXIT_SUCCESS);
}

static void
host_selected (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *tm = NULL;
	GtkTreeIter iter = {0};

	curhost = NULL;

	if (gtk_tree_selection_get_selected (selection, &tm, &iter)) {
		gtk_tree_model_get (tm, &iter, CHOOSER_LIST_HOST_COLUMN,
				    &curhost, -1);
	}

	gtk_widget_set_sensitive (manage, curhost != NULL);
}

static void
row_activated (GtkTreeView *tree_view,
	       GtkTreePath *path,
	       GtkTreeViewColumn *column)
{
	if (curhost != NULL)
		gdm_chooser_manage (NULL, NULL);
}

void
display_chooser_information (void)
{
	GtkWidget *dialog;

	/* How to get HIG compliance? */
	dialog = gtk_message_dialog_new
		(GTK_WINDOW (chooser) /* parent */,
		 GTK_DIALOG_MODAL /* flags */,
		 GTK_MESSAGE_INFO,
		 GTK_BUTTONS_OK,
		 _("The main area of this application shows the hosts on "
		   "the local network that have \"XDMCP\" enabled. This "
		   "allows users to login remotely to other computers as "
		   "if they were logged on using the console.\n\n"
		   "You can rescan the network for new hosts by clicking "
		   "\"Refresh\".  When you have selected a host click "
		   "\"Connect\" to open a session to that computer."));
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

	if (RUNNING_UNDER_GDM)
		gdm_wm_center_window (GTK_WINDOW (dialog));

	gdm_wm_no_login_focus_push ();
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	gdm_wm_no_login_focus_pop ();
}

static void 
gdm_chooser_gui_init (void)
{
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	gchar *defaulthosticon;
	int width;
	int height;

	glade_helper_add_glade_directory (GDM_GLADE_DIR);
	glade_helper_search_gnome_dirs (FALSE);

    /* Enable theme */
    if (RUNNING_UNDER_GDM) {
	const char *theme_name;

	if ( ! ve_string_empty (gdm_config_get_string (GDM_KEY_GTKRC)))
		gtk_rc_parse (gdm_config_get_string (GDM_KEY_GTKRC));

	theme_name = g_getenv ("GDM_GTK_THEME");
	if (ve_string_empty (theme_name))
		theme_name = gdm_config_get_string (GDM_KEY_GTK_THEME);

	if ( ! ve_string_empty (theme_name)) {
		gdm_set_theme (theme_name);
	}
    }

    defaulthosticon = gdm_config_get_string (GDM_KEY_DEFAULT_HOST_IMG);

    /* Load default host image */
    if (g_access (defaulthosticon, R_OK) != 0) {
	gdm_common_error ("Could not open default host icon: %s", defaulthosticon);
	/* bogus image */
	defhostimg = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				     FALSE /* has_alpha */,
				     8 /* bits_per_sample */,
				     48 /* width */,
				     48 /* height */);
    } else {
	defhostimg = gdk_pixbuf_new_from_file (defaulthosticon, NULL);
    }

    /* Main window */
    chooser_app = glade_helper_load ("gdmchooser.glade",
				     "gdmchooser_main",
				     GTK_TYPE_DIALOG,
				     FALSE /* dump_on_destroy */);
    glade_xml_signal_autoconnect (chooser_app);
   
    chooser = glade_helper_get (chooser_app, "gdmchooser_main",
				GTK_TYPE_DIALOG);
    manage = glade_helper_get (chooser_app, "connect_button",
			       GTK_TYPE_BUTTON);
    rescan = glade_helper_get (chooser_app, "rescan_button",
			       GTK_TYPE_BUTTON);
    cancel = glade_helper_get (chooser_app, "quit_button",
			       GTK_TYPE_BUTTON);
    status_label = glade_helper_get (chooser_app, "status_label",
				     GTK_TYPE_LABEL);
    add_entry = glade_helper_get (chooser_app, "add_entry",
				  GTK_TYPE_ENTRY);
    add_button = glade_helper_get (chooser_app, "add_button",
				   GTK_TYPE_BUTTON);

    browser = glade_helper_get (chooser_app, "chooser_iconlist",
				GTK_TYPE_TREE_VIEW);

    gtk_dialog_set_has_separator (GTK_DIALOG (chooser), FALSE);

    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (browser), TRUE);

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

    g_signal_connect (selection, "changed",
		      G_CALLBACK (host_selected),
		      NULL);
    g_signal_connect (browser, "row_activated",
		      G_CALLBACK (row_activated),
		      NULL);

    browser_model = (GtkTreeModel *)gtk_list_store_new (3,
							GDK_TYPE_PIXBUF,
							G_TYPE_STRING,
							G_TYPE_POINTER);
    gtk_tree_view_set_model (GTK_TREE_VIEW (browser), browser_model);
    column = gtk_tree_view_column_new_with_attributes
	    ("Icon",
	     gtk_cell_renderer_pixbuf_new (),
	     "pixbuf", CHOOSER_LIST_ICON_COLUMN,
	     NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (browser), column);

    column = gtk_tree_view_column_new_with_attributes
	    ("Hostname",
	     gtk_cell_renderer_text_new (),
	     "markup", CHOOSER_LIST_LABEL_COLUMN,
	     NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (browser), column);

    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (browser_model),
					  CHOOSER_LIST_LABEL_COLUMN,
					  GTK_SORT_ASCENDING);


    if ( ! gdm_config_get_bool (GDM_KEY_ALLOW_ADD)) {
	    GtkWidget *w = glade_helper_get (chooser_app, "add_hbox",
					     GTK_TYPE_HBOX);
	    gtk_widget_hide (w);
    }

    gtk_window_get_size (GTK_WINDOW (chooser),
			 &width, &height);
    if (RUNNING_UNDER_GDM) {
	    if (width > gdm_wm_screen.width)
		    width = gdm_wm_screen.width;
	    if (height > gdm_wm_screen.height)
		    height = gdm_wm_screen.height;
    } else {
	    if (width > gdk_screen_width ())
		    width = gdk_screen_width ();
	    if (height > gdk_screen_height ())
		    height = gdk_screen_height ();
    }
    gtk_widget_set_size_request (GTK_WIDGET (chooser), 
				 width, height);
    gtk_window_set_default_size (GTK_WINDOW (chooser), 
				 width, height);
    gtk_window_resize (GTK_WINDOW (chooser), 
		       width, height);


    /* cursor blinking is evil on remote displays, don't do it forever */
    gdm_common_setup_blinking ();
    gdm_common_setup_blinking_entry (add_entry);

    if (RUNNING_UNDER_GDM) {
	    gtk_widget_show_now (chooser);
	    gdm_wm_center_window (GTK_WINDOW (chooser));
    }
}

/* 
 * If new configuration keys are added to this program, make sure to add the
 * key to the gdm_read_config and gdm_reread_config functions.
 */
static gboolean
gdm_read_config (void)
{
	/* Read config data in bulk */
	gdmcomm_comm_bulk_start ();

	gdmcomm_set_debug (gdm_config_get_bool (GDM_KEY_DEBUG));

	/*
	 * Read all the keys at once and close sockets connection so we do
	 * not have to keep the socket open.  
	 */
	gdm_config_get_string (GDM_KEY_HOSTS);
	gdm_config_get_string (GDM_KEY_GTKRC);
	gdm_config_get_string (GDM_KEY_GTK_THEME);
	gdm_config_get_string (GDM_KEY_DEFAULT_HOST_IMG);
	gdm_config_get_string (GDM_KEY_HOST_IMAGE_DIR);
	gdm_config_get_string (GDM_KEY_MULTICAST_ADDR);
	gdm_config_get_string (GDM_KEY_BACKGROUND_COLOR);
	gdm_config_get_int    (GDM_KEY_XINERAMA_SCREEN);
	gdm_config_get_int    (GDM_KEY_MAX_ICON_WIDTH);
	gdm_config_get_int    (GDM_KEY_MAX_ICON_HEIGHT);
	gdm_config_get_int    (GDM_KEY_SCAN_TIME);
	gdm_config_get_int    (GDM_KEY_BACKGROUND_TYPE);
	gdm_config_get_bool   (GDM_KEY_ALLOW_ADD);
	gdm_config_get_bool   (GDM_KEY_BROADCAST);
	gdm_config_get_bool   (GDM_KEY_MULTICAST);

	gdmcomm_comm_bulk_stop ();

	return FALSE;
}

static gboolean
gdm_reread_config (int sig, gpointer data)
{
	/* reparse config stuff here.  At least ones we care about */

	/* Read config data in bulk */
	gdmcomm_comm_bulk_start ();

	if (gdm_config_reload_bool (GDM_KEY_DEBUG))
		gdmcomm_set_debug (gdm_config_get_bool (GDM_KEY_DEBUG));

	/* FIXME: The following is evil, we should update on the fly rather
	 * then just restarting */
	/* Also we may not need to check ALL those keys but just a few */
	if (gdm_config_reload_string (GDM_KEY_HOSTS) ||
	    gdm_config_reload_string (GDM_KEY_GTKRC) ||
	    gdm_config_reload_string (GDM_KEY_GTK_THEME) ||
	    gdm_config_reload_string (GDM_KEY_DEFAULT_HOST_IMG) ||
	    gdm_config_reload_string (GDM_KEY_HOST_IMAGE_DIR) ||
	    gdm_config_reload_string (GDM_KEY_MULTICAST_ADDR) ||
	    gdm_config_reload_int    (GDM_KEY_XINERAMA_SCREEN) ||
	    gdm_config_reload_int    (GDM_KEY_MAX_ICON_WIDTH) ||
	    gdm_config_reload_int    (GDM_KEY_MAX_ICON_HEIGHT) ||
	    gdm_config_reload_int    (GDM_KEY_SCAN_TIME) ||
	    gdm_config_reload_bool   (GDM_KEY_ALLOW_ADD) ||
	    gdm_config_reload_bool   (GDM_KEY_BROADCAST) ||
	    gdm_config_reload_bool   (GDM_KEY_MULTICAST)) {

		if (RUNNING_UNDER_GDM) {
			/* Set busy cursor */
			gdm_common_setup_cursor (GDK_WATCH);
			gdm_wm_save_wm_order ();
		}

		/* we don't need to tell the slave that we're restarting
		   it doesn't care about our state.  Unlike with the greeter */
		execvp (stored_argv[0], stored_argv);
		_exit (DISPLAY_REMANAGE);
	}

	/* we only use the color and do it for all types except NONE */
	if (gdm_config_reload_string (GDM_KEY_BACKGROUND_COLOR) ||
	    gdm_config_reload_int    (GDM_KEY_BACKGROUND_TYPE)) {

		if (gdm_config_get_int (GDM_KEY_BACKGROUND_TYPE) != GDM_BACKGROUND_NONE) {
			gdm_common_setup_background_color (gdm_config_get_string
				(GDM_KEY_BACKGROUND_COLOR));
		}
	}

	gdmcomm_comm_bulk_stop ();

	return TRUE;
}


static void
gdm_chooser_signals_init (void)
{
    struct sigaction hup;
    struct sigaction term;
    sigset_t mask;

    ve_signal_add (SIGHUP, gdm_reread_config, NULL);

    hup.sa_handler = ve_signal_notify;
    hup.sa_flags = 0;
    sigemptyset (&hup.sa_mask);
    sigaddset (&hup.sa_mask, SIGCHLD);

    term.sa_handler = gdm_chooser_cancel;
    term.sa_flags = 0;
    sigemptyset (&term.sa_mask);

    if (sigaction (SIGHUP, &hup, NULL) < 0) {
        gdm_common_fail_exit ("%s: Error setting up %s signal handler: %s",
	    "gdm_signals_init", "HUP", strerror (errno));
    }

    if (sigaction (SIGINT, &term, NULL) < 0) {
        gdm_common_fail_exit ("%s: Error setting up %s signal handler: %s",
           "gdm_signals_init", "INT", strerror (errno));
    }

    if (sigaction (SIGTERM, &term, NULL) < 0) {
        gdm_common_fail_exit ("%s: Error setting up %s signal handler: %s",
           "gdm_signals_init", "TERM", strerror (errno));
    }

    sigfillset (&mask);
    sigdelset (&mask, SIGTERM);
    sigdelset (&mask, SIGHUP);
    sigdelset (&mask, SIGINT);
    
    if (sigprocmask (SIG_SETMASK, &mask, NULL) == -1) 
	gdm_common_fail_exit ("Could not set signal mask!");
}

GOptionEntry chooser_options [] = {
       { "xdmaddress", '\0', 0, G_OPTION_ARG_STRING, &xdm_address,
          N_("Socket for xdm communication"), N_("SOCKET") },
       { "clientaddress", '\0', 0, G_OPTION_ARG_STRING, &client_address,
          N_("Client address to return in response to xdm"), N_("ADDRESS") },
       { "connectionType", '\0', 0, G_OPTION_ARG_INT, &connection_type,
          N_("Connection type to return in response to xdm"), N_("TYPE") },
       { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &chooser_hosts,
          NULL, NULL },
       { NULL }
 };

static gboolean
gdm_event (GSignalInvocationHint *ihint,
	   guint		n_param_values,
	   const GValue	       *param_values,
	   gpointer		data)
{
	GdkEvent *event;

	/* HAAAAAAAAAAAAAAAAACK */
	/* Since the user has not logged in yet and may have left/right
	 * mouse buttons switched, we just translate every right mouse click
	 * to a left mouse click */
	if (n_param_values != 2 ||
	    !G_VALUE_HOLDS (&param_values[1], GDK_TYPE_EVENT))
	  return FALSE;
	
	event = g_value_get_boxed (&param_values[1]);
	if ((event->type == GDK_BUTTON_PRESS ||
	     event->type == GDK_2BUTTON_PRESS ||
	     event->type == GDK_3BUTTON_PRESS ||
	     event->type == GDK_BUTTON_RELEASE)
	    && event->button.button == 3)
		event->button.button = 1;

	return TRUE;
}      

int 
main (int argc, char *argv[])
{
    gchar *GdmHosts;
    gchar **hosts_opt = NULL;
    GOptionContext *ctx;
    const char *gdm_version;
    int i;
    guint sid;

    stored_argv = g_new0 (char *, argc + 1);
    for (i = 0; i < argc; i++)
	    stored_argv[i] = g_strdup (argv[i]);
    stored_argv[i] = NULL;
    stored_argc = argc;

    if (g_getenv ("RUNNING_UNDER_GDM") != NULL)
	    RUNNING_UNDER_GDM = TRUE;

    gdm_common_openlog ("gdmchooser", LOG_PID, LOG_DAEMON);

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    gtk_init (&argc, &argv);

    ctx = g_option_context_new (_("- gdm login chooser")); 
    g_option_context_add_main_entries(ctx, chooser_options, _("main options"));
    g_option_context_parse(ctx, &argc, &argv, NULL);
    g_option_context_free(ctx);

    glade_init ();

    /* Read all configuration at once, so the values get cached */
    gdm_read_config ();

    GdmHosts = g_strdup (gdm_config_get_string (GDM_KEY_HOSTS));

    /* if broadcasting, then append BROADCAST to hosts */
    if (gdm_config_get_bool (GDM_KEY_BROADCAST)) {
	    gchar *tmp;
	    if (ve_string_empty (GdmHosts)) {
		    tmp = "BROADCAST";
	    } else {
		    tmp = g_strconcat (GdmHosts, ",BROADCAST", NULL);
	    }
	    g_free (GdmHosts);
	    GdmHosts = tmp;
    }

#ifdef ENABLE_IPV6
    if (gdm_config_get_bool (GDM_KEY_MULTICAST)) {
	    gchar *tmp;
	    if (ve_string_empty (GdmHosts)) {
		    tmp = "MULTICAST";
	    } else {
		    tmp = g_strconcat (GdmHosts, ",MULTICAST", NULL);
	    }
	    g_free (GdmHosts);
	    GdmHosts = tmp;
    }
#endif

    if (RUNNING_UNDER_GDM)
	    gdm_wm_screen_init (gdm_config_get_int (GDM_KEY_XINERAMA_SCREEN));

    gdm_version = g_getenv ("GDM_VERSION");

    /* Load the background as early as possible so GDM does not leave  */
    /* the background unfilled.   The cursor should be a watch already */
    /* but just in case */
    if (RUNNING_UNDER_GDM) {
	if (gdm_config_get_int (GDM_KEY_BACKGROUND_TYPE) != GDM_BACKGROUND_NONE)
		gdm_common_setup_background_color (gdm_config_get_string (GDM_KEY_BACKGROUND_COLOR));

	gdm_common_setup_cursor (GDK_WATCH);
    }

    if (RUNNING_UNDER_GDM &&
	gdm_version != NULL &&
	strcmp (gdm_version, VERSION) != 0) {
	    GtkWidget *dialog;
	    gchar *msg;

	    gdm_wm_init (0);

	    gdm_wm_focus_new_windows (TRUE);

	    msg = g_strdup_printf (_("The chooser version (%s) does not match the daemon "
	                             "version (%s).  "
	                             "You have probably just upgraded GDM.  "
	                             "Please restart the GDM daemon or the computer."),
	                           VERSION, gdm_version);

	    dialog = ve_hig_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_OK,
					_("Cannot run chooser"),
					msg);
	    g_free (msg);
	    
	    gtk_widget_show_all (dialog);
	    gdm_wm_center_window (GTK_WINDOW (dialog));

	    gdm_common_setup_cursor (GDK_LEFT_PTR);

	    gtk_dialog_run (GTK_DIALOG (dialog));

	    return EXIT_SUCCESS;
    }
    
    gtk_window_set_default_icon_from_file (DATADIR"/pixmaps/gdm-xnest.png", NULL);

    gdm_chooser_gui_init ();
    gdm_chooser_signals_init ();

    /* when no hosts on the command line, take them from the config */
    if (hosts_opt == NULL ||
	hosts_opt[0] == NULL) {
	    int i;
	    hosts_opt = g_strsplit (GdmHosts, ",", -1);
	    for (i = 0; hosts_opt != NULL && hosts_opt[i] != NULL; i++) {
		    g_strstrip (hosts_opt[i]);
	    }
    }
    gdm_chooser_xdmcp_init (hosts_opt);
    g_strfreev (hosts_opt);

    sid = g_signal_lookup ("event",
				 GTK_TYPE_WIDGET);
    g_signal_add_emission_hook (sid,
				0 /* detail */,
				gdm_event,
				NULL /* data */,
				NULL /* destroy_notify */);

    gtk_widget_queue_resize (chooser);
    gtk_widget_show_now (chooser);

    if (RUNNING_UNDER_GDM)
	    gdm_wm_center_window (GTK_WINDOW (chooser));

    if (RUNNING_UNDER_GDM &&
	/* can it ever happen that it'd be NULL here ??? */
	chooser->window != NULL) {
	    gdm_wm_init (GDK_WINDOW_XWINDOW (chooser->window));

	    /* Run the focus, note that this will work no matter what
	     * since gdm_wm_init will set the display to the gdk one
	     * if it fails */
	    gdm_wm_focus_window (GDK_WINDOW_XWINDOW (chooser->window));
    }

    if (gdm_config_get_bool (GDM_KEY_ALLOW_ADD))
	    gtk_widget_grab_focus (add_entry);

    gdm_chooser_add_entry_changed ();

    if (RUNNING_UNDER_GDM) {
	    gdm_wm_restore_wm_order ();
	    gdm_common_setup_cursor (GDK_LEFT_PTR);
    }

    gtk_main ();

    exit (EXIT_SUCCESS);
}

/* EOF */
