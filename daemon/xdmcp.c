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
#include <gnome.h>

#ifdef HAVE_LIBXDMCP
#include <stdio.h>
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

#ifdef HAVE_TCPWRAPPERS
  #include <tcpd.h>
#endif

#include <vicious.h>

#include "gdm.h"
#include "display.h"
#include "auth.h"
#endif /* HAVE_LIBXDMCP */

#include "misc.h"
#include "choose.h"
#include "xdmcp.h"

gint pending = 0;

#ifdef HAVE_LIBXDMCP

/* TCP Wrapper syslog control */
gint allow_severity = LOG_INFO;
gint deny_severity = LOG_WARNING;

static gint gdm_xdmcpfd = -1;
static guint xdmcp_source = 0;
static gint globsessid;
static gchar *sysid;
static ARRAY8 servhost;
static XdmcpBuffer buf;

extern GSList *displays;
extern gint sessions;
extern gchar *GdmLogDir;
extern gchar *GdmServAuthDir;

/* Tunables */
extern gint GdmMaxPending;	/* only accept this number of pending sessions */
extern gint GdmMaxManageWait;	/* Dispose sessions not responding with MANAGE after 10 secs */
extern gint GdmMaxSessions;	/* Maximum number of remote sessions */
extern gint GdmPort;		/* UDP port number */
extern gboolean GdmIndirect;	/* Honor XDMCP_INDIRECT, i.e. choosing */
extern gint GdmMaxIndirectWait;	/* Max wait between INDIRECT_QUERY and MANAGE */
extern gint GdmDispPerHost;	/* Max number of displays per remote host */
extern gchar *GdmTimedLogin;
extern gboolean GdmAllowRemoteAutoLogin;
extern gchar *GdmWilling;	/* The willing script */

extern gboolean GdmXdmcp;	/* xdmcp enabled */

/* Local prototypes */
static gboolean gdm_xdmcp_decode_packet (GIOChannel *source,
					 GIOCondition cond,
					 gpointer data);
static void gdm_xdmcp_handle_query (struct sockaddr_in *clnt_sa,
				    gint len,
				    gint type);
static void gdm_xdmcp_send_forward_query (GdmIndirectDisplay *id,
					  struct sockaddr_in *clnt_sa,
					  struct in_addr *display_addr,
					  ARRAYofARRAY8Ptr authlist);
static void gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_request (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_managed_forward (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_got_managed_forward (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_keepalive (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_in *clnt_sa,
						     struct in_addr *origin);
static void gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa);
static void gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type);
static void gdm_xdmcp_send_accept (const char *hostname, struct sockaddr_in *clnt_sa, gint displaynum);
static void gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa, const char *reason);
static void gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_alive (struct sockaddr_in *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_managed_forward (struct sockaddr_in *clnt_sa,
					    struct sockaddr_in *origin);
static void gdm_xdmcp_send_got_managed_forward (struct sockaddr_in *clnt_sa,
						struct in_addr *origin);
static gboolean gdm_xdmcp_host_allow (struct sockaddr_in *cnlt_sa);
static GdmDisplay *gdm_xdmcp_display_alloc (struct in_addr *addr, const char *hostname, gint);
static GdmDisplay *gdm_xdmcp_display_lookup (CARD32 sessid);
static void gdm_xdmcp_display_dispose_check (const gchar *name);
static void gdm_xdmcp_displays_check (void);
static int gdm_xdmcp_displays_from_host (struct in_addr *addr);

static GdmForwardQuery * gdm_forward_query_alloc (struct sockaddr_in *mgr_sa,
						  struct sockaddr_in *dsp_sa);
static GdmForwardQuery * gdm_forward_query_lookup (struct sockaddr_in *clnt_sa);
static void gdm_forward_query_dispose (GdmForwardQuery *q);

static GSList *forward_queries = NULL;

typedef struct {
	int times;
	guint handler;
	struct sockaddr_in manager; 
	struct sockaddr_in origin;
} ManagedForward;
#define MANAGED_FORWARD_INTERVAL 1500 /* 1.5 seconds */

static GList *managed_forwards = NULL;


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

static int
gdm_xdmcp_displays_from_host (struct in_addr *addr)
{
	GSList *li;
	int count = 0;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (disp->type == TYPE_XDMCP &&
		    memcmp (&disp->addr, addr, sizeof (struct in_addr)) == 0)
			count ++;
	}

	return count;
}


