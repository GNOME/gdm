/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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

#include "config.h"

#ifdef HAVE_LIBXDMCP
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
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

#include <glib/gi18n.h>

#ifdef HAVE_LIBXDMCP
#include "gdm.h"
#include "display.h"
#include "auth.h"
#endif /* HAVE_LIBXDMCP */

#include "misc.h"
#include "choose.h"
#include "xdmcp.h"
#include "cookie.h"

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-daemon-config.h"

/* This is to be changed when the Xdmcp Multicast address is decided */
#define XDMCP_MULTICAST_ADDRESS "ff02::1"

static int gdm_xdmcpfd = -1;

static gint xdmcp_sessions = 0;	/* Number of remote sessions */
static gint xdmcp_pending = 0;  /* Number of pending remote sessions */

#ifdef HAVE_LIBXDMCP

/*
 * On Sun, we need to define allow_severity and deny_severity to link
 * against libwrap.
 */
#ifdef __sun
#include <syslog.h>

gint allow_severity = LOG_INFO;
gint deny_severity = LOG_WARNING;
#endif

static guint xdmcp_source = 0;
static CARD32 globsessid;
static gchar *sysid;
static ARRAY8 servhost;
static XdmcpBuffer buf;
static gboolean initted = FALSE;

/* Local prototypes */
static gboolean gdm_xdmcp_decode_packet (GIOChannel *source,
					 GIOCondition cond,
					 gpointer data);

static void gdm_xdmcp_handle_forward_query (struct sockaddr_storage *clnt_sa,
                                            gint len);
static void gdm_xdmcp_handle_query         (struct sockaddr_storage *clnt_sa,
                                            gint len,
                                            gint type);
static void gdm_xdmcp_handle_request       (struct sockaddr_storage *clnt_sa,
                                            gint len);

static void gdm_xdmcp_handle_manage        (struct sockaddr_storage *clnt_sa,
                                            gint len);
static void gdm_xdmcp_handle_managed_forward (struct sockaddr_storage *clnt_sa,
                                            gint len);
static void gdm_xdmcp_handle_got_managed_forward (struct sockaddr_storage *clnt_sa,
                                            gint len);
static void gdm_xdmcp_handle_keepalive     (struct sockaddr_storage *clnt_sa,
                                            gint len);
static void gdm_xdmcp_send_willing         (struct sockaddr_storage *clnt_sa);
static void gdm_xdmcp_send_unwilling       (struct sockaddr_storage *clnt_sa,
                                            gint type);
static void gdm_xdmcp_send_accept          (GdmHostent *he,
                                            struct sockaddr_storage *clnt_sa,
                                            gint displaynum);
static void gdm_xdmcp_send_decline         (struct sockaddr_storage *clnt_sa,
                                            const char *reason);
static void gdm_xdmcp_send_refuse          (struct sockaddr_storage *clnt_sa,
                                            CARD32 sessid);
static void gdm_xdmcp_send_failed          (struct sockaddr_storage *clnt_sa,
                                            CARD32 sessid);
static void gdm_xdmcp_send_alive           (struct sockaddr_storage *clnt_sa,
                                            CARD16 dspnum, CARD32 sessid);
static void gdm_xdmcp_send_managed_forward (struct sockaddr_storage *clnt_sa,
                                            struct sockaddr_storage *origin);
static gboolean gdm_xdmcp_host_allow       (struct sockaddr_storage *clnt_sa);
static GdmForwardQuery * gdm_forward_query_alloc (struct sockaddr_storage *mgr_sa,
                                            struct sockaddr_storage *dsp_sa);
static GdmForwardQuery * gdm_forward_query_lookup (struct sockaddr_storage *clnt_sa);
static int gdm_xdmcp_displays_from_host    (struct sockaddr_storage *addr);
static GdmDisplay *gdm_xdmcp_display_lookup_by_host (struct sockaddr_storage *addr,
						     int dspnum);
static GdmDisplay *gdm_xdmcp_display_alloc (struct sockaddr_storage *addr,
                                            GdmHostent *he,
					    int displaynum);

static void gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_storage *clnt_sa,
						     struct sockaddr_storage *origin);

static void gdm_xdmcp_send_got_managed_forward (struct sockaddr_storage *clnt_sa,
						struct sockaddr_storage *origin);

static void gdm_xdmcp_send_forward_query   (GdmIndirectDisplay *id,
                                            struct sockaddr_storage *clnt_sa,
                                            struct sockaddr_storage *display_addr,
                                            ARRAYofARRAY8Ptr authlist);
static void gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_storage *clnt_sa,
						     struct sockaddr_storage *origin);

static GdmDisplay *gdm_xdmcp_display_lookup (CARD32 sessid);
static void gdm_xdmcp_display_dispose_check (const gchar *hostname, int dspnum);
static void gdm_xdmcp_displays_check       (void);
static void gdm_forward_query_dispose      (GdmForwardQuery *q);

static GSList *forward_queries = NULL;

typedef struct {
	int times;
	guint handler;
	struct sockaddr_storage manager;
	struct sockaddr_storage origin;
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
 * Furthermore user passwords go over the wire in cleartext anyway,
 * so protecting cookies is not that important.
 */

typedef struct _XdmAuth {
	ARRAY8 authentication;
	ARRAY8 authorization;
} XdmAuthRec, *XdmAuthPtr;

static XdmAuthRec serv_authlist = {
	{ (CARD16) 0, (CARD8 *) 0 },
	{ (CARD16) 0, (CARD8 *) 0 }
};

static int
gdm_xdmcp_displays_from_host (struct sockaddr_storage *addr)
{
	GSList *li;
	int count = 0;
        GSList *displays;

        displays = gdm_daemon_config_get_display_list ();

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (SERVER_IS_XDMCP (disp)) {

			if (gdm_address_equal (&disp->addr, addr)) {
				count++;
			}
		}
	}
	return count;
}

static GdmDisplay *
gdm_xdmcp_display_lookup_by_host (struct sockaddr_storage *addr, int dspnum)
{
	GSList *li;
        GSList *displays;

        displays = gdm_daemon_config_get_display_list ();

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;

		if (SERVER_IS_XDMCP (disp)) {

			if (gdm_address_equal (&disp->addr, addr)
			    && disp->xdmcp_dispnum == dspnum) {
				return disp;
			}
		}
	}

	return NULL;
}

/* for debugging */
static const char *
ai_family_str (struct addrinfo *ai)
{
	const char *str;
	switch (ai->ai_family) {
	case AF_INET:
		str = "inet";
		break;
	case AF_INET6:
		str = "inet6";
		break;
	case AF_UNIX:
		str = "unix";
		break;
	case AF_UNSPEC:
		str = "unspecified";
		break;
	default:
		str = "unknown";
		break;
	}
	return str;
}

