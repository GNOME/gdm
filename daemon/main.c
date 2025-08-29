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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <locale.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-manager.h"
#include "gdm-log.h"
#include "gdm-common.h"
#include "gdm-file-utils.h"

#include "gdm-settings.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_DBUS_NAME "org.gnome.DisplayManager"

static GDBusConnection *get_system_bus (void);
static gboolean         bus_reconnect (void);

extern char **environ;

static GdmManager      *manager       = NULL;
static int              name_id       = -1;
static GdmSettings     *settings      = NULL;
static uid_t            gdm_uid       = -1;
static gid_t            gdm_gid       = -1;

static gboolean
timed_exit_cb (GMainLoop *loop)
{
        g_main_loop_quit (loop);
        return FALSE;
}

static void
bus_connection_closed (void)
{
        g_debug ("Disconnected from D-Bus");

        if (manager == NULL) {
                /* probably shutting down or something */
                return;
        }

        g_clear_object (&manager);

        g_timeout_add_seconds (3, (GSourceFunc)bus_reconnect, NULL);
}

static GDBusConnection *
get_system_bus (void)
{
        GError          *error;
        GDBusConnection *bus;

        error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        g_signal_connect (bus, "closed",
                          G_CALLBACK (bus_connection_closed), NULL);
        g_dbus_connection_set_exit_on_close (bus, FALSE);

 out:
        return bus;
}

static void
delete_pid (void)
{
        g_unlink (GDM_PID_FILE);
}

static void
write_pid (void)
{
        int     pf;
        ssize_t written;
        char    pid[9];

        errno = 0;
        pf = open (GDM_PID_FILE, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644);
        if (pf < 0) {
                g_warning (_("Cannot write PID file %s: possibly out of disk space: %s"),
                           GDM_PID_FILE,
                           g_strerror (errno));

                return;
        }

        snprintf (pid, sizeof (pid), "%lu\n", (long unsigned) getpid ());
        errno = 0;
        written = write (pf, pid, strlen (pid));
        close (pf);

        if (written < 0) {
                g_warning (_("Cannot write PID file %s: possibly out of disk space: %s"),
                           GDM_PID_FILE,
                           g_strerror (errno));
                return;
        }

        atexit (delete_pid);
}

static void
gdm_daemon_ensure_dirs (uid_t uid,
                        gid_t gid)
{
        g_autoptr (GError) error = NULL;

        /* Set up /run/gdm */
        if (!gdm_ensure_dir (GDM_RUN_DIR, 0, 0, 0755, FALSE, &error)) {
                g_warning ("Failed to create " GDM_RUN_DIR ": %s", error->message);
                g_clear_error (&error);
        }

        /* Set up /var/log/gdm */
        if (!gdm_ensure_dir (LOGDIR, 0, gid, 0711, FALSE, &error)) {
                g_warning ("Failed to create " LOGDIR ": %s", error->message);
                g_clear_error (&error);
        }
}

static void
gdm_daemon_lookup_user (uid_t *uidp,
                        gid_t *gidp)
{
        char          *username;
        char          *groupname;
        uid_t          uid;
        gid_t          gid;
        struct passwd *pwent;
        struct group  *grent;

        username = NULL;
        groupname = NULL;
        uid = 0;
        gid = 0;

        gdm_settings_direct_get_string (GDM_KEY_USER, &username);
        gdm_settings_direct_get_string (GDM_KEY_GROUP, &groupname);

        if (username == NULL || groupname == NULL) {
                return;
        }

        g_debug ("Changing user:group to %s:%s", username, groupname);

        /* Lookup user and groupid for the GDM user */
        gdm_get_pwent_for_name (username, &pwent);

        /* Set uid and gid */
        if G_UNLIKELY (pwent == NULL) {
                g_critical (_("Can’t find the GDM user “%s”. Aborting!"), username);
        } else {
                uid = pwent->pw_uid;
        }

        if G_UNLIKELY (uid == 0) {
                g_critical (_("The GDM user should not be root. Aborting!"));
        }

        grent = getgrnam (groupname);

        if G_UNLIKELY (grent == NULL) {
                g_critical (_("Can’t find the GDM group “%s”. Aborting!"), groupname);
        } else  {
                gid = grent->gr_gid;
        }

        if G_UNLIKELY (gid == 0) {
                g_critical (_("The GDM group should not be root. Aborting!"));
        }

        if (uidp != NULL) {
                *uidp = uid;
        }

        if (gidp != NULL) {
                *gidp = gid;
        }

        g_free (username);
        g_free (groupname);
}

static gboolean
on_shutdown_signal_cb (gpointer user_data)
{
        GMainLoop *mainloop = user_data;

        g_main_loop_quit (mainloop);

        return FALSE;
}

static gboolean
on_sighup_cb (gpointer user_data)
{
        g_debug ("Got HUP signal");

        gdm_settings_reload (settings);

        return TRUE;
}

static gboolean
is_debug_set (void)
{
        gboolean debug;
        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        return debug;
}

/* SIGUSR1 is used by the X server to tell us that we're ready, so
 * block it. We'll unblock it in the worker thread in gdm-server.c
 */
