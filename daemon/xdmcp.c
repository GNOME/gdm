/* GDM - The GNOME Display Manager
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

/* This file contains the XDMCP implementation for managing remote
 * displays. 
 */

/* Theory of operation:
 *
 * Process idles waiting for UDP packets on port 177.
 * Incoming packets are decoded and checked against tcp_wrapper.
 *
 * A typical session looks like this:
 * 
 * Display sends Query/BroadcastQuery to Manager.
 *
 * Manager selects an appropriate authentication scheme from the
 * display's list of supported ones and sends Willing/Unwilling.
 * 
 * Assuming the display accepts the auth. scheme it sends back a
 * Request.
 *
 * If the manager accepts to service the display (i.e. loadavg is low)
 * it sends back an Accept containing a unique SessionID. The
 * SessionID is stored in an accept queue by the Manager. Should the
 * manager refuse to start a session a Decline is sent to the display.
 *
 * The display returns a Manage request containing the supplied
 * SessionID. The manager will then start a session on the display. In
 * case the SessionID is not on the accept queue the manager returns
 * Refuse. If the manager fails to open the display for connections
 * Failed is returned.
 *
 * During the session the display periodically sends KeepAlive packets
 * to the manager. The manager responds with Alive.
 *
 * Similarly the manager xpings the display once in a while and shuts
 * down the connection on failure.
 * 
 */

#include <config.h> 

#ifdef HAVE_LIBXDMCP
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xdmcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#ifdef HAVE_TCPWRAPPERS
  #include <tcpd.h>
#endif
#endif /* HAVE_LIBXDMCP */

#include <libgnome/libgnome.h>

#ifdef HAVE_LIBXDMCP
#include "gdm.h"
#include "display.h"
#include "auth.h"
#endif /* HAVE_LIBXDMCP */

#include "misc.h"
#include "choose.h"
#include "xdmcp.h"
#include "cookie.h"
#include "gdmconfig.h"

#define XDMCP_MULTICAST_ADDRESS "ff02::1"    /*This is to be changed when the Xdmcp Multicast address is decided */

int gdm_xdmcpfd = -1;

#ifdef HAVE_LIBXDMCP

/* TCP Wrapper syslog control */
gint allow_severity = LOG_INFO;
gint deny_severity = LOG_WARNING;

static guint xdmcp_source = 0;
static CARD32 globsessid;
static gchar *sysid;
static ARRAY8 servhost;
static XdmcpBuffer buf;
static gboolean initted = FALSE;

extern GSList *displays;
extern gint xdmcp_pending;
extern gint xdmcp_sessions;

/* Local prototypes */
static gboolean gdm_xdmcp_decode_packet (GIOChannel *source,
					 GIOCondition cond,
					 gpointer data);
/*************************** IPv6 specific prototypes *****************************/

#ifdef ENABLE_IPV6
static void gdm_xdmcp_handle_query (struct sockaddr_storage *clnt_sa,
                                  gint len,
                                  gint type);
static void gdm_xdmcp_handle_forward_query (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_handle_request (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_handle_manage (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_handle_managed_forward (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_handle_got_managed_forward (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_handle_keepalive (struct sockaddr_storage *clnt_sa, gint len);
static void gdm_xdmcp_send_willing (struct sockaddr_storage *clnt_sa);
static void gdm_xdmcp_send_unwilling (struct sockaddr_storage *clnt_sa, gint type);
static void gdm_xdmcp_send_accept (GdmHostent *he, struct sockaddr_storage *clnt_sa,
               gint displaynum);
static void gdm_xdmcp_send_decline (struct sockaddr_storage *clnt_sa, const char *reason);
static void gdm_xdmcp_send_refuse (struct sockaddr_storage *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_failed (struct sockaddr_storage *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_alive (struct sockaddr_storage *clnt_sa, CARD16 dspnum, CARD32 sessid);
static void gdm_xdmcp_send_managed_forward (struct sockaddr_storage *clnt_sa,
                                                          struct sockaddr_storage *origin);
static void gdm_xdmcp_send_forward_query6 (GdmIndirectDisplay *id,
                                                        struct sockaddr_in6 *clnt_sa,
                                                        struct in6_addr *display_addr,
                                                         ARRAYofARRAY8Ptr authlist);
static gboolean gdm_xdmcp_host_allow (struct sockaddr_storage *clnt_sa);
static GdmForwardQuery * gdm_forward_query_alloc (struct sockaddr_storage *mgr_sa,
                                                struct sockaddr_storage *dsp_sa);
static GdmForwardQuery * gdm_forward_query_lookup (struct sockaddr_storage *clnt_sa);
static void gdm_xdmcp_whack_queued_managed_forwards6 (struct sockaddr_in6 *clnt_sa,
                                                                   struct in6_addr *origin);
static void gdm_xdmcp_send_got_managed_forward6 (struct sockaddr_in6 *clnt_sa,
                                                              struct in6_addr *origin);
static int gdm_xdmcp_displays_from_host (struct sockaddr_storage *addr);
static GdmDisplay *gdm_xdmcp_display_lookup_by_host (struct sockaddr_storage *addr, int dspnum);
static GdmDisplay *gdm_xdmcp_display_alloc (struct sockaddr_storage *addr, GdmHostent *he, int displaynum);

#else

static void gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_query (struct sockaddr_in *clnt_sa,
                                  gint len,
                                  gint type);
 static void gdm_xdmcp_handle_request (struct sockaddr_in *clnt_sa, gint len);
 static void gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len);
 static void gdm_xdmcp_handle_managed_forward (struct sockaddr_in *clnt_sa, gint len);
 static void gdm_xdmcp_handle_got_managed_forward (struct sockaddr_in *clnt_sa, gint len);
 static void gdm_xdmcp_handle_keepalive (struct sockaddr_in *clnt_sa, gint len);
 static void gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa);
 static void gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type);
 static void gdm_xdmcp_send_accept (GdmHostent *he /* eaten and freed */,
                                   struct sockaddr_in *clnt_sa,
                                   int displaynum);
static void gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa, const char *reason);
 static void gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid);
 static void gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid);
 static void gdm_xdmcp_send_alive (struct sockaddr_in *clnt_sa, CARD16 dspnum, CARD32 sessid);
 static void gdm_xdmcp_send_managed_forward (struct sockaddr_in *clnt_sa,
                                            struct sockaddr_in *origin);
 static gboolean gdm_xdmcp_host_allow (struct sockaddr_in *clnt_sa);
static GdmForwardQuery * gdm_forward_query_alloc (struct sockaddr_in *mgr_sa,
                                                struct sockaddr_in *dsp_sa);
static GdmForwardQuery * gdm_forward_query_lookup (struct sockaddr_in *clnt_sa);
static int gdm_xdmcp_displays_from_host (struct sockaddr_in *addr);
static GdmDisplay *gdm_xdmcp_display_lookup_by_host (struct sockaddr_in *addr, int dspnum);
static GdmDisplay *gdm_xdmcp_display_alloc (struct sockaddr_in *addr, GdmHostent *he, int displaynum);

#endif  /*IPv4 / IPv6*/

static void gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_in *clnt_sa,
                                                   struct in_addr *origin);
static void gdm_xdmcp_send_got_managed_forward (struct sockaddr_in *clnt_sa,
                                               struct in_addr *origin);
static void gdm_xdmcp_send_forward_query (GdmIndirectDisplay *id,
                                        struct sockaddr_in *clnt_sa,
                                        struct in_addr *display_addr,
                                        ARRAYofARRAY8Ptr authlist);
static void gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_in *clnt_sa,
						     struct in_addr *origin);
static void gdm_xdmcp_send_got_managed_forward (struct sockaddr_in *clnt_sa,
						struct in_addr *origin);
static GdmDisplay *gdm_xdmcp_display_lookup (CARD32 sessid);
static void gdm_xdmcp_display_dispose_check (const gchar *hostname, int dspnum);
static void gdm_xdmcp_displays_check (void);
static void gdm_forward_query_dispose (GdmForwardQuery *q);

static GSList *forward_queries = NULL;

typedef struct {
	int times;
	guint handler;
#ifdef ENABLE_IPV6
	struct sockaddr_storage manager;
	struct sockaddr_storage origin;
#else
	struct sockaddr_in manager; 
	struct sockaddr_in origin;
#endif
} ManagedForward;
#define MANAGED_FORWARD_INTERVAL 1500 /* 1.5 seconds */

static GSList *managed_forwards = NULL;


/* 
 * We don't support XDM-AUTHENTICATION-1 and XDM-AUTHORIZATION-1.
 *
 * The latter would be quite useful to avoid sending unencrypted
 * cookies over the wire. Unfortunately it isn't supported without
 * XDM-AUTHENTICATION-1 which requires a key database with private
 * keys from all X terminals on your LAN. Fun, fun, fun.
 * 
 * Furthermore user passwords go over the wire in cleartext anyway so
 * protecting cookies is not that important. 
 */

typedef struct _XdmAuth {
    ARRAY8 authentication;
    ARRAY8 authorization;
} XdmAuthRec, *XdmAuthPtr;

static XdmAuthRec serv_authlist = { 
    { (CARD16) 0, (CARD8 *) 0 },
    { (CARD16) 0, (CARD8 *) 0 }
};

#ifdef ENABLE_IPV6
static gboolean have_ipv6 (void)
{
	int s;

	s = socket (AF_INET6, SOCK_STREAM, 0);
	if (s != -1) {
		VE_IGNORE_EINTR (close (s));
		return TRUE;
	}

	return FALSE;
}
#endif

static int
#ifdef ENABLE_IPV6
gdm_xdmcp_displays_from_host (struct sockaddr_storage *addr)
#else
gdm_xdmcp_displays_from_host (struct sockaddr_in *addr)
#endif
{
	GSList *li;
	int count = 0;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (SERVER_IS_XDMCP (disp)) {
#ifdef ENABLE_IPV6
		    if (disp->addrtype == AF_INET6 &&
			memcmp (&disp->addr6, &((struct sockaddr_in6 *)addr)->sin6_addr, sizeof (struct in6_addr)) == 0)
			count ++;
		    else
#endif
		    if (disp->addrtype == AF_INET &&
		    memcmp (&disp->addr, &((struct sockaddr_in *)addr)->sin_addr, sizeof (struct in_addr)) == 0)
			count ++;
		}
	}

	return count;
}

static GdmDisplay *
#ifdef ENABLE_IPV6
gdm_xdmcp_display_lookup_by_host (struct sockaddr_storage *addr, int dspnum)
#else
gdm_xdmcp_display_lookup_by_host (struct sockaddr_in *addr, int dspnum)
#endif
{
	GSList *li;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (SERVER_IS_XDMCP (disp)) {
#ifdef ENABLE_IPV6
		    if (disp->addrtype == AF_INET6) {
			if ((memcmp (&disp->addr6, &((struct sockaddr_in6 *)addr)->sin6_addr, sizeof (struct in6_addr)) == 0) && disp->xdmcp_dispnum == dspnum)
			return disp;
		    }
		    else
#endif
		    if ((memcmp (&disp->addr, &((struct sockaddr_in *)addr)->sin_addr, sizeof (struct in_addr)) == 0) && disp->xdmcp_dispnum == dspnum)
			return disp;
		}
	}

	return NULL;
}


