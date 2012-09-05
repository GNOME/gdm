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

#include "config.h"

#include <gio/gio.h>

#include "gdm-session-worker-common.h"

static const GDBusErrorEntry gdm_session_worker_error_entries[] = {
        { GDM_SESSION_WORKER_ERROR_GENERIC              , "org.gnome.DisplayManager.SessionWorker.Error.Generic", },
        { GDM_SESSION_WORKER_ERROR_WITH_SESSION_COMMAND , "org.gnome.DisplayManager.SessionWorker.Error.WithSessionCommand" },
        { GDM_SESSION_WORKER_ERROR_FORKING              , "org.gnome.DisplayManager.SessionWorker.Error.Forking" },
        { GDM_SESSION_WORKER_ERROR_OPENING_MESSAGE_PIPE , "org.gnome.DisplayManager.SessionWorker.Error.OpeningMessagePipe" },
        { GDM_SESSION_WORKER_ERROR_COMMUNICATING        , "org.gnome.DisplayManager.SessionWorker.Error.Communicating" },
        { GDM_SESSION_WORKER_ERROR_WORKER_DIED          , "org.gnome.DisplayManager.SessionWorker.Error.WorkerDied" },
        { GDM_SESSION_WORKER_ERROR_SERVICE_UNAVAILABLE  , "org.gnome.DisplayManager.SessionWorker.Error.ServiceUnavailable" },
        { GDM_SESSION_WORKER_ERROR_AUTHENTICATING       , "org.gnome.DisplayManager.SessionWorker.Error.Authenticating" },
        { GDM_SESSION_WORKER_ERROR_AUTHORIZING          , "org.gnome.DisplayManager.SessionWorker.Error.Authorizing" },
        { GDM_SESSION_WORKER_ERROR_OPENING_LOG_FILE     , "org.gnome.DisplayManager.SessionWorker.Error.OpeningLogFile" },
        { GDM_SESSION_WORKER_ERROR_OPENING_SESSION      , "org.gnome.DisplayManager.SessionWorker.Error.OpeningSession" },
        { GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS   , "org.gnome.DisplayManager.SessionWorker.Error.GivingCredentials" },
        { GDM_SESSION_WORKER_ERROR_WRONG_STATE          , "org.gnome.DisplayManager.SessionWorker.Error.WrongState" },
        { GDM_SESSION_WORKER_ERROR_OUTSTANDING_REQUEST  , "org.gnome.DisplayManager.SessionWorker.Error.OutstandingRequest" },
        { GDM_SESSION_WORKER_ERROR_IN_REAUTH_SESSION    , "org.gnome.DisplayManager.SessionWorker.Error.InReauthSession" }
};

GQuark
gdm_session_worker_error_quark (void)
{
        static volatile gsize error_quark = 0;

        g_dbus_error_register_error_domain ("gdm-session-worker-error-quark",
                                            &error_quark,
                                            gdm_session_worker_error_entries,
                                            G_N_ELEMENTS (gdm_session_worker_error_entries));

        return (GQuark) error_quark;
}
