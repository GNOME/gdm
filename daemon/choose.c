/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@mkp.net>
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

/* This file contains the XDMCP chooser glue */

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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";

gint ipending = 0;
GSList *indirect = NULL;
GSList *i2 = NULL;

/* Tunables */
extern gint GdmMaxIndirect;	/* Maximum pending indirects, i.e. simultaneous choosing sessions */
extern gint GdmMaxIndirectWait;	/* Maximum age before a pending session is removed from the list */

extern void gdm_debug (gchar *, ...);


GdmIndirectDisplay *gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa);
GdmIndirectDisplay *gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa);
static void gdm_choose_indirect_dispose (GdmIndirectDisplay *);
gboolean gdm_choose_socket_handler (GIOChannel *source, GIOCondition cond, gint fd);


gboolean
gdm_choose_socket_handler (GIOChannel *source, GIOCondition cond, gint fd)
{
    gchar buf[PIPE_SIZE];
    gint len;

    if (cond != G_IO_IN) 
	return (TRUE);

    g_io_channel_read (source, buf, PIPE_SIZE-1, &len);
    buf[len-1] = '\0';

    gdm_debug ("gdm_choose_socket_handler: Read `%s'", buf);

    /* gdm_choose_indirect_alloc (); */

    return (TRUE);
}


GdmIndirectDisplay *
gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa)
{
    GdmIndirectDisplay *id;

    if (!clnt_sa)
	return (NULL);

    id = g_new0 (GdmIndirectDisplay, 1);

    if (!id)
	return (NULL);

    id->dsp_sa = g_new0 (struct sockaddr_in, 1);
    memcpy (id->dsp_sa, clnt_sa, sizeof (struct sockaddr_in));
    id->acctime = time (NULL);
    
    indirect = g_slist_append (indirect, id);
    ipending++;
    
    gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ",
	       inet_ntoa (id->dsp_sa->sin_addr), ipending);

    return (id);
}


GdmIndirectDisplay *
gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa)
{
    GSList *ilist = indirect;
    GdmIndirectDisplay *id;
    
    while (ilist) {
        id = (GdmIndirectDisplay *) ilist->data;

	if (id && time (NULL) > id->acctime + GdmMaxIndirectWait)	{
	    gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s",
		       inet_ntoa (clnt_sa->sin_addr));
	    gdm_choose_indirect_dispose (id);
	}
	
	if (id && id->dsp_sa->sin_addr.s_addr == clnt_sa->sin_addr.s_addr)
	return (id);
	
        ilist = ilist->next;
    }
    
    gdm_debug ("gdm_choose_indirect_lookup: Host %s not found", 
	       inet_ntoa (clnt_sa->sin_addr));

    return (NULL);
}


static void
gdm_choose_indirect_dispose (GdmIndirectDisplay *id)
{
    if (!id)
	return;

    gdm_debug ("gdm_choose_indirect_dispose: Disposing %d", 
	       inet_ntoa (id->dsp_sa->sin_addr));

    if (id->dsp_sa)
	g_free (id->dsp_sa);

    g_free (id);

    ipending--;
}


/* EOF */
