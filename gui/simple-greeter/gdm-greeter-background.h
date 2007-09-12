/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GDM_GREETER_BACKGROUND_H
#define __GDM_GREETER_BACKGROUND_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_BACKGROUND         (gdm_greeter_background_get_type ())
#define GDM_GREETER_BACKGROUND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER_BACKGROUND, GdmGreeterBackground))
#define GDM_GREETER_BACKGROUND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER_BACKGROUND, GdmGreeterBackgroundClass))
#define GDM_IS_GREETER_BACKGROUND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER_BACKGROUND))
#define GDM_IS_GREETER_BACKGROUND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_GREETER_BACKGROUND))
#define GDM_GREETER_BACKGROUND_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_GREETER_BACKGROUND, GdmGreeterBackgroundClass))

typedef struct GdmGreeterBackgroundPrivate GdmGreeterBackgroundPrivate;

typedef struct
{
        GtkWindow               parent;
        GdmGreeterBackgroundPrivate *priv;
} GdmGreeterBackground;

typedef struct
{
        GtkWindowClass   parent_class;
} GdmGreeterBackgroundClass;

GType                  gdm_greeter_background_get_type                       (void);

GtkWidget            * gdm_greeter_background_new                            (void);

G_END_DECLS

#endif /* __GDM_GREETER_BACKGROUND_H */
