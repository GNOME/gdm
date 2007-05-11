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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-server.h"

extern char **environ;

#define GDM_SERVER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SERVER, GdmServerPrivate))

/* These are the servstat values, also used as server
 * process exit codes */
#define SERVER_TIMEOUT 2	/* Server didn't start */
#define SERVER_DEAD 250		/* Server stopped */
#define SERVER_PENDING 251	/* Server started but not ready for connections yet */
#define SERVER_RUNNING 252	/* Server running and ready for connections */
#define SERVER_ABORT 253	/* Server failed badly. Suspending display. */

struct GdmServerPrivate
{
	char    *command;
	GPid     pid;

	gboolean disable_tcp;
	int      priority;
	uid_t    uid;
	char    *session_args;

	char    *log_dir;
	char    *display_name;
	char    *auth_file;

	gboolean is_parented;
	char    *parent_display_name;
	char    *parent_auth_file;
	char    *chosen_hostname;
};

enum {
	PROP_0,
	PROP_DISPLAY_NAME,
	PROP_AUTH_FILE,
	PROP_IS_PARENTED,
	PROP_PARENT_DISPLAY_NAME,
	PROP_PARENT_AUTH_FILE,
	PROP_CHOSEN_HOSTNAME,
	PROP_COMMAND,
	PROP_PRIORITY,
	PROP_UID,
	PROP_SESSION_ARGS,
	PROP_LOG_DIR,
	PROP_DISABLE_TCP,
};

