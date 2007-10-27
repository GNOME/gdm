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
#include <glib/gstdio.h>
#include <glib-object.h>

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
#include "gdm-greeter-session.h"

#include "ck-connector.h"

#define GDM_SIMPLE_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_SLAVE, GdmSimpleSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmSimpleSlavePrivate
{
        char              *id;
        GPid               pid;
        guint              output_watch_id;
        guint              error_watch_id;

        guint              greeter_reset_id;

        int                ping_interval;

        GPid               server_pid;
        guint              connection_attempts;

        /* user selected */
        char              *selected_session;
        char              *selected_language;
        char              *selected_user;

        CkConnector       *ckc;
        GdmServer         *server;
        GdmGreeterServer  *greeter_server;
        GdmGreeterSession *greeter;
        GdmSession        *session;
        DBusGConnection   *connection;
};

enum {
        PROP_0,
};

static void     gdm_simple_slave_class_init     (GdmSimpleSlaveClass *klass);
static void     gdm_simple_slave_init           (GdmSimpleSlave      *simple_slave);
static void     gdm_simple_slave_finalize       (GObject             *object);

G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

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
        g_debug ("script environment: %s", str);
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
        char          *temp;

        display_name = NULL;
        display_hostname = NULL;
        display_x11_authority_file = NULL;
        display_is_local = FALSE;

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
        temp = g_strconcat (display_name, ".Xservers", NULL);
        x_servers_file = g_build_filename (AUTHDIR, temp, NULL);
        g_free (temp);

        g_hash_table_insert (hash, g_strdup ("X_SERVERS"), x_servers_file);

        if (! display_is_local) {
                g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (display_hostname));
        }

        /* Runs as root */
        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (display_x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (display_name));

        /*g_setenv ("PATH", gdm_daemon_config_get_value_string (GDM_KEY_ROOT_PATH), TRUE);*/

        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

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
on_session_started (GdmSession     *session,
                    GPid            pid,
                    GdmSimpleSlave *slave)
{
        g_debug ("session started on pid %d\n", (int) pid);

        /* FIXME: should we do something here? */
}

static void
on_session_exited (GdmSession     *session,
                   int             exit_code,
                   GdmSimpleSlave *slave)
{
        g_debug ("session exited with code %d\n", exit_code);

        gdm_slave_stopped (GDM_SLAVE (slave));
}