gboolean
gdm_xdmcp_init (void)
{
#ifdef ENABLE_IPV6
    struct sockaddr_storage serv_sa = {0};
#else
    struct sockaddr_in serv_sa = {0};
#endif
    gint addrlen;
    gchar hostbuf[1024];
    struct utsname name;
    int udpport = gdm_get_value_int (GDM_KEY_UDP_PORT);

    if ( ! gdm_get_value_bool (GDM_KEY_XDMCP))
	    return TRUE;

    globsessid = g_random_int ();
    
    /* Fetch and store local hostname in XDMCP friendly format */
    hostbuf[1023] = '\0';
    if G_UNLIKELY (gethostname (hostbuf, 1023) != 0) {
	gdm_error (_("%s: Could not get server hostname: %s!"), 
		   "gdm_xdmcp_init", strerror (errno));
	strcmp (hostbuf, "localhost.localdomain");
    }

    if ( ! initted) {
	    uname (&name);
	    sysid = g_strconcat (name.sysname, " ", name.release, NULL);

	    servhost.data = (CARD8 *) g_strdup (hostbuf);
	    servhost.length = strlen ((char *) servhost.data);
	    
	    initted = TRUE;
    }
    
    gdm_debug ("XDMCP: Start up on host %s, port %d", hostbuf, udpport);
    
    /* Open socket for communications */
#ifdef ENABLE_IPV6
    gdm_xdmcpfd = socket (AF_INET6, SOCK_DGRAM, 0); /* UDP */
    if (gdm_xdmcpfd < 0)
#endif
	gdm_xdmcpfd = socket (AF_INET, SOCK_DGRAM, 0); /* UDP */
    
    if G_UNLIKELY (gdm_xdmcpfd < 0) {
	gdm_error (_("%s: Could not create socket!"), "gdm_xdmcp_init");
        gdm_set_value_bool (GDM_KEY_XDMCP, FALSE);
	return FALSE;
    }

#ifdef ENABLE_IPV6
    if (have_ipv6 ()) {
	((struct sockaddr_in6 *)(&serv_sa))->sin6_family = AF_INET6;
	((struct sockaddr_in6 *)(&serv_sa))->sin6_port = htons (udpport); /* UDP 177 */
	((struct sockaddr_in6 *)(&serv_sa))->sin6_addr = in6addr_any;
	addrlen = sizeof (struct sockaddr_in6);

	/* Checking and Setting Multicast options */
	if (gdm_get_value_bool (GDM_KEY_MULTICAST)) {
	    int socktemp;       /* temporary socket for getting info about available interfaces*/
	    int i, num;
	    char *buf;
	    struct ipv6_mreq mreq;

	    /* For interfaces' list */
	    struct ifconf ifc;
	    struct ifreq *ifr;

	    /* Extract Multicast address for IPv6 */
	    if (ve_string_empty (gdm_get_value_string (GDM_KEY_MULTICAST_ADDR))) {

		/* Stuff it with all-node multicast address */
		gdm_set_value_string (GDM_KEY_MULTICAST_ADDR, XDMCP_MULTICAST_ADDRESS);
	    }

	    socktemp = socket (AF_INET, SOCK_DGRAM, 0);
#ifdef SIOCGIFNUM
	    if (ioctl (socktemp, SIOCGIFNUM, &num) < 0) {
		num = 64;
	    }
#else
	    num = 64;
#endif
	    ifc.ifc_len = sizeof (struct ifreq) * num;
	    ifc.ifc_buf = buf = malloc (ifc.ifc_len);
	    if (ioctl (socktemp, SIOCGIFCONF, &ifc) >= 0) {
		ifr = ifc.ifc_req;
		num = ifc.ifc_len / sizeof (struct ifreq); /* No of interfaces */

		/* Joining multicast group with all interfaces*/
		for (i = 0 ; i < num ; i++) {
		    struct ifreq ifreq;
		    int ifindex;

		    memset (&ifreq, 0, sizeof (ifreq));
		    strncpy (ifreq.ifr_name, ifr[i].ifr_name, sizeof (ifreq.ifr_name));
		    /* paranoia */
		    ifreq.ifr_name[sizeof (ifreq.ifr_name) - 1] = '\0';

		    if (ioctl (socktemp, SIOCGIFFLAGS, &ifreq) < 0)
			gdm_debug ("XDMCP: Could not get SIOCGIFFLAGS for %s", ifr[i].ifr_name);

		    ifindex = if_nametoindex (ifr[i].ifr_name);

		    if ((!(ifreq.ifr_flags & IFF_UP) ||
                        (ifreq.ifr_flags & IFF_LOOPBACK)) ||
                       ((ifindex == 0 ) && (errno == ENXIO))) {
                               /* Not a valid interface or loopback interface*/
			continue;
			}

		    mreq.ipv6mr_interface = ifindex;
		    inet_pton (AF_INET6, gdm_get_value_string (GDM_KEY_MULTICAST_ADDR), &mreq.ipv6mr_multiaddr);
		    setsockopt (gdm_xdmcpfd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof (mreq));
		} 
           }
           g_free (buf);
           close (socktemp);
	}
    }
    else
#endif
    {
	((struct sockaddr_in *)(&serv_sa))->sin_family = AF_INET;
	((struct sockaddr_in *)(&serv_sa))->sin_port = htons (udpport); /* UDP 177 */
	((struct sockaddr_in *)(&serv_sa))->sin_addr.s_addr = htonl (INADDR_ANY);
	addrlen = sizeof (struct sockaddr_in);
    }
   
    if G_UNLIKELY (bind (gdm_xdmcpfd, (struct sockaddr*) &serv_sa, addrlen) == -1) {
	gdm_error (_("%s: Could not bind to XDMCP socket!"), "gdm_xdmcp_init");
	gdm_xdmcp_close ();
	gdm_set_value_bool (GDM_KEY_XDMCP, FALSE);
	return FALSE;
    }

    return TRUE;
}


void
gdm_xdmcp_run (void)
{
	GIOChannel *xdmcpchan;

	xdmcpchan = g_io_channel_unix_new (gdm_xdmcpfd);
	g_io_channel_set_encoding (xdmcpchan, NULL, NULL);
	g_io_channel_set_buffered (xdmcpchan, FALSE);

	xdmcp_source = g_io_add_watch_full
		(xdmcpchan, G_PRIORITY_DEFAULT,
		 G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		 gdm_xdmcp_decode_packet, NULL, NULL);
	g_io_channel_unref (xdmcpchan);
}


void
gdm_xdmcp_close (void)
{
	if (xdmcp_source > 0) {
		g_source_remove (xdmcp_source);
		xdmcp_source = 0;
	}

	if (gdm_xdmcpfd > 0) {
		VE_IGNORE_EINTR (close (gdm_xdmcpfd));
		gdm_xdmcpfd = -1;
	}
}


static gboolean
gdm_xdmcp_decode_packet (GIOChannel *source, GIOCondition cond, gpointer data)
{
#ifdef ENABLE_IPV6
    struct sockaddr_storage clnt_sa;
#else
    struct sockaddr_in clnt_sa;
#endif
    gint sa_len = sizeof (clnt_sa);
    XdmcpHeader header;
    static const char * const opcode_names[] = {
	NULL,
	"BROADCAST_QUERY", "QUERY", "INDIRECT_QUERY", "FORWARD_QUERY",
	"WILLING", "UNWILLING", "REQUEST", "ACCEPT", "DECLINE", "MANAGE", "REFUSE",
	"FAILED", "KEEPALIVE", "ALIVE"
    };
    static const char * const gdm_opcode_names[] = {
	"MANAGED_FORWARD", "GOT_MANAGED_FORWARD"
    };

    /* packets come at somewhat random times */
    gdm_random_tick ();

    if (cond != G_IO_IN)
	    gdm_debug ("gdm_xdmcp_decode_packet: GIOCondition %d", (int)cond);

    if ( ! (cond & G_IO_IN)) 
        return TRUE;

    if G_UNLIKELY (!XdmcpFill (gdm_xdmcpfd, &buf, (XdmcpNetaddr)&clnt_sa, &sa_len)) {
	gdm_debug (_("%s: Could not create XDMCP buffer!"),
		   "gdm_xdmcp_decode_packet");
	return TRUE;
    }
    
    if G_UNLIKELY (!XdmcpReadHeader (&buf, &header)) {
	gdm_error (_("%s: Could not read XDMCP header!"),
		   "gdm_xdmcp_decode_packet");
	return TRUE;
    }
    
    if G_UNLIKELY (header.version != XDM_PROTOCOL_VERSION &&
		   header.version != GDM_XDMCP_PROTOCOL_VERSION) {
	gdm_error (_("%s: Incorrect XDMCP version!"),
		   "gdm_xdmcp_decode_packet");
	return TRUE;
    }

    if (header.opcode <= ALIVE) {
#ifdef ENABLE_IPV6
	if (clnt_sa.ss_family == AF_INET6) {
	    char buffer6[INET6_ADDRSTRLEN];

	    gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", opcode_names[header.opcode], inet_ntop (AF_INET6, &((struct sockaddr_in6 *)(&clnt_sa))->sin6_addr, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
	    gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", opcode_names[header.opcode], inet_ntoa (((struct sockaddr_in *)(&clnt_sa))->sin_addr));
	}
    }

    if (header.opcode >= GDM_XDMCP_FIRST_OPCODE &&
        header.opcode < GDM_XDMCP_LAST_OPCODE) {
#ifdef ENABLE_IPV6
	if (clnt_sa.ss_family == AF_INET6) {

		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", gdm_opcode_names[header.opcode - GDM_XDMCP_FIRST_OPCODE], inet_ntop (AF_INET6, &((struct sockaddr_in6 *)(&clnt_sa))->sin6_addr, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
		char buffer[INET_ADDRSTRLEN];

		gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", gdm_opcode_names[header.opcode - GDM_XDMCP_FIRST_OPCODE], inet_ntop (AF_INET, &((struct sockaddr_in *)(&clnt_sa))->sin_addr, buffer, INET_ADDRSTRLEN));
	}
    }

    switch (header.opcode) {
	
    case BROADCAST_QUERY:
	gdm_xdmcp_handle_query (&clnt_sa, header.length, BROADCAST_QUERY);
	break;
	
    case QUERY:
	gdm_xdmcp_handle_query (&clnt_sa, header.length, QUERY);
	break;
	
    case INDIRECT_QUERY:
	gdm_xdmcp_handle_query (&clnt_sa, header.length, INDIRECT_QUERY);
	break;
	
    case FORWARD_QUERY:
	gdm_xdmcp_handle_forward_query (&clnt_sa, header.length);
	break;
	
    case REQUEST:
	gdm_xdmcp_handle_request (&clnt_sa, header.length);
	break;
	
    case MANAGE:
	gdm_xdmcp_handle_manage (&clnt_sa, header.length);
	break;
	
    case KEEPALIVE:
	gdm_xdmcp_handle_keepalive (&clnt_sa, header.length);
	break;

    case GDM_XDMCP_MANAGED_FORWARD:
	gdm_xdmcp_handle_managed_forward (&clnt_sa, header.length);
	break;

    case GDM_XDMCP_GOT_MANAGED_FORWARD:
	gdm_xdmcp_handle_got_managed_forward (&clnt_sa, header.length);
	break;
	
    default:
#ifdef ENABLE_IPV6
	if (clnt_sa.ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_error (_("%s: Unknown opcode from host %s"),
                          "gdm_xdmcp_decode_packet",
                          inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)(&clnt_sa))->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
		gdm_error (_("%s: Unknown opcode from host %s"),
                          "gdm_xdmcp_decode_packet",
                          inet_ntoa (((struct sockaddr_in *)(&clnt_sa))->sin_addr));
	}
	break;
    }

    return TRUE;
}