enum {
	READY,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_server_class_init	(GdmServerClass *klass);
static void	gdm_server_init	        (GdmServer      *server);
static void	gdm_server_finalize	(GObject        *object);

G_DEFINE_TYPE (GdmServer, gdm_server, G_TYPE_OBJECT)

/* copied from nautilus */
static int ready_pipes[2];

static gboolean
ready_io_cb (GIOChannel  *io,
	     GIOCondition condition,
	     GdmServer   *server)
{
        char a;

        while (read (ready_pipes[0], &a, 1) != 1)
                ;

	g_debug ("Got USR1 from X server - emitting READY");

	g_signal_emit (server, signals[READY], 0);

        return TRUE;
}

static void
sigusr1_handler (int sig)
{
        while (write (ready_pipes[1], "a", 1) != 1)
                ;
}

static void
setup_ready_signal (GdmServer *server)
{
        struct sigaction sa;
        GIOChannel      *io;

        if (pipe (ready_pipes) == -1) {
                g_error ("Could not create pipe() for ready signal");
        }

        io = g_io_channel_unix_new (ready_pipes[0]);
        g_io_add_watch (io, G_IO_IN, (GIOFunc)ready_io_cb, server);

        sa.sa_handler = sigusr1_handler;
        sigemptyset (&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction (SIGUSR1, &sa, NULL);
}

gboolean
gdm_server_stop (GdmServer *server)
{
	g_debug ("Stopping server");

	return TRUE;
}

/* We keep a connection (parent_dsp) open with the parent X server
 * before running a proxy on it to prevent the X server resetting
 * as we open and close other connections.
 * Note that XDMCP servers, by default, reset when the seed X
 * connection closes whereas usually the X server only quits when
 * all X connections have closed.
 */
#if 0
static gboolean
connect_to_parent (GdmServer *server)
{
	int maxtries;
	int openretries;

	g_debug ("gdm_server_start: Connecting to parent display \'%s\'",
		   d->parent_disp);

	d->parent_dsp = NULL;

	maxtries = SERVER_IS_XDMCP (d) ? 10 : 2;

	openretries = 0;
	while (openretries < maxtries &&
	       d->parent_dsp == NULL) {
		d->parent_dsp = XOpenDisplay (d->parent_disp);

		if G_UNLIKELY (d->parent_dsp == NULL) {
			g_debug ("gdm_server_start: Sleeping %d on a retry", 1+openretries*2);
			gdm_sleep_no_signal (1+openretries*2);
			openretries++;
		}
	}

	if (d->parent_dsp == NULL)
		gdm_error (_("%s: failed to connect to parent display \'%s\'"),
			   "gdm_server_start", d->parent_disp);

	return d->parent_dsp != NULL;
}
#endif

static gboolean
gdm_server_resolve_command_line (GdmServer  *server,
				 const char *vtarg,
				 int        *argcp,
				 char     ***argvp)
{
	int      argc;
	char   **argv;
	int      len;
	int      i;
	gboolean gotvtarg = FALSE;
	gboolean query_in_arglist = FALSE;

	g_shell_parse_argv (server->priv->command, &argc, &argv, NULL);

	for (len = 0; argv != NULL && argv[len] != NULL; len++) {
		char *arg = argv[len];

		/* HACK! Not to add vt argument to servers that already force
		 * allocation.  Mostly for backwards compat only */
		if (strncmp (arg, "vt", 2) == 0 &&
		    isdigit (arg[2]) &&
		    (arg[3] == '\0' ||
		     (isdigit (arg[3]) && arg[4] == '\0')))
			gotvtarg = TRUE;
		if (strcmp (arg, "-query") == 0 ||
		    strcmp (arg, "-indirect") == 0)
			query_in_arglist = TRUE;
	}

	argv = g_renew (char *, argv, len + 10);
	/* shift args down one */
	for (i = len - 1; i >= 1; i--) {
		argv[i+1] = argv[i];
	}

	/* server number is the FIRST argument, before any others */
	argv[1] = g_strdup (server->priv->display_name);
	len++;

	if (server->priv->auth_file != NULL) {
		argv[len++] = g_strdup ("-auth");
		argv[len++] = g_strdup (server->priv->auth_file);
	}

	if (server->priv->chosen_hostname) {
		/* run just one session */
		argv[len++] = g_strdup ("-terminate");
		argv[len++] = g_strdup ("-query");
		argv[len++] = g_strdup (server->priv->chosen_hostname);
		query_in_arglist = TRUE;
	}

	if (server->priv->disable_tcp && ! query_in_arglist) {
		argv[len++] = g_strdup ("-nolisten");
		argv[len++] = g_strdup ("tcp");
	}

	if (vtarg != NULL && ! gotvtarg) {
		argv[len++] = g_strdup (vtarg);
	}

	argv[len++] = NULL;

	*argvp = argv;
	*argcp = len;

	return TRUE;
}

/* somewhat safer rename (safer if the log dir is unsafe), may in fact
   lose the file though, it guarantees that a is gone, but not that
   b exists */
static void
safer_rename (const char *a, const char *b)
{
	errno = 0;
	if (link (a, b) < 0) {
		if (errno == EEXIST) {
			VE_IGNORE_EINTR (g_unlink (a));
			return;
		}
		VE_IGNORE_EINTR (g_unlink (b));
		/* likely this system doesn't support hard links */
		g_rename (a, b);
		VE_IGNORE_EINTR (g_unlink (a));
		return;
	}
	VE_IGNORE_EINTR (g_unlink (a));
}

static void
rotate_logs (GdmServer *server)
{
	const char *dname;
	const char *logdir;

	dname = server->priv->display_name;
	logdir = server->priv->log_dir;

	/* I'm too lazy to write a loop */
	char *fname4 = gdm_make_filename (logdir, dname, ".log.4");
	char *fname3 = gdm_make_filename (logdir, dname, ".log.3");
	char *fname2 = gdm_make_filename (logdir, dname, ".log.2");
	char *fname1 = gdm_make_filename (logdir, dname, ".log.1");
	char *fname = gdm_make_filename (logdir, dname, ".log");

	/* Rotate the logs (keep 4 last) */
	VE_IGNORE_EINTR (g_unlink (fname4));
	safer_rename (fname3, fname4);
	safer_rename (fname2, fname3);
	safer_rename (fname1, fname2);
	safer_rename (fname, fname1);

	g_free (fname4);
	g_free (fname3);
	g_free (fname2);
	g_free (fname1);
	g_free (fname);
}

static void
change_user (GdmServer *server)
{
	if (server->priv->uid != 0) {
		struct passwd *pwent;

		g_debug ("Changing user for child process: %d", (int)server->priv->uid);

		pwent = getpwuid (server->priv->uid);
		if (pwent == NULL) {
			g_warning (_("%s: Server was to be spawned by uid %d but "
				     "that user doesn't exist"),
				   "gdm_server_spawn",
				   (int)server->priv->uid);
			_exit (SERVER_ABORT);
		}

		if (setgid (pwent->pw_gid) < 0)  {
			g_warning (_("%s: Couldn't set groupid to %d"), 
				   "gdm_server_spawn", (int)pwent->pw_gid);
			_exit (SERVER_ABORT);
		}

		if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
			g_warning (_("%s: initgroups () failed for %s"),
				   "gdm_server_spawn", pwent->pw_name);
			_exit (SERVER_ABORT);
		}

		if (setuid (server->priv->uid) < 0)  {
			g_warning (_("%s: Couldn't set userid to %d"),
				   "gdm_server_spawn", (int)server->priv->uid);
			_exit (SERVER_ABORT);
		}
	} else {
		gid_t groups[1] = { 0 };

		if (setgid (0) < 0)  {
			g_warning (_("%s: Couldn't set groupid to 0"), 
				   "gdm_server_spawn");
			/* Don't error out, it's not fatal, if it fails we'll
			 * just still be */
		}
		/* this will get rid of any suplementary groups etc... */
		setgroups (1, groups);
	}
}

static void
server_child_setup (GdmServer *server)
{
	char            *logfile;
	int              logfd;
	struct sigaction ign_signal;
	sigset_t         mask;

	/* Rotate the X server logs */
	rotate_logs (server);

	/* Log all output from spawned programs to a file */
	logfile = gdm_make_filename (server->priv->log_dir,
				     server->priv->display_name,
				     ".log");
	g_debug ("Opening logfile for server %s", logfile);

	VE_IGNORE_EINTR (g_unlink (logfile));
	VE_IGNORE_EINTR (logfd = open (logfile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL, 0644));

	if (logfd != -1) {
		VE_IGNORE_EINTR (dup2 (logfd, 1));
		VE_IGNORE_EINTR (dup2 (logfd, 2));
		close (logfd);
	} else {
		g_warning (_("%s: Could not open logfile for display %s!"),
			   "gdm_server_spawn",
			   server->priv->display_name);
	}

	/* The X server expects USR1/TTIN/TTOU to be SIG_IGN */
	ign_signal.sa_handler = SIG_IGN;
	ign_signal.sa_flags = SA_RESTART;
	sigemptyset (&ign_signal.sa_mask);

	if (sigaction (SIGUSR1, &ign_signal, NULL) < 0) {
		g_warning (_("%s: Error setting %s to %s"),
			   "gdm_server_spawn", "USR1", "SIG_IGN");
		_exit (SERVER_ABORT);
	}

	if (sigaction (SIGTTIN, &ign_signal, NULL) < 0) {
		g_warning (_("%s: Error setting %s to %s"),
			   "gdm_server_spawn", "TTIN", "SIG_IGN");
		_exit (SERVER_ABORT);
	}

	if (sigaction (SIGTTOU, &ign_signal, NULL) < 0) {
		g_warning (_("%s: Error setting %s to %s"),
			   "gdm_server_spawn", "TTOU", "SIG_IGN");
		_exit (SERVER_ABORT);
	}

	/* And HUP and TERM are at SIG_DFL from gdm_unset_signals,
	   we also have an empty mask and all that fun stuff */

	/* unblock signals (especially HUP/TERM/USR1) so that we
	 * can control the X server */
	sigemptyset (&mask);
	sigprocmask (SIG_SETMASK, &mask, NULL);

	if (server->priv->priority != 0) {
		if (setpriority (PRIO_PROCESS, 0, server->priv->priority)) {
			g_warning (_("%s: Server priority couldn't be set to %d: %s"),
				   "gdm_server_spawn",
				   server->priv->priority,
				   g_strerror (errno));
		}
	}

	setpgid (0, 0);

	change_user (server);

#if sun
	{
		/* Remove old communication pipe, if present */
		char old_pipe[MAXPATHLEN];

		sprintf (old_pipe, "%s/%d", SDTLOGIN_DIR, server->priv->display_name);
		g_unlink (old_pipe);
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
get_server_environment (GdmServer *server)
{
	GPtrArray  *env;
	char      **l;
	GHashTable *hash;

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (l = environ; *l != NULL; l++) {
		char **str;
		str = g_strsplit (*l, "=", 2);
		g_hash_table_insert (hash, str[0], str[1]);
	}

	/* modify environment here */
	if (server->priv->is_parented) {
		if (server->priv->parent_auth_file != NULL) {
			g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (server->priv->parent_auth_file));
		}

		if (server->priv->parent_display_name != NULL) {
			g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (server->priv->parent_display_name));
		}
	} else {
		g_hash_table_insert (hash, g_strdup ("DISPLAY="), g_strdup (server->priv->display_name));
	}

	if (server->priv->uid != 0) {
		struct passwd *pwent;

		pwent = getpwuid (server->priv->uid);

		if (pwent->pw_dir != NULL
		    && g_file_test (pwent->pw_dir, G_FILE_TEST_EXISTS)) {
			g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (pwent->pw_dir));
		} else {
			/* Hack */
			g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
		}
		g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (pwent->pw_shell));
		g_hash_table_remove (hash, "MAIL");
	}

	g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
	g_hash_table_destroy (hash);

	g_ptr_array_add (env, NULL);

	return env;
}

