/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-session.h"
#include "gdm-session-private.h"

enum {
        USER_VERIFIED = 0,
        USER_VERIFICATION_ERROR,
        INFO,
        PROBLEM,
        INFO_QUERY,
        SECRET_INFO_QUERY,
        SESSION_STARTED,
        SESSION_STARTUP_ERROR,
        SESSION_EXITED,
        SESSION_DIED,
        OPENED,
        CLOSED,
        CONNECTED,
        DISCONNECTED,
        SELECTED_USER_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void gdm_session_class_init (gpointer g_iface);

GType
gdm_session_get_type (void)
{
        static GType session_type = 0;

        if (!session_type) {
                session_type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                              "GdmSession",
                                                              sizeof (GdmSessionIface),
                                                              (GClassInitFunc) gdm_session_class_init,
                                                              0, NULL, 0);

                g_type_interface_add_prerequisite (session_type, G_TYPE_OBJECT);
        }

        return session_type;
}

void
gdm_session_open (GdmSession *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->open (session);
}

void
gdm_session_close (GdmSession *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->close (session);
}

void
gdm_session_begin_verification (GdmSession *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->begin_verification (session);
}

void
gdm_session_begin_verification_for_user (GdmSession *session,
                                         const char *username)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->begin_verification_for_user (session, username);
}

void
gdm_session_answer_query (GdmSession *session,
                          const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->answer_query (session, text);
}

void
gdm_session_select_session (GdmSession *session,
                            const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->select_session (session, text);
}

void
gdm_session_select_language (GdmSession *session,
                             const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->select_language (session, text);
}

void
gdm_session_select_user (GdmSession *session,
                         const char *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->select_user (session, text);
}

void
gdm_session_cancel (GdmSession *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->cancel (session);
}

void
gdm_session_start_session (GdmSession *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        GDM_SESSION_GET_IFACE (session)->start_session (session);
}

static void
gdm_session_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [USER_VERIFIED] =
                g_signal_new ("user-verified",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, user_verified),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [USER_VERIFICATION_ERROR] =
                g_signal_new ("user-verification-error",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, user_verification_error),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
         signals [INFO_QUERY] =
                g_signal_new ("info-query",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SECRET_INFO_QUERY] =
                g_signal_new ("secret-info-query",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, secret_info_query),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [INFO] =
                g_signal_new ("info",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, info),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [PROBLEM] =
                g_signal_new ("problem",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, problem),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SESSION_STARTED] =
                g_signal_new ("session-started",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, session_started),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [SESSION_STARTUP_ERROR] =
                g_signal_new ("session-startup-error",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, session_startup_error),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        signals [SESSION_EXITED] =
                g_signal_new ("session-exited",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, session_exited),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [SESSION_DIED] =
                g_signal_new ("session-died",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, session_died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [OPENED] =
                g_signal_new ("opened",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, opened),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [CLOSED] =
                g_signal_new ("closed",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, closed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [SELECTED_USER_CHANGED] =
                g_signal_new ("selected-user-changed",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSessionIface, selected_user_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

}

void
_gdm_session_user_verified (GdmSession   *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));

        g_signal_emit (session, signals [USER_VERIFIED], 0);
}

void
_gdm_session_user_verification_error (GdmSession   *session,
                                      const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [USER_VERIFICATION_ERROR], 0, text);
}

void
_gdm_session_info_query (GdmSession   *session,
                         const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [INFO_QUERY], 0, text);
}

void
_gdm_session_secret_info_query (GdmSession   *session,
                                const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SECRET_INFO_QUERY], 0, text);
}

void
_gdm_session_info (GdmSession   *session,
                   const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [INFO], 0, text);
}

void
_gdm_session_problem (GdmSession   *session,
                      const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [PROBLEM], 0, text);
}

void
_gdm_session_session_started (GdmSession   *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SESSION_STARTED], 0);
}

void
_gdm_session_session_startup_error (GdmSession   *session,
                                    const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SESSION_STARTUP_ERROR], 0, text);
}

void
_gdm_session_session_exited (GdmSession   *session,
                             int           exit_code)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SESSION_EXITED], 0, exit_code);
}

void
_gdm_session_session_died (GdmSession   *session,
                           int           signal_number)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SESSION_DIED], 0, signal_number);
}

void
_gdm_session_opened (GdmSession   *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [OPENED], 0);
}

void
_gdm_session_closed (GdmSession   *session)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [CLOSED], 0);
}

void
_gdm_session_selected_user_changed (GdmSession   *session,
                                    const char   *text)
{
        g_return_if_fail (GDM_IS_SESSION (session));
        g_signal_emit (session, signals [SELECTED_USER_CHANGED], 0, text);
}
