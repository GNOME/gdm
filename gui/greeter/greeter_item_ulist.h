/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#ifndef GREETER_ITEM_ULIST_H
#define GREETER_ITEM_ULIST_H

#include "greeter_item.h"

gboolean greeter_item_ulist_setup (void);
void greeter_item_ulist_enable (void);
void greeter_item_ulist_disable (void);
void greeter_item_ulist_set_user (const char *user);
void greeter_item_ulist_unset_selected_user (void);
void greeter_item_ulist_select_user (gchar *login);
void greeter_item_ulist_check_show_userlist (void);

#endif
