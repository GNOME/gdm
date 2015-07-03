/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef WITH_PLYMOUTH
#include <linux/vt.h>
#endif

#include <systemd/sd-daemon.h>

#ifdef ENABLE_SYSTEMD_JOURNAL
#include <systemd/sd-journal.h>
#endif

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#include "gdm-server.h"

extern char **environ;

#define GDM_SERVER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SERVER, GdmServerPrivate))

#define MAX_LOGS 5

struct GdmServerPrivate
{
        char    *command;
        GPid     pid;

        gboolean disable_tcp;
        int      priority;
        char    *user_name;
        char    *session_args;

        char    *log_dir;
        char    *display_name;
        char    *display_device;
        char    *display_seat_id;
        char    *auth_file;

        guint    child_watch_id;

        gboolean is_initial;
};

enum {
        PROP_0,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_DEVICE,
        PROP_AUTH_FILE,
        PROP_USER_NAME,
        PROP_DISABLE_TCP,
        PROP_IS_INITIAL,
};

enum {
        READY,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_server_class_init   (GdmServerClass *klass);
static void     gdm_server_init         (GdmServer      *server);
static void     gdm_server_finalize     (GObject        *object);

G_DEFINE_TYPE (GdmServer, gdm_server, G_TYPE_OBJECT)

char *
gdm_server_get_display_device (GdmServer *server)
{
        /* systemd finds the display device out on its own based on the display */
        return NULL;
}

static void
gdm_server_ready (GdmServer *server)
{
        g_debug ("GdmServer: Got USR1 from X server - emitting READY");

        gdm_run_script (GDMCONFDIR "/Init", GDM_USERNAME,
                        server->priv->display_name,
                        NULL, /* hostname */
                        server->priv->auth_file);

        g_signal_emit (server, signals[READY], 0);
}

static GSList *active_servers;
static gboolean sigusr1_thread_running;
static GCond sigusr1_thread_cond;
static GMutex sigusr1_thread_mutex;

static gboolean
got_sigusr1 (gpointer user_data)
{
        GPid pid = GPOINTER_TO_UINT (user_data);
        GSList *l;

        g_debug ("GdmServer: got SIGUSR1 from PID %d", pid);

        for (l = active_servers; l; l = l->next) {
                GdmServer *server = l->data;

                if (server->priv->pid == pid)
                        gdm_server_ready (server);
        }

        return G_SOURCE_REMOVE;
}

static gpointer
sigusr1_thread_main (gpointer user_data)
{
        sigset_t sigusr1_mask;

        /* Handle only SIGUSR1 */
        sigemptyset (&sigusr1_mask);
        sigaddset (&sigusr1_mask, SIGUSR1);
        sigprocmask (SIG_SETMASK, &sigusr1_mask, NULL);

        g_mutex_lock (&sigusr1_thread_mutex);
        sigusr1_thread_running = TRUE;
        g_cond_signal (&sigusr1_thread_cond);
        g_mutex_unlock (&sigusr1_thread_mutex);

        /* Spin waiting for a SIGUSR1 */
        while (TRUE) {
                siginfo_t info;

                if (sigwaitinfo (&sigusr1_mask, &info) == -1)
                        continue;

                g_idle_add (got_sigusr1, GUINT_TO_POINTER (info.si_pid));
        }

        return NULL;
}

static void
gdm_server_launch_sigusr1_thread_if_needed (void)
{
        static GThread *sigusr1_thread;

        if (sigusr1_thread == NULL) {
                sigusr1_thread = g_thread_new ("gdm SIGUSR1 catcher", sigusr1_thread_main, NULL);

                g_mutex_lock (&sigusr1_thread_mutex);
                while (!sigusr1_thread_running)
                        g_cond_wait (&sigusr1_thread_cond, &sigusr1_thread_mutex);
                g_mutex_unlock (&sigusr1_thread_mutex);
        }
}

static void
gdm_server_init_command (GdmServer *server)
{
        gboolean debug = FALSE;
        const char *debug_options;
        const char *verbosity = "";

        if (server->priv->command != NULL) {
                return;
        }

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        if (debug) {
                debug_options = " -logverbose 7 -core ";
        } else {
                debug_options = "";
        }

#define X_SERVER_ARG_FORMAT " -background none -noreset -audit 4 -verbose %s%s"

        /* This is a temporary hack to work around the fact that XOrg
         * currently lacks support for multi-seat hotplugging for
         * display devices. This bit should be removed as soon as XOrg
         * gains native support for automatically enumerating usb
         * based graphics adapters at start-up via udev. */

        /* systemd ships an X server wrapper tool which simply invokes
         * the usual X but ensures it only uses the display devices of
         * the seat. */

        /* We do not rely on this wrapper server if, a) the machine
         * wasn't booted using systemd, or b) the wrapper tool is
         * missing, or c) we are running for the main seat 'seat0'. */

#ifdef ENABLE_SYSTEMD_JOURNAL
        /* For systemd, we don't have a log file but instead log to stdout,
           so set it to the xserver's built-in default verbosity */
        if (debug)
            verbosity = "7 -logfile /dev/null";
        else
            verbosity = "3 -logfile /dev/null";
#endif

        if (g_access (SYSTEMD_X_SERVER, X_OK) < 0) {
                goto fallback;
        }

        if (server->priv->display_seat_id == NULL ||
            strcmp (server->priv->display_seat_id, "seat0") == 0) {
                goto fallback;
        }

        server->priv->command = g_strdup_printf (SYSTEMD_X_SERVER X_SERVER_ARG_FORMAT, verbosity, debug_options);
        return;

fallback:
        server->priv->command = g_strdup_printf (X_SERVER X_SERVER_ARG_FORMAT, verbosity, debug_options);

}

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

