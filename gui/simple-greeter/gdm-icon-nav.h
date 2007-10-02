/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 The Free Software Foundation
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
 */

#ifndef __GDM_ICON_NAV_H__
#define __GDM_ICON_NAV_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _GdmIconNav GdmIconNav;
typedef struct _GdmIconNavClass GdmIconNavClass;
typedef struct _GdmIconNavPrivate GdmIconNavPrivate;

#define GDM_TYPE_ICON_NAV            (gdm_icon_nav_get_type ())
#define GDM_ICON_NAV(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GDM_TYPE_ICON_NAV, GdmIconNav))
#define GDM_ICON_NAV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GDM_TYPE_ICON_NAV, GdmIconNavClass))
#define GDM_IS_ICON_NAV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GDM_TYPE_ICON_NAV))
#define GDM_IS_ICON_NAV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GDM_TYPE_ICON_NAV))
#define GDM_ICON_NAV_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  GDM_TYPE_ICON_NAV, GdmIconNavClass))

typedef enum {
        GDM_ICON_NAV_MODE_ONE_ROW,
        GDM_ICON_NAV_MODE_ONE_COLUMN,
        GDM_ICON_NAV_MODE_MULTIPLE_ROWS,
        GDM_ICON_NAV_MODE_MULTIPLE_COLUMNS
} GdmIconNavMode;

struct _GdmIconNav {
        GtkHBox base_instance;

        GdmIconNavPrivate *priv;
};

struct _GdmIconNavClass {
        GtkHBoxClass parent_class;
};

GType            gdm_icon_nav_get_type          (void) G_GNUC_CONST;

GtkWidget       *gdm_icon_nav_new               (GtkWidget         *iconview,
                                                 GdmIconNavMode     mode,
                                                 gboolean           show_buttons);

gboolean         gdm_icon_nav_get_show_buttons  (GdmIconNav        *nav);

void             gdm_icon_nav_set_show_buttons  (GdmIconNav        *nav,
                                                 gboolean           show_buttons);

GdmIconNavMode   gdm_icon_nav_get_mode          (GdmIconNav        *nav);

void             gdm_icon_nav_set_mode          (GdmIconNav        *nav,
                                                 GdmIconNavMode     mode);

G_END_DECLS

#endif /* __GDM_ICON_NAV_H__ */
