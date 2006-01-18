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

/* This file contains the XDMCP chooser glue */

#include "config.h"

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
#include "gdmconfig.h"

static gint ipending = 0;
static GSList *indirect = NULL;

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
	int t;
	int status = 0;  /* 4 for IPv4 and 6 for IPv6  */
	struct in_addr addr;
#ifdef ENABLE_IPV6
	struct in6_addr addr6;
#endif
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

	if (p == NULL) {
		g_free (msg);
		return FALSE;
	}

	t = inet_pton (AF_INET, p, &addr);   /* Check if address is IPv4  */

	if (t > 0)
		status = AF_INET;  /* Yes, it's an IPv4 address */
	else
	{
		if (t == 0) {         /* It might be an IPv6 one */
#ifdef ENABLE_IPV6
			t = inet_pton (AF_INET6, p, &addr6);
#endif
			if (t > 0)
				status = AF_INET6;
		}
	}

	if (p == NULL || t <= 0) {
		g_free (msg);
		return FALSE;
	}

	g_free (msg);


#ifdef ENABLE_IPV6
	if (status == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_choose_data: got indirect id: %d address: %s", id, inet_ntop (AF_INET6, &addr6, buffer6, INET6_ADDRSTRLEN));
	}
	else
#endif
	{
		char buffer[INET_ADDRSTRLEN];

		gdm_debug ("gdm_choose_data: got indirect id: %d address: %s", id, inet_ntop (AF_INET, &addr, buffer, INET_ADDRSTRLEN));
	}

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *idisp = li->data;
		if (idisp->id == id) {
			/* whack the oldest if more then allowed */
			while (ipending >= gdm_get_value_int (GDM_KEY_MAX_INDIRECT) &&
			       remove_oldest_pending ())
				;

			idisp->acctime = time (NULL);
#ifdef ENABLE_IPV6
			if (status == AF_INET6) {
				g_free (idisp->chosen_host6);
				idisp->chosen_host6 = g_new (struct in6_addr, 1);
				memcpy (idisp->chosen_host6, &addr6, sizeof (struct in6_addr));
			}
			else
#endif
			{
				g_free (idisp->chosen_host);
				idisp->chosen_host = g_new (struct in_addr, 1);
				memcpy (idisp->chosen_host, &addr, sizeof (struct in_addr));
			}

			/* Now this display is pending */
			ipending++;

			return TRUE;
		}
	}

	return FALSE;
}


GdmIndirectDisplay *
#ifdef ENABLE_IPV6
gdm_choose_indirect_alloc (struct sockaddr_storage *clnt_sa)
#else
gdm_choose_indirect_alloc (struct sockaddr_in *clnt_sa)
#endif
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
#ifdef ENABLE_IPV6
    id->dsp_sa = g_new0 (struct sockaddr_storage, 1);
    memcpy (id->dsp_sa, clnt_sa, sizeof (struct sockaddr_storage));
    id->chosen_host6 = NULL;
#else
    id->dsp_sa = g_new0 (struct sockaddr_in, 1);
    memcpy (id->dsp_sa, clnt_sa, sizeof (struct sockaddr_in));
#endif
    id->acctime = 0;
    id->chosen_host = NULL;
    
    indirect = g_slist_prepend (indirect, id);
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	    char buffer6[INET6_ADDRSTRLEN];

	    gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)(id->dsp_sa))->sin6_addr), buffer6, INET6_ADDRSTRLEN), ipending);
    }
    else
#endif
    {
	    char buffer[INET_ADDRSTRLEN];

	    gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ", inet_ntop (AF_INET, &((struct sockaddr_in *)(id->dsp_sa))->sin_addr, buffer, INET_ADDRSTRLEN), ipending);
    }

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
			if (
#ifdef ENABLE_IPV6
			    (idisp->chosen_host6 == NULL) ||
#endif
			    (idisp->chosen_host == NULL))
				gdm_choose_indirect_dispose (idisp);
			return;
		}
	}
}

#ifdef ENABLE_IPV6
GdmIndirectDisplay *
gdm_choose_indirect_lookup_by_chosen6 (struct in6_addr *chosen,
                                     struct in6_addr *origin)
{
	GSList *li;
	char buffer6[INET6_ADDRSTRLEN];

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *id = li->data;