gboolean
gdm_xdmcp_init (void)
{
    struct sockaddr_in serv_sa = {0};
    gchar hostbuf[256];
    struct utsname name;
    
    globsessid = time (NULL);
    
    /* Fetch and store local hostname in XDMCP friendly format */
    if (gethostname (hostbuf, 255) != 0) {
	gdm_error (_("gdm_xdmcp_init: Could not get server hostname: %s!"), strerror (errno));
	GdmXdmcp = FALSE;
	return FALSE;
    }
    
    uname (&name);
    sysid = g_strconcat (name.sysname, " ", name.release, NULL);

    servhost.data = g_strdup (hostbuf);
    servhost.length = strlen (servhost.data);
    
    gdm_debug ("Start up on host %s, port %d", hostbuf, GdmPort);
    
    /* Open socket for communications */
    gdm_xdmcpfd = socket (AF_INET, SOCK_DGRAM, 0); /* UDP */
    
    if (gdm_xdmcpfd < 0) {
	gdm_error (_("gdm_xdmcp_init: Could not create socket!"));
	GdmXdmcp = FALSE;
	return FALSE;
    }
    
    serv_sa.sin_family = AF_INET;
    serv_sa.sin_port = htons (GdmPort); /* UDP 177 */
    serv_sa.sin_addr.s_addr = htonl (INADDR_ANY);
    
    if (bind (gdm_xdmcpfd, (struct sockaddr*) &serv_sa, sizeof (serv_sa)) == -1) {
	gdm_error (_("gdm_xdmcp_init: Could not bind to XDMCP socket!"));
	gdm_xdmcp_close ();
	GdmXdmcp = FALSE;
	return FALSE;
    }

    return TRUE;
}


void
gdm_xdmcp_run (void)
{
	GIOChannel *xdmcpchan;

	xdmcpchan = g_io_channel_unix_new (gdm_xdmcpfd);
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
		close (gdm_xdmcpfd);
		gdm_xdmcpfd = -1;
	}
}


static gboolean
gdm_xdmcp_decode_packet (GIOChannel *source, GIOCondition cond, gpointer data)
{
    struct sockaddr_in clnt_sa;
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
    
    if (!XdmcpFill (gdm_xdmcpfd, &buf, (XdmcpNetaddr)&clnt_sa, &sa_len)) {
	gdm_error (_("gdm_xdmcp_decode: Could not create XDMCP buffer!"));
	return TRUE;
    }
    
    if (!XdmcpReadHeader (&buf, &header)) {
	gdm_error (_("gdm_xdmcp_decode: Could not read XDMCP header!"));
	return TRUE;
    }
    
    if (header.version != XDM_PROTOCOL_VERSION &&
	header.version != GDM_XDMCP_PROTOCOL_VERSION) {
	gdm_error (_("gdm_xdmcp_decode: Incorrect XDMCP version!"));
	return TRUE;
    }

    if (header.opcode <= ALIVE)
	    gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", 
		       opcode_names[header.opcode], inet_ntoa (clnt_sa.sin_addr));
    if (header.opcode >= GDM_XDMCP_FIRST_OPCODE &&
        header.opcode < GDM_XDMCP_LAST_OPCODE)
	    gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", 
		       gdm_opcode_names[header.opcode - GDM_XDMCP_FIRST_OPCODE],
		       inet_ntoa (clnt_sa.sin_addr));

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
	gdm_xdmcp_displays_check(); /* Purge pending displays */
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
	gdm_error (_("gdm_xdmcp_decode_packet: Unknown opcode from host %s"),
		   inet_ntoa (clnt_sa.sin_addr));
	break;
    }

    return TRUE;
}