        gdm_server_init_command (server);

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

        argv = g_renew (char *, argv, len + 12);
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

        if (server->priv->display_seat_id != NULL) {
                argv[len++] = g_strdup ("-seat");
                argv[len++] = g_strdup (server->priv->display_seat_id);
        }

        /* If we were compiled with Xserver >= 1.17 we need to specify
         * '-listen tcp' as the X server dosen't listen on tcp sockets
         * by default anymore. In older versions we need to pass
         * -nolisten tcp to disable listening on tcp sockets.
         */
#ifdef HAVE_XSERVER_THAT_DEFAULTS_TO_LOCAL_ONLY
        if (!server->priv->disable_tcp && ! query_in_arglist) {
                argv[len++] = g_strdup ("-listen");
                argv[len++] = g_strdup ("tcp");
        }
#else
        if (server->priv->disable_tcp && ! query_in_arglist) {
                argv[len++] = g_strdup ("-nolisten");
                argv[len++] = g_strdup ("tcp");
        }

#endif

        if (vtarg != NULL && ! gotvtarg) {
                argv[len++] = g_strdup (vtarg);
        }

        argv[len++] = NULL;

        *argvp = argv;
        *argcp = len;

        return TRUE;
}

static void
rotate_logs (const char *path,
             guint       n_copies)
{
        int i;

        for (i = n_copies - 1; i > 0; i--) {
                char *name_n;
                char *name_n1;

                name_n = g_strdup_printf ("%s.%d", path, i);
                if (i > 1) {
                        name_n1 = g_strdup_printf ("%s.%d", path, i - 1);
                } else {
                        name_n1 = g_strdup (path);
                }

                VE_IGNORE_EINTR (g_unlink (name_n));
                VE_IGNORE_EINTR (g_rename (name_n1, name_n));

                g_free (name_n1);
                g_free (name_n);
        }

        VE_IGNORE_EINTR (g_unlink (path));
}

static void
change_user (GdmServer *server)
{
        struct passwd *pwent;

        if (server->priv->user_name == NULL) {
                return;
        }

        gdm_get_pwent_for_name (server->priv->user_name, &pwent);
        if (pwent == NULL) {
                g_warning (_("Server was to be spawned by user %s but that user doesn’t exist"),
                           server->priv->user_name);
                _exit (EXIT_FAILURE);
        }

        g_debug ("GdmServer: Changing (uid:gid) for child process to (%d:%d)",
                 pwent->pw_uid,
                 pwent->pw_gid);

        if (pwent->pw_uid != 0) {
                if (setgid (pwent->pw_gid) < 0)  {
                        g_warning (_("Couldn’t set groupid to %d"),
                                   pwent->pw_gid);
                        _exit (EXIT_FAILURE);
                }

                if (initgroups (pwent->pw_name, pwent->pw_gid) < 0) {
                        g_warning (_("initgroups () failed for %s"),
                                   pwent->pw_name);
                        _exit (EXIT_FAILURE);
                }

                if (setuid (pwent->pw_uid) < 0)  {
                        g_warning (_("Couldn’t set userid to %d"),
                                   (int)pwent->pw_uid);
                        _exit (EXIT_FAILURE);
                }
        } else {
                gid_t groups[1] = { 0 };

                if (setgid (0) < 0)  {
                        g_warning (_("Couldn’t set groupid to %d"), 0);
                        /* Don't error out, it's not fatal, if it fails we'll
                         * just still be */
                }

                /* this will get rid of any suplementary groups etc... */
                setgroups (1, groups);
        }
}

