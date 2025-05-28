/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020-2023 Marco Trevisan <marco.trevisan@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <ctype.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gdm-common.h"

#define DCONF_SYSCONFIG_PROFILES_PATH DCONF_SYSCONFIG "/dconf/profile"
#define DCONF_SYSCONFIG_DB_PATH DCONF_SYSCONFIG "/dconf/db"
#define DCONF_SYSTEM_DB_PREFIX "system-db:"
#define DCONF_SYSTEM_DB_DEFAULT_NAME "gdm_auth_config"

#define GDM_DEFAULT_DCONF_PROFILE \
        DCONF_PROFILES_PATH "/" GDM_DCONF_PROFILE
#define GDM_CONFIG_DCONF_PROFILE \
        DCONF_SYSCONFIG_PROFILES_PATH "/" GDM_DCONF_PROFILE

#define GDM_CONFIG_DCONF_DB_NAME GDM_DCONF_PROFILE
#define GDM_CONFIG_DCONF_DB DCONF_SYSTEM_DB_PREFIX GDM_CONFIG_DCONF_DB_NAME

#define GDM_CONFIG_DCONF_OVERRIDE_NAME "01_gdm-config"
#define GDM_CONFIG_DCONF_LOCKS_NAME GDM_CONFIG_DCONF_OVERRIDE_NAME "-locks"

#define LOGIN_SCHEMA "org.gnome.login-screen"

#define GSD_SC_SCHEMA "org.gnome.settings-daemon.peripherals.smartcard"
#define GSD_SC_REMOVAL_ACTION_KEY "removal-action"

#define PASSWORD_KEY "enable-password-authentication"
#define FINGERPRINT_KEY "enable-fingerprint-authentication"
#define SMARTCARD_KEY "enable-smartcard-authentication"

static int opt_enable = -1;
static int opt_disable = -1;
static int opt_required = -1;
static gboolean opt_cmd_help = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_verbose = FALSE;
static const char *opt_removal_action = NULL;

typedef enum {
        COMMAND_HELP,
        COMMAND_SHOW,
        COMMAND_PASSWORD,
        COMMAND_FINGERPRINT,
        COMMAND_SMARTCARD,
        COMMAND_RESET,
        COMMAND_UNKNOWN,
} GdmConfigCommand;

typedef enum {
        AUTH_PASSWORD = COMMAND_PASSWORD,
        AUTH_FINGERPRINT = COMMAND_FINGERPRINT,
        AUTH_SMARTCARD = COMMAND_SMARTCARD,
        AUTH_NONE = COMMAND_UNKNOWN,
} GdmAuthType;

typedef enum {
        ACTION_UNSET,
        ACTION_INVALID,
        ACTION_ENABLED,
        ACTION_DISABLED,
        ACTION_REQUIRED,
} GdmAuthAction;

typedef struct
{
        GdmConfigCommand  config_command;
        GPtrArray        *args;
} OptionData;

typedef struct _GdmConfigCommandHandler GdmConfigCommandHandler;
typedef gboolean (*CommandHandlerFunc) (GdmConfigCommand, GError **);

typedef struct _GdmConfigCommandHandler {
        GPtrArray          *entries;
        GOptionParseFunc    post_parse_func;
        CommandHandlerFunc  handler_func;
        CommandHandlerFunc  options_handler_func;
        gpointer            data;
        GDestroyNotify      data_destroy;
} GdmConfigCommandHandler;

static const GOptionEntry generic_entries[] =
{
        {
                "help", 'h', 0, G_OPTION_ARG_NONE, &opt_cmd_help,
                N_("Show command help"), NULL
        },
        {
                "verbose", 'v', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_verbose,
                N_("Show verbose output"), NULL
        },
        {
                "debug", 'u', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_debug,
                N_("Show debug output"), NULL
        },
        { NULL }
};

static const GOptionEntry toggle_entries[] =
{
        {
                "enable", 'e', 0, G_OPTION_ARG_NONE, &opt_enable,
                N_("Enable the authentication method"), NULL
        },
        {
                "disable", 'd', 0, G_OPTION_ARG_NONE, &opt_disable,
                N_("Disable the authentication method"), NULL
        },
        { NULL }
};

static GOptionEntry smartcard_entries[] =
{
        {
                "required", 'r', 0, G_OPTION_ARG_NONE, &opt_required,
                N_("Require the authentication method"), NULL
        },
        {
                "removal-action", 'a', 0, G_OPTION_ARG_STRING, &opt_removal_action,
                N_("Action to perform on smartcard removal"), NULL,
        },
        { NULL }
};

static GOptionEntry reset_entries[] =
{
        {
                "yes", 'y', 0, G_OPTION_ARG_NONE, &opt_required,
                N_("Assume yes to any answer"), NULL
        },
        { NULL }
};

static const char* SC_REMOVAL_ACTIONS[] = {
        "none",         /* GSD_SMARTCARD_REMOVAL_ACTION_NONE */
        "lock-screen",  /* GSD_SMARTCARD_REMOVAL_ACTION_LOCK_SCREEN */
        "force-logout", /* GSD_SMARTCARD_REMOVAL_ACTION_FORCE_LOGOUT */
        "user-defined",
        NULL,
};

static gboolean handle_password (GdmConfigCommand, GError **);
static gboolean handle_fingerprint (GdmConfigCommand, GError **);
static gboolean handle_smartcard (GdmConfigCommand, GError **);
static gboolean handle_smartcard_options (GdmConfigCommand, GError **);
static gboolean handle_show (GdmConfigCommand, GError **);
static gboolean handle_reset (GdmConfigCommand, GError **);

static GdmAuthAction
get_requested_action (void)
{
        if (opt_enable == TRUE && opt_disable == TRUE) {
                return ACTION_INVALID;
        } else if (opt_required == TRUE && opt_disable == TRUE) {
                return ACTION_INVALID;
        }

        if (opt_required == TRUE)
                return ACTION_REQUIRED;

        if (opt_enable == TRUE)
                return ACTION_ENABLED;

        if (opt_disable == TRUE)
                return ACTION_DISABLED;

        return ACTION_UNSET;
}

static gboolean
smartcard_option_is_valid (const char *option_key,
                           const char *option_value)
{
        if (!option_key || !option_value)
                return FALSE;

        if (g_str_equal (GSD_SC_REMOVAL_ACTION_KEY, option_key)) {
                return g_strv_contains (SC_REMOVAL_ACTIONS, option_value);
        }

        return FALSE;
}

static GdmConfigCommand
parse_config_command (const char *command) {
        if (g_str_equal (command, "help")) {
                return COMMAND_HELP;
        } else if (g_str_equal (command, "password")) {
                return COMMAND_PASSWORD;
        } else if (g_str_equal (command, "fingerprint")) {
                return COMMAND_FINGERPRINT;
        } else if (g_str_equal (command, "smartcard")) {
                return COMMAND_SMARTCARD;
        } else if (g_str_equal (command, "show")) {
                return COMMAND_SHOW;
        } else if (g_str_equal (command, "reset")) {
                return COMMAND_RESET;
        }

        return COMMAND_UNKNOWN;
}

const char *
config_command_to_string (GdmConfigCommand config_command)
{
        switch (config_command) {
                case COMMAND_HELP:
                        return "help";
                case COMMAND_PASSWORD:
                        return "password";
                case COMMAND_FINGERPRINT:
                        return "fingerprint";
                case COMMAND_SMARTCARD:
                        return "smartcard";
                case COMMAND_RESET:
                        return "reset";
                case COMMAND_SHOW:
                        return "show";
                case COMMAND_UNKNOWN:
                        return "unknown";
        }

        g_assert_not_reached ();
}

