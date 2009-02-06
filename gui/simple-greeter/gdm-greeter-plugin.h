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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __GDM_GREETER_PLUGIN
#define __GDM_GREETER_PLUGIN

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gdm-greeter-extension.h"

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_PLUGIN (gdm_greeter_plugin_get_type ())
#define GDM_GREETER_PLUGIN(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), GDM_TYPE_GREETER_PLUGIN, GdmGreeterPlugin))
#define GDM_IS_GREETER_PLUGIN(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDM_TYPE_GREETER_PLUGIN))

typedef struct _GdmGreeterPlugin GdmGreeterPlugin;
typedef struct _GdmGreeterPluginPrivate GdmGreeterPluginPrivate;
typedef struct _GdmGreeterPluginClass GdmGreeterPluginClass;

struct _GdmGreeterPlugin
{
        GObject                parent;
        GdmGreeterPluginPrivate *priv;
};

struct _GdmGreeterPluginClass
{
        GObjectClass   parent_class;

        void          (* loaded)         (GdmGreeterPlugin *plugin);
        void          (* load_failed)    (GdmGreeterPlugin *plugin);
        void          (* unloaded)       (GdmGreeterPlugin *plugin);
};

GType             gdm_greeter_plugin_get_type (void) G_GNUC_CONST;
GdmGreeterPlugin *gdm_greeter_plugin_new (const char *filename);
void              gdm_greeter_plugin_load (GdmGreeterPlugin *plugin);
void              gdm_greeter_plugin_unload (GdmGreeterPlugin *plugin);
const char       *gdm_greeter_plugin_get_filename (GdmGreeterPlugin *plugin);
GdmGreeterExtension *gdm_greeter_plugin_get_extension (GdmGreeterPlugin *plugin);

G_END_DECLS

#endif
