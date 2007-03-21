/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Signal routines
 *
 * (c) 2000, 2002 Queen of England
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _VE_SIGNAL_H
#define _VE_SIGNAL_H

#include <glib.h>

typedef gboolean (*VeSignalFunc) (int	        signal,
				  gpointer	data);
guint	ve_signal_add		(int		signal,
				 VeSignalFunc	function,
				 gpointer    	data);
guint   ve_signal_add_full      (int            priority,
				 int            signal,
				 VeSignalFunc   function,
				 gpointer       data,
				 GDestroyNotify destroy);
/* You must handle the signal notify yourself, you add
 * this function as the signal notification function
 * however */
void    ve_signal_notify        (int            signal);

gboolean ve_signal_was_notified (int            signal);
void    ve_signal_unnotify      (int            signal);

#endif /* _VE_CONFIG_H */
