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

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"
#include "filecheck.h"

#include "gdm-greeter.h"
#include "gdm-socket-protocol.h"

extern char **environ;

#define GDM_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER, GdmGreeterPrivate))

struct GdmGreeterPrivate
{
	char    *command;
	GPid     pid;

	char    *username;
	uid_t    uid;
	gid_t    gid;
	int      pipe1[2];
	int      pipe2[2];

	int      greeter_fd_in;
	int      greeter_fd_out;

	char    *display_name;
	char    *display_auth_file;

	int      user_max_filesize;

	gboolean interrupted;
	gboolean always_restart_greeter;

	guint    child_watch_id;
};

enum {
	PROP_0,
	PROP_DISPLAY_NAME,
};

static void	gdm_greeter_class_init	(GdmGreeterClass *klass);
static void	gdm_greeter_init	(GdmGreeter      *greeter);
static void	gdm_greeter_finalize	(GObject         *object);

G_DEFINE_TYPE (GdmGreeter, gdm_greeter, G_TYPE_OBJECT)

static void
change_user (GdmGreeter *greeter)
{
	if (greeter->priv->uid != 0) {
		struct passwd *pwent;

		pwent = getpwuid (greeter->priv->uid);
		if (pwent == NULL) {
			g_warning (_("%s: Greeter was to be spawned by uid %d but "
				     "that user doesn't exist"),
				   "gdm_greeter_spawn",
				   (int)greeter->priv->uid);
			_exit (1);
		}

		if (setgid (greeter->priv->gid) < 0)  {
			g_warning (_("%s: Couldn't set groupid to %d"),
				   "gdm_greeter_spawn", (int)greeter->priv->gid);
			_exit (1);
		}

		if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
			g_warning (_("%s: initgroups () failed for %s"),
				   "gdm_greeter_spawn", pwent->pw_name);
			_exit (1);
		}

		if (setuid (greeter->priv->uid) < 0)  {
			g_warning (_("%s: Couldn't set userid to %d"),
				   "gdm_greeter_spawn", (int)greeter->priv->uid);
			_exit (1);
		}
	} else {
		gid_t groups[1] = { 0 };

		if (setgid (0) < 0)  {
			g_warning (_("%s: Couldn't set groupid to 0"),
				   "gdm_greeter_spawn");
			/* Don't error out, it's not fatal, if it fails we'll
			 * just still be */
		}
		/* this will get rid of any suplementary groups etc... */
		setgroups (1, groups);
	}
}

static void
greeter_child_setup (GdmGreeter *greeter)
{

	/* Plumbing */
	VE_IGNORE_EINTR (close (greeter->priv->pipe1[1]));
	VE_IGNORE_EINTR (close (greeter->priv->pipe2[0]));

	VE_IGNORE_EINTR (dup2 (greeter->priv->pipe1[0], STDIN_FILENO));
	VE_IGNORE_EINTR (dup2 (greeter->priv->pipe2[1], STDOUT_FILENO));

	gdm_close_all_descriptors (2 /* from */,
				   -1 /* except */,
				   -1 /* except2 */);

	gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

	change_user (greeter);
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
get_greeter_environment (GdmGreeter *greeter)
{
	GPtrArray     *env;
	char         **l;
	GHashTable    *hash;
	struct passwd *pwent;

	env = g_ptr_array_new ();

	/* create a hash table of current environment, then update keys has necessary */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (l = environ; *l != NULL; l++) {
		char **str;
		str = g_strsplit (*l, "=", 2);
		g_hash_table_insert (hash, str[0], str[1]);
	}


	g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (greeter->priv->display_auth_file));
	g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (greeter->priv->display_name));

#if 0
	/* hackish ain't it */
	set_xnest_parent_stuff ();
