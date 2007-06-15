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
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
#include <sched.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"
#include "filecheck.h"

#include "gdm-greeter-proxy.h"

#define GDM_GREETER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define GDM_GREETER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"


extern char **environ;

#define GDM_GREETER_PROXY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_PROXY, GdmGreeterProxyPrivate))

struct GdmGreeterProxyPrivate
{
	char           *command;
	GPid            pid;

	char           *user_name;
	char           *group_name;

	char           *x11_display_name;
	char           *x11_authority_file;

	int             user_max_filesize;

	gboolean        interrupted;
	gboolean        always_restart_greeter;

	guint           child_watch_id;

	char           *server_address;
	DBusConnection *greeter_connection;
};

enum {
	PROP_0,
	PROP_X11_DISPLAY_NAME,
	PROP_X11_AUTHORITY_FILE,
	PROP_USER_NAME,
	PROP_GROUP_NAME,
	PROP_SERVER_ADDRESS,
};

enum {
	STARTED,
	STOPPED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_greeter_proxy_class_init	(GdmGreeterProxyClass *klass);
static void	gdm_greeter_proxy_init	(GdmGreeterProxy      *greeter_proxy);
static void	gdm_greeter_proxy_finalize	(GObject         *object);

G_DEFINE_TYPE (GdmGreeterProxy, gdm_greeter_proxy, G_TYPE_OBJECT)


static void
change_user (GdmGreeterProxy *greeter_proxy)

{
	struct passwd *pwent;
	struct group  *grent;

	if (greeter_proxy->priv->user_name == NULL) {
		return;
	}

	pwent = getpwnam (greeter_proxy->priv->user_name);
	if (pwent == NULL) {
		g_warning (_("GreeterProxy was to be spawned by user %s but that user doesn't exist"),
			   greeter_proxy->priv->user_name);
		_exit (1);
	}

	grent = getgrnam (greeter_proxy->priv->group_name);
	if (grent == NULL) {
		g_warning (_("GreeterProxy was to be spawned by group %s but that user doesn't exist"),
			   greeter_proxy->priv->group_name);
		_exit (1);
	}

	g_debug ("Changing (uid:gid) for child process to (%d:%d)",
		 pwent->pw_uid,
		 grent->gr_gid);

	if (pwent->pw_uid != 0) {
		if (setgid (grent->gr_gid) < 0)  {
			g_warning (_("Couldn't set groupid to %d"),
				   grent->gr_gid);
			_exit (1);
		}

		if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
			g_warning (_("initgroups () failed for %s"),
				   pwent->pw_name);
			_exit (1);
		}

		if (setuid (pwent->pw_uid) < 0)  {
			g_warning (_("Couldn't set userid to %d"),
				   (int)pwent->pw_uid);
			_exit (1);
		}
	} else {
		gid_t groups[1] = { 0 };

		if (setgid (0) < 0)  {
			g_warning (_("Couldn't set groupid to 0"));
			/* Don't error out, it's not fatal, if it fails we'll
			 * just still be */
		}

		/* this will get rid of any suplementary groups etc... */
		setgroups (1, groups);
	}
}

static void
greeter_proxy_child_setup (GdmGreeterProxy *greeter_proxy)
{
	change_user (greeter_proxy);
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
get_greeter_environment (GdmGreeterProxy *greeter_proxy)
{
	GPtrArray     *env;
	GHashTable    *hash;
	struct passwd *pwent;

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	g_hash_table_insert (hash, g_strdup ("GDM_GREETER_DBUS_ADDRESS"), g_strdup (greeter_proxy->priv->server_address));

	g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (greeter_proxy->priv->x11_authority_file));
	g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (greeter_proxy->priv->x11_display_name));

#if 0
	/* hackish ain't it */
	set_xnest_parent_stuff ();
#endif

	g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (greeter_proxy->priv->user_name));
	g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (greeter_proxy->priv->user_name));
	g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (greeter_proxy->priv->user_name));

	g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
	g_hash_table_remove (hash, "MAIL");

	g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

	pwent = getpwnam (greeter_proxy->priv->user_name);
	if (pwent != NULL) {
		if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
			g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
			g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (pwent->pw_dir));
		}

		g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
	}

#if 0
	defaultpath = gdm_daemon_config_get_value_string (GDM_KEY_PATH);
	if (ve_string_empty (g_getenv ("PATH"))) {
		g_setenv ("PATH", defaultpath, TRUE);
	} else if ( ! ve_string_empty (defaultpath)) {
		gchar *temp_string = g_strconcat (g_getenv ("PATH"),
						  ":", defaultpath, NULL);
		g_setenv ("PATH", temp_string, TRUE);
		g_free (temp_string);
	}
#endif

	g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

#if 0
	if ( ! ve_string_empty (d->theme_name))
		g_setenv ("GDM_GTK_THEME", d->theme_name, TRUE);
	if (gdm_daemon_config_get_value_bool (GDM_KEY_DEBUG_GESTURES)) {
		g_setenv ("G_DEBUG_GESTURES", "true", TRUE);
	}
#endif

