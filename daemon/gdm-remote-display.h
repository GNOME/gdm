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


#ifndef __GDM_REMOTE_DISPLAY_H
#define __GDM_REMOTE_DISPLAY_H

#include <glib-object.h>
#include <stdint.h>

#include "gdm-display.h"

G_BEGIN_DECLS

#define GDM_TYPE_REMOTE_DISPLAY         (gdm_remote_display_get_type ())
G_DECLARE_FINAL_TYPE (GdmRemoteDisplay, gdm_remote_display, GDM, REMOTE_DISPLAY, GdmDisplay)

GdmDisplay *        gdm_remote_display_new                     (const char *remote_id);

char *              gdm_remote_display_get_remote_id           (GdmRemoteDisplay *self);

void                gdm_remote_display_set_remote_id           (GdmRemoteDisplay *self,
                                                                const char       *remote_id);

G_END_DECLS

#endif /* __GDM_REMOTE_DISPLAY_H */

