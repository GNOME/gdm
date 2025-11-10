/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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

#include <gio/gio.h>

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "gdm-common.h"
#include "gdm-file-utils.h"

#include "gdm-session-enum-types.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_GREETER_SESSION "gnome-greeter"
#define GDM_GREETER_USERNAME "gdm-greeter"
#define GDM_GREETER_DISP_NAME "GDM Greeter"
#define GDM_SESSION_MODE "gdm"

#define INITIAL_SETUP_SESSION "gnome-initial-setup"
#define INITIAL_SETUP_USERNAME "gnome-initial-setup"
#define INITIAL_SETUP_DISP_NAME "GNOME Initial Setup"
#define INITIAL_SETUP_SESSION_MODE "initial-setup"
#define INITIAL_SETUP_GROUPNAME "gnome-initial-setup"
#define INITIAL_SETUP_DCONF_PROFILE "gnome-initial-setup"

extern char **environ;

struct _GdmLaunchEnvironment
{
        GObject         parent;
        GdmSession     *session;
        char           *session_name;
        GPid            pid;

        GdmDynamicUserStore *dyn_user_store;
        char                *preferred_user_name;
        char                *user_disp_name;
        char                *user_member_of;
        char                *dyn_user_name;
        uid_t                dyn_uid;
        char                *dyn_user_home;

        char           *dconf_profile;
        char           *session_id;
        char           *session_mode;
        char           *display_seat_id;
        char           *display_hostname;
        gboolean        display_is_local;
};

enum {
        PROP_0,
        PROP_SESSION_MODE,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_PREFERRED_USER_NAME,
        PROP_USER_DISP_NAME,
        PROP_USER_MEMBER_OF,
        PROP_DCONF_PROFILE,
        PROP_SESSION_NAME,
};

