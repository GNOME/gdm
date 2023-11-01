/* gdm-session-settings.c - Loads session and language from ~/.dmrc
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */
#include "config.h"
#include "gdm-session-settings.h"

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <act/act-user-manager.h>

struct _GdmSessionSettings
{
        GObject parent;
        ActUserManager *user_manager;
        ActUser *user;
        char *session_name;
        char *session_type;
        char *language_name;
};

static void gdm_session_settings_finalize (GObject *object);
static void gdm_session_settings_class_install_properties (GdmSessionSettingsClass *
                                              settings_class);

static void gdm_session_settings_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec);
static void gdm_session_settings_get_property (GObject      *object,
                                              guint         prop_id,
                                              GValue       *value,
                                              GParamSpec   *pspec);

enum {
        PROP_0 = 0,
        PROP_SESSION_NAME,
        PROP_SESSION_TYPE,
        PROP_LANGUAGE_NAME,
        PROP_IS_LOADED
};

G_DEFINE_TYPE (GdmSessionSettings, gdm_session_settings, G_TYPE_OBJECT)

static void
gdm_session_settings_class_init (GdmSessionSettingsClass *settings_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (settings_class);

        object_class->finalize = gdm_session_settings_finalize;

        gdm_session_settings_class_install_properties (settings_class);
}

static void
gdm_session_settings_class_install_properties (GdmSessionSettingsClass *settings_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (settings_class);
        object_class->set_property = gdm_session_settings_set_property;
        object_class->get_property = gdm_session_settings_get_property;

        param_spec = g_param_spec_string ("session-name", "Session Name",
                                          "The name of the session",
                                          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
        g_object_class_install_property (object_class, PROP_SESSION_NAME, param_spec);

        param_spec = g_param_spec_string ("session-type", "Session Type",
                                          "The type of the session",
                                          NULL, G_PARAM_READWRITE);
        g_object_class_install_property (object_class, PROP_SESSION_TYPE, param_spec);

        param_spec = g_param_spec_string ("language-name", "Language Name",
                                          "The name of the language",
                                          NULL,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
        g_object_class_install_property (object_class, PROP_LANGUAGE_NAME, param_spec);

        param_spec = g_param_spec_boolean ("is-loaded", NULL, NULL,
                                           FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
        g_object_class_install_property (object_class, PROP_IS_LOADED, param_spec);
}

static void
gdm_session_settings_init (GdmSessionSettings *settings)
{
        settings->user_manager = act_user_manager_get_default ();
}

static void
gdm_session_settings_finalize (GObject *object)
{
        GdmSessionSettings *settings;
        GObjectClass *parent_class;

        settings = GDM_SESSION_SETTINGS (object);

        if (settings->user != NULL) {
                g_object_unref (settings->user);
        }

        g_free (settings->session_name);
        g_free (settings->language_name);

        parent_class = G_OBJECT_CLASS (gdm_session_settings_parent_class);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

void
gdm_session_settings_set_language_name (GdmSessionSettings *settings,
                                        const char         *language_name)
{
        g_return_if_fail (GDM_IS_SESSION_SETTINGS (settings));

        if (settings->language_name == NULL ||
            strcmp (settings->language_name, language_name) != 0) {
                settings->language_name = g_strdup (language_name);
                g_object_notify (G_OBJECT (settings), "language-name");
        }
}

void
gdm_session_settings_set_session_name (GdmSessionSettings *settings,
                                       const char         *session_name)
{
        g_return_if_fail (GDM_IS_SESSION_SETTINGS (settings));

        if (settings->session_name == NULL ||
            strcmp (settings->session_name, session_name) != 0) {
                settings->session_name = g_strdup (session_name);
                g_object_notify (G_OBJECT (settings), "session-name");
        }
}

void
gdm_session_settings_set_session_type (GdmSessionSettings *settings,
                                       const char         *session_type)
{
        g_return_if_fail (GDM_IS_SESSION_SETTINGS (settings));

        if (settings->session_type == NULL ||
            g_strcmp0 (settings->session_type, session_type) != 0) {
                settings->session_type = g_strdup (session_type);
                g_object_notify (G_OBJECT (settings), "session-type");
        }
}

char *
gdm_session_settings_get_language_name (GdmSessionSettings *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), NULL);
        return g_strdup (settings->language_name);
}

char *
gdm_session_settings_get_session_name (GdmSessionSettings *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), NULL);
        return g_strdup (settings->session_name);
}

char *
gdm_session_settings_get_session_type (GdmSessionSettings *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), NULL);
        return g_strdup (settings->session_type);
}