static void 
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_query (struct sockaddr_storage *clnt_sa, gint len, gint type)
#else
gdm_xdmcp_handle_query (struct sockaddr_in *clnt_sa, gint len, gint type)
#endif
{
    ARRAYofARRAY8 clnt_authlist;
    gint i = 0, explen = 1;

#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_xdmcp_handle_query: Opcode %d from %s", type, inet_ntop (AF_INET6, &((struct sockaddr_in6 *)(clnt_sa))->sin6_addr, buffer6, INET6_ADDRSTRLEN));
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_handle_query: Opcode %d from %s", type, inet_ntoa (((struct sockaddr_in *)(clnt_sa))->sin_addr));
    }
 
    /* Extract array of authentication names from Xdmcp packet */
    if G_UNLIKELY (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
	gdm_error (_("%s: Could not extract authlist from packet"),
		   "gdm_xdmcp_handle_query"); 
	return;
    }
    
    /* Crude checksumming */
    for (i = 0 ; i < clnt_authlist.length ; i++) {
	    if G_UNLIKELY (gdm_get_value_bool (GDM_KEY_DEBUG)) {
		    char *s = g_strndup ((char *) clnt_authlist.data[i].data, clnt_authlist.length);
		    gdm_debug ("gdm_xdmcp_handle_query: authlist: %s", ve_sure_string (s));
		    g_free (s);
	    }
	    explen += 2+clnt_authlist.data[i].length;
    }
    
    if G_UNLIKELY (len != explen) {
	gdm_error (_("%s: Error in checksum"),
		   "gdm_xdmcp_handle_query"); 
	XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
	return;
    }

    /* Check with tcp_wrappers if client is allowed to access */
    if (gdm_xdmcp_host_allow (clnt_sa)) {

	/* If this is an INDIRECT_QUERY, try to look up the display in
 	 * the pending list. If found send a FORWARD_QUERY to the
 	 * chosen manager. Otherwise alloc a new indirect display. */

	if (gdm_get_value_bool (GDM_KEY_INDIRECT) &&
	    type == INDIRECT_QUERY) {
		GdmIndirectDisplay *id = gdm_choose_indirect_lookup (clnt_sa);

		if (id != NULL && (
#ifdef ENABLE_IPV6
		    (id->chosen_host6 != NULL) ||
#endif
		    (id->chosen_host != NULL)))
		{
			/* if user chose us, then just send willing */
			if ((((struct sockaddr_in *)clnt_sa)->sin_family == AF_INET && gdm_is_local_addr ((struct in_addr *)(id->chosen_host)))
#ifdef ENABLE_IPV6
			    || (clnt_sa->ss_family == AF_INET6 && gdm_is_local_addr6 ((struct in6_addr *)(id->chosen_host6)))
#endif
                          ) {

				/* get rid of indirect, so that we don't get
				 * the chooser */
				gdm_choose_indirect_dispose (id);
				gdm_xdmcp_send_willing (clnt_sa);
			}
			else
			if ((((struct sockaddr_in *)clnt_sa)->sin_family == AF_INET && gdm_is_loopback_addr (&(((struct sockaddr_in *)clnt_sa)->sin_addr)))
#ifdef ENABLE_IPV6
			    || (clnt_sa->ss_family == AF_INET6 && gdm_is_loopback_addr6 (&(((struct sockaddr_in6 *)clnt_sa)->sin6_addr)))
#endif
			) {

				/* woohoo! fun, I have no clue how to get
				 * the correct ip, SO I just send forward
				 * queries with all the different IPs */
				const GList *list = gdm_peek_local_address_list ();

				while (list != NULL) {
					struct sockaddr *saddr = (struct sockaddr *)(list->data);
#ifdef ENABLE_IPV6
					struct in6_addr *addr6;
#endif
					struct in_addr *addr;

#ifdef ENABLE_IPV6
					if (saddr->sa_family == AF_INET6) {
						addr6 = &(((struct sockaddr_in6 *)saddr)->sin6_addr);
						if ( ! gdm_is_loopback_addr6 (addr6)) {
							/* forward query to * chosen host */
							gdm_xdmcp_send_forward_query6 (id, (struct sockaddr_in6 *)clnt_sa, addr6, &clnt_authlist);
						}
					}
					else
#endif
					{
						addr = &(((struct sockaddr_in *)saddr)->sin_addr);
						if ( ! gdm_is_loopback_addr (addr)) {
							/* forward query to * chosen host */
							gdm_xdmcp_send_forward_query (id, (struct sockaddr_in *)clnt_sa, addr, &clnt_authlist);
						}

					}

					list = list->next;
				}
			} else {
				/* or send forward query to chosen host */
				gdm_xdmcp_send_forward_query
					(id, (struct sockaddr_in *)clnt_sa,
					 &(((struct sockaddr_in *)clnt_sa)->sin_addr),
					 &clnt_authlist);
			}
		} else if (id == NULL) {
			id = gdm_choose_indirect_alloc (clnt_sa);
			if (id != NULL) {
				gdm_xdmcp_send_willing (clnt_sa);
			}
		} else  {
			gdm_xdmcp_send_willing (clnt_sa);
		}
	} else {
		gdm_xdmcp_send_willing (clnt_sa);
	}
    } else if (type == QUERY) {
	    /* unwilling is ONLY sent for direct queries, never for broadcast
	     * nor indirects */
	    gdm_xdmcp_send_unwilling (clnt_sa, type);
    }

    /* Dispose authlist from remote display */
    XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
}

#ifdef ENABLE_IPV6
static void
gdm_xdmcp_send_forward_query6 (GdmIndirectDisplay *id, struct sockaddr_in6 *clnt_sa, struct in6_addr *display_addr, ARRAYofARRAY8Ptr authlist)
{
	struct sockaddr_in6 sock = {0};
	XdmcpHeader header;
	int i, authlen;
	ARRAY8 address;
	ARRAY8 port;
	char buffer6[INET6_ADDRSTRLEN];

	g_assert (id != NULL);
	g_assert (id->chosen_host != NULL);

	gdm_debug ("gdm_xdmcp_send_forward_query6: Sending forward query to %s", inet_ntop (AF_INET6, &(id->chosen_host6), buffer6, sizeof (buffer6)));
	gdm_debug ("gdm_xdmcp_send_forward_query6: Query contains %s:%d", inet_ntop (AF_INET6, display_addr, buffer6, sizeof (buffer6)), (int) ntohs (clnt_sa->sin6_port));

	authlen = 1;
	for (i = 0 ; i < authlist->length ; i++) {
               authlen += 2 + authlist->data[i].length;
	}

	/* we depend on this being 2 elsewhere as well */
	port.length = 2;
	port.data = g_new (char, 2);
	memcpy (port.data, &(clnt_sa->sin6_port), 2);
	address.length = sizeof (struct in6_addr);
	address.data = (void *)g_new (struct in6_addr, 1);
	memcpy (address.data, display_addr, sizeof (struct in6_addr));


	header.opcode = (CARD16) FORWARD_QUERY;
	header.length = authlen;
	header.length += 2 + address.length;
	header.length += 2 + port.length;
	header.version = XDM_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpWriteARRAY8 (&buf, &port);
	XdmcpWriteARRAYofARRAY8 (&buf, authlist);

	memset (&sock, 0, sizeof (sock));

	sock.sin6_family = AF_INET6;
	sock.sin6_port = htons (XDM_UDP_PORT);
	memcpy (sock.sin6_addr.s6_addr, id->chosen_host6->s6_addr, sizeof (struct in6_addr));
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr) &sock, (int)sizeof (struct sockaddr_in6));
	g_free (port.data);
	g_free (address.data);
}
#endif

static void
gdm_xdmcp_send_forward_query (GdmIndirectDisplay *id,
			      struct sockaddr_in *clnt_sa,
			      struct in_addr *display_addr,
			      ARRAYofARRAY8Ptr authlist)
{
	struct sockaddr_in sock = {0};
	XdmcpHeader header;
	int i, authlen;
	ARRAY8 address;
	ARRAY8 port;

	gdm_assert (id != NULL);
	gdm_assert (id->chosen_host != NULL);

	gdm_debug ("gdm_xdmcp_send_forward_query: Sending forward query to %s",
		   inet_ntoa (*id->chosen_host));
	gdm_debug ("gdm_xdmcp_send_forward_query: Query contains %s:%d", 
		   inet_ntoa (*display_addr),
		   (int) ntohs (clnt_sa->sin_port));

	authlen = 1;
	for (i = 0 ; i < authlist->length ; i++) {
		authlen += 2 + authlist->data[i].length;
	}

	/* we depend on this being 2 elsewhere as well */
	port.length = 2;
	port.data = (CARD8 *) g_new (char, 2);
	memcpy (port.data, &(clnt_sa->sin_port), 2);

	address.length = sizeof (struct in_addr);
	address.data = (void *)g_new (struct in_addr, 1);
	memcpy (address.data, display_addr, sizeof (struct in_addr));

	header.opcode = (CARD16) FORWARD_QUERY;
	header.length = authlen;
	header.length += 2 + address.length;
	header.length += 2 + port.length;
	header.version = XDM_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpWriteARRAY8 (&buf, &port);
	XdmcpWriteARRAYofARRAY8 (&buf, authlist);

	sock.sin_family = AF_INET;
	sock.sin_port = htons (XDM_UDP_PORT);
	sock.sin_addr.s_addr = id->chosen_host->s_addr;
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr) &sock,
		    (int)sizeof (struct sockaddr_in));

	g_free (port.data);
	g_free (address.data);
}

