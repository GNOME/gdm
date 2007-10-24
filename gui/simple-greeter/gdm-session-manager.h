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

#ifndef __GDM_SESSION_MANAGER_H
#define __GDM_SESSION_MANAGER_H

#include <glib-object.h>

#include "gdm-session-client.h"

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_MANAGER         (gdm_session_manager_get_type ())
#define GDM_SESSION_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SESSION_MANAGER, GdmSessionManager))
#define GDM_SESSION_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SESSION_MANAGER, GdmSessionManagerClass))
#define GDM_IS_SESSION_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SESSION_MANAGER))
#define GDM_IS_SESSION_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SESSION_MANAGER))
#define GDM_SESSION_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SESSION_MANAGER, GdmSessionManagerClass))

typedef struct GdmSessionManagerPrivate GdmSessionManagerPrivate;

typedef struct
{
        GObject                   parent;
        GdmSessionManagerPrivate *priv;
} GdmSessionManager;

typedef struct
{
        GObjectClass   parent_class;
} GdmSessionManagerClass;

GType                  gdm_session_manager_get_type            (void);

GdmSessionManager    * gdm_session_manager_new                 (void);

void                   gdm_session_manager_add_client          (GdmSessionManager *manager,
                                                                GdmSessionClient  *client);
void                   gdm_session_manager_add_autostart_dir   (GdmSessionManager *manager,
                                                                const char        *path,
                                                                guint              runlevel);

gboolean               gdm_session_manager_start               (GdmSessionManager *manager,
                                                                GError             **error);
void                   gdm_session_manager_stop                (GdmSessionManager *manager);

G_END_DECLS

#endif /* __GDM_SESSION_MANAGER_H */