enum {
        OPENED,
        STARTED,
        STOPPED,
        EXITED,
        DIED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_launch_environment_class_init    (GdmLaunchEnvironmentClass *klass);
static void     gdm_launch_environment_init          (GdmLaunchEnvironment      *launch_environment);
static void     gdm_launch_environment_finalize      (GObject                   *object);

G_DEFINE_TYPE (GdmLaunchEnvironment, gdm_launch_environment, G_TYPE_OBJECT)

#define GID_NOBODY ((gid_t) 65534)
#define MIGRATED_STAMPFILE_PATH (GDM_WORKING_DIR "/.migrated-dyn-users")

static gboolean
migrate_working_dir (GError **error)
{
        /* Makes sure GDM_WORKING_DIR has the right permissions and ownership.
         * If it existed before the migration, g_mkdir_with_parents won't set
         * the right mode. On some systems it'll be set to 0700 and gdm:gdm,
         * which prevents the greeter session from accessing the subdirs within */
        if (g_file_test (GDM_WORKING_DIR, G_FILE_TEST_IS_DIR)) {
                if (g_chmod (GDM_WORKING_DIR, 0755) < 0) {
                        int errsv = errno;
                        g_set_error (error,
                                     G_IO_ERROR,
                                     g_io_error_from_errno (errsv),
                                     "Failed to chmod directory '%s': %s",
                                     GDM_WORKING_DIR,
                                     g_strerror (errsv));
                        return FALSE;
                }

                if (chown (GDM_WORKING_DIR, 0, 0) < 0) {
                        int errsv = errno;
                        g_set_error (error,
                                     G_IO_ERROR,
                                     g_io_error_from_errno (errsv),
                                     "Failed to chown directory '%s': %s",
                                     GDM_WORKING_DIR,
                                     g_strerror (errsv));
                        return FALSE;
                }
        }

        if (g_file_test (MIGRATED_STAMPFILE_PATH, G_FILE_TEST_EXISTS))
                return TRUE;

        if (g_mkdir_with_parents (GDM_WORKING_DIR "/seat0", 0755) < 0) {
                int errsv = errno;
                g_set_error (error,
                             G_IO_ERROR,
                             g_io_error_from_errno (errsv),
                             "Failed to create directory '%s': %s",
                             GDM_WORKING_DIR "/seat0",
                             g_strerror (errsv));
                return FALSE;
        }

        if (!gdm_copy_dir_recursively (GDM_WORKING_DIR "/.local/state",
                                       GDM_WORKING_DIR "/seat0/state",
                                       error)) {
               if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                       return FALSE;

                /* The directory not existing isn't an error */
                g_clear_error (error);
       }

        if (!gdm_copy_dir_recursively (GDM_WORKING_DIR "/.config",
                                       GDM_WORKING_DIR "/seat0/config",
                                       error)) {
                if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                       return FALSE;

                g_clear_error (error);
        }

        if (!g_file_set_contents (MIGRATED_STAMPFILE_PATH, "1", -1, error))
                return FALSE;

        return TRUE;
}

static gboolean
setup_seat_persist_dirs (const char  *seat_id,
                         uid_t        owner,
                         char       **out_config_dir,
                         char       **out_state_dir)
{
        g_autoptr (GError) error = NULL;
        g_autofree char *config_dir = NULL;
        g_autofree char *state_dir = NULL;

        /* HACK: This is a hack to address a regression introduced by the
         *       transition to dynamic users in GDM 49. GDM would no longer
         *       store settings changes, and thus lose the screen-reader enabled
         *       state and similar settings. The long-term fix would be to
         *       transition to userdb, store user settings in userdb, and then
         *       load these settings from GDM. Until then, this hack will do.
         *       Note that this hack will break once we implement homed support!
         *       This hack operates on the assumption that there's only ever one
         *       login screen per seat, which is true for now. This isn't
         *       necessarily true in the future, however, because the greeter
         *       and homed lock screen may coexist */

        if (!migrate_working_dir (&error)) {
                g_warning ("Failed to migrate " GDM_WORKING_DIR ": %s", error->message);
                g_clear_error (&error);
        }

        config_dir = g_strdup_printf (GDM_WORKING_DIR "/%s/config", seat_id);
        if (!gdm_ensure_dir (config_dir, owner, GID_NOBODY, 0700, TRUE, &error)) {
                g_warning ("Failed to initialize persist config dir for seat %s: %s",
                           seat_id, error->message);
                return FALSE;
        }

        state_dir = g_strdup_printf (GDM_WORKING_DIR "/%s/state", seat_id);
        if (!gdm_ensure_dir (state_dir, owner, GID_NOBODY, 0700, TRUE, &error)) {
                g_warning ("Failed to initialize persist state dir for seat %s: %s",
                           seat_id, error->message);
                return FALSE;
        }

        *out_config_dir = g_steal_pointer (&config_dir);
        *out_state_dir = g_steal_pointer (&state_dir);
        return TRUE;
}

static char *
get_var_cb (const char *var,
            gpointer user_data)
{
        const char *value = g_hash_table_lookup (user_data, var);
        return g_strdup (value);
}

static void
load_env_func (const char *var,
               const char *value,
               gpointer user_data)
{
        GHashTable *environment = user_data;
        g_hash_table_replace (environment, g_strdup (var), g_strdup (value));
}

static GHashTable *
build_launch_environment (GdmLaunchEnvironment *launch_environment,
                          gboolean              start_session)
{
        gboolean is_initial_setup = FALSE;
        GHashTable    *hash;
        static const char *const optional_environment[] = {
                "GI_TYPELIB_PATH",
                "LANG",
                "LANGUAGE",
                "LC_ADDRESS",
                "LC_ALL",
                "LC_COLLATE",
                "LC_CTYPE",
                "LC_IDENTIFICATION",
                "LC_MEASUREMENT",
                "LC_MESSAGES",
                "LC_MONETARY",
                "LC_NAME",
                "LC_NUMERIC",
                "LC_PAPER",
                "LC_TELEPHONE",
                "LC_TIME",
                "LD_LIBRARY_PATH",
                "PATH",
                "WINDOWPATH",
                "XCURSOR_PATH",
                "XDG_CONFIG_DIRS",
                NULL
        };
        g_autofree char *system_data_dirs = NULL;
        g_auto (GStrv) supported_session_types = NULL;
        gboolean debug;
        int i;

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        for (i = 0; optional_environment[i] != NULL; i++) {
                if (g_getenv (optional_environment[i]) == NULL) {
                        continue;
                }

                g_hash_table_insert (hash,
                                     g_strdup (optional_environment[i]),
                                     g_strdup (g_getenv (optional_environment[i])));
        }

        if (launch_environment->session_mode != NULL) {
                g_hash_table_insert (hash, g_strdup ("GNOME_SHELL_SESSION_MODE"), g_strdup (launch_environment->session_mode));
                g_hash_table_insert (hash, g_strdup ("DCONF_PROFILE"), g_strdup (launch_environment->dconf_profile));

                is_initial_setup = strcmp (launch_environment->session_mode, INITIAL_SETUP_SESSION_MODE) == 0;

                if (!is_initial_setup) {
			/* gvfs is needed for fetching remote avatars in the initial setup. Disable it otherwise. */
			g_hash_table_insert (hash, g_strdup ("GVFS_DISABLE_FUSE"), g_strdup ("1"));
			g_hash_table_insert (hash, g_strdup ("GIO_USE_VFS"), g_strdup ("local"));
			g_hash_table_insert (hash, g_strdup ("GVFS_REMOTE_VOLUME_MONITOR_IGNORE"), g_strdup ("1"));
		}
        }

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);
        if (debug)
                g_hash_table_insert (hash, g_strdup ("GNOME_SESSION_DEBUG"), g_strdup ("1"));