static gboolean
remove_oldest_forward (void)
{
	GSList *li;
	GdmForwardQuery *oldest = NULL;

	for (li = forward_queries; li != NULL; li = li->next) {
		GdmForwardQuery *query = li->data;

		if (oldest == NULL ||
		    query->acctime < oldest->acctime) {
			oldest = query;
		}
	}

	if (oldest != NULL) {
		gdm_forward_query_dispose (oldest);
		return TRUE;
	} else {
		return FALSE;
	}
}

static GdmForwardQuery *
#ifdef ENABLE_IPV6
gdm_forward_query_alloc (struct sockaddr_storage *mgr_sa,
			 struct sockaddr_storage *dsp_sa)
#else
gdm_forward_query_alloc (struct sockaddr_in *mgr_sa,
			 struct sockaddr_in *dsp_sa)
#endif
{
	GdmForwardQuery *q;
	int count;

	count = g_slist_length (forward_queries);

	while (count > GDM_MAX_FORWARD_QUERIES &&
	       remove_oldest_forward ())
		count --;

	q = g_new0 (GdmForwardQuery, 1);
#ifdef ENABLE_IPV6
	q->dsp_sa = g_new0 (struct sockaddr_storage, 1);
	memcpy (q->dsp_sa, dsp_sa, sizeof (struct sockaddr_storage));
	q->from_sa = g_new0 (struct sockaddr_storage, 1);
	memcpy (q->from_sa, mgr_sa, sizeof (struct sockaddr_storage));
#else
	q->dsp_sa = g_new0 (struct sockaddr_in, 1);
	memcpy (q->dsp_sa, dsp_sa, sizeof (struct sockaddr_in));
	q->from_sa = g_new0 (struct sockaddr_in, 1);
	memcpy (q->from_sa, mgr_sa, sizeof (struct sockaddr_in));
#endif

	forward_queries = g_slist_prepend (forward_queries, q);

	return q;
}

static GdmForwardQuery *
#ifdef ENABLE_IPV6
gdm_forward_query_lookup (struct sockaddr_storage *clnt_sa)
#else
gdm_forward_query_lookup (struct sockaddr_in *clnt_sa)
#endif
{
	GSList *li, *qlist;
	GdmForwardQuery *q;
	time_t curtime = time (NULL);

	qlist = g_slist_copy (forward_queries);

	for (li = qlist; li != NULL; li = li->next) {
		q = (GdmForwardQuery *) li->data;
		if (q == NULL)
			continue;
#ifdef ENABLE_IPV6
		if (clnt_sa->ss_family == AF_INET6) {
			if (memcmp (((struct sockaddr_in6 *)(q->dsp_sa))->sin6_addr.s6_addr, ((struct sockaddr_in6 *)clnt_sa)->sin6_addr.s6_addr, sizeof (struct in6_addr)) == 0) {
				g_slist_free (qlist);
				return q;
			}

			if (q->acctime > 0 &&  curtime > q->acctime + GDM_FORWARD_QUERY_TIMEOUT) {
				char buffer6[INET6_ADDRSTRLEN];

				gdm_debug ("gdm_forward_query_lookup: Disposing stale forward query from %s", inet_ntop (AF_INET6, &((struct sockaddr_in6 *)q->dsp_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
				gdm_forward_query_dispose (q);
				continue;
			}
		}
		else
#endif
		{
			if (((struct sockaddr_in *)(q->dsp_sa))->sin_addr.s_addr == ((struct sockaddr_in *)clnt_sa)->sin_addr.s_addr) {
				g_slist_free (qlist);
				return q;
			}

			if (q->acctime > 0 &&
                           curtime > q->acctime + GDM_FORWARD_QUERY_TIMEOUT)   {
				gdm_debug ("gdm_forward_query_lookup: Disposing stale forward query from %s",
                                          inet_ntoa (((struct sockaddr_in*)q->dsp_sa)->sin_addr));
				gdm_forward_query_dispose (q);
				continue;
			}
		}

	}
	g_slist_free (qlist);

#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_forward_query_lookup: Host %s not found", inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
		gdm_debug ("gdm_forward_query_lookup: Host %s not found",
                          inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
	}

	return NULL;
}


static void
gdm_forward_query_dispose (GdmForwardQuery *q)
{
	if (q == NULL)
		return;

	forward_queries = g_slist_remove (forward_queries, q);

	q->acctime = 0;

#ifdef ENABLE_IPV6
	if (q->dsp_sa->ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_forward_query_dispose: Disposing %s", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)(q->dsp_sa))->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
               gdm_debug ("gdm_forward_query_dispose: Disposing %s",
                          inet_ntoa (((struct sockaddr_in *)(q->dsp_sa))->sin_addr));
	}

	g_free (q->dsp_sa);
	q->dsp_sa = NULL;
	g_free (q->from_sa);
	q->from_sa = NULL;

	g_free (q);
}


static void 
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_forward_query (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len)
#endif
{
    ARRAY8 clnt_addr;
    ARRAY8 clnt_port;
    ARRAYofARRAY8 clnt_authlist;
    gint i = 0, explen = 1;
#ifdef ENABLE_IPV6
    struct sockaddr_storage disp_sa = {0};
#else
    struct sockaddr_in disp_sa = {0};
#endif

    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
#ifdef ENABLE_IPV6
	    if (clnt_sa->ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_error ("%s: Got FORWARD_QUERY from banned host %s",
			   "gdm_xdmcp_handle_forward query",
			   inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	    }
	    else
#endif
	    {
		gdm_error ("%s: Got FORWARD_QUERY from banned host %s",
			   "gdm_xdmcp_handle_forward query",
			   inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
	    }

	    return;
    }
    
    /* Read display address */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_addr)) {
	gdm_error (_("%s: Could not read display address"),
		   "gdm_xdmcp_handle_forward_query");
	return;
    }
    
    /* Read display port */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_port)) {
	XdmcpDisposeARRAY8 (&clnt_addr);
	gdm_error (_("%s: Could not read display port number"),
		   "gdm_xdmcp_handle_forward_query");
	return;
    }
    
    /* Extract array of authentication names from Xdmcp packet */
    if G_UNLIKELY (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
	XdmcpDisposeARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY8 (&clnt_port);
	gdm_error (_("%s: Could not extract authlist from packet"),
		   "gdm_xdmcp_handle_forward_query"); 
	return;
    }
    
    /* Crude checksumming */
    explen = 1;
    explen += 2+clnt_addr.length;
    explen += 2+clnt_port.length;
    
    for (i = 0 ; i < clnt_authlist.length ; i++) {
	    if G_UNLIKELY (gdm_get_value_bool (GDM_KEY_DEBUG)) {
		    char *s = g_strndup ((char *) clnt_authlist.data[i].data, clnt_authlist.length);
		    gdm_debug ("gdm_xdmcp_handle_forward_query: authlist: %s", ve_sure_string (s));
		    g_free (s);
	    }
	    explen += 2+clnt_authlist.data[i].length;
    }
    
    if G_UNLIKELY (len != explen) {
	gdm_error (_("%s: Error in checksum"),
		   "gdm_xdmcp_handle_forward_query"); 
	goto out;
    }

#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	struct in6_addr ipv6_addr;
	struct in6_addr * ipv6_addr_ptr = NULL;

	if (clnt_port.length == 2 &&
	       clnt_addr.length == 4) {

		/* Convert IPv4 address to IPv6 if needed */
		struct sockaddr_in tmp_disp_sa = {0};
		((struct sockaddr_in *)(&tmp_disp_sa))->sin_family = AF_INET;
		memcpy (&((struct sockaddr_in *)(&tmp_disp_sa))->sin_port, clnt_port.data, 2);
		memcpy (&((struct sockaddr_in *)(&tmp_disp_sa))->sin_addr.s_addr, clnt_addr.data, 4);

		char * ipv4_addr = inet_ntoa (((struct sockaddr_in *)(&tmp_disp_sa))->sin_addr);
		strcpy (buffer6, "::ffff:");
		strncat (buffer6, ipv4_addr, INET_ADDRSTRLEN);

		inet_pton (AF_INET6, buffer6, &ipv6_addr);
		ipv6_addr_ptr = &ipv6_addr;

	} else if (clnt_port.length == 2 &&
	       clnt_addr.length == 16) {

		ipv6_addr_ptr = (struct in6_addr *)clnt_addr.data;

	} else {

		gdm_error (_("%s: Bad address"),
                          "gdm_xdmcp_handle_forward_query");
		goto out;
	}

	g_assert (16 == sizeof (struct in6_addr));

	gdm_xdmcp_whack_queued_managed_forwards6 ((struct sockaddr_in6 *)clnt_sa, ipv6_addr_ptr);

	((struct sockaddr_in6 *)(&disp_sa))->sin6_family = AF_INET6;

	/* Find client port number */
	memcpy (&((struct sockaddr_in6 *)(&disp_sa))->sin6_port, clnt_port.data, 2);

	/* Find client address */
	memcpy (&((struct sockaddr_in6 *)(&disp_sa))->sin6_addr.s6_addr, ipv6_addr_ptr, 16);

	gdm_debug ("gdm_xdmcp_handle_forward_query: Got FORWARD_QUERY for display: %s, port %d", inet_ntop (AF_INET6, &((struct sockaddr_in6 *)(&disp_sa))->sin6_addr, buffer6, INET6_ADDRSTRLEN), ntohs (((struct sockaddr_in6 *)(&disp_sa))->sin6_port));
   }
   else
