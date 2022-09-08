/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GDM_HOST_CHOOSER_WIDGET_H
#define __GDM_HOST_CHOOSER_WIDGET_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include "gdm-chooser-host.h"

G_BEGIN_DECLS

#define GDM_TYPE_HOST_CHOOSER_WIDGET (gdm_host_chooser_widget_get_type ())
G_DECLARE_FINAL_TYPE (GdmHostChooserWidget, gdm_host_chooser_widget, GDM, HOST_CHOOSER_WIDGET, GtkBox)

GtkWidget *            gdm_host_chooser_widget_new                (int                   kind_mask);

void                   gdm_host_chooser_widget_refresh            (GdmHostChooserWidget *widget);

GdmChooserHost *       gdm_host_chooser_widget_get_host           (GdmHostChooserWidget *widget);

G_END_DECLS

#endif /* __GDM_HOST_CHOOSER_WIDGET_H */
