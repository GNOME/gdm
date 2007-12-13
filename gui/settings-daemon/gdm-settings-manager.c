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
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-settings-manager.h"
#include "gdm-settings-plugins-engine.h"

#define GDM_SETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SETTINGS_MANAGER, GdmSettingsManagerPrivate))

struct GdmSettingsManagerPrivate
{
        char *gconf_prefix;
};

enum {
        PROP_0,
        PROP_GCONF_PREFIX,
};

static void     gdm_settings_manager_class_init  (GdmSettingsManagerClass *klass);
static void     gdm_settings_manager_init        (GdmSettingsManager      *settings_manager);
static void     gdm_settings_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GdmSettingsManager, gdm_settings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

gboolean
gdm_settings_manager_start (GdmSettingsManager *manager,
                            GError            **error)
{
        gboolean ret;

        g_debug ("Starting settings manager");

        gdm_settings_plugins_engine_init (manager->priv->gconf_prefix);

        ret = TRUE;
        return ret;
}

void
gdm_settings_manager_stop (GdmSettingsManager *manager)
{
        g_debug ("Stopping settings manager");

        gdm_settings_plugins_engine_shutdown ();
}

static void
_set_gconf_prefix (GdmSettingsManager *self,
                   const char         *prefix)
{
        g_free (self->priv->gconf_prefix);
        self->priv->gconf_prefix = g_strdup (prefix);
}

static void
gdm_settings_manager_set_property (GObject        *object,
                                   guint           prop_id,
                                   const GValue   *value,
                                   GParamSpec     *pspec)
{
        GdmSettingsManager *self;

        self = GDM_SETTINGS_MANAGER (object);

        switch (prop_id) {
        case PROP_GCONF_PREFIX:
                _set_gconf_prefix (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_settings_manager_get_property (GObject        *object,
                                   guint           prop_id,
                                   GValue         *value,
                                   GParamSpec     *pspec)
{
        GdmSettingsManager *self;

        self = GDM_SETTINGS_MANAGER (object);

        switch (prop_id) {
        case PROP_GCONF_PREFIX:
                g_value_set_string (value, self->priv->gconf_prefix);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_settings_manager_constructor (GType                  type,
                                  guint                  n_construct_properties,
                                  GObjectConstructParam *construct_properties)
{
        GdmSettingsManager      *manager;
        GdmSettingsManagerClass *klass;

        klass = GDM_SETTINGS_MANAGER_CLASS (g_type_class_peek (GDM_TYPE_SETTINGS_MANAGER));

        manager = GDM_SETTINGS_MANAGER (G_OBJECT_CLASS (gdm_settings_manager_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (manager);
}

static void
gdm_settings_manager_dispose (GObject *object)
{
        GdmSettingsManager *manager;

        manager = GDM_SETTINGS_MANAGER (object);

        gdm_settings_manager_stop (manager);

        G_OBJECT_CLASS (gdm_settings_manager_parent_class)->dispose (object);
}

static void
gdm_settings_manager_class_init (GdmSettingsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_settings_manager_get_property;
        object_class->set_property = gdm_settings_manager_set_property;
        object_class->constructor = gdm_settings_manager_constructor;
        object_class->dispose = gdm_settings_manager_dispose;
        object_class->finalize = gdm_settings_manager_finalize;

        g_object_class_install_property (object_class,
                                         PROP_GCONF_PREFIX,
                                         g_param_spec_string ("gconf-prefix",
                                                              "gconf-prefix",
                                                              "gconf-prefix",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmSettingsManagerPrivate));
}

static void
gdm_settings_manager_init (GdmSettingsManager *manager)
{

        manager->priv = GDM_SETTINGS_MANAGER_GET_PRIVATE (manager);
}

static void
gdm_settings_manager_finalize (GObject *object)
{
        GdmSettingsManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SETTINGS_MANAGER (object));

        manager = GDM_SETTINGS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_free (manager->priv->gconf_prefix);

        G_OBJECT_CLASS (gdm_settings_manager_parent_class)->finalize (object);
}

GdmSettingsManager *
gdm_settings_manager_new (const char *gconf_prefix)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GDM_TYPE_SETTINGS_MANAGER,
                                               "gconf-prefix", gconf_prefix,
                                               NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GDM_SETTINGS_MANAGER (manager_object);
}
