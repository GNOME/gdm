/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * session.h - authenticates and authorizes users with system
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef GDM_SESSION_H
#define GDM_SESSION_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION (gdm_session_get_type ())
#define GDM_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_SESSION, GdmSession))
#define GDM_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_SESSION, GdmSessionClass))
#define GDM_IS_SESSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_SESSION))
#define GDM_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_SESSION))
#define GDM_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_SESSION, GdmSessionClass))
#define GDM_SESSION_ERROR (gdm_session_error_quark ())

typedef enum _GdmSessionError GdmSessionError;

typedef struct _GdmSessionPrivate GdmSessionPrivate;

typedef struct
{
	GObject parent;

	/*< private > */
	GdmSessionPrivate *priv;
} GdmSession;

typedef struct
{
	GObjectClass parent_class;

	/* signals */
	void (* user_verified)           (GdmSession        *session);

	void (* user_verification_error) (GdmSession        *session,
					  GError            *error);

	void (* info_query)              (GdmSession        *session,
					  const char        *query_text);

	void (* secret_info_query)       (GdmSession        *session,
					  const char        *query_text);

	void (* info)                    (GdmSession        *session,
					  const char        *info);

	void (* problem)                 (GdmSession        *session,
					  const char        *problem);

	void (* session_started)         (GdmSession        *session,
					  GPid               pid);

	void (* session_startup_error)   (GdmSession        *session,
					  GError            *error);

	void (* session_exited)          (GdmSession        *session,
					  int                exit_code);

	void (* session_died)            (GdmSession        *session,
					  int                signal_number);
} GdmSessionClass;

enum _GdmSessionError {
	GDM_SESSION_ERROR_GENERIC = 0,
	GDM_SESSION_ERROR_WITH_SESSION_COMMAND,
	GDM_SESSION_ERROR_FORKING,
	GDM_SESSION_ERROR_OPENING_MESSAGE_PIPE,
	GDM_SESSION_ERROR_COMMUNICATING,
	GDM_SESSION_ERROR_WORKER_DIED,
	GDM_SESSION_ERROR_AUTHENTICATING,
	GDM_SESSION_ERROR_AUTHORIZING,
	GDM_SESSION_ERROR_OPENING_LOG_FILE,
	GDM_SESSION_ERROR_OPENING_SESSION,
	GDM_SESSION_ERROR_GIVING_CREDENTIALS
};

GType        gdm_session_get_type                 (void);
GQuark       gdm_session_error_quark              (void);

GdmSession * gdm_session_new                      (void) G_GNUC_MALLOC;

gboolean     gdm_session_open                     (GdmSession  *session,
						   const char *service_name,
						   const char *hostname,
						   const char *console_name,
						   int standard_output_fd,
						   int standard_error_fd,
						   GError   **error);

gboolean     gdm_session_open_for_user            (GdmSession  *session,
						   const char  *service_name,
						   const char  *username,
						   const char  *hostname,
						   const char  *console_name,
						   int standard_output_fd,
						   int standard_error_fd,
						   GError      **error);
void         gdm_session_start_program            (GdmSession *session,
						   const char * const * args);

void         gdm_session_set_environment_variable (GdmSession  *session,
						   const char *key,
						   const char *value);

void         gdm_session_answer_query             (GdmSession  *session,
						   const char *answer);

char       * gdm_session_get_username             (GdmSession *session);

void         gdm_session_close                    (GdmSession *session);
gboolean     gdm_session_is_running               (GdmSession *session);

G_END_DECLS

#endif /* GDM_SESSION_H */
