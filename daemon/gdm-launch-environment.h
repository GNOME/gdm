/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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


#ifndef __GDM_LAUNCH_ENVIRONMENT_H
#define __GDM_LAUNCH_ENVIRONMENT_H

#include <glib-object.h>
#include "gdm-session.h"

G_BEGIN_DECLS

#define GDM_TYPE_LAUNCH_ENVIRONMENT (gdm_launch_environment_get_type ())
G_DECLARE_FINAL_TYPE (GdmLaunchEnvironment, gdm_launch_environment, GDM, LAUNCH_ENVIRONMENT, GObject)

gboolean              gdm_launch_environment_start              (GdmLaunchEnvironment *launch_environment);
gboolean              gdm_launch_environment_stop               (GdmLaunchEnvironment *launch_environment);
GdmSession *          gdm_launch_environment_get_session        (GdmLaunchEnvironment *launch_environment);
char *                gdm_launch_environment_get_session_id     (GdmLaunchEnvironment *launch_environment);

GdmLaunchEnvironment *gdm_create_greeter_launch_environment (const char *display_name,
                                                             const char *seat_id,
                                                             const char *session_type,
                                                             const char *display_hostname,
                                                             gboolean    display_is_local);
GdmLaunchEnvironment *gdm_create_initial_setup_launch_environment (const char *display_name,
                                                                   const char *seat_id,
                                                                   const char *session_type,
                                                                   const char *display_hostname,
                                                                   gboolean    display_is_local);
GdmLaunchEnvironment *gdm_create_chooser_launch_environment (const char *display_name,
                                                             const char *seat_id,
                                                             const char *display_hostname);

G_END_DECLS

#endif /* __GDM_LAUNCH_ENVIRONMENT_H */