#endif
   {
	if (clnt_port.length != 2 ||
	    clnt_addr.length != 4) {
		gdm_error (_("%s: Bad address"),
                          "gdm_xdmcp_handle_forward_query");
		goto out;
	}

	g_assert (4 == sizeof (struct in_addr));
	gdm_xdmcp_whack_queued_managed_forwards ((struct sockaddr_in *)clnt_sa, (struct in_addr *)clnt_addr.data);
	((struct sockaddr_in *)(&disp_sa))->sin_family = AF_INET;
	/* Find client port number */
	memcpy (&((struct sockaddr_in *)(&disp_sa))->sin_port, clnt_port.data, 2);
	/* Find client address */
	memcpy (&((struct sockaddr_in *)(&disp_sa))->sin_addr.s_addr, clnt_addr.data, 4);
	gdm_debug ("gdm_xdmcp_handle_forward_query: Got FORWARD_QUERY for display: %s, port %d", inet_ntoa (((struct sockaddr_in *)(&disp_sa))->sin_addr), ntohs (((struct sockaddr_in *)(&disp_sa))->sin_port));
    }


    
    
    
    /* Check with tcp_wrappers if display is allowed to access */
    if (gdm_xdmcp_host_allow (&disp_sa)) {
	    GdmForwardQuery *q;

	    q = gdm_forward_query_lookup (&disp_sa);
	    if (q != NULL)
		    gdm_forward_query_dispose (q);
	    gdm_forward_query_alloc (clnt_sa, &disp_sa);

	    gdm_xdmcp_send_willing (&disp_sa);
    }

  out:
    /* Cleanup */
    XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
    XdmcpDisposeARRAY8 (&clnt_port);
    XdmcpDisposeARRAY8 (&clnt_addr);
}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_willing (struct sockaddr_storage *clnt_sa)
#else
gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa)
#endif
{
    ARRAY8 status;
    XdmcpHeader header;
    static char *last_status = NULL;
    static time_t last_willing = 0;
    char *bin;
    FILE *fd;
    char *willing = gdm_get_value_string (GDM_KEY_WILLING);
    int dispperhost = gdm_get_value_int (GDM_KEY_DISPLAYS_PER_HOST);
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_xdmcp_send_willing: Sending WILLING to %s", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), buffer6, INET6_ADDRSTRLEN));
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_send_willing: Sending WILLING to %s", inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
    }

    if (last_willing == 0 ||
	time (NULL) - 3 > last_willing) {
	    char statusBuf[256] = "";
	    bin = ve_first_word (willing);
	    if ( ! ve_string_empty (bin) &&
		 access (bin, X_OK) == 0 &&
		 (fd = popen (willing, "r")) != NULL) {
		    if (fgets (statusBuf, sizeof (statusBuf), fd) != NULL &&
			! ve_string_empty (g_strstrip (statusBuf))) {
			    g_free (last_status);
			    last_status = g_strdup (statusBuf);
		    } else {
			    g_free (last_status);
			    last_status = g_strdup (sysid);
		    }
		    pclose (fd);
	    } else {
		    g_free (last_status);
		    last_status = g_strdup (sysid);
	    }
	    last_willing = time (NULL);
	    g_free (bin);
    }
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	if ( ! gdm_is_local_addr6 (&(((struct sockaddr_in6 *)clnt_sa)->sin6_addr)) &&
	    gdm_xdmcp_displays_from_host (clnt_sa) >= dispperhost) {
		/* Don't translate, this goes over the wire to servers where we
		 * don't know the charset or language, so it must be ascii */
		status.data = (CARD8 *) g_strdup_printf ("%s (Server is busy)", last_status);
	} else {
		status.data = (CARD8 *) g_strdup (last_status);
	}
     }
    else
#endif
    {
	if ( ! gdm_is_local_addr (&(((struct sockaddr_in *)clnt_sa)->sin_addr)) &&
	    gdm_xdmcp_displays_from_host (clnt_sa) >= dispperhost) {
		/* Don't translate, this goes over the wire to servers where we
		 * don't know the charset or language, so it must be ascii */
		status.data = (CARD8 *) g_strdup_printf ("%s (Server is busy)", last_status);
	} else {
		status.data = (CARD8 *) g_strdup (last_status);
	}
    }

    status.length = strlen ((char *) status.data);
    
    header.opcode = (CARD16) WILLING;
    header.length = 6 + serv_authlist.authentication.length;
    header.length += servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &serv_authlist.authentication); /* Hardcoded authentication */
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in));
    }

    g_free (status.data);
}

static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_unwilling (struct sockaddr_storage *clnt_sa, gint type)
#else
gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type)
#endif
{
    ARRAY8 status;
    XdmcpHeader header;
    static time_t last_time = 0;

    /* only send at most one packet per second,
       no harm done if we don't send it at all */
    if (last_time + 1 >= time (NULL))
	    return;
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_xdmcp_send_unwilling: Sending UNWILLING to %s", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	gdm_error (_("Denied XDMCP query from host %s"), buffer6); 
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_send_unwilling: Sending UNWILLING to %s", inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
    
	gdm_error (_("Denied XDMCP query from host %s"), inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));    
    }
    
    /* Don't translate, this goes over the wire to servers where we
     * don't know the charset or language, so it must be ascii */
    status.data = (CARD8 *) "Display not authorized to connect";
    status.length = strlen ((char *) status.data);
    
    header.opcode = (CARD16) UNWILLING;
    header.length = 4 + servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in));
    }

    last_time = time (NULL);
}

static void
#ifdef ENABLE_IPV6
gdm_xdmcp_really_send_managed_forward (struct sockaddr_storage *clnt_sa,
                                      struct sockaddr_storage *origin)
#else
gdm_xdmcp_really_send_managed_forward (struct sockaddr_in *clnt_sa,
				       struct sockaddr_in *origin)
#endif
{
	ARRAY8 address;
	XdmcpHeader header;
	struct in_addr addr;

#ifdef ENABLE_IPV6
	struct in6_addr addr6;

	if (clnt_sa->ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_xdmcp_really_send_managed_forward: "
                          "Sending MANAGED_FORWARD to %s",
                          inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), buffer6, INET6_ADDRSTRLEN));

		address.length = sizeof (struct in6_addr);
		address.data = (void *)&addr6;
		memcpy (address.data, &(((struct sockaddr_in6 *)origin)->sin6_addr), sizeof (struct in6_addr));
	}
	else
#endif
	{
		gdm_debug ("gdm_xdmcp_really_send_managed_forward: "
                          "Sending MANAGED_FORWARD to %s",
                          inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));

		address.length = sizeof (struct in_addr);
		address.data = (void *)&addr;
		memcpy (address.data, &(((struct sockaddr_in *)origin)->sin_addr), sizeof (struct in_addr));
	}

	header.opcode = (CARD16) GDM_XDMCP_MANAGED_FORWARD;
	header.length = 4 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
		XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in6));
	}
	else
#endif
	{
		XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa, (int)sizeof (struct sockaddr_in));
	}

}

static gboolean
managed_forward_handler (gpointer data)
{
	ManagedForward *mf = data;
	if (gdm_xdmcpfd > 0)
		gdm_xdmcp_really_send_managed_forward (&(mf->manager),
						       &(mf->origin));
	mf->times ++;
	if (gdm_xdmcpfd <= 0 || mf->times >= 2) {
		managed_forwards = g_slist_remove (managed_forwards, mf);
		mf->handler = 0;
		/* mf freed by glib */
		return FALSE;
	}
	return TRUE;
}

static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_managed_forward (struct sockaddr_storage *clnt_sa,
                               struct sockaddr_storage *origin)
#else
gdm_xdmcp_send_managed_forward (struct sockaddr_in *clnt_sa,
				struct sockaddr_in *origin)
#endif
{
	ManagedForward *mf;

	gdm_xdmcp_really_send_managed_forward (clnt_sa, origin);

	mf = g_new0 (ManagedForward, 1);
	mf->times = 0;
#ifdef ENABLE_IPV6
	memcpy (&(mf->manager), clnt_sa, sizeof (struct sockaddr_storage));
	memcpy (&(mf->origin), origin, sizeof (struct sockaddr_storage));
#else
	memcpy (&(mf->manager), clnt_sa, sizeof (struct sockaddr_in));
	memcpy (&(mf->origin), origin, sizeof (struct sockaddr_in));
#endif

	mf->handler = g_timeout_add_full (G_PRIORITY_DEFAULT,
					  MANAGED_FORWARD_INTERVAL,
					  managed_forward_handler,
					  mf,
					  (GDestroyNotify) g_free);
	managed_forwards = g_slist_prepend (managed_forwards, mf);
}

#ifdef ENABLE_IPV6
static void
gdm_xdmcp_send_got_managed_forward6 (struct sockaddr_in6 *clnt_sa,
                                   struct in6_addr *origin)
{
	ARRAY8 address;
	XdmcpHeader header;
	struct in6_addr addr;
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_xdmcp_send_managed_forward: "
                  "Sending GOT_MANAGED_FORWARD to %s",
                  inet_ntop (AF_INET6, &clnt_sa->sin6_addr, buffer6, INET6_ADDRSTRLEN));

	address.length = sizeof (struct in6_addr);
	address.data = (void *)&addr;
	memcpy (address.data, origin, sizeof (struct in6_addr));

	header.opcode = (CARD16) GDM_XDMCP_GOT_MANAGED_FORWARD;
	header.length = 16 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in6));
}
#endif

static void
gdm_xdmcp_send_got_managed_forward (struct sockaddr_in *clnt_sa,
				    struct in_addr *origin)
{
	ARRAY8 address;
	XdmcpHeader header;
	struct in_addr addr;

	gdm_debug ("gdm_xdmcp_send_managed_forward: "
		   "Sending GOT_MANAGED_FORWARD to %s",
		   inet_ntoa (clnt_sa->sin_addr));

	address.length = sizeof (struct in_addr);
	address.data = (void *)&addr;
	memcpy (address.data, origin, sizeof (struct in_addr));

	header.opcode = (CARD16) GDM_XDMCP_GOT_MANAGED_FORWARD;
	header.length = 4 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_in));
}

