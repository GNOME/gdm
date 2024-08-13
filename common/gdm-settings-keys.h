/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GDM_SETTINGS_KEYS_H
#define _GDM_SETTINGS_KEYS_H

#include <glib.h>

G_BEGIN_DECLS

#define GDM_KEY_USER "daemon/User"
#define GDM_KEY_GROUP "daemon/Group"
#define GDM_KEY_AUTO_LOGIN_ENABLE "daemon/AutomaticLoginEnable"
#define GDM_KEY_AUTO_LOGIN_USER "daemon/AutomaticLogin"
#define GDM_KEY_TIMED_LOGIN_ENABLE "daemon/TimedLoginEnable"
#define GDM_KEY_TIMED_LOGIN_USER "daemon/TimedLogin"
#define GDM_KEY_TIMED_LOGIN_DELAY "daemon/TimedLoginDelay"
#define GDM_KEY_INITIAL_SETUP_ENABLE "daemon/InitialSetupEnable"
#define GDM_KEY_PREFERRED_DISPLAY_SERVER "daemon/PreferredDisplayServer"
#ifdef ENABLE_WAYLAND_SUPPORT
#define GDM_KEY_WAYLAND_ENABLE "daemon/WaylandEnable"
#endif
#ifdef ENABLE_X11_SUPPORT
#define GDM_KEY_XORG_ENABLE "daemon/XorgEnable"
#endif
#define GDM_KEY_REMOTE_LOGIN_ENABLE "daemon/RemoteLoginEnable"

#define GDM_KEY_DEBUG "debug/Enable"

#define GDM_KEY_DISALLOW_TCP "security/DisallowTCP"
#define GDM_KEY_ALLOW_REMOTE_AUTOLOGIN "security/AllowRemoteAutoLogin"

#define GDM_KEY_XDMCP_ENABLE "xdmcp/Enable"
#define GDM_KEY_SHOW_LOCAL_GREETER "xdmcp/ShowLocalGreeter"
#define GDM_KEY_MAX_PENDING "xdmcp/MaxPending"
#define GDM_KEY_MAX_SESSIONS "xdmcp/MaxSessions"
#define GDM_KEY_MAX_WAIT "xdmcp/MaxWait"
#define GDM_KEY_DISPLAYS_PER_HOST "xdmcp/DisplaysPerHost"
#define GDM_KEY_UDP_PORT "xdmcp/Port"
#define GDM_KEY_INDIRECT "xdmcp/HonorIndirect"
#define GDM_KEY_MAX_WAIT_INDIRECT "xdmcp/MaxWaitIndirect"
#define GDM_KEY_WILLING "xdmcp/Willing"

#define GDM_KEY_MULTICAST "chooser/Multicast"
#define GDM_KEY_MULTICAST_ADDR "chooser/MulticastAddr"

G_END_DECLS

#endif /* _GDM_SETTINGS_KEYS_H */
