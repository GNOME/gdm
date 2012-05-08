/*
 * Copyright © 2009 Bastien Nocera <hadess@hadess.net>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _UM_CROP_AREA_H_
#define _UM_CROP_AREA_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define UM_TYPE_CROP_AREA (um_crop_area_get_type ())
#define UM_CROP_AREA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UM_TYPE_CROP_AREA, \
                                                                           UmCropArea))
#define UM_CROP_AREA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UM_TYPE_CROP_AREA, \
                                                                        UmCropAreaClass))
#define UM_IS_CROP_AREA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UM_TYPE_CROP_AREA))
#define UM_IS_CROP_AREA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UM_TYPE_CROP_AREA))
#define UM_CROP_AREA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UM_TYPE_CROP_AREA, \
                                                                          UmCropAreaClass))

typedef struct _UmCropAreaClass UmCropAreaClass;
typedef struct _UmCropArea UmCropArea;
typedef struct _UmCropAreaPrivate UmCropAreaPrivate;

struct _UmCropAreaClass {
        GtkDrawingAreaClass parent_class;
};

struct _UmCropArea {
        GtkDrawingArea parent_instance;
        UmCropAreaPrivate *priv;
};

GType      um_crop_area_get_type             (void) G_GNUC_CONST;

GtkWidget *um_crop_area_new                  (void);
GdkPixbuf *um_crop_area_get_picture          (UmCropArea *area);
void       um_crop_area_set_picture          (UmCropArea *area,
                                              GdkPixbuf  *pixbuf);
void       um_crop_area_set_min_size         (UmCropArea *area,
                                              gint        width,
                                              gint        height);
void       um_crop_area_set_constrain_aspect (UmCropArea *area,
                                              gboolean    constrain);

G_END_DECLS

#endif /* _UM_CROP_AREA_H_ */