        g_hash_table_insert (hash, g_strdup ("LOGNAME"), g_strdup (launch_environment->dyn_user_name));
        g_hash_table_insert (hash, g_strdup ("USER"), g_strdup (launch_environment->dyn_user_name));
        g_hash_table_insert (hash, g_strdup ("USERNAME"), g_strdup (launch_environment->dyn_user_name));

        g_hash_table_insert (hash, g_strdup ("GDM_VERSION"), g_strdup (VERSION));
        g_hash_table_remove (hash, "MAIL");

        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup (launch_environment->dyn_user_home));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup (launch_environment->dyn_user_home));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup (NOLOGIN_PATH));

        if (start_session && launch_environment->display_seat_id != NULL) {
                char *seat_id;
                char *config_dir;
                char *state_dir;

                seat_id = launch_environment->display_seat_id;

                g_hash_table_insert (hash, g_strdup ("GDM_SEAT_ID"), g_strdup (seat_id));

                if (!is_initial_setup &&
                    setup_seat_persist_dirs(seat_id, launch_environment->dyn_uid, &config_dir, &state_dir)) {
                        g_hash_table_insert (hash, g_strdup ("XDG_CONFIG_HOME"), config_dir);
                        g_hash_table_insert (hash, g_strdup ("XDG_STATE_HOME"), state_dir);
                }
        }

        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

        /* Now populate XDG_DATA_DIRS from env.d if we're running initial setup; this allows
         * e.g. Flatpak apps to be recognized by gnome-shell.
         */
        if (g_strcmp0 (launch_environment->session_mode, INITIAL_SETUP_SESSION_MODE) == 0)
                gdm_load_env_d (load_env_func, get_var_cb, hash);

        /* Prepend our own XDG_DATA_DIRS value */
        system_data_dirs = g_strdup (g_hash_table_lookup (hash, "XDG_DATA_DIRS"));
        if (!system_data_dirs)
                system_data_dirs = g_strjoinv (":", (char **) g_get_system_data_dirs ());

        g_hash_table_insert (hash,
                             g_strdup ("XDG_DATA_DIRS"),
                             g_strdup_printf ("%s:%s:%s",
                                              DATADIR "/gdm/greeter",
                                              DATADIR,
                                              system_data_dirs));

        g_object_get (launch_environment->session,
                      "supported-session-types",
                      &supported_session_types,
                      NULL);
        g_hash_table_insert (hash,
                             g_strdup ("GDM_SUPPORTED_SESSION_TYPES"),
                             g_strjoinv (":", supported_session_types));

        return hash;
}