/* for debugging */
static const char *
ai_type_str (struct addrinfo *ai)
{
	const char *str;
	switch (ai->ai_socktype) {
	case SOCK_STREAM:
		str = "stream";
		break;
	case SOCK_DGRAM:
		str = "datagram";
		break;
	case SOCK_SEQPACKET:
		str = "seqpacket";
		break;
	case SOCK_RAW:
		str = "raw";
		break;
	default:
		str = "unknown";
		break;
	}
	return str;
}

/* for debugging */
static const char *
ai_protocol_str (struct addrinfo *ai)
{
	const char *str;
	switch (ai->ai_protocol) {
	case 0:
		str = "default";
		break;
	case IPPROTO_TCP:
		str = "TCP";
		break;
	case IPPROTO_UDP:
		str = "UDP";
		break;
	case IPPROTO_RAW:
		str = "raw";
		break;
	default:
		str = "unknown";
		break;
	}

	return str;
}

/* for debugging */
static char *
ai_flags_str (struct addrinfo *ai)
{
	GString *str;

	str = g_string_new ("");
	if (ai->ai_flags == 0) {
		g_string_append (str, "none");
	} else {
		if (ai->ai_flags & AI_PASSIVE) {
			g_string_append (str, "passive ");
		}
		if (ai->ai_flags & AI_CANONNAME) {
			g_string_append (str, "canon ");
		}
		if (ai->ai_flags & AI_NUMERICHOST) {
			g_string_append (str, "numhost ");
		}
		if (ai->ai_flags & AI_NUMERICSERV) {
			g_string_append (str, "numserv ");
		}
		if (ai->ai_flags & AI_V4MAPPED) {
			g_string_append (str, "v4mapped ");
		}
		if (ai->ai_flags & AI_ALL) {
			g_string_append (str, "all ");
		}
	}
	return g_string_free (str, FALSE);
}

/* for debugging */
static void
debug_addrinfo (struct addrinfo *ai)
{
	char *str;
	str = ai_flags_str (ai);
	g_debug ("XDMCP: addrinfo family=%s type=%s proto=%s flags=%s",
		 ai_family_str (ai),
		 ai_type_str (ai),
		 ai_protocol_str (ai),
		 str);
	g_free (str);
}

static int
gdm_xdmcp_create_socket (struct addrinfo *ai)
{
	int sock;

	sock = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sock < 0) {
		g_warning ("socket: %s", g_strerror (errno));
		return sock;
	}

	if (bind (sock, ai->ai_addr, ai->ai_addrlen) < 0) {
		g_warning ("bind: %s", g_strerror (errno));
		close (sock);
		return -1;
	}

	return sock;
}

static int
gdm_xdmcp_bind (guint                     port,
		int                       family,
		struct sockaddr_storage * hostaddr)
{
	struct addrinfo  hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai;
	char             strport[NI_MAXSERV];
	int              gaierr;
	int              sock;

	sock = -1;

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	snprintf (strport, sizeof (strport), "%u", port);
	if ((gaierr = getaddrinfo (NULL, strport, &hints, &ai_list)) != 0) {
		g_error ("Unable to connect to socket: %s", gai_strerror (gaierr));
		return -1;
	}

	/* should only be one but.. */
	for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
			continue;
		}

		debug_addrinfo (ai);

		if (sock < 0) {
			char *host;
			char *serv;

			gdm_address_get_info ((struct sockaddr_storage *)ai->ai_addr, &host, &serv);
			g_debug ("XDMCP: Attempting to bind to host %s port %s", host, serv);
			g_free (host);
			g_free (serv);
			sock = gdm_xdmcp_create_socket (ai);
			if (sock >= 0) {
				if (hostaddr != NULL) {
					memcpy (hostaddr, ai->ai_addr, ai->ai_addrlen);
				}
			}
		}
	}

	freeaddrinfo (ai_list);

	return sock;
}

gboolean
gdm_xdmcp_init (void)
{
	struct sockaddr_storage serv_sa = { 0 };
	gchar hostbuf[1024];
	struct utsname name;
	int udpport;

	if ( ! gdm_daemon_config_get_value_bool (GDM_KEY_XDMCP))
		return TRUE;

	udpport = gdm_daemon_config_get_value_int (GDM_KEY_UDP_PORT);
	globsessid = g_random_int ();

	/* Fetch and store local hostname in XDMCP friendly format */
	hostbuf[1023] = '\0';
	if G_UNLIKELY (gethostname (hostbuf, 1023) != 0) {
		gdm_error (_("%s: Could not get server hostname: %s!"),
			   "gdm_xdmcp_init", strerror (errno));
		strcpy (hostbuf, "localhost.localdomain");
	}

	if ( ! initted) {
		uname (&name);
		sysid           = g_strconcat (name.sysname, " ",
					       name.release, NULL);
		servhost.data   = (CARD8 *) g_strdup (hostbuf);
		servhost.length = strlen ((char *) servhost.data);

		initted         = TRUE;
	}

	gdm_debug ("XDMCP: Start up on host %s, port %d", hostbuf, udpport);

	/* Open socket for communications */
#ifdef ENABLE_IPV6
	gdm_xdmcpfd = gdm_xdmcp_bind (udpport, AF_INET6, &serv_sa);
	if (gdm_xdmcpfd < 0)
#endif
		gdm_xdmcpfd = gdm_xdmcp_bind (udpport, AF_INET, &serv_sa);

	if G_UNLIKELY (gdm_xdmcpfd < 0) {
		gdm_error (_("%s: Could not create socket!"), "gdm_xdmcp_init");
		gdm_daemon_config_set_value_bool (GDM_KEY_XDMCP, FALSE);
		return FALSE;
	}

#ifdef ENABLE_IPV6
	/* Checking and Setting Multicast options */
	if (gdm_daemon_config_get_value_bool (GDM_KEY_MULTICAST)) {
		/*
		 * socktemp is a temporary socket for getting info about
		 * available interfaces
		 */
		int socktemp;
		int i, num;
		char *buf;
		struct ipv6_mreq mreq;

		/* For interfaces' list */
		struct ifconf ifc;
		struct ifreq *ifr;

		/* Extract Multicast address for IPv6 */
		if (ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_MULTICAST_ADDR))) {

			/* Stuff it with all-node multicast address */
			gdm_daemon_config_set_value_string (GDM_KEY_MULTICAST_ADDR,
							    XDMCP_MULTICAST_ADDRESS);
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

			/* Joining multicast group with all interfaces */
			for (i = 0 ; i < num ; i++) {
				struct ifreq ifreq;
				int ifindex;

				memset (&ifreq, 0, sizeof (ifreq));
				strncpy (ifreq.ifr_name, ifr[i].ifr_name, sizeof (ifreq.ifr_name));
				/* paranoia */
				ifreq.ifr_name[sizeof (ifreq.ifr_name) - 1] = '\0';

				if (ioctl (socktemp, SIOCGIFFLAGS, &ifreq) < 0) {
					gdm_debug ("XDMCP: Could not get SIOCGIFFLAGS for %s",
						   ifr[i].ifr_name);
				}

				ifindex = if_nametoindex (ifr[i].ifr_name);

				if ((!(ifreq.ifr_flags & IFF_UP) ||
				     (ifreq.ifr_flags & IFF_LOOPBACK)) ||
				    ((ifindex == 0 ) && (errno == ENXIO))) {
					/* Not a valid interface or loopback interface*/
					continue;
				}

				mreq.ipv6mr_interface = ifindex;
				inet_pton (AF_INET6,
					   gdm_daemon_config_get_value_string (GDM_KEY_MULTICAST_ADDR),
					   &mreq.ipv6mr_multiaddr);
				setsockopt (gdm_xdmcpfd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
					    &mreq, sizeof (mreq));
			}
		}
		g_free (buf);
		close (socktemp);
	}
