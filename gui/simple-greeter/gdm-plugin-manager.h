/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Written by: Ray Strode <rstrode@redhat.com>
 *
 */

#ifndef __GDM_PLUGIN_MANAGER_H
#define __GDM_PLUGIN_MANAGER_H

#include <glib-object.h>

#include "gdm-greeter-plugin.h"

G_BEGIN_DECLS

#define GDM_TYPE_PLUGIN_MANAGER         (gdm_plugin_manager_get_type ())
#define GDM_PLUGIN_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_PLUGIN_MANAGER, GdmPluginManager))
#define GDM_PLUGIN_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_PLUGIN_MANAGER, GdmPluginManagerClass))
#define GDM_IS_PLUGIN_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_PLUGIN_MANAGER))
#define GDM_IS_PLUGIN_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_PLUGIN_MANAGER))
#define GDM_PLUGIN_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_PLUGIN_MANAGER, GdmPluginManagerClass))

typedef struct GdmPluginManagerPrivate GdmPluginManagerPrivate;

typedef struct
{
        GObject                parent;
        GdmPluginManagerPrivate *priv;
} GdmPluginManager;

typedef struct
{
        GObjectClass   parent_class;

        void          (* plugins_loaded)              (GdmPluginManager *plugin_manager);
        void          (* plugin_added)                (GdmPluginManager *plugin_manager,
                                                       GdmGreeterPlugin *plugin);
        void          (* plugin_removed)              (GdmPluginManager *plugin_manager,
                                                       GdmGreeterPlugin *plugin);
} GdmPluginManagerClass;

GType             gdm_plugin_manager_get_type              (void);

GdmPluginManager *gdm_plugin_manager_ref_default           (void);

GdmGreeterPlugin *gdm_plugin_manager_get_plugin            (GdmPluginManager *plugin,
                                                            const char       *name);

G_END_DECLS

#endif /* __GDM_PLUGIN_MANAGER_H */