static void 
gdm_xdmcp_handle_query (struct sockaddr_in *clnt_sa, gint len, gint type)
{
    ARRAYofARRAY8 clnt_authlist;
    gint i = 0, explen = 1;

    gdm_debug ("gdm_xdmcp_handle_query: Opcode %d from %s", 
	       type, inet_ntoa (clnt_sa->sin_addr));
    
    /* Extract array of authentication names from Xdmcp packet */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
	gdm_error (_("gdm_xdmcp_handle_query: Could not extract authlist from packet")); 
	return;
    }
    
    /* Crude checksumming */
    for (i = 0 ; i < clnt_authlist.length ; i++) {
	gdm_debug ("gdm_xdmcp_handle_query: authlist: %s",
		   (char *)clnt_authlist.data);
	explen += 2+clnt_authlist.data[i].length;
    }
    
    if (len != explen) {
	gdm_error (_("gdm_xdmcp_handle_query: Error in checksum")); 
	XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
	return;
    }

    /* Check with tcp_wrappers if client is allowed to access */
    if (gdm_xdmcp_host_allow (clnt_sa)) {

	/* If this is an INDIRECT_QUERY, try to look up the display in
 	 * the pending list. If found send a FORWARD_QUERY to the
 	 * chosen manager. Otherwise alloc a new indirect display. */

	if (GdmIndirect &&
	    type == INDIRECT_QUERY) {
		GdmIndirectDisplay *id = gdm_choose_indirect_lookup (clnt_sa);

		if (id != NULL &&
		    id->chosen_host != NULL) {
			/* if user chose us, then just send willing */
			if (gdm_is_local_addr (id->chosen_host)) {
				/* get rid of indirect, so that we don't get
				 * the chooser */
				gdm_choose_indirect_dispose (id);
				gdm_xdmcp_send_willing (clnt_sa);
			} else if (gdm_is_loopback_addr (&(clnt_sa->sin_addr))) {
				/* woohoo! fun, I have no clue how to get
				 * the correct ip, SO I just send forward
				 * queries with all the different IPs */
				const GList *list = gdm_peek_local_address_list ();

				while (list != NULL) {
					struct in_addr *addr = list->data;
					
					if ( ! gdm_is_loopback_addr (addr)) {
						/* forward query to
						 * chosen host */
						gdm_xdmcp_send_forward_query
							(id, clnt_sa, addr,
							 &clnt_authlist);
					}

					list = list->next;
				}
			} else {
				/* or send forward query to chosen host */
				gdm_xdmcp_send_forward_query
					(id, clnt_sa,
					 &(clnt_sa->sin_addr),
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

	g_assert (id != NULL);
	g_assert (id->chosen_host != NULL);

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
	port.data = g_new (char, 2);
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
gdm_forward_query_alloc (struct sockaddr_in *mgr_sa,
			 struct sockaddr_in *dsp_sa)
{
	GdmForwardQuery *q;
	int count;

	count = g_slist_length (forward_queries);

	while (count > GDM_MAX_FORWARD_QUERIES &&
	       remove_oldest_forward ())
		count --;

	q = g_new0 (GdmForwardQuery, 1);
	q->dsp_sa = g_new0 (struct sockaddr_in, 1);
	memcpy (q->dsp_sa, dsp_sa, sizeof (struct sockaddr_in));
	q->from_sa = g_new0 (struct sockaddr_in, 1);
	memcpy (q->from_sa, mgr_sa, sizeof (struct sockaddr_in));

	forward_queries = g_slist_prepend (forward_queries, q);

	return q;
}

static GdmForwardQuery *
gdm_forward_query_lookup (struct sockaddr_in *clnt_sa)
{
	GSList *li, *qlist;
	GdmForwardQuery *q;
	time_t curtime = time (NULL);

	qlist = g_slist_copy (forward_queries);

	for (li = qlist; li != NULL; li = li->next) {
		q = (GdmForwardQuery *) li->data;
		if (q == NULL)
			continue;

		if (q->dsp_sa->sin_addr.s_addr == clnt_sa->sin_addr.s_addr) {
			g_slist_free (qlist);
			return q;
		}

		if (q->acctime > 0 &&
		    curtime > q->acctime + GDM_FORWARD_QUERY_TIMEOUT)	{
			gdm_debug ("gdm_forward_query_lookup: Disposing stale forward query from %s",
				   inet_ntoa (clnt_sa->sin_addr));
			gdm_forward_query_dispose (q);
		}

	}
	g_slist_free (qlist);

	gdm_debug ("gdm_forward_query_lookup: Host %s not found", 
		   inet_ntoa (clnt_sa->sin_addr));

	return NULL;
}


static void
gdm_forward_query_dispose (GdmForwardQuery *q)
{
	if (q == NULL)
		return;

	forward_queries = g_slist_remove (forward_queries, q);

	q->acctime = 0;

	gdm_debug ("gdm_forward_query_dispose: Disposing %s", 
		   inet_ntoa (q->dsp_sa->sin_addr));

	g_free (q->dsp_sa);
	q->dsp_sa = NULL;
	g_free (q->from_sa);
	q->from_sa = NULL;

	g_free (q);
}


static void 
gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len)
{
    ARRAY8 clnt_addr;
    ARRAY8 clnt_port;
    ARRAYofARRAY8 clnt_authlist;
    gint i = 0, explen = 1;
    struct sockaddr_in disp_sa = {0};
    
    /* Read display address */
    if (! XdmcpReadARRAY8 (&buf, &clnt_addr)) {
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not read display address"));
	return;
    }
    
    /* Read display port */
    if (! XdmcpReadARRAY8 (&buf, &clnt_port)) {
	XdmcpDisposeARRAY8 (&clnt_addr);
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not read display port number"));
	return;
    }
    
    /* Extract array of authentication names from Xdmcp packet */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
	XdmcpDisposeARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY8 (&clnt_port);
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not extract authlist from packet")); 
	return;
    }
    
    /* Crude checksumming */
    explen = 1;
    explen += 2+clnt_addr.length;
    explen += 2+clnt_port.length;
    
    for (i = 0 ; i < clnt_authlist.length ; i++) {
	gdm_debug ("gdm_xdmcp_handle_forward_query: authlist: %s",
		   (char *)clnt_authlist.data);
	explen += 2+clnt_authlist.data[i].length;
    }
    
    if (len != explen) {
	gdm_error (_("gdm_xdmcp_handle_forward_query: Error in checksum")); 
	goto out;
    }
    
    if (clnt_port.length != 2 ||
	clnt_addr.length != 4) {
	    gdm_error (_("gdm_xdmcp_handle_forward_query: Bad address")); 
	    goto out;
    }

    g_assert (4 == sizeof (struct in_addr));
    gdm_xdmcp_whack_queued_managed_forwards
	    (clnt_sa, (struct in_addr *)clnt_addr.data);

    disp_sa.sin_family = AF_INET;

    /* Find client port number */
    memcpy (&disp_sa.sin_port, clnt_port.data, 2);
    
    /* Find client address */
    memcpy (&disp_sa.sin_addr.s_addr, clnt_addr.data, 4);
    
    gdm_debug ("gdm_xdmcp_handle_forward_query: Got FORWARD_QUERY for display: %s, port %d", 
	       inet_ntoa (disp_sa.sin_addr), ntohs (disp_sa.sin_port));
    
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
gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa)
{
    ARRAY8 status;
    XdmcpHeader header;
    static char *last_status = NULL;
    static time_t last_willing = 0;
    char *bin;
    FILE *fd;
    
    gdm_debug ("gdm_xdmcp_send_willing: Sending WILLING to %s", inet_ntoa (clnt_sa->sin_addr));

    if (last_willing == 0 ||
	time (NULL) - 3 > last_willing) {
	    char statusBuf[256] = "";
	    bin = ve_first_word (GdmWilling);
	    if ( ! ve_string_empty (bin) &&
		 access (bin, X_OK) == 0 &&
		 (fd = popen (GdmWilling, "r")) != NULL) {
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

    if ( ! gdm_is_local_addr (&(clnt_sa->sin_addr)) &&
	 gdm_xdmcp_displays_from_host (&(clnt_sa->sin_addr)) >= GdmDispPerHost) {
	    /* Don't translate, this goes over the wire to servers where we
	     * don't know the charset or language, so it must be ascii */
	    status.data = g_strdup_printf ("%s (Server is busy)", last_status);
    } else {
	    status.data = g_strdup (last_status);
    }
    status.length = strlen (status.data);
    
    header.opcode = (CARD16) WILLING;
    header.length = 6 + serv_authlist.authentication.length;
    header.length += servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &serv_authlist.authentication); /* Hardcoded authentication */
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));

    g_free (status.data);
}

static void
gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type)
{
    ARRAY8 status;
    XdmcpHeader header;
    
    gdm_debug ("gdm_xdmcp_send_unwilling: Sending UNWILLING to %s", inet_ntoa (clnt_sa->sin_addr));
    
    gdm_error (_("Denied XDMCP query from host %s"), inet_ntoa (clnt_sa->sin_addr));
    
    /* Don't translate, this goes over the wire to servers where we
     * don't know the charset or language, so it must be ascii */
    status.data = "Display not authorized to connect";
    status.length = strlen (status.data);
    
    header.opcode = (CARD16) UNWILLING;
    header.length = 4 + servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));
}