#endif

	return TRUE;
}

void
gdm_xdmcp_run (void)
{
	GIOChannel *xdmcpchan;

	if (gdm_xdmcpfd < 0) {
		gdm_error ("XDMCP: socket is not open");
		return;
	}

	if (xdmcp_source > 0) {
		gdm_error ("XDMCP: already listening");
		return;
	}

	g_debug ("XDMCP: Starting to listen on XDMCP port");

	xdmcpchan = g_io_channel_unix_new (gdm_xdmcpfd);

	g_io_channel_set_encoding (xdmcpchan, NULL, NULL);
	g_io_channel_set_buffered (xdmcpchan, FALSE);

	xdmcp_source = g_io_add_watch_full (xdmcpchan,
					    G_PRIORITY_DEFAULT,
					    G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
					    gdm_xdmcp_decode_packet,
					    NULL,
					    NULL);
	g_io_channel_unref (xdmcpchan);
}

void
gdm_xdmcp_close (void)
{
	g_debug ("XMCP: closing XDMCP listener");

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
gdm_xdmcp_decode_packet (GIOChannel *source,
			 GIOCondition cond,
			 gpointer data)
{
	struct sockaddr_storage clnt_sa;
	gint sa_len = sizeof (clnt_sa);
	XdmcpHeader header;
	char *host;
	char *port;

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

	gdm_debug ("gdm_xdmcp_decode_packet: GIOCondition %d", (int)cond);

	if ( ! (cond & G_IO_IN)) {
		return TRUE;
	}

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

	gdm_address_get_info (&clnt_sa, &host, &port);

	if (header.opcode <= ALIVE) {
		g_debug ("gdm_xdmcp_decode: Received opcode %s from client %s : %s",
			 opcode_names[header.opcode],
			 host,
			 port);
	}

	if (header.opcode >= GDM_XDMCP_FIRST_OPCODE &&
	    header.opcode < GDM_XDMCP_LAST_OPCODE) {
		g_debug ("gdm_xdmcp_decode: Received opcode %s from client %s : %s",
			 gdm_opcode_names[header.opcode - GDM_XDMCP_FIRST_OPCODE],
			 host,
			 port);
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
		g_debug ("gdm_xdmcp_decode: Unknown opcode from client %s : %s",
			 host,
			 port);

		break;
	}

	g_free (host);
	g_free (port);

	return TRUE;
}

static void
gdm_xdmcp_handle_query (struct sockaddr_storage *clnt_sa,
			gint len,
			gint type)
{
	ARRAYofARRAY8 clnt_authlist;
	gint i = 0, explen = 1;
	char *host;
	char *port;

	gdm_address_get_info (clnt_sa, &host, &port);
	gdm_debug ("gdm_xdmcp_handle_query: Opcode %d from %s : %s", type, host, port);
	g_free (host);
	g_free (port);

	/* Extract array of authentication names from Xdmcp packet */
	if G_UNLIKELY (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
		gdm_error (_("%s: Could not extract authlist from packet"),
			   "gdm_xdmcp_handle_query");
		return;
	}

	/* Crude checksumming */
	for (i = 0 ; i < clnt_authlist.length ; i++) {
		if G_UNLIKELY (gdm_daemon_config_get_value_bool (GDM_KEY_DEBUG)) {
			char *s = g_strndup ((char *) clnt_authlist.data[i].data,
					     clnt_authlist.length);
			gdm_debug ("gdm_xdmcp_handle_query: authlist: %s",
				   ve_sure_string (s));
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

		if (gdm_daemon_config_get_value_bool (GDM_KEY_INDIRECT) && type == INDIRECT_QUERY) {

			GdmIndirectDisplay *id = gdm_choose_indirect_lookup (clnt_sa);

			if (id != NULL && id->chosen_host != NULL) {
				/* if user chose us, then just send willing */
				if (gdm_address_is_local (id->chosen_host)) {
					/* get rid of indirect, so that we don't get
					 * the chooser */
					gdm_choose_indirect_dispose (id);
					gdm_xdmcp_send_willing (clnt_sa);
				} else if (gdm_address_is_loopback (clnt_sa)) {
					/* woohoo! fun, I have no clue how to get
					 * the correct ip, SO I just send forward
					 * queries with all the different IPs */
					const GList *list = gdm_address_peek_local_list ();

					while (list != NULL) {
						struct sockaddr_storage *saddr = list->data;

						if (! gdm_address_is_loopback (saddr)) {
							/* forward query to * chosen host */
							gdm_xdmcp_send_forward_query (id,
										      clnt_sa,
										      saddr,
										      &clnt_authlist);
						}

						list = list->next;
					}
				} else {
					/* or send forward query to chosen host */
					gdm_xdmcp_send_forward_query (id,
								      clnt_sa,
								      clnt_sa,
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

#define SIN(__s)   ((struct sockaddr_in *) __s)
#define SIN6(__s)  ((struct sockaddr_in6 *) __s)

static void
set_port_for_request (struct sockaddr_storage *ss,
		      ARRAY8                  *port)
{
	/* we depend on this being 2 elsewhere as well */
	port->length = 2;

	switch (ss->ss_family) {
	case AF_INET:
		port->data = (CARD8 *)g_memdup (&(SIN (ss)->sin_port), port->length);
		break;
	case AF_INET6:
		port->data = (CARD8 *)g_memdup (&(SIN6 (ss)->sin6_port), port->length);
		break;
	default:
		port->data = NULL;
		break;
	}
}

static void
set_address_for_request (struct sockaddr_storage *ss,
			 ARRAY8                  *address)
{

	switch (ss->ss_family) {
	case AF_INET:
		address->length = sizeof (struct in_addr);
		address->data = g_memdup (&SIN (ss)->sin_addr, address->length);
		break;
	case AF_INET6:
		address->length = sizeof (struct in6_addr);
		address->data = g_memdup (&SIN6 (ss)->sin6_addr, address->length);
		break;
	default:
		address->length = 0;
		address->data = NULL;
		break;
	}

}

static void
gdm_xdmcp_send_forward_query (GdmIndirectDisplay      *id,
			      struct sockaddr_storage *clnt_sa,
			      struct sockaddr_storage *display_addr,
			      ARRAYofARRAY8Ptr         authlist)
{
	struct sockaddr_storage *sa;
	XdmcpHeader header;
	int i, authlen;
	ARRAY8 address;
	ARRAY8 port;
	char *host;
	char *serv;

	gdm_assert (id != NULL);
	gdm_assert (id->chosen_host != NULL);

	gdm_address_get_info (id->chosen_host, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_forward_query: Sending forward query to %s",
		   host);
	g_free (host);

	gdm_address_get_info (display_addr, &host, &serv);
	gdm_debug ("gdm_xdmcp_send_forward_query: Query contains %s:%s",
		   host, serv);
	g_free (host);
	g_free (serv);

	authlen = 1;
	for (i = 0 ; i < authlist->length ; i++) {
		authlen += 2 + authlist->data[i].length;
	}

	set_port_for_request (clnt_sa, &port);
	set_address_for_request (display_addr, &address);

	sa = g_memdup (id->chosen_host, sizeof (id->chosen_host));

	header.opcode = (CARD16) FORWARD_QUERY;
	header.length = authlen;
	header.length += 2 + address.length;
	header.length += 2 + port.length;
	header.version = XDM_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpWriteARRAY8 (&buf, &port);
	XdmcpWriteARRAYofARRAY8 (&buf, authlist);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr) sa,
		    (int)sizeof (struct sockaddr_storage));

	g_free (port.data);
	g_free (address.data);
	g_free (sa);
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
gdm_forward_query_alloc (struct sockaddr_storage *mgr_sa,
			 struct sockaddr_storage *dsp_sa)
{
	GdmForwardQuery *q;
	int count;

	count = g_slist_length (forward_queries);

	while (count > GDM_MAX_FORWARD_QUERIES && remove_oldest_forward ())
		count--;

	q = g_new0 (GdmForwardQuery, 1);
	q->dsp_sa = g_memdup (dsp_sa, sizeof (struct sockaddr_storage));
	q->from_sa = g_memdup (mgr_sa, sizeof (struct sockaddr_storage));

	forward_queries = g_slist_prepend (forward_queries, q);

	return q;
}

static GdmForwardQuery *
gdm_forward_query_lookup (struct sockaddr_storage *clnt_sa)
{
	GSList *li, *qlist;
	GdmForwardQuery *q;
	time_t curtime = time (NULL);

	qlist = g_slist_copy (forward_queries);

	for (li = qlist; li != NULL; li = li->next) {
		q = (GdmForwardQuery *) li->data;
		if (q == NULL)
			continue;

		if (gdm_address_equal (q->dsp_sa, clnt_sa)) {
			g_slist_free (qlist);
			return q;
		}

		if (q->acctime > 0 &&  curtime > q->acctime + GDM_FORWARD_QUERY_TIMEOUT) {
			char *host;
			char *serv;

			gdm_address_get_info (q->dsp_sa, &host, &serv);

			gdm_debug ("gdm_forward_query_lookup: Disposing stale forward query from %s:%s",
				   host, serv);
			g_free (host);
			g_free (serv);

			gdm_forward_query_dispose (q);
			continue;
		}
	}

	g_slist_free (qlist);

	{
		char *host;

		gdm_address_get_info (clnt_sa, &host, NULL);
		gdm_debug ("gdm_forward_query_lookup: Host %s not found",
			   host);
		g_free (host);
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

	{
		char *host;

		gdm_address_get_info (q->dsp_sa, &host, NULL);
		gdm_debug ("gdm_forward_query_dispose: Disposing %s", host);
		g_free (host);
	}

	g_free (q->dsp_sa);
	q->dsp_sa = NULL;
	g_free (q->from_sa);
	q->from_sa = NULL;

	g_free (q);
}

static gboolean
create_sa_from_request (ARRAY8 *req_addr,
			ARRAY8 *req_port,
			int    family,
			struct sockaddr_storage **sap)
{
	uint16_t         port;
	char             host_buf [NI_MAXHOST];
	char             serv_buf [NI_MAXSERV];
	char            *serv;
	const char      *host;
	struct addrinfo  hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai;
	int              gaierr;
	gboolean         found;

	if (sap != NULL) {
		*sap = NULL;
	}

	if (req_addr == NULL) {
		return FALSE;
	}

	serv = NULL;
	if (req_port != NULL) {
		/* port must always be length 2 */
		if (req_port->length != 2) {
			return FALSE;
		}

		memcpy (&port, req_port->data, 2);
		snprintf (serv_buf, sizeof (serv_buf), "%d", ntohs (port));
		serv = serv_buf;
	} else {
		/* assume XDM_UDP_PORT */
		snprintf (serv_buf, sizeof (serv_buf), "%d", XDM_UDP_PORT);
		serv = serv_buf;
	}

	host = NULL;
	if (req_addr->length == 4) {
		host = inet_ntop (AF_INET,
				  (const void *)req_addr->data,
				  host_buf,
				  sizeof (host_buf));
	} else if (req_addr->length == 16) {
		host = inet_ntop (AF_INET6,
				  (const void *)req_addr->data,
				  host_buf,
				  sizeof (host_buf));
	}

	if (host == NULL) {
		gdm_error (_("Bad address"));
		return FALSE;
	}

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = family;
	hints.ai_flags = AI_V4MAPPED; /* this should convert IPv4 address to IPv6 if needed */
	if ((gaierr = getaddrinfo (host, serv, &hints, &ai_list)) != 0) {
		g_warning ("Unable get address: %s", gai_strerror (gaierr));
		return FALSE;
	}

	/* just take the first one */
	ai = ai_list;

	found = FALSE;
	if (ai != NULL) {
		found = TRUE;
		if (sap != NULL) {
			*sap = g_memdup (ai->ai_addr, ai->ai_addrlen);
		}
	}

	freeaddrinfo (ai_list);

	return found;
}

static void
gdm_xdmcp_handle_forward_query (struct sockaddr_storage *clnt_sa,
				gint len)
{
	ARRAY8 clnt_addr;
	ARRAY8 clnt_port;
	ARRAYofARRAY8 clnt_authlist;
	gint i = 0;
	gint explen = 1;
	struct sockaddr_storage *disp_sa;
	char *host;
	char *serv;

	disp_sa = NULL;

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		char *host;

		gdm_address_get_info (clnt_sa, &host, NULL);

		gdm_error ("%s: Got FORWARD_QUERY from banned host %s",
			   "gdm_xdmcp_handle_forward query",
			   host);
		g_free (host);
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
	explen += 2 + clnt_addr.length;
	explen += 2 + clnt_port.length;

	for (i = 0 ; i < clnt_authlist.length ; i++) {
		char *s = g_strndup ((char *) clnt_authlist.data[i].data,
				     clnt_authlist.length);
		gdm_debug ("gdm_xdmcp_handle_forward_query: authlist: %s",
			   ve_sure_string (s));
		g_free (s);

		explen += 2 + clnt_authlist.data[i].length;
	}

	if G_UNLIKELY (len != explen) {
		gdm_error (_("%s: Error in checksum"),
			   "gdm_xdmcp_handle_forward_query");
		goto out;
	}

	if (! create_sa_from_request (&clnt_addr, &clnt_port, clnt_sa->ss_family, &disp_sa)) {
		gdm_error ("Unable to parse address for request");
		goto out;
	}

	gdm_xdmcp_whack_queued_managed_forwards (clnt_sa,
						 disp_sa);

	gdm_address_get_info (disp_sa, &host, &serv);
	gdm_debug ("gdm_xdmcp_handle_forward_query: Got FORWARD_QUERY for display: %s, port %s",
		   host, serv);
	g_free (host);
	g_free (serv);

	/* Check with tcp_wrappers if display is allowed to access */
	if (gdm_xdmcp_host_allow (disp_sa)) {
		GdmForwardQuery *q;

		q = gdm_forward_query_lookup (disp_sa);
		if (q != NULL)
			gdm_forward_query_dispose (q);
		gdm_forward_query_alloc (clnt_sa, disp_sa);

		gdm_xdmcp_send_willing (disp_sa);
	}

 out:

	g_free (disp_sa);
	XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
	XdmcpDisposeARRAY8 (&clnt_port);
	XdmcpDisposeARRAY8 (&clnt_addr);
}

static void
gdm_xdmcp_send_willing (struct sockaddr_storage *clnt_sa)
{
	ARRAY8 status;
	XdmcpHeader header;
	static char *last_status = NULL;
	static time_t last_willing = 0;
	char *bin;
	FILE *fd;
	const char *willing = gdm_daemon_config_get_value_string (GDM_KEY_WILLING);
	int dispperhost = gdm_daemon_config_get_value_int (GDM_KEY_DISPLAYS_PER_HOST);
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_willing: Sending WILLING to %s", host);
	g_free (host);

	if (last_willing == 0 ||
	    time (NULL) - 3 > last_willing) {
		char statusBuf[256] = "";
		bin = ve_first_word (willing);

		if ( ! ve_string_empty (bin) &&
		     g_access (bin, X_OK) == 0 &&
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

	if (! gdm_address_is_local (clnt_sa) &&
	    gdm_xdmcp_displays_from_host (clnt_sa) >= dispperhost) {
		/*
		 * Don't translate, this goes over the wire to servers where we
		 * don't know the charset or language, so it must be ascii
		 */
		status.data = (CARD8 *) g_strdup_printf ("%s (Server is busy)",
							 last_status);
	} else {
		status.data = (CARD8 *) g_strdup (last_status);
	}

	status.length = strlen ((char *) status.data);

	header.opcode   = (CARD16) WILLING;
	header.length   = 6 + serv_authlist.authentication.length;
	header.length  += servhost.length + status.length;
	header.version  = XDM_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	/* Hardcoded authentication */
	XdmcpWriteARRAY8 (&buf, &serv_authlist.authentication);
	XdmcpWriteARRAY8 (&buf, &servhost);
	XdmcpWriteARRAY8 (&buf, &status);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	g_free (status.data);
}

static void
gdm_xdmcp_send_unwilling (struct sockaddr_storage *clnt_sa, gint type)
{
	ARRAY8 status;
	XdmcpHeader header;
	static time_t last_time = 0;
	char *host;

	/* only send at most one packet per second,
	   no harm done if we don't send it at all */
	if (last_time + 1 >= time (NULL))
		return;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_unwilling: Sending UNWILLING to %s", host);
	gdm_error (_("Denied XDMCP query from host %s"), host);
	g_free (host);

	/*
	 * Don't translate, this goes over the wire to servers where we
	 * don't know the charset or language, so it must be ascii
	 */
	status.data = (CARD8 *) "Display not authorized to connect";
	status.length = strlen ((char *) status.data);

	header.opcode = (CARD16) UNWILLING;
	header.length = 4 + servhost.length + status.length;
	header.version = XDM_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &servhost);
	XdmcpWriteARRAY8 (&buf, &status);
	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	last_time = time (NULL);
}

static void
gdm_xdmcp_really_send_managed_forward (struct sockaddr_storage *clnt_sa,
				       struct sockaddr_storage *origin)
{
	ARRAY8 address;
	XdmcpHeader header;
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_really_send_managed_forward: "
		   "Sending MANAGED_FORWARD to %s",
		   host);
	g_free (host);

	set_address_for_request (origin, &address);

	header.opcode = (CARD16) GDM_XDMCP_MANAGED_FORWARD;
	header.length = 4 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	g_free (address.data);
}

static gboolean
managed_forward_handler (ManagedForward *mf)
{
	if (gdm_xdmcpfd > 0)
		gdm_xdmcp_really_send_managed_forward (&(mf->manager),
						       &(mf->origin));
	mf->times++;
	if (gdm_xdmcpfd <= 0 || mf->times >= 2) {
		managed_forwards = g_slist_remove (managed_forwards, mf);
		mf->handler = 0;
		/* mf freed by glib */
		return FALSE;
	}
	return TRUE;
}

static void
gdm_xdmcp_send_managed_forward (struct sockaddr_storage *clnt_sa,
				struct sockaddr_storage *origin)
{
	ManagedForward *mf;

	gdm_xdmcp_really_send_managed_forward (clnt_sa, origin);

	mf = g_new0 (ManagedForward, 1);
	mf->times = 0;

	memcpy (&(mf->manager), clnt_sa, sizeof (struct sockaddr_storage));
	memcpy (&(mf->origin), origin, sizeof (struct sockaddr_storage));

	mf->handler = g_timeout_add_full (G_PRIORITY_DEFAULT,
					  MANAGED_FORWARD_INTERVAL,
					  (GSourceFunc)managed_forward_handler,
					  mf,
					  (GDestroyNotify) g_free);
	managed_forwards = g_slist_prepend (managed_forwards, mf);
}

static void
gdm_xdmcp_send_got_managed_forward (struct sockaddr_storage *clnt_sa,
				    struct sockaddr_storage *origin)
{
	ARRAY8 address;
	XdmcpHeader header;
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_managed_forward: "
		   "Sending GOT_MANAGED_FORWARD to %s",
		   host);
	g_free (host);

	set_address_for_request (origin, &address);

	header.opcode = (CARD16) GDM_XDMCP_GOT_MANAGED_FORWARD;
	header.length = 4 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));
}

static void
gdm_xdmcp_handle_request (struct sockaddr_storage *clnt_sa,
			  gint len)
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
	int maxsessions  = gdm_daemon_config_get_value_int (GDM_KEY_MAX_SESSIONS);
	int maxpending   = gdm_daemon_config_get_value_int (GDM_KEY_MAX_PENDING);
	int dispperhost  = gdm_daemon_config_get_value_int (GDM_KEY_DISPLAYS_PER_HOST);
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_handle_request: Got REQUEST from %s", host);

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error (_("%s: Got REQUEST from banned host %s"),
			   "gdm_xdmcp_handle_request",
			   host);
		g_free (host);
		return;
	}
	g_free (host);

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
		    strncmp ((char *) clnt_authorization.data[i].data,
			     "MIT-MAGIC-COOKIE-1", 18) == 0)
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
	explen += 1 + 2 * clnt_conntyp.length; /* Connection Type */
	explen += 1;		    /* Connection Address */
	for (i = 0 ; i < clnt_addr.length ; i++)
		explen += 2 + clnt_addr.data[i].length;
	explen += 2 + clnt_authname.length; /* Authentication Name */
	explen += 2 + clnt_authdata.length; /* Authentication Data */
	explen += 1;		    /* Authorization Names */
	for (i = 0 ; i < clnt_authorization.length ; i++)
		explen += 2 + clnt_authorization.data[i].length;
	explen += 2 + clnt_manufacturer.length;

	if G_UNLIKELY (explen != len) {
		gdm_address_get_info (clnt_sa, &host, NULL);
		gdm_error (_("%s: Failed checksum from %s"),
			   "gdm_xdmcp_handle_request",
			   host);
		g_free (host);

		XdmcpDisposeARRAY8 (&clnt_authname);
		XdmcpDisposeARRAY8 (&clnt_authdata);
		XdmcpDisposeARRAY8 (&clnt_manufacturer);
		XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
		XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
		XdmcpDisposeARRAY16 (&clnt_conntyp);
		return;
	}

	{
		char *s = g_strndup ((char *) clnt_manufacturer.data, clnt_manufacturer.length);
		gdm_debug ("gdm_xdmcp_handle_request: xdmcp_pending=%d, MaxPending=%d, xdmcp_sessions=%d, MaxSessions=%d, ManufacturerID=%s",
			   xdmcp_pending, maxpending, xdmcp_sessions,
			   maxsessions, ve_sure_string (s));
		g_free (s);
	}

	/* Check if ok to manage display */
	if (mitauth &&
	    xdmcp_sessions < maxsessions &&
	    (gdm_address_is_local (clnt_sa) ||
	     gdm_xdmcp_displays_from_host (clnt_sa) < dispperhost)) {
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
			gdm_xdmcp_send_decline (clnt_sa,
						"Maximum number of open sessions reached");
		} else {
			gdm_info ("Maximum number of open XDMCP sessions from host %s reached",
				  host);
			gdm_xdmcp_send_decline (clnt_sa,
						"Maximum number of open sessions from your host reached");
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
gdm_xdmcp_send_accept (GdmHostent              *he /* eaten and freed */,
		       struct sockaddr_storage *clnt_sa,
		       gint                     displaynum)
{
	XdmcpHeader header;
	ARRAY8 authentype;
	ARRAY8 authendata;
	ARRAY8 authname;
	ARRAY8 authdata;
	GdmDisplay *d;
	char *host;

	d = gdm_xdmcp_display_alloc (clnt_sa,
				     he /* eaten and freed */,
				     displaynum);

	authentype.data   = (CARD8 *) 0;
	authentype.length = (CARD16)  0;

	authendata.data   = (CARD8 *) 0;
	authendata.length = (CARD16)  0;

	authname.data     = (CARD8 *) "MIT-MAGIC-COOKIE-1";
	authname.length   = strlen ((char *) authname.data);

	authdata.data     = (CARD8 *) d->bcookie;
	authdata.length   = 16;

	header.version    = XDM_PROTOCOL_VERSION;
	header.opcode     = (CARD16) ACCEPT;
	header.length     = 4;
	header.length    += 2 + authentype.length;
	header.length    += 2 + authendata.length;
	header.length    += 2 + authname.length;
	header.length    += 2 + authdata.length;

	XdmcpWriteHeader (&buf, &header);
	XdmcpWriteCARD32 (&buf, d->sessionid);
	XdmcpWriteARRAY8 (&buf, &authentype);
	XdmcpWriteARRAY8 (&buf, &authendata);
	XdmcpWriteARRAY8 (&buf, &authname);
	XdmcpWriteARRAY8 (&buf, &authdata);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_accept: Sending ACCEPT to %s with SessionID=%ld",
		   host,
		   (long)d->sessionid);
	g_free (host);
}

static void
gdm_xdmcp_send_decline (struct sockaddr_storage *clnt_sa,
			const char *reason)
{
	XdmcpHeader header;
	ARRAY8 authentype;
	ARRAY8 authendata;
	ARRAY8 status;
	GdmForwardQuery *fq;
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_send_decline: Sending DECLINE to %s",
		   host);
	g_free (host);

	authentype.data   = (CARD8 *) 0;
	authentype.length = (CARD16)  0;

	authendata.data   = (CARD8 *) 0;
	authendata.length = (CARD16)  0;

	status.data       = (CARD8 *) reason;
	status.length     = strlen ((char *) status.data);

	header.version    = XDM_PROTOCOL_VERSION;
	header.opcode     = (CARD16) DECLINE;
	header.length     = 2 + status.length;
	header.length    += 2 + authentype.length;
	header.length    += 2 + authendata.length;

	XdmcpWriteHeader (&buf, &header);
	XdmcpWriteARRAY8 (&buf, &status);
	XdmcpWriteARRAY8 (&buf, &authentype);
	XdmcpWriteARRAY8 (&buf, &authendata);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	/* Send MANAGED_FORWARD to indicate that the connection
	 * reached some sort of resolution */
	fq = gdm_forward_query_lookup (clnt_sa);
	if (fq != NULL) {
		gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
		gdm_forward_query_dispose (fq);
	}
}

static void
gdm_xdmcp_handle_manage (struct sockaddr_storage *clnt_sa,
			 gint len)
{
	CARD32 clnt_sessid;
	CARD16 clnt_dspnum;
	ARRAY8 clnt_dspclass;
	GdmDisplay *d;
	GdmIndirectDisplay *id;
	GdmForwardQuery *fq;
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_handle_manage: Got MANAGE from %s", host);

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error (_("%s: Got Manage from banned host %s"),
			   "gdm_xdmcp_handle_manage",
			   host);
		g_free (host);
		return;
	}
	g_free (host);

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

	if G_UNLIKELY (gdm_daemon_config_get_value_bool (GDM_KEY_DEBUG)) {
		char *s = g_strndup ((char *) clnt_dspclass.data, clnt_dspclass.length);
		gdm_debug ("gdm_xdmcp-handle_manage: Got display=%d, SessionID=%ld Class=%s from %s",
			   (int)clnt_dspnum, (long)clnt_sessid, ve_sure_string (s), host);

		g_free (s);
	}

	d = gdm_xdmcp_display_lookup (clnt_sessid);
	if (d != NULL &&
	    d->dispstat == XDMCP_PENDING) {

		gdm_debug ("gdm_xdmcp_handle_manage: Looked up %s", d->name);

		if (gdm_daemon_config_get_value_bool (GDM_KEY_INDIRECT)) {
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
gdm_xdmcp_handle_managed_forward (struct sockaddr_storage *clnt_sa,
				  gint len)
{
	ARRAY8 clnt_address;
	GdmIndirectDisplay *id;
	char *host;
	struct sockaddr_storage *disp_sa;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_handle_managed_forward: Got MANAGED_FORWARD from %s",
		   host);

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error ("%s: Got MANAGED_FORWARD from banned host %s",
			   "gdm_xdmcp_handle_request", host);
		g_free (host);
		return;
	}
	g_free (host);

	/* Hostname */
	if G_UNLIKELY ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_managed_forward");
		return;
	}

	disp_sa = NULL;
	if (! create_sa_from_request (&clnt_address, NULL, clnt_sa->ss_family, &disp_sa)) {
		gdm_error ("Unable to parse address for request");
		XdmcpDisposeARRAY8 (&clnt_address);
		return;
	}

	id = gdm_choose_indirect_lookup_by_chosen (clnt_sa, disp_sa);
	if (id != NULL) {
		gdm_choose_indirect_dispose (id);
	}

	/* Note: we send GOT even on not found, just in case our previous
	 * didn't get through and this was a second managed forward */
	gdm_xdmcp_send_got_managed_forward (clnt_sa, disp_sa);

	XdmcpDisposeARRAY8 (&clnt_address);
}

static void
gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_storage *clnt_sa,
					 struct sockaddr_storage *origin)
{
	GSList *li;

