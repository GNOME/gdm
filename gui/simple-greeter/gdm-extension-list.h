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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Written by: Ray Strode <rstrode@redhat.com>
 */

#ifndef __GDM_EXTENSION_LIST_H
#define __GDM_EXTENSION_LIST_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gdm-login-extension.h"

G_BEGIN_DECLS

#define GDM_TYPE_EXTENSION_LIST         (gdm_extension_list_get_type ())
#define GDM_EXTENSION_LIST(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_EXTENSION_LIST, GdmExtensionList))
#define GDM_EXTENSION_LIST_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_EXTENSION_LIST, GdmExtensionListClass))
#define GDM_IS_EXTENSION_LIST(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_EXTENSION_LIST))
#define GDM_IS_EXTENSION_LIST_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_EXTENSION_LIST))
#define GDM_EXTENSION_LIST_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_EXTENSION_LIST, GdmExtensionListClass))

typedef struct GdmExtensionListPrivate GdmExtensionListPrivate;
typedef struct _GdmExtensionList GdmExtensionList;

struct _GdmExtensionList
{
        GtkAlignment        parent;
        GdmExtensionListPrivate *priv;
};

typedef struct
{
        GtkAlignmentClass       parent_class;

        void (* deactivated)    (GdmExtensionList    *widget,
                                 GdmLoginExtension *extension);
        void (* activated)      (GdmExtensionList    *widget,
                                 GdmLoginExtension *extension);
} GdmExtensionListClass;

GType       gdm_extension_list_get_type               (void);
GtkWidget * gdm_extension_list_new                    (void);

GdmLoginExtension *gdm_extension_list_get_active_extension (GdmExtensionList    *widget);
void               gdm_extension_list_add_extension        (GdmExtensionList    *widget,
                                                            GdmLoginExtension *extension);
void               gdm_extension_list_remove_extension     (GdmExtensionList    *widget,
                                                            GdmLoginExtension *extension);

int         gdm_extension_list_get_number_of_visible_extensions (GdmExtensionList *widget);
G_END_DECLS

#endif /* __GDM_EXTENSION_LIST_H */
