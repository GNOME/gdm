/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Authors: halton.huo@sun.com
 * Copyright (C) 2009 Sun Microsystems, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef __GDM_DYNAMIC_DISPLAY_H
#define __GDM_DYNAMIC_DISPLAY_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include "gdm-display.h"

G_BEGIN_DECLS

#define GDM_TYPE_DYNAMIC_DISPLAY         (gdm_dynamic_display_get_type ())
#define GDM_DYNAMIC_DISPLAY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_DYNAMIC_DISPLAY, GdmDynamicDisplay))
#define GDM_DYNAMIC_DISPLAY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_DYNAMIC_DISPLAY, GdmDynamicDisplayClass))
#define GDM_IS_DYNAMIC_DISPLAY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_DYNAMIC_DISPLAY))
#define GDM_IS_DYNAMIC_DISPLAY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_DYNAMIC_DISPLAY))
#define GDM_DYNAMIC_DISPLAY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_DYNAMIC_DISPLAY, GdmDynamicDisplayClass))

typedef struct _GdmDynamicDisplayPrivate GdmDynamicDisplayPrivate;

typedef struct
{
        GdmDisplay                parent;
        GdmDynamicDisplayPrivate *priv;
} GdmDynamicDisplay;

typedef struct
{
        GdmDisplayClass   parent_class;

} GdmDynamicDisplayClass;

GType               gdm_dynamic_display_get_type                (void);
GdmDisplay *        gdm_dynamic_display_new                     (int display_number);


G_END_DECLS

#endif /* __GDM_DYNAMIC_DISPLAY_H */
