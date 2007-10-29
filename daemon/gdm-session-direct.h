/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __GDM_SESSION_DIRECT_H
#define __GDM_SESSION_DIRECT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_DIRECT (gdm_session_direct_get_type ())
#define GDM_SESSION_DIRECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_SESSION_DIRECT, GdmSessionDirect))
#define GDM_SESSION_DIRECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_SESSION_DIRECT, GdmSessionDirectClass))
#define GDM_IS_SESSION_DIRECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_SESSION_DIRECT))
#define GDM_IS_SESSION_DIRECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_SESSION_DIRECT))
#define GDM_SESSION_DIRECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_SESSION_DIRECT, GdmSessionDirectClass))
#define GDM_SESSION_DIRECT_ERROR (gdm_session_direct_error_quark ())

typedef struct _GdmSessionDirectPrivate GdmSessionDirectPrivate;

typedef struct
{
        GObject parent;

        /*< private > */
        GdmSessionDirectPrivate *priv;
} GdmSessionDirect;

typedef struct
{
        GObjectClass parent_class;

        /* signals */
        void (* opened)                  (GdmSessionDirect        *session_direct);
        void (* closed)                  (GdmSessionDirect        *session_direct);

        void (* user_verified)           (GdmSessionDirect        *session_direct);

        void (* user_verification_error) (GdmSessionDirect        *session_direct,
                                          GError            *error);

        void (* info_query)              (GdmSessionDirect        *session_direct,
                                          const char        *query_text);

        void (* secret_info_query)       (GdmSessionDirect        *session_direct,
                                          const char        *query_text);

        void (* info)                    (GdmSessionDirect        *session_direct,
                                          const char        *info);

        void (* problem)                 (GdmSessionDirect        *session_direct,
                                          const char        *problem);

        void (* session_started)         (GdmSessionDirect        *session_direct,
                                          GPid               pid);

        void (* session_startup_error)   (GdmSessionDirect        *session_direct,
                                          GError            *error);

        void (* session_exited)          (GdmSessionDirect        *session_direct,
                                          int                exit_code);

        void (* session_died)            (GdmSessionDirect        *session_direct,
                                          int                signal_number);
} GdmSessionDirectClass;

typedef enum _GdmSessionDirectError {
        GDM_SESSION_DIRECT_ERROR_GENERIC = 0,
        GDM_SESSION_DIRECT_ERROR_WITH_SESSION_DIRECT_COMMAND,
        GDM_SESSION_DIRECT_ERROR_FORKING,
        GDM_SESSION_DIRECT_ERROR_COMMUNICATING,
        GDM_SESSION_DIRECT_ERROR_WORKER_DIED,
        GDM_SESSION_DIRECT_ERROR_AUTHENTICATING,
        GDM_SESSION_DIRECT_ERROR_AUTHORIZING,
        GDM_SESSION_DIRECT_ERROR_OPENING_LOG_FILE,
        GDM_SESSION_DIRECT_ERROR_OPENING_SESSION_DIRECT,
        GDM_SESSION_DIRECT_ERROR_GIVING_CREDENTIALS
} GdmSessionDirectError;

GType              gdm_session_direct_get_type                 (void);
GQuark             gdm_session_direct_error_quark              (void);

GdmSessionDirect * gdm_session_direct_new                     (void) G_GNUC_MALLOC;

gboolean           gdm_session_direct_open                     (GdmSessionDirect    *session_direct,
                                                                const char    *service_name,
                                                                const char    *hostname,
                                                                const char    *x11_display_name,
                                                                const char    *console_name,
                                                                GError       **error);
void               gdm_session_direct_close                    (GdmSessionDirect     *session_direct);
gboolean           gdm_session_direct_begin_verification       (GdmSessionDirect     *session_direct,
                                                                const char     *username,
                                                                GError        **error);
void               gdm_session_direct_start_program            (GdmSessionDirect     *session_direct,
                                                                const char     *command);
void               gdm_session_direct_set_environment_variable (GdmSessionDirect     *session_direct,
                                                                const char     *key,
                                                                const char     *value);

void               gdm_session_direct_answer_query             (GdmSessionDirect     *session_direct,
                                                                const char     *answer);

char             * gdm_session_direct_get_username             (GdmSessionDirect     *session_direct);

gboolean           gdm_session_direct_is_running               (GdmSessionDirect     *session_direct);

G_END_DECLS

#endif /* GDM_SESSION_DIRECT_H */
