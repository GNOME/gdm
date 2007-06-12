/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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


#ifndef __GDM_SIMPLE_GREETER_H
#define __GDM_SIMPLE_GREETER_H

#include <glib-object.h>

#include "gdm-greeter.h"

G_BEGIN_DECLS

#define GDM_TYPE_SIMPLE_GREETER         (gdm_simple_greeter_get_type ())
#define GDM_SIMPLE_GREETER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeter))
#define GDM_SIMPLE_GREETER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeterClass))
#define GDM_IS_SIMPLE_GREETER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SIMPLE_GREETER))
#define GDM_IS_SIMPLE_GREETER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SIMPLE_GREETER))
#define GDM_SIMPLE_GREETER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeterClass))

typedef struct GdmSimpleGreeterPrivate GdmSimpleGreeterPrivate;

typedef struct
{
	GdmGreeter	 	 parent;
	GdmSimpleGreeterPrivate *priv;
} GdmSimpleGreeter;

typedef struct
{
	GdmGreeterClass   parent_class;

} GdmSimpleGreeterClass;

GType		    gdm_simple_greeter_get_type		       (void);
GdmGreeter *	    gdm_simple_greeter_new		       (void);


G_END_DECLS

#endif /* __GDM_SIMPLE_GREETER_H */
