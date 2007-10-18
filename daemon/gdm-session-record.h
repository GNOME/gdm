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

#ifndef __GDM_SESSION_RECORD_H
#define __GDM_SESSION_RECORD_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
        GDM_SESSION_RECORD_TYPE_LOGIN,
        GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT,
        GDM_SESSION_RECORD_TYPE_LOGOUT,
} GdmSessionRecordType;

void
gdm_session_record_write (GdmSessionRecordType  record_type,
                          GPid                  session_pid,
                          const char           *user_name,
                          const char           *host_name,
                          const char           *x11_display_name,
                          const char           *display_device);


G_END_DECLS

#endif /* GDM_SESSION_RECORD_H */