static void
on_session_setup_complete (GdmSession        *session,
                           const char        *service_name,
                           GdmLaunchEnvironment *launch_environment)
{
        g_autoptr(GHashTable) hash = NULL;
        GHashTableIter    iter;
        gpointer          key, value;

        hash = build_launch_environment (launch_environment, TRUE);

        g_hash_table_iter_init (&iter, hash);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                gdm_session_set_environment_variable (launch_environment->session, key, value);
        }
}

static void
on_session_opened (GdmSession           *session,
                   const char           *service_name,
                   const char           *session_id,
                   GdmLaunchEnvironment *launch_environment)
{
        launch_environment->session_id = g_strdup (session_id);

        g_signal_emit (G_OBJECT (launch_environment), signals [OPENED], 0);
        gdm_session_start_session (launch_environment->session, service_name);
}

static void
on_session_started (GdmSession           *session,
                    const char           *service_name,
                    int                   pid,
                    GdmLaunchEnvironment *launch_environment)
{
        launch_environment->pid = pid;
        g_signal_emit (G_OBJECT (launch_environment), signals [STARTED], 0);
}

static void
on_session_exited (GdmSession           *session,
                   int                   exit_code,
                   GdmLaunchEnvironment *launch_environment)
{
        gdm_session_stop_conversation (launch_environment->session, "gdm-launch-environment");

        g_signal_emit (G_OBJECT (launch_environment), signals [EXITED], 0, exit_code);
}

static void
on_session_died (GdmSession           *session,
                 int                   signal_number,
                 GdmLaunchEnvironment *launch_environment)
{
        gdm_session_stop_conversation (launch_environment->session, "gdm-launch-environment");

        g_signal_emit (G_OBJECT (launch_environment), signals [DIED], 0, signal_number);
}

static void
on_conversation_started (GdmSession           *session,
                         const char           *service_name,
                         GdmLaunchEnvironment *launch_environment)
{
        g_autofree char *log_path = NULL;
        g_autofree char *log_file = NULL;

        log_file = g_strdup ("greeter.log");

        log_path = g_build_filename (LOGDIR, log_file, NULL);

        gdm_session_setup_for_program (launch_environment->session,
                                       "gdm-launch-environment",
                                       launch_environment->dyn_user_name,
                                       log_path);
}

static void
on_conversation_stopped (GdmSession           *session,
                         const char           *service_name,
                         GdmLaunchEnvironment *launch_environment)
{
        g_autoptr(GdmSession) conversation_session = NULL;

        conversation_session = g_steal_pointer (&launch_environment->session);

        g_debug ("GdmLaunchEnvironment: conversation stopped");

        if (launch_environment->pid > 1) {
                gdm_signal_pid (-launch_environment->pid, SIGTERM);
                g_signal_emit (G_OBJECT (launch_environment), signals [STOPPED], 0);
        }

        if (conversation_session != NULL) {
                gdm_session_close (conversation_session);
        }
}

gboolean
gdm_launch_environment_ensure_uid (GdmLaunchEnvironment  *launch_environment,
                                   GdmDynamicUserStore   *dyn_user_store,
                                   uid_t                 *uid,
                                   GError               **error)
{
        if (launch_environment->dyn_uid != 0) {
                *uid = launch_environment->dyn_uid;
                return TRUE;
        }

        if (!gdm_dynamic_user_store_create (dyn_user_store,
                                            launch_environment->preferred_user_name,
                                            launch_environment->user_disp_name,
                                            launch_environment->user_member_of,
                                            &launch_environment->dyn_user_name,
                                            &launch_environment->dyn_uid,
                                            &launch_environment->dyn_user_home,
                                            error))
                return FALSE;

        /* We've allocated the UID, so let's make sure we can deallocate later */
        g_set_object (&launch_environment->dyn_user_store, dyn_user_store);

        *uid = launch_environment->dyn_uid;
        return TRUE;
}

