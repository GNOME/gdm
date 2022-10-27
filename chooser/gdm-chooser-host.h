/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GDM_CHOOSER_HOST__
#define __GDM_CHOOSER_HOST__

#include <glib-object.h>
#include "gdm-address.h"

G_BEGIN_DECLS

#define GDM_TYPE_CHOOSER_HOST (gdm_chooser_host_get_type ())
G_DECLARE_FINAL_TYPE (GdmChooserHost, gdm_chooser_host, GDM, CHOOSER_HOST, GObject)

typedef enum {
        GDM_CHOOSER_HOST_KIND_XDMCP = 1 << 0,
} GdmChooserHostKind;

#define GDM_CHOOSER_HOST_KIND_MASK_ALL (GDM_CHOOSER_HOST_KIND_XDMCP)

const char           *gdm_chooser_host_get_description     (GdmChooserHost   *chooser_host);
GdmAddress *          gdm_chooser_host_get_address         (GdmChooserHost   *chooser_host);
gboolean              gdm_chooser_host_get_willing         (GdmChooserHost   *chooser_host);
GdmChooserHostKind    gdm_chooser_host_get_kind            (GdmChooserHost   *chooser_host);

G_END_DECLS

#endif
