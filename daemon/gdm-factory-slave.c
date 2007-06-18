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

#include "gdm-factory-slave.h"
#include "gdm-factory-slave-glue.h"

#include "gdm-server.h"
#include "gdm-greeter-proxy.h"
#include "gdm-greeter-server.h"
#include "gdm-session-relay.h"

extern char **environ;

#define GDM_FACTORY_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_FACTORY_SLAVE, GdmFactorySlavePrivate))

#define GDM_DBUS_NAME	                   "org.gnome.DisplayManager"
#define GDM_DBUS_FACTORY_DISPLAY_INTERFACE "org.gnome.DisplayManager.StaticFactoryDisplay"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmFactorySlavePrivate
{
	char             *id;
	GPid              pid;
        guint             output_watch_id;
        guint             error_watch_id;

	GPid              server_pid;
	Display          *server_display;
	guint             connection_attempts;

	GdmServer        *server;
	GdmSessionRelay  *session_relay;
	GdmGreeterServer *greeter_server;
	GdmGreeterProxy  *greeter;
	DBusGProxy       *factory_display_proxy;
        DBusGConnection  *connection;
};

enum {
	PROP_0,
};

enum {
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_factory_slave_class_init	(GdmFactorySlaveClass *klass);
static void	gdm_factory_slave_init	        (GdmFactorySlave      *factory_slave);
static void	gdm_factory_slave_finalize	(GObject             *object);

G_DEFINE_TYPE (GdmFactorySlave, gdm_factory_slave, GDM_TYPE_SLAVE)

static void
set_busy_cursor (GdmFactorySlave *factory_slave)
{
	if (factory_slave->priv->server_display != NULL) {
		Cursor xcursor;

		xcursor = XCreateFontCursor (factory_slave->priv->server_display, GDK_WATCH);
		XDefineCursor (factory_slave->priv->server_display,
			       DefaultRootWindow (factory_slave->priv->server_display),
			       xcursor);
		XFreeCursor (factory_slave->priv->server_display, xcursor);
		XSync (factory_slave->priv->server_display, False);
	}
}

static void
gdm_factory_slave_whack_temp_auth_file (GdmFactorySlave *factory_slave)
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
create_temp_auth_file (GdmFactorySlave *factory_slave)
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
	g_debug ("environment: %s", str);
	g_ptr_array_add (env, str);
}

static GPtrArray *
get_script_environment (GdmFactorySlave *slave,
			const char     *username)
{
	GPtrArray     *env;
	GHashTable    *hash;
	struct passwd *pwent;
	char          *x_servers_file;
	char          *display_name;
	char          *display_hostname;
	char          *display_x11_authority_file;
	gboolean       display_is_local;

	g_object_get (slave,
		      "display-name", &display_name,
		      "display-hostname", &display_hostname,
		      "display-is-local", &display_is_local,
		      "display-x11-authority-file", &display_x11_authority_file,
		      NULL);

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

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

#if 0
	if (display_is_parented) {
		g_hash_table_insert (hash, g_strdup ("GDM_PARENT_DISPLAY"), g_strdup (parent_display_name));

		/*g_hash_table_insert (hash, "GDM_PARENT_XAUTHORITY"), slave->priv->parent_temp_auth_file));*/
	}
#endif

	/* some env for use with the Pre and Post scripts */
	x_servers_file = gdm_make_filename (AUTHDIR,
					    display_name,
					    ".Xservers");
	g_hash_table_insert (hash, g_strdup ("X_SERVERS"), x_servers_file);

	if (! display_is_local) {
		g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (display_hostname));
	}

	/* Runs as root */
	g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (display_x11_authority_file));
	g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (display_name));

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

	g_free (display_name);
	g_free (display_hostname);
	g_free (display_x11_authority_file);

	return env;
}

