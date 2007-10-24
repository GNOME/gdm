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

#include "gdm-session-manager.h"

#define GDM_SESSION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_MANAGER, GdmSessionManagerPrivate))

struct GdmSessionManagerPrivate
{
};

enum {
        PROP_0,
};

static void     gdm_session_manager_class_init  (GdmSessionManagerClass *klass);
static void     gdm_session_manager_init        (GdmSessionManager      *session_manager);
static void     gdm_session_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GdmSessionManager, gdm_session_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

gboolean
gdm_session_manager_start (GdmSessionManager *session,
                           GError           **error)
{
        gboolean res;

        g_return_val_if_fail (GDM_IS_SESSION_MANAGER (session), FALSE);

        res = TRUE;

        return res;
}

void
gdm_session_manager_stop (GdmSessionManager *session)
{
        g_return_if_fail (GDM_IS_SESSION_MANAGER (session));

}

static void
gdm_session_manager_set_property (GObject        *object,
                                  guint           prop_id,
                                  const GValue   *value,
                                  GParamSpec     *pspec)
{
        GdmSessionManager *self;

        self = GDM_SESSION_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_manager_get_property (GObject        *object,
                                  guint           prop_id,
                                  GValue         *value,
                                  GParamSpec     *pspec)
{
        GdmSessionManager *self;

        self = GDM_SESSION_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_session_manager_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        GdmSessionManager      *session_manager;
        GdmSessionManagerClass *klass;

        klass = GDM_SESSION_MANAGER_CLASS (g_type_class_peek (GDM_TYPE_SESSION_MANAGER));

        session_manager = GDM_SESSION_MANAGER (G_OBJECT_CLASS (gdm_session_manager_parent_class)->constructor (type,
                                                                                                                     n_construct_properties,
                                                                                                                     construct_properties));

        return G_OBJECT (session_manager);
}

static void
gdm_session_manager_dispose (GObject *object)
{
        GdmSessionManager *session_manager;

        session_manager = GDM_SESSION_MANAGER (object);

        G_OBJECT_CLASS (gdm_session_manager_parent_class)->dispose (object);
}

static void
gdm_session_manager_class_init (GdmSessionManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_manager_get_property;
        object_class->set_property = gdm_session_manager_set_property;
        object_class->constructor = gdm_session_manager_constructor;
        object_class->dispose = gdm_session_manager_dispose;
        object_class->finalize = gdm_session_manager_finalize;

        g_type_class_add_private (klass, sizeof (GdmSessionManagerPrivate));
}

static void
gdm_session_manager_init (GdmSessionManager *session)
{

        session->priv = GDM_SESSION_MANAGER_GET_PRIVATE (session);

}

static void
gdm_session_manager_finalize (GObject *object)
{
        GdmSessionManager *session_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_MANAGER (object));

        session_manager = GDM_SESSION_MANAGER (object);

        g_return_if_fail (session_manager->priv != NULL);

        G_OBJECT_CLASS (gdm_session_manager_parent_class)->finalize (object);
}

GdmSessionManager *
gdm_session_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GDM_TYPE_SESSION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GDM_SESSION_MANAGER (manager_object);
}
