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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>

#include "gdm.h"
#include "misc.h"
#include "choose.h"
#include "xdmcp.h"

static gint ipending = 0;
static GSList *indirect = NULL;

/* Tunables */
extern gint GdmMaxIndirect;	/* Maximum pending indirects, i.e. simultaneous choosing sessions */
extern gint GdmMaxIndirectWait;	/* Maximum age before a pending session is removed from the list */

static guint indirect_id = 1;

static gboolean
remove_oldest_pending (void)
{
	GSList *li;
	GdmIndirectDisplay *oldest = NULL;

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *idisp = li->data;
		if (idisp->acctime == 0)
			continue;

		if (oldest == NULL ||
		    idisp->acctime < oldest->acctime) {
			oldest = idisp;
		}
	}

	if (oldest != NULL) {
		gdm_choose_indirect_dispose (oldest);
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
gdm_choose_data (const char *data)
{
	int id;
	struct in_addr addr;
	GSList *li;
	char *msg = g_strdup (data);
	char *p;

	p = strtok (msg, " ");
	if (p == NULL || strcmp (GDM_SOP_CHOSEN, p) != 0) {
		g_free (msg);
		return FALSE;
	}

	p = strtok (NULL, " ");
	if (p == NULL || sscanf (p, "%d", &id) != 1) {
		g_free (msg);
		return FALSE;
	}

	p = strtok (NULL, " ");
	if (p == NULL || inet_aton (p, &addr) == 0) {
		g_free (msg);
		return FALSE;
	}

	g_free (msg);

	gdm_debug ("gdm_choose_data: got indirect id: %d address: %s",
		   id, inet_ntoa (addr));

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *idisp = li->data;
		if (idisp->id == id) {
			/* whack the oldest if more then allowed */
			while (ipending >= GdmMaxIndirect &&
			       remove_oldest_pending ())
				;

			idisp->acctime = time (NULL);
			g_free (idisp->chosen_host);
			idisp->chosen_host = g_new (struct in_addr, 1);
			memcpy (idisp->chosen_host, &addr,
				sizeof (struct in_addr));

			/* Now this display is pending */
			ipending++;

			return TRUE;
		}
	}

	return FALSE;
}


GdmIndirectDisplay *
gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa)
{
    GdmIndirectDisplay *id;

    if (clnt_sa == NULL)
	    return NULL;

    id = g_new0 (GdmIndirectDisplay, 1);
    id->id = indirect_id++;
    /* deal with a rollover, that will NEVER EVER happen,
     * but I'm a paranoid bastard */
    if (id->id == 0)
	    id->id = indirect_id++;
    id->dsp_sa = g_new0 (struct sockaddr_in, 1);
    memcpy (id->dsp_sa, clnt_sa, sizeof (struct sockaddr_in));
    id->acctime = 0;
    id->chosen_host = NULL;
    
    indirect = g_slist_prepend (indirect, id);
    
    gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ",
	       inet_ntoa (id->dsp_sa->sin_addr), ipending);

    return (id);
}

/* dispose of indirect display of id, if no host is set */
void
gdm_choose_indirect_dispose_empty_id (guint id)
{
	GSList *li;
	
	if (id == 0)
		return;

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *idisp = li->data;

		if (idisp == NULL)
			continue;

		if (idisp->id == id) {
			if (idisp->chosen_host == NULL)
				gdm_choose_indirect_dispose (idisp);
			return;
		}
	}
}

GdmIndirectDisplay *
gdm_choose_indirect_lookup_by_chosen (struct in_addr *chosen,
				      struct in_addr *origin)
{
	GSList *li;

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *id = li->data;
		if (id != NULL &&
		    id->chosen_host != NULL &&
		    id->chosen_host->s_addr == chosen->s_addr) {
			if (id->dsp_sa->sin_addr.s_addr == origin->s_addr) {
				return id;
			} else if (gdm_is_loopback_addr (&(id->dsp_sa->sin_addr)) &&
				   gdm_is_local_addr (origin)) {
				return id;
			}
		}
	}
    
	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Chosen %s host not found",
		   inet_ntoa (*chosen));
	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Origin was: %s",
		   inet_ntoa (*origin));

	return NULL;
}


GdmIndirectDisplay *
gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa)
{
    GSList *li, *ilist;
    GdmIndirectDisplay *id;
    time_t curtime = time (NULL);

    ilist = g_slist_copy (indirect);
    
    for (li = ilist; li != NULL; li = li->next) {
        id = (GdmIndirectDisplay *) li->data;
	if (id == NULL)
		continue;

	if (id->acctime > 0 &&
	    curtime > id->acctime + GdmMaxIndirectWait)	{
	    gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s",
		       inet_ntoa (clnt_sa->sin_addr));
	    gdm_choose_indirect_dispose (id);
	}
	
	if (id->dsp_sa->sin_addr.s_addr == clnt_sa->sin_addr.s_addr) {
		g_slist_free (ilist);
		return id;
	}
    }
    g_slist_free (ilist);
    
    gdm_debug ("gdm_choose_indirect_lookup: Host %s not found", 
	       inet_ntoa (clnt_sa->sin_addr));

    return NULL;
}


void
gdm_choose_indirect_dispose (GdmIndirectDisplay *id)
{
    if (id == NULL)
	return;

    indirect = g_slist_remove (indirect, id);

    if (id->acctime > 0)
	    ipending--;
    id->acctime = 0;

    gdm_debug ("gdm_choose_indirect_dispose: Disposing %s", 
	       inet_ntoa (id->dsp_sa->sin_addr));

    g_free (id->dsp_sa);
    id->dsp_sa = NULL;
    g_free (id->chosen_host);
    id->chosen_host = NULL;

    g_free (id);
}


/* EOF */
