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

#ifndef __GDM_FINGERPRINT_EXTENSION_H
#define __GDM_FINGERPRINT_EXTENSION_H

#include <glib-object.h>
#include "gdm-greeter-extension.h"

G_BEGIN_DECLS

#define GDM_TYPE_FINGERPRINT_EXTENSION (gdm_fingerprint_extension_get_type ())
#define GDM_FINGERPRINT_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_FINGERPRINT_EXTENSION, GdmFingerprintExtension))
#define GDM_FINGERPRINT_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_FINGERPRINT_EXTENSION, GdmFingerprintExtensionClass))
#define GDM_IS_FINGERPRINT_EXTENSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_FINGERPRINT_EXTENSION))
#define GDM_IS_FINGERPRINT_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_FINGERPRINT_EXTENSION))
#define GDM_FINGERPRINT_EXTENSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_FINGERPRINT_EXTENSION, GdmFingerprintExtensionClass))

typedef struct _GdmFingerprintExtensionPrivate GdmFingerprintExtensionPrivate;

typedef struct
{
        GObject                  parent;
        GdmFingerprintExtensionPrivate *priv;
} GdmFingerprintExtension;

typedef struct
{
        GObjectClass parent_class;
} GdmFingerprintExtensionClass;

GType                 gdm_fingerprint_extension_get_type      (void);

GdmFingerprintExtension *gdm_fingerprint_extension_new       (void);

G_END_DECLS

#endif /* GDM_FINGERPRINT_EXTENSION_H */