#endif

	g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (greeter->priv->username));
	g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (greeter->priv->username));
	g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (greeter->priv->username));

	g_hash_table_insert (hash, g_strdup ("GDM_GREETER_PROTOCOL_VERSION"), g_strdup (GDM_GREETER_PROTOCOL_VERSION));
	g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
	g_hash_table_remove (hash, "MAIL");

	g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
	g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

	pwent = getpwnam (greeter->priv->username);
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
gdm_slave_whack_temp_auth_file (GdmGreeter *greeter)
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
create_temp_auth_file (GdmGreeter *greeter)
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
whack_greeter_fds (GdmGreeter *greeter)
{
	if (greeter->priv->greeter_fd_out > 0)
		VE_IGNORE_EINTR (close (greeter->priv->greeter_fd_out));
	greeter->priv->greeter_fd_out = -1;
	if (greeter->priv->greeter_fd_in > 0)
		VE_IGNORE_EINTR (close (greeter->priv->greeter_fd_in));
	greeter->priv->greeter_fd_in = -1;
}

/* return true for "there was an interruption received",
   and interrupted will be TRUE if we are actually interrupted from doing what
   we want.  If FALSE is returned, just continue on as we would normally */
static gboolean
check_for_interruption (GdmGreeter *greeter,
			const char *msg)
{
	/* Hell yeah we were interrupted, the greeter died */
	if (msg == NULL) {
		greeter->priv->interrupted = TRUE;
		return TRUE;
	}

	if (msg[0] == BEL) {
		/* Different interruptions come here */
		/* Note that we don't want to actually do anything.  We want
		 * to just set some flag and go on and schedule it after we
		 * dump out of the login in the main login checking loop */
#if 0
		switch (msg[1]) {
		case GDM_INTERRUPT_TIMED_LOGIN:
			/* only allow timed login if display is local,
			 * it is allowed for this display (it's only allowed
			 * for the first local display) and if it's set up
			 * correctly */
			if ((d->attached || gdm_daemon_config_get_value_bool (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN))
                            && d->timed_login_ok &&
			    ! ve_string_empty (ParsedTimedLogin) &&
                            strcmp (ParsedTimedLogin, gdm_root_user ()) != 0 &&
			    gdm_daemon_config_get_value_int (GDM_KEY_TIMED_LOGIN_DELAY) > 0) {
				do_timed_login = TRUE;
			}
			break;
		case GDM_INTERRUPT_CONFIGURE:
			if (d->attached &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_CONFIG_AVAILABLE, d->name) &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_SYSTEM_MENU, d->name) &&
			    ! ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_CONFIGURATOR))) {
				do_configurator = TRUE;
			}
			break;
		case GDM_INTERRUPT_SUSPEND:
			if (d->attached &&
			    gdm_daemon_config_get_value_bool_per_display (GDM_KEY_SYSTEM_MENU, d->name) &&
			    ! ve_string_empty (gdm_daemon_config_get_value_string (GDM_KEY_SUSPEND))) {
			    	gchar *msg = g_strdup_printf ("%s %ld", 
							      GDM_SOP_SUSPEND_MACHINE,
							      (long)getpid ());

				gdm_slave_send (msg, FALSE /* wait_for_ack */);
				g_free (msg);
			}
			/* Not interrupted, continue reading input,
			 * just proxy this to the master server */
			return TRUE;
		case GDM_INTERRUPT_LOGIN_SOUND:
			if (d->attached &&
			    ! play_login_sound (gdm_daemon_config_get_value_string (GDM_KEY_SOUND_ON_LOGIN_FILE))) {
				gdm_error (_("Login sound requested on non-local display or the play software "
					     "cannot be run or the sound does not exist"));
			}
			return TRUE;
		case GDM_INTERRUPT_SELECT_USER:
			gdm_verify_select_user (&msg[2]);
			break;
		case GDM_INTERRUPT_CANCEL:
			do_cancel = TRUE;
			break;
		case GDM_INTERRUPT_CUSTOM_CMD:
			if (d->attached &&
			    ! ve_string_empty (&msg[2])) {
				gchar *message = g_strdup_printf ("%s %ld %s", 
								  GDM_SOP_CUSTOM_CMD,
								  (long)getpid (), &msg[2]);

				gdm_slave_send (message, TRUE);
				g_free (message);
			}
			return TRUE;
		case GDM_INTERRUPT_THEME:
			g_free (d->theme_name);
			d->theme_name = NULL;
			if ( ! ve_string_empty (&msg[2]))
				d->theme_name = g_strdup (&msg[2]);
			gdm_slave_send_string (GDM_SOP_CHOSEN_THEME, &msg[2]);
			return TRUE;
		case GDM_INTERRUPT_SELECT_LANG:
			if (msg + 2) {
				const char *locale;
				const char *gdm_system_locale;

				locale = (gchar*)(msg + 3);
				gdm_system_locale = setlocale (LC_CTYPE, NULL);

				greeter->priv->always_restart_greeter = (gboolean)(*(msg + 2));
				ve_clearenv ();
				if (!strcmp (locale, DEFAULT_LANGUAGE)) {
					locale = gdm_system_locale;
				}
				g_setenv ("GDM_LANG", locale, TRUE);
				g_setenv ("LANG", locale, TRUE);
				g_unsetenv ("LC_ALL");
				g_unsetenv ("LC_MESSAGES");
				setlocale (LC_ALL, "");
				setlocale (LC_MESSAGES, "");
				gdm_saveenv ();

				do_restart_greeter = TRUE;
			}
			break;
		default:
			break;
		}
