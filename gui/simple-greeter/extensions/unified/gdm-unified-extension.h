/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 *
 * Written By: Ray Strode <rstrode@redhat.com>
 */

#ifndef __GDM_UNIFIED_EXTENSION_H
#define __GDM_UNIFIED_EXTENSION_H

#include <glib-object.h>
#include "gdm-login-extension.h"

G_BEGIN_DECLS

#define GDM_TYPE_UNIFIED_EXTENSION (gdm_unified_extension_get_type ())
#define GDM_UNIFIED_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_UNIFIED_EXTENSION, GdmUnifiedExtension))
#define GDM_UNIFIED_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_UNIFIED_EXTENSION, GdmUnifiedExtensionClass))
#define GDM_IS_UNIFIED_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_UNIFIED_EXTENSION))
#define GDM_IS_UNIFIED_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_UNIFIED_EXTENSION))
#define GDM_UNIFIED_EXTENSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_UNIFIED_EXTENSION, GdmUnifiedExtensionClass))

#define GDM_UNIFIED_EXTENSION_NAME "gdm-unified-extension"

typedef struct _GdmUnifiedExtensionPrivate GdmUnifiedExtensionPrivate;

typedef struct
{
        GObject                  parent;
        GdmUnifiedExtensionPrivate *priv;
} GdmUnifiedExtension;

typedef struct
{
        GObjectClass parent_class;
} GdmUnifiedExtensionClass;

GType                 gdm_unified_extension_get_type      (void);
void                  gdm_unified_extension_load          (void);

G_END_DECLS

#endif /* GDM_UNIFIED_EXTENSION_H */
