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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display-factory.h"
#include "gdm-display-store.h"
#include "gdm-dynamic-user-store.h"

typedef struct _GdmDisplayFactoryPrivate
{
        GdmDisplayStore     *display_store;
        GdmDynamicUserStore *dyn_user_store;
        guint                purge_displays_id;
} GdmDisplayFactoryPrivate;

enum {
        PROP_0,
        PROP_DISPLAY_STORE,
        PROP_DYN_USER_STORE,
};

static void     gdm_display_factory_class_init  (GdmDisplayFactoryClass *klass);
static void     gdm_display_factory_init        (GdmDisplayFactory      *factory);
static void     gdm_display_factory_finalize    (GObject                *object);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GdmDisplayFactory, gdm_display_factory, G_TYPE_OBJECT)

GQuark
gdm_display_factory_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_display_factory_error");
        }

        return ret;
}

static gboolean
purge_display (char       *id,
               GdmDisplay *display,
               gpointer    user_data)
{
        int status;

        status = gdm_display_get_status (display);

        switch (status) {
        case GDM_DISPLAY_FINISHED:
        case GDM_DISPLAY_FAILED:
                return TRUE;
        default:
                return FALSE;
        }
}

static gboolean
purge_displays (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);
        priv->purge_displays_id = 0;
        gdm_display_store_foreach_remove (priv->display_store,
                                          (GdmDisplayStoreFunc)purge_display,
                                          NULL);

        return G_SOURCE_REMOVE;
}

void
gdm_display_factory_queue_purge_displays (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        g_return_if_fail (GDM_IS_DISPLAY_FACTORY (factory));

        priv = gdm_display_factory_get_instance_private (factory);
        if (priv->purge_displays_id == 0) {
                priv->purge_displays_id = g_idle_add ((GSourceFunc) purge_displays, factory);
        }
}

GdmDisplayStore *
gdm_display_factory_get_display_store (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), NULL);

        priv = gdm_display_factory_get_instance_private (factory);
        return priv->display_store;
}

GdmDynamicUserStore *
gdm_display_factory_get_dyn_user_store (GdmDisplayFactory *factory)
{
        GdmDisplayFactoryPrivate *priv;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), NULL);

        priv = gdm_display_factory_get_instance_private (factory);
        return priv->dyn_user_store;
}

gboolean
gdm_display_factory_start (GdmDisplayFactory *factory)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        g_object_ref (factory);
        ret = GDM_DISPLAY_FACTORY_GET_CLASS (factory)->start (factory);
        g_object_unref (factory);

        return ret;
}

gboolean
gdm_display_factory_stop (GdmDisplayFactory *factory)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_DISPLAY_FACTORY (factory), FALSE);

        g_object_ref (factory);
        ret = GDM_DISPLAY_FACTORY_GET_CLASS (factory)->stop (factory);
        g_object_unref (factory);

        return ret;
}

static void
gdm_display_factory_set_display_store (GdmDisplayFactory *factory,
                                       GdmDisplayStore   *display_store)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);
        g_clear_object (&priv->display_store);

        if (display_store != NULL) {
                priv->display_store = g_object_ref (display_store);
        }
}

static void
gdm_display_factory_set_dyn_user_store (GdmDisplayFactory   *factory,
                                        GdmDynamicUserStore *dyn_user_store)
{
        GdmDisplayFactoryPrivate *priv;

        priv = gdm_display_factory_get_instance_private (factory);
        g_clear_object (&priv->dyn_user_store);

        if (dyn_user_store != NULL) {
                priv->dyn_user_store = g_object_ref (dyn_user_store);
        }
}

static void
gdm_display_factory_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmDisplayFactory *self;

        self = GDM_DISPLAY_FACTORY (object);

        switch (prop_id) {
        case PROP_DISPLAY_STORE:
                gdm_display_factory_set_display_store (self, g_value_get_object (value));
                break;
        case PROP_DYN_USER_STORE:
                gdm_display_factory_set_dyn_user_store (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_display_factory_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        GdmDisplayFactory *self;
        GdmDisplayFactoryPrivate *priv;

        self = GDM_DISPLAY_FACTORY (object);
        priv = gdm_display_factory_get_instance_private (self);

        switch (prop_id) {
        case PROP_DISPLAY_STORE:
                g_value_set_object (value, priv->display_store);
                break;
        case PROP_DYN_USER_STORE:
                g_value_set_object (value, priv->dyn_user_store);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_display_factory_class_init (GdmDisplayFactoryClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_display_factory_get_property;
        object_class->set_property = gdm_display_factory_set_property;
        object_class->finalize = gdm_display_factory_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_STORE,
                                         g_param_spec_object ("display-store",
                                                              "display store",
                                                              "display store",
                                                              GDM_TYPE_DISPLAY_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DYN_USER_STORE,
                                         g_param_spec_object ("dyn-user-store",
                                                              "dyn user store",
                                                              "dyn user store",
                                                              GDM_TYPE_DYNAMIC_USER_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gdm_display_factory_init (GdmDisplayFactory *factory)
{
}

static void
gdm_display_factory_finalize (GObject *object)
{
        GdmDisplayFactory *factory;
        GdmDisplayFactoryPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DISPLAY_FACTORY (object));

        factory = GDM_DISPLAY_FACTORY (object);
        priv = gdm_display_factory_get_instance_private (factory);

        g_return_if_fail (priv != NULL);

        g_clear_handle_id (&priv->purge_displays_id, g_source_remove);

        // TODO: Do I need to clear the dyn_user_store, or does it happen automatically?
        // TODO: Also apply whatever we decide here to gdm-launch-env.c

        G_OBJECT_CLASS (gdm_display_factory_parent_class)->finalize (object);
}