#endif
		/* this was an interruption, if it wasn't
		 * handled then the user will just get an error as if he
		 * entered an invalid login or passward.  Seriously BEL
		 * cannot be part of a login/password really */
		greeter->priv->interrupted = TRUE;
		return TRUE;
	}
	return FALSE;
}

static char *
gdm_slave_greeter_ctl (GdmGreeter *greeter,
		       char        cmd,
		       const char *str)
{
	char *buf = NULL;
	int c;

	if ( ! ve_string_empty (str)) {
		gdm_fdprintf (greeter->priv->greeter_fd_out, "%c%c%s\n", STX, cmd, str);
	} else {
		gdm_fdprintf (greeter->priv->greeter_fd_out, "%c%c\n", STX, cmd);
	}

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
	/* let the other process (greeter) do its stuff */
	sched_yield ();
#endif

	do {
		g_free (buf);
		buf = NULL;
		/* Skip random junk that might have accumulated */
		do {
			c = gdm_fdgetc (greeter->priv->greeter_fd_in);
		} while (c != EOF && c != STX);

		if (c == EOF ||
		    (buf = gdm_fdgets (greeter->priv->greeter_fd_in)) == NULL) {
			greeter->priv->interrupted = TRUE;
			/* things don't seem well with the greeter, it probably died */
			return NULL;
		}
	} while (check_for_interruption (greeter, buf) && ! greeter->priv->interrupted);

	if ( ! ve_string_empty (buf)) {
		return buf;
	} else {
		g_free (buf);
		return NULL;
	}
}

static void
gdm_slave_greeter_ctl_no_ret (GdmGreeter *greeter,
			      char        cmd,
			      const char *str)
{
	g_free (gdm_slave_greeter_ctl (greeter, cmd, str));
}
/**
 * check_user_file
 * check_global_file
 * is_in_trusted_pic_dir
 * get_facefile_from_gnome2_dir_config 
 * path_is_local
 * gdm_daemon_config_get_facefile_from_home
 * gdm_daemon_config_get_facefile_from_global
 *
 * These functions are used for accessing the user's face image from their
 * home directory.
 */
static gboolean
check_user_file (const char *path,
                 guint       uid)
{
        char    *dir;
        char    *file;
        gboolean is_ok;

        if (path == NULL)
                return FALSE;

        if (g_access (path, R_OK) != 0)
                return FALSE;

        dir = g_path_get_dirname (path);
        file = g_path_get_basename (path);

        is_ok = gdm_file_check ("run_pictures",
                                uid,
                                dir,
                                file,
                                TRUE, TRUE,
				0,
				0);
        g_free (dir);
        g_free (file);

        return is_ok;
}

static gboolean
check_global_file (const char *path,
                   guint       uid)
{
        if (path == NULL)
                return FALSE;

        if (g_access (path, R_OK) != 0)
                return FALSE;

        return TRUE;
}

/* If path starts with a "trusted" directory, don't sanity check things */
/* This is really somewhat "outdated" as we now really want things in
 * the picture dir or in ~/.gnome2/photo */
static gboolean
is_in_trusted_pic_dir (const char *path)
{
        /* our own pixmap dir is trusted */
        if (strncmp (path, PIXMAPDIR, sizeof (PIXMAPDIR)) == 0)
                return TRUE;

        return FALSE;
}