static void
gdm_session_settings_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmSessionSettings *settings;

        settings = GDM_SESSION_SETTINGS (object);

        switch (prop_id) {
                case PROP_LANGUAGE_NAME:
                        gdm_session_settings_set_language_name (settings, g_value_get_string (value));
                break;

                case PROP_SESSION_NAME:
                        gdm_session_settings_set_session_name (settings, g_value_get_string (value));
                break;

                case PROP_SESSION_TYPE:
                        gdm_session_settings_set_session_type (settings, g_value_get_string (value));
                break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gdm_session_settings_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmSessionSettings *settings;

        settings = GDM_SESSION_SETTINGS (object);

        switch (prop_id) {
                case PROP_SESSION_NAME:
                        g_value_set_string (value, settings->session_name);
                break;

                case PROP_SESSION_TYPE:
                        g_value_set_string (value, settings->session_type);
                break;

                case PROP_LANGUAGE_NAME:
                        g_value_set_string (value, settings->language_name);
                break;

                case PROP_IS_LOADED:
                        g_value_set_boolean (value, gdm_session_settings_is_loaded (settings));
                break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

GdmSessionSettings *
gdm_session_settings_new (void)
{
        GdmSessionSettings *settings;

        settings = g_object_new (GDM_TYPE_SESSION_SETTINGS,
                                 NULL);

        return settings;
}

gboolean
gdm_session_settings_is_loaded (GdmSessionSettings  *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), FALSE);

        if (settings->user == NULL) {
                return FALSE;
        }

        return act_user_is_loaded (settings->user);
}

static void
load_settings_from_user (GdmSessionSettings *settings)
{
        const char *session_name;
        const char *session_type;
        const char *language_name;

        if (!act_user_is_loaded (settings->user)) {
                g_warning ("GdmSessionSettings: trying to load user settings from unloaded user");
                return;
        }

        /* Load settings even if the user doesn't have saved state, as they could have been
         * configured in AccountsService by the administrator */
        session_type = act_user_get_session_type (settings->user);
        session_name = act_user_get_session (settings->user);

        g_debug ("GdmSessionSettings: saved session is %s (type %s)", session_name, session_type);

        if (session_type != NULL && session_type[0] != '\0') {
                gdm_session_settings_set_session_type (settings, session_type);
        }

        if (session_name != NULL && session_name[0] != '\0') {
                gdm_session_settings_set_session_name (settings, session_name);
        }

        language_name = act_user_get_language (settings->user);

        g_debug ("GdmSessionSettings: saved language is %s", language_name);
        if (language_name != NULL && language_name[0] != '\0') {
                gdm_session_settings_set_language_name (settings, language_name);
        }

        g_object_notify (G_OBJECT (settings), "is-loaded");
}

static void
on_user_is_loaded_changed (ActUser            *user,
                           GParamSpec         *pspec,
                           GdmSessionSettings *settings)
{
        if (act_user_is_loaded (settings->user)) {
                load_settings_from_user (settings);
                g_signal_handlers_disconnect_by_func (G_OBJECT (settings->user),
                                                      G_CALLBACK (on_user_is_loaded_changed),
                                                      settings);
        }
}

gboolean
gdm_session_settings_load (GdmSessionSettings  *settings,
                           const char          *username)
{
        g_autoptr(ActUser) old_user = NULL;

        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), FALSE);
        g_return_val_if_fail (username != NULL, FALSE);
        g_return_val_if_fail (!gdm_session_settings_is_loaded (settings), FALSE);

        old_user = g_steal_pointer (&settings->user);
        if (old_user != NULL) {
                g_signal_handlers_disconnect_by_func (G_OBJECT (old_user),
                                                      G_CALLBACK (on_user_is_loaded_changed),
                                                      settings);
        }

        settings->user = act_user_manager_get_user (settings->user_manager,
                                                          username);

        if (!act_user_is_loaded (settings->user)) {
                g_signal_connect (settings->user,
                                  "notify::is-loaded",
                                  G_CALLBACK (on_user_is_loaded_changed),
                                  settings);
                return FALSE;
        }

        load_settings_from_user (settings);

        return TRUE;
}

gboolean
gdm_session_settings_save (GdmSessionSettings  *settings,
                           const char          *username)
{
        g_autoptr(ActUser) user = NULL;

        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), FALSE);
        g_return_val_if_fail (username != NULL, FALSE);
        g_return_val_if_fail (gdm_session_settings_is_loaded (settings), FALSE);

        user = act_user_manager_get_user (settings->user_manager,
                                          username);


        if (!act_user_is_loaded (user)) {
                return FALSE;
        }

        if (settings->session_name != NULL) {
                act_user_set_session (user, settings->session_name);
        }

        if (settings->session_type != NULL) {
                act_user_set_session_type (user, settings->session_type);
        }

        if (settings->language_name != NULL) {
                act_user_set_language (user, settings->language_name);
        }

        if (!act_user_is_local_account (user)) {
                g_autoptr (GError) error = NULL;

                act_user_manager_cache_user (settings->user_manager, username, &error);

                if (error != NULL) {
                        g_debug ("GdmSessionSettings: Could not locally cache remote user: %s", error->message);
                        return FALSE;
                }

        }

        return TRUE;
}