/**
 * gdm_launch_environment_start:
 * @disp: Pointer to a GdmDisplay structure
 *
 * Starts a local X launch_environment. Handles retries and fatal errors properly.
 */
gboolean
gdm_launch_environment_start (GdmLaunchEnvironment *launch_environment)
{
        g_autoptr(GError) error = NULL;

        g_return_val_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (launch_environment), FALSE);
        g_return_val_if_fail (launch_environment->dyn_uid != 0, FALSE);

        g_debug ("GdmLaunchEnvironment: Starting...");

        launch_environment->session = gdm_session_new (GDM_SESSION_VERIFICATION_MODE_LOGIN,
                                                       launch_environment->dyn_uid,
                                                       launch_environment->display_hostname,
                                                       NULL,
                                                       launch_environment->display_seat_id,
                                                       launch_environment->display_is_local,
                                                       NULL);

        g_signal_connect_object (launch_environment->session,
                                 "conversation-started",
                                 G_CALLBACK (on_conversation_started),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "conversation-stopped",
                                 G_CALLBACK (on_conversation_stopped),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "setup-complete",
                                 G_CALLBACK (on_session_setup_complete),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "session-opened",
                                 G_CALLBACK (on_session_opened),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "session-started",
                                 G_CALLBACK (on_session_started),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "session-exited",
                                 G_CALLBACK (on_session_exited),
                                 launch_environment,
                                 0);
        g_signal_connect_object (launch_environment->session,
                                 "session-died",
                                 G_CALLBACK (on_session_died),
                                 launch_environment,
                                 0);

        gdm_session_start_conversation (launch_environment->session, "gdm-launch-environment");
        gdm_session_select_session (launch_environment->session, launch_environment->session_name);

        g_object_set (G_OBJECT (launch_environment->session),
                      "session-type", "wayland",
                      NULL);

        return TRUE;
}

gboolean
gdm_launch_environment_stop (GdmLaunchEnvironment *launch_environment)
{
        g_return_val_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (launch_environment), FALSE);

        if (launch_environment->pid > 1) {
                gdm_signal_pid (-launch_environment->pid, SIGTERM);
        }

        if (launch_environment->session != NULL) {
                gdm_session_close (launch_environment->session);

                g_clear_object (&launch_environment->session);
        }

        if (launch_environment->dyn_uid != 0) {
                gdm_dynamic_user_store_remove (launch_environment->dyn_user_store,
                                               launch_environment->dyn_uid);
                launch_environment->dyn_uid = 0;
        }

        g_signal_emit (G_OBJECT (launch_environment), signals [STOPPED], 0);

        return TRUE;
}

GdmSession *
gdm_launch_environment_get_session (GdmLaunchEnvironment *launch_environment)
{
        g_return_val_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (launch_environment), NULL);

        return launch_environment->session;
}

char *
gdm_launch_environment_get_session_id (GdmLaunchEnvironment *launch_environment)
{
        g_return_val_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (launch_environment), NULL);

        return g_strdup (launch_environment->session_id);
}

static void
_gdm_launch_environment_set_session_mode (GdmLaunchEnvironment *launch_environment,
                                          const char           *session_mode)
{
        g_free (launch_environment->session_mode);
        launch_environment->session_mode = g_strdup (session_mode);
}

static void
_gdm_launch_environment_set_display_seat_id (GdmLaunchEnvironment *launch_environment,
                                             const char           *sid)
{
        g_free (launch_environment->display_seat_id);
        launch_environment->display_seat_id = g_strdup (sid);
}

