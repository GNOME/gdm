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
#include <stdio.h>
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
#include <fcntl.h>

#ifdef HAVE_TCPWRAPPERS
  #include <tcpd.h>
#endif

#include "gdm.h"
#include "xdmcp.h"
#include "display.h"
#include "misc.h"
#include "auth.h"

static const gchar RCSid[]="$Id$";

/* TCP Wrapper syslog control */
gint allow_severity = LOG_INFO;
gint deny_severity = LOG_WARNING;

gint xdmcpfd;
gint choosefd;
gint globsessid;
gint pending = 0;
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
extern gint GdmIndirect;	/* Honor XDMCP_INDIRECT, i.e. choosing */
extern gint GdmMaxIndirectWait;	/* Max wait between INDIRECT_QUERY and MANAGE */
extern gint GdmDispPerHost;	/* Max number of displays per remote host */

/* Local prototypes */
static gboolean gdm_xdmcp_decode_packet (void);
static void gdm_xdmcp_handle_query (struct sockaddr_in *clnt_sa, gint len, gint type);
static void gdm_xdmcp_send_forward_query (GdmIndirectDisplay *id, ARRAYofARRAY8Ptr authlist);
static void gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_request (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_handle_keepalive (struct sockaddr_in *clnt_sa, gint len);
static void gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa);
static void gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type);
static void gdm_xdmcp_send_accept (struct sockaddr_in *clnt_sa, gint displaynum);
static void gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa);
static void gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid);
static void gdm_xdmcp_send_alive (struct sockaddr_in *clnt_sa, CARD32 sessid);
static gboolean gdm_xdmcp_host_allow (struct sockaddr_in *cnlt_sa);
static GdmDisplay *gdm_xdmcp_display_alloc (struct sockaddr_in *, gint);
static GdmDisplay *gdm_xdmcp_display_lookup (CARD32 sessid);
static void gdm_xdmcp_display_dispose_check (gchar *name);
static void gdm_xdmcp_displays_check (void);


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


extern gboolean gdm_choose_socket_handler (GIOChannel *source, GIOCondition cond, gint fd);
extern GdmIndirectDisplay *gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa);
extern GdmIndirectDisplay *gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa);


int
gdm_xdmcp_init (void)
{
    struct sockaddr_in serv_sa;
    gchar hostbuf[256];
    struct utsname name;
    
    globsessid = time (NULL);
    
    /* Fetch and store local hostname in XDMCP friendly format */
    if (gethostname (hostbuf, 255))
	gdm_fail (_("gdm_xdmcp_init: Could not get server hostname: %s!"), strerror (errno));
    
    uname (&name);
    sysid = g_strconcat (name.sysname, " ", name.release, NULL);

    servhost.data = g_strdup (hostbuf);
    servhost.length = strlen (servhost.data);
    
    gdm_debug ("Start up on host %s, port %d", hostbuf, GdmPort);
    
    /* Open socket for communications */
    xdmcpfd = socket (AF_INET, SOCK_DGRAM, 0); /* UDP */
    
    if (xdmcpfd == -1)
	gdm_fail (_("gdm_xdmcp_init: Could not create socket!"));
    
    serv_sa.sin_family = AF_INET;
    serv_sa.sin_port = htons (GdmPort); /* UDP 177 */
    serv_sa.sin_addr.s_addr = htonl (INADDR_ANY);
    
    if (bind (xdmcpfd, (struct sockaddr_in *) &serv_sa, sizeof (serv_sa)) == -1)
	gdm_fail (_("gdm_xdmcp_init: Could not bind to XDMCP socket!"));

    /* Setup FIFO if choosing is enabled */
    if (GdmIndirect) {
	gchar *fifopath;

	fifopath = g_strconcat (GdmServAuthDir, "/.gdmchooser", NULL);

	if (!fifopath)
	    gdm_fail (_("gdm_xdmcp_init: Can't alloc fifopath"));

	unlink (fifopath);

	if (mkfifo (fifopath, 0660) < 0)
	    gdm_fail (_("gdm_xdmcp_init: Could not make FIFO for chooser"));

	choosefd = open (fifopath, O_RDWR); /* Open with write to avoid EOF */

	if (!choosefd)
	    gdm_fail (_("gdm_xdmcp_init: Could not open FIFO for chooser"));

	chmod (fifopath, 0660);

	g_free (fifopath);
    }

    return (TRUE);
}