static gchar *
get_facefile_from_gnome2_dir_config (const char *homedir,
                                     guint       uid)
{
	char *picfile = NULL;
	char *cfgdir;

	/* Sanity check on ~user/.gnome2/gdm */
	cfgdir = g_build_filename (homedir, ".gnome2", "gdm", NULL);
	if (G_LIKELY (check_user_file (cfgdir, uid))) {
		GKeyFile *cfg;
		char *cfgfile;

		cfgfile = g_build_filename (homedir, ".gnome2", "gdm", NULL);
		cfg = gdm_common_config_load (cfgfile, NULL);
		g_free (cfgfile);

		if (cfg != NULL) {
			gdm_common_config_get_string (cfg, "face/picture=", &picfile, NULL);
			g_key_file_free (cfg);
		}

		/* must exist and be absolute (note that this check
		 * catches empty strings)*/
		/* Note that these days we just set ~/.face */
		if G_UNLIKELY (picfile != NULL &&
			       (picfile[0] != '/' ||
				/* this catches readability by user */
				g_access (picfile, R_OK) != 0)) {
			g_free (picfile);
			picfile = NULL;
		}

		if (picfile != NULL) {
			char buf[PATH_MAX];
			if (realpath (picfile, buf) == NULL) {
				g_free (picfile);
				picfile = NULL;
			} else {
				g_free (picfile);
				picfile = g_strdup (buf);
			}
		}

		if G_UNLIKELY (picfile != NULL) {
			if (! is_in_trusted_pic_dir (picfile)) {
				/* if not in trusted dir, check it out */

				/* Note that strict permissions checking is done
				 * on this file.  Even if it may not even be owned by the
				 * user.  This setting should ONLY point to pics in trusted
				 * dirs. */
				if (! check_user_file (picfile, uid)) {
					g_free (picfile);
					picfile = NULL;
				}
			}
		}
	}
	g_free (cfgdir);

	return picfile;
}

static GHashTable *fstype_hash = NULL;
extern char *filesystem_type (char *path, char *relpath, struct stat *statp);

static gboolean
path_is_local (const char *path)
{
        gpointer local = NULL;

        if (path == NULL)
                return FALSE;

        if (fstype_hash == NULL)
                fstype_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        else
                local = g_hash_table_lookup (fstype_hash, path);

        if (local == NULL) {
                struct stat statbuf;

                if (g_stat (path, &statbuf) == 0) {
                        char *type = filesystem_type ((char *)path, (char *)path, &statbuf);
                        gboolean is_local = ((strcmp (ve_sure_string (type), "nfs") != 0) &&
                                             (strcmp (ve_sure_string (type), "afs") != 0) &&
                                             (strcmp (ve_sure_string (type), "autofs") != 0) &&
                                             (strcmp (ve_sure_string (type), "unknown") != 0) &&
                                             (strcmp (ve_sure_string (type), "ncpfs") != 0));
                        local = GINT_TO_POINTER (is_local ? 1 : -1);
                        g_hash_table_insert (fstype_hash, g_strdup (path), local);
                }
        }

        return GPOINTER_TO_INT (local) > 0;
}

static char *
gdm_daemon_config_get_facefile_from_home (const char *homedir,
					  guint       uid)
{
	char    *picfile = NULL;
	char    *path;
	gboolean is_local;

	/* special case: look at parent of home to detect autofs
	   this is so we don't try to trigger an automount */
	path = g_path_get_dirname (homedir);
	is_local = path_is_local (path);
	g_free (path);

	/* now check that home dir itself is local */
	if (is_local) {
		is_local = path_is_local (homedir);
	}

	/* Only look at local home directories so we don't try to
	   read from remote (e.g. NFS) volumes */
	if (! is_local)
		return NULL;

	picfile = g_build_filename (homedir, ".face", NULL);

	if (check_user_file (picfile, uid))
		return picfile;
	else {
		g_free (picfile);
		picfile = NULL;
	}

	picfile = g_build_filename (homedir, ".face.icon", NULL);

	if (check_user_file (picfile, uid))
		return picfile;
	else {
		g_free (picfile);
		picfile = NULL;
	}

	picfile = get_facefile_from_gnome2_dir_config (homedir, uid);
	if (check_user_file (picfile, uid))
		return picfile;
	else {
		g_free (picfile);
		picfile = NULL;
	}

	/* Nothing found yet, try the old locations */

	picfile = g_build_filename (homedir, ".gnome2", "photo", NULL);
	if (check_user_file (picfile, uid))
		return picfile;
	else {
		g_free (picfile);
		picfile = NULL;
	}

	picfile = g_build_filename (homedir, ".gnome", "photo", NULL);
	if (check_user_file (picfile, uid))
		return picfile;
	else {
		g_free (picfile);
		picfile = NULL;
	}

	return NULL;
}