	for (li = managed_forwards; li != NULL; li = li->next) {
		ManagedForward *mf = li->data;

		if (gdm_address_equal (&mf->manager, clnt_sa) &&
		    gdm_address_equal (&mf->origin, origin)) {
			managed_forwards = g_slist_remove_link (managed_forwards, li);
			g_slist_free_1 (li);
			g_source_remove (mf->handler);
			/* mf freed by glib */
			return;
		}
	}
}

static void
gdm_xdmcp_handle_got_managed_forward (struct sockaddr_storage *clnt_sa,
				      gint len)
{
	struct sockaddr_storage *disp_sa;
	ARRAY8 clnt_address;
	char  *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_handle_got_managed_forward: Got MANAGED_FORWARD from %s",
		   host);

	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error ("%s: Got GOT_MANAGED_FORWARD from banned host %s",
			   "gdm_xdmcp_handle_request", host);
		g_free (host);
		return;
	}
	g_free (host);

	/* Hostname */
	if G_UNLIKELY ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_got_managed_forward");
		return;
	}

	if (! create_sa_from_request (&clnt_address, NULL, clnt_sa->ss_family, &disp_sa)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_got_managed_forward");
		XdmcpDisposeARRAY8 (&clnt_address);
		return;
	}

	gdm_xdmcp_whack_queued_managed_forwards (clnt_sa, disp_sa);

	XdmcpDisposeARRAY8 (&clnt_address);
}

