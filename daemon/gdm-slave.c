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
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-slave.h"
#include "gdm-slave-glue.h"

#include "gdm-server.h"
#include "gdm-greeter.h"

extern char **environ;

#define GDM_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SLAVE, GdmSlavePrivate))

#define GDM_SLAVE_COMMAND LIBEXECDIR"/gdm-slave"

#define GDM_DBUS_NAME	           "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

struct GdmSlavePrivate
{
	char            *id;
	GPid             pid;
        guint            output_watch_id;
        guint            error_watch_id;

	int              ping_interval;

	GPid             server_pid;
	Display         *server_display;

	/* cached display values */
	char            *display_id;
	char            *display_name;
	int             *display_number;
	char            *display_hostname;
	gboolean         display_is_local;
	gboolean         display_is_parented;
	char            *display_auth_file;
	char            *parent_display_name;
	char            *parent_display_auth_file;


	GdmServer       *server;
	GdmGreeter      *greeter;
	DBusGProxy      *display_proxy;
        DBusGConnection *connection;
};

enum {
	PROP_0,
	PROP_DISPLAY_ID,
};

static void	gdm_slave_class_init	(GdmSlaveClass *klass);
static void	gdm_slave_init	        (GdmSlave      *slave);
static void	gdm_slave_finalize	(GObject            *object);

G_DEFINE_TYPE (GdmSlave, gdm_slave, G_TYPE_OBJECT)

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
			g_debug ("waitpid () should not fail in 'GdmSpawn'");
		}
	}

	return status;
}

static void
slave_died (GdmSlave *slave)
{
	int exit_status;

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
	      GdmSlave *slave)
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
	     GdmSlave *slave)
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
spawn_slave (GdmSlave *slave)
{
	char	   *command;
	char	  **argv;
	gboolean    result;
	GIOChannel *channel;
	GError	   *error = NULL;
	int	    standard_output;
	int	    standard_error;


	result = FALSE;

	command = g_strdup_printf ("%s --id %s", GDM_SLAVE_COMMAND, slave->priv->display_id);

	if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

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
kill_slave (GdmSlave *slave)
{
	if (slave->priv->pid <= 1) {
		return;
	}

	signal_pid (slave->priv->pid, SIGTERM);

	/* watch should call slave_died */
}

static void
set_busy_cursor (GdmSlave *slave)
{
	if (slave->priv->server_display != NULL) {
		Cursor xcursor;

		xcursor = XCreateFontCursor (slave->priv->server_display, GDK_WATCH);
		XDefineCursor (slave->priv->server_display,
			       DefaultRootWindow (slave->priv->server_display),
			       xcursor);
		XFreeCursor (slave->priv->server_display, xcursor);
		XSync (slave->priv->server_display, False);
	}
}

static void
gdm_slave_whack_temp_auth_file (GdmSlave *slave)
{
#if 0
	uid_t old;

	old = geteuid ();
	if (old != 0)
		seteuid (0);
	if (d->parent_temp_auth_file != NULL) {
		VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
	}
	g_free (d->parent_temp_auth_file);
	d->parent_temp_auth_file = NULL;
	if (old != 0)
		seteuid (old);
#endif
}


static void
create_temp_auth_file (GdmSlave *slave)
{
#if 0
	if (d->type == TYPE_FLEXI_XNEST &&
	    d->parent_auth_file != NULL) {
		if (d->parent_temp_auth_file != NULL) {
			VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
		}
		g_free (d->parent_temp_auth_file);
		d->parent_temp_auth_file =
			copy_auth_file (d->server_uid,
					gdm_daemon_config_get_gdmuid (),
					d->parent_auth_file);
	}
#endif
}

static void
listify_hash (const char *key,
	      const char *value,
	      GPtrArray  *env)
{
	char *str;
	str = g_strdup_printf ("%s=%s", key, value);
	g_ptr_array_add (env, str);
}

static GPtrArray *
get_script_environment (GdmSlave   *slave,
			const char *username)
{
	GPtrArray     *env;
	char         **l;
	GHashTable    *hash;
	struct passwd *pwent;
	char          *x_servers_file;

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (l = environ; *l != NULL; l++) {
		char **str;
		str = g_strsplit (*l, "=", 2);
		g_hash_table_insert (hash, str[0], str[1]);
	}

	/* modify environment here */
	g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

	g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (username));
	g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (username));
	g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (username));

	pwent = getpwnam (username);
	if (pwent != NULL) {
		if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
			g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
			g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
		}

		g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
	}

	if (slave->priv->display_is_parented) {
		g_hash_table_insert (hash, g_strdup ("GDM_PARENT_DISPLAY"), g_strdup (slave->priv->parent_display_name));

		/*g_hash_table_insert (hash, "GDM_PARENT_XAUTHORITY"), slave->priv->parent_temp_auth_file));*/
	}

	/* some env for use with the Pre and Post scripts */
	x_servers_file = gdm_make_filename (AUTHDIR,
					    slave->priv->display_name,
					    ".Xservers");
	g_hash_table_insert (hash, g_strdup ("X_SERVERS"), x_servers_file);

	if (! slave->priv->display_is_local) {
		g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (slave->priv->display_hostname));
	}

	/* Runs as root */
	g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (slave->priv->display_auth_file));
	g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (slave->priv->display_name));

	/*g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);*/

	g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