#if 0
	if (SERVER_IS_FLEXI (d)) {
		g_setenv ("GDM_FLEXI_SERVER", "yes", TRUE);
	} else {
		g_unsetenv ("GDM_FLEXI_SERVER");
	}
#endif


	g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
	g_hash_table_destroy (hash);

	g_ptr_array_add (env, NULL);

	return env;
}

static void
gdm_slave_whack_temp_auth_file (GdmGreeterProxy *greeter_proxy)
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
create_temp_auth_file (GdmGreeterProxy *greeter_proxy)
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
gdm_slave_quick_exit (GdmGreeterProxy *greeter_proxy)
{
	/* FIXME */
	_exit (1);
}

static void
greeter_proxy_child_watch (GPid        pid,
		     int         status,
		     GdmGreeterProxy *greeter_proxy)
{
	g_debug ("child (pid:%d) done (%s:%d)",
		 (int) pid,
		 WIFEXITED (status) ? "status"
		 : WIFSIGNALED (status) ? "signal"
		 : "unknown",
		 WIFEXITED (status) ? WEXITSTATUS (status)
		 : WIFSIGNALED (status) ? WTERMSIG (status)
		 : -1);

	g_spawn_close_pid (greeter_proxy->priv->pid);
	greeter_proxy->priv->pid = -1;
}

static gboolean
gdm_greeter_proxy_spawn (GdmGreeterProxy *greeter_proxy)
{
	gchar          **argv;
	GError          *error;
	GPtrArray       *env;
	gboolean         ret;

	ret = FALSE;

	create_temp_auth_file (greeter_proxy);

	g_debug ("Running greeter_proxy process: %s", greeter_proxy->priv->command);

	argv = NULL;
	if (! g_shell_parse_argv (greeter_proxy->priv->command, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

	env = get_greeter_environment (greeter_proxy);

	error = NULL;
	ret = g_spawn_async_with_pipes (NULL,
					argv,
					(char **)env->pdata,
					G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
					(GSpawnChildSetupFunc)greeter_proxy_child_setup,
					greeter_proxy,
					&greeter_proxy->priv->pid,
					NULL,
					NULL,
					NULL,
					&error);

	g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

	if (! ret) {
		g_warning ("Could not start command '%s': %s",
			   greeter_proxy->priv->command,
			   error->message);
		g_error_free (error);
	} else {
		g_debug ("gdm_slave_greeter_proxy: GreeterProxy on pid %d", (int)greeter_proxy->priv->pid);
	}

	greeter_proxy->priv->child_watch_id = g_child_watch_add (greeter_proxy->priv->pid,
								 (GChildWatchFunc)greeter_proxy_child_watch,
								 greeter_proxy);

	g_strfreev (argv);
 out:

	return ret;
}

/**
 * gdm_greeter_proxy_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X greeter_proxy. Handles retries and fatal errors properly.
 */
gboolean
gdm_greeter_proxy_start (GdmGreeterProxy *greeter_proxy)
{
	gboolean    res;

	g_debug ("Starting greeter...");

	res = gdm_greeter_proxy_spawn (greeter_proxy);

	if (res) {

	}


	return res;
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
			g_debug ("waitpid () should not fail");
		}
	}

	return status;
}

static void
greeter_proxy_died (GdmGreeterProxy *greeter_proxy)
{
	int exit_status;

	g_debug ("Waiting on process %d", greeter_proxy->priv->pid);
	exit_status = wait_on_child (greeter_proxy->priv->pid);

	if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
		g_debug ("Wait on child process failed");
	} else {
		/* exited normally */
	}

	g_spawn_close_pid (greeter_proxy->priv->pid);
	greeter_proxy->priv->pid = -1;

	g_debug ("GreeterProxy died");
}

gboolean
gdm_greeter_proxy_stop (GdmGreeterProxy *greeter_proxy)
{

	if (greeter_proxy->priv->pid <= 1) {
		return TRUE;
	}

	/* remove watch source before we can wait on child */
	if (greeter_proxy->priv->child_watch_id > 0) {
		g_source_remove (greeter_proxy->priv->child_watch_id);
		greeter_proxy->priv->child_watch_id = 0;
	}

	g_debug ("Stopping greeter_proxy");

	signal_pid (greeter_proxy->priv->pid, SIGTERM);
	greeter_proxy_died (greeter_proxy);

	return TRUE;
}

void
gdm_greeter_proxy_set_server_address (GdmGreeterProxy *greeter_proxy,
				      const char      *address)
{
	g_return_if_fail (GDM_IS_GREETER_PROXY (greeter_proxy));

	g_free (greeter_proxy->priv->server_address);
	greeter_proxy->priv->server_address = g_strdup (address);
}

static void
_gdm_greeter_proxy_set_x11_display_name (GdmGreeterProxy *greeter_proxy,
					 const char *name)
{
        g_free (greeter_proxy->priv->x11_display_name);
        greeter_proxy->priv->x11_display_name = g_strdup (name);
}

static void
_gdm_greeter_proxy_set_x11_authority_file (GdmGreeterProxy *greeter_proxy,
					   const char *file)
{
        g_free (greeter_proxy->priv->x11_authority_file);
        greeter_proxy->priv->x11_authority_file = g_strdup (file);
}

