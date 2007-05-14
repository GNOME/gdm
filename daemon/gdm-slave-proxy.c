/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-slave-proxy.h"

#define GDM_SLAVE_PROXY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SLAVE, GdmSlaveProxyPrivate))

#define GDM_SLAVE_PROXY_COMMAND LIBEXECDIR"/gdm-slave"

struct GdmSlaveProxyPrivate
{
	char    *display_id;
	GPid     pid;
        guint    output_watch_id;
        guint    error_watch_id;
};

enum {
	PROP_0,
	PROP_DISPLAY_ID,
};

static void	gdm_slave_proxy_class_init	(GdmSlaveProxyClass *klass);
static void	gdm_slave_proxy_init	        (GdmSlaveProxy      *slave);
static void	gdm_slave_proxy_finalize	(GObject            *object);

G_DEFINE_TYPE (GdmSlaveProxy, gdm_slave_proxy, G_TYPE_OBJECT)

/* adapted from gspawn.c */
static int
wait_on_child (int pid)
{
	int status;

 wait_again:
	if (waitpid (pid, &status, 0) < 0) {
		if (errno == EINTR) {
			goto wait_again;
		} else if (errno == ECHILD) {
			; /* do nothing, child already reaped */
		} else {
			g_debug ("waitpid () should not fail in 'GdmSpawnProxy'");
		}
	}

	return status;
}

static void
slave_died (GdmSlaveProxy *slave)
{
	int exit_status;

	if (slave->priv->pid < 0) {
		return;
	}

	g_debug ("Waiting on process %d", slave->priv->pid);
	exit_status = wait_on_child (slave->priv->pid);

	if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
		g_debug ("Wait on child process failed");
	} else {
		/* exited normally */
	}

	g_spawn_close_pid (slave->priv->pid);
	slave->priv->pid = -1;

	g_debug ("Slave died");
}

static gboolean
output_watch (GIOChannel    *source,
	      GIOCondition   condition,
	      GdmSlaveProxy *slave)
{
	gboolean finished = FALSE;

	if (condition & G_IO_IN) {
		GIOStatus status;
		GError	 *error = NULL;
		char	 *line;

		line = NULL;
		status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

		switch (status) {
		case G_IO_STATUS_NORMAL:
			{
				char *p;

				g_debug ("command output: %s", line);

				if ((p = strstr (line, "ADDRESS=")) != NULL) {
					char *address;

					address = g_strdup (p + strlen ("ADDRESS="));
					g_debug ("Got address %s", address);

					g_free (address);
				}
			}
			break;
		case G_IO_STATUS_EOF:
			finished = TRUE;
			break;
		case G_IO_STATUS_ERROR:
			finished = TRUE;
			g_debug ("Error reading from child: %s\n", error->message);
			return FALSE;
		case G_IO_STATUS_AGAIN:
		default:
			break;
		}

		g_free (line);
	} else if (condition & G_IO_HUP) {
		finished = TRUE;
	}

	if (finished) {
		slave_died (slave);

		slave->priv->output_watch_id = 0;

		return FALSE;
	}

	return TRUE;
}

/* just for debugging */
static gboolean
error_watch (GIOChannel	   *source,
	     GIOCondition   condition,
	     GdmSlaveProxy *slave)
{
	gboolean finished = FALSE;

	if (condition & G_IO_IN) {
		GIOStatus status;
		GError	 *error = NULL;
		char	 *line;

		line = NULL;
		status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

		switch (status) {
		case G_IO_STATUS_NORMAL:
			g_debug ("command error output: %s", line);
			break;
		case G_IO_STATUS_EOF:
			finished = TRUE;
			break;
		case G_IO_STATUS_ERROR:
			finished = TRUE;
			g_debug ("Error reading from child: %s\n", error->message);
			return FALSE;
		case G_IO_STATUS_AGAIN:
		default:
			break;
		}
		g_free (line);
	} else if (condition & G_IO_HUP) {
		finished = TRUE;
	}

	if (finished) {
		slave->priv->error_watch_id = 0;

		return FALSE;
	}

	return TRUE;
}