void
gdm_xdmcp_run (void)
{
    GIOChannel *xdmcpchan;
    
    xdmcpchan = g_io_channel_unix_new (xdmcpfd);
    g_io_add_watch_full (xdmcpchan, G_PRIORITY_DEFAULT,
			 G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
			 (GIOFunc) gdm_xdmcp_decode_packet,
			 GINT_TO_POINTER (xdmcpfd), NULL);
    g_io_channel_unref (xdmcpchan);

    if (GdmIndirect) {
	GIOChannel *choosechan;

	choosechan = g_io_channel_unix_new (choosefd);
	g_io_add_watch_full (choosechan, G_PRIORITY_DEFAULT,
			     G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
			     (GIOFunc) gdm_choose_socket_handler,
			     GINT_TO_POINTER (choosefd), NULL);
	g_io_channel_unref (choosechan);
    }
}


void
gdm_xdmcp_close (void)
{
    close (xdmcpfd);

    if (GdmIndirect && choosefd)
	close (choosefd);
}


static gboolean
gdm_xdmcp_decode_packet (void)
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
    
    if (!XdmcpFill (xdmcpfd, &buf, &clnt_sa, &sa_len)) {
	gdm_error (_("gdm_xdmcp_decode: Could not create XDMCP buffer!"));
	return TRUE;
    }
    
    if (!XdmcpReadHeader (&buf, &header)) {
	gdm_error (_("gdm_xdmcp_decode: Could not read XDMCP header!"));
	return TRUE;
    }
    
    if (header.version != XDM_PROTOCOL_VERSION) {
	gdm_error (_("gdm_xdmcp_decode: Incorrect XDMCP version!"));
	return TRUE;
    }

    gdm_debug ("gdm_xdmcp_decode: Received opcode %s from client %s", 
	       opcode_names[header.opcode], inet_ntoa (clnt_sa.sin_addr));


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
	gdm_debug ("gdm_xdmcp_handle_query: authlist: %s", clnt_authlist.data);
	explen += 2+clnt_authlist.data[i].length;
    }
    
    if (len != explen) {
	gdm_error (_("gdm_xdmcp_handle_query: Error in checksum")); 
	return;
    }
    
    /* Check with tcp_wrappers if client is allowed to access */
    if (gdm_xdmcp_host_allow (clnt_sa)) {

	/* If this is an INDIRECT_QUERY, try to look up the display in
 	 * the pending list. If found send a FORWARD_QUERY to the
 	 * chosen manager. Otherwise alloc a new indirect display. */

	if (GdmIndirect && type==INDIRECT_QUERY) {
	    GdmIndirectDisplay *id = gdm_choose_indirect_lookup (clnt_sa);

	    if (id) 
		if (id->acctime + GdmMaxIndirectWait < time (NULL)) /* Expired? */
		    gdm_xdmcp_send_forward_query (id, &clnt_authlist);
		else
		    gdm_xdmcp_send_unwilling (clnt_sa, type);
	    else
		gdm_choose_indirect_alloc (clnt_sa);
	}

	gdm_xdmcp_send_willing (clnt_sa);
    }
    else
	gdm_xdmcp_send_unwilling (clnt_sa, type);

    /* Dispose authlist from remote display */
    /* XdmcpDisposeARRAYofARRAY8 (&clnt_authlist); */
}


static void
gdm_xdmcp_send_forward_query (GdmIndirectDisplay *id, ARRAYofARRAY8Ptr authlist)
{
    ARRAY8 status;
    XdmcpHeader header;
    
    status.data = sysid;
    status.length = strlen (sysid);
    
    header.opcode = (CARD16) WILLING;
    header.length = 6 + serv_authlist.authentication.length;
    header.length += servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &serv_authlist.authentication); /* Hardcoded authentication */
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
    /* XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in)); */
}