static gboolean
gdm_factory_slave_exec_script (GdmFactorySlave *slave,
			      const char     *dir,
			      const char     *login)
{
	char      *script;
	char     **argv;
	gint       status;
	GError    *error;
	GPtrArray *env;
	gboolean   res;
	gboolean   ret;
	char      *display_name;
	char      *display_hostname;

	g_assert (dir != NULL);
	g_assert (login != NULL);

	g_object_get (slave,
		      "display-name", &display_name,
		      "display-hostname", &display_hostname,
		      NULL);

	script = g_build_filename (dir, display_name, NULL);
	if (g_access (script, R_OK|X_OK) != 0) {
		g_free (script);
		script = NULL;
	}

	if (script == NULL &&
	    display_hostname != NULL) {
		script = g_build_filename (dir, display_hostname, NULL);
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

	gdm_factory_slave_whack_temp_auth_file (slave);

	if (WIFEXITED (status)) {
		g_debug ("Process exit status: %d", WEXITSTATUS (status));
		ret = WEXITSTATUS (status) != 0;
	} else {
		ret = TRUE;
	}

 out:
	g_free (script);
	g_free (display_name);
	g_free (display_hostname);

	return ret;
}

static void
on_greeter_start (GdmGreeterProxy *greeter,
		  GdmFactorySlave  *slave)
{
	g_debug ("Greeter started");
}

static void
on_greeter_stop (GdmGreeterProxy *greeter,
		 GdmFactorySlave  *slave)
{
	g_debug ("Greeter stopped");
}

static void
on_relay_info (GdmSessionRelay *relay,
	       const char      *text,
	       GdmFactorySlave  *slave)
{
	g_debug ("Info: %s", text);
	gdm_greeter_server_info (slave->priv->greeter_server, text);
}

static void
on_relay_problem (GdmSessionRelay *relay,
		  const char      *text,
		  GdmFactorySlave  *slave)
{
	g_debug ("Problem: %s", text);
	gdm_greeter_server_problem (slave->priv->greeter_server, text);
}

static void
on_relay_info_query (GdmSessionRelay *relay,
		     const char      *text,
		     GdmFactorySlave  *slave)
{

	g_debug ("Info query: %s", text);
	gdm_greeter_server_info_query (slave->priv->greeter_server, text);
}

static void
on_relay_secret_info_query (GdmSessionRelay *relay,
			    const char      *text,
			    GdmFactorySlave  *slave)
{
	g_debug ("Secret info query: %s", text);
	gdm_greeter_server_secret_info_query (slave->priv->greeter_server, text);
}

static void
on_relay_ready (GdmSessionRelay *relay,
		GdmFactorySlave *slave)
{
	g_debug ("Relay is ready");
	gdm_session_relay_open (slave->priv->session_relay);

}

static void
on_greeter_answer (GdmGreeterServer *greeter_server,
		   const char       *text,
		   GdmFactorySlave  *slave)
{
	g_debug ("Greeter answer: %s", text);
	gdm_session_relay_answer_query (slave->priv->session_relay, text);
}

static void
on_greeter_session_selected (GdmGreeterServer *greeter_server,
			     const char       *text,
			     GdmFactorySlave  *slave)
{
	gdm_session_relay_select_session (slave->priv->session_relay, text);
}

static void
on_greeter_language_selected (GdmGreeterServer *greeter_server,
			      const char       *text,
			      GdmFactorySlave  *slave)
{
	gdm_session_relay_select_language (slave->priv->session_relay, text);
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
		      GdmFactorySlave  *slave)
{
	char    *display_id;
	char    *server_address;
	char    *product_id;
	GError  *error;
	gboolean res;

	g_debug ("Greeter started");

	error = NULL;
        GDM_FACTORY_SLAVE (slave)->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (GDM_FACTORY_SLAVE (slave)->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

	g_object_get (slave,
		      "display-id", &display_id,
		      NULL);

	g_debug ("Connecting to display %s", display_id);
	GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy = dbus_g_proxy_new_for_name (GDM_FACTORY_SLAVE (slave)->priv->connection,
											    GDM_DBUS_NAME,
											    display_id,
											    GDM_DBUS_FACTORY_DISPLAY_INTERFACE);
	g_free (display_id);

	if (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy == NULL) {
		g_warning ("Failed to create display proxy %s", display_id);
		return;
	}

	server_address = gdm_session_relay_get_address (slave->priv->session_relay);

	error = NULL;
	res = dbus_g_proxy_call (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy,
				 "CreateProductDisplay",
				 &error,
				 G_TYPE_STRING, server_address,
				 G_TYPE_INVALID,
				 DBUS_TYPE_G_OBJECT_PATH, &product_id,
				 G_TYPE_INVALID);
	g_free (server_address);

	if (! res) {
		if (error != NULL) {
			g_warning ("Failed to create product display: %s", error->message);
			g_error_free (error);
		} else {
			g_warning ("Failed to create product display");
		}
	}
}

static void
run_greeter (GdmFactorySlave *slave)
{
	gboolean       display_is_local;
	char          *display_name;
	char          *auth_file;
	char          *address;

	g_debug ("Running greeter");

	g_object_get (slave,
		      "display-is-local", &display_is_local,
		      "display-name", &display_name,
		      "display-x11-authority-file", &auth_file,
		      NULL);

	/* Set the busy cursor */
	set_busy_cursor (slave);

	/* FIXME: send a signal back to the master */

#if 0

	/* OK from now on it's really the user whacking us most likely,
	 * we have already started up well */
	do_xfailed_on_xio_error = FALSE;
#endif

#if 0
	/* checkout xinerama */
	gdm_screen_init (slave);
#endif

#ifdef HAVE_TSOL
	/* Check out Solaris Trusted Xserver extension */
	gdm_tsol_init (d);
#endif

	/* Run the init script. gdmslave suspends until script has terminated */
	gdm_factory_slave_exec_script (slave,
				       GDMCONFDIR"/Init",
				      "gdm");

	slave->priv->greeter_server = gdm_greeter_server_new ();
	g_signal_connect (slave->priv->greeter_server,
			  "query-answer",
			  G_CALLBACK (on_greeter_answer),
			  slave);
	g_signal_connect (slave->priv->greeter_server,
			  "session-selected",
			  G_CALLBACK (on_greeter_session_selected),
			  slave);
	g_signal_connect (slave->priv->greeter_server,
			  "language-selected",
			  G_CALLBACK (on_greeter_language_selected),
			  slave);
	g_signal_connect (slave->priv->greeter_server,
			  "connected",
			  G_CALLBACK (on_greeter_connected),
			  slave);
	gdm_greeter_server_start (slave->priv->greeter_server);

	address = gdm_greeter_server_get_address (slave->priv->greeter_server);

	slave->priv->greeter = gdm_greeter_proxy_new (display_name);
	g_signal_connect (slave->priv->greeter,
			  "started",
			  G_CALLBACK (on_greeter_start),
			  slave);
	g_signal_connect (slave->priv->greeter,
			  "stopped",
			  G_CALLBACK (on_greeter_stop),
			  slave);
	g_object_set (slave->priv->greeter,
		      "x11-authority-file", auth_file,
		      NULL);
	gdm_greeter_proxy_set_server_address (slave->priv->greeter, address);
	gdm_greeter_proxy_start (slave->priv->greeter);

	g_free (address);

	g_free (display_name);
	g_free (auth_file);
}

static void
set_local_auth (GdmFactorySlave *slave)
{
	GString *binary_cookie;
	GString *cookie;
	char    *display_x11_cookie;

	g_object_get (slave,
		      "display-x11-cookie", &display_x11_cookie,
		      NULL);

	g_debug ("Setting authorization key for display %s", display_x11_cookie);

	cookie = g_string_new (display_x11_cookie);
	binary_cookie = g_string_new (NULL);
	if (! gdm_string_hex_decode (cookie,
				     0,
				     NULL,
				     binary_cookie,
				     0)) {
		g_warning ("Unable to decode hex cookie");
		goto out;
	}

	g_debug ("Decoded cookie len %d", binary_cookie->len);

	XSetAuthorization ("MIT-MAGIC-COOKIE-1",
			   (int) strlen ("MIT-MAGIC-COOKIE-1"),
			   (char *)binary_cookie->str,
			   binary_cookie->len);

 out:
	g_string_free (binary_cookie, TRUE);
	g_string_free (cookie, TRUE);
	g_free (display_x11_cookie);
}

static gboolean
connect_to_display (GdmFactorySlave *slave)
{
	char          *display_name;
	gboolean       ret;

	ret = FALSE;

	g_object_get (slave,
		      "display-name", &display_name,
		      NULL);

	/* We keep our own (windowless) connection (dsp) open to avoid the
	 * X server resetting due to lack of active connections. */

	g_debug ("Server is ready - opening display %s", display_name);

	g_setenv ("DISPLAY", display_name, TRUE);
	g_unsetenv ("XAUTHORITY"); /* just in case it's set */

	set_local_auth (slave);

#if 0
	/* X error handlers to avoid the default one (i.e. exit (1)) */
	do_xfailed_on_xio_error = TRUE;
	XSetErrorHandler (gdm_factory_slave_xerror_handler);
	XSetIOErrorHandler (gdm_factory_slave_xioerror_handler);
#endif

	gdm_sigchld_block_push ();
	slave->priv->server_display = XOpenDisplay (display_name);
	gdm_sigchld_block_pop ();

	if (slave->priv->server_display == NULL) {
		g_warning ("Unable to connect to display %s", display_name);
		ret = FALSE;
	} else {
		g_debug ("Connected to display %s", display_name);
		ret = TRUE;
	}

	g_free (display_name);

	return ret;
}

static gboolean
idle_connect_to_display (GdmFactorySlave *slave)
{
	gboolean res;

	slave->priv->connection_attempts++;

	res = connect_to_display (slave);
	if (res) {
		/* FIXME: handle wait-for-go */

		run_greeter (slave);
	} else {
		if (slave->priv->connection_attempts >= MAX_CONNECT_ATTEMPTS) {
			g_warning ("Unable to connect to display after %d tries - bailing out", slave->priv->connection_attempts);
			exit (1);
		}
	}

	return FALSE;
}

static void
server_ready_cb (GdmServer *server,
		 GdmFactorySlave  *slave)
{
	g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static gboolean
gdm_factory_slave_run (GdmFactorySlave *slave)
{
	char    *display_name;
	gboolean display_is_local;

	g_object_get (slave,
		      "display-is-local", &display_is_local,
		      "display-name", &display_name,
		      NULL);

	/* if this is local display start a server if one doesn't
	 * exist */
	if (display_is_local) {
		gboolean res;

		slave->priv->server = gdm_server_new (display_name);

		g_signal_connect (slave->priv->server,
				  "ready",
				  G_CALLBACK (server_ready_cb),
				  slave);

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

		g_debug ("Started X server");
	} else {
		g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
	}

	g_free (display_name);

	return TRUE;
}

static gboolean
gdm_factory_slave_start (GdmSlave *slave)
{
	gboolean res;
	gboolean ret;

	ret = FALSE;

	g_debug ("Starting factory slave");

	res = GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->start (slave);


	GDM_FACTORY_SLAVE (slave)->priv->session_relay = gdm_session_relay_new ();
	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "info",
			  G_CALLBACK (on_relay_info),
			  slave);

	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "problem",
			  G_CALLBACK (on_relay_problem),
			  slave);

	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "info-query",
			  G_CALLBACK (on_relay_info_query),
			  slave);

	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "secret-info-query",
			  G_CALLBACK (on_relay_secret_info_query),
			  slave);
	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "ready",
			  G_CALLBACK (on_relay_ready),
			  slave);