static const char *
get_command_title (GdmConfigCommand config_command)
{
        switch (config_command) {
                case COMMAND_PASSWORD:
                        return _("Configure Password Authentication.");
                case COMMAND_FINGERPRINT:
                        return _("Configure Fingerprint Authentication.");
                case COMMAND_SMARTCARD:
                        return _("Configure Smart Card Authentication.");
                case COMMAND_RESET:
                        return _("Reset the GDM Authentication configuration.");
                case COMMAND_SHOW:
                        return _("Show GDM Authentication configuration.");
                default:
                        g_assert_not_reached ();
        }
}

static const char *
get_command_group_title (GdmConfigCommand config_command)
{
        switch (config_command) {
                case COMMAND_PASSWORD:
                        return _("Password options");
                case COMMAND_FINGERPRINT:
                        return _("Fingerprint options");
                case COMMAND_SMARTCARD:
                        return _("Smart Card options");
                case COMMAND_RESET:
                        return _("Reset options");
                case COMMAND_SHOW:
                        return _("Show options");
                default:
                        g_assert_not_reached ();
        }
}

static GdmAuthType
config_command_to_auth_type (GdmConfigCommand config_command)
{
        switch (config_command) {
                case COMMAND_PASSWORD:
                case COMMAND_SMARTCARD:
                case COMMAND_FINGERPRINT:
                        return (GdmAuthType) config_command;
                default:
                        return AUTH_NONE;
        }
}

const char *
auth_type_to_string (GdmAuthType auth_type)
{
        return config_command_to_string ((GdmConfigCommand) auth_type);
}

const char *
get_pam_module_missing_error (GdmAuthType auth_type)
{
        switch (auth_type) {
                case AUTH_PASSWORD:
                        return _("No PAM module available for Password authentication");
                case AUTH_SMARTCARD:
                        return _("No PAM module available for Smart Card authentication");
                case AUTH_FINGERPRINT:
                        return _("No PAM module available for Fingerprint authentication");
                default:
                        g_assert_not_reached ();
        }
}

const char *
auth_type_to_option_key (GdmAuthType auth_type)
{
        switch (auth_type) {
                case AUTH_PASSWORD:
                        return PASSWORD_KEY;
                case AUTH_FINGERPRINT:
                        return FINGERPRINT_KEY;
                case AUTH_SMARTCARD:
                        return SMARTCARD_KEY;
                default:
                        g_assert_not_reached ();
        }
}

static gboolean
toggle_option_check (GOptionContext *context,
                     GOptionGroup *group,
                     gpointer user_data,
                     GError **error)
{
        OptionData *data = user_data;

        if (data->args->len < 2) {
                g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                             _("“%s” needs at least one parameter"),
                             config_command_to_string (data->config_command));
                return FALSE;
        }

        if (get_requested_action () == ACTION_INVALID) {

                g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                             /* TRANSLATORS: “command” can't be enabled... */
                             _("“%s” can't be enabled and disabled at the same time"),
                             config_command_to_string (data->config_command));
                return FALSE;
        }

        return TRUE;
}

static gboolean
smartcard_option_check (GOptionContext *context,
                        GOptionGroup *group,
                        gpointer user_data,
                        GError **error)
{
        OptionData *data = user_data;

        if (!toggle_option_check (context, group, user_data, error))
                return FALSE;

        if (opt_removal_action &&
            !smartcard_option_is_valid (GSD_SC_REMOVAL_ACTION_KEY, opt_removal_action)) {
                g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                             /* TRANSLATORS: option is not a valid command “option-key” value */
                             _("“%s” is not a valid %s “%s” value"),
                             opt_removal_action,
                             config_command_to_string (data->config_command),
                             GSD_SC_REMOVAL_ACTION_KEY);
                return FALSE;
        }

        return TRUE;
}

static void
gdm_config_command_handler_free (GdmConfigCommandHandler *command_handler)
{
        if (command_handler->data_destroy)
                command_handler->data_destroy (command_handler->data);

        g_clear_pointer (&command_handler->entries, g_ptr_array_unref);
        g_free (command_handler);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdmConfigCommandHandler, gdm_config_command_handler_free);

static GdmConfigCommandHandler *
config_command_get_handler (GdmConfigCommand config_command)
{
        g_autoptr (GdmConfigCommandHandler) cmd_entries = NULL;

        cmd_entries = g_new0 (GdmConfigCommandHandler, 1);
        cmd_entries->entries = g_ptr_array_new ();

        switch (config_command) {
                case COMMAND_PASSWORD:
                case COMMAND_FINGERPRINT:
                case COMMAND_SMARTCARD:
                        g_ptr_array_add (cmd_entries->entries, (gpointer) toggle_entries);
                        cmd_entries->post_parse_func = toggle_option_check;
                        break;
                case COMMAND_SHOW:
                        cmd_entries->handler_func = handle_show;
                        return g_steal_pointer (&cmd_entries);
                case COMMAND_RESET:
                        g_ptr_array_add (cmd_entries->entries, (gpointer) reset_entries);
                        cmd_entries->handler_func = handle_reset;
                        return g_steal_pointer (&cmd_entries);
                case COMMAND_HELP:
                case COMMAND_UNKNOWN:
                        return NULL;
                default:
                        g_assert_not_reached ();
        }

        if (config_command == COMMAND_PASSWORD) {
                cmd_entries->handler_func = handle_password;
        } else if (config_command == COMMAND_FINGERPRINT) {
                cmd_entries->handler_func = handle_fingerprint;
        } else if (config_command == COMMAND_SMARTCARD) {
                g_autofree char *removal_args = NULL;
                int i;

                removal_args = g_strjoinv ("|", (GStrv) SC_REMOVAL_ACTIONS);
                for (i = 0; smartcard_entries[i].long_name; ++i) {
                        if (!g_str_equal (smartcard_entries[i].long_name, "removal-action"))
                                continue;

                        smartcard_entries[i].arg_description = removal_args;

                        cmd_entries->data = g_steal_pointer (&removal_args);
                        cmd_entries->data_destroy = g_free;
                }
                g_ptr_array_add (cmd_entries->entries, smartcard_entries);
                cmd_entries->post_parse_func = smartcard_option_check;
                cmd_entries->handler_func = handle_smartcard;
                cmd_entries->options_handler_func = handle_smartcard_options;
        } else {
                g_assert_not_reached ();
        }

        return g_steal_pointer (&cmd_entries);
}

static void
config_command_handler_add_to_group (GdmConfigCommandHandler *command_handler,
                                  GOptionGroup            *group)
{
        guint i;

        if (!command_handler || !command_handler->entries)
                return;

        for (i = 0; i < command_handler->entries->len; ++i) {
                const GOptionEntry *entries;

                entries = g_ptr_array_index (command_handler->entries, i);
                g_option_group_add_entries (group, entries);
        }

        g_option_group_set_parse_hooks (group, NULL,
                                        command_handler->post_parse_func);
}

static GPtrArray *
read_file_contents_to_array (const char  *file_path,
                             GError     **error)
{
        g_autoptr(GPtrArray) array = NULL;
        g_autofree char *content = NULL;

        array = g_ptr_array_new_with_free_func (g_free);

        if (!g_file_get_contents (file_path, &content, NULL, error)) {
                return NULL;
        }

        if (content) {
                char **lines = g_strsplit (content, "\n", -1);
                int i;

                if (lines && *lines) {
                        array->len = g_strv_length (lines) + 1;
                        array->pdata = (gpointer *) g_steal_pointer (&lines);
                }

                /* Strip the trailing empty and NULL lines */
                for (i = array->len - 1; i >= 0; i--) {
                        char *line = g_ptr_array_index (array, i);

                        if (line && (g_ascii_isgraph (line[0]) || strlen (line) > 1))
                                break;

                        g_ptr_array_remove_index (array, i);
                }
        }

        return g_steal_pointer (&array);
}

