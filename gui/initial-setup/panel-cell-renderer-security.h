/* -*- Security: C; tab-width: 8; indent-tabs-security: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc
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

#ifndef PANEL_CELL_RENDERER_SECURITY_H
#define PANEL_CELL_RENDERER_SECURITY_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define PANEL_TYPE_CELL_RENDERER_SECURITY           (panel_cell_renderer_security_get_type())
#define PANEL_CELL_RENDERER_SECURITY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), PANEL_TYPE_CELL_RENDERER_SECURITY, PanelCellRendererSecurity))
#define PANEL_CELL_RENDERER_SECURITY_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST((cls), PANEL_TYPE_CELL_RENDERER_SECURITY, PanelCellRendererSecurityClass))
#define PANEL_IS_CELL_RENDERER_SECURITY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), PANEL_TYPE_CELL_RENDERER_SECURITY))
#define PANEL_IS_CELL_RENDERER_SECURITY_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE((cls), PANEL_TYPE_CELL_RENDERER_SECURITY))
#define PANEL_CELL_RENDERER_SECURITY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_CELL_RENDERER_SECURITY, PanelCellRendererSecurityClass))

G_BEGIN_DECLS

typedef struct _PanelCellRendererSecurity           PanelCellRendererSecurity;
typedef struct _PanelCellRendererSecurityClass      PanelCellRendererSecurityClass;

typedef enum {
  NM_AP_SEC_UNKNOWN,
  NM_AP_SEC_NONE,
  NM_AP_SEC_WEP,
  NM_AP_SEC_WPA,
  NM_AP_SEC_WPA2
} NMAccessPointSecurity;

struct _PanelCellRendererSecurity
{
        GtkCellRendererPixbuf    parent;
        guint                    security;
        gchar                   *icon_name;
};

struct _PanelCellRendererSecurityClass
{
        GtkCellRendererPixbufClass parent_class;
};

GType            panel_cell_renderer_security_get_type      (void);
GtkCellRenderer *panel_cell_renderer_security_new           (void);

G_END_DECLS

#endif /* PANEL_CELL_RENDERER_SECURITY_H */

