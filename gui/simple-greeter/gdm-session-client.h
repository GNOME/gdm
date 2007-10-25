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

#ifndef __GDM_SESSION_CLIENT_H
#define __GDM_SESSION_CLIENT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SESSION_CLIENT         (gdm_session_client_get_type ())
#define GDM_SESSION_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SESSION_CLIENT, GdmSessionClient))
#define GDM_SESSION_CLIENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SESSION_CLIENT, GdmSessionClientClass))
#define GDM_IS_SESSION_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SESSION_CLIENT))
#define GDM_IS_SESSION_CLIENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SESSION_CLIENT))
#define GDM_SESSION_CLIENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SESSION_CLIENT, GdmSessionClientClass))

typedef struct GdmSessionClientPrivate GdmSessionClientPrivate;

typedef struct
{
        GObject                  parent;
        GdmSessionClientPrivate *priv;
} GdmSessionClient;

typedef struct
{
        GObjectClass   parent_class;
} GdmSessionClientClass;

GType                  gdm_session_client_get_type              (void);

GdmSessionClient     * gdm_session_client_new                   (void);
GdmSessionClient     * gdm_session_client_new_from_desktop_file (const char       *filename);

void                   gdm_session_client_set_priority          (GdmSessionClient *client,
                                                                 guint             priority);
void                   gdm_session_client_set_command           (GdmSessionClient *client,
                                                                 const char       *command);
void                   gdm_session_client_set_try_exec          (GdmSessionClient *client,
                                                                 const char       *try_exec);

guint                  gdm_session_client_get_priority          (GdmSessionClient *client);
const char           * gdm_session_client_get_command           (GdmSessionClient *client);
const char           * gdm_session_client_get_try_exec          (GdmSessionClient *client);

gboolean               gdm_session_client_start                 (GdmSessionClient *client,
                                                                 GError          **error);
void                   gdm_session_client_stop                  (GdmSessionClient *cilent);

G_END_DECLS

#endif /* __GDM_SESSION_CLIENT_H */