static void
_gdm_launch_environment_set_display_hostname (GdmLaunchEnvironment *launch_environment,
                                              const char           *name)
{
        g_free (launch_environment->display_hostname);
        launch_environment->display_hostname = g_strdup (name);
}

static void
_gdm_launch_environment_set_display_is_local (GdmLaunchEnvironment *launch_environment,
                                              gboolean              is_local)
{
        launch_environment->display_is_local = is_local;
}

static void
_gdm_launch_environment_set_preferred_user_name (GdmLaunchEnvironment *launch_environment,
                                                 const char           *name)
{
        g_free (launch_environment->preferred_user_name);
        launch_environment->preferred_user_name = g_strdup (name);
}

static void
_gdm_launch_environment_set_user_disp_name (GdmLaunchEnvironment *launch_environment,
                                            const char           *disp_name)
{
        g_free (launch_environment->user_disp_name);
        launch_environment->user_disp_name = g_strdup (disp_name);
}

static void
_gdm_launch_environment_set_user_member_of (GdmLaunchEnvironment *launch_environment,
                                            const char           *member_of)
{
        g_free (launch_environment->user_member_of);
        launch_environment->user_member_of = g_strdup (member_of);
}

static void
_gdm_launch_environment_set_dconf_profile (GdmLaunchEnvironment *launch_environment,
                                            const char           *profile)
{
        g_free (launch_environment->dconf_profile);
        launch_environment->dconf_profile = g_strdup (profile);
}

static void
_gdm_launch_environment_set_session_name (GdmLaunchEnvironment *launch_environment,
                                          const char           *session_name)
{
        g_set_str (&launch_environment->session_name, session_name);
}

