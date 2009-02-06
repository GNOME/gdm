/*
 * Copyright 2009 Red Hat, Inc.  *
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
 * Written by: Ray Strode
 */

#ifndef __GDM_GREETER_EXTENSION_H
#define __GDM_GREETER_EXTENSION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_EXTENSION         (gdm_greeter_extension_get_type ())
#define GDM_GREETER_EXTENSION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER_EXTENSION, GdmGreeterExtension))
#define GDM_GREETER_EXTENSION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER_EXTENSION, GdmGreeterExtensionClass))
#define GDM_IS_GREETER_EXTENSION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER_EXTENSION))
#define GDM_GREETER_EXTENSION_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GDM_TYPE_GREETER_EXTENSION, GdmGreeterExtensionIface))

typedef struct _GdmGreeterExtension      GdmGreeterExtension;
typedef struct _GdmGreeterExtensionIface GdmGreeterExtensionIface;

struct _GdmGreeterExtensionIface
{
        GTypeInterface base_iface;

        void (* loaded) (GdmGreeterExtension *extension);
        void (* load_failed) (GdmGreeterExtension *extension,
                              GError              *error);
};

GType gdm_greeter_extension_get_type (void) G_GNUC_CONST;

void gdm_greeter_extension_loaded      (GdmGreeterExtension *extension);
void gdm_greeter_extension_load_failed (GdmGreeterExtension *extension,
                                        GError              *error);

typedef GdmGreeterExtension * (* GdmGreeterPluginGetExtensionFunc) (void);

G_END_DECLS
#endif /* __GDM_GREETER_EXTENSION_H */
