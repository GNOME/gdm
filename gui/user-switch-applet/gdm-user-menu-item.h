/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * A menu item bound to a GdmUser.
 */

#ifndef __GDM_USER_MENU_ITEM__
#define __GDM_USER_MENU_ITEM__ 1

#include <gtk/gtkimagemenuitem.h>

#include "gdm-user.h"

G_BEGIN_DECLS

#define GDM_TYPE_USER_MENU_ITEM \
  (gdm_user_menu_item_get_type ())
#define GDM_USER_MENU_ITEM(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), GDM_TYPE_USER_MENU_ITEM, GdmUserMenuItem))
#define GDM_USER_MENU_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_USER_MENU_ITEM, GdmUserMenuItemClass))
#define GDM_IS_USER_MENU_ITEM(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDM_TYPE_USER_MENU_ITEM))
#define GDM_IS_USER_MENU_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_USER_MENU_ITEM))
#define GDM_USER_MENU_ITEM_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), GDM_TYPE_USER_MENU_ITEM, GdmUserMenuItemClass))

typedef struct _GdmUserMenuItem GdmUserMenuItem;
typedef struct _GdmUserMenuItemClass GdmUserMenuItemClass;

GType      gdm_user_menu_item_get_type      (void) G_GNUC_CONST;

GtkWidget *gdm_user_menu_item_new           (GdmUser         *user);

GdmUser   *gdm_user_menu_item_get_user      (GdmUserMenuItem *item);

gint       gdm_user_menu_item_get_icon_size (GdmUserMenuItem *item);
void       gdm_user_menu_item_set_icon_size (GdmUserMenuItem *item,
                                             gint             pixel_size);

G_END_DECLS

#endif /* !__GDM_USER_MENU_ITEM__ */