static void
gdm_xdmcp_send_refuse (struct sockaddr_storage *clnt_sa,
		       CARD32 sessid)
{
	XdmcpHeader header;
	GdmForwardQuery *fq;

	gdm_debug ("gdm_xdmcp_send_refuse: Sending REFUSE to %ld",
		   (long)sessid);

	header.version = XDM_PROTOCOL_VERSION;
	header.opcode  = (CARD16) REFUSE;
	header.length  = 4;

	XdmcpWriteHeader (&buf, &header);
	XdmcpWriteCARD32 (&buf, sessid);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));

	/*
	 * This was from a forwarded query quite apparently so
	 * send MANAGED_FORWARD
	 */
	fq = gdm_forward_query_lookup (clnt_sa);
	if (fq != NULL) {
		gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
		gdm_forward_query_dispose (fq);
	}
}

static void
gdm_xdmcp_send_failed (struct sockaddr_storage *clnt_sa, CARD32 sessid)
{
	XdmcpHeader header;
	ARRAY8 status;

	gdm_debug ("gdm_xdmcp_send_failed: Sending FAILED to %ld", (long)sessid);

	/*
	 * Don't translate, this goes over the wire to servers where we
	 * don't know the charset or language, so it must be ascii
	 */
	status.data    = (CARD8 *) "Failed to start session";
	status.length  = strlen ((char *) status.data);

	header.version = XDM_PROTOCOL_VERSION;
	header.opcode  = (CARD16) FAILED;
	header.length  = 6+status.length;

	XdmcpWriteHeader (&buf, &header);
	XdmcpWriteCARD32 (&buf, sessid);
	XdmcpWriteARRAY8 (&buf, &status);

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));
}

