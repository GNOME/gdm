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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __GDM_SESSION_H
#define __GDM_SESSION_H

#include <glib-object.h>
#include <gio/gio.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION (gdm_session_get_type ())
G_DECLARE_FINAL_TYPE (GdmSession, gdm_session, GDM, SESSION, GObject)

typedef enum
{
        GDM_SESSION_VERIFICATION_MODE_LOGIN,
        GDM_SESSION_VERIFICATION_MODE_CHOOSER,
        GDM_SESSION_VERIFICATION_MODE_REAUTHENTICATE
} GdmSessionVerificationMode;

typedef enum {
        /* We reuse the existing display server, e.g. X server
         * in "classic" mode from the greeter for the first seat. */
        GDM_SESSION_DISPLAY_MODE_REUSE_VT,

        /* Doesn't know anything about VTs. Tries to set DRM
         * master and will throw a tantrum if something bad
         * happens. e.g. weston-launch or mutter-launch. */
        GDM_SESSION_DISPLAY_MODE_NEW_VT,

        /* Uses logind sessions to manage itself. We need to set an
         * XDG_VTNR and it will switch to the correct VT on startup.
         * e.g. mutter-wayland with logind integration, X server with
         * logind integration. */
        GDM_SESSION_DISPLAY_MODE_LOGIND_MANAGED,
} GdmSessionDisplayMode;

GdmSessionDisplayMode gdm_session_display_mode_from_string (const char *str);
const char * gdm_session_display_mode_to_string (GdmSessionDisplayMode mode);

GdmSession      *gdm_session_new                      (GdmSessionVerificationMode verification_mode,
                                                       uid_t         allowed_user,
                                                       const char   *display_name,
                                                       const char   *display_hostname,
                                                       const char   *display_device,
                                                       const char   *display_seat_id,
                                                       const char   *display_x11_authority_file,
                                                       gboolean      display_is_local,
                                                       const char * const *environment);
uid_t             gdm_session_get_allowed_user       (GdmSession     *session);
void              gdm_session_start_reauthentication (GdmSession *session,
                                                      GPid        pid_of_caller,
                                                      uid_t       uid_of_caller);

const char       *gdm_session_get_server_address          (GdmSession     *session);
const char       *gdm_session_get_username                (GdmSession     *session);
const char       *gdm_session_get_display_device          (GdmSession     *session);
const char       *gdm_session_get_display_seat_id         (GdmSession     *session);
const char       *gdm_session_get_session_id              (GdmSession     *session);
gboolean          gdm_session_bypasses_xsession           (GdmSession     *session);
gboolean          gdm_session_session_registers           (GdmSession     *session);
GdmSessionDisplayMode gdm_session_get_display_mode  (GdmSession     *session);
gboolean          gdm_session_start_conversation          (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_stop_conversation           (GdmSession *session,
                                                           const char *service_name);
const char       *gdm_session_get_conversation_session_id (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_setup                       (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_setup_for_user              (GdmSession *session,
                                                           const char *service_name,
                                                           const char *username);
void              gdm_session_setup_for_program           (GdmSession *session,
                                                           const char *service_name,
                                                           const char *username,
                                                           const char *log_file);
void              gdm_session_set_environment_variable    (GdmSession *session,
                                                           const char *key,
                                                           const char *value);
void              gdm_session_send_environment            (GdmSession *self,
                                                           const char *service_name);
void              gdm_session_reset                       (GdmSession *session);
void              gdm_session_cancel                      (GdmSession *session);
void              gdm_session_authenticate                (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_authorize                   (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_accredit                    (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_open_session                (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_start_session               (GdmSession *session,
                                                           const char *service_name);
void              gdm_session_close                       (GdmSession *session);

void              gdm_session_answer_query                (GdmSession *session,
                                                           const char *service_name,
                                                           const char *text);
void              gdm_session_report_error                (GdmSession *session,
                                                           const char *service_name,
                                                           GDBusError  code,
                                                           const char *message);
void              gdm_session_select_program              (GdmSession *session,
                                                           const char *command_line);
void              gdm_session_select_session              (GdmSession *session,
                                                           const char *session_name);
void              gdm_session_select_user                 (GdmSession *session,
                                                           const char *username);
void              gdm_session_set_timed_login_details     (GdmSession *session,
                                                           const char *username,
                                                           int         delay);
gboolean          gdm_session_client_is_connected         (GdmSession *session);
gboolean          gdm_session_is_running                  (GdmSession *session);
gboolean          gdm_session_is_frozen                   (GdmSession *session);
GPid              gdm_session_get_pid                     (GdmSession *session);

G_END_DECLS

#endif /* GDM_SESSION_H */