static gboolean
write_array_to_file (GPtrArray  *array,
                     char       *file_path,
                     GError    **error)
{
        g_autoptr (GPtrArray) array_copy = NULL;
        g_autofree char *contents = NULL;

        array_copy = g_ptr_array_copy (array, NULL, NULL);
        g_ptr_array_set_free_func (array_copy, NULL);

        if (array->len) {
                /* Ensure final new line */
                if (!g_str_equal (g_ptr_array_index (array_copy, array->len - 1), ""))
                        g_ptr_array_add (array_copy, "");
        }

        g_ptr_array_add (array_copy, NULL);

        contents = g_strjoinv ("\n", (char **) array_copy->pdata);

        return g_file_set_contents (file_path, contents, -1, error);
}

GPtrArray *
build_distro_hook_arguments (const char       *distro_hook,
                             GdmConfigCommand  config_command,
                             GPtrArray        *command_args)
{
        g_autoptr(GPtrArray) call_args = NULL;
        GdmAuthAction action;

        action = get_requested_action ();
        call_args = g_ptr_array_new ();

        g_ptr_array_add (call_args, (char *) distro_hook);
        g_ptr_array_add (call_args, (char *) config_command_to_string (config_command));

        if (command_args)
                g_ptr_array_extend (call_args, command_args, NULL, NULL);
        else if (action == ACTION_REQUIRED)
                g_ptr_array_add (call_args, "require");
        else if (action == ACTION_ENABLED)
                g_ptr_array_add (call_args, "enable");
        else if (action == ACTION_DISABLED)
                g_ptr_array_add (call_args, "disable");
        else if (config_command != COMMAND_RESET)
                return NULL;

        g_ptr_array_add (call_args, NULL);

        if (opt_verbose) {
                g_autofree char *cmdline = NULL;

                cmdline = g_strjoinv (" ", (GStrv) call_args->pdata);
                g_debug ("Calling hook command “%s”", cmdline);
        }

        return g_steal_pointer (&call_args);
}

static gboolean
try_run_distro_hook (GdmConfigCommand   config_command,
                     GPtrArray         *command_args,
                     char             **stdout_out,
                     GError           **error)
{
        g_autoptr(GPtrArray) call_args = NULL;
        g_autoptr(GError) local_error = NULL;
        g_autofree char *distro_hook = NULL;
        g_autofree char *local_stdout = NULL;
        g_autofree char *local_stderr = NULL;
        gint exit_status;

        /* Distro hooks need to follow these rules:
         *  - The name of the hook should be "gdm-auth-config-$DISTRO_NAME",
         *  - Must be executable and being placed in the GDM's libexec dir.
         *
         * And it will be called in this way:
         *  gdm-auth-config-foo [command] [enable|disable|require|$option] \
         *                                [option-specific-params]
         * or
         *  gdm-auth-config-foo show [command] [$option]
         * or
         *  gdm-auth-config-foo reset [require]
         *
         * In set mode, if the exit code is 19 (as SIGSTOP), we won't proceed
         * doing further actions, as we consider that the script already handled
         * all the required changes. Otherwise if it the exit status is 0,
         * then we will continue performing the default actions.
         *
         * When in 'show' mode it should print in stdout one of these values:
         *  - required
         *  - enabled
         *  - disabled
         *  - command's option specific output
         *
         * However, we'll always double-check that the returned value matches
         * the system settings.
         *
         * For example:
         *  gdm-auth-config-foo show smartcard
         *  > expected: enabled|disabled|required
         *  gdm-auth-config-foo show smartcard removal-action
         *  > expected: 'none|log-out|lock-screen'
         *  gdm-auth-config-foo password disable
         *  gdm-auth-config-foo smartcard require
         *  > expected: exit status of 0 if done or 19 if we need to continue
         */

        if (DISTRO[0] == '\0' || g_str_equal (DISTRO, "none")) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             _("No distro detected, no hook to run"));
                return FALSE;
        }

        distro_hook = g_build_filename (LIBEXECDIR, "gdm-auth-config-" DISTRO, NULL);
        g_debug ("Looking for distro hook “%s”", distro_hook);

        if (!g_file_test (distro_hook, G_FILE_TEST_IS_REGULAR |
                                       G_FILE_TEST_IS_EXECUTABLE)) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "%s is not an executable", distro_hook);
                return FALSE;
        }

        call_args = build_distro_hook_arguments (distro_hook, config_command, command_args);

        if (!call_args) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             _("No valid args found to run hook “%s”"), distro_hook);
                return FALSE;
        }

        if (opt_verbose)
                g_print (_("Running distro hook “%s”\n"), distro_hook);

        g_spawn_sync (NULL, (GStrv) call_args->pdata, NULL, G_SPAWN_DEFAULT,
                      NULL, NULL, &local_stdout, &local_stderr, &exit_status, &local_error);

        if (local_stdout)
                local_stdout = g_strstrip (local_stdout);
        if (local_stderr)
                local_stderr = g_strstrip (local_stderr);

        if (!local_error) {
                if (WEXITSTATUS (exit_status) == SIGSTOP) {
                        g_print ("%s\n", local_stdout);
                        g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                     _("Distro hook “%s” requested stopping"),
                                     distro_hook);
                        return FALSE;
                } else {
#if GLIB_CHECK_VERSION (2, 70, 0)
                        g_spawn_check_wait_status (exit_status, &local_error);
#else
                        g_spawn_check_exit_status (exit_status, &local_error);
#endif
                }
        }

        if (local_error) {
                g_warning (_("Distro hook failed with exit status %d and error %s:\n"
                             "Standard output:\n%s\n"
                             "Error output:\n%s"),
                           WEXITSTATUS (exit_status), local_error->message,
                           local_stderr, local_stdout);
                g_propagate_error (error, g_steal_pointer (&local_error));
                return FALSE;
        }

        g_debug ("Distro hook ran correctly\n"
                 "Standard output:\n%s\n"
                 "Error output:\n%s",
                local_stdout, local_stderr);

        if (stdout_out)
                *stdout_out = g_steal_pointer (&local_stdout);

        return TRUE;
}

static gboolean
set_distro_hook_config (GdmConfigCommand   config_command,
                        const char        *option_key,
                        const char        *option_value,
                        GError           **error)
{
        g_autoptr(GPtrArray) command_args = NULL;
        g_autoptr(GError) local_error = NULL;
        g_autofree char *local_stdout = NULL;

        command_args = g_ptr_array_sized_new (2);
        g_ptr_array_add (command_args, (char *) option_key);
        g_ptr_array_add (command_args, (char *) option_value);

        if (!try_run_distro_hook (config_command, command_args, &local_stdout, &local_error)) {
                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        g_debug ("Distro hook for %s %s requested us to stop",
                                 config_command_to_string (config_command), option_key);
                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }

                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
                        g_debug ("Distro hook for command %s option-key %s not found: %s",
                        config_command_to_string (config_command), option_key,
                        local_error->message);
                }

                g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                            /* TRANSLATORS: Failed to set command “command” option key “option-key” via distro hook */
                                            _("Failed to set command “%s” option key “%s” via distro hook: "),
                                            config_command_to_string (config_command),
                                            option_key);
                return FALSE;
        }

        g_debug ("Distro hook for %s %s completed, "
                 "continuing with default action...",
                 config_command_to_string (config_command), option_key);
        g_print ("%s\n", local_stdout);

        return TRUE;
}

static char *
get_distro_hook_config (GdmAuthType   auth_type,
                        const char   *option_key,
                        GError      **error)
{
        g_autofree char *output = NULL;
        g_autoptr(GPtrArray) command_args = NULL;

        command_args = g_ptr_array_sized_new (2);
        g_ptr_array_add (command_args, (char *) auth_type_to_string (auth_type));
        g_ptr_array_add (command_args, (char *) option_key);

        if (!try_run_distro_hook (COMMAND_SHOW, command_args, &output, error)) {
                return NULL;
        }

        return g_steal_pointer (&output);
}

static gboolean
make_directory_with_parents (const char *path,
                             GError     **error)
{
        if (!g_file_test (path, G_FILE_TEST_IS_DIR) &&
            g_mkdir_with_parents (path, 0755) != 0) {
                g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                             _("Failed to create directory %s"), path);
                return FALSE;
        }

        return TRUE;
}