static void
_gdm_greeter_proxy_set_user_name (GdmGreeterProxy *greeter_proxy,
				  const char *name)
{
        g_free (greeter_proxy->priv->user_name);
        greeter_proxy->priv->user_name = g_strdup (name);
}

static void
_gdm_greeter_proxy_set_group_name (GdmGreeterProxy *greeter_proxy,
				   const char *name)
{
        g_free (greeter_proxy->priv->group_name);
        greeter_proxy->priv->group_name = g_strdup (name);
}

static void
gdm_greeter_proxy_set_property (GObject      *object,
				guint	      prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GdmGreeterProxy *self;

	self = GDM_GREETER_PROXY (object);

	switch (prop_id) {
	case PROP_X11_DISPLAY_NAME:
		_gdm_greeter_proxy_set_x11_display_name (self, g_value_get_string (value));
		break;
	case PROP_X11_AUTHORITY_FILE:
		_gdm_greeter_proxy_set_x11_authority_file (self, g_value_get_string (value));
		break;
	case PROP_USER_NAME:
		_gdm_greeter_proxy_set_user_name (self, g_value_get_string (value));
		break;
	case PROP_GROUP_NAME:
		_gdm_greeter_proxy_set_group_name (self, g_value_get_string (value));
		break;
	case PROP_SERVER_ADDRESS:
		gdm_greeter_proxy_set_server_address (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_greeter_proxy_get_property (GObject    *object,
				guint       prop_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GdmGreeterProxy *self;

	self = GDM_GREETER_PROXY (object);

	switch (prop_id) {
	case PROP_X11_DISPLAY_NAME:
		g_value_set_string (value, self->priv->x11_display_name);
		break;
	case PROP_X11_AUTHORITY_FILE:
		g_value_set_string (value, self->priv->x11_authority_file);
		break;
	case PROP_USER_NAME:
		g_value_set_string (value, self->priv->user_name);
		break;
	case PROP_GROUP_NAME:
		g_value_set_string (value, self->priv->group_name);
		break;
	case PROP_SERVER_ADDRESS:
		g_value_set_string (value, self->priv->server_address);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_greeter_proxy_constructor (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
        GdmGreeterProxy      *greeter_proxy;
        GdmGreeterProxyClass *klass;

        klass = GDM_GREETER_PROXY_CLASS (g_type_class_peek (GDM_TYPE_GREETER_PROXY));

        greeter_proxy = GDM_GREETER_PROXY (G_OBJECT_CLASS (gdm_greeter_proxy_parent_class)->constructor (type,
										       n_construct_properties,
										       construct_properties));

        return G_OBJECT (greeter_proxy);
}

static void
gdm_greeter_proxy_class_init (GdmGreeterProxyClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_greeter_proxy_get_property;
	object_class->set_property = gdm_greeter_proxy_set_property;
        object_class->constructor = gdm_greeter_proxy_constructor;
	object_class->finalize = gdm_greeter_proxy_finalize;

	g_type_class_add_private (klass, sizeof (GdmGreeterProxyPrivate));

	g_object_class_install_property (object_class,
					 PROP_X11_DISPLAY_NAME,
					 g_param_spec_string ("x11-display-name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_X11_AUTHORITY_FILE,
					 g_param_spec_string ("x11-authority-file",
							      "authority file",
							      "authority file",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_USER_NAME,
					 g_param_spec_string ("user-name",
							      "user name",
							      "user name",
							      "gdm",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_GROUP_NAME,
					 g_param_spec_string ("group-name",
							      "group name",
							      "group name",
							      "gdm",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_SERVER_ADDRESS,
					 g_param_spec_string ("server-address",
							      "server address",
							      "server address",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	signals [STARTED] =
		g_signal_new ("started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterProxyClass, started),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [STOPPED] =
		g_signal_new ("stopped",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterProxyClass, stopped),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
gdm_greeter_proxy_init (GdmGreeterProxy *greeter_proxy)
{

	greeter_proxy->priv = GDM_GREETER_PROXY_GET_PRIVATE (greeter_proxy);

	greeter_proxy->priv->pid = -1;

	greeter_proxy->priv->command = g_strdup (LIBEXECDIR "/gdm-simple-greeter --g-fatal-warnings");
	greeter_proxy->priv->user_max_filesize = 65536;
}

static void
gdm_greeter_proxy_finalize (GObject *object)
{
	GdmGreeterProxy *greeter_proxy;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_GREETER_PROXY (object));

	greeter_proxy = GDM_GREETER_PROXY (object);

	g_return_if_fail (greeter_proxy->priv != NULL);

	gdm_greeter_proxy_stop (greeter_proxy);

	G_OBJECT_CLASS (gdm_greeter_proxy_parent_class)->finalize (object);
}

GdmGreeterProxy *
gdm_greeter_proxy_new (const char *display_name)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_GREETER_PROXY,
			       "x11-display-name", display_name,
			       NULL);

	return GDM_GREETER_PROXY (object);
}