static void
on_session_died (GdmSession     *session,
                 int             signal_number,
                 GdmSimpleSlave *slave)
{
        g_debug ("session died with signal %d, (%s)",
                 signal_number,
                 g_strsignal (signal_number));

        gdm_slave_stopped (GDM_SLAVE (slave));
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

static gboolean
add_user_authorization (GdmSimpleSlave *slave,
                        char          **filename)
{
        char    *username;
        gboolean ret;

        username = gdm_session_get_username (slave->priv->session);
        ret = gdm_slave_add_user_authorization (GDM_SLAVE (slave),
                                                username,
                                                filename);
        g_free (username);

        return ret;
}

static gboolean
slave_open_ck_session (GdmSimpleSlave *slave,
                       const char     *display_name,
                       const char     *display_hostname,
                       gboolean        display_is_local)
{
        char          *username;
        char          *x11_display_device;
        struct passwd *pwent;
        gboolean       ret;
        int            res;
        DBusError      error;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        username = gdm_session_get_username (slave->priv->session);

        x11_display_device = NULL;

        pwent = getpwnam (username);
        if (pwent == NULL) {
                return FALSE;
        }

        slave->priv->ckc = ck_connector_new ();
        if (slave->priv->ckc == NULL) {
                g_warning ("Couldn't create new ConsoleKit connector");
                goto out;
        }

        if (slave->priv->server != NULL) {
                x11_display_device = gdm_server_get_display_device (slave->priv->server);
        }

        if (x11_display_device == NULL) {
                x11_display_device = g_strdup ("");
        }

        dbus_error_init (&error);
        res = ck_connector_open_session_with_parameters (slave->priv->ckc,
                                                         &error,
                                                         "unix-user", &pwent->pw_uid,
                                                         "x11-display", &display_name,
                                                         "x11-display-device", &x11_display_device,
                                                         "remote-host-name", &display_hostname,
                                                         "is-local", &display_is_local,
                                                         NULL);
        g_free (x11_display_device);

        if (! res) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("%s\n", error.message);
                        dbus_error_free (&error);
                } else {
                        g_warning ("cannot open CK session: OOM, D-Bus system bus not available,\n"
                                   "ConsoleKit not available or insufficient privileges.\n");
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
setup_session_environment (GdmSimpleSlave *slave)
{
        int         display_number;
        char       *display_x11_cookie;
        char       *display_name;
        char       *display_hostname;
        char       *auth_file;
        const char *session_cookie;
        gboolean    display_is_local;

        display_name = NULL;
        display_hostname = NULL;
        display_x11_cookie = NULL;
        auth_file = NULL;
        session_cookie = NULL;
        display_is_local = FALSE;

        g_object_get (slave,
                      "display-number", &display_number,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-is-local", &display_is_local,
                      "display-x11-cookie", &display_x11_cookie,
                      NULL);

        add_user_authorization (slave, &auth_file);

        if (slave_open_ck_session (slave,
                                   display_name,
                                   display_hostname,
                                   display_is_local)) {
                session_cookie = ck_connector_get_cookie (slave->priv->ckc);
        }

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
        if (session_cookie != NULL) {
                gdm_session_set_environment_variable (slave->priv->session,
                                                      "XDG_SESSION_COOKIE",
                                                      session_cookie);
        }

        gdm_session_set_environment_variable (slave->priv->session,
                                              "PATH",
                                              "/bin:/usr/bin:" BINDIR);

        g_free (display_name);
        g_free (display_hostname);
        g_free (display_x11_cookie);
        g_free (auth_file);
}

static void
on_session_user_verified (GdmSession     *session,
                          GdmSimpleSlave *slave)
{
        char    *username;
        char    *command;
        char    *filename;
        gboolean res;

        gdm_greeter_session_stop (slave->priv->greeter);
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

        gdm_session_start_program (session, command);

        g_free (filename);
        g_free (command);
}

static gboolean
greeter_reset_timeout (GdmSimpleSlave *slave)
{
        gdm_greeter_server_reset (slave->priv->greeter_server);
        slave->priv->greeter_reset_id = 0;
        return FALSE;
}

static void
queue_greeter_reset (GdmSimpleSlave *slave)
{
        if (slave->priv->greeter_reset_id > 0) {
                return;
        }

        slave->priv->greeter_reset_id = g_timeout_add_seconds (2, (GSourceFunc)greeter_reset_timeout, slave);
}

static void
on_session_user_verification_error (GdmSession     *session,
                                    GError         *error,
                                    GdmSimpleSlave *slave)
{
        char *username;

        username = gdm_session_get_username (session);

        g_debug ("could not successfully authenticate user '%s': %s",
                 username,
                 error->message);

        gdm_greeter_server_problem (slave->priv->greeter_server, _("Unable to authenticate user"));

        g_free (username);

        queue_greeter_reset (slave);
}

static void
on_session_info (GdmSession     *session,
                 const char     *text,
                 GdmSimpleSlave *slave)
{
        g_debug ("Info: %s", text);
        gdm_greeter_server_info (slave->priv->greeter_server, text);
}

static void
on_session_problem (GdmSession     *session,
                    const char     *text,
                    GdmSimpleSlave *slave)
{
        g_debug ("Problem: %s", text);
        gdm_greeter_server_problem (slave->priv->greeter_server, text);
}

static void
on_session_info_query (GdmSession     *session,
                       const char     *text,
                       GdmSimpleSlave *slave)
{

        g_debug ("Info query: %s", text);
        gdm_greeter_server_info_query (slave->priv->greeter_server, text);
}

static void
on_session_secret_info_query (GdmSession     *session,
                              const char     *text,
                              GdmSimpleSlave *slave)
{
        g_debug ("Secret info query: %s", text);
        gdm_greeter_server_secret_info_query (slave->priv->greeter_server, text);
}

static void
on_session_opened (GdmSession     *session,
                   GdmSimpleSlave *slave)
{
        gboolean res;

        g_debug ("session opened");
        res = gdm_greeter_server_ready (slave->priv->greeter_server);
        if (! res) {
                g_warning ("Unable to send ready");
        }
}

static void
create_new_session (GdmSimpleSlave *slave)
{
        g_debug ("Creating new session");

        slave->priv->session = gdm_session_new ();

        g_signal_connect (slave->priv->session,
                          "opened",
                          G_CALLBACK (on_session_opened),
                          slave);

        g_signal_connect (slave->priv->session,
                          "info",
                          G_CALLBACK (on_session_info),
                          slave);

        g_signal_connect (slave->priv->session,
                          "problem",
                          G_CALLBACK (on_session_problem),
                          slave);

        g_signal_connect (slave->priv->session,
                          "info-query",
                          G_CALLBACK (on_session_info_query),
                          slave);

        g_signal_connect (slave->priv->session,
                          "secret-info-query",
                          G_CALLBACK (on_session_secret_info_query),
                          slave);

        g_signal_connect (slave->priv->session,
                          "user-verified",
                          G_CALLBACK (on_session_user_verified),
                          slave);

        g_signal_connect (slave->priv->session,
                          "user-verification-error",
                          G_CALLBACK (on_session_user_verification_error),
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
}

static void
on_greeter_start (GdmGreeterSession *greeter,
                  GdmSimpleSlave    *slave)
{
        g_debug ("Greeter started");
}

static void
on_greeter_stop (GdmGreeterSession *greeter,
                 GdmSimpleSlave    *slave)
{
        g_debug ("Greeter stopped");
}

static void
on_greeter_begin_verification (GdmGreeterServer *greeter_server,
                               GdmSimpleSlave   *slave)
{
        GError *error;
        gboolean res;

        g_debug ("begin verification");
        error = NULL;
        res = gdm_session_begin_verification (slave->priv->session,
                                              NULL,
                                              &error);
        if (! res) {
                g_warning ("Unable to begin verification: %s", error->message);
                g_error_free (error);
        }
}

static void
on_greeter_begin_verification_for_user (GdmGreeterServer *greeter_server,
                                        const char       *username,
                                        GdmSimpleSlave   *slave)
{
        GError *error;
        gboolean res;

        g_debug ("begin verification");
        error = NULL;
        res = gdm_session_begin_verification (slave->priv->session,
                                              username,
                                              &error);
        if (! res) {
                g_warning ("Unable to begin verification for user: %s", error->message);
                g_error_free (error);
        }
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
on_greeter_user_selected (GdmGreeterServer *greeter_server,
                          const char       *text,
                          GdmSimpleSlave   *slave)
{
        g_debug ("Greeter user selected");
}

static void
on_greeter_cancel (GdmGreeterServer *greeter_server,
                   GdmSimpleSlave   *slave)
{
        gboolean       display_is_local;
        char          *display_name;
        char          *display_device;

        g_debug ("Greeter cancelled");

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-is-local", &display_is_local,
                      NULL);

        if (slave->priv->session != NULL) {
                gdm_session_close (slave->priv->session);
                g_object_unref (slave->priv->session);
        }

        create_new_session (slave);

        if (display_is_local) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        } else {
                display_device = g_strdup ("");
        }

        gdm_session_open (slave->priv->session,
                          "gdm",
                          "" /* hostname */,
                          display_name,
                          display_device,
                          NULL);

        g_free (display_device);
        g_free (display_name);
}

static void
on_greeter_connected (GdmGreeterServer *greeter_server,
                      GdmSimpleSlave   *slave)
{
        gboolean       display_is_local;
        char          *display_name;
        char          *display_device;

        g_object_get (slave,
                      "display-name", &display_name,
                      "display-is-local", &display_is_local,
                      NULL);

        g_debug ("Greeter started");

        if (display_is_local) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        } else {
                display_device = g_strdup ("");
        }

        gdm_session_open (slave->priv->session,
                          "gdm",
                          "" /* hostname */,
                          display_name,
                          display_device,
                          NULL);
        g_free (display_device);

        /* If XDMCP stop pinging */
        if ( ! display_is_local) {
                alarm (0);
        }

        g_free (display_name);
}

static void
run_greeter (GdmSimpleSlave *slave)
{
        gboolean       display_is_local;
        char          *display_id;
        char          *display_name;
        char          *display_device;
        char          *display_hostname;
        char          *auth_file;
        char          *address;

        g_debug ("Running greeter");

        display_is_local = FALSE;
        display_id = NULL;
        display_name = NULL;
        auth_file = NULL;
        display_device = NULL;
        display_hostname = NULL;

        g_object_get (slave,
                      "display-id", &display_id,
                      "display-is-local", &display_is_local,
                      "display-name", &display_name,
                      "display-hostname", &display_hostname,
                      "display-x11-authority-file", &auth_file,
                      NULL);

        g_debug ("Creating greeter for %s %s", display_name, display_hostname);

        if (slave->priv->server != NULL) {
                display_device = gdm_server_get_display_device (slave->priv->server);
        }

        /* Set the busy cursor */
        gdm_slave_set_busy_cursor (GDM_SLAVE (slave));

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

        /* Run the init script. gdmslave suspends until script has terminated */
        gdm_simple_slave_exec_script (slave,
                                      GDMCONFDIR"/Init",
                                      "gdm");

        create_new_session (slave);

        slave->priv->greeter_server = gdm_greeter_server_new (display_id);
        g_signal_connect (slave->priv->greeter_server,
                          "begin-verification",
                          G_CALLBACK (on_greeter_begin_verification),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "begin-verification-for-user",
                          G_CALLBACK (on_greeter_begin_verification_for_user),
                          slave);
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
                          "user-selected",
                          G_CALLBACK (on_greeter_user_selected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "connected",
                          G_CALLBACK (on_greeter_connected),
                          slave);
        g_signal_connect (slave->priv->greeter_server,
                          "cancelled",
                          G_CALLBACK (on_greeter_cancel),
                          slave);
        gdm_greeter_server_start (slave->priv->greeter_server);

        address = gdm_greeter_server_get_address (slave->priv->greeter_server);

        g_debug ("Creating greeter on %s %s %s", display_name, display_device, display_hostname);
        slave->priv->greeter = gdm_greeter_session_new (display_name,
                                                        display_device,
                                                        display_hostname,
                                                        display_is_local);
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
        gdm_greeter_session_set_server_address (slave->priv->greeter, address);
        gdm_greeter_session_start (slave->priv->greeter);

        g_free (display_id);
        g_free (display_name);
        g_free (display_device);
        g_free (display_hostname);
        g_free (auth_file);
}

static gboolean
idle_connect_to_display (GdmSimpleSlave *slave)
{
        gboolean res;

        slave->priv->connection_attempts++;

        res = gdm_slave_connect_to_x11_display (GDM_SLAVE (slave));
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
server_ready_cb (GdmServer      *server,
                 GdmSimpleSlave *slave)
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
                gdm_greeter_session_stop (GDM_SIMPLE_SLAVE (slave)->priv->greeter);
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

        return TRUE;
}

static void
gdm_simple_slave_set_property (GObject      *object,
                               guint          prop_id,
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
                               GValue      *value,
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
gdm_simple_slave_init (GdmSimpleSlave *slave)
{
        const char * const *languages;

        slave->priv = GDM_SIMPLE_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;

        languages = g_get_language_names ();
        if (languages != NULL) {
                slave->priv->selected_language = g_strdup (languages[0]);
        }

        slave->priv->selected_session = g_strdup ("gnome.desktop");
}

static void
gdm_simple_slave_finalize (GObject *object)
{
        GdmSimpleSlave *simple_slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SIMPLE_SLAVE (object));

        simple_slave = GDM_SIMPLE_SLAVE (object);

        g_return_if_fail (simple_slave->priv != NULL);

        gdm_simple_slave_stop (GDM_SLAVE (simple_slave));

        if (simple_slave->priv->greeter_reset_id > 0) {
                g_source_remove (simple_slave->priv->greeter_reset_id);
                simple_slave->priv->greeter_reset_id = 0;
        }

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