#if 0
	g_signal_connect (GDM_FACTORY_SLAVE (slave)->priv->session_relay,
			  "user-verified",
			  G_CALLBACK (on_relay_user_verified),
			  slave);
#endif

	gdm_session_relay_start (GDM_FACTORY_SLAVE (slave)->priv->session_relay);

	gdm_factory_slave_run (GDM_FACTORY_SLAVE (slave));

	ret = TRUE;

	return ret;
}

static gboolean
gdm_factory_slave_stop (GdmSlave *slave)
{
	gboolean res;

	g_debug ("Stopping factory_slave");

	res = GDM_SLAVE_CLASS (gdm_factory_slave_parent_class)->stop (slave);

	if (GDM_FACTORY_SLAVE (slave)->priv->session_relay != NULL) {
		gdm_session_relay_stop (GDM_FACTORY_SLAVE (slave)->priv->session_relay);
		g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->session_relay);
		GDM_FACTORY_SLAVE (slave)->priv->session_relay = NULL;
	}

	if (GDM_FACTORY_SLAVE (slave)->priv->greeter_server != NULL) {
		gdm_greeter_server_stop (GDM_FACTORY_SLAVE (slave)->priv->greeter_server);
		g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->greeter_server);
		GDM_FACTORY_SLAVE (slave)->priv->greeter_server = NULL;
	}

	if (GDM_FACTORY_SLAVE (slave)->priv->greeter != NULL) {
		gdm_greeter_proxy_stop (GDM_FACTORY_SLAVE (slave)->priv->greeter);
		g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->greeter);
		GDM_FACTORY_SLAVE (slave)->priv->greeter = NULL;
	}

	if (GDM_FACTORY_SLAVE (slave)->priv->server != NULL) {
		gdm_server_stop (GDM_FACTORY_SLAVE (slave)->priv->server);
		g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->server);
		GDM_FACTORY_SLAVE (slave)->priv->server = NULL;
	}

	if (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy != NULL) {
		g_object_unref (GDM_FACTORY_SLAVE (slave)->priv->factory_display_proxy);
	}

	return TRUE;
}