static gboolean
check_distro_hook_is_required (GdmConfigCommand config_command)
{
        switch (config_command) {
                case COMMAND_SMARTCARD:
                        /* Enabling smartcard or requiring authentication might
                         * involve further actions that depend on distribution,
                         * (such as switching PAM profiles), as per this we
                         * always require an hook to be present.
                         * For the generic cases for which GDM already ships PAM
                         * configuration files, we may use a generic hook that
                         * does nothing, but leaves us to change the parameters
                         * that we can handle.
                         */
                        return (opt_enable == TRUE || opt_required == TRUE);
                default:
                        return FALSE;
        }
}

static const char *
get_dconf_system_profile (void)
{
        const char *profile;

        profile = g_getenv ("DCONF_PROFILE");
        if (profile)
                return profile;

        return "user";
}

static char *
get_dconf_db_path (const char *db_name)
{
        g_autofree char *db_dir = NULL;

        db_dir = g_strdup_printf ("%s.d", db_name);

        return g_build_filename (DCONF_SYSCONFIG_DB_PATH, db_dir, NULL);
}

static char *
get_dconf_system_profile_file (GError **error)
{
        const char *profile;
        const char * const *xdg_data_dirs;
        const char *prefix;

        /* This is based on what happens on dconf_engine_open_profile_file */
        profile = get_dconf_system_profile ();
        prefix = DCONF_SYSCONFIG;

        xdg_data_dirs = g_get_system_data_dirs ();
        do {
                g_autofree char *filename = NULL;

                filename = g_build_filename (prefix, "dconf", "profile", profile, NULL);
                if (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
                            return g_steal_pointer (&filename);
                }
        } while ((prefix = *xdg_data_dirs++));

        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     _("dconf profile not found"));

        return NULL;
}

static char *
ensure_dconf_configurable_system_profile (GError **error)
{
        g_autofree char *profile_file = NULL;
        g_autofree char *db_path = NULL;
        g_autoptr(GPtrArray) profile_lines = NULL;
        const char *gdm_sys_db;

        gdm_sys_db = DCONF_SYSTEM_DB_PREFIX DCONF_SYSTEM_DB_DEFAULT_NAME;
        profile_file = g_build_filename (DCONF_SYSCONFIG_PROFILES_PATH,
                                         get_dconf_system_profile (), NULL);

        if (!g_file_test (profile_file, G_FILE_TEST_IS_REGULAR)) {
                if (!make_directory_with_parents (DCONF_SYSCONFIG_PROFILES_PATH, error))
                        return FALSE;

                profile_lines = g_ptr_array_new_with_free_func (g_free);
                g_ptr_array_add (profile_lines, g_strdup ("user-db:user"));
                g_ptr_array_add (profile_lines, g_strdup (gdm_sys_db));
        } else {
                profile_lines = read_file_contents_to_array (profile_file, error);
                if (!profile_lines)
                        return FALSE;

                if (!g_ptr_array_find_with_equal_func (profile_lines, gdm_sys_db,
                                                       g_str_equal, NULL)) {
                        g_ptr_array_add (profile_lines, g_strdup (gdm_sys_db));
                } else {
                        g_clear_pointer (&profile_lines, g_ptr_array_unref);
                }
        }

        if (profile_lines) {
                if (!write_array_to_file (profile_lines, profile_file, error))
                        return FALSE;
        }

        db_path = get_dconf_db_path (DCONF_SYSTEM_DB_DEFAULT_NAME);
        if (!make_directory_with_parents (db_path, error)) {
                return FALSE;
        }

        return g_steal_pointer (&profile_file);
}

GPtrArray *
get_system_dconf_profile_contents (GError **error)
{
        g_autofree char *profile_file = NULL;

        profile_file = get_dconf_system_profile_file (error);
        if (!profile_file)
                return NULL;

        return read_file_contents_to_array (profile_file, error);
}

char *
get_system_dconf_profile_db_name (const char  *preferred,
                                  GError     **error)
{
        g_autoptr (GPtrArray) profile_contents = NULL;
        const char *db_name = NULL;
        int i;

        profile_contents = get_system_dconf_profile_contents (error);
        if (!profile_contents)
                return NULL;

        for (i = 0; i < profile_contents->len; i++) {
                const char *profile_line = g_ptr_array_index (profile_contents, i);

                if (g_str_has_prefix (profile_line, DCONF_SYSTEM_DB_PREFIX) &&
                    strlen (profile_line) > sizeof (DCONF_SYSTEM_DB_PREFIX) - 1) {
                        db_name = profile_line + sizeof (DCONF_SYSTEM_DB_PREFIX) - 1;

                        if (!preferred || g_str_equal (preferred, db_name))
                                return g_strdup (db_name);
                }
        }

        if (db_name)
                return g_strdup (db_name);

        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     _("dconf has no system-db configured"));

        return NULL;
}

static char *
ensure_system_user_dconf_profile (GError **error)
{
        g_autofree char *db_name = NULL;
        g_autoptr(GError) local_error = NULL;

        db_name = get_system_dconf_profile_db_name (DCONF_SYSTEM_DB_DEFAULT_NAME,
                                                    &local_error);
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
                g_autofree char *profile_file = NULL;

                profile_file = ensure_dconf_configurable_system_profile (error);
                if (!profile_file)
                        return FALSE;

                g_debug ("Initialized dconf profile at %s", profile_file);
                db_name = get_system_dconf_profile_db_name (DCONF_SYSTEM_DB_DEFAULT_NAME,
                                                            error);
                if (!db_name)
                        return FALSE;
        } else if (local_error) {
                g_propagate_error (error, g_steal_pointer (&local_error));
                return FALSE;
        }

        g_assert (db_name);
        g_debug ("Found system database with name “%s”", db_name);

        return g_steal_pointer (&db_name);
}

static gboolean
ensure_gdm_config_dconf_profile (GError **error)
{
        g_autofree char* current_content = NULL;
        gboolean needs_profile_update = TRUE;

        g_debug ("Ensuring dconf profile in " GDM_CONFIG_DCONF_PROFILE);

        g_file_get_contents (GDM_CONFIG_DCONF_PROFILE, &current_content,
                             NULL, NULL);

        if (current_content) {
                g_auto(GStrv) content_lines = NULL;

                content_lines = g_strsplit (current_content, "\n", -1);

                if (g_strv_contains ((const char **) content_lines,
                                     GDM_CONFIG_DCONF_DB)) {
                        g_debug ("Profile doesn't need to be updated");
                        needs_profile_update = FALSE;
                }
        }

        if (needs_profile_update) {
                g_autofree char* default_content = NULL;
                g_autofree char* profile_content = NULL;

                if (!g_file_get_contents (GDM_DEFAULT_DCONF_PROFILE, &default_content,
                                          NULL, error))
                        return FALSE;

                profile_content = g_strdup_printf ("# This profile is managed by %s\n"
                                                   "%s\n"
                                                   "%s\n",
                                                   g_get_prgname(),
                                                   default_content,
                                                   GDM_CONFIG_DCONF_DB);

                g_debug ("Creating profile file "
                         GDM_CONFIG_DCONF_PROFILE
                         " using default content from "
                         GDM_DEFAULT_DCONF_PROFILE " and "
                         GDM_CONFIG_DCONF_DB);

                if (!make_directory_with_parents (DCONF_SYSCONFIG_PROFILES_PATH, error))
                        return FALSE;

                if (!g_file_set_contents (GDM_CONFIG_DCONF_PROFILE, profile_content,
                                          -1, error))
                        return FALSE;
        }

        return TRUE;
}

static char *schema_to_key (const char *schema)
{
        g_auto(GStrv) split = NULL;

        split = g_strsplit (schema, ".", -1);
        return g_strjoinv ("/", split);
}

