/* GDM - The Gnome Display Manager
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

#ifndef GDM_NET_H
#define GDM_NET_H

#include <glib.h>

#ifndef TYPEDEF_GDM_CONNECTION
#define TYPEDEF_GDM_CONNECTION
typedef struct _GdmConnection GdmConnection;
#endif  /* TYPEDEF_GDM_CONNECTION */

/* Something that will get stuff line by line */
typedef void (* GdmConnectionHandler) (GdmConnection *conn,
				       const char *str,
				       gpointer data);

gboolean	gdm_connection_is_writable (GdmConnection *conn);
gboolean	gdm_connection_write (GdmConnection *conn,
		                      const char *str);

GdmConnection *	gdm_connection_open_unix (const char *sockname,
					  mode_t mode);
GdmConnection *	gdm_connection_open_fifo (const char *fifo,
					  mode_t mode);

void		gdm_connection_set_close_notify (GdmConnection *conn,
						 gpointer close_data,
						 gpointer close_notify);

void		gdm_connection_set_handler (GdmConnection *conn,
					    GdmConnectionHandler handler,
					    gpointer data,
					    GDestroyNotify destroy_notify);

guint32		gdm_connection_get_user_flags (GdmConnection *conn);
void		gdm_connection_set_user_flags (GdmConnection *conn,
					       guint32 flags);
#define		GDM_CONNECTION_SET_USER_FLAG(conn,flag) {			\
			guint32 _flags = gdm_connection_get_user_flags (conn);	\
			_flags |= flag;						\
			gdm_connection_set_user_flags (conn, _flags);		\
		}
#define		GDM_CONNECTION_UNSET_USER_FLAG(conn,flag) {			\
			guint32 _flags = gdm_connection_get_user_flags (conn);	\
			_flags &= ~flag;					\
			gdm_connection_set_user_flags (conn, _flags);		\
		}

void		gdm_connection_close (GdmConnection *conn);

#endif /* GDM_NET_H */

/* EOF */