static void
gdm_factory_slave_set_property (GObject      *object,
			       guint	      prop_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
	GdmFactorySlave *self;

	self = GDM_FACTORY_SLAVE (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_factory_slave_get_property (GObject    *object,
			       guint       prop_id,
			       GValue	   *value,
			       GParamSpec *pspec)
{
	GdmFactorySlave *self;

	self = GDM_FACTORY_SLAVE (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_factory_slave_constructor (GType                  type,
			      guint                  n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
        GdmFactorySlave      *factory_slave;
        GdmFactorySlaveClass *klass;

        klass = GDM_FACTORY_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_FACTORY_SLAVE));

        factory_slave = GDM_FACTORY_SLAVE (G_OBJECT_CLASS (gdm_factory_slave_parent_class)->constructor (type,
													 n_construct_properties,
													 construct_properties));

        return G_OBJECT (factory_slave);
}

static void
gdm_factory_slave_class_init (GdmFactorySlaveClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

	object_class->get_property = gdm_factory_slave_get_property;
	object_class->set_property = gdm_factory_slave_set_property;
        object_class->constructor = gdm_factory_slave_constructor;
	object_class->finalize = gdm_factory_slave_finalize;

	slave_class->start = gdm_factory_slave_start;
	slave_class->stop = gdm_factory_slave_stop;

	g_type_class_add_private (klass, sizeof (GdmFactorySlavePrivate));

	dbus_g_object_type_install_info (GDM_TYPE_FACTORY_SLAVE, &dbus_glib_gdm_factory_slave_object_info);
}

static void
gdm_factory_slave_init (GdmFactorySlave *factory_slave)
{

	factory_slave->priv = GDM_FACTORY_SLAVE_GET_PRIVATE (factory_slave);

	factory_slave->priv->pid = -1;
}

static void
gdm_factory_slave_finalize (GObject *object)
{
	GdmFactorySlave *factory_slave;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_FACTORY_SLAVE (object));

	factory_slave = GDM_FACTORY_SLAVE (object);

	g_return_if_fail (factory_slave->priv != NULL);

	G_OBJECT_CLASS (gdm_factory_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_factory_slave_new (const char *id)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_FACTORY_SLAVE,
			       "display-id", id,
			       NULL);

	return GDM_SLAVE (object);
}