static gboolean
write_dconf_setting_to_key_file (const char  *db_name,
                                 const char  *file_name,
                                 const char  *schema,
                                 const char  *key,
                                 GVariant    *value,
                                 GError     **error)
{
        g_autoptr(GKeyFile) key_file = NULL;
        g_autoptr(GError) local_error = NULL;
        g_autoptr(GVariant) value_ref = NULL;
        g_autofree char *value_str = NULL;
        g_autofree char *header_comment = NULL;
        g_autofree char *db_dir = NULL;
        g_autofree char *file_path = NULL;
        g_autofree char *setting_path = NULL;
        gboolean create_new = FALSE;

        db_dir = get_dconf_db_path (db_name);
        file_path = g_build_filename (db_dir, file_name, NULL);
        key_file = g_key_file_new ();

        if (!g_key_file_load_from_file (key_file, file_path,
                                        G_KEY_FILE_KEEP_COMMENTS, &local_error)) {
                create_new = TRUE;
                g_debug ("Impossible to load setting file “%s” %s", file_path,
                         local_error->message);

                if (!value) {
                        /* We had no value to set anyways... */
                        return TRUE;
                }
        }

        header_comment = g_strdup_printf ("This file has been generated by %s\n"
                                          "Do no edit!", g_get_prgname ());

        if (create_new) {
                if (!g_key_file_set_comment (key_file, NULL, NULL,
                                             header_comment, error)) {
                        return FALSE;
                }
        } else {
                g_autofree char *file_comment = NULL;

                file_comment = g_key_file_get_comment (key_file, NULL, NULL, &local_error);
                file_comment = g_strstrip (file_comment);

                if (local_error) {
                        /* TRANSLATORS: First value is a file path, second is an error message */
                        g_warning (_("Failed to get the “%s” header comment: %s, was it modified?"),
                                   file_path, local_error->message);
                } else if (g_strcmp0 (file_comment, header_comment) != 0) {
                        g_warning (_("File “%s” header comment does not match, was it modified?"),
                                   file_path);
                }
        }

        setting_path = schema_to_key (schema);

        if (value) {
                value_ref = g_variant_ref_sink (value);
                value_str = g_variant_print (value_ref, FALSE);

                if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
                        g_key_file_set_boolean (key_file, setting_path, key,
                                                g_variant_get_boolean (value));
                else
                        g_key_file_set_string (key_file, setting_path, key, value_str);
        } else {
                g_clear_error (&local_error);
                g_key_file_remove_key (key_file, setting_path, key, &local_error);

                if (local_error &&
                    !g_error_matches (local_error, G_KEY_FILE_ERROR,
                                      G_KEY_FILE_ERROR_KEY_NOT_FOUND) &&
                    !g_error_matches (local_error, G_KEY_FILE_ERROR,
                                      G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }
        }

        g_debug ("Writing %s configuration “%s = %s” to file %s",
                 setting_path, key, value_str,
                 file_path);

        if (create_new && !make_directory_with_parents (db_dir, error))
                return FALSE;

        return g_key_file_save_to_file (key_file, file_path, error);
}

static gboolean
remove_dconf_setting_key_file (const char *db_name,
                               const char *key_file,
                               GError     **error)
{
        g_autofree char *db_dir = NULL;
        g_autofree char *key_file_path = NULL;
        g_autoptr (GFile) file = NULL;

        db_dir = get_dconf_db_path (db_name);
        key_file_path = g_build_filename (db_dir, key_file, NULL);
        file = g_file_new_for_path (key_file_path);

        g_debug ("Removing setting key file %s", key_file_path);

        if (g_file_delete (file, NULL, error)) {
                /* Try to remove the parent dir, if empty */
                g_autoptr (GFile) parent_dir = g_file_get_parent (file);
                g_file_delete (parent_dir, NULL, NULL);
                return TRUE;
        }

        return FALSE;
}

static char *
get_dconf_db_lock_path (const char *db_name,
                        const char *lock_name)
{
        g_autofree char *db_dir = NULL;
        g_autofree char *db_locks_dir = NULL;

        db_dir = get_dconf_db_path (db_name);
        db_locks_dir = g_build_filename (db_dir, "locks", NULL);

        return g_build_filename (db_locks_dir, lock_name, NULL);
}

static gboolean
set_dconf_setting_lock_full (const char *db_name,
                             const char *lock_name,
                             const char *schema,
                             const char *key,
                             gboolean    add,
                             GError    **error)
{
        g_autoptr(GPtrArray) locks_list = NULL;
        g_autoptr(GError) local_error = NULL;
        g_autofree char *lock_file = NULL;
        g_autofree char *file_header = NULL;
        g_autofree char *locked_schema = NULL;
        g_autofree char *full_key = NULL;
        gboolean found;
        unsigned int idx;

        lock_file = get_dconf_db_lock_path (db_name, lock_name);
        locks_list = read_file_contents_to_array (lock_file, &local_error);

        if (local_error) {
                g_debug ("Impossible to read current locks: %s", local_error->message);

                if (!add) {
                        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
                            g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
                                return TRUE;

                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }
        }

        file_header = g_strdup_printf ("# This file is managed by %s",
                                       g_get_prgname ());

        if (!locks_list) {
                g_autofree char *db_locks_dir = NULL;

                g_assert (add);
                locks_list = g_ptr_array_new_with_free_func (g_free);
                g_ptr_array_add (locks_list, g_steal_pointer (&file_header));
                g_ptr_array_add (locks_list, g_strdup ("# Do no edit!"));

                db_locks_dir = g_path_get_dirname (lock_file);
                if (!make_directory_with_parents (db_locks_dir, error))
                        return FALSE;
        } else {
                if (locks_list->len && !g_str_equal (g_ptr_array_index (locks_list, 0), file_header)) {
                        /* XXX: Fail with an error instead? */
                        g_warning (_("No expected header found on lock file “%s”, was it modified?"),
                                   lock_file);
                }
        }

        locked_schema = schema_to_key (schema);
        full_key = g_strdup_printf ("/%s/%s", locked_schema, key);
        found = g_ptr_array_find_with_equal_func (locks_list, full_key, g_str_equal, &idx);

        if (found && add) {
                g_debug ("No need to add lock %s to %s, already present",
                         full_key, lock_file);
                return TRUE;
        } else if (!found && !add) {
                g_debug ("No need to remove lock %s to %s, not present",
                         full_key, lock_file);
                return TRUE;
        }

        if (add) {
                g_debug ("Adding key %s lock to %s", full_key, lock_file);
                g_ptr_array_add (locks_list, g_strdup (full_key));
        } else {
                g_debug ("Removing key %s lock from %s", full_key, lock_file);
                g_ptr_array_remove_index (locks_list, idx);
        }

        return write_array_to_file (locks_list, lock_file, error);
}

static gboolean
set_dconf_setting_lock (const char *db_name,
                        const char *lock_name,
                        const char *schema,
                        const char *key,
                        GError    **error)
{
        return set_dconf_setting_lock_full (db_name, lock_name, schema, key, TRUE, error);
}

static gboolean
unset_dconf_setting_lock (const char *db_name,
                          const char *lock_name,
                          const char *schema,
                          const char *key,
                          GError    **error)
{
        return set_dconf_setting_lock_full (db_name, lock_name, schema, key, FALSE, error);
}

static gboolean
remove_dconf_setting_lock (const char  *db_name,
                           const char  *lock_name,
                           GError     **error)
{
        g_autofree char *lock_path = NULL;
        g_autoptr (GFile) file = NULL;

        lock_path = get_dconf_db_lock_path (db_name, lock_name);
        file = g_file_new_for_path (lock_path);

        g_debug ("Removing setting lock file %s", lock_path);

        if (g_file_delete (file, NULL, error)) {
                /* Try to remove the parent dir, if empty */
                g_autoptr (GFile) parent_dir = g_file_get_parent (file);
                g_file_delete (parent_dir, NULL, NULL);
                return TRUE;
        }

        return FALSE;
}

static gboolean
update_dconf (GError **error)
{
        g_auto(GStrv) environ = NULL;
        const char* command[] = { "dconf", "update", NULL };
        gint exit_code;

        g_debug ("Updating dconf...");

        environ = g_get_environ ();
        environ = g_environ_setenv (environ, "G_DEBUG", "fatal-warnings", FALSE);

        if (!g_spawn_sync (NULL, (GStrv) command, environ, G_SPAWN_SEARCH_PATH,
                           NULL, NULL, NULL, NULL, &exit_code, error))
                return FALSE;

#if GLIB_CHECK_VERSION (2, 70, 0)
        return g_spawn_check_wait_status (exit_code, error);
#else
        return g_spawn_check_exit_status (exit_code, error);
#endif
}

static gboolean
write_system_setting_to_db (const char  *db_name,
                            const char  *schema,
                            const char  *key,
                            GVariant    *value,
                            GError     **error)
{
        return write_dconf_setting_to_key_file (db_name,
                                                GDM_CONFIG_DCONF_OVERRIDE_NAME,
                                                schema,
                                                key,
                                                value,
                                                error);
}

static gboolean
write_locked_setting_to_db (const char   *db_name,
                            const char   *schema,
                            const char   *key,
                            GVariant     *value,
                            GError      **error)
{
        if (!write_system_setting_to_db (db_name, schema, key, value, error)) {
                return FALSE;
        }

        if (!set_dconf_setting_lock_full (db_name, GDM_CONFIG_DCONF_LOCKS_NAME,
                                          schema, key, value != NULL, error)) {
                return FALSE;
        }

        if (!update_dconf (error))
                return FALSE;

        return TRUE;
}

static gboolean
write_locked_system_settings_to_db (const char   *schema,
                                    const char   *key,
                                    GVariant     *value,
                                    GError      **error)
{
        g_autofree char *db_name = NULL;

        if (value) {
                db_name = ensure_system_user_dconf_profile (error);
                if (!db_name)
                        return FALSE;
        } else {
                g_autoptr (GError) local_error = NULL;

                db_name = get_system_dconf_profile_db_name (DCONF_SYSTEM_DB_DEFAULT_NAME,
                                                            &local_error);

                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
                        return TRUE;
                } else if (local_error) {
                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }
        }

        return write_locked_setting_to_db (db_name, schema, key, value, error);
}