static gboolean
spawn_slave (GdmSlaveProxy *slave)
{
	char	   *command;
	char	  **argv;
	gboolean    result;
	GIOChannel *channel;
	GError	   *error = NULL;
	int	    standard_output;
	int	    standard_error;


	result = FALSE;

	command = g_strdup_printf ("%s --display-id %s", GDM_SLAVE_PROXY_COMMAND, slave->priv->display_id);

	if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Running command: %s", command);

	error = NULL;
	result = g_spawn_async_with_pipes (NULL,
					   argv,
					   NULL,
					   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
					   NULL,
					   NULL,
					   &slave->priv->pid,
					   NULL,
					   &standard_output,
					   &standard_error,
					   &error);

	if (! result) {
		g_warning ("Could not start command '%s': %s", command, error->message);
		g_error_free (error);
		g_strfreev (argv);
		goto out;
	}

	g_strfreev (argv);

	/* output channel */
	channel = g_io_channel_unix_new (standard_output);
	g_io_channel_set_close_on_unref (channel, TRUE);
	g_io_channel_set_flags (channel,
				g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
				NULL);
	slave->priv->output_watch_id = g_io_add_watch (channel,
						       G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
						       (GIOFunc)output_watch,
						       slave);
	g_io_channel_unref (channel);

	/* error channel */
	channel = g_io_channel_unix_new (standard_error);
	g_io_channel_set_close_on_unref (channel, TRUE);
	g_io_channel_set_flags (channel,
				g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
				NULL);
	slave->priv->error_watch_id = g_io_add_watch (channel,
						      G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
						      (GIOFunc)error_watch,
						      slave);
	g_io_channel_unref (channel);

	result = TRUE;

 out:
	g_free (command);

	return result;
}

static int
signal_pid (int pid,
	    int signal)
{
	int status = -1;

	/* perhaps block sigchld */
	g_debug ("Killing pid %d", pid);

	status = kill (pid, signal);

	if (status < 0) {
		if (errno == ESRCH) {
			g_warning ("Child process %lu was already dead.",
				   (unsigned long) pid);
		} else {
			g_warning ("Couldn't kill child process %lu: %s",
				   (unsigned long) pid,
				   g_strerror (errno));
		}
	}

	/* perhaps unblock sigchld */

	return status;
}

static void
kill_slave (GdmSlaveProxy *slave)
{
	if (slave->priv->pid <= 1) {
		return;
	}

	signal_pid (slave->priv->pid, SIGTERM);
	slave_died (slave);
}

gboolean
gdm_slave_proxy_start (GdmSlaveProxy *slave)
{
	spawn_slave (slave);

	return TRUE;
}

gboolean
gdm_slave_proxy_stop (GdmSlaveProxy *slave)
{
	g_debug ("Killing slave");

	kill_slave (slave);
	if (slave->priv->output_watch_id > 0) {
		g_source_remove (slave->priv->output_watch_id);
	}
	if (slave->priv->error_watch_id > 0) {
		g_source_remove (slave->priv->error_watch_id);
	}

	return TRUE;
}

static void
_gdm_slave_proxy_set_display_id (GdmSlaveProxy *slave,
				 const char    *id)
{
        g_free (slave->priv->display_id);
        slave->priv->display_id = g_strdup (id);
}

static void
gdm_slave_proxy_set_property (GObject      *object,
			guint	       prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GdmSlaveProxy *self;

	self = GDM_SLAVE_PROXY (object);

	switch (prop_id) {
	case PROP_DISPLAY_ID:
		_gdm_slave_proxy_set_display_id (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_slave_proxy_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmSlaveProxy *self;

	self = GDM_SLAVE_PROXY (object);

	switch (prop_id) {
	case PROP_DISPLAY_ID:
		g_value_set_string (value, self->priv->display_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_slave_proxy_class_init (GdmSlaveProxyClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_slave_proxy_get_property;
	object_class->set_property = gdm_slave_proxy_set_property;
	object_class->finalize = gdm_slave_proxy_finalize;

	g_type_class_add_private (klass, sizeof (GdmSlaveProxyPrivate));

	g_object_class_install_property (object_class,
					 PROP_DISPLAY_ID,
					 g_param_spec_string ("display-id",
							      "id",
							      "id",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

}

static void
gdm_slave_proxy_init (GdmSlaveProxy *slave)
{

	slave->priv = GDM_SLAVE_PROXY_GET_PRIVATE (slave);

	slave->priv->pid = -1;
}

static void
gdm_slave_proxy_finalize (GObject *object)
{
	GdmSlaveProxy *slave;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SLAVE (object));

	slave = GDM_SLAVE_PROXY (object);

	g_return_if_fail (slave->priv != NULL);

	G_OBJECT_CLASS (gdm_slave_proxy_parent_class)->finalize (object);
}

GdmSlaveProxy *
gdm_slave_proxy_new (const char *id)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SLAVE,
			       "display-id", id,
			       NULL);

	return GDM_SLAVE_PROXY (object);
}
