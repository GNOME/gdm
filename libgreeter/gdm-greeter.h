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


#ifndef __GDM_GREETER_H
#define __GDM_GREETER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_GREETER         (gdm_greeter_get_type ())
#define GDM_GREETER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER, GdmGreeter))
#define GDM_GREETER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER, GdmGreeterClass))
#define GDM_IS_GREETER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER))
#define GDM_IS_GREETER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_GREETER))
#define GDM_GREETER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_GREETER, GdmGreeterClass))

typedef struct GdmGreeterPrivate GdmGreeterPrivate;

typedef struct
{
	GObject		   parent;
	GdmGreeterPrivate *priv;
} GdmGreeter;

typedef struct
{
	GObjectClass   parent_class;

	/* signals */
	void (* query_answer)         (GdmGreeter *greeter,
				       const char *text);
	void (* session_selected)     (GdmGreeter *greeter,
				       const char *text);
	void (* language_selected)    (GdmGreeter *greeter,
				       const char *text);
	void (* user_selected)        (GdmGreeter *greeter,
				       const char *text);
	void (* cancelled)            (GdmGreeter *greeter);

	/* methods */
	gboolean (*start)             (GdmGreeter *greeter);
	gboolean (*stop)              (GdmGreeter *greeter);
	gboolean (*reset)             (GdmGreeter *greeter);

	gboolean (*info_query)        (GdmGreeter *greeter,
				       const char *text);
	gboolean (*secret_info_query) (GdmGreeter *greeter,
				       const char *text);
	gboolean (*info)              (GdmGreeter *greeter,
				       const char *text);
	gboolean (*problem)           (GdmGreeter *greeter,
				       const char *text);

} GdmGreeterClass;

typedef enum
{
	 GDM_GREETER_ERROR_GENERAL
} GdmGreeterError;

#define GDM_GREETER_ERROR gdm_greeter_error_quark ()

GQuark		    gdm_greeter_error_quark		       (void);
GType		    gdm_greeter_get_type		       (void);

gboolean            gdm_greeter_start                          (GdmGreeter *greeter);
gboolean            gdm_greeter_stop                           (GdmGreeter *greeter);
gboolean            gdm_greeter_reset                          (GdmGreeter *greeter);

/* emit signals */
gboolean            gdm_greeter_emit_answer_query              (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_emit_select_session            (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_emit_select_language           (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_emit_select_user               (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_emit_cancelled                 (GdmGreeter *greeter);

/* actions */
gboolean            gdm_greeter_info_query                     (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_secret_info_query              (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_info                           (GdmGreeter *greeter,
								const char *text);
gboolean            gdm_greeter_problem                        (GdmGreeter *greeter,
								const char *text);

G_END_DECLS

#endif /* __GDM_GREETER_H */