static void
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_request (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_request (struct sockaddr_in *clnt_sa, gint len)
#endif
{
    CARD16 clnt_dspnum;
    ARRAY16 clnt_conntyp;
    ARRAYofARRAY8 clnt_addr;
    ARRAY8 clnt_authname;
    ARRAY8 clnt_authdata;
    ARRAYofARRAY8 clnt_authorization;
    ARRAY8 clnt_manufacturer;
    gint explen;
    gint i;
    gboolean mitauth = FALSE;
    gboolean entered = FALSE;
    int maxsessions  = gdm_get_value_int (GDM_KEY_MAX_SESSIONS);
    int maxpending   = gdm_get_value_int (GDM_KEY_MAX_PENDING);
    int dispperhost  = gdm_get_value_int (GDM_KEY_DISPLAYS_PER_HOST);
    
#ifdef ENABLE_IPV6
    char buffer6[INET6_ADDRSTRLEN];

    inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN);

    if (clnt_sa->ss_family == AF_INET6) {
	gdm_debug ("gdm_xdmcp_handle_request: Got REQUEST from %s",buffer6);
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_handle_request: Got REQUEST from %s",
                  inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
   }
    
    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
	    gdm_error (_("%s: Got REQUEST from banned host %s"),
                      "gdm_xdmcp_handle_request",
                       inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
	    gdm_error (_("%s: Got REQUEST from banned host %s"),
                      "gdm_xdmcp_handle_request",
                       inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
	}

	return;
    }

    gdm_xdmcp_displays_check (); /* Purge pending displays */
    
    /* Remote display number */
    if G_UNLIKELY (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("%s: Could not read Display Number"),
		   "gdm_xdmcp_handle_request");
	return;
    }
    
    /* We don't care about connection type. Address says it all */
    if G_UNLIKELY (! XdmcpReadARRAY16 (&buf, &clnt_conntyp)) {
	gdm_error (_("%s: Could not read Connection Type"),
		   "gdm_xdmcp_handle_request");
	return;
    }
    
    /* This is TCP/IP - we don't care */
    if G_UNLIKELY (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_addr)) {
	gdm_error (_("%s: Could not read Client Address"),
		   "gdm_xdmcp_handle_request");
        XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    /* Read authentication type */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_authname)) {
	gdm_error (_("%s: Could not read Authentication Names"),
		   "gdm_xdmcp_handle_request");
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    /* Read authentication data */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_authdata)) {
	gdm_error (_("%s: Could not read Authentication Data"),
		   "gdm_xdmcp_handle_request");
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	XdmcpDisposeARRAY8 (&clnt_authname);
	return;
    }
    
    /* Read and select from supported authorization list */
    if G_UNLIKELY (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authorization)) {
	gdm_error (_("%s: Could not read Authorization List"),
		   "gdm_xdmcp_handle_request");
	XdmcpDisposeARRAY8 (&clnt_authdata);
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	XdmcpDisposeARRAY8 (&clnt_authname);
	return;
    }
    
    /* libXdmcp doesn't terminate strings properly so we cheat and use strncmp () */
    for (i = 0 ; i < clnt_authorization.length ; i++)
	if (clnt_authorization.data[i].length == 18 &&
	    strncmp ((char *) clnt_authorization.data[i].data, "MIT-MAGIC-COOKIE-1", 18) == 0)
	    mitauth = TRUE;
    
    /* Manufacturer ID */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_manufacturer)) {
	gdm_error (_("%s: Could not read Manufacturer ID"),
		   "gdm_xdmcp_handle_request");
	XdmcpDisposeARRAY8 (&clnt_authname);
	XdmcpDisposeARRAY8 (&clnt_authdata);
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    /* Crude checksumming */
    explen = 2;		    /* Display Number */
    explen += 1+2*clnt_conntyp.length; /* Connection Type */
    explen += 1;		    /* Connection Address */
    for (i = 0 ; i < clnt_addr.length ; i++)
	explen += 2+clnt_addr.data[i].length;
    explen += 2+clnt_authname.length; /* Authentication Name */
    explen += 2+clnt_authdata.length; /* Authentication Data */
    explen += 1;		    /* Authorization Names */
    for (i = 0 ; i < clnt_authorization.length ; i++)
	explen += 2+clnt_authorization.data[i].length;
    explen += 2+clnt_manufacturer.length;
    
    if G_UNLIKELY (explen != len) {
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
	    gdm_error (_("%s: Failed checksum from %s"),
                      "gdm_xdmcp_handle_request",
                       inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
	    gdm_error (_("%s: Failed checksum from %s"),
                      "gdm_xdmcp_handle_request",
                       inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
	}

	XdmcpDisposeARRAY8 (&clnt_authname);
	XdmcpDisposeARRAY8 (&clnt_authdata);
	XdmcpDisposeARRAY8 (&clnt_manufacturer);
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }

    if G_UNLIKELY (gdm_get_value_bool (GDM_KEY_DEBUG)) {
	    char *s = g_strndup ((char *) clnt_manufacturer.data, clnt_manufacturer.length);
	    gdm_debug ("gdm_xdmcp_handle_request: xdmcp_pending=%d, MaxPending=%d, xdmcp_sessions=%d, MaxSessions=%d, ManufacturerID=%s",
		       xdmcp_pending, maxpending, xdmcp_sessions, maxsessions, ve_sure_string (s));
	    g_free (s);
    }

    /* Check if ok to manage display */
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	if (mitauth &&
	    xdmcp_sessions < maxsessions &&
	    (gdm_is_local_addr6 (&((struct sockaddr_in6 *)clnt_sa)->sin6_addr) || gdm_xdmcp_displays_from_host (clnt_sa) < dispperhost)) 

		entered = TRUE;
	} else
#endif
	{
	if (mitauth &&
	    xdmcp_sessions < maxsessions &&
	    (gdm_is_local_addr (&(((struct sockaddr_in *)clnt_sa)->sin_addr)) ||
	    gdm_xdmcp_displays_from_host (clnt_sa) < dispperhost)) 

		entered = TRUE;
	}

	if (entered) {
	    GdmHostent *he;
		he = gdm_gethostbyaddr (clnt_sa);

	    /* Check if we are already talking to this host */
	    gdm_xdmcp_display_dispose_check (he->hostname, clnt_dspnum);

	    if (xdmcp_pending >= maxpending) {
		    gdm_debug ("gdm_xdmcp_handle_request: maximum pending");
		    /* Don't translate, this goes over the wire to servers where we
		    * don't know the charset or language, so it must be ascii */
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum pending servers");	
		    gdm_hostent_free (he);
	    } else {
		    /* the addrs are NOT copied */
		    gdm_xdmcp_send_accept (he /* eaten and freed */, clnt_sa, clnt_dspnum);
	    }
	} else {
	    /* Don't translate, this goes over the wire to servers where we
	    * don't know the charset or language, so it must be ascii */
	    if ( ! mitauth) {
		    gdm_xdmcp_send_decline (clnt_sa, "Only MIT-MAGIC-COOKIE-1 supported");	
	    } else if (xdmcp_sessions >= maxsessions) {
		    gdm_info ("Maximum number of open XDMCP sessions reached");
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum number of open sessions reached");	
	    } else {
#ifdef ENABLE_IPV6
		if (clnt_sa->ss_family == AF_INET6)
		    gdm_info ("Maximum number of open XDMCP sessions from host %s reached", buffer6);
		else
#endif
		    gdm_info ("Maximum number of open XDMCP sessions from host %s reached",
			      inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum number of open sessions from your host reached");	
	    }
    }

    XdmcpDisposeARRAY8 (&clnt_authname);
    XdmcpDisposeARRAY8 (&clnt_authdata);
    XdmcpDisposeARRAY8 (&clnt_manufacturer);
    XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
    XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
    XdmcpDisposeARRAY16 (&clnt_conntyp);
}


static void
gdm_xdmcp_send_accept (GdmHostent *he /* eaten and freed */,
#ifdef ENABLE_IPV6
		       struct sockaddr_storage *clnt_sa,
#else
		       struct sockaddr_in *clnt_sa,
#endif
		       gint displaynum)
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 authname;
    ARRAY8 authdata;
    GdmDisplay *d;
    
    d = gdm_xdmcp_display_alloc (clnt_sa,
				 he /* eaten and freed */,
				 displaynum);
    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    authname.data = (CARD8 *) "MIT-MAGIC-COOKIE-1";
    authname.length = strlen ((char *) authname.data);
    
    authdata.data = (CARD8 *) d->bcookie;
    authdata.length = 16;
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) ACCEPT;
    header.length = 4;
    header.length += 2 + authentype.length;
    header.length += 2 + authendata.length;
    header.length += 2 + authname.length;
    header.length += 2 + authdata.length;
    
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteCARD32 (&buf, d->sessionid);
    XdmcpWriteARRAY8 (&buf, &authentype);
    XdmcpWriteARRAY8 (&buf, &authendata);
    XdmcpWriteARRAY8 (&buf, &authname);
    XdmcpWriteARRAY8 (&buf, &authdata);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in6));

	gdm_debug ("gdm_xdmcp_send_accept: Sending ACCEPT to %s with SessionID=%ld", inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN), (long)d->sessionid);
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in));

	gdm_debug ("gdm_xdmcp_send_accept: Sending ACCEPT to %s with SessionID=%ld",
                  inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr), (long)d->sessionid);
    }
    
}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_decline (struct sockaddr_storage *clnt_sa, const char *reason)
#else
gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa, const char *reason)
#endif
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 status;
    GdmForwardQuery *fq;
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_xdmcp_send_decline: Sending DECLINE to %s",
                  inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_send_decline: Sending DECLINE to %s",
                  inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
    }

    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    status.data = (CARD8 *) reason;
    status.length = strlen ((char *) status.data);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) DECLINE;
    header.length = 2 + status.length;
    header.length += 2 + authentype.length;
    header.length += 2 + authendata.length;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpWriteARRAY8 (&buf, &authentype);
    XdmcpWriteARRAY8 (&buf, &authendata);
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in));
    }

    /* Send MANAGED_FORWARD to indicate that the connection 
     * reached some sort of resolution */
    fq = gdm_forward_query_lookup (clnt_sa);
    if (fq != NULL) {
	    gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
	    gdm_forward_query_dispose (fq);
    }
}


static void 
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_manage (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len)
#endif
{
    CARD32 clnt_sessid;
    CARD16 clnt_dspnum;
    ARRAY8 clnt_dspclass;
    GdmDisplay *d;
    GdmIndirectDisplay *id;
    GdmForwardQuery *fq;
    
#ifdef ENABLE_IPV6
    char buffer6[INET6_ADDRSTRLEN];

    inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN);

    if (clnt_sa->ss_family == AF_INET6) {
	gdm_debug ("gdm_xdmcp_handle_manage: Got MANAGE from %s", buffer6);
    }
    else
#endif
    {
       gdm_debug ("gdm_xdmcp_handle_manage: Got MANAGE from %s", inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
    }

    
    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
           gdm_error (_("%s: Got Manage from banned host %s"),
                      "gdm_xdmcp_handle_manage",
                       buffer6);
	}
	else
#endif
	{
	    gdm_error (_("%s: Got Manage from banned host %s"),
                      "gdm_xdmcp_handle_manage",
                       inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
	}

	return;
    }
    
    /* SessionID */
    if G_UNLIKELY (! XdmcpReadCARD32 (&buf, &clnt_sessid)) {
	gdm_error (_("%s: Could not read Session ID"),
		   "gdm_xdmcp_handle_manage");
	return;
    }
    
    /* Remote display number */
    if G_UNLIKELY (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("%s: Could not read Display Number"),
		   "gdm_xdmcp_handle_manage");
	return;
    }
    
    /* Display Class */
    if G_UNLIKELY (! XdmcpReadARRAY8 (&buf, &clnt_dspclass)) {
	gdm_error (_("%s: Could not read Display Class"),
		   "gdm_xdmcp_handle_manage");
	return;
    }

    if G_UNLIKELY (gdm_get_value_bool (GDM_KEY_DEBUG)) {
	    char *s = g_strndup ((char *) clnt_dspclass.data, clnt_dspclass.length);

#ifdef ENABLE_IPV6
	    if (clnt_sa->ss_family == AF_INET6)
		gdm_debug ("gdm_xdmcp-handle_manage: Got display=%d, SessionID=%ld Class=%s from %s", (int)clnt_dspnum, (long)clnt_sessid, ve_sure_string (s), buffer6);
	else
#endif
		gdm_debug ("gdm_xdmcp_handle_manage: Got Display=%d, SessionID=%ld Class=%s from %s",
                       (int)clnt_dspnum, (long)clnt_sessid, ve_sure_string (s), inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));

	    g_free (s);
    }
    
    
    d = gdm_xdmcp_display_lookup (clnt_sessid);
    if (d != NULL &&
        d->dispstat == XDMCP_PENDING) {

	gdm_debug ("gdm_xdmcp_handle_manage: Looked up %s", d->name);

	if (gdm_get_value_bool (GDM_KEY_INDIRECT)) {
		id = gdm_choose_indirect_lookup (clnt_sa);

		/* This was an indirect thingie and nothing was yet chosen,
		 * use a chooser */
		if (d->dispstat == XDMCP_PENDING &&
		    id != NULL &&
		    id->chosen_host == NULL) {
			d->use_chooser = TRUE;
			d->indirect_id = id->id;
		} else {
			d->indirect_id = 0;
			d->use_chooser = FALSE;
			if (id != NULL)
				gdm_choose_indirect_dispose (id);
		}
	} else {
		d->indirect_id = 0;
		d->use_chooser = FALSE;
	}

	/* this was from a forwarded query quite apparently so
	 * send MANAGED_FORWARD */
	fq = gdm_forward_query_lookup (clnt_sa);
	if (fq != NULL) {
		gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
		gdm_forward_query_dispose (fq);
	}
	
	d->dispstat = XDMCP_MANAGED;
	xdmcp_sessions++;
	xdmcp_pending--;

	/* Start greeter/session */
	if G_UNLIKELY (!gdm_display_manage (d)) {
	    gdm_xdmcp_send_failed (clnt_sa, clnt_sessid);
	    XdmcpDisposeARRAY8(&clnt_dspclass);
	    return;
	}
    }
    else if G_UNLIKELY (d != NULL && d->dispstat == XDMCP_MANAGED) {
	gdm_debug ("gdm_xdmcp_handle_manage: Session id %ld already managed",
		   (long)clnt_sessid);	
    }
    else {
	gdm_debug ("gdm_xdmcp_handle_manage: Failed to look up session id %ld",
		   (long)clnt_sessid);
	gdm_xdmcp_send_refuse (clnt_sa, clnt_sessid);
    }

    XdmcpDisposeARRAY8(&clnt_dspclass);
}

