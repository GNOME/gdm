/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2001 George Lebl
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

#ifndef GDM_WM_H
#define GDM_WM_H

#include <gnome.h>
#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xlib.h>

/* login window will be given focus every time a window
 * is killed */
void	gdm_wm_init			(Window login_window);

/* By default new windows aren't given focus, you have to
 * call this function with a TRUE */
void	gdm_wm_focus_new_windows	(gboolean focus);

void	gdm_wm_focus_window		(Window window);

/* movement for the impatient */
void	gdm_wm_move_window_now		(Window window,
					 int x,
					 int y);
void	gdm_wm_get_window_pos		(Window window,
					 int *xp,
					 int *yp);

/* refuse to focus the login window, poor mans modal dialogs */
void	gdm_wm_no_login_focus_push	(void);
void	gdm_wm_no_login_focus_pop	(void);

/*
 * Xinerama support stuff
 */
void	gdm_wm_screen_init		(int cur_screen_num);
void	gdm_wm_set_screen		(int cur_screen_num);

/* Not really a WM function, center a gtk window on current screen
 * by setting uposition */
void	gdm_wm_center_window		(GtkWindow *cw);

/* access to the screen structures */
extern GdkRectangle *gdm_wm_allscreens;
extern int gdm_wm_screens;
extern GdkRectangle gdm_wm_screen;


#endif /* GDM_WM_H */

/* EOF */
