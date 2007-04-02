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

/* This file contains the XDMCP chooser glue */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
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

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-daemon-config.h"

#include "gdm-socket-protocol.h"

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

static gboolean
get_first_address_for_node (const char               *node,
			    struct sockaddr_storage **sa)
{
	struct addrinfo  hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai;
	int              gaierr;
	gboolean         found;
	char             strport[NI_MAXSERV];

	found = FALSE;

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = AF_UNSPEC;

	snprintf (strport, sizeof (strport), "%u", XDM_UDP_PORT);

	if ((gaierr = getaddrinfo (node, strport, &hints, &ai_list)) != 0) {
		g_warning ("Unable get address: %s", gai_strerror (gaierr));
		return FALSE;
	}

	for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
			continue;
		}
#ifndef ENABLE_IPV6
		if (ai->ai_family == AF_INET6) {
			continue;
		}
#endif
		found = TRUE;
		break;
	}

	if (ai != NULL) {
		if (sa != NULL) {
			*sa = g_memdup (ai->ai_addr, ai->ai_addrlen);
		}
	}

	freeaddrinfo (ai_list);

	return found;
}

gboolean
gdm_choose_data (const char *data)
{
	int id;
	struct sockaddr_storage *sa;
	GSList *li;
	char *msg;
	char *p;
	char *host;
	gboolean ret;

	msg = g_strdup (data);
	sa = NULL;
	ret = FALSE;

	p = strtok (msg, " ");
	if (p == NULL || strcmp (GDM_SOP_CHOSEN, p) != 0) {
		goto out;
	}

	p = strtok (NULL, " ");
	if (p == NULL || sscanf (p, "%d", &id) != 1) {
		goto out;
	}

	p = strtok (NULL, " ");

	if (p == NULL) {
		goto out;
	}

	if (! get_first_address_for_node (p, &sa)) {
		goto out;
	}

	gdm_address_get_info (sa, &host, NULL);
	gdm_debug ("gdm_choose_data: got indirect id: %d address: %s",
		   id,
		   host);
	g_free (host);

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *idisp = li->data;
		if (idisp->id == id) {
			/* whack the oldest if more then allowed */
			while (ipending >= gdm_daemon_config_get_value_int (GDM_KEY_MAX_INDIRECT) &&
			       remove_oldest_pending ())
				;

			idisp->acctime = time (NULL);

			g_free (idisp->chosen_host);
			idisp->chosen_host = g_memdup (sa, sizeof (struct sockaddr_storage));

			/* Now this display is pending */
			ipending++;

			ret = TRUE;
			break;
		}
	}
 out:
	g_free (sa);
	g_free (msg);

	return ret;
}


GdmIndirectDisplay *
gdm_choose_indirect_alloc (struct sockaddr_storage *clnt_sa)
{
	GdmIndirectDisplay *id;
	char *host;

	if (clnt_sa == NULL)
		return NULL;

	id = g_new0 (GdmIndirectDisplay, 1);
	id->id = indirect_id++;
	/* deal with a rollover, that will NEVER EVER happen,
	 * but I'm a paranoid bastard */
	if (id->id == 0)
	    id->id = indirect_id++;

	id->dsp_sa = g_memdup (clnt_sa, sizeof (struct sockaddr_storage));
	id->chosen_host = NULL;

	id->acctime = 0;

	indirect = g_slist_prepend (indirect, id);

	gdm_address_get_info (id->dsp_sa, &host, NULL);

	gdm_debug ("gdm_choose_display_alloc: display=%s, pending=%d ",
		   host,
		   ipending);
	g_free (host);

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
gdm_choose_indirect_lookup_by_chosen (struct sockaddr_storage *chosen,
				      struct sockaddr_storage *origin)
{
	GSList *li;
	char *host;

	for (li = indirect; li != NULL; li = li->next) {
		GdmIndirectDisplay *id = li->data;

		if (id != NULL &&
		    id->chosen_host != NULL &&
		    gdm_address_equal (id->chosen_host, chosen)) {
			if (gdm_address_equal (id->dsp_sa, origin)) {
				return id;
			} else if (gdm_address_is_loopback (id->dsp_sa) &&
				   gdm_address_is_local (origin)) {
				return id;
			}
		}
	}

	gdm_address_get_info (chosen, &host, NULL);

	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Chosen %s host not found",
		   host);
	gdm_debug ("gdm_choose_indirect_lookup_by_chosen: Origin was: %s",
		   host);
	g_free (host);

	return NULL;
}


GdmIndirectDisplay *
gdm_choose_indirect_lookup (struct sockaddr_storage *clnt_sa)
{
	GSList *li, *ilist;
	GdmIndirectDisplay *id;
	time_t curtime = time (NULL);
	char *host;

	ilist = g_slist_copy (indirect);

	for (li = ilist; li != NULL; li = li->next) {
		id = (GdmIndirectDisplay *) li->data;
		if (id == NULL)
			continue;

		if (id->acctime > 0 &&
		    curtime > id->acctime + gdm_daemon_config_get_value_int (GDM_KEY_MAX_WAIT_INDIRECT)) {

			gdm_address_get_info (clnt_sa, &host, NULL);
			gdm_debug ("gdm_choose_indirect_check: Disposing stale INDIRECT query from %s",
				   host);
			g_free (host);

			gdm_choose_indirect_dispose (id);
			continue;
		}

		if (gdm_address_equal (id->dsp_sa, clnt_sa)) {
			g_slist_free (ilist);
			return id;
		}
	}
	g_slist_free (ilist);

	gdm_address_get_info (clnt_sa, &host, NULL);
	gdm_debug ("gdm_choose_indirect_lookup: Host %s not found",
		   host);
	g_free (host);

	return NULL;
}


void
gdm_choose_indirect_dispose (GdmIndirectDisplay *id)
{
	char *host;

	if (id == NULL)
		return;

	indirect = g_slist_remove (indirect, id);

	if (id->acctime > 0)
		ipending--;
	id->acctime = 0;

	gdm_address_get_info (id->dsp_sa, &host, NULL);
	gdm_debug ("gdm_choose_indirect_dispose: Disposing %s",
		   host);
	g_free (host);

	g_free (id->chosen_host);
	id->chosen_host = NULL;

	g_free (id->dsp_sa);
	id->dsp_sa = NULL;

	g_free (id);
}