static void 
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_managed_forward (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_managed_forward (struct sockaddr_in *clnt_sa, gint len)
#endif
{
	ARRAY8 clnt_address;
	GdmIndirectDisplay *id;
#ifdef ENABLE_IPV6
	char buffer6[INET6_ADDRSTRLEN];

	inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN);

	if (clnt_sa->ss_family == AF_INET6) {
               gdm_debug ("gdm_xdmcp_handle_managed_forward: "
                          "Got MANAGED_FORWARD from %s",
                          buffer6);
		/* Check with tcp_wrappers if client is allowed to access */
		if (! gdm_xdmcp_host_allow (clnt_sa)) {
			gdm_error ("%s: Got MANAGED_FORWARD from banned host %s", "gdm_xdmcp_handle_request", buffer6);
			return;
		}
	}
	else
#endif
	{
		gdm_debug ("gdm_xdmcp_handle_managed_forward: "
                          "Got MANAGED_FORWARD from %s",
                          inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));

		/* Check with tcp_wrappers if client is allowed to access */
		if (! gdm_xdmcp_host_allow (clnt_sa)) {
			gdm_error ("%s: Got MANAGED_FORWARD from banned host %s", 
			   "gdm_xdmcp_handle_request",
			   inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
			return;
		}
	}

	/* Hostname */
	if G_UNLIKELY ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_managed_forward");
		return;
	}
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
		if (clnt_address.length != sizeof (struct in6_addr)) {
			gdm_error (_("%s: Could not read address"),
                                  "gdm_xdmcp_handle_managed_forward");
			XdmcpDisposeARRAY8 (&clnt_address);
			return;
		}

		id = gdm_choose_indirect_lookup_by_chosen6 (&(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), (struct in6_addr *)clnt_address.data);
	}
	else
#endif
	{
		if (clnt_address.length != sizeof (struct in_addr)) {
			gdm_error (_("%s: Could not read address"),
                                  "gdm_xdmcp_handle_managed_forward");
			XdmcpDisposeARRAY8 (&clnt_address);
			return;
		}

		id = gdm_choose_indirect_lookup_by_chosen
                       (&(((struct sockaddr_in *)clnt_sa)->sin_addr), (struct in_addr *)clnt_address.data);
	}

	if (id != NULL) {
		gdm_choose_indirect_dispose (id);
	}

	/* Note: we send GOT even on not found, just in case our previous
	 * didn't get through and this was a second managed forward */
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
		gdm_xdmcp_send_got_managed_forward6 ((struct sockaddr_in6 *)clnt_sa, (struct in6_addr *)clnt_address.data);
	}
	else
#endif
	{
		gdm_xdmcp_send_got_managed_forward ((struct sockaddr_in *)clnt_sa, (struct in_addr *)clnt_address.data);
	}

	XdmcpDisposeARRAY8 (&clnt_address);
}

#ifdef ENABLE_IPV6
static void
gdm_xdmcp_whack_queued_managed_forwards6 (struct sockaddr_in6 *clnt_sa,
                                        struct in6_addr *origin)
{
	GSList *li;

	for (li = managed_forwards; li != NULL; li = li->next) {
		ManagedForward *mf = li->data;

		if ((memcmp (((struct sockaddr_in6 *)(&(mf->manager)))->sin6_addr.s6_addr, (clnt_sa->sin6_addr.s6_addr), sizeof (struct in6_addr)) == 0) &&
		    (memcmp (((struct sockaddr_in6 *)(&(mf->origin)))->sin6_addr.s6_addr, origin->s6_addr, sizeof (struct in6_addr)) == 0)) {
			managed_forwards = g_slist_remove_link (managed_forwards, li);
			g_slist_free_1 (li);
			g_source_remove (mf->handler);
			/* mf freed by glib */
			return;
		}
	}
}
#endif

static void
gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_in *clnt_sa,
					 struct in_addr *origin)
{
	GSList *li;

	for (li = managed_forwards; li != NULL; li = li->next) {
		ManagedForward *mf = li->data;
               if (((struct sockaddr_in *)(&mf->manager))->sin_addr.s_addr == clnt_sa->sin_addr.s_addr &&
                   ((struct sockaddr_in *)(&mf->origin))->sin_addr.s_addr == origin->s_addr) {

			managed_forwards = g_slist_remove_link (managed_forwards, li);
			g_slist_free_1 (li);
			g_source_remove (mf->handler);
			/* mf freed by glib */
			return;
		}
	}
}

static void 
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_got_managed_forward (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_got_managed_forward (struct sockaddr_in *clnt_sa, gint len)
#endif
{
	ARRAY8 clnt_address;

#ifdef ENABLE_IPV6
	char buffer6[INET6_ADDRSTRLEN];

	if (clnt_sa->ss_family == AF_INET6) {
		gdm_debug ("gdm_xdmcp_handle_got_managed_forward: "
                          "Got MANAGED_FORWARD from %s",
                          inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
		if (! gdm_xdmcp_host_allow (clnt_sa)) {
			gdm_error ("%s: Got GOT_MANAGED_FORWARD from banned host %s", "gdm_xdmcp_handle_request", buffer6);
			return;
		}
	}
	else
#endif
	{
		gdm_debug ("gdm_xdmcp_handle_got_managed_forward: "
                          "Got MANAGED_FORWARD from %s",
                          inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));

		/* Check with tcp_wrappers if client is allowed to access */
		if (! gdm_xdmcp_host_allow (clnt_sa)) {
			gdm_error ("%s: Got GOT_MANAGED_FORWARD from banned host %s", 
			   "gdm_xdmcp_handle_request",
			   inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
			return;
		}
	}

	/* Hostname */
	if G_UNLIKELY ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_got_managed_forward");
		return;
	}
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
		if (clnt_address.length != sizeof (struct in6_addr)) {
			gdm_error (_("%s: Could not read address"),
                                  "gdm_xdmcp_handle_got_managed_forward");
			XdmcpDisposeARRAY8 (&clnt_address);
			return;
		}

		gdm_xdmcp_whack_queued_managed_forwards6 ((struct sockaddr_in6 *)clnt_sa, (struct in6_addr *)clnt_address.data);
	}
	else
#endif
	{
		if (clnt_address.length != sizeof (struct in_addr)) {
			gdm_error (_("%s: Could not read address"),
                                  "gdm_xdmcp_handle_got_managed_forward");
			XdmcpDisposeARRAY8 (&clnt_address);
			return;
		}
		gdm_xdmcp_whack_queued_managed_forwards ((struct sockaddr_in *)clnt_sa, (struct in_addr *)clnt_address.data);
	}

	XdmcpDisposeARRAY8 (&clnt_address);
}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_refuse (struct sockaddr_storage *clnt_sa, CARD32 sessid)
#else
gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid)
#endif
{
    XdmcpHeader header;
    GdmForwardQuery *fq;
    
    gdm_debug ("gdm_xdmcp_send_refuse: Sending REFUSE to %ld", (long)sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode= (CARD16) REFUSE;
    header.length = 4;
    
    XdmcpWriteHeader (&buf, &header);  
    XdmcpWriteCARD32 (&buf, sessid);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                  (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in));
    }

    /* this was from a forwarded query quite apparently so
    * send MANAGED_FORWARD */
    fq = gdm_forward_query_lookup (clnt_sa);
    if (fq != NULL) {
	    gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
	    gdm_forward_query_dispose (fq);
    }
}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_failed (struct sockaddr_storage *clnt_sa, CARD32 sessid)
#else
gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid)
#endif
{
    XdmcpHeader header;
    ARRAY8 status;
    
    gdm_debug ("gdm_xdmcp_send_failed: Sending FAILED to %ld", (long)sessid);
    
    /* Don't translate, this goes over the wire to servers where we
     * don't know the charset or language, so it must be ascii */
    status.data = (CARD8 *) "Failed to start session";
    status.length = strlen ((char *) status.data);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) FAILED;
    header.length = 6+status.length;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpWriteARRAY8 (&buf, &status);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in));
    }

}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_handle_keepalive (struct sockaddr_storage *clnt_sa, gint len)
#else
gdm_xdmcp_handle_keepalive (struct sockaddr_in *clnt_sa, gint len)
#endif
{
    CARD16 clnt_dspnum;
    CARD32 clnt_sessid;
    
#ifdef ENABLE_IPV6
    char buffer6[INET6_ADDRSTRLEN];

    if (clnt_sa->ss_family == AF_INET6) {
	gdm_debug ("gdm_xdmcp_handle_keepalive: Got KEEPALIVE from %s",
                  inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr),
buffer6, INET6_ADDRSTRLEN));

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error (_("%s: Got KEEPALIVE from banned host %s"),
                      "gdm_xdmcp_handle_keepalive", buffer6);
		return;

	}
    }
    else
#endif
    {
	gdm_debug ("gdm_xdmcp_handle_keepalive: Got KEEPALIVE from %s",
                  inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));

    /* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error (_("%s: Got KEEPALIVE from banned host %s"), 
		   "gdm_xdmcp_handle_keepalive",
		   inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr));
		return;
	}
    }
    
    /* Remote display number */
    if G_UNLIKELY (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("%s: Could not read Display Number"),
		   "gdm_xdmcp_handle_keepalive");
	return;
    }
    
    /* SessionID */
    if G_UNLIKELY (! XdmcpReadCARD32 (&buf, &clnt_sessid)) {
	gdm_error (_("%s: Could not read Session ID"),
		   "gdm_xdmcp_handle_keepalive");
	return;
    }
    
    gdm_xdmcp_send_alive (clnt_sa, clnt_dspnum, clnt_sessid);
}


