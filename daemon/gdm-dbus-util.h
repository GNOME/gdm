/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GDM_DBUS_UTIL_H
#define __GDM_DBUS_UTIL_H

#include <gio/gio.h>
#include <unistd.h>
#include <sys/types.h>

GDBusServer *gdm_dbus_setup_private_server (GDBusAuthObserver  *observer,
                                            GError            **error);

gboolean gdm_dbus_get_pid_for_name (const char  *system_bus_name,
                                    pid_t       *out_pid,
                                    GError     **error);

gboolean gdm_dbus_get_uid_for_name (const char  *system_bus_name,
                                    uid_t       *out_uid,
                                    GError     **error);

void gdm_dbus_error_ensure (GQuark domain);
#endif
