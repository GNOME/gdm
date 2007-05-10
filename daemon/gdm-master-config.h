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


#ifndef __GDM_DAEMON_CONFIG_H
#define __GDM_DAEMON_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_DAEMON_CONFIG         (gdm_daemon_config_get_type ())
#define GDM_DAEMON_CONFIG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_DAEMON_CONFIG, GdmDaemonConfig))
#define GDM_DAEMON_CONFIG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_DAEMON_CONFIG, GdmDaemonConfigClass))
#define GDM_IS_DAEMON_CONFIG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_DAEMON_CONFIG))
#define GDM_IS_DAEMON_CONFIG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_DAEMON_CONFIG))
#define GDM_DAEMON_CONFIG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_DAEMON_CONFIG, GdmDaemonConfigClass))

typedef struct GdmDaemonConfigPrivate GdmDaemonConfigPrivate;

typedef struct
{
	GObject		        parent;
	GdmDaemonConfigPrivate *priv;
} GdmDaemonConfig;

typedef struct
{
	GObjectClass   parent_class;
} GdmDaemonConfigClass;

typedef enum
{
	 GDM_DAEMON_CONFIG_ERROR_GENERAL
} GdmDaemonConfigError;

#define GDM_DAEMON_CONFIG_ERROR gdm_daemon_config_error_quark ()

typedef struct _GdmXserver GdmXserver;

struct _GdmXserver
{
	char    *id;
	char    *name;
	char    *command;
	gboolean flexible;
	gboolean choosable; /* not implemented yet */
	gboolean chooser; /* instead of greeter, run chooser */
	gboolean handled;
	int      number;
	int      priority;
};

GQuark		    gdm_daemon_config_error_quark		     (void);
GType		    gdm_daemon_config_get_type		             (void);

GdmDaemonConfig *   gdm_daemon_config_new			     (void);
void                gdm_daemon_config_load                           (GdmDaemonConfig *config);
gboolean            gdm_daemon_config_get_bool_for_id                (GdmDaemonConfig *config,
								      int              id,
								      gboolean        *value);
gboolean            gdm_daemon_config_get_int_for_id                 (GdmDaemonConfig *config,
								      int              id,
								      int             *value);
gboolean            gdm_daemon_config_get_string_for_id              (GdmDaemonConfig *config,
								      int              id,
								      char           **value);

GdmXserver *        gdm_daemon_config_find_xserver                   (GdmDaemonConfig *config,
								      const char      *id);
char *              gdm_daemon_config_get_xservers                   (GdmDaemonConfig *config);
GSList *            gdm_daemon_config_get_xserver_list               (GdmDaemonConfig *config);

G_END_DECLS

#endif /* __GDM_DAEMON_CONFIG_H */