static void
gdm_xdmcp_handle_keepalive (struct sockaddr_storage *clnt_sa,
			    gint len)
{
	CARD16 clnt_dspnum;
	CARD32 clnt_sessid;
	char *host;

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_xdmcp_handle_keepalive: Got KEEPALIVE from %s",
		   host);

	/* Check with tcp_wrappers if client is allowed to access */
	if (! gdm_xdmcp_host_allow (clnt_sa)) {
		gdm_error (_("%s: Got KEEPALIVE from banned host %s"),
			   "gdm_xdmcp_handle_keepalive",
			   host);
		g_free (host);
		return;
	}
	g_free (host);

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
gdm_xdmcp_send_alive (struct sockaddr_storage *clnt_sa,
                      CARD16 dspnum,
		      CARD32 sessid)
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

	XdmcpFlush (gdm_xdmcpfd,
		    &buf,
		    (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_storage));
}

static gboolean
gdm_xdmcp_host_allow (struct sockaddr_storage *clnt_sa)
{
#ifdef HAVE_TCPWRAPPERS

	/*
	 * Avoids a warning, my tcpd.h file doesn't include this prototype, even
	 * though the library does include the function and the manpage mentions it
	 */
	extern int hosts_ctl (char *daemon,
			      char *client_name,
			      char *client_addr,
			      char *client_user);

	GdmHostent *client_he;
	char *client;
	gboolean ret;
	char *host;

	/* Find client hostname */
	client_he = gdm_gethostbyaddr (clnt_sa);

	if (client_he->not_found) {
		client = "unknown";
	} else {
		gdm_debug ("gdm_xdmcp_host_allow: client->hostname is %s\n",
                           client_he->hostname);
		client = client_he->hostname;
	}

	/* Check with tcp_wrappers if client is allowed to access */
	host = NULL;
	gdm_address_get_info (clnt_sa, &host, NULL);
	ret = hosts_ctl ("gdm", client, host, "");
	g_free (host);

	gdm_hostent_free (client_he);

	return ret;
#else /* HAVE_TCPWRAPPERS */
	return (TRUE);
#endif /* HAVE_TCPWRAPPERS */
}