static gboolean
gdm_server_setup_journal_fds (GdmServer *server)
{
#ifdef ENABLE_SYSTEMD_JOURNAL
    if (sd_booted () > 0) {
        int out, err;
        const char *prefix = "gdm-Xorg-";
        char *identifier;
        gsize size;

        size = strlen (prefix) + strlen (server->priv->display_name) + 1;
        identifier = g_alloca (size);
        strcpy (identifier, prefix);
        strcat (identifier, server->priv->display_name);
        identifier[size - 1] = '\0';

        out = sd_journal_stream_fd (identifier, LOG_INFO, FALSE);
        if (out < 0)
            return FALSE;

        err = sd_journal_stream_fd (identifier, LOG_WARNING, FALSE);
        if (err < 0) {
            close (out);
            return FALSE;
        }

        VE_IGNORE_EINTR (dup2 (out, 1));
        VE_IGNORE_EINTR (dup2 (err, 2));
        return TRUE;
    }
#endif
    return FALSE;
}

static void
gdm_server_setup_logfile (GdmServer *server)
{
        int              logfd;
        char            *log_file;
        char            *log_path;

        log_file = g_strdup_printf ("%s.log", server->priv->display_name);
        log_path = g_build_filename (server->priv->log_dir, log_file, NULL);
        g_free (log_file);

        /* Rotate the X server logs */
        rotate_logs (log_path, MAX_LOGS);

        g_debug ("GdmServer: Opening logfile for server %s", log_path);

        VE_IGNORE_EINTR (g_unlink (log_path));
        VE_IGNORE_EINTR (logfd = open (log_path, O_CREAT|O_APPEND|O_TRUNC|O_WRONLY|O_EXCL, 0644));

        g_free (log_path);

        if (logfd != -1) {
                VE_IGNORE_EINTR (dup2 (logfd, 1));
                VE_IGNORE_EINTR (dup2 (logfd, 2));
                close (logfd);
        } else {
                g_warning (_("%s: Could not open log file for display %s!"),
                           "gdm_server_spawn",
                           server->priv->display_name);
        }
}