static gboolean
write_gdm_auth_setting_to_db (GdmAuthType   auth_type,
                              gboolean      value,
                              GError      **error)
{
        if (!ensure_gdm_config_dconf_profile (error))
                return FALSE;

        return write_system_setting_to_db (GDM_CONFIG_DCONF_DB_NAME,
                                           LOGIN_SCHEMA,
                                           auth_type_to_option_key (auth_type),
                                           g_variant_new_boolean (value),
                                           error);
}

static gboolean
set_gdm_auth_setting_lock (GdmAuthType   config_command,
                           GError      **error)
{
        return set_dconf_setting_lock (GDM_CONFIG_DCONF_DB_NAME,
                                       GDM_CONFIG_DCONF_LOCKS_NAME,
                                       LOGIN_SCHEMA,
                                       auth_type_to_option_key (config_command),
                                       error);
}

static gboolean
write_locked_gdm_settings_to_db (GdmAuthType   auth_type,
                                 gboolean      value,
                                 GError      **error)
{
        if (!write_gdm_auth_setting_to_db (auth_type, value, error))
                return FALSE;

        if (!set_gdm_auth_setting_lock (auth_type, error))
                return FALSE;

        if (!update_dconf (error))
                return FALSE;

        return TRUE;
}

static GPtrArray *
create_cmd_args (int argc, gchar **argv, GdmConfigCommand command)
{
        GPtrArray *args;
        const char *command_str;
        int i;

        args = g_ptr_array_new_full (argc, g_free);
        command_str = config_command_to_string (command);
        g_ptr_array_add (args, g_strdup_printf ("%s %s", g_get_prgname (), command_str));

        for (i = 2; i < argc; i++) {
                g_ptr_array_add (args, g_strdup (argv[i]));
        }

        return g_steal_pointer (&args);
}

static gboolean
have_pam_module (GdmAuthType auth_type)
{
        g_autofree char *pam_profile = NULL;

        pam_profile = g_strdup_printf(PAM_PROFILES_DIR "/gdm-%s",
                auth_type_to_string(auth_type));

        g_debug ("Checking for PAM profile “%s” existance", pam_profile);

        return g_file_test (pam_profile, G_FILE_TEST_IS_REGULAR);
}

static gboolean
handle_config_command_options (GdmConfigCommand          config_command,
                               GdmConfigCommandHandler  *command_handler,
                               GError                  **error)
{
        if (!command_handler->options_handler_func)
                return TRUE;

        return command_handler->options_handler_func (config_command, error);
}

static gboolean
handle_config_command (GdmConfigCommand          config_command,
                       GdmConfigCommandHandler  *command_handler,
                       GError                  **error)
{
        g_autoptr(GError) local_error = NULL;
        g_autofree char *output = NULL;
        GdmAuthType auth_type;

        auth_type = config_command_to_auth_type (config_command);
        g_assert (auth_type != AUTH_NONE);

        if (!have_pam_module (auth_type)) {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED,
                                     get_pam_module_missing_error (auth_type));
                return FALSE;
        }

        if (try_run_distro_hook (config_command, NULL, &output, &local_error)) {
                g_print ("%s", output);
                return handle_config_command_options (config_command, command_handler, error);
        }

        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                g_debug ("Hook completed, stopping execution: %s",
                         local_error->message);

                if (!handle_config_command_options (config_command, command_handler, error))
                        return FALSE;

                /* If the option handle didn't fail, we still need to propagate
                 * the cancellation, or we will continue with default handler */
                g_propagate_error (error, g_steal_pointer (&local_error));
                return FALSE;
        }

        if (check_distro_hook_is_required (config_command)) {
                g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                        _("Failed to run a required distro hook: "));
                return FALSE;
        }

        g_debug ("Failed to run optional distro hook: %s", local_error->message);
        return TRUE;
}

static gboolean
print_usage (int                   argc,
             char                 *argv[],
             GdmConfigCommand      config_command)
{
        g_autoptr(GOptionContext) ctx = NULL;
        g_autofree char *usage = NULL;

        ctx = g_option_context_new (_("COMMAND"));
        g_option_context_set_help_enabled (ctx, FALSE);

        g_option_context_parse (ctx, &argc, &argv, NULL);
        usage = g_strdup_printf (_("Commands:\n"
                "  help         Shows this information\n"
                "  password     Configure the password authentication\n"
                "  fingerprint  Configure the fingerprint authentication\n"
                "  smartcard    Configure the smartcard authentication\n"
                "  reset        Resets the default configuration\n"
                "  show         Shows the current configuration\n"
                "\n"
                "Use “%s COMMAND --help” to get help on each command.\n"),
                        g_get_prgname ());

        g_option_context_set_description (ctx, usage);

        usage = g_option_context_get_help (ctx, FALSE, NULL);
        if (config_command == COMMAND_HELP) {
                g_print ("%s", usage);
                return TRUE;
        }

        g_printerr ("%s", usage);
        return FALSE;
}

