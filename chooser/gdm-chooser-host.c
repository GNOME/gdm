/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-address.h"
#include "gdm-chooser-host.h"

struct _GdmChooserHost
{
        GObject            parent;

        GdmAddress        *address;
        char              *description;
        GdmChooserHostKind kind;
        gboolean           willing;
};

enum {
        PROP_0,
        PROP_ADDRESS,
        PROP_DESCRIPTION,
        PROP_KIND,
        PROP_WILLING,
};

static void     gdm_chooser_host_class_init  (GdmChooserHostClass *klass);
static void     gdm_chooser_host_init        (GdmChooserHost      *chooser_host);
static void     gdm_chooser_host_finalize    (GObject             *object);

G_DEFINE_TYPE (GdmChooserHost, gdm_chooser_host, G_TYPE_OBJECT)

GdmAddress *
gdm_chooser_host_get_address (GdmChooserHost *host)
{
        g_return_val_if_fail (GDM_IS_CHOOSER_HOST (host), NULL);

        return host->address;
}

const char *
gdm_chooser_host_get_description (GdmChooserHost *host)
{
        g_return_val_if_fail (GDM_IS_CHOOSER_HOST (host), NULL);

        return host->description;
}

GdmChooserHostKind
gdm_chooser_host_get_kind (GdmChooserHost *host)
{
        g_return_val_if_fail (GDM_IS_CHOOSER_HOST (host), 0);

        return host->kind;
}

gboolean
gdm_chooser_host_get_willing (GdmChooserHost *host)
{
        g_return_val_if_fail (GDM_IS_CHOOSER_HOST (host), FALSE);

        return host->willing;
}

static void
_gdm_chooser_host_set_address (GdmChooserHost *host,
                               GdmAddress     *address)
{
        if (host->address != NULL) {
                gdm_address_free (host->address);
        }

        g_assert (address != NULL);

        gdm_address_debug (address);
        host->address = gdm_address_copy (address);
}

static void
_gdm_chooser_host_set_description (GdmChooserHost *host,
                                   const char     *description)
{
        g_free (host->description);
        host->description = g_strdup (description);
}

static void
_gdm_chooser_host_set_kind (GdmChooserHost *host,
                            int             kind)
{
        if (host->kind != kind) {
                host->kind = kind;
        }
}

static void
_gdm_chooser_host_set_willing (GdmChooserHost *host,
                               gboolean        willing)
{
        if (host->willing != willing) {
                host->willing = willing;
        }
}

static void
gdm_chooser_host_set_property (GObject      *object,
                               guint         param_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GdmChooserHost *host;

        host = GDM_CHOOSER_HOST (object);

        switch (param_id) {
        case PROP_ADDRESS:
                _gdm_chooser_host_set_address (host, g_value_get_boxed (value));
                break;
        case PROP_DESCRIPTION:
                _gdm_chooser_host_set_description (host, g_value_get_string (value));
                break;
        case PROP_KIND:
                _gdm_chooser_host_set_kind (host, g_value_get_int (value));
                break;
        case PROP_WILLING:
                _gdm_chooser_host_set_willing (host, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_chooser_host_get_property (GObject    *object,
                               guint       param_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
        GdmChooserHost *host;

        host = GDM_CHOOSER_HOST (object);

        switch (param_id) {
        case PROP_ADDRESS:
                g_value_set_boxed (value, host->address);
                break;
        case PROP_DESCRIPTION:
                g_value_set_string (value, host->description);
                break;
        case PROP_KIND:
                g_value_set_int (value, host->kind);
                break;
        case PROP_WILLING:
                g_value_set_boolean (value, host->willing);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
gdm_chooser_host_class_init (GdmChooserHostClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gdm_chooser_host_set_property;
        object_class->get_property = gdm_chooser_host_get_property;
        object_class->finalize = gdm_chooser_host_finalize;


        g_object_class_install_property (object_class,
                                         PROP_ADDRESS,
                                         g_param_spec_boxed ("address",
                                                             "address",
                                                             "address",
                                                             GDM_TYPE_ADDRESS,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_DESCRIPTION,
                                         g_param_spec_string ("description",
                                                              "description",
                                                              "description",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (object_class,
                                         PROP_KIND,
                                         g_param_spec_int ("kind",
                                                           "kind",
                                                           "kind",
                                                           0,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (object_class,
                                         PROP_WILLING,
                                         g_param_spec_boolean ("willing",
                                                               "willing",
                                                               "willing",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gdm_chooser_host_init (GdmChooserHost *widget)
{
}

static void
gdm_chooser_host_finalize (GObject *object)
{
        GdmChooserHost *host;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_CHOOSER_HOST (object));

        host = GDM_CHOOSER_HOST (object);

        g_free (host->description);
        gdm_address_free (host->address);

        G_OBJECT_CLASS (gdm_chooser_host_parent_class)->finalize (object);
}
