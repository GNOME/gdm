/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2009-2010  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 *
 * Written by: Matthias Clasen <mclasen@redhat.com>
 */

#ifndef __UM_PHOTO_DIALOG_H__
#define __UM_PHOTO_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _UmPhotoDialog UmPhotoDialog;
typedef void (SelectAvatarCallback) (GdkPixbuf   *pixbuf,
                                     const gchar *filename,
                                     gpointer     data);

UmPhotoDialog *um_photo_dialog_new      (GtkWidget            *button,
                                         SelectAvatarCallback  callback,
                                         gpointer              data);
void           um_photo_dialog_free     (UmPhotoDialog *dialog);

G_END_DECLS

#endif