static void
server_child_setup (GdmServer *server)
{
        struct sigaction ign_signal;
        sigset_t         mask;

        if (!gdm_server_setup_journal_fds(server))
            gdm_server_setup_logfile(server);

        /* The X server expects USR1/TTIN/TTOU to be SIG_IGN */
        ign_signal.sa_handler = SIG_IGN;
        ign_signal.sa_flags = SA_RESTART;
        sigemptyset (&ign_signal.sa_mask);

        if (sigaction (SIGUSR1, &ign_signal, NULL) < 0) {
                g_warning (_("%s: Error setting %s to %s"),
                           "gdm_server_spawn", "USR1", "SIG_IGN");
                _exit (EXIT_FAILURE);
        }

        if (sigaction (SIGTTIN, &ign_signal, NULL) < 0) {
                g_warning (_("%s: Error setting %s to %s"),
                           "gdm_server_spawn", "TTIN", "SIG_IGN");
                _exit (EXIT_FAILURE);
        }

        if (sigaction (SIGTTOU, &ign_signal, NULL) < 0) {
                g_warning (_("%s: Error setting %s to %s"),
                           "gdm_server_spawn", "TTOU", "SIG_IGN");
                _exit (EXIT_FAILURE);
        }

        /* And HUP and TERM are at SIG_DFL from gdm_unset_signals,
           we also have an empty mask and all that fun stuff */

        /* unblock signals (especially HUP/TERM/USR1) so that we
         * can control the X server */
        sigemptyset (&mask);
        sigprocmask (SIG_SETMASK, &mask, NULL);

        /* Terminate the process when the parent dies */
#ifdef HAVE_SYS_PRCTL_H
        prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif

        if (server->priv->priority != 0) {
                if (setpriority (PRIO_PROCESS, 0, server->priv->priority)) {
                        g_warning (_("%s: Server priority couldn’t be set to %d: %s"),
                                   "gdm_server_spawn",
                                   server->priv->priority,
                                   g_strerror (errno));
                }
        }

        setpgid (0, 0);

        change_user (server);
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
                g_free (str);
        }

        /* modify environment here */
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (server->priv->display_name));

        if (server->priv->user_name != NULL) {
                struct passwd *pwent;

                gdm_get_pwent_for_name (server->priv->user_name, &pwent);

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

static void
server_child_watch (GPid       pid,
                    int        status,
                    GdmServer *server)
{
        g_debug ("GdmServer: child (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_object_ref (server);

        if (WIFEXITED (status)) {
                int code = WEXITSTATUS (status);
                g_signal_emit (server, signals [EXITED], 0, code);
        } else if (WIFSIGNALED (status)) {
                int num = WTERMSIG (status);
                g_signal_emit (server, signals [DIED], 0, num);
        }

        g_spawn_close_pid (server->priv->pid);
        server->priv->pid = -1;

        g_object_unref (server);
}

static void
prune_active_servers_list (GdmServer *server)
{
        active_servers = g_slist_remove (active_servers, server);
}

static gboolean
gdm_server_spawn (GdmServer    *server,
                  const char   *vtarg,
                  GError      **error)
{
        int              argc;
        gchar          **argv = NULL;
        GPtrArray       *env = NULL;
        gboolean         ret = FALSE;
        char            *freeme;

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
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             _("%s: Empty server command for display %s"),
                             "gdm_server_spawn",
                             server->priv->display_name);
                goto out;
        }

        env = get_server_environment (server);

        freeme = g_strjoinv (" ", argv);
        g_debug ("GdmServer: Starting X server process: %s", freeme);
        g_free (freeme);

        active_servers = g_slist_append (active_servers, server);

        g_object_weak_ref (G_OBJECT (server),
                           (GWeakNotify)
                           prune_active_servers_list,
                           server);

        gdm_server_launch_sigusr1_thread_if_needed ();

        if (!g_spawn_async_with_pipes (NULL,
                                       argv,
                                       (char **)env->pdata,
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                       (GSpawnChildSetupFunc)server_child_setup,
                                       server,
                                       &server->priv->pid,
                                       NULL,
                                       NULL,
                                       NULL,
                                       error))
                goto out;

        g_debug ("GdmServer: Started X server process %d - waiting for READY", (int)server->priv->pid);

        server->priv->child_watch_id = g_child_watch_add (server->priv->pid,
                                                          (GChildWatchFunc)server_child_watch,
                                                          server);

        ret = TRUE;
 out:
        g_strfreev (argv);
        if (env) {
                g_ptr_array_foreach (env, (GFunc)g_free, NULL);
                g_ptr_array_free (env, TRUE);
        }
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
        gboolean res = FALSE;
        const char *vtarg = NULL;
        GError *local_error = NULL;
        GError **error = &local_error;

        /* Hardcode the VT for the initial X server, but nothing else */
        if (server->priv->is_initial) {
                vtarg = "vt" GDM_INITIAL_VT;
        }

        /* fork X server process */
        if (!gdm_server_spawn (server, vtarg, error)) {
                goto out;
        }

        res = TRUE;
 out:
        if (local_error) {
                g_printerr ("%s\n", local_error->message);
                g_clear_error (&local_error);
        }
        return res;
}

static void
server_died (GdmServer *server)
{
        int exit_status;

        g_debug ("GdmServer: Waiting on process %d", server->priv->pid);
        exit_status = gdm_wait_on_pid (server->priv->pid);

        if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                g_debug ("GdmServer: Wait on child process failed");
        } else {
                /* exited normally */
        }

        g_spawn_close_pid (server->priv->pid);
        server->priv->pid = -1;

        if (server->priv->display_device != NULL) {
                g_free (server->priv->display_device);
                server->priv->display_device = NULL;
                g_object_notify (G_OBJECT (server), "display-device");
        }

        g_debug ("GdmServer: Server died");
}

gboolean
gdm_server_stop (GdmServer *server)
{
        int res;

        if (server->priv->pid <= 1) {
                return TRUE;
        }

        /* remove watch source before we can wait on child */
        if (server->priv->child_watch_id > 0) {
                g_source_remove (server->priv->child_watch_id);
                server->priv->child_watch_id = 0;
        }

        g_debug ("GdmServer: Stopping server");

        res = gdm_signal_pid (server->priv->pid, SIGTERM);
        if (res < 0) {
        } else {
                server_died (server);
        }

        return TRUE;
}


