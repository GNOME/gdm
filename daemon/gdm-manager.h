/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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


#ifndef __GDM_MANAGER_H
#define __GDM_MANAGER_H

#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-manager-glue.h"

G_BEGIN_DECLS

#define GDM_TYPE_MANAGER (gdm_manager_get_type ())
G_DECLARE_FINAL_TYPE (GdmManager, gdm_manager, GDM, MANAGER, GdmDBusManagerSkeleton)

typedef enum
{
         GDM_MANAGER_ERROR_GENERAL
} GdmManagerError;

#define GDM_MANAGER_ERROR gdm_manager_error_quark ()

GQuark              gdm_manager_error_quark                    (void);

GdmManager *        gdm_manager_new                            (void);
void                gdm_manager_start                          (GdmManager *manager);
void                gdm_manager_stop                           (GdmManager *manager);

void                gdm_manager_set_xdmcp_enabled              (GdmManager *manager,
                                                                gboolean    enabled);
void                gdm_manager_set_show_local_greeter         (GdmManager *manager,
                                                                gboolean    show_local_greeter);
void                gdm_manager_set_remote_login_enabled       (GdmManager *manager,
                                                                gboolean    enabled);
gboolean            gdm_manager_get_displays                   (GdmManager *manager,
                                                                GPtrArray **displays,
                                                                GError    **error);


G_END_DECLS

#endif /* __GDM_MANAGER_H */