static void
block_sigusr1 (void)
{
        sigset_t mask;

        sigemptyset (&mask);
        sigaddset (&mask, SIGUSR1);
        sigprocmask (SIG_BLOCK, &mask, NULL);
}

int
main (int    argc,
      char **argv)
{
        GMainLoop          *main_loop;
        GOptionContext     *context;
        GError             *error = NULL;
        gboolean            res;
        static gboolean     do_timed_exit    = FALSE;
        static gboolean     print_version    = FALSE;
        static gboolean     fatal_warnings   = FALSE;
        static GOptionEntry entries []   = {
                { "fatal-warnings", 0, 0, G_OPTION_ARG_NONE, &fatal_warnings, N_("Make all warnings fatal"), NULL },
                { "timed-exit", 0, 0, G_OPTION_ARG_NONE, &do_timed_exit, N_("Exit after a time (for debugging)"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &print_version, N_("Print GDM version"), NULL },

                { NULL }
        };

        block_sigusr1 ();

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        context = g_option_context_new (_("GNOME Display Manager"));
        g_option_context_add_main_entries (context, entries, NULL);

        error = NULL;
        res = g_option_context_parse (context, &argc, &argv, &error);
        g_option_context_free (context);
        if (! res) {
                g_printerr ("Failed to parse options: %s\n", error->message);
                g_error_free (error);
                return EXIT_FAILURE;
        }

        if (print_version) {
                g_print ("GDM %s\n", VERSION);
                return EXIT_SUCCESS;
        }

        /* XDM compliant error message */
        if (getuid () != 0) {
                /* make sure the pid file doesn't get wiped */
                g_printerr ("%s\n", _("Only the root user can run GDM"));
                return EXIT_FAILURE;
        }

        if (fatal_warnings) {
                GLogLevelFlags fatal_mask;

                fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
                fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
                g_log_set_always_fatal (fatal_mask);
        }

        gdm_log_init ();

        settings = gdm_settings_new ();
        if (! gdm_settings_direct_init (settings, DATADIR "/gdm/gdm.schemas", "/")) {
                g_warning ("Unable to initialize settings");
                return EXIT_FAILURE;
        }

        gdm_log_set_debug (is_debug_set ());

        gdm_daemon_lookup_user (&gdm_uid, &gdm_gid);

        gdm_daemon_ensure_dirs (gdm_uid, gdm_gid);

        /* Connect to the bus, own the name and start the manager */
        bus_reconnect ();

        /* pid file */
        delete_pid ();
        write_pid ();

        g_chdir ("/");

        main_loop = g_main_loop_new (NULL, FALSE);

        g_unix_signal_add (SIGTERM, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGINT, on_shutdown_signal_cb, main_loop);
        g_unix_signal_add (SIGHUP, on_sighup_cb, NULL);

        if (do_timed_exit) {
                g_timeout_add_seconds (30, (GSourceFunc) timed_exit_cb, main_loop);
        }

        g_main_loop_run (main_loop);

        g_debug ("GDM finished, cleaning up...");

        g_clear_object (&manager);
        g_clear_object (&settings);

        gdm_settings_direct_shutdown ();
        gdm_log_shutdown ();

        g_main_loop_unref (main_loop);

        return EXIT_SUCCESS;
}

static void
on_name_acquired (GDBusConnection *bus,
                  const char      *name,
                  gpointer         user_data)
{
        gboolean xdmcp_enabled;
        gboolean show_local_greeter;
        gboolean remote_login_enabled;

        manager = gdm_manager_new ();
        if (manager == NULL) {
                g_warning ("Could not construct manager object");
                exit (EXIT_FAILURE);
        }

        g_debug ("Successfully connected to D-Bus");

        show_local_greeter = TRUE;
        gdm_settings_direct_get_boolean (GDM_KEY_SHOW_LOCAL_GREETER, &show_local_greeter);
        gdm_manager_set_show_local_greeter (manager, show_local_greeter);

        xdmcp_enabled = FALSE;
        gdm_settings_direct_get_boolean (GDM_KEY_XDMCP_ENABLE, &xdmcp_enabled);
        gdm_manager_set_xdmcp_enabled (manager, xdmcp_enabled);

        remote_login_enabled = FALSE;
        gdm_settings_direct_get_boolean (GDM_KEY_REMOTE_LOGIN_ENABLE, &remote_login_enabled);
        gdm_manager_set_remote_login_enabled (manager, remote_login_enabled);

        gdm_manager_start (manager);
}

static void
on_name_lost (GDBusConnection *bus,
              const char      *name,
              gpointer         user_data)
{
        g_debug ("Lost GDM name on bus");

        bus_connection_closed ();
}

static gboolean
bus_reconnect ()
{
        GDBusConnection *bus;
        gboolean         ret;

        ret = TRUE;

        bus = get_system_bus ();
        if (bus == NULL) {
                goto out;
        }

        name_id = g_bus_own_name_on_connection (bus,
                                                GDM_DBUS_NAME,
                                                G_BUS_NAME_OWNER_FLAGS_NONE,
                                                on_name_acquired,
                                                on_name_lost,
                                                NULL,
                                                NULL);

        ret = FALSE;
 out:
        return ret;
}
