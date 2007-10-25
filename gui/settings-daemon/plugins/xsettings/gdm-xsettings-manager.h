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

#ifndef __GDM_XSETTINGS_MANAGER_H
#define __GDM_XSETTINGS_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_XSETTINGS_MANAGER         (gdm_xsettings_manager_get_type ())
#define GDM_XSETTINGS_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_XSETTINGS_MANAGER, GdmXsettingsManager))
#define GDM_XSETTINGS_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_XSETTINGS_MANAGER, GdmXsettingsManagerClass))
#define GDM_IS_XSETTINGS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_XSETTINGS_MANAGER))
#define GDM_IS_XSETTINGS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_XSETTINGS_MANAGER))
#define GDM_XSETTINGS_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_XSETTINGS_MANAGER, GdmXsettingsManagerClass))

typedef struct GdmXsettingsManagerPrivate GdmXsettingsManagerPrivate;

#define GDM_XSETTINGS_ALL_LEVELS (GDM_XSETTINGS_LEVEL_STARTUP | GDM_XSETTINGS_LEVEL_CONFIGURATION | GDM_XSETTINGS_LEVEL_LOGIN_WINDOW | GDM_XSETTINGS_LEVEL_HOST_CHOOSER | GDM_XSETTINGS_LEVEL_REMOTE_HOST | GDM_XSETTINGS_LEVEL_SHUTDOWN)

typedef struct
{
        GObject                     parent;
        GdmXsettingsManagerPrivate *priv;
} GdmXsettingsManager;

typedef struct
{
        GObjectClass   parent_class;
} GdmXsettingsManagerClass;

GType                gdm_xsettings_manager_get_type            (void);

GdmXsettingsManager * gdm_xsettings_manager_new                 (void);
gboolean             gdm_xsettings_manager_start               (GdmXsettingsManager *manager,
                                                                GError             **error);
void                 gdm_xsettings_manager_stop                (GdmXsettingsManager *manager);

G_END_DECLS

#endif /* __GDM_XSETTINGS_MANAGER_H */