static char *
gdm_daemon_config_get_facefile_from_global (const char *username,
					    guint       uid)
{
#if 0
	char       *picfile = NULL;
	const char *facedir;

	facedir = gdm_daemon_config_get_value_string (GDM_KEY_GLOBAL_FACE_DIR);

	/* Try the global face directory */

	picfile = g_build_filename (facedir, username, NULL);

	if (check_global_file (picfile, uid))
		return picfile;

	g_free (picfile);
	picfile = gdm_make_filename (facedir, username, ".png");

	if (check_global_file (picfile, uid))
		return picfile;

	g_free (picfile);
#endif
	return NULL;
}

static void
gdm_slave_quick_exit (GdmGreeter *greeter)
{
	/* FIXME */
	_exit (1);
}

/* This is VERY evil! */
static void
run_pictures (GdmGreeter *greeter)
{
	char *response;
	int max_write;
	char buf[1024];
	size_t bytes;
	struct passwd *pwent;
	char *picfile;
	FILE *fp;

	response = NULL;
	for (;;) {
		struct stat s;
		char *tmp, *ret;
		int i, r;

		g_free (response);
		response = gdm_slave_greeter_ctl (greeter, GDM_NEEDPIC, "");
		if (ve_string_empty (response)) {
			g_free (response);
			return;
		}

		pwent = getpwnam (response);
		if G_UNLIKELY (pwent == NULL) {
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "");
			continue;
		}

		picfile = NULL;

		NEVER_FAILS_seteuid (0);
		if G_UNLIKELY (setegid (pwent->pw_gid) != 0 ||
			       seteuid (pwent->pw_uid) != 0) {
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "");
			continue;
		}

		picfile = gdm_daemon_config_get_facefile_from_home (pwent->pw_dir, pwent->pw_uid);

		if (! picfile)
			picfile = gdm_daemon_config_get_facefile_from_global (pwent->pw_name, pwent->pw_uid);

		if (! picfile) {
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "");
			continue;
		}

		VE_IGNORE_EINTR (r = g_stat (picfile, &s));
		if G_UNLIKELY (r < 0 || s.st_size > greeter->priv->user_max_filesize) {
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "");
			continue;
		}

		VE_IGNORE_EINTR (fp = fopen (picfile, "r"));
		g_free (picfile);
		if G_UNLIKELY (fp == NULL) {
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "");
			continue;
		}

		tmp = g_strdup_printf ("buffer:%d", (int)s.st_size);
		ret = gdm_slave_greeter_ctl (greeter, GDM_READPIC, tmp);
		g_free (tmp);

		if G_UNLIKELY (ret == NULL || strcmp (ret, "OK") != 0) {
			VE_IGNORE_EINTR (fclose (fp));
			g_free (ret);
			continue;
		}
		g_free (ret);

		gdm_fdprintf (greeter->priv->greeter_fd_out, "%c", STX);

#ifdef PIPE_BUF
		max_write = MIN (PIPE_BUF, sizeof (buf));
#else
		/* apparently Hurd doesn't have PIPE_BUF */
		max_write = fpathconf (greeter->priv->greeter_fd_out, _PC_PIPE_BUF);
		/* could return -1 if no limit */
		if (max_write > 0)
			max_write = MIN (max_write, sizeof (buf));
		else
			max_write = sizeof (buf);
