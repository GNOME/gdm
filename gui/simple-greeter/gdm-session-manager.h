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

typedef enum {
        GDM_SESSION_LEVEL_NONE          = 1 << 0,
        GDM_SESSION_LEVEL_STARTUP       = 1 << 1,
        GDM_SESSION_LEVEL_CONFIGURATION = 1 << 2,
        GDM_SESSION_LEVEL_LOGIN_WINDOW  = 1 << 3,
        GDM_SESSION_LEVEL_HOST_CHOOSER  = 1 << 4,
        GDM_SESSION_LEVEL_REMOTE_HOST   = 1 << 5,
        GDM_SESSION_LEVEL_SHUTDOWN      = 1 << 6,
} GdmSessionLevel;

#define GDM_SESSION_ALL_LEVELS (GDM_SESSION_LEVEL_STARTUP | GDM_SESSION_LEVEL_CONFIGURATION | GDM_SESSION_LEVEL_LOGIN_WINDOW | GDM_SESSION_LEVEL_HOST_CHOOSER | GDM_SESSION_LEVEL_REMOTE_HOST | GDM_SESSION_LEVEL_SHUTDOWN)

typedef struct
{
        GObject                   parent;
        GdmSessionManagerPrivate *priv;
} GdmSessionManager;

typedef struct
{
        GObjectClass   parent_class;

        void          (* level_changed)    (GdmSessionManager *manager,
                                            guint              old_level,
                                            guint              new_level);
} GdmSessionManagerClass;

typedef gboolean (* GdmSessionLevelNotifyFunc) (GdmSessionManager *manager,
                                                gboolean           enabled,
                                                gpointer           data);

GType               gdm_session_manager_get_type            (void);

GdmSessionManager * gdm_session_manager_new                 (void);

void                gdm_session_manager_add_client          (GdmSessionManager        *manager,
                                                             GdmSessionClient         *client,
                                                             GdmSessionLevel           levels);
guint               gdm_session_manager_add_notify          (GdmSessionManager        *manager,
                                                             GdmSessionLevel           levels,
                                                             GdmSessionLevelNotifyFunc func,
                                                             gpointer                  data);

void                gdm_session_manager_load_autostart_dir  (GdmSessionManager        *manager,
                                                             const char               *path,
                                                             GdmSessionLevel           levels);

void                gdm_session_manager_set_level           (GdmSessionManager        *manager,
                                                             GdmSessionLevel           level);
GdmSessionLevel     gdm_session_manager_get_level           (GdmSessionManager        *manager);


G_END_DECLS

#endif /* __GDM_SESSION_MANAGER_H */
