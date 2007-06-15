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

#include "gdm-simple-slave.h"
#include "gdm-simple-slave-glue.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-greeter-server.h"
#include "gdm-greeter-proxy.h"

extern char **environ;

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define GDM_DBUS_NAME	           "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmSimpleSlavePrivate
{
	char             *id;
	GPid              pid;
        guint             output_watch_id;
        guint             error_watch_id;

	int               ping_interval;

	GPid              server_pid;
	Display          *server_display;
	guint             connection_attempts;

	/* user selected */
	char             *selected_session;
	char             *selected_language;

	GdmServer        *server;
	GdmGreeterServer *greeter_server;
	GdmGreeterProxy  *greeter;
	GdmSession       *session;
	DBusGProxy       *display_proxy;
        DBusGConnection  *connection;
};

enum {
	PROP_0,
	PROP_DISPLAY_ID,
};

enum {
	SESSION_STARTED,
	SESSION_EXITED,
	SESSION_DIED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_simple_slave_class_init	(GdmSimpleSlaveClass *klass);
static void	gdm_simple_slave_init	        (GdmSimpleSlave      *simple_slave);
static void	gdm_simple_slave_finalize	(GObject             *object);

G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

static void
set_busy_cursor (GdmSimpleSlave *simple_slave)
{
	if (simple_slave->priv->server_display != NULL) {
		Cursor xcursor;

		xcursor = XCreateFontCursor (simple_slave->priv->server_display, GDK_WATCH);
		XDefineCursor (simple_slave->priv->server_display,
			       DefaultRootWindow (simple_slave->priv->server_display),
			       xcursor);
		XFreeCursor (simple_slave->priv->server_display, xcursor);
		XSync (simple_slave->priv->server_display, False);
	}
}

static void
gdm_simple_slave_whack_temp_auth_file (GdmSimpleSlave *simple_slave)
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
create_temp_auth_file (GdmSimpleSlave *simple_slave)
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
get_script_environment (GdmSimpleSlave *slave,
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
gdm_simple_slave_exec_script (GdmSimpleSlave *slave,
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

	gdm_simple_slave_whack_temp_auth_file (slave);

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
on_session_started (GdmSession *session,
                    GPid        pid,
		    GdmSimpleSlave   *slave)
{
	g_debug ("session started on pid %d\n", (int) pid);
	g_signal_emit (slave, signals [SESSION_STARTED], 0, pid);
}

static void
on_session_exited (GdmSession *session,
                   int         exit_code,
		   GdmSimpleSlave   *slave)
{
	g_debug ("session exited with code %d\n", exit_code);
	g_signal_emit (slave, signals [SESSION_EXITED], 0, exit_code);
}

static void
on_session_died (GdmSession *session,
                 int         signal_number,
		 GdmSimpleSlave   *slave)
{
	g_debug ("session died with signal %d, (%s)",
		 signal_number,
		 g_strsignal (signal_number));
	g_signal_emit (slave, signals [SESSION_DIED], 0, signal_number);
}

static gboolean
is_prog_in_path (const char *prog)
{
	char    *f;
	gboolean ret;

	f = g_find_program_in_path (prog);
	ret = (f != NULL);
	g_free (f);
	return ret;
}

static gboolean
get_session_command (const char *file,
		     char      **command)
{
	GKeyFile   *key_file;
	GError     *error;
	char       *full_path;
	char       *exec;
	gboolean    ret;
	gboolean    res;
	const char *search_dirs[] = {
		"/etc/X11/sessions/",
		DMCONFDIR "/Sessions/",
		DATADIR "/gdm/BuiltInSessions/",
		DATADIR "/xsessions/",
		NULL
	};

	exec = NULL;
	ret = FALSE;
	if (command != NULL) {
		*command = NULL;
	}

	key_file = g_key_file_new ();

	error = NULL;
	full_path = NULL;
	res = g_key_file_load_from_dirs (key_file,
					 file,
					 search_dirs,
					 &full_path,
					 G_KEY_FILE_NONE,
					 &error);
	if (! res) {
		g_debug ("File '%s' not found: %s", file, error->message);
		g_error_free (error);
		if (command != NULL) {
			*command = NULL;
		}
		goto out;
	}

	error = NULL;
	res = g_key_file_get_boolean (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_HIDDEN,
				      &error);
	if (error == NULL && res) {
		g_debug ("Session %s is marked as hidden", file);
		goto out;
	}

	error = NULL;
	exec = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC,
				      &error);
	if (exec == NULL) {
		g_debug ("%s key not found", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
		goto out;
	}

	res = is_prog_in_path (exec);
	g_free (exec);

	if (! res) {
		g_debug ("Command not found: %s", G_KEY_FILE_DESKTOP_KEY_TRY_EXEC);
		goto out;
	}

	error = NULL;
	exec = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_EXEC,
				      &error);
	if (error != NULL) {
		g_debug ("%s key not found: %s",
			 G_KEY_FILE_DESKTOP_KEY_EXEC,
			 error->message);
		g_error_free (error);
		goto out;
	}

	if (command != NULL) {
		*command = g_strdup (exec);
	}
	ret = TRUE;

out:
	g_free (exec);

	return ret;
}

static void
setup_session_environment (GdmSimpleSlave *slave)
{
	char *display_name;
	char *auth_file;

	g_object_get (slave,
		      "display-name", &display_name,
		      "display-x11-authority-file", &auth_file,
		      NULL);

	gdm_session_set_environment_variable (slave->priv->session,
					      "GDMSESSION",
					      slave->priv->selected_session);
	gdm_session_set_environment_variable (slave->priv->session,
					      "DESKTOP_SESSION",
					      slave->priv->selected_session);

	gdm_session_set_environment_variable (slave->priv->session,
					      "LANG",
					      slave->priv->selected_language);
	gdm_session_set_environment_variable (slave->priv->session,
					      "GDM_LANG",
					      slave->priv->selected_language);

	gdm_session_set_environment_variable (slave->priv->session,
					      "DISPLAY",
					      display_name);
	gdm_session_set_environment_variable (slave->priv->session,
					      "XAUTHORITY",
					      auth_file);

	gdm_session_set_environment_variable (slave->priv->session,
					      "PATH",
					      "/bin:/usr/bin:" BINDIR);

	g_free (display_name);
	g_free (auth_file);
}

static void
on_user_verified (GdmSession *session,
		  GdmSimpleSlave   *slave)
{
	char    *username;
	int      argc;
	char   **argv;
	char    *command;
	char    *filename;
	GError  *error;
	gboolean res;

	gdm_greeter_proxy_stop (slave->priv->greeter);
	gdm_greeter_server_stop (slave->priv->greeter_server);

	username = gdm_session_get_username (session);

	g_debug ("%s%ssuccessfully authenticated\n",
		 username ? username : "",
		 username ? " " : "");
	g_free (username);

	if (slave->priv->selected_session != NULL) {
		filename = g_strdup (slave->priv->selected_session);
	} else {
		filename = g_strdup ("gnome.desktop");
	}

	setup_session_environment (slave);

	res = get_session_command (filename, &command);
	if (! res) {
		g_warning ("Could find session file: %s", filename);
		return;
	}

	error = NULL;
	res = g_shell_parse_argv (command, &argc, &argv, &error);
	if (! res) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
	}

	gdm_session_start_program (session,
				   argc,
				   (const char **)argv);

	g_free (filename);
	g_free (command);
	g_strfreev (argv);
}

static void
on_user_verification_error (GdmSession     *session,
                            GError         *error,
			    GdmSimpleSlave *slave)
{
	char *username;

	username = gdm_session_get_username (session);

	g_debug ("%s%scould not be successfully authenticated: %s\n",
		 username ? username : "",
		 username ? " " : "",
		 error->message);

	g_free (username);
}

static void
on_info (GdmSession     *session,
         const char     *text,
	 GdmSimpleSlave *slave)
{
	g_debug ("Info: %s", text);
	gdm_greeter_server_info (slave->priv->greeter_server, text);
}

static void
on_problem (GdmSession     *session,
            const char     *text,
	    GdmSimpleSlave *slave)
{
	g_debug ("Problem: %s", text);
	gdm_greeter_server_problem (slave->priv->greeter_server, text);
}

static void
on_info_query (GdmSession     *session,
               const char     *text,
	       GdmSimpleSlave *slave)
{

	g_debug ("Info query: %s", text);
	gdm_greeter_server_info_query (slave->priv->greeter_server, text);
}

static void
on_secret_info_query (GdmSession     *session,
                      const char     *text,
		      GdmSimpleSlave *slave)
{
	g_debug ("Secret info query: %s", text);
	gdm_greeter_server_secret_info_query (slave->priv->greeter_server, text);
}

static void
on_greeter_answer (GdmGreeterServer *greeter_server,
		   const char       *text,
		   GdmSimpleSlave   *slave)
{
	gdm_session_answer_query (slave->priv->session, text);
}

static void
on_greeter_session_selected (GdmGreeterServer *greeter_server,
			     const char       *text,
			     GdmSimpleSlave   *slave)
{
	g_free (slave->priv->selected_session);
	slave->priv->selected_session = g_strdup (text);
}

static void
on_greeter_language_selected (GdmGreeterServer *greeter_server,
			      const char       *text,
			      GdmSimpleSlave   *slave)
{
	g_free (slave->priv->selected_language);
	slave->priv->selected_language = g_strdup (text);
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
		      GdmSimpleSlave   *slave)
{
	gboolean       display_is_local;

	g_object_get (slave,
		      "display-is-local", &display_is_local,
		      NULL);

	g_debug ("Greeter started");

	gdm_session_open (slave->priv->session,
			  "gdm",
			  NULL /* hostname */,
			  "/dev/console",
			  STDOUT_FILENO,
			  STDERR_FILENO,
			  NULL);

	/* If XDMCP stop pinging */
	if ( ! display_is_local) {
		alarm (0);
	}
}

static void
run_greeter (GdmSimpleSlave *slave)
{
	gboolean       display_is_local;
	char          *display_name;
	char          *auth_file;

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

	/* If XDMCP setup pinging */
	if ( ! display_is_local && slave->priv->ping_interval > 0) {
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
	gdm_simple_slave_exec_script (slave,
				      GDMCONFDIR"/Init",
				      "gdm");

	slave->priv->session = gdm_session_new ();

	g_signal_connect (slave->priv->session,
			  "info",
			  G_CALLBACK (on_info),
			  slave);

	g_signal_connect (slave->priv->session,
			  "problem",
			  G_CALLBACK (on_problem),
			  slave);

	g_signal_connect (slave->priv->session,
			  "info-query",
			  G_CALLBACK (on_info_query),
			  slave);

	g_signal_connect (slave->priv->session,
			  "secret-info-query",
			  G_CALLBACK (on_secret_info_query),
			  slave);

	g_signal_connect (slave->priv->session,
			  "user-verified",
			  G_CALLBACK (on_user_verified),
			  slave);

	g_signal_connect (slave->priv->session,
			  "user-verification-error",
			  G_CALLBACK (on_user_verification_error),
			  slave);

	g_signal_connect (slave->priv->session,
			  "session-started",
			  G_CALLBACK (on_session_started),
			  slave);
	g_signal_connect (slave->priv->session,
			  "session-exited",
			  G_CALLBACK (on_session_exited),
			  slave);
	g_signal_connect (slave->priv->session,
			  "session-died",
			  G_CALLBACK (on_session_died),
			  slave);

	slave->priv->greeter_server = gdm_greeter_server_new (display_name);
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

	slave->priv->greeter = gdm_greeter_proxy_new (display_name);
	g_object_set (slave->priv->greeter,
		      "x11-authority-file", auth_file,
		      NULL);
	gdm_greeter_proxy_start (slave->priv->greeter);

	g_free (display_name);
	g_free (auth_file);
}

static void
set_local_auth (GdmSimpleSlave *slave)
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
connect_to_display (GdmSimpleSlave *slave)
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
	XSetErrorHandler (gdm_simple_slave_xerror_handler);
	XSetIOErrorHandler (gdm_simple_slave_xioerror_handler);
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
idle_connect_to_display (GdmSimpleSlave *slave)
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
		 GdmSimpleSlave  *slave)
{
	g_timeout_add (500, (GSourceFunc)idle_connect_to_display, slave);
}

static gboolean
gdm_simple_slave_run (GdmSimpleSlave *slave)
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
gdm_simple_slave_start (GdmSlave *slave)
{
	gboolean res;

	res = GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->start (slave);

	gdm_simple_slave_run (GDM_SIMPLE_SLAVE (slave));

	return TRUE;
}

static gboolean
gdm_simple_slave_stop (GdmSlave *slave)
{
	gboolean res;

	g_debug ("Stopping simple_slave");

	res = GDM_SLAVE_CLASS (gdm_simple_slave_parent_class)->stop (slave);

	if (GDM_SIMPLE_SLAVE (slave)->priv->greeter != NULL) {
		gdm_greeter_proxy_stop (GDM_SIMPLE_SLAVE (slave)->priv->greeter);
		g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->greeter);
		GDM_SIMPLE_SLAVE (slave)->priv->greeter = NULL;
	}