static void
gdm_xdmcp_really_send_managed_forward (struct sockaddr_in *clnt_sa,
				       struct sockaddr_in *origin)
{
	ARRAY8 address;
	XdmcpHeader header;
	struct in_addr addr;

	gdm_debug ("gdm_xdmcp_really_send_managed_forward: "
		   "Sending MANAGED_FORWARD to %s",
		   inet_ntoa (clnt_sa->sin_addr));

	address.length = sizeof (struct in_addr);
	address.data = (void *)&addr;
	memcpy (address.data, &(origin->sin_addr), sizeof (struct in_addr));

	header.opcode = (CARD16) GDM_XDMCP_MANAGED_FORWARD;
	header.length = 4 + address.length;
	header.version = GDM_XDMCP_PROTOCOL_VERSION;
	XdmcpWriteHeader (&buf, &header);

	XdmcpWriteARRAY8 (&buf, &address);
	XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		    (int)sizeof (struct sockaddr_in));
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
		managed_forwards = g_list_remove (managed_forwards, mf);
		mf->handler = 0;
		/* mf freed by glib */
		return FALSE;
	}
	return TRUE;
}

static void
gdm_xdmcp_send_managed_forward (struct sockaddr_in *clnt_sa,
				struct sockaddr_in *origin)
{
	ManagedForward *mf;

	gdm_xdmcp_really_send_managed_forward (clnt_sa, origin);

