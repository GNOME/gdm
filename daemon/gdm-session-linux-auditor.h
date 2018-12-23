/* gdm-linux-session-auditor.h - Object for linux auditing of session login/logout
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
#ifndef GDM_SESSION_LINUX_AUDITOR_H
#define GDM_SESSION_LINUX_AUDITOR_H

#include <glib.h>
#include <glib-object.h>

#include "gdm-session-auditor.h"

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_LINUX_AUDITOR (gdm_session_linux_auditor_get_type ())
G_DECLARE_FINAL_TYPE (GdmSessionLinuxAuditor, gdm_session_linux_auditor, GDM, SESSION_LINUX_AUDITOR, GdmSessionAuditor)

GdmSessionAuditor *gdm_session_linux_auditor_new                            (const char *hostname,
                                                                             const char *display_device);

G_END_DECLS
#endif /* GDM_SESSION_LINUX_AUDITOR_H */
