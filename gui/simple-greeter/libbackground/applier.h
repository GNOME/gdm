/* -*- mode: c; style: linux -*- */

/* applier.h
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen <hovinen@helixcode.com>
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
 */

#ifndef __APPLIER_H
#define __APPLIER_H

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <X11/Xlib.h>

#include "preferences.h"

#define BG_APPLIER(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, bg_applier_get_type (), BGApplier)
#define BG_APPLIER_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, bg_applier_get_type (), BGApplierClass)
#define IS_BG_APPLIER(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, bg_applier_get_type ())

typedef struct _BGApplier BGApplier;
typedef struct _BGApplierClass BGApplierClass;

typedef struct _BGApplierPrivate BGApplierPrivate;

typedef enum _BGApplierType {
	BG_APPLIER_ROOT, BG_APPLIER_PREVIEW
} BGApplierType;

struct _BGApplier
{
	GObject           object;
	BGApplierPrivate *p;
};

struct _BGApplierClass
{
	GObjectClass klass;
};

GType      bg_applier_get_type             (void);

GObject   *bg_applier_new                  (BGApplierType          type);
GObject   *bg_applier_new_at_size          (BGApplierType          type,
					    const guint width,
					    const guint height);
GObject   *bg_applier_new_for_screen       (BGApplierType          type,
					    GdkScreen             *screen);

void       bg_applier_apply_prefs          (BGApplier             *bg_applier,
					    const BGPreferences *prefs);

gboolean   bg_applier_render_color_p       (const BGApplier       *bg_applier,
					    const BGPreferences *prefs);

GtkWidget *bg_applier_get_preview_widget   (BGApplier             *bg_applier);
GdkPixbuf *bg_applier_get_wallpaper_pixbuf (BGApplier             *bg_applier);

#endif /* __APPLIER_H */