	mf = g_new0 (ManagedForward, 1);
	mf->times = 0;
	memcpy (&(mf->manager), clnt_sa, sizeof (struct sockaddr_in));
	memcpy (&(mf->origin), origin, sizeof (struct sockaddr_in));

	mf->handler = g_timeout_add_full (G_PRIORITY_DEFAULT,
					  MANAGED_FORWARD_INTERVAL,
					  managed_forward_handler,
					  mf,
					  (GDestroyNotify) g_free);
	managed_forwards = g_list_prepend (managed_forwards, mf);
}

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

static char *
get_host_from_addr (struct sockaddr_in *clnt_sa)
{
	char *hostname;
	struct hostent *he;

	/* Find client hostname */
	he = gethostbyaddr ((gchar *) &(clnt_sa->sin_addr),
			    sizeof (struct in_addr),
			    AF_INET);

	if (he != NULL) {
		hostname = g_strdup (he->h_name);
	} else {
		hostname = g_strdup (inet_ntoa (clnt_sa->sin_addr));
	}
	return hostname;
}

static void
gdm_xdmcp_handle_request (struct sockaddr_in *clnt_sa, gint len)
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
    
    gdm_debug ("gdm_xdmcp_handle_request: Got REQUEST from %s", 
	       inet_ntoa (clnt_sa->sin_addr));
    
    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
	gdm_error (_("gdm_xdmcp_handle_request: Got REQUEST from banned host %s"), 
		   inet_ntoa (clnt_sa->sin_addr));
	return;
    }
    
    /* Remote display number */
    if (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Display Number"));
	return;
    }
    
    /* We don't care about connection type. Address says it all */
    if (! XdmcpReadARRAY16 (&buf, &clnt_conntyp)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Connection Type"));
	return;
    }
    
    /* This is TCP/IP - we don't care */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_addr)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Client Address"));
        XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    /* Read authentication type */
    if (! XdmcpReadARRAY8 (&buf, &clnt_authname)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authentication Names"));
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    /* Read authentication data */
    if (! XdmcpReadARRAY8 (&buf, &clnt_authdata)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authentication Data"));
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	XdmcpDisposeARRAY8 (&clnt_authname);
	return;
    }
    
    /* Read and select from supported authorization list */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authorization)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authorization List"));
	XdmcpDisposeARRAY8 (&clnt_authdata);
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	XdmcpDisposeARRAY8 (&clnt_authname);
	return;
    }
    
    /* libXdmcp doesn't terminate strings properly so we cheat and use strncmp() */
    for (i = 0 ; i < clnt_authorization.length ; i++)
	if (! strncmp (clnt_authorization.data[i].data, "MIT-MAGIC-COOKIE-1", 18))
	    mitauth = TRUE;
    
    /* Manufacturer ID */
    if (! XdmcpReadARRAY8 (&buf, &clnt_manufacturer)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Manufacturer ID"));
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
    
    if (explen != len) {
	gdm_error (_("gdm_xdmcp_handle_request: Failed checksum from %s"),
		   inet_ntoa (clnt_sa->sin_addr));
	XdmcpDisposeARRAY8 (&clnt_authname);
	XdmcpDisposeARRAY8 (&clnt_authdata);
	XdmcpDisposeARRAY8 (&clnt_manufacturer);
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
	XdmcpDisposeARRAY16 (&clnt_conntyp);
	return;
    }
    
    gdm_debug ("gdm_xdmcp_handle_request: pending=%d, MaxPending=%d, sessions=%d, MaxSessions=%d",
	       pending, GdmMaxPending, sessions, GdmMaxSessions);


    /* Check if ok to manage display */
    if (mitauth &&
	sessions < GdmMaxSessions &&
	(gdm_is_local_addr (&(clnt_sa->sin_addr)) ||
	 gdm_xdmcp_displays_from_host (&(clnt_sa->sin_addr)) < GdmDispPerHost)) {
	    char *disp;
	    char *hostname = get_host_from_addr (clnt_sa);
	    disp = g_strdup_printf ("%s:%d", hostname, clnt_dspnum);

	    /* Check if we are already talking to this host */
	    gdm_xdmcp_display_dispose_check (disp);
	    g_free (disp);

	    if (pending >= GdmMaxPending) {
		    gdm_debug ("gdm_xdmcp_handle_request: maximum pending");
		    /* Don't translate, this goes over the wire to servers where we
		    * don't know the charset or language, so it must be ascii */
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum pending servers");	
	    } else {
		    gdm_xdmcp_send_accept (hostname, clnt_sa, clnt_dspnum);
	    }

	    g_free (hostname);
    } else {
	    /* Don't translate, this goes over the wire to servers where we
	    * don't know the charset or language, so it must be ascii */
	    if ( ! mitauth)
		    gdm_xdmcp_send_decline (clnt_sa, "Only MIT-MAGIC-COOKIE-1 supported");	
	    else if (sessions >= GdmMaxSessions)
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum number of open sessions reached");	
	    else 
		    gdm_xdmcp_send_decline (clnt_sa, "Maximum number of open sessions from your host reached");	
    }

    XdmcpDisposeARRAY8 (&clnt_authname);
    XdmcpDisposeARRAY8 (&clnt_authdata);
    XdmcpDisposeARRAY8 (&clnt_manufacturer);
    XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
    XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
    XdmcpDisposeARRAY16 (&clnt_conntyp);
}