	if (GDM_SIMPLE_SLAVE (slave)->priv->session != NULL) {
		gdm_session_close (GDM_SIMPLE_SLAVE (slave)->priv->session);
		g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->session);
		GDM_SIMPLE_SLAVE (slave)->priv->session = NULL;
	}

	if (GDM_SIMPLE_SLAVE (slave)->priv->server != NULL) {
		gdm_server_stop (GDM_SIMPLE_SLAVE (slave)->priv->server);
		g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->server);
		GDM_SIMPLE_SLAVE (slave)->priv->server = NULL;
	}

	if (GDM_SIMPLE_SLAVE (slave)->priv->display_proxy != NULL) {
		g_object_unref (GDM_SIMPLE_SLAVE (slave)->priv->display_proxy);
	}

	return TRUE;
}

static void
gdm_simple_slave_set_property (GObject      *object,
			       guint	      prop_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
	GdmSimpleSlave *self;

	self = GDM_SIMPLE_SLAVE (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_simple_slave_get_property (GObject    *object,
			       guint       prop_id,
			       GValue	   *value,
			       GParamSpec *pspec)
{
	GdmSimpleSlave *self;

	self = GDM_SIMPLE_SLAVE (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_simple_slave_constructor (GType                  type,
			      guint                  n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
        GdmSimpleSlave      *simple_slave;
        GdmSimpleSlaveClass *klass;

        klass = GDM_SIMPLE_SLAVE_CLASS (g_type_class_peek (GDM_TYPE_SIMPLE_SLAVE));

        simple_slave = GDM_SIMPLE_SLAVE (G_OBJECT_CLASS (gdm_simple_slave_parent_class)->constructor (type,
										 n_construct_properties,
										 construct_properties));

        return G_OBJECT (simple_slave);
}

static void
gdm_simple_slave_class_init (GdmSimpleSlaveClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	GdmSlaveClass *slave_class = GDM_SLAVE_CLASS (klass);

	object_class->get_property = gdm_simple_slave_get_property;
	object_class->set_property = gdm_simple_slave_set_property;
        object_class->constructor = gdm_simple_slave_constructor;
	object_class->finalize = gdm_simple_slave_finalize;

	slave_class->start = gdm_simple_slave_start;
	slave_class->stop = gdm_simple_slave_stop;

	g_type_class_add_private (klass, sizeof (GdmSimpleSlavePrivate));

	dbus_g_object_type_install_info (GDM_TYPE_SIMPLE_SLAVE, &dbus_glib_gdm_simple_slave_object_info);
}

static void
gdm_simple_slave_init (GdmSimpleSlave *simple_slave)
{

	simple_slave->priv = GDM_SIMPLE_SLAVE_GET_PRIVATE (simple_slave);

	simple_slave->priv->pid = -1;
}

static void
gdm_simple_slave_finalize (GObject *object)
{
	GdmSimpleSlave *simple_slave;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SIMPLE_SLAVE (object));

	simple_slave = GDM_SIMPLE_SLAVE (object);

	g_return_if_fail (simple_slave->priv != NULL);

	G_OBJECT_CLASS (gdm_simple_slave_parent_class)->finalize (object);
}

GdmSlave *
gdm_simple_slave_new (const char *id)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SIMPLE_SLAVE,
			       "display-id", id,
			       NULL);

	return GDM_SLAVE (object);
}