static void
_gdm_server_set_display_name (GdmServer  *server,
                              const char *name)
{
        g_free (server->priv->display_name);
        server->priv->display_name = g_strdup (name);
}

static void
_gdm_server_set_display_seat_id (GdmServer  *server,
                                 const char *name)
{
        g_free (server->priv->display_seat_id);
        server->priv->display_seat_id = g_strdup (name);
}

static void
_gdm_server_set_auth_file (GdmServer  *server,
                           const char *auth_file)
{
        g_free (server->priv->auth_file);
        server->priv->auth_file = g_strdup (auth_file);
}

static void
_gdm_server_set_user_name (GdmServer  *server,
                           const char *name)
{
        g_free (server->priv->user_name);
        server->priv->user_name = g_strdup (name);
}

static void
_gdm_server_set_disable_tcp (GdmServer  *server,
                             gboolean    disabled)
{
        server->priv->disable_tcp = disabled;
}

static void
_gdm_server_set_is_initial (GdmServer  *server,
                            gboolean    initial)
{
        server->priv->is_initial = initial;
}

static void
gdm_server_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
        GdmServer *self;

        self = GDM_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_NAME:
                _gdm_server_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_SEAT_ID:
                _gdm_server_set_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_AUTH_FILE:
                _gdm_server_set_auth_file (self, g_value_get_string (value));
                break;
        case PROP_USER_NAME:
                _gdm_server_set_user_name (self, g_value_get_string (value));
                break;
        case PROP_DISABLE_TCP:
                _gdm_server_set_disable_tcp (self, g_value_get_boolean (value));
                break;
        case PROP_IS_INITIAL:
                _gdm_server_set_is_initial (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_server_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        GdmServer *self;

        self = GDM_SERVER (object);

        switch (prop_id) {
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->priv->display_name);
                break;
        case PROP_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->priv->display_seat_id);
                break;
        case PROP_DISPLAY_DEVICE:
                g_value_take_string (value,
                                     gdm_server_get_display_device (self));
                break;
        case PROP_AUTH_FILE:
                g_value_set_string (value, self->priv->auth_file);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, self->priv->user_name);
                break;
        case PROP_DISABLE_TCP:
                g_value_set_boolean (value, self->priv->disable_tcp);
                break;
        case PROP_IS_INITIAL:
                g_value_set_boolean (value, self->priv->is_initial);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_server_class_init (GdmServerClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_server_get_property;
        object_class->set_property = gdm_server_set_property;
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
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmServerClass, exited),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        signals [DIED] =
                g_signal_new ("died",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmServerClass, died),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "name",
                                                              "name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "Seat ID",
                                                              "ID of the seat this display is running on",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_DEVICE,
                                         g_param_spec_string ("display-device",
                                                              "Display Device",
                                                              "Path to terminal display is running on",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_AUTH_FILE,
                                         g_param_spec_string ("auth-file",
                                                              "Authorization File",
                                                              "Path to X authorization file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "user name",
                                                              "user name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_DISABLE_TCP,
                                         g_param_spec_boolean ("disable-tcp",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_IS_INITIAL,
                                         g_param_spec_boolean ("is-initial",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gdm_server_init (GdmServer *server)
{
        server->priv = GDM_SERVER_GET_PRIVATE (server);

        server->priv->pid = -1;

        server->priv->log_dir = g_strdup (LOGDIR);
}

static void
gdm_server_finalize (GObject *object)
{
        GdmServer *server;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SERVER (object));

        server = GDM_SERVER (object);

        g_return_if_fail (server->priv != NULL);

        gdm_server_stop (server);

        g_free (server->priv->command);
        g_free (server->priv->user_name);
        g_free (server->priv->session_args);
        g_free (server->priv->log_dir);
        g_free (server->priv->display_name);
        g_free (server->priv->display_seat_id);
        g_free (server->priv->display_device);
        g_free (server->priv->auth_file);

        G_OBJECT_CLASS (gdm_server_parent_class)->finalize (object);
}

GdmServer *
gdm_server_new (const char *display_name,
                const char *seat_id,
                const char *auth_file,
                gboolean    initial)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SERVER,
                               "display-name", display_name,
                               "display-seat-id", seat_id,
                               "auth-file", auth_file,
                               "is-initial", initial,
                               NULL);

        return GDM_SERVER (object);
}
