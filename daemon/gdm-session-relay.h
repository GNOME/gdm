/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef __GDM_SESSION_SERVER_H
#define __GDM_SESSION_SERVER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_RELAY         (gdm_session_relay_get_type ())
#define GDM_SESSION_RELAY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SESSION_RELAY, GdmSessionRelay))
#define GDM_SESSION_RELAY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SESSION_RELAY, GdmSessionRelayClass))
#define GDM_IS_SESSION_RELAY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SESSION_RELAY))
#define GDM_IS_SESSION_RELAY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SESSION_RELAY))
#define GDM_SESSION_RELAY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SESSION_RELAY, GdmSessionRelayClass))

typedef struct GdmSessionRelayPrivate GdmSessionRelayPrivate;

typedef struct
{
	GObject	                parent;
	GdmSessionRelayPrivate *priv;
} GdmSessionRelay;

typedef struct
{
	GObjectClass   parent_class;

	void (* user_verified)           (GdmSessionRelay   *session_relay);

	void (* user_verification_error) (GdmSessionRelay   *session_relay,
					  GError            *error);

	void (* info_query)              (GdmSessionRelay   *session_relay,
					  const char        *query_text);

	void (* secret_info_query)       (GdmSessionRelay   *session_relay,
					  const char        *query_text);

	void (* info)                    (GdmSessionRelay   *session_relay,
					  const char        *info);

	void (* problem)                 (GdmSessionRelay   *session_relay,
					  const char        *problem);

	void (* session_started)         (GdmSessionRelay   *session_relay,
					  GPid               pid);

	void (* session_startup_error)   (GdmSessionRelay   *session_relay,
					  GError            *error);

	void (* session_exited)          (GdmSessionRelay   *session_relay,
					  int                exit_code);

	void (* session_died)            (GdmSessionRelay   *session_relay,
					  int                signal_number);

	void (* ready)                   (GdmSessionRelay  *session_relay);
	void (* connected)               (GdmSessionRelay  *session_relay);
	void (* disconnected)            (GdmSessionRelay  *session_relay);

} GdmSessionRelayClass;

GType		   gdm_session_relay_get_type          (void);
GdmSessionRelay *  gdm_session_relay_new	       (void);

void               gdm_session_relay_answer_query      (GdmSessionRelay *session_relay,
							const char      *text);
void               gdm_session_relay_open              (GdmSessionRelay *session_relay);

void               gdm_session_relay_select_session    (GdmSessionRelay *session_relay,
							const char      *session);
void               gdm_session_relay_select_language   (GdmSessionRelay *session_relay,
							const char      *language);
void               gdm_session_relay_select_user       (GdmSessionRelay *session_relay,
							const char      *user);
void               gdm_session_relay_reset             (GdmSessionRelay *session_relay);

gboolean           gdm_session_relay_start             (GdmSessionRelay *session_relay);
gboolean           gdm_session_relay_stop              (GdmSessionRelay *session_relay);
char *             gdm_session_relay_get_address       (GdmSessionRelay *session_relay);

G_END_DECLS

#endif /* __GDM_SESSION_RELAY_H */
