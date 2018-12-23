/* gdm-session-auditor.h - Object for auditing session login/logout
 *
 * Copyright (C) 2004, 2008 Sun Microsystems, Inc.
 * Copyright (C) 2005, 2008 Red Hat, Inc.
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
 *
 * Written by: Brian A. Cameron <Brian.Cameron@sun.com>
 *             Gary Winiger <Gary.Winiger@sun.com>
 *             Ray Strode <rstrode@redhat.com>
 *             Steve Grubb <sgrubb@redhat.com>
 */
#ifndef GDM_SESSION_AUDITOR_H
#define GDM_SESSION_AUDITOR_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_AUDITOR (gdm_session_auditor_get_type ())
G_DECLARE_DERIVABLE_TYPE (GdmSessionAuditor, gdm_session_auditor, GDM, SESSION_AUDITOR, GObject)

struct _GdmSessionAuditorClass
{
        GObjectClass        parent_class;

        void               (* report_password_changed)        (GdmSessionAuditor *auditor);
        void               (* report_password_change_failure) (GdmSessionAuditor *auditor);
        void               (* report_user_accredited)         (GdmSessionAuditor *auditor);
        void               (* report_login)                   (GdmSessionAuditor *auditor);
        void               (* report_login_failure)           (GdmSessionAuditor *auditor,
                                                               int                error_code,
                                                               const char        *error_message);
        void               (* report_logout)                  (GdmSessionAuditor *auditor);
};

GdmSessionAuditor        *gdm_session_auditor_new                (const char *hostname,
                                                                  const char *display_device);
void                      gdm_session_auditor_set_username (GdmSessionAuditor *auditor,
                                                            const char        *username);
void                      gdm_session_auditor_report_password_changed (GdmSessionAuditor *auditor);
void                      gdm_session_auditor_report_password_change_failure (GdmSessionAuditor *auditor);
void                      gdm_session_auditor_report_user_accredited (GdmSessionAuditor *auditor);
void                      gdm_session_auditor_report_login (GdmSessionAuditor *auditor);
void                      gdm_session_auditor_report_login_failure (GdmSessionAuditor *auditor,
                                                                    int                error_code,
                                                                    const char        *error_message);
void                      gdm_session_auditor_report_logout (GdmSessionAuditor *auditor);

G_END_DECLS
#endif /* GDM_SESSION_AUDITOR_H */