static gboolean
handle_command (int   argc,
                char *argv[])
{
        GdmConfigCommand config_command;
        GdmAuthType auth_type;
        g_autoptr(GOptionContext) option_cx = NULL;
        g_autoptr(GdmConfigCommandHandler) command_handler = NULL;
        g_autoptr(GOptionGroup) group = NULL;
        g_autoptr(GPtrArray) cmd_args = NULL;
        g_autoptr(GPtrArray) args_copy = NULL;
        g_autoptr(GError) error = NULL;
        g_autofree OptionData *options_data = NULL;
        const char *title;
        const char *group_title;

        config_command = parse_config_command (argv[1]);
        command_handler = config_command_get_handler (config_command);

        if (!command_handler) {
                return print_usage (argc, argv, config_command);
        }

        g_assert (command_handler);
        title = get_command_title (config_command);
        group_title = get_command_group_title (config_command);

        cmd_args = create_cmd_args (argc, argv, config_command);

        options_data = g_new0 (OptionData, 1);
        options_data->config_command = config_command;
        options_data->args = cmd_args;

        option_cx = g_option_context_new (NULL);
        g_option_context_set_help_enabled (option_cx, FALSE);
        g_option_context_set_summary (option_cx, title);

        group = g_option_group_new (config_command_to_string (config_command),
                                    group_title,
                                    N_("Command options"),
                                    g_steal_pointer (&options_data), g_free);
        g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);
        g_option_group_add_entries (group, generic_entries);
        config_command_handler_add_to_group (command_handler, group);
        g_option_context_add_group (option_cx, g_steal_pointer (&group));

        /* We need to make a further container copy here as the parsing
         * may remove some elements from the array, leading us to a leak
         */
        args_copy = g_ptr_array_copy (cmd_args, NULL, NULL);
        g_ptr_array_set_free_func (args_copy, NULL);
        argc = args_copy->len;
        argv = (char **) args_copy->pdata;

        if (!g_option_context_parse (option_cx, &argc, &argv, &error) ||
            opt_cmd_help || /* help requested */
            argc > 1 /* More than one parameter has not been parsed */) {
                g_autofree char *help = NULL;
                help = g_option_context_get_help (option_cx, FALSE, NULL);

                if (error && argc > 1) {
                        g_printerr ("%sError: %s\n", help, error->message);
                } else if (opt_cmd_help) {
                        g_print ("%s", help);
                        return TRUE;
                } else {
                        g_printerr ("%s", help);
                }

                return FALSE;
        }

        if (geteuid () != 0 && !g_getenv ("UNDER_JHBUILD")) {
                /* TRANSLATORS: You need to be root to use PROGRAM-NAME “command” command */
                g_critical (_("You need to be root to use %s “%s” command"),
                              g_get_prgname(), config_command_to_string (config_command));
                return FALSE;
        }

        if (opt_debug) {
                g_setenv ("G_MESSAGES_DEBUG", G_LOG_DOMAIN, FALSE);
        }

        auth_type = config_command_to_auth_type (config_command);
        if (auth_type != AUTH_NONE &&
            !handle_config_command (config_command, command_handler, &error)) {
                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        return TRUE;

                g_critical ("Failed to handle “%s” command: %s",
                            config_command_to_string (config_command),
                            error->message);
                return FALSE;
        }

        if (!command_handler->handler_func (config_command, &error)) {
                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        return FALSE;

                g_critical ("Failed to run “%s” command: %s",
                            config_command_to_string (config_command),
                            error->message);
                return FALSE;
        }

        if (opt_verbose && config_command != COMMAND_SHOW) {
                handle_show (COMMAND_SHOW, NULL);
        }

        return TRUE;
}

static gboolean
handle_toggle_command (GdmConfigCommand   config_command,
                       GError           **error)
{
        g_autoptr(GError) local_error = NULL;
        GdmAuthType auth_type = config_command_to_auth_type (config_command);
        gboolean enabled = (get_requested_action () == ACTION_ENABLED);

        g_assert (auth_type != AUTH_NONE);

        if (!write_locked_gdm_settings_to_db (auth_type, enabled, &local_error)) {
                g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                            _("Failed to set %s setting: "),
                                            config_command_to_string (config_command));
                return FALSE;
        }

        return TRUE;
}

static gboolean
handle_password (GdmConfigCommand   config_command,
                 GError           **error)
{
        return handle_toggle_command (config_command, error);
}

static gboolean
handle_fingerprint (GdmConfigCommand   config_command,
                    GError           **error)
{
        return handle_toggle_command (config_command, error);
}

static gboolean
toggle_smartcard_settings (const char  *key,
                           GVariant    *value,
                           GError     **error)
{
        g_autoptr(GError) local_error = NULL;

        /* While this is not strictly needed we also do this for the gdm
         * database so that we keep the values in sync for both profiles */
        if (!write_locked_setting_to_db (GDM_CONFIG_DCONF_DB_NAME,
                                         GSD_SC_SCHEMA,
                                         key,
                                         value ? g_variant_ref_sink (value) : NULL,
                                         &local_error)) {
                g_debug ("Failed to write setting to %s database: %s",
                         GDM_CONFIG_DCONF_DB_NAME,
                         local_error->message);
        }

        return write_locked_system_settings_to_db (GSD_SC_SCHEMA,
                                                   key,
                                                   value,
                                                   error);
}

static gboolean
handle_smartcard_options (GdmConfigCommand   config_command,
                          GError           **error)
{
        g_autoptr(GError) local_error = NULL;
        GVariant *value;

        if (!opt_removal_action)
                return TRUE;

        if (!set_distro_hook_config (config_command, GSD_SC_REMOVAL_ACTION_KEY,
                                     opt_removal_action, &local_error)) {
                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        return TRUE;

                if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }

                g_clear_error (&local_error);
        }

        if (g_str_equal (opt_removal_action, "user-defined")) {
                value = NULL;
        } else {
                value = g_variant_new_string (opt_removal_action);
        }

        return toggle_smartcard_settings (GSD_SC_REMOVAL_ACTION_KEY, value, error);
}

static gboolean
handle_smartcard (GdmConfigCommand   config_command,
                  GError           **error)
{
        g_autoptr(GError) local_error = NULL;
        GdmAuthAction action;

        action = get_requested_action ();

        if (action != ACTION_UNSET) {
                gboolean enabled = (action == ACTION_REQUIRED || action == ACTION_ENABLED);
                if (!write_locked_gdm_settings_to_db (AUTH_SMARTCARD, enabled, &local_error)) {
                        g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                                    _("Failed to set smartcard setting"));
                        return FALSE;
                }

                if (!write_locked_gdm_settings_to_db (AUTH_PASSWORD, action != ACTION_REQUIRED, &local_error)) {
                        g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                                    _("Failed to set password setting"));
                        return FALSE;
                }
        }

        if (!handle_smartcard_options (config_command, error))
                return FALSE;

        return TRUE;
}

static const char *
get_boolean_string (gboolean val)
{
        return val ? N_("Enabled") : N_("Disabled");
}

static const char *
get_gdm_setting_state (GdmAuthType auth_type) {
        g_autoptr(GSettings) login_settings = NULL;
        gboolean enabled;

        if (!have_pam_module (auth_type))
                return N_("Not supported");

        login_settings = g_settings_new (LOGIN_SCHEMA);
        enabled = g_settings_get_boolean (login_settings,
                                          auth_type_to_option_key (auth_type));

        if (enabled) {
                g_autofree char *output = NULL;

                output = get_distro_hook_config (auth_type, NULL, NULL);

                if (g_strcmp0 (output, "enabled") == 0) {
                        return get_boolean_string (TRUE);
                } else if (g_strcmp0 (output, "disabled") == 0) {
                        return get_boolean_string (FALSE);
                } else if (g_strcmp0 (output, "required") == 0) {
                        return N_("Required");
                }
        }

        return get_boolean_string (enabled);
}

static char *
get_smartcard_option (const char *option_key) {
        g_autoptr(GSettings) smartcard_settings = NULL;
        g_autofree char *option_value = NULL;

        option_value = get_distro_hook_config (AUTH_SMARTCARD, option_key, NULL);
        if (option_value && smartcard_option_is_valid (option_key, option_value)) {
                return g_steal_pointer (&option_value);
        }

        smartcard_settings = g_settings_new (GSD_SC_SCHEMA);

        if (g_settings_is_writable (smartcard_settings, option_key)) {
                g_autoptr(GVariant) defalut_value = NULL;

                defalut_value = g_settings_get_default_value (smartcard_settings,
                                                              option_key);
                return g_strdup_printf ("user-defined (default is %s)",
                                        g_variant_get_string (defalut_value, NULL));
        }

        return g_settings_get_string (smartcard_settings, option_key);
}