static void
server_add_xserver_args (GdmServer *server,
			 int       *argc,
			 char    ***argv)
{
	int    count;
	char **args;
	int    len;
	int    i;

	len = *argc;
	g_shell_parse_argv (server->priv->session_args, &count, &args, NULL);
	*argv = g_renew (char *, *argv, len + count + 1);

	for (i=0; i < count;i++) {
		*argv[len++] = g_strdup (args[i]);
	}

	*argc += count;

	argv[len] = NULL;
	g_strfreev (args);
}

static gboolean
gdm_server_spawn (GdmServer  *server,
		  const char *vtarg)
{
	int              argc;
	gchar          **argv = NULL;
	GError          *error;
	GPtrArray       *env;
	gboolean         ret;

	ret = FALSE;

	/* Figure out the server command */
	argv = NULL;
	argc = 0;
	gdm_server_resolve_command_line (server,
					 vtarg,
					 &argc,
					 &argv);

	if (server->priv->session_args) {
		server_add_xserver_args (server, &argc, &argv);
	}

	if (argv[0] == NULL) {
		g_warning (_("%s: Empty server command for display %s"),
			   "gdm_server_spawn",
			   server->priv->display_name);
		_exit (SERVER_ABORT);
	}

	env = get_server_environment (server);

	g_debug ("Starting X server process");
	error = NULL;
	ret = g_spawn_async_with_pipes (NULL,
					argv,
					(char **)env->pdata,
					G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
					(GSpawnChildSetupFunc)server_child_setup,
					server,
					&server->priv->pid,
					NULL,
					NULL,
					NULL,
					&error);

	if (! ret) {
		g_warning ("Could not start command '%s': %s",
			   server->priv->command,
			   error->message);
		g_error_free (error);
	}

	g_strfreev (argv);
	g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

	g_debug ("Started X server process: %d", (int)server->priv->pid);

	sleep (10);

	return ret;
}