#if 0
	if ( ! ve_string_empty (d->theme_name))
		g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);
#endif
	g_hash_table_remove (hash, "MAIL");


	g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
	g_hash_table_destroy (hash);

	g_ptr_array_add (env, NULL);

	return env;
}

static gboolean
gdm_slave_exec_script (GdmSlave      *slave,
		       const char    *dir,
		       const char    *login)
{
	char      *script;
	char     **argv;
	gint       status;
	GError    *error;
	GPtrArray *env;
	gboolean   res;
	gboolean   ret;

	g_assert (dir != NULL);
	g_assert (login != NULL);

	script = g_build_filename (dir, slave->priv->display_name, NULL);
	if (g_access (script, R_OK|X_OK) != 0) {
		g_free (script);
		script = NULL;
	}

	if (script == NULL &&
	    slave->priv->display_hostname != NULL) {
		script = g_build_filename (dir, slave->priv->display_hostname, NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}

#if 0
	if (script == NULL &&
	    SERVER_IS_XDMCP (d)) {
		script = g_build_filename (dir, "XDMCP", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}
	if (script == NULL &&
	    SERVER_IS_FLEXI (d)) {
		script = g_build_filename (dir, "Flexi", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}
#endif

	if (script == NULL) {
		script = g_build_filename (dir, "Default", NULL);
		if (g_access (script, R_OK|X_OK) != 0) {
			g_free (script);
			script = NULL;
		}
	}

	if (script == NULL) {
		return TRUE;
	}

	create_temp_auth_file (slave);

	g_debug ("Running process: %s", script);
	error = NULL;
	if (! g_shell_parse_argv (script, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

	env = get_script_environment (slave, login);

	res = g_spawn_sync (NULL,
			    argv,
			    (char **)env->pdata,
			    G_SPAWN_SEARCH_PATH,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    &status,
			    &error);

	g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

	gdm_slave_whack_temp_auth_file (slave);

	if (WIFEXITED (status)) {
		ret = WEXITSTATUS (status) != 0;
	} else {
		ret = TRUE;
	}

 out:
	g_free (script);

	return ret;
}

static gboolean
gdm_slave_run (GdmSlave *slave)
{
	/* if this is local display start a server if one doesn't
	 * exist */
	if (slave->priv->display_is_local) {
		gboolean res;

		slave->priv->server = gdm_server_new (slave->priv->display_name);

		res = gdm_server_start (slave->priv->server);
		if (! res) {
			g_warning (_("Could not start the X "
				     "server (your graphical environment) "
				     "due to some internal error. "
				     "Please contact your system administrator "
				     "or check your syslog to diagnose. "
				     "In the meantime this display will be "
				     "disabled.  Please restart GDM when "
				     "the problem is corrected."));
			exit (1);
		}
	}

	/* We can use d->handled from now on on this display,
	 * since the lookup was done in server start */

	g_setenv ("DISPLAY", slave->priv->display_name, TRUE);
	g_unsetenv ("XAUTHORITY"); /* just in case it's set */

#if 0
	gdm_auth_set_local_auth (d);
#endif

#if 0
	/* X error handlers to avoid the default one (i.e. exit (1)) */
	do_xfailed_on_xio_error = TRUE;
	XSetErrorHandler (gdm_slave_xerror_handler);
	XSetIOErrorHandler (gdm_slave_xioerror_handler);
#endif

	/* We keep our own (windowless) connection (dsp) open to avoid the
	 * X server resetting due to lack of active connections. */

	g_debug ("gdm_slave_run: Opening display %s", slave->priv->display_name);

	gdm_sigchld_block_push ();
	slave->priv->server_display = XOpenDisplay (slave->priv->display_name);
	gdm_sigchld_block_pop ();

	if (slave->priv->server_display == NULL) {
		return FALSE;
	}


	/* FIXME: handle wait for go */


	/* Set the busy cursor */
	set_busy_cursor (slave);


	/* FIXME: send a signal back to the master */

#if 0

	/* OK from now on it's really the user whacking us most likely,
	 * we have already started up well */
	do_xfailed_on_xio_error = FALSE;
#endif

	/* If XDMCP setup pinging */
	if ( ! slave->priv->display_is_local && slave->priv->ping_interval > 0) {
		alarm (slave->priv->ping_interval);
	}

#if 0
	/* checkout xinerama */
	gdm_screen_init (slave);
#endif

#ifdef HAVE_TSOL
	/* Check out Solaris Trusted Xserver extension */
	gdm_tsol_init (d);
#endif

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_slave_exec_script (slave,
			       GDMCONFDIR"/Init",
			       "gdm");

	slave->priv->greeter = gdm_greeter_new (slave->priv->display_name);
	gdm_greeter_start (slave->priv->greeter);

	/* If XDMCP stop pinging */
	if ( ! slave->priv->display_is_local) {
		alarm (0);
	}
}

gboolean
gdm_slave_start (GdmSlave *slave)
{
	gboolean res;
	char    *id;
	GError  *error;

	g_debug ("Starting slave");

	g_assert (slave->priv->display_proxy == NULL);

	g_debug ("Creating proxy for %s", slave->priv->display_id);
	slave->priv->display_proxy = dbus_g_proxy_new_for_name (slave->priv->connection,
								GDM_DBUS_NAME,
								slave->priv->display_id,
								GDM_DBUS_DISPLAY_INTERFACE);
	if (slave->priv->display_proxy == NULL) {
		g_warning ("Unable to create display proxy");
		return FALSE;
	}

	/* Make sure display ID works */
	error = NULL;
	res = dbus_g_proxy_call (slave->priv->display_proxy,
				 "GetId",
				 &error,
				 G_TYPE_INVALID,
				 DBUS_TYPE_G_OBJECT_PATH, &id,
				 G_TYPE_INVALID);
	if (! res) {
		if (error != NULL) {
			g_warning ("Failed to get display id %s: %s", slave->priv->display_id, error->message);
			g_error_free (error);
		} else {
			g_warning ("Failed to get display id %s", slave->priv->display_id);
		}

		return FALSE;
	}

	g_debug ("Got display id: %s", id);

	if (strcmp (id, slave->priv->display_id) != 0) {
		g_critical ("Display ID doesn't match");
		exit (1);
	}

	/* cache some values up front */
	error = NULL;
	res = dbus_g_proxy_call (slave->priv->display_proxy,
				 "IsLocal",
				 &error,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, &slave->priv->display_is_local,
				 G_TYPE_INVALID);
	if (! res) {
		if (error != NULL) {
			g_warning ("Failed to get value: %s", error->message);
			g_error_free (error);
		} else {
			g_warning ("Failed to get value");
		}

		return FALSE;
	}

	error = NULL;
	res = dbus_g_proxy_call (slave->priv->display_proxy,
				 "GetX11Display",
				 &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &slave->priv->display_name,
				 G_TYPE_INVALID);
	if (! res) {
		if (error != NULL) {
			g_warning ("Failed to get value: %s", error->message);
			g_error_free (error);
		} else {
			g_warning ("Failed to get value");
		}

		return FALSE;
	}

	gdm_slave_run (slave);

	return TRUE;
}

static gboolean
gdm_slave_stop (GdmSlave *slave)
{
	g_debug ("Stopping slave");

	if (slave->priv->display_proxy != NULL) {
		g_object_unref (slave->priv->display_proxy);
	}

	return TRUE;
}

static void
_gdm_slave_set_display_id (GdmSlave   *slave,
			   const char *id)
{
        g_free (slave->priv->display_id);
        slave->priv->display_id = g_strdup (id);
}

static void
gdm_slave_set_property (GObject      *object,
			guint	       prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GdmSlave *self;

	self = GDM_SLAVE (object);

	switch (prop_id) {
	case PROP_DISPLAY_ID:
		_gdm_slave_set_display_id (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_slave_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmSlave *self;

	self = GDM_SLAVE (object);

	switch (prop_id) {
	case PROP_DISPLAY_ID:
		g_value_set_string (value, self->priv->display_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
register_slave (GdmSlave *slave)
{
        GError *error = NULL;

        error = NULL;
        slave->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (slave->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (slave->priv->connection, slave->priv->id, G_OBJECT (slave));

        return TRUE;
}


static GObject *
gdm_slave_constructor (GType                  type,
		       guint                  n_construct_properties,
		       GObjectConstructParam *construct_properties)
{
        GdmSlave      *slave;
        GdmSlaveClass *klass;
	gboolean       res;
	const char    *id;

        klass = GDM_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_SLAVE));

        slave = GDM_SLAVE (G_OBJECT_CLASS (gdm_slave_parent_class)->constructor (type,
										 n_construct_properties,
										 construct_properties));

	id = NULL;
	if (g_str_has_prefix (slave->priv->display_id, "/org/gnome/DisplayManager/Display")) {
		id = slave->priv->display_id + strlen ("/org/gnome/DisplayManager/Display");
	}

	slave->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Slave%s", id);
	g_debug ("Registering %s", slave->priv->id);

        res = register_slave (slave);
        if (! res) {
		g_warning ("Unable to register slave with system bus");
        }

        return G_OBJECT (slave);
}

static void
gdm_slave_class_init (GdmSlaveClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_slave_get_property;
	object_class->set_property = gdm_slave_set_property;
        object_class->constructor = gdm_slave_constructor;
	object_class->finalize = gdm_slave_finalize;

	g_type_class_add_private (klass, sizeof (GdmSlavePrivate));

	g_object_class_install_property (object_class,
					 PROP_DISPLAY_ID,
					 g_param_spec_string ("display-id",
							      "id",
							      "id",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	dbus_g_object_type_install_info (GDM_TYPE_SLAVE, &dbus_glib_gdm_slave_object_info);
}

static void
gdm_slave_init (GdmSlave *slave)
{

	slave->priv = GDM_SLAVE_GET_PRIVATE (slave);

	slave->priv->pid = -1;
}

static void
gdm_slave_finalize (GObject *object)
{
	GdmSlave *slave;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SLAVE (object));

	slave = GDM_SLAVE (object);

	g_return_if_fail (slave->priv != NULL);

	G_OBJECT_CLASS (gdm_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_slave_new (const char *id)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SLAVE,
			       "display-id", id,
			       NULL);

	return GDM_SLAVE (object);
}
