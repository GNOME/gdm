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


#ifndef __GDM_GREETER_PROXY_H
#define __GDM_GREETER_PROXY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_PROXY         (gdm_greeter_proxy_get_type ())
#define GDM_GREETER_PROXY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER_PROXY, GdmGreeterProxy))
#define GDM_GREETER_PROXY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER_PROXY, GdmGreeterProxyClass))
#define GDM_IS_GREETER_PROXY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER_PROXY))
#define GDM_IS_GREETER_PROXY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_GREETER_PROXY))
#define GDM_GREETER_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_GREETER_PROXY, GdmGreeterProxyClass))

typedef struct GdmGreeterProxyPrivate GdmGreeterProxyPrivate;

typedef struct
{
	GObject	                parent;
	GdmGreeterProxyPrivate *priv;
} GdmGreeterProxy;

typedef struct
{
	GObjectClass   parent_class;

	void (* query_answer)      (GdmGreeterProxy  *greeter_proxy,
				    const char       *text);
	void (* session_selected)  (GdmGreeterProxy  *greeter_proxy,
				    const char       *name);
	void (* language_selected) (GdmGreeterProxy  *greeter_proxy,
				    const char       *name);
	void (* started)           (GdmGreeterProxy  *greeter_proxy);
	void (* stopped)           (GdmGreeterProxy  *greeter_proxy);
} GdmGreeterProxyClass;

GType		    gdm_greeter_proxy_get_type          (void);
GdmGreeterProxy *   gdm_greeter_proxy_new	        (const char      *display_id);
gboolean            gdm_greeter_proxy_start             (GdmGreeterProxy *greeter_proxy);
gboolean            gdm_greeter_proxy_stop              (GdmGreeterProxy *greeter_proxy);


gboolean            gdm_greeter_proxy_info_query        (GdmGreeterProxy *greeter_proxy,
							 const char      *text);
gboolean            gdm_greeter_proxy_secret_info_query (GdmGreeterProxy *greeter_proxy,
							 const char      *text);
gboolean            gdm_greeter_proxy_info              (GdmGreeterProxy *greeter_proxy,
							 const char      *text);
gboolean            gdm_greeter_proxy_problem           (GdmGreeterProxy *greeter_proxy,
							 const char      *text);

G_END_DECLS

#endif /* __GDM_GREETER_PROXY_H */