static gboolean
handle_show (GdmConfigCommand   config_command,
             GError           **error)
{
        g_autofree char *removal_setting = NULL;
        g_autoptr(GError) local_error = NULL;
        g_autofree char *default_dconf_profile = NULL;

        /* This is not completely correct when getting the smartcard removal
         * option, as that's a per-user / system setting until we don't add the
         * system db to the gdm ones, but we need to set it now otherwise the
         * gsettings backend won't give us proper results for auth parameters,
         * as the backend isn't really ever released on unref, however the
         * alternative may be just using DConfClient API directly or just
         * calling gsettings tool without DCONF_PROFILE set or doing a fork
         * here, but that's probably too much for this tool */
        default_dconf_profile = g_strdup (g_getenv ("DCONF_PROFILE"));
        g_setenv ("DCONF_PROFILE", GDM_DCONF_PROFILE, TRUE);

        removal_setting = get_smartcard_option (GSD_SC_REMOVAL_ACTION_KEY);

        g_print(_("GDM Authorization configuration\n"
                  "\n"
                  "  Password authentication: %s\n"
                  "  Fingerprint authentication: %s\n"
                  "  Smart Card authentication: %s\n"
                  "  Smart Card removal action: %s\n"),
                  get_gdm_setting_state (AUTH_PASSWORD),
                  get_gdm_setting_state (AUTH_FINGERPRINT),
                  get_gdm_setting_state (AUTH_SMARTCARD),
                  removal_setting
                );

        if (default_dconf_profile)
                g_setenv ("DCONF_PROFILE", default_dconf_profile, TRUE);

        return TRUE;
}

static gboolean
handle_reset (GdmConfigCommand   config_command,
              GError           **error)
{
        g_autoptr(GError) local_error = NULL;
        g_autoptr(GFile) profile_file = NULL;
        g_autofree char *system_db_name = NULL;
        int i;

        if (get_requested_action () != ACTION_REQUIRED) {
                const char *reply_Y = C_("Interactive question", "Y");
                const char *reply_y = C_("Interactive question", "y");
                const char *reply_N = C_("Interactive question", "N");
                const char *reply_n = C_("Interactive question", "n");
                char buffer[50];

                while (TRUE) {
                        g_print (C_("Interactive question", "Do you want to continue? [Y/n]? "));
                        if (!fgets(buffer, sizeof (buffer), stdin))
                                continue;

                        g_strstrip (buffer);

                        if (g_str_equal (buffer, reply_Y))
                                break;
                        if (g_str_equal (buffer, reply_y))
                                break;
                        if (g_str_equal (buffer, reply_N))
                                break;
                        if (g_str_equal (buffer, reply_n))
                                break;
                }

                if (!g_str_equal (buffer, reply_y) && !g_str_equal (buffer, reply_Y)) {
                        g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                     _("User cancelled the request"));
                        return FALSE;
                }
        }

        if (!try_run_distro_hook (COMMAND_RESET, NULL, NULL, &local_error)) {
                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        return TRUE;

                if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
                        g_debug ("Ignored reset distro hook: %s", local_error->message);
                        g_clear_error (&local_error);
                } else {
                        g_propagate_error (error, g_steal_pointer (&local_error));
                        return FALSE;
                }
        }

        for (i = 0; i < AUTH_NONE; i++) {
                const char *option_key;

                if (config_command_to_auth_type (i) == AUTH_NONE)
                        continue;

                option_key = auth_type_to_option_key (i);
                if (!write_system_setting_to_db (GDM_CONFIG_DCONF_DB_NAME,
                                                 LOGIN_SCHEMA,
                                                 option_key,
                                                 NULL,
                                                 &local_error)) {
                        g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                                    _("Failed to reset %s setting: "),
                                                    config_command_to_string (config_command));
                        return FALSE;
                }

                if (!unset_dconf_setting_lock (GDM_CONFIG_DCONF_DB_NAME,
                                               GDM_CONFIG_DCONF_LOCKS_NAME,
                                               LOGIN_SCHEMA, option_key, error))
                        return FALSE;
        }

        if (!toggle_smartcard_settings (GSD_SC_REMOVAL_ACTION_KEY, NULL, error))
                return FALSE;

        system_db_name = get_system_dconf_profile_db_name (DCONF_SYSTEM_DB_DEFAULT_NAME, NULL);
        if (g_strcmp0 (system_db_name, DCONF_SYSTEM_DB_DEFAULT_NAME) == 0) {
                g_autofree char *profile_file = NULL;
                g_autoptr(GPtrArray) profile_contents = NULL;

                profile_file = get_dconf_system_profile_file (error);
                profile_contents = get_system_dconf_profile_contents (NULL);

                if (profile_contents) {
                        g_autofree char *full_line = NULL;
                        guint index;

                        full_line = g_strconcat (DCONF_SYSTEM_DB_PREFIX, system_db_name, NULL);
                        while (g_ptr_array_find_with_equal_func (profile_contents, full_line,
                                                                 g_str_equal, &index)) {
                                g_debug ("Removing %s db from profile %s",
                                         system_db_name, profile_file);
                                g_ptr_array_remove_index (profile_contents, index);
                                write_array_to_file (profile_contents, profile_file, NULL);
                        }
                }
        }

        if (system_db_name) {
                if (!remove_dconf_setting_lock (system_db_name,
                                                GDM_CONFIG_DCONF_LOCKS_NAME,
                                                &local_error)) {
                        g_warning ("Failed to remove lock file for DB %s: %s",
                                   system_db_name,
                                   local_error->message);
                        g_clear_error (&local_error);
                }

                if (!remove_dconf_setting_key_file (system_db_name,
                                                    GDM_CONFIG_DCONF_OVERRIDE_NAME,
                                                    &local_error)) {
                        g_warning ("Failed to remove setting key file for DB %s: %s",
                                   system_db_name,
                                   local_error->message);
                        g_clear_error (&local_error);
                }
        }

        if (!remove_dconf_setting_lock (GDM_CONFIG_DCONF_DB_NAME,
                                        GDM_CONFIG_DCONF_LOCKS_NAME,
                                        &local_error)) {
                g_warning ("Failed to remove lock file for DB %s: %s",
                           GDM_CONFIG_DCONF_DB_NAME,
                           local_error->message);
                g_clear_error (&local_error);
        }

        if (!remove_dconf_setting_key_file (GDM_CONFIG_DCONF_DB_NAME,
                                            GDM_CONFIG_DCONF_OVERRIDE_NAME,
                                            &local_error)) {
                g_warning ("Failed to remove setting key file for DB %s: %s",
                           GDM_CONFIG_DCONF_DB_NAME,
                           local_error->message);
                g_clear_error (&local_error);
        }

        /* finally remove the profile/gdm */
        profile_file = g_file_new_for_path (GDM_CONFIG_DCONF_PROFILE);
        if (!g_file_delete (profile_file, NULL, &local_error)) {
                g_warning ("Failed to remove gdm-auth-config profile override: %s",
                           local_error->message);
                g_clear_error (&local_error);
        }

        if (!update_dconf (error))
                return FALSE;

        return TRUE;
}

int
main (int argc, char *argv[])
{
        g_autofree char *program_name = NULL;

        setlocale (LC_ALL, "");
        textdomain (GETTEXT_PACKAGE);

        program_name = g_path_get_basename (argv[0]);
        g_set_prgname (program_name);

        if (argc < 2) {
                print_usage (argc, argv, COMMAND_UNKNOWN);
                return EXIT_FAILURE;
        }

        if (!handle_command (argc, argv))
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}
