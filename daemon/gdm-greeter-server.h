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


#ifndef __GDM_GREETER_SERVER_H
#define __GDM_GREETER_SERVER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_SERVER         (gdm_greeter_server_get_type ())
#define GDM_GREETER_SERVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER_SERVER, GdmGreeterServer))
#define GDM_GREETER_SERVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER_SERVER, GdmGreeterServerClass))
#define GDM_IS_GREETER_SERVER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER_SERVER))
#define GDM_IS_GREETER_SERVER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_GREETER_SERVER))
#define GDM_GREETER_SERVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_GREETER_SERVER, GdmGreeterServerClass))

typedef struct GdmGreeterServerPrivate GdmGreeterServerPrivate;

typedef struct
{
	GObject	                 parent;
	GdmGreeterServerPrivate *priv;
} GdmGreeterServer;

typedef struct
{
	GObjectClass   parent_class;

	void (* query_answer)      (GdmGreeterServer  *greeter_server,
				    const char        *text);
	void (* session_selected)  (GdmGreeterServer  *greeter_server,
				    const char        *name);
	void (* language_selected) (GdmGreeterServer  *greeter_server,
				    const char        *name);
	void (* connected)         (GdmGreeterServer  *greeter_server);
	void (* disconnected)      (GdmGreeterServer  *greeter_server);
} GdmGreeterServerClass;

GType		    gdm_greeter_server_get_type          (void);
GdmGreeterServer *  gdm_greeter_server_new	         (void);

gboolean            gdm_greeter_server_start             (GdmGreeterServer *greeter_server);
gboolean            gdm_greeter_server_stop              (GdmGreeterServer *greeter_server);
char *              gdm_greeter_server_get_address       (GdmGreeterServer *greeter_server);

gboolean            gdm_greeter_server_info_query        (GdmGreeterServer *greeter_server,
							  const char       *text);
gboolean            gdm_greeter_server_secret_info_query (GdmGreeterServer *greeter_server,
							  const char       *text);
gboolean            gdm_greeter_server_info              (GdmGreeterServer *greeter_server,
							  const char       *text);
gboolean            gdm_greeter_server_problem           (GdmGreeterServer *greeter_server,
							  const char       *text);

G_END_DECLS

#endif /* __GDM_GREETER_SERVER_H */