static void 
gdm_xdmcp_handle_forward_query (struct sockaddr_in *clnt_sa, gint len)
{
    ARRAY8 clnt_addr;
    ARRAY8 clnt_port;
    ARRAYofARRAY8 clnt_authlist;
    gint i = 0, explen = 1;
    static struct sockaddr_in *disp_sa;
    guint port = 0;
    struct in_addr ia;
    
    gdm_debug ("gdm_xdmcp_handle_forward_query: ForwardQuery from %s", 
	       inet_ntoa (clnt_sa->sin_addr));
    
    /* Read display address */
    if (! XdmcpReadARRAY8 (&buf, &clnt_addr)) {
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not read display address"));
	return;
    }
    
    /* Read display port */
    if (! XdmcpReadARRAY8 (&buf, &clnt_port)) {
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not read display port number"));
	return;
    }
    
    /* Extract array of authentication names from Xdmcp packet */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authlist)) {
	XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
	XdmcpDisposeARRAYofARRAY8 (&clnt_port);
	gdm_error (_("gdm_xdmcp_handle_forward_query: Could not extract authlist from packet")); 
	return;
    }
    
    /* Crude checksumming */
    explen = 1;
    explen += 2+clnt_addr.length;
    explen += 2+clnt_port.length;
    
    for (i = 0 ; i < clnt_authlist.length ; i++) {
	gdm_debug ("gdm_xdmcp_handle_forward_query: authlist: %s", clnt_authlist.data);
	explen += 2+clnt_authlist.data[i].length;
    }
    
    if (len != explen) {
	gdm_error (_("gdm_xdmcp_handle_forward_query: Error in checksum")); 
	goto out;
    }
    
    /* Find client port number */
    for (i = 0 ; i < clnt_port.length ; i++)
	port = port*256+clnt_port.data[i];
    
    /* Find client address. Ugly, ugly. Endianness sucks... */
    memmove (&ia.s_addr, clnt_addr.data, MIN(clnt_addr.length, sizeof(ia.s_addr)));
    
    gdm_debug ("gdm_xdmcp_handle_forward_query: Got FORWARD_QUERY from display: %s, port %d", 
	       inet_ntoa (ia), port);
    
    /* Assemble sockaddr_in struct to pass on */
    disp_sa = g_new0 (struct sockaddr_in, 1);
    disp_sa->sin_family = AF_INET;
    disp_sa->sin_port = htons (port);
    disp_sa->sin_addr.s_addr = ia.s_addr;
    
    /* Check with tcp_wrappers if display is allowed to access */
    if (gdm_xdmcp_host_allow (disp_sa)) 
	gdm_xdmcp_send_willing (disp_sa);
    else
	gdm_xdmcp_send_unwilling (disp_sa, FORWARD_QUERY);

  out:
    g_free(disp_sa);
    /* Cleanup */
    XdmcpDisposeARRAYofARRAY8 (&clnt_authlist);
    XdmcpDisposeARRAYofARRAY8 (&clnt_port);
    XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
}


static void
gdm_xdmcp_send_willing (struct sockaddr_in *clnt_sa)
{
    ARRAY8 status;
    XdmcpHeader header;
    
    gdm_debug ("gdm_xdmcp_send_willing: Sending WILLING to %s", inet_ntoa (clnt_sa->sin_addr));
    
    status.data = sysid;
    status.length = strlen (sysid);
    
    header.opcode = (CARD16) WILLING;
    header.length = 6 + serv_authlist.authentication.length;
    header.length += servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &serv_authlist.authentication); /* Hardcoded authentication */
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
}

static void
gdm_xdmcp_send_unwilling (struct sockaddr_in *clnt_sa, gint type)
{
    ARRAY8 status;
    XdmcpHeader header;
    
    gdm_debug ("gdm_xdmcp_send_unwilling: Sending UNWILLING to %s", inet_ntoa (clnt_sa->sin_addr));
    
    gdm_error (_("Denied XDMCP query from host %s"), inet_ntoa (clnt_sa->sin_addr));
    
    status.data = _("Display not authorized to connect");
    status.length = strlen (status.data);
    
    header.opcode = (CARD16) UNWILLING;
    header.length = 4 + servhost.length + status.length;
    header.version = XDM_PROTOCOL_VERSION;
    XdmcpWriteHeader (&buf, &header);
    
    XdmcpWriteARRAY8 (&buf, &servhost);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
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
	return;
    }
    
    /* Read authentication type */
    if (! XdmcpReadARRAY8 (&buf, &clnt_authname)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authentication Names"));
	return;
    }
    
    /* Read authentication data */
    if (! XdmcpReadARRAY8 (&buf, &clnt_authdata)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authentication Data"));
	return;
    }
    
    /* Read and select from supported authorization list */
    if (! XdmcpReadARRAYofARRAY8 (&buf, &clnt_authorization)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Authorization List"));
	return;
    }
    
    /* libXdmcp doesn't terminate strings properly so we cheat and use strncmp() */
    for (i = 0 ; i < clnt_authorization.length ; i++)
	if (! strncmp (clnt_authorization.data[i].data, "MIT-MAGIC-COOKIE-1", 18))
	    mitauth = TRUE;
    
    /* Manufacturer ID */
    if (! XdmcpReadARRAY8 (&buf, &clnt_manufacturer)) {
	gdm_error (_("gdm_xdmcp_handle_request: Could not read Manufacturer ID"));
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
	return;
    }
    
    gdm_debug ("gdm_xdmcp_handle_request: pending=%d, MaxPending=%d, sessions=%d, MaxSessions=%d",
	       pending, GdmMaxPending, sessions, GdmMaxSessions);
    
    /* Check if ok to manage display */
    if (mitauth &&
	pending < GdmMaxPending && 
	sessions < GdmMaxSessions)
	gdm_xdmcp_send_accept (clnt_sa, clnt_dspnum);
    else
	gdm_xdmcp_send_decline (clnt_sa);	

    XdmcpDisposeARRAY8 (&clnt_authname);
    XdmcpDisposeARRAY8 (&clnt_authdata);
    XdmcpDisposeARRAY8 (&clnt_manufacturer);
    XdmcpDisposeARRAYofARRAY8 (&clnt_addr);
    XdmcpDisposeARRAYofARRAY8 (&clnt_authorization);
    XdmcpDisposeARRAY16 (&clnt_conntyp);
}


