/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
 * Copyright (C) 2012 Jasper St. Pierre <jstpierre@mecheye.net>
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

#ifndef __GDM_SESSION_WORKER_COMMON_H
#define __GDM_SESSION_WORKER_COMMON_H

#include <glib-object.h>

#define GDM_SESSION_WORKER_ERROR           (gdm_session_worker_error_quark ())

GQuark gdm_session_worker_error_quark (void);

typedef enum _GdmSessionWorkerError {
        GDM_SESSION_WORKER_ERROR_GENERIC = 0,
        GDM_SESSION_WORKER_ERROR_WITH_SESSION_COMMAND,
        GDM_SESSION_WORKER_ERROR_FORKING,
        GDM_SESSION_WORKER_ERROR_OPENING_MESSAGE_PIPE,
        GDM_SESSION_WORKER_ERROR_COMMUNICATING,
        GDM_SESSION_WORKER_ERROR_WORKER_DIED,
        GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE,
        GDM_SESSION_WORKER_ERROR_TOO_MANY_RETRIES,
        GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
        GDM_SESSION_WORKER_ERROR_AUTHORIZING,
        GDM_SESSION_WORKER_ERROR_OPENING_LOG_FILE,
        GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
        GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
        GDM_SESSION_WORKER_ERROR_WRONG_STATE,
        GDM_SESSION_WORKER_ERROR_OUTSTANDING_REQUEST,
        GDM_SESSION_WORKER_ERROR_IN_REAUTH_SESSION,
} GdmSessionWorkerError;

#endif /* GDM_SESSION_WORKER_COMMON_H */