#endif

		i = 0;
		while (i < s.st_size) {
			int written;

			VE_IGNORE_EINTR (bytes = fread (buf, sizeof (char),
							max_write, fp));

			if (bytes <= 0)
				break;

			if G_UNLIKELY (i + bytes > s.st_size)
				bytes = s.st_size - i;

			/* write until we succeed in writing something */
			VE_IGNORE_EINTR (written = write (greeter->priv->greeter_fd_out, buf, bytes));
			if G_UNLIKELY (written < 0 &&
				       (errno == EPIPE || errno == EBADF)) {
				/* something very, very bad has happened */

				gdm_slave_quick_exit (greeter);
			}

			if G_UNLIKELY (written < 0)
				written = 0;

			/* write until we succeed in writing everything */
			while (written < bytes) {
				int n;
				VE_IGNORE_EINTR (n = write (greeter->priv->greeter_fd_out, &buf[written], bytes-written));
				if G_UNLIKELY (n < 0 &&
					       (errno == EPIPE || errno == EBADF)) {

					/* something very, very bad has happened */
					gdm_slave_quick_exit (greeter);

				} else if G_LIKELY (n > 0) {
					written += n;
				}
			}

			/* we have written bytes bytes if it likes it or not */
			i += bytes;
		}

		VE_IGNORE_EINTR (fclose (fp));

		/* eek, this "could" happen, so just send some garbage */
		while G_UNLIKELY (i < s.st_size) {
			bytes = MIN (sizeof (buf), s.st_size - i);
			errno = 0;
			bytes = write (greeter->priv->greeter_fd_out, buf, bytes);
			if G_UNLIKELY (bytes < 0 && (errno == EPIPE || errno == EBADF)) {

				/* something very, very bad has happened */
				gdm_slave_quick_exit (greeter);

			}
			if (bytes > 0)
				i += bytes;
		}

		gdm_slave_greeter_ctl_no_ret (greeter, GDM_READPIC, "done");

	}

	g_free (response); /* not reached */
}

static void
greeter_child_watch (GPid        pid,
		     int         status,
		     GdmGreeter *greeter)
{
	g_debug ("child (pid:%d) done (%s:%d)",
		 (int) pid,
		 WIFEXITED (status) ? "status"
		 : WIFSIGNALED (status) ? "signal"
		 : "unknown",
		 WIFEXITED (status) ? WEXITSTATUS (status)
		 : WIFSIGNALED (status) ? WTERMSIG (status)
		 : -1);

	g_spawn_close_pid (greeter->priv->pid);
	greeter->priv->pid = -1;
}

static gboolean
gdm_greeter_spawn (GdmGreeter *greeter)
{
	gchar          **argv;
	GError          *error;
	GPtrArray       *env;
	gboolean         ret;

	ret = FALSE;

	/* Open a pipe for greeter communications */
	if (pipe (greeter->priv->pipe1) < 0) {
		g_warning ("Can't init pipe to gdmgreeter");
		exit (1);
	}

	if (pipe (greeter->priv->pipe2) < 0) {
		VE_IGNORE_EINTR (close (greeter->priv->pipe1[0]));
		VE_IGNORE_EINTR (close (greeter->priv->pipe1[1]));
		g_warning ("Can't init pipe to gdmgreeter");
		exit (1);
	}

	create_temp_auth_file (greeter);

	g_debug ("Running greeter process: %s", greeter->priv->command);

	argv = NULL;
	if (! g_shell_parse_argv (greeter->priv->command, NULL, &argv, &error)) {
		g_warning ("Could not parse command: %s", error->message);
		g_error_free (error);
		goto out;
	}

	env = get_greeter_environment (greeter);

	error = NULL;
	ret = g_spawn_async_with_pipes (NULL,
					argv,
					(char **)env->pdata,
					G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD,
					(GSpawnChildSetupFunc)greeter_child_setup,
					greeter,
					&greeter->priv->pid,
					NULL,
					NULL,
					NULL,
					&error);

	g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);

	if (! ret) {
		g_warning ("Could not start command '%s': %s",
			   greeter->priv->command,
			   error->message);
		g_error_free (error);
	} else {

		whack_greeter_fds (greeter);

		greeter->priv->greeter_fd_out = greeter->priv->pipe1[1];
		greeter->priv->greeter_fd_in = greeter->priv->pipe2[0];

		g_debug ("gdm_slave_greeter: Greeter on pid %d", (int)greeter->priv->pid);
	}

	greeter->priv->child_watch_id = g_child_watch_add (greeter->priv->pid,
							   (GChildWatchFunc)greeter_child_watch,
							   greeter);

	g_strfreev (argv);
 out:

	return ret;
}

