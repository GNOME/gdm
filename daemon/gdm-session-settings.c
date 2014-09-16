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

struct _GdmSessionSettingsPrivate
{
        ActUserManager *user_manager;
        ActUser *user;
        char *session_name;
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

        g_type_class_add_private (settings_class, sizeof (GdmSessionSettingsPrivate));
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
                                        NULL, G_PARAM_READWRITE);
        g_object_class_install_property (object_class, PROP_SESSION_NAME, param_spec);

        param_spec = g_param_spec_string ("language-name", "Language Name",
                                        "The name of the language",
                                        NULL,
                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_LANGUAGE_NAME, param_spec);

        param_spec = g_param_spec_boolean ("is-loaded", NULL, NULL,
                                           FALSE, G_PARAM_READABLE);
        g_object_class_install_property (object_class, PROP_IS_LOADED, param_spec);
}

static void
gdm_session_settings_init (GdmSessionSettings *settings)
{
        settings->priv = G_TYPE_INSTANCE_GET_PRIVATE (settings,
                                                     GDM_TYPE_SESSION_SETTINGS,
                                                     GdmSessionSettingsPrivate);

        settings->priv->user_manager = act_user_manager_get_default ();

}

static void
gdm_session_settings_finalize (GObject *object)
{
        GdmSessionSettings *settings;
        GObjectClass *parent_class;

        settings = GDM_SESSION_SETTINGS (object);

        if (settings->priv->user != NULL) {
                g_object_unref (settings->priv->user);
        }

        g_free (settings->priv->session_name);
        g_free (settings->priv->language_name);

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

        if (settings->priv->language_name == NULL ||
            strcmp (settings->priv->language_name, language_name) != 0) {
                settings->priv->language_name = g_strdup (language_name);
                g_object_notify (G_OBJECT (settings), "language-name");
        }
}

void
gdm_session_settings_set_session_name (GdmSessionSettings *settings,
                                       const char         *session_name)
{
        g_return_if_fail (GDM_IS_SESSION_SETTINGS (settings));

        if (settings->priv->session_name == NULL ||
            strcmp (settings->priv->session_name, session_name) != 0) {
                settings->priv->session_name = g_strdup (session_name);
                g_object_notify (G_OBJECT (settings), "session-name");
        }
}

char *
gdm_session_settings_get_language_name (GdmSessionSettings *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), NULL);
        return g_strdup (settings->priv->language_name);
}

char *
gdm_session_settings_get_session_name (GdmSessionSettings *settings)
{
        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), NULL);
        return g_strdup (settings->priv->session_name);
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
                        g_value_set_string (value, settings->priv->session_name);
                break;

                case PROP_LANGUAGE_NAME:
                        g_value_set_string (value, settings->priv->language_name);
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
        if (settings->priv->user == NULL) {
                return FALSE;
        }

        return act_user_is_loaded (settings->priv->user);
}

static void
load_settings_from_user (GdmSessionSettings *settings)
{
        const char *session_name;
        const char *language_name;

        if (!act_user_is_loaded (settings->priv->user)) {
                g_warning ("GdmSessionSettings: trying to load user settings from unloaded user");
                return;
        }

        session_name = act_user_get_x_session (settings->priv->user);
        g_debug ("GdmSessionSettings: saved session is %s", session_name);

        if (session_name != NULL) {
                gdm_session_settings_set_session_name (settings, session_name);
        }

        language_name = act_user_get_language (settings->priv->user);

        g_debug ("GdmSessionSettings: saved language is %s", language_name);
        if (language_name != NULL) {
                gdm_session_settings_set_language_name (settings, language_name);
        }

        g_object_notify (G_OBJECT (settings), "is-loaded");
}

static void
on_user_is_loaded_changed (ActUser            *user,
                           GParamSpec         *pspec,
                           GdmSessionSettings *settings)
{
        if (act_user_is_loaded (settings->priv->user)) {
                load_settings_from_user (settings);
                g_signal_handlers_disconnect_by_func (G_OBJECT (settings->priv->user),
                                                      G_CALLBACK (on_user_is_loaded_changed),
                                                      settings);
        }
}

gboolean
gdm_session_settings_load (GdmSessionSettings  *settings,
                           const char          *username)
{
        ActUser *old_user;

        g_return_val_if_fail (settings != NULL, FALSE);
        g_return_val_if_fail (username != NULL, FALSE);
        g_return_val_if_fail (!gdm_session_settings_is_loaded (settings), FALSE);

        if (settings->priv->user != NULL) {
                old_user = settings->priv->user;

                g_signal_handlers_disconnect_by_func (G_OBJECT (settings->priv->user),
                                                      G_CALLBACK (on_user_is_loaded_changed),
                                                      settings);
        } else {
                old_user = NULL;
        }

        settings->priv->user = act_user_manager_get_user (settings->priv->user_manager,
                                                          username);

        g_clear_object (&old_user);

        if (!act_user_is_loaded (settings->priv->user)) {
                g_signal_connect (settings->priv->user,
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
        ActUser  *user;

        g_return_val_if_fail (GDM_IS_SESSION_SETTINGS (settings), FALSE);
        g_return_val_if_fail (username != NULL, FALSE);
        g_return_val_if_fail (gdm_session_settings_is_loaded (settings), FALSE);

        user = act_user_manager_get_user (settings->priv->user_manager,
                                          username);


        if (!act_user_is_loaded (user)) {
                g_object_unref (user);
                return FALSE;
        }

        if (settings->priv->session_name != NULL) {
                act_user_set_x_session (user, settings->priv->session_name);
        }

        if (settings->priv->language_name != NULL) {
                act_user_set_language (user, settings->priv->language_name);
        }
        g_object_unref (user);

        return TRUE;
}