static void
gdm_xdmcp_send_accept (const char *hostname,
		       struct sockaddr_in *clnt_sa,
		       gint displaynum)
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 authname;
    ARRAY8 authdata;
    GdmDisplay *d;
    
    d = gdm_xdmcp_display_alloc (&(clnt_sa->sin_addr), hostname, displaynum);
    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    authname.data = "MIT-MAGIC-COOKIE-1";
    authname.length = strlen (authname.data);
    
    authdata.data = d->bcookie;
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
    
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));
    
    gdm_debug ("gdm_xdmcp_send_accept: Sending ACCEPT to %s with SessionID=%ld", 
	       inet_ntoa (clnt_sa->sin_addr), (long)d->sessionid);
}


static void
gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa, const char *reason)
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 status;
    GdmForwardQuery *fq;
    
    gdm_debug ("gdm_xdmcp_send_decline: Sending DECLINE to %s", 
	       inet_ntoa (clnt_sa->sin_addr));
    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    status.data = (char *)reason;
    status.length = strlen (status.data);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) DECLINE;
    header.length = 2 + status.length;
    header.length += 2 + authentype.length;
    header.length += 2 + authendata.length;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpWriteARRAY8 (&buf, &authentype);
    XdmcpWriteARRAY8 (&buf, &authendata);
    
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));

    /* Send MANAGED_FORWARD to indicate that the connection 
     * reached some sort of resolution */
    fq = gdm_forward_query_lookup (clnt_sa);
    if (fq != NULL) {
	    gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
	    gdm_forward_query_dispose (fq);
    }
}