/**
 * gdm_greeter_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X greeter. Handles retries and fatal errors properly.
 */

gboolean
gdm_greeter_start (GdmGreeter *greeter)
{
	gboolean    res;
	const char *gdmlang;

	res = gdm_greeter_spawn (greeter);

	if (res) {
		run_pictures (greeter); /* Append pictures to greeter if browsing is on */

		if (greeter->priv->always_restart_greeter)
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_ALWAYS_RESTART, "Y");
		else
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_ALWAYS_RESTART, "N");

		gdmlang = g_getenv ("GDM_LANG");
		if (gdmlang)
			gdm_slave_greeter_ctl_no_ret (greeter, GDM_SETLANG, gdmlang);
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
greeter_died (GdmGreeter *greeter)
{
	int exit_status;

	g_debug ("Waiting on process %d", greeter->priv->pid);
	exit_status = wait_on_child (greeter->priv->pid);

	if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
		g_debug ("Wait on child process failed");
	} else {
		/* exited normally */
	}

	g_spawn_close_pid (greeter->priv->pid);
	greeter->priv->pid = -1;

	g_debug ("Greeter died");
}

gboolean
gdm_greeter_stop (GdmGreeter *greeter)
{

	if (greeter->priv->pid <= 1) {
		return TRUE;
	}

	/* remove watch source before we can wait on child */
	if (greeter->priv->child_watch_id > 0) {
		g_source_remove (greeter->priv->child_watch_id);
		greeter->priv->child_watch_id = 0;
	}

	g_debug ("Stopping greeter");

	signal_pid (greeter->priv->pid, SIGTERM);
	greeter_died (greeter);

	return TRUE;
}


static void
_gdm_greeter_set_display_name (GdmGreeter  *greeter,
			      const char *name)
{
        g_free (greeter->priv->display_name);
        greeter->priv->display_name = g_strdup (name);
}

static void
gdm_greeter_set_property (GObject      *object,
			guint	       prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GdmGreeter *self;

	self = GDM_GREETER (object);

	switch (prop_id) {
	case PROP_DISPLAY_NAME:
		_gdm_greeter_set_display_name (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_greeter_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmGreeter *self;

	self = GDM_GREETER (object);

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
gdm_greeter_constructor (GType                  type,
		       guint                  n_construct_properties,
		       GObjectConstructParam *construct_properties)
{
        GdmGreeter      *greeter;
        GdmGreeterClass *klass;
	struct passwd   *pwent;

        klass = GDM_GREETER_CLASS (g_type_class_peek (GDM_TYPE_GREETER));

        greeter = GDM_GREETER (G_OBJECT_CLASS (gdm_greeter_parent_class)->constructor (type,
										       n_construct_properties,
										       construct_properties));

	pwent = getpwuid (greeter->priv->uid);
	if (pwent != NULL) {
		greeter->priv->username = g_strdup (pwent->pw_name);
	}

        return G_OBJECT (greeter);
}

static void
gdm_greeter_class_init (GdmGreeterClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_greeter_get_property;
	object_class->set_property = gdm_greeter_set_property;
        object_class->constructor = gdm_greeter_constructor;
	object_class->finalize = gdm_greeter_finalize;

	g_type_class_add_private (klass, sizeof (GdmGreeterPrivate));

	g_object_class_install_property (object_class,
					 PROP_DISPLAY_NAME,
					 g_param_spec_string ("display-name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gdm_greeter_init (GdmGreeter *greeter)
{

	greeter->priv = GDM_GREETER_GET_PRIVATE (greeter);

	greeter->priv->pid = -1;

	greeter->priv->command = g_strdup (LIBEXECDIR "/gdmgreeter");
	greeter->priv->user_max_filesize = 65536;
}

static void
gdm_greeter_finalize (GObject *object)
{
	GdmGreeter *greeter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_GREETER (object));

	greeter = GDM_GREETER (object);

	g_return_if_fail (greeter->priv != NULL);

	gdm_greeter_stop (greeter);

	G_OBJECT_CLASS (gdm_greeter_parent_class)->finalize (object);
}

GdmGreeter *
gdm_greeter_new (const char *display_name)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_GREETER,
			       "display-name", display_name,
			       NULL);

	return GDM_GREETER (object);
}
