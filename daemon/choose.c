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


static GdmIndirectDisplay *gdm_choose_indirect_alloc (struct in_addr *);
static GdmIndirectDisplay *gdm_choose_indirect_lookup (struct in_addr *);
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

    return (TRUE);
}


static GdmIndirectDisplay *
gdm_choose_indirect_alloc (struct in_addr *addr)
{
    GdmIndirectDisplay *i;

    if (!addr)
	return (NULL);

    i = g_new0 (GdmIndirectDisplay, 1);

    if (!i)
	return (NULL);

    i->addr->s_addr = addr->s_addr;
    i->dispnum = -1;
    i->manager = NULL;
    i->acctime = time (NULL);
    
    indirect = g_slist_append (indirect, i);
    ipending++;
    
    gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ",
	       inet_ntoa (*i->addr), ipending);
    
    return (i);
}


static GdmIndirectDisplay *
gdm_choose_indirect_lookup (struct in_addr *addr)
{
    GSList *ilist = indirect;
    GdmIndirectDisplay *i;
    
    while (ilist) {
        i = (GdmIndirectDisplay *) ilist->data;

	if (i && time (NULL) > i->acctime + GdmMaxIndirectWait)	{
	    gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s",
		       inet_ntoa (*i->addr));
	    gdm_choose_indirect_dispose (i);
	}
	
        if (i && i->addr->s_addr == addr->s_addr)
            return (i);
	
        ilist = ilist->next;
    }

    gdm_debug ("gdm_choose_indirect_lookup: Host %s not found", 
	       inet_ntoa (*addr));

    return (NULL);
}


static void
gdm_choose_indirect_dispose (GdmIndirectDisplay *i)
{
    gdm_debug ("gdm_choose_indirect_dispose: Disposing %s", inet_ntoa (*i->addr));

    if (i->addr)
	g_free (i->addr);

    if (i->manager)
	g_free (i->manager);

    g_free (i);

    ipending--;
}


/* EOF */
