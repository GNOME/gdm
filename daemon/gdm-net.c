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

#include "config.h"
#include <gnome.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "gdm-net.h"

/* Kind of a weird setup, new connections whack old connections */
#define MAX_CONNECTIONS 10

struct _GdmConnection {
	int fd;
	guint source;
	gboolean writable;

	GString *buffer;

	int close_level; /* 0 - normal
			    1 - no close, when called raise to 2
			    2 - close was requested */

	char *filename; /* unix socket or fifo filename */

	guint32 user_flags;

	GdmConnectionHandler handler;
	gpointer data;
	GDestroyNotify destroy_notify;

	gpointer close_data;
	GDestroyNotify close_notify;

	GdmConnection *parent;

	GList *subconnections;
	int n_subconnections;
};

static gboolean
close_if_needed (GdmConnection *conn, GIOCondition cond)
{
	/* non-subconnections are never closed */
	if (conn->filename != NULL)
		return TRUE;

	if (cond & G_IO_ERR ||
	    cond & G_IO_HUP) {
		gdm_debug ("close_if_needed: Got HUP/ERR on %d", conn->fd);
		conn->source = 0;
		gdm_connection_close (conn);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gdm_connection_handler (GIOChannel *source,
		        GIOCondition cond,
		        gpointer data)
{
	GdmConnection *conn = data;
	gchar buf[PIPE_SIZE];
	char *p;
	gint len;

	if ( ! (cond & G_IO_IN)) 
		return close_if_needed (conn, cond);

	if (g_io_channel_read (source, buf, sizeof (buf) - 1, &len)
	    != G_IO_ERROR_NONE)
		return close_if_needed (conn, cond);

	if (len <= 0)
		return close_if_needed (conn, cond);

	/* null terminate as the string is NOT */
	buf[len] = '\0';

	if (conn->buffer == NULL)
		conn->buffer = g_string_new (NULL);

	for (p = buf; *p != '\0'; p++) {
		if (*p == '\r' ||
		    (*p == '\n' &&
		     ve_string_empty (conn->buffer->str)))
			/*ignore \r or empty lines*/
			continue;
		if (*p == '\n') {
			conn->close_level = 1;
			conn->handler (conn, conn->buffer->str,
				       conn->data);
			if (conn->close_level == 2) {
				conn->close_level = 0;
				conn->source = 0;
				gdm_connection_close (conn);
				return FALSE;
			}
			conn->close_level = 0;
			g_string_truncate (conn->buffer, 0);
		} else {
			g_string_append_c (conn->buffer, *p);
		}
	}

	return close_if_needed (conn, cond);
}

gboolean
gdm_connection_is_writable (GdmConnection *conn)
{
	g_return_val_if_fail (conn != NULL, FALSE);

	return conn->writable;
}

gboolean
gdm_connection_write (GdmConnection *conn, const char *str)
{
	int ret;
#ifndef MSG_NOSIGNAL
	void (*old_handler)(int);
#endif

	g_return_val_if_fail (conn != NULL, FALSE);
	g_return_val_if_fail (str != NULL, FALSE);

	if ( ! conn->writable)
		return FALSE;

#ifdef MSG_NOSIGNAL
	ret = send (conn->fd, str, strlen (str), MSG_NOSIGNAL);
#else
	old_handler = signal (SIGPIPE, SIG_IGN);
	ret = send (conn->fd, str, strlen (str), 0);
	signal (SIGPIPE, old_handler);
#endif

	if (ret < 0)
		return FALSE;
	else
		return TRUE;
}

static gboolean
gdm_socket_handler (GIOChannel *source,
		    GIOCondition cond,
		    gpointer data)
{
	int fd;
	GIOChannel *unixchan;
	GdmConnection *conn = data;
	GdmConnection *newconn;
	struct sockaddr_un addr;
	socklen_t addr_size = sizeof (addr);

	if ( ! (cond & G_IO_IN)) 
		return TRUE;

	fd = accept (conn->fd,
		     (struct sockaddr *)&addr,
		     &addr_size);
	if (fd < 0) {
		gdm_debug ("gdm_socket_handler: Rejecting connection");
		return TRUE;
	}

	gdm_debug ("gdm_socket_handler: Accepting new connection fd %d", fd);

	newconn = g_new0 (GdmConnection, 1);
	newconn->close_level = 0;
	newconn->fd = fd;
	newconn->writable = TRUE;
	newconn->filename = NULL;
	newconn->user_flags = 0;
	newconn->buffer = NULL;
	newconn->parent = conn;
	newconn->subconnections = NULL;
	newconn->n_subconnections = 0;
	newconn->handler = conn->handler;
	newconn->data = conn->data;
	newconn->destroy_notify = NULL; /* the data belongs to
					   parent connection */

	conn->subconnections = g_list_append (conn->subconnections, newconn);
	conn->n_subconnections ++;
	if (conn->n_subconnections > MAX_CONNECTIONS) {
		GdmConnection *old = conn->subconnections->data;
		conn->subconnections =
			g_list_remove (conn->subconnections, old);
		gdm_connection_close (old);
	}

	unixchan = g_io_channel_unix_new (newconn->fd);
	newconn->source = g_io_add_watch_full
		(unixchan, G_PRIORITY_DEFAULT,
		 G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		 gdm_connection_handler, newconn, NULL);
	g_io_channel_unref (unixchan);

	return TRUE;
}

GdmConnection *
gdm_connection_open_unix (const char *sockname, mode_t mode)
{
	GIOChannel *unixchan;
	GdmConnection *conn;
	struct sockaddr_un addr;
	int fd;

	unlink (sockname);

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		gdm_error (_("%s: Could not make socket"),
			   "gdm_connection_open_unix");
		return NULL;
	}

	memset(&addr, 0, sizeof(addr));
	strcpy (addr.sun_path, sockname);
	addr.sun_family = AF_UNIX;
	if (bind (fd,
		  (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		gdm_error (_("%s: Could not bind socket"),
			   "gdm_connection_open_unix");
		close (fd);
		return NULL;
	}

	chmod (sockname, mode);

	conn = g_new0 (GdmConnection, 1);
	conn->close_level = 0;
	conn->fd = fd;
	conn->writable = FALSE;
	conn->buffer = NULL;
	conn->filename = g_strdup (sockname);
	conn->user_flags = 0;
	conn->parent = NULL;
	conn->subconnections = NULL;
	conn->n_subconnections = 0;

	unixchan = g_io_channel_unix_new (conn->fd);
	conn->source = g_io_add_watch_full
		(unixchan, G_PRIORITY_DEFAULT,
		 G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		 gdm_socket_handler, conn, NULL);
	g_io_channel_unref (unixchan);

	listen (fd, 5);

	return conn;
}

GdmConnection *
gdm_connection_open_fifo (const char *fifo, mode_t mode)
{
	GIOChannel *fifochan;
	GdmConnection *conn;
	int fd;

	unlink (fifo);

	if (mkfifo (fifo, 0660) < 0) {
		gdm_error (_("%s: Could not make FIFO"),
			   "gdm_connection_open_fifo");
		return NULL;
	}

	fd = open (fifo, O_RDWR); /* Open with write to avoid EOF */

	if (fd < 0) {
		gdm_error (_("%s: Could not open FIFO"),
			   "gdm_connection_open_fifo");
		return NULL;
	}

	chmod (fifo, mode);

	conn = g_new0 (GdmConnection, 1);
	conn->close_level = 0;
	conn->fd = fd;
	conn->writable = FALSE;
	conn->buffer = NULL;
	conn->filename = g_strdup (fifo);
	conn->user_flags = 0;
	conn->parent = NULL;
	conn->subconnections = NULL;
	conn->n_subconnections = 0;

	fifochan = g_io_channel_unix_new (conn->fd);
	conn->source = g_io_add_watch_full
		(fifochan, G_PRIORITY_DEFAULT,
		 G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
		 gdm_connection_handler, conn, NULL);
	g_io_channel_unref (fifochan);

	return conn;
}

void
gdm_connection_set_handler (GdmConnection *conn,
			    GdmConnectionHandler handler,
			    gpointer data,
			    GDestroyNotify destroy_notify)
{
	g_return_if_fail (conn != NULL);

	if (conn->destroy_notify != NULL)
		conn->destroy_notify (conn->data);

	conn->handler = handler;
	conn->data = data;
	conn->destroy_notify = destroy_notify;
}

guint32
gdm_connection_get_user_flags (GdmConnection *conn)
{
	g_return_val_if_fail (conn != NULL, 0);

	return conn->user_flags;
}

void
gdm_connection_set_user_flags (GdmConnection *conn,
			       guint32 flags)
{
	g_return_if_fail (conn != NULL);

	conn->user_flags = flags;
}

void
gdm_connection_close (GdmConnection *conn)
{
	GList *list;

	g_return_if_fail (conn != NULL);

	if (conn->close_level > 0) {
		/* flag that close was requested */
		conn->close_level = 2;
		return;
	}

	if (conn->close_notify != NULL) {
		conn->close_notify (conn->close_data);
		conn->close_notify = NULL;
	}
	conn->close_data = NULL;

	if (conn->buffer != NULL) {
		g_string_free (conn->buffer, TRUE);
		conn->buffer = NULL;
	}

	if (conn->parent != NULL) {
		conn->parent->subconnections =
			g_list_remove (conn->parent->subconnections, conn);
		/* This is evil a bit, but safe, whereas -- would not be */
		conn->parent->n_subconnections =
			g_list_length (conn->parent->subconnections);
		conn->parent = NULL;
	}

	list = conn->subconnections;
	conn->subconnections = NULL;
	g_list_foreach (list, (GFunc) gdm_connection_close, NULL);
	g_list_free (list);

	if (conn->destroy_notify != NULL) {
		conn->destroy_notify (conn->data);
		conn->destroy_notify = NULL;
	}
	conn->data = NULL;

	if (conn->source > 0) {
		g_source_remove (conn->source);
		conn->source = 0;
	}

	if (conn->fd > 0) {
		close (conn->fd);
		conn->fd = -1;
	}

	g_free (conn->filename);
	conn->filename = NULL;

	g_free (conn);
}

void
gdm_connection_set_close_notify (GdmConnection *conn,
				 gpointer close_data,
				 gpointer close_notify)
{
	g_return_if_fail (conn != NULL);

	if (conn->close_notify != NULL)
		conn->close_notify (conn->close_data);

	conn->close_data = close_data;
	conn->close_notify = close_notify;
}

/* EOF */