static void
gdm_xdmcp_send_accept (struct sockaddr_in *clnt_sa, gint displaynum)
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 authname;
    ARRAY8 authdata;
    GdmDisplay *d;
    
    d = gdm_xdmcp_display_alloc (clnt_sa, displaynum);
    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    authname.data = "MIT-MAGIC-COOKIE-1";
    authname.length = strlen (authname.data);
    
    authdata.data = d->bcookie;
    authdata.length = strlen (d->bcookie); /* I.e. 16 */
    
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
    
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
    
    gdm_debug ("gdm_xdmcp_send_accept: Sending ACCEPT to %s with SessionID=%d", 
	       inet_ntoa (clnt_sa->sin_addr), d->sessionid);
}


static void
gdm_xdmcp_send_decline (struct sockaddr_in *clnt_sa)
{
    XdmcpHeader header;
    ARRAY8 authentype;
    ARRAY8 authendata;
    ARRAY8 status;
    
    gdm_debug ("gdm_xdmcp_send_decline: Sending DECLINE to %s", 
	       inet_ntoa (clnt_sa->sin_addr));
    
    authentype.data = (CARD8 *) 0;
    authentype.length = (CARD16) 0;
    
    authendata.data = (CARD8 *) 0;
    authendata.length = (CARD16) 0;
    
    status.data = "Session refused";
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
    
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
}


static void 
gdm_xdmcp_handle_manage (struct sockaddr_in *clnt_sa, gint len)
{
    CARD32 clnt_sessid;
    CARD16 clnt_dspnum;
    static ARRAY8 clnt_dspclass;
    GdmDisplay *d;
    gint logfd;
    
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
    
    gdm_debug ("gdm_xdmcp_handle_manage: Got Display=%d, SessionID=%d from %s", 
	       clnt_dspnum, clnt_sessid, inet_ntoa (clnt_sa->sin_addr));
    
    /* Display Class */
    if (! XdmcpReadARRAY8 (&buf, &clnt_dspclass)) {
	gdm_error (_("gdm_xdmcp_handle_manage: Could not read Display Class"));
	return;
    }
    
    d = gdm_xdmcp_display_lookup (clnt_sessid);
    
    if (d && d->dispstat == XDMCP_PENDING) {
	gchar *logfile;

	gdm_debug ("gdm_xdmcp_handle_manage: Looked up %s", d->name);
	
	/* Log all output from spawned programs to a file */
	logfile = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);
	logfd = open (logfile, O_CREAT|O_TRUNC|O_APPEND|O_WRONLY, 0666);
	g_free (logfile);

	if (logfd != -1) {
	    dup2 (logfd, 1);
	    dup2 (logfd, 2);
            close (logfd);
	}
	else
	    gdm_error (_("gdm_xdmcp_handle_manage: Could not open logfile for display %s!"), d->name);
	
	d->dispstat = XDMCP_MANAGED;
	sessions++;
	pending--;
	
	/* Start greeter/session */
	if (!gdm_display_manage (d)) {
	    gdm_xdmcp_send_failed (clnt_sa, clnt_sessid);
	    return;
	}
    }
    else if (d && d->dispstat == XDMCP_MANAGED) {
	gdm_debug ("gdm_xdmcp_handle_manage: Session id %d already managed", clnt_sessid);	
    }
    else {
	gdm_debug ("gdm_xdmcp_handle_manage: Failed to look up session id %d", clnt_sessid);
	gdm_xdmcp_send_refuse (clnt_sa, clnt_sessid);
    }

    XdmcpDisposeARRAY8(&clnt_dspclass);
}


