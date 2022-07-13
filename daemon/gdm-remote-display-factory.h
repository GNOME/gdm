/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2022 Joan Torres <joan.torres@suse.com>
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


#ifndef __GDM_REMOTE_DISPLAY_FACTORY_H
#define __GDM_REMOTE_DISPLAY_FACTORY_H

#include <glib-object.h>

#include "gdm-display-factory.h"
#include "gdm-display-store.h"

G_BEGIN_DECLS

#define GDM_TYPE_REMOTE_DISPLAY_FACTORY (gdm_remote_display_factory_get_type ())
G_DECLARE_FINAL_TYPE (GdmRemoteDisplayFactory, gdm_remote_display_factory, GDM, REMOTE_DISPLAY_FACTORY, GdmDisplayFactory)

GdmRemoteDisplayFactory *    gdm_remote_display_factory_new                      (GdmDisplayStore         *display_store);

G_END_DECLS

#endif /* __GDM_REMOTE_DISPLAY_FACTORY_H */
