/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef PANEL_CELL_RENDERER_MODE_H
#define PANEL_CELL_RENDERER_MODE_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define PANEL_TYPE_CELL_RENDERER_MODE           (panel_cell_renderer_mode_get_type())
#define PANEL_CELL_RENDERER_MODE(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), PANEL_TYPE_CELL_RENDERER_MODE, PanelCellRendererMode))
#define PANEL_CELL_RENDERER_MODE_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST((cls), PANEL_TYPE_CELL_RENDERER_MODE, PanelCellRendererModeClass))
#define PANEL_IS_CELL_RENDERER_MODE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), PANEL_TYPE_CELL_RENDERER_MODE))
#define PANEL_IS_CELL_RENDERER_MODE_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE((cls), PANEL_TYPE_CELL_RENDERER_MODE))
#define PANEL_CELL_RENDERER_MODE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_CELL_RENDERER_MODE, PanelCellRendererModeClass))

G_BEGIN_DECLS

typedef struct _PanelCellRendererMode           PanelCellRendererMode;
typedef struct _PanelCellRendererModeClass      PanelCellRendererModeClass;

struct _PanelCellRendererMode
{
        GtkCellRendererPixbuf    parent;
        guint                    mode;
        gchar                   *icon_name;
};

struct _PanelCellRendererModeClass
{
        GtkCellRendererPixbufClass parent_class;
};

GType            panel_cell_renderer_mode_get_type      (void);
GtkCellRenderer *panel_cell_renderer_mode_new           (void);

G_END_DECLS

#endif /* PANEL_CELL_RENDERER_MODE_H */