		if (id != NULL &&
                   id->chosen_host6 != NULL &&
                   id->chosen_host6->s6_addr == chosen->s6_addr) {

			if (((struct sockaddr_in6 *)(id->dsp_sa))->sin6_addr.s6_addr == origin->s6_addr) {
				return id;
			} else if (gdm_is_loopback_addr6 (&(((struct sockaddr_in6 *)(id->dsp_sa))->sin6_addr)) && gdm_is_local_addr6 (origin)) {
				return id;
			}
		}
	}
   
	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Chosen %s host not found", inet_ntop (AF_INET6, chosen, buffer6, INET6_ADDRSTRLEN));

	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Origin was: %s", inet_ntop (AF_INET6, origin, buffer6, INET6_ADDRSTRLEN));

	return NULL;
}
#endif


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
			if (((struct sockaddr_in *)(id->dsp_sa))->sin_addr.s_addr == origin->s_addr) {
				return id;
			} else if (gdm_is_loopback_addr (&(((struct sockaddr_in *)(id->dsp_sa))->sin_addr)) &&
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
#ifdef ENABLE_IPV6
gdm_choose_indirect_lookup (struct sockaddr_storage *clnt_sa)
#else
gdm_choose_indirect_lookup (struct sockaddr_in *clnt_sa)
#endif
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
	    curtime > id->acctime + gdm_get_value_int (GDM_KEY_MAX_WAIT_INDIRECT)) {
#ifdef ENABLE_IPV6
	    if (clnt_sa->ss_family == AF_INET6) {
		char buffer6[INET6_ADDRSTRLEN];

		gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)clnt_sa)->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	    }
	    else
#endif
	    {
		char buffer[INET_ADDRSTRLEN];

		gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s", inet_ntop (AF_INET, &((struct sockaddr_in *)clnt_sa)->sin_addr, buffer, INET_ADDRSTRLEN));
	    }

	    gdm_choose_indirect_dispose (id);
	    continue;
	}
	
#ifdef ENABLE_IPV6
	if (clnt_sa->ss_family == AF_INET6) {
	    if (memcmp (((struct sockaddr_in6 *)(id->dsp_sa))->sin6_addr.s6_addr, ((struct sockaddr_in6 *)clnt_sa)->sin6_addr.s6_addr, sizeof (struct in6_addr)) == 0) {
		g_slist_free (ilist);
		return id;
	    }
	}
	else
#endif
	{
	    if (((struct sockaddr_in *)(id->dsp_sa))->sin_addr.s_addr == ((struct sockaddr_in *)clnt_sa)->sin_addr.s_addr) {
		g_slist_free (ilist);
		return id;
	    }
	}
    }
    g_slist_free (ilist);
    
#ifdef ENABLE_IPV6
    if (clnt_sa->ss_family == AF_INET6) {
	char buffer6[INET6_ADDRSTRLEN];

	gdm_debug ("gdm_choose_indirect_lookup: Host %s not found", inet_ntop (AF_INET6, &((struct sockaddr_in6 *)clnt_sa)->sin6_addr, buffer6, INET6_ADDRSTRLEN));
    }
    else
#endif
    {
	char buffer[INET_ADDRSTRLEN];

	gdm_debug ("gdm_choose_indirect_lookup: Host %s not found", inet_ntop (AF_INET, &((struct sockaddr_in *)clnt_sa)->sin_addr, buffer, INET_ADDRSTRLEN));
    }

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

#ifdef ENABLE_IPV6
    if (id->dsp_sa->ss_family == AF_INET6) {
	    char buffer6[INET6_ADDRSTRLEN];

	    gdm_debug ("gdm_choose_indirect_dispose: Disposing %s", inet_ntop (AF_INET6, &(((struct sockaddr_in6 *)(id->dsp_sa))->sin6_addr), buffer6, INET6_ADDRSTRLEN));
	    g_free (id->chosen_host6);
	    id->chosen_host6 = NULL;
    }
    else
#endif
    {
	    char buffer[INET_ADDRSTRLEN];

	    gdm_debug ("gdm_choose_indirect_dispose: Disposing %s", inet_ntop (AF_INET, &((struct sockaddr_in *)(id->dsp_sa))->sin_addr,
buffer, INET_ADDRSTRLEN));
	    g_free (id->chosen_host);
	    id->chosen_host = NULL;
    }

    g_free (id->dsp_sa);
    id->dsp_sa = NULL;

    g_free (id);
}


/* EOF */
