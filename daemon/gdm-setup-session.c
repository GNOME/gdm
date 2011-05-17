/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 * Author: Matthias Clasen
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
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-welcome-session.h"
#include "gdm-setup-session.h"

#define GDM_GREETER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define GDM_GREETER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

#define GDM_SETUP_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SETUP_SESSION, GdmSetupSessionPrivate))

struct GdmSetupSessionPrivate
{
        gpointer dummy;
};

enum {
        PROP_0,
};

static void     gdm_setup_session_class_init (GdmSetupSessionClass *klass);
static void     gdm_setup_session_init       (GdmSetupSession      *setup_session);
static void     gdm_setup_session_finalize   (GObject              *object);

G_DEFINE_TYPE (GdmSetupSession, gdm_setup_session, GDM_TYPE_WELCOME_SESSION)

static void
gdm_setup_session_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_setup_session_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_setup_session_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmSetupSession      *setup_session;

        setup_session = GDM_SETUP_SESSION (G_OBJECT_CLASS (gdm_setup_session_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (setup_session);
}

static void
gdm_setup_session_class_init (GdmSetupSessionClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_setup_session_get_property;
        object_class->set_property = gdm_setup_session_set_property;
        object_class->constructor = gdm_setup_session_constructor;
        object_class->finalize = gdm_setup_session_finalize;

        g_type_class_add_private (klass, sizeof (GdmSetupSessionPrivate));
}

static void
gdm_setup_session_init (GdmSetupSession *setup_session)
{
        setup_session->priv = GDM_SETUP_SESSION_GET_PRIVATE (setup_session);
}

static void
gdm_setup_session_finalize (GObject *object)
{
        GdmSetupSession *setup_session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SETUP_SESSION (object));

        setup_session = GDM_SETUP_SESSION (object);

        g_return_if_fail (setup_session->priv != NULL);

        G_OBJECT_CLASS (gdm_setup_session_parent_class)->finalize (object);
}

GdmSetupSession *
gdm_setup_session_new (const char *display_name,
                       const char *seat_id,
                       const char *display_device,
                       const char *display_hostname,
                       gboolean    display_is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SETUP_SESSION,
                               "command", BINDIR "/gnome-session --session gdm",
                               "server-dbus-path", GDM_GREETER_SERVER_DBUS_PATH,
                               "server-dbus-interface", GDM_GREETER_SERVER_DBUS_INTERFACE,
                               "server-env-var-name", "GDM_GREETER_DBUS_ADDRESS",
                               "register-ck-session", TRUE,
                               "x11-display-name", display_name,
                               "x11-display-seat-id", seat_id,
                               "x11-display-device", display_device,
                               "x11-display-hostname", display_hostname,
                               "x11-display-is-local", display_is_local,
                               "runtime-dir", GDM_SCREENSHOT_DIR,
                               NULL);

        return GDM_SETUP_SESSION (object);
}