static void
gdm_launch_environment_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
        GdmLaunchEnvironment *self;

        self = GDM_LAUNCH_ENVIRONMENT (object);

        switch (prop_id) {
        case PROP_SESSION_MODE:
                _gdm_launch_environment_set_session_mode (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_SEAT_ID:
                _gdm_launch_environment_set_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                _gdm_launch_environment_set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_launch_environment_set_display_is_local (self, g_value_get_boolean (value));
                break;
        case PROP_PREFERRED_USER_NAME:
                _gdm_launch_environment_set_preferred_user_name (self, g_value_get_string (value));
                break;
        case PROP_USER_DISP_NAME:
                _gdm_launch_environment_set_user_disp_name (self, g_value_get_string (value));
                break;
        case PROP_USER_MEMBER_OF:
                _gdm_launch_environment_set_user_member_of (self, g_value_get_string (value));
                break;
        case PROP_DCONF_PROFILE:
                _gdm_launch_environment_set_dconf_profile (self, g_value_get_string (value));
                break;
        case PROP_SESSION_NAME:
                _gdm_launch_environment_set_session_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_launch_environment_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
        GdmLaunchEnvironment *self;

        self = GDM_LAUNCH_ENVIRONMENT (object);

        switch (prop_id) {
        case PROP_SESSION_MODE:
                g_value_set_string (value, self->session_mode);
                break;
        case PROP_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->display_seat_id);
                break;
        case PROP_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->display_hostname);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->display_is_local);
                break;
        case PROP_PREFERRED_USER_NAME:
                g_value_set_string (value, self->preferred_user_name);
                break;
        case PROP_USER_DISP_NAME:
                g_value_set_string (value, self->user_disp_name);
                break;
        case PROP_USER_MEMBER_OF:
                g_value_set_string (value, self->user_member_of);
                break;
        case PROP_DCONF_PROFILE:
                g_value_set_string (value, self->dconf_profile);
                break;
        case PROP_SESSION_NAME:
                g_value_set_string (value, self->session_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_launch_environment_class_init (GdmLaunchEnvironmentClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_launch_environment_get_property;
        object_class->set_property = gdm_launch_environment_set_property;
        object_class->finalize = gdm_launch_environment_finalize;

        g_object_class_install_property (object_class,
                                         PROP_SESSION_MODE,
                                         g_param_spec_string ("session-mode",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "seat id",
                                                              "seat id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "hostname",
                                                              "hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "is local",
                                                               "is local",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_PREFERRED_USER_NAME,
                                         g_param_spec_string ("preferred-user-name",
                                                              "preferred user name",
                                                              "preferred user name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_USER_DISP_NAME,
                                         g_param_spec_string ("user-display-name",
                                                              "user display name",
                                                              "user display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_USER_MEMBER_OF,
                                         g_param_spec_string ("user-member-of",
                                                              "user member of",
                                                              "user member of",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DCONF_PROFILE,
                                         g_param_spec_string ("dconf-profile",
                                                              "dconf profile",
                                                              "dconf profile",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_SESSION_NAME,
                                         g_param_spec_string ("session-name",
                                                              "session name",
                                                              "session name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        signals [OPENED] =
                g_signal_new ("opened",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STARTED] =
                g_signal_new ("started",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              0,
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
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
}

static void
gdm_launch_environment_init (GdmLaunchEnvironment *launch_environment)
{
        launch_environment->session_name = NULL;
        launch_environment->session = NULL;
}

static void
gdm_launch_environment_finalize (GObject *object)
{
        GdmLaunchEnvironment *launch_environment;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LAUNCH_ENVIRONMENT (object));

        launch_environment = GDM_LAUNCH_ENVIRONMENT (object);

        gdm_launch_environment_stop (launch_environment);

        if (launch_environment->session) {
                g_object_unref (launch_environment->session);
        }

        g_free (launch_environment->session_name);
        g_free (launch_environment->preferred_user_name);
        g_free (launch_environment->dyn_user_name);
        g_free (launch_environment->dyn_user_home);
        g_free (launch_environment->user_disp_name);
        g_free (launch_environment->user_member_of);
        g_free (launch_environment->dconf_profile);
        g_free (launch_environment->display_seat_id);
        g_free (launch_environment->display_hostname);
        g_free (launch_environment->session_id);

        g_clear_object (&launch_environment->dyn_user_store);

        G_OBJECT_CLASS (gdm_launch_environment_parent_class)->finalize (object);
}

GdmLaunchEnvironment *
gdm_create_greeter_launch_environment (const char *seat_id,
                                       const char *display_hostname,
                                       gboolean    display_is_local)
{
        return g_object_new (GDM_TYPE_LAUNCH_ENVIRONMENT,
                             "session-name", GDM_GREETER_SESSION,
                             "preferred-user-name", GDM_GREETER_USERNAME,
                             "user-display-name", GDM_GREETER_DISP_NAME,
                             "user-member-of", GDM_GROUPNAME,
                             "dconf-profile", GDM_DCONF_PROFILE,
                             "display-seat-id", seat_id,
                             "session-mode", GDM_SESSION_MODE,
                             "display-hostname", display_hostname,
                             "display-is-local", display_is_local,
                             NULL);
}

GdmLaunchEnvironment *
gdm_create_initial_setup_launch_environment (const char *seat_id,
                                             const char *display_hostname,
                                             gboolean    display_is_local)
{
        return g_object_new (GDM_TYPE_LAUNCH_ENVIRONMENT,
                             "session-name", INITIAL_SETUP_SESSION,
                             "preferred-user-name", INITIAL_SETUP_USERNAME,
                             "user-display-name", INITIAL_SETUP_DISP_NAME,
                             "user-member-of", INITIAL_SETUP_GROUPNAME,
                             "dconf-profile", INITIAL_SETUP_DCONF_PROFILE,
                             "display-seat-id", seat_id,
                             "session-mode", INITIAL_SETUP_SESSION_MODE,
                             "display-hostname", display_hostname,
                             "display-is-local", display_is_local,
                             NULL);
}
