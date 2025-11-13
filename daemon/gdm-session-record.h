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

#ifndef __GDM_SESSION_RECORD_H
#define __GDM_SESSION_RECORD_H

#include <glib.h>
#include "gdm-session.h"

G_BEGIN_DECLS

typedef enum {
        GDM_SESSION_RECORD_LOGIN,
        GDM_SESSION_RECORD_LOGOUT,
        GDM_SESSION_RECORD_FAILED,
} GdmSessionRecordEvent;

void
gdm_session_record (GdmSessionRecordEvent  event,
                    GdmSession            *session,
                    GPid                   pid);

G_END_DECLS

#endif /* GDM_SESSION_RECORD_H */
