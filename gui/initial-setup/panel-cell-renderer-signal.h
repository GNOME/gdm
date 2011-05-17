/* -*- Mode: C; tab-width: 8; indent-tabs-signal: nil; c-basic-offset: 8 -*-
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

#ifndef PANEL_CELL_RENDERER_SIGNAL_H
#define PANEL_CELL_RENDERER_SIGNAL_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define PANEL_TYPE_CELL_RENDERER_SIGNAL                 (panel_cell_renderer_signal_get_type())
#define PANEL_CELL_RENDERER_SIGNAL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), PANEL_TYPE_CELL_RENDERER_SIGNAL, PanelCellRendererSignal))
#define PANEL_CELL_RENDERER_SIGNAL_CLASS(cls)           (G_TYPE_CHECK_CLASS_CAST((cls), PANEL_TYPE_CELL_RENDERER_SIGNAL, PanelCellRendererSignalClass))
#define PANEL_IS_CELL_RENDERER_SIGNAL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), PANEL_TYPE_CELL_RENDERER_SIGNAL))
#define PANEL_IS_CELL_RENDERER_SIGNAL_CLASS(cls)        (G_TYPE_CHECK_CLASS_TYPE((cls), PANEL_TYPE_CELL_RENDERER_SIGNAL))
#define PANEL_CELL_RENDERER_SIGNAL_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_CELL_RENDERER_SIGNAL, PanelCellRendererSignalClass))

G_BEGIN_DECLS

typedef struct _PanelCellRendererSignal         PanelCellRendererSignal;
typedef struct _PanelCellRendererSignalClass    PanelCellRendererSignalClass;

struct _PanelCellRendererSignal
{
        GtkCellRendererPixbuf    parent;
        guint                    signal;
        gchar                   *icon_name;
};

struct _PanelCellRendererSignalClass
{
        GtkCellRendererPixbufClass parent_class;
};

GType            panel_cell_renderer_signal_get_type    (void);
GtkCellRenderer *panel_cell_renderer_signal_new         (void);

G_END_DECLS

#endif /* PANEL_CELL_RENDERER_SIGNAL_H */

