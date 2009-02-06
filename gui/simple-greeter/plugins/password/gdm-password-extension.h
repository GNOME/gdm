/*
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written By: Ray Strode <rstrode@redhat.com>
 */

#ifndef __GDM_PASSWORD_EXTENSION_H
#define __GDM_PASSWORD_EXTENSION_H

#include <glib-object.h>
#include "gdm-greeter-extension.h"

G_BEGIN_DECLS

#define GDM_TYPE_PASSWORD_EXTENSION (gdm_password_extension_get_type ())
#define GDM_PASSWORD_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_PASSWORD_EXTENSION, GdmPasswordExtension))
#define GDM_PASSWORD_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_PASSWORD_EXTENSION, GdmPasswordExtensionClass))
#define GDM_IS_PASSWORD_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_PASSWORD_EXTENSION))
#define GDM_IS_PASSWORD_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_PASSWORD_EXTENSION))
#define GDM_PASSWORD_EXTENSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_PASSWORD_EXTENSION, GdmPasswordExtensionClass))

typedef struct _GdmPasswordExtensionPrivate GdmPasswordExtensionPrivate;

typedef struct
{
        GObject                  parent;
        GdmPasswordExtensionPrivate *priv;
} GdmPasswordExtension;

typedef struct
{
        GObjectClass parent_class;
} GdmPasswordExtensionClass;

GType                 gdm_password_extension_get_type      (void);

GdmPasswordExtension *gdm_password_extension_new       (void);

G_END_DECLS

#endif /* GDM_PASSWORD_EXTENSION_H */