static GdmDisplay *
gdm_xdmcp_display_alloc (struct sockaddr_storage *addr,
			 GdmHostent *he /* eaten and freed */,
			 int displaynum)
{
	GdmDisplay *d  = NULL;
	const char *proxycmd = gdm_daemon_config_get_value_string (GDM_KEY_XDMCP_PROXY_XSERVER);

	d = g_new0 (GdmDisplay, 1);

	if (gdm_daemon_config_get_value_bool (GDM_KEY_XDMCP_PROXY) && proxycmd != NULL) {
		d->type = TYPE_XDMCP_PROXY;
		d->command = g_strdup (proxycmd);
		gdm_debug ("Using proxy server for XDMCP: %s\n", d->command);
	} else {
		d->type = TYPE_XDMCP;
	}

	d->logout_action    = GDM_LOGOUT_ACTION_NONE;
	d->authfile         = NULL;
	d->auths            = NULL;
	d->userauth         = NULL;
	d->greetpid         = 0;
	d->servpid          = 0;
	d->servstat         = 0;
	d->sesspid          = 0;
	d->slavepid         = 0;
	d->attached         = FALSE;
	d->dispstat         = XDMCP_PENDING;
	d->sessionid        = globsessid++;

	if (d->sessionid == 0) {
		d->sessionid = globsessid++;
	}

	d->acctime          = time (NULL);
	d->dispnum          = displaynum;
	d->xdmcp_dispnum    = displaynum;

	d->handled          = TRUE;
	d->tcp_disallowed   = FALSE;
	d->vt               = -1;
	d->x_servers_order  = -1;
	d->logged_in        = FALSE;
	d->login            = NULL;
	d->sleep_before_run = 0;

	if (gdm_daemon_config_get_value_bool (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN) &&
	    ! ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_TIMED_LOGIN))) {
		d->timed_login_ok = TRUE;
	} else {
		d->timed_login_ok = FALSE;
	}

	d->name = g_strdup_printf ("%s:%d",
				   he->hostname,
				   displaynum);

	memcpy (&d->addr, addr, sizeof (struct sockaddr_storage));

	d->hostname              = he->hostname;
	he->hostname             = NULL;
	d->addrs                 = he->addrs;
	he->addrs                = NULL;
	d->addr_count            = he->addr_count;
	he->addr_count           = 0;

	gdm_hostent_free (he);

	d->slave_notify_fd       = -1;
	d->master_notify_fd      = -1;
	d->xsession_errors_bytes = 0;
	d->xsession_errors_fd    = -1;
	d->session_output_fd     = -1;
	d->chooser_output_fd     = -1;
	d->chooser_last_line     = NULL;
	d->theme_name            = NULL;

	/* Secure display with cookie */
	if G_UNLIKELY (! gdm_auth_secure_display (d)) {
		gdm_error ("gdm_xdmcp_display_alloc: Error setting up cookies for %s",
			   d->name);
	}

	if (d->type == TYPE_XDMCP_PROXY) {
		d->parent_disp      = d->name;
		d->name             = g_strdup (":-1");
		d->dispnum          = -1;
		d->server_uid       = gdm_daemon_config_get_gdmuid ();
		d->parent_auth_file = d->authfile;
		d->authfile         = NULL;
	}

	gdm_daemon_config_display_list_append (d);

	xdmcp_pending++;

	gdm_debug ("gdm_xdmcp_display_alloc: display=%s, session id=%ld, xdmcp_pending=%d",
		   d->name, (long)d->sessionid, xdmcp_pending);

	return d;
}

static GdmDisplay *
gdm_xdmcp_display_lookup (CARD32 sessid)
{
	GSList *dlist = gdm_daemon_config_get_display_list ();
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
        GSList *displays;

        displays = gdm_daemon_config_get_display_list ();

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
        GSList *displays;

        displays = gdm_daemon_config_get_display_list ();

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    SERVER_IS_XDMCP (d) &&
		    d->dispstat == XDMCP_PENDING &&
		    curtime > d->acctime + gdm_daemon_config_get_value_int (GDM_KEY_MAX_WAIT)) {
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
	const gchar *proxyreconnect = gdm_daemon_config_get_value_string (GDM_KEY_XDMCP_PROXY_RECONNECT);

	command_argv[0] = (char *)proxyreconnect;
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

void
gdm_xdmcp_recount_sessions (void)
{
	GSList *li;
	GSList *displays;

	displays = gdm_daemon_config_get_display_list ();

	xdmcp_sessions = 0;
	xdmcp_pending = 0;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;
		if (SERVER_IS_XDMCP (d)) {
			if (d->dispstat == XDMCP_MANAGED)
				xdmcp_sessions++;
			else if (d->dispstat == XDMCP_PENDING)
				xdmcp_pending++;
		}
	}
}