/**
 * gdm_server_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X server. Handles retries and fatal errors properly.
 */

gboolean
gdm_server_start (GdmServer *server)
{
	char *vtarg = NULL;
	int vtfd = -1;
	int vt = -1;
	gboolean res;

#if 0
	if (d->type == TYPE_XDMCP_PROXY &&
	    ! connect_to_parent (d))
		return FALSE;
#endif

#if 0
	if (d->type == TYPE_STATIC ||
	    d->type == TYPE_FLEXI) {
		vtarg = gdm_get_empty_vt_argument (&vtfd, &vt);
	}
#endif

	/* fork X server process */
	res = gdm_server_spawn (server, vtarg);

#if 0
	/* If we were holding a vt open for the server, close it now as it has
	 * already taken the bait. */
	if (vtfd > 0) {
		VE_IGNORE_EINTR (close (vtfd));
	}
#endif

	return res;
}

static void
_gdm_server_set_display_name (GdmServer  *server,
			      const char *name)
{
        g_free (server->priv->display_name);
        server->priv->display_name = g_strdup (name);
}

static void
gdm_server_set_property (GObject      *object,
			guint	       prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GdmServer *self;

	self = GDM_SERVER (object);

	switch (prop_id) {
	case PROP_DISPLAY_NAME:
		_gdm_server_set_display_name (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_server_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmServer *self;

	self = GDM_SERVER (object);

	switch (prop_id) {
	case PROP_DISPLAY_NAME:
		g_value_set_string (value, self->priv->display_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_server_constructor (GType                  type,
		       guint                  n_construct_properties,
		       GObjectConstructParam *construct_properties)
{
        GdmServer      *server;
        GdmServerClass *klass;

        klass = GDM_SERVER_CLASS (g_type_class_peek (GDM_TYPE_SERVER));

        server = GDM_SERVER (G_OBJECT_CLASS (gdm_server_parent_class)->constructor (type,
										    n_construct_properties,
										    construct_properties));
        return G_OBJECT (server);
}

static void
gdm_server_class_init (GdmServerClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_server_get_property;
	object_class->set_property = gdm_server_set_property;
        object_class->constructor = gdm_server_constructor;
	object_class->finalize = gdm_server_finalize;

	g_type_class_add_private (klass, sizeof (GdmServerPrivate));

	signals [READY] =
		g_signal_new ("ready",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmServerClass, ready),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_object_class_install_property (object_class,
					 PROP_DISPLAY_NAME,
					 g_param_spec_string ("display-name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gdm_server_init (GdmServer *server)
{

	server->priv = GDM_SERVER_GET_PRIVATE (server);

	server->priv->pid = -1;
	server->priv->command = g_strdup ("/usr/bin/Xorg");
	server->priv->log_dir = g_strdup (LOGDIR);

	setup_ready_signal (server);
}

static void
gdm_server_finalize (GObject *object)
{
	GdmServer *server;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SERVER (object));

	server = GDM_SERVER (object);

	g_return_if_fail (server->priv != NULL);

	G_OBJECT_CLASS (gdm_server_parent_class)->finalize (object);
}

GdmServer *
gdm_server_new (const char *display_name)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SERVER,
			       "display-name", display_name,
			       NULL);

	return GDM_SERVER (object);
}