static void 
gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len)
{
    CARD32 clnt_sessid;
    CARD16 clnt_dspnum;
    ARRAY8 clnt_dspclass;
    GdmDisplay *d;
    GdmIndirectDisplay *id;
    GdmForwardQuery *fq;
    
    gdm_debug ("gdm_xdmcp_handle_manage: Got MANAGE from %s", inet_ntoa (clnt_sa->sin_addr));
    
    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
	gdm_error (_("gdm_xdmcp_handle_manage: Got Manage from banned host %s"), 
		   inet_ntoa (clnt_sa->sin_addr));
	return;
    }
    
    /* SessionID */
    if (! XdmcpReadCARD32 (&buf, &clnt_sessid)) {
	gdm_error (_("gdm_xdmcp_handle_manage: Could not read Session ID"));
	return;
    }
    
    /* Remote display number */
    if (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("gdm_xdmcp_handle_manage: Could not read Display Number"));
	return;
    }
    
    gdm_debug ("gdm_xdmcp_handle_manage: Got Display=%d, SessionID=%ld from %s", 
	       (int)clnt_dspnum, (long)clnt_sessid, inet_ntoa (clnt_sa->sin_addr));
    
    /* Display Class */
    if (! XdmcpReadARRAY8 (&buf, &clnt_dspclass)) {
	gdm_error (_("gdm_xdmcp_handle_manage: Could not read Display Class"));
	return;
    }
    
    d = gdm_xdmcp_display_lookup (clnt_sessid);
    if (d != NULL &&
        d->dispstat == XDMCP_PENDING) {

	gdm_debug ("gdm_xdmcp_handle_manage: Looked up %s", d->name);

	if (GdmIndirect) {
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
	sessions++;
	pending--;

	/* Start greeter/session */
	if (!gdm_display_manage (d)) {
	    gdm_xdmcp_send_failed (clnt_sa, clnt_sessid);
	    XdmcpDisposeARRAY8(&clnt_dspclass);
	    return;
	}
    }
    else if (d != NULL && d->dispstat == XDMCP_MANAGED) {
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
gdm_xdmcp_handle_managed_forward (struct sockaddr_in *clnt_sa, gint len)
{
	ARRAY8 clnt_address;
	GdmIndirectDisplay *id;

	gdm_debug ("gdm_xdmcp_handle_managed_forward: "
		   "Got MANAGED_FORWARD from %s",
		   inet_ntoa (clnt_sa->sin_addr));

	/* Hostname */
	if ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_managed_forward");
		return;
	}

	if (clnt_address.length != sizeof (struct in_addr)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_managed_forward");
		XdmcpDisposeARRAY8 (&clnt_address);
		return;
	}

	id = gdm_choose_indirect_lookup_by_chosen
		(&(clnt_sa->sin_addr), (struct in_addr *)clnt_address.data);
	if (id != NULL) {
		gdm_choose_indirect_dispose (id);
	}

	/* Note: we send GOT even on not found, just in case our previous
	 * didn't get through and this was a second managed forward */
	gdm_xdmcp_send_got_managed_forward
		(clnt_sa, (struct in_addr *)clnt_address.data);

	XdmcpDisposeARRAY8 (&clnt_address);
}

static void
gdm_xdmcp_whack_queued_managed_forwards (struct sockaddr_in *clnt_sa,
					 struct in_addr *origin)
{
	GList *li;

	for (li = managed_forwards; li != NULL; li = li->next) {
		ManagedForward *mf = li->data;
		if (mf->manager.sin_addr.s_addr == clnt_sa->sin_addr.s_addr &&
		    mf->origin.sin_addr.s_addr == origin->s_addr) {
			managed_forwards = g_list_remove_link (managed_forwards, li);
			g_list_free_1 (li);
			g_source_remove (mf->handler);
			/* mf freed by glib */
			return;
		}
	}
}

static void 
gdm_xdmcp_handle_got_managed_forward (struct sockaddr_in *clnt_sa, gint len)
{
	ARRAY8 clnt_address;

	gdm_debug ("gdm_xdmcp_handle_got_managed_forward: "
		   "Got MANAGED_FORWARD from %s",
		   inet_ntoa (clnt_sa->sin_addr));

	/* Hostname */
	if ( ! XdmcpReadARRAY8 (&buf, &clnt_address)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_got_managed_forward");
		return;
	}

	if (clnt_address.length != sizeof (struct in_addr)) {
		gdm_error (_("%s: Could not read address"),
			   "gdm_xdmcp_handle_got_managed_forward");
		XdmcpDisposeARRAY8 (&clnt_address);
		return;
	}

	gdm_xdmcp_whack_queued_managed_forwards
		(clnt_sa, (struct in_addr *)clnt_address.data);

	XdmcpDisposeARRAY8 (&clnt_address);
}


static void
gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid)
{
    XdmcpHeader header;
    GdmForwardQuery *fq;
    
    gdm_debug ("gdm_xdmcp_send_refuse: Sending REFUSE to %ld", (long)sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode= (CARD16) REFUSE;
    header.length = 4;
    
    XdmcpWriteHeader (&buf, &header);  
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));

    /* this was from a forwarded query quite apparently so
    * send MANAGED_FORWARD */
    fq = gdm_forward_query_lookup (clnt_sa);
    if (fq != NULL) {
	    gdm_xdmcp_send_managed_forward (fq->from_sa, clnt_sa);
	    gdm_forward_query_dispose (fq);
    }
}


static void
gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid)
{
    XdmcpHeader header;
    ARRAY8 status;
    
    gdm_debug ("gdm_xdmcp_send_failed: Sending FAILED to %ld", (long)sessid);
    
    /* Don't translate, this goes over the wire to servers where we
     * don't know the charset or language, so it must be ascii */
    status.data = "Failed to start session";
    status.length = strlen (status.data);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) FAILED;
    header.length = 6+status.length;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));
}


static void
gdm_xdmcp_handle_keepalive (struct sockaddr_in *clnt_sa, gint len)
{
    CARD16 clnt_dspnum;
    CARD32 clnt_sessid;
    
    gdm_debug ("gdm_xdmcp_handle_keepalive: Got KEEPALIVE from %s", 
	       inet_ntoa (clnt_sa->sin_addr));
    
    /* Check with tcp_wrappers if client is allowed to access */
    if (! gdm_xdmcp_host_allow (clnt_sa)) {
	gdm_error (_("gdm_xdmcp_handle_keepalive: Got KEEPALIVE from banned host %s"), 
		   inet_ntoa (clnt_sa->sin_addr));
	return;
    }
    
    /* Remote display number */
    if (! XdmcpReadCARD16 (&buf, &clnt_dspnum)) {
	gdm_error (_("gdm_xdmcp_handle_keepalive: Could not read Display Number"));
	return;
    }
    
    /* SessionID */
    if (! XdmcpReadCARD32 (&buf, &clnt_sessid)) {
	gdm_error (_("gdm_xdmcp_handle_keepalive: Could not read Session ID"));
	return;
    }
    
    gdm_xdmcp_send_alive (clnt_sa, clnt_sessid);
}


static void
gdm_xdmcp_send_alive (struct sockaddr_in *clnt_sa, CARD32 sessid)
{
    XdmcpHeader header;
    
    gdm_debug ("Sending ALIVE to %ld", (long)sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) ALIVE;
    header.length = 5;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD8 (&buf, 1);
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpFlush (gdm_xdmcpfd, &buf, (XdmcpNetaddr)clnt_sa,
		(int)sizeof (struct sockaddr_in));
}


