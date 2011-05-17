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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "panel-cell-renderer-security.h"

enum {
        PROP_0,
        PROP_SECURITY,
        PROP_LAST
};

G_DEFINE_TYPE (PanelCellRendererSecurity, panel_cell_renderer_security, GTK_TYPE_CELL_RENDERER_PIXBUF)

static gpointer parent_class = NULL;

/**
 * panel_cell_renderer_security_get_property:
 **/
static void
panel_cell_renderer_security_get_property (GObject *object, guint param_id,
                                       GValue *value, GParamSpec *pspec)
{
        PanelCellRendererSecurity *renderer = PANEL_CELL_RENDERER_SECURITY (object);

        switch (param_id) {
        case PROP_SECURITY:
                g_value_set_uint (value, renderer->security);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

/**
 * panel_cell_renderer_set_name:
 **/
static void
panel_cell_renderer_set_name (PanelCellRendererSecurity *renderer)
{
        const gchar *icon_name = NULL;

        if (renderer->security != NM_AP_SEC_UNKNOWN &&
            renderer->security != NM_AP_SEC_NONE)
                icon_name = "network-wireless-encrypted-symbolic";

        if (icon_name != NULL) {
                g_object_set (renderer,
                              "icon-name", icon_name,
                              "visible", TRUE,
                              NULL);
        } else {
                g_object_set (renderer,
                              "icon-name", NULL,
                              "visible", FALSE,
                              NULL);
        }
}

/**
 * panel_cell_renderer_security_set_property:
 **/
static void
panel_cell_renderer_security_set_property (GObject *object, guint param_id,
                                       const GValue *value, GParamSpec *pspec)
{
        PanelCellRendererSecurity *renderer = PANEL_CELL_RENDERER_SECURITY (object);

        switch (param_id) {
        case PROP_SECURITY:
                renderer->security = g_value_get_uint (value);
                panel_cell_renderer_set_name (renderer);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

/**
 * panel_cell_renderer_finalize:
 **/
static void
panel_cell_renderer_finalize (GObject *object)
{
        PanelCellRendererSecurity *renderer;
        renderer = PANEL_CELL_RENDERER_SECURITY (object);
        g_free (renderer->icon_name);
        G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * panel_cell_renderer_security_class_init:
 **/
static void
panel_cell_renderer_security_class_init (PanelCellRendererSecurityClass *class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (class);
        object_class->finalize = panel_cell_renderer_finalize;

        parent_class = g_type_class_peek_parent (class);

        object_class->get_property = panel_cell_renderer_security_get_property;
        object_class->set_property = panel_cell_renderer_security_set_property;

        g_object_class_install_property (object_class, PROP_SECURITY,
                                         g_param_spec_uint ("security", NULL,
                                                            NULL,
                                                            0, G_MAXUINT, 0,
                                                            G_PARAM_READWRITE));
}

/**
 * panel_cell_renderer_security_init:
 **/
static void
panel_cell_renderer_security_init (PanelCellRendererSecurity *renderer)
{
        renderer->security = 0;
        renderer->icon_name = NULL;
}

/**
 * panel_cell_renderer_security_new:
 **/
GtkCellRenderer *
panel_cell_renderer_security_new (void)
{
        return g_object_new (PANEL_TYPE_CELL_RENDERER_SECURITY, NULL);
}