static void
gdm_xdmcp_send_refuse (struct sockaddr_in *clnt_sa, CARD32 sessid)
{
    XdmcpHeader header;
    
    gdm_debug ("gdm_xdmcp_send_refuse: Sending REFUSE to %d", sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode= (CARD16) REFUSE;
    header.length = 4;
    
    XdmcpWriteHeader (&buf, &header);  
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
}


static void
gdm_xdmcp_send_failed (struct sockaddr_in *clnt_sa, CARD32 sessid)
{
    XdmcpHeader header;
    ARRAY8 status;
    
    gdm_debug ("gdm_xdmcp_send_failed: Sending FAILED to %d", sessid);
    
    status.data = "Failed to start session";
    status.length = strlen (status.data);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) FAILED;
    header.length = 6+status.length;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpWriteARRAY8 (&buf, &status);
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
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
    
    gdm_debug ("Sending ALIVE to %d", sessid);
    
    header.version = XDM_PROTOCOL_VERSION;
    header.opcode = (CARD16) ALIVE;
    header.length = 5;
    
    XdmcpWriteHeader (&buf, &header);
    XdmcpWriteCARD8 (&buf, 1);
    XdmcpWriteCARD32 (&buf, sessid);
    XdmcpFlush (xdmcpfd, &buf, clnt_sa, sizeof (struct sockaddr_in));
}


static gboolean
gdm_xdmcp_host_allow (struct sockaddr_in *clnt_sa)
{
#ifdef HAVE_TCPWRAPPERS
    struct hostent *client_he;
    gchar *client;
    
    /* Find client hostname */
    client_he = gethostbyaddr ((gchar *) &clnt_sa->sin_addr,
			       sizeof (struct in_addr),
			       AF_INET);
    
    client = (client_he && client_he->h_name) ? client_he->h_name : NULL;
    
    /* Check with tcp_wrappers if client is allowed to access */
    return (hosts_ctl ("gdm", client ? client : "unknown", inet_ntoa (clnt_sa->sin_addr), ""));
#else /* HAVE_TCPWRAPPERS */
    return (TRUE);
#endif /* HAVE_TCPWRAPPERS */
}


static GdmDisplay *
gdm_xdmcp_display_alloc (struct sockaddr_in *clnt_sa, gint displaynum)
{
    GdmDisplay *d = NULL;
    struct hostent *client_he = NULL;
    
    d = g_malloc (sizeof (GdmDisplay));
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
    d->dispstat = XDMCP_PENDING;
    d->sessionid = globsessid++;
    d->acctime = time (NULL);
    d->dispnum = displaynum;
    
    /* Find client hostname */
    client_he = gethostbyaddr ((gchar *) &clnt_sa->sin_addr,
			       sizeof (struct in_addr),
			       AF_INET);
    
    if (client_he) {
	d->name = g_strdup_printf ("%s:%d", client_he->h_name, 
				   displaynum);
	d->hostname = g_strdup (client_he->h_name);
    }
    else {
	d->name = g_strdup_printf ("%s:%d", inet_ntoa (clnt_sa->sin_addr),
				   displaynum);
	d->hostname = g_strdup (inet_ntoa (clnt_sa->sin_addr));
    }
    
    /* Check if we are already talking to this host */
    gdm_xdmcp_display_dispose_check (d->name);
    
    /* Secure display with cookie */
    if (! gdm_auth_secure_display (d))
	gdm_error ("gdm_xdmcp_display_alloc: Error setting up cookies for %s", d->name);
    
    displays = g_slist_append (displays, d);
    
    pending++;
    
    gdm_debug ("gdm_xdmcp_display_alloc: display=%s, session id=%d, pending=%d ",
	       d->name, d->sessionid, pending);
    
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
gdm_xdmcp_display_dispose_check (gchar *name)
{
	GSList *dlist;

	if (name == NULL)
		return;

	gdm_debug ("gdm_xdmcp_display_dispose_check (%s)", name);

	dlist = displays;
	while (dlist != NULL) {
		GdmDisplay *d = dlist->data;

		if (d != NULL &&
		    strcmp (d->name, name) == 0) {
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
			gdm_debug ("gdm_xdmcp_displays_check: Disposing session id %d",
				   d->sessionid);
			gdm_display_dispose (d);

			/* restart as the list is now fucked */
			dlist = displays;
		} else {
			/* just go on */
			dlist = dlist->next;
		}
	}
}


/* EOF */