static gboolean
gdm_xdmcp_host_allow (struct sockaddr_in *clnt_sa)
{
#ifdef HAVE_TCPWRAPPERS
	
    /* avoids a warning, my tcpd.h file doesn't include this prototype, even
     * though the library does include the function and the manpage mentions it
     */
    extern int hosts_ctl (char *daemon, char *client_name, char *client_addr, char *client_user);

    struct hostent *client_he;
    gchar *client;
    
    /* Find client hostname */
    client_he = gethostbyaddr ((gchar *) &clnt_sa->sin_addr,
			       sizeof (struct in_addr),
			       AF_INET);
    
    client = (client_he != NULL) ? client_he->h_name : NULL;
    
    /* Check with tcp_wrappers if client is allowed to access */
    return (hosts_ctl ("gdm", client ? client : "unknown", inet_ntoa (clnt_sa->sin_addr), ""));
#else /* HAVE_TCPWRAPPERS */
    return (TRUE);
#endif /* HAVE_TCPWRAPPERS */
}


static GdmDisplay *
gdm_xdmcp_display_alloc (struct in_addr *addr, const char *hostname, gint displaynum)
{
    GdmDisplay *d = NULL;
    
    d = g_new0 (GdmDisplay, 1);
    d->authfile = NULL;
    d->auths = NULL;
    d->userauth = NULL;
    d->command = NULL;
    d->greetpid = 0;
    d->servpid = 0;
    d->servstat = 0;
    d->sessionid = 0;
    d->sesspid = 0;
    d->slavepid = 0;
    d->type = TYPE_XDMCP;
    d->console = FALSE;
    d->dispstat = XDMCP_PENDING;
    d->sessionid = globsessid++;
    d->acctime = time (NULL);
    d->dispnum = displaynum;

#ifdef __linux__
    d->vt = -1;
#endif

    d->logged_in = FALSE;
    d->login = NULL;

    d->sleep_before_run = 0;
    if (GdmAllowRemoteAutoLogin &&
	! ve_string_empty (GdmTimedLogin)) {
	    d->timed_login_ok = TRUE;
    } else {
	    d->timed_login_ok = FALSE;
    }
    
    d->name = g_strdup_printf ("%s:%d", hostname,
			       displaynum);
    d->hostname = g_strdup (hostname);
    memcpy (&d->addr, addr, sizeof (struct in_addr));
    
    /* Secure display with cookie */
    if (! gdm_auth_secure_display (d))
	gdm_error ("gdm_xdmcp_display_alloc: Error setting up cookies for %s", d->name);
    
    displays = g_slist_append (displays, d);
    
    pending++;
    
    gdm_debug ("gdm_xdmcp_display_alloc: display=%s, session id=%ld, pending=%d ",
	       d->name, (long)d->sessionid, pending);
    
    return (d);
}


static GdmDisplay *
gdm_xdmcp_display_lookup (CARD32 sessid)
{
    GSList *dlist = displays;
    GdmDisplay *d;
    
    if(!sessid)
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
gdm_xdmcp_display_dispose_check (const gchar *name)
{
	GSList *dlist;

	if (name == NULL)
		return;

	gdm_debug ("gdm_xdmcp_display_dispose_check (%s)", name);

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    d->type == TYPE_XDMCP &&
		    strcmp (d->name, name) == 0) {
			if (d->dispstat == XDMCP_MANAGED)
				gdm_display_unmanage (d);
			else
				gdm_display_dispose (d);

			/* restart as the list is now fucked */
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

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    d->type == TYPE_XDMCP &&
		    d->dispstat == XDMCP_PENDING &&
		    time (NULL) > d->acctime + GdmMaxManageWait) {
			gdm_debug ("gdm_xdmcp_displays_check: Disposing session id %ld",
				   (long)d->sessionid);
			gdm_display_dispose (d);

			/* restart as the list is now fucked */
			dlist = displays;
		} else {
			/* just go on */
			dlist = dlist->next;
		}
	}
}

#else /* HAVE_LIBXDMCP */

/* Here come some empty stubs for no XDMCP support */
int
gdm_xdmcp_init  (void)
{
	gdm_error (_("gdm_xdmcp_init: No XDMCP support"));
	return FALSE;
}

void
gdm_xdmcp_run (void)
{
	gdm_error (_("gdm_xdmcp_run: No XDMCP support"));
}

void
gdm_xdmcp_close (void)
{
	gdm_error (_("gdm_xdmcp_close: No XDMCP support"));
}

#endif /* HAVE_LIBXDMCP */

/* EOF */