static void
#ifdef ENABLE_IPV6
gdm_xdmcp_send_alive (struct sockaddr_storage *clnt_sa, CARD16 dspnum, CARD32 sessid)
#else
gdm_xdmcp_send_alive (struct sockaddr_in *clnt_sa, CARD16 dspnum, CARD32 sessid)
#endif
{
    XdmcpHeader header;
    GdmDisplay *d;
    int send_running = 0;
    CARD32 send_sessid = 0;
    
    d = gdm_xdmcp_display_lookup (sessid);
    if (d == NULL)
	    d = gdm_xdmcp_display_lookup_by_host (clnt_sa, dspnum);

    if (d != NULL) {
	    send_sessid = d->sessionid;
	    if (d->dispstat == XDMCP_MANAGED)
		    send_running = 1;
    }

    gdm_debug ("Sending ALIVE to %ld (running %d, sessid %ld)",
	       (long)sessid, send_running, (long)send_sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) ALIVE;
    header.length = 5;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD8 (&buf, send_running);
    XdmcpWriteCARD32 (&buf, send_sessid);
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in6));
    }
    else
#endif
    {
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
                   (int)sizeof (struct sockaddr_in));
    }
}


static gboolean
gdm_xdmcp_host_allow (
#ifdef ENABLE_IPV6
                      struct sockaddr_storage *clnt_sa
#else
		      struct sockaddr_in *clnt_sa
#endif
		     )
{
#ifdef HAVE_TCPWRAPPERS
	
    /* avoids a warning, my tcpd.h file doesn't include this prototype, even
     * though the library does include the function and the manpage mentions it
     */
    extern int hosts_ctl (char *daemon, char *client_name, char *client_addr, char *client_user);

    GdmHostent *client_he;
    char *client;
    gboolean ret;
    
    /* Find client hostname */
    client_he = gdm_gethostbyaddr (clnt_sa);

    if (client_he->not_found)
	    client = "unknown";
    else {
		gdm_debug ("gdm_xdmcp_host_allow: client->hostname is %s\n", client_he->hostname);
	    client = client_he->hostname;
	}

    /* Check with tcp_wrappers if client is allowed to access */
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	ret = (hosts_ctl ("gdm", client, (char *) inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN), ""));
    } else
#endif
    {
       ret = (hosts_ctl ("gdm", client, inet_ntoa (((struct sockaddr_in *)clnt_sa)->sin_addr), ""));
    }

    gdm_hostent_free (client_he);

    return ret;
#else /* HAVE_TCPWRAPPERS */
    return (TRUE);
#endif /* HAVE_TCPWRAPPERS */
}


static GdmDisplay *
gdm_xdmcp_display_alloc (
#ifdef ENABLE_IPV6
			 struct sockaddr_storage *addr,
#else
			 struct sockaddr_in *addr,
#endif
			 GdmHostent *he /* eaten and freed */,
			 int displaynum)
{
    GdmDisplay *d = NULL;
    char *proxycmd = gdm_get_value_string (GDM_KEY_XDMCP_PROXY_XSERVER);
    
    d = g_new0 (GdmDisplay, 1);

    if (gdm_get_value_bool (GDM_KEY_XDMCP_PROXY) && proxycmd != NULL) {
	    d->type = TYPE_XDMCP_PROXY;
	    d->command = g_strdup (proxycmd);
	    gdm_debug ("Using proxy server for XDMCP: %s\n", d->command);
    } else {
	    d->type = TYPE_XDMCP;
    }

    d->logout_action = GDM_LOGOUT_ACTION_NONE;
    d->authfile = NULL;
    d->auths = NULL;
    d->userauth = NULL;
    d->greetpid = 0;
    d->servpid = 0;
    d->servstat = 0;
    d->sesspid = 0;
    d->slavepid = 0;
    d->attached = FALSE;
    d->dispstat = XDMCP_PENDING;
    d->sessionid = globsessid++;
    if (d->sessionid == 0)
	    d->sessionid = globsessid++;
    d->acctime = time (NULL);
    d->dispnum = displaynum;
    d->xdmcp_dispnum = displaynum;

    d->handled = TRUE;
    d->tcp_disallowed = FALSE;

    d->vt = -1;

    d->x_servers_order = -1;

    d->logged_in = FALSE;
    d->login = NULL;

    d->sleep_before_run = 0;
    if (gdm_get_value_bool (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN) &&
	! ve_string_empty (gdm_get_value_string (GDM_KEY_TIMED_LOGIN))) {
	    d->timed_login_ok = TRUE;
    } else {
	    d->timed_login_ok = FALSE;
    }
    
    d->name = g_strdup_printf ("%s:%d", he->hostname,
			       displaynum);
#ifdef ENABLE_IPV6
    if (addr->ss_family == AF_INET6) {
	memcpy (&d->addr6, &((struct sockaddr_in6 *)addr)->sin6_addr, sizeof (struct in6_addr));
	d->addrtype = AF_INET6;
    }
    else
#endif
    {
	memcpy (&d->addr, &((struct sockaddr_in *)addr)->sin_addr, sizeof (struct in_addr));
	d->addrtype = AF_INET;
    }

    d->hostname = he->hostname;
    he->hostname = NULL;
    d->addrs = he->addrs;
    he->addrs = NULL;
    d->addr_count = he->addr_count;
    he->addr_count = 0;

    gdm_hostent_free (he);

    d->slave_notify_fd = -1;
    d->master_notify_fd = -1;

    d->xsession_errors_bytes = 0;
    d->xsession_errors_fd = -1;
    d->session_output_fd = -1;

    d->chooser_output_fd = -1;
    d->chooser_last_line = NULL;

    d->theme_name = NULL;

    /* Secure display with cookie */
    if G_UNLIKELY (! gdm_auth_secure_display (d))
	gdm_error ("gdm_xdmcp_display_alloc: Error setting up cookies for %s", d->name);

    if (d->type == TYPE_XDMCP_PROXY) {
	    d->parent_disp = d->name;
	    d->name = g_strdup (":-1");
	    d->dispnum = -1;

	    d->server_uid = gdm_get_gdmuid ();

	    d->parent_auth_file = d->authfile;
	    d->authfile = NULL;
    }

    displays = g_slist_append (displays, d);
    
    xdmcp_pending++;
    
    gdm_debug ("gdm_xdmcp_display_alloc: display=%s, session id=%ld, xdmcp_pending=%d ",
	       d->name, (long)d->sessionid, xdmcp_pending);
    
    return d;
}


static GdmDisplay *
gdm_xdmcp_display_lookup (CARD32 sessid)
{
    GSList *dlist = displays;
    GdmDisplay *d;
    
    if (!sessid)
	return (NULL);

    while (dlist) {
	d = (GdmDisplay *) dlist->data;
	
	if (d && d->sessionid == sessid)
	    return (d);
	
	dlist = dlist->next;
    }
    
    return (NULL);
}


static void
gdm_xdmcp_display_dispose_check (const gchar *hostname, int dspnum)
{
	GSList *dlist;

	if (hostname == NULL)
		return;

	gdm_debug ("gdm_xdmcp_display_dispose_check (%s:%d)", hostname, dspnum);

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    SERVER_IS_XDMCP (d) &&
		    d->xdmcp_dispnum == dspnum &&
		    strcmp (d->hostname, hostname) == 0) {
			if (d->dispstat == XDMCP_MANAGED)
				gdm_display_unmanage (d);
			else
				gdm_display_dispose (d);

			/* restart as the list is now broken */
			dlist = displays;
		} else {
			/* just go on */
			dlist = dlist->next;
		}
	}
}


static void 
gdm_xdmcp_displays_check (void)
{
	GSList *dlist;
	time_t curtime = time (NULL);

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    SERVER_IS_XDMCP (d) &&
		    d->dispstat == XDMCP_PENDING &&
		    curtime > d->acctime + gdm_get_value_int (GDM_KEY_MAX_WAIT)) {
			gdm_debug ("gdm_xdmcp_displays_check: Disposing session id %ld",
				   (long)d->sessionid);
			gdm_display_dispose (d);

			/* restart as the list is now broken */
			dlist = displays;
		} else {
			/* just go on */
			dlist = dlist->next;
		}
	}
}

static void
reconnect_to_parent (GdmDisplay *to)
{
	GError *error;
	gchar *command_argv[10];
	gchar *proxyreconnect = gdm_get_value_string (GDM_KEY_XDMCP_PROXY_RECONNECT);

	command_argv[0] = proxyreconnect;
	command_argv[1] = "--display";
	command_argv[2] = to->parent_disp;
	command_argv[3] = "--display-authfile";
	command_argv[4] = to->parent_auth_file;
	command_argv[5] = "--to";
	command_argv[6] = to->name;
	command_argv[7] = "--to-authfile";
	command_argv[8] = to->authfile;
	command_argv[9] = NULL;

	gdm_debug ("XDMCP: migrating display by running "
		   "'%s --display %s --display-authfile %s --to %s --to-authfile %s'",
		   proxyreconnect,
		   to->parent_disp, to->parent_auth_file,
		   to->name, to->authfile);

	error = NULL;
	if (!g_spawn_async (NULL, command_argv, NULL, 0, NULL, NULL, NULL, &error)) {
		gdm_error (_("%s: Failed to run "
			     "'%s --display %s --display-authfile %s --to %s --to-authfile %s': %s"),
			   "gdm_xdmcp_migrate",
			   proxyreconnect,
			   to->parent_disp, to->parent_auth_file,
			   to->name, to->authfile,
			   error->message);
		g_error_free (error);
	}
}

void
gdm_xdmcp_migrate (GdmDisplay *from, GdmDisplay *to)
{
	if (from->type != TYPE_XDMCP_PROXY ||
	    to->type   != TYPE_XDMCP_PROXY)
		return;

	g_free (to->parent_disp);
	to->parent_disp = from->parent_disp;
	from->parent_disp = NULL;

	g_free (to->parent_auth_file);
	to->parent_auth_file = from->parent_auth_file;
	from->parent_auth_file = NULL;

	reconnect_to_parent (to);
}

#else /* HAVE_LIBXDMCP */

/* Here come some empty stubs for no XDMCP support */
int
gdm_xdmcp_init  (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_init");
	return FALSE;
}

void
gdm_xdmcp_run (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_run");
}

void
gdm_xdmcp_close (void)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_close");
}

void
gdm_xdmcp_migrate (GdmDisplay *from, GdmDisplay *to)
{
	gdm_error (_("%s: No XDMCP support"), "gdm_xdmcp_migrate");
}

#endif /* HAVE_LIBXDMCP */

/* EOF */
