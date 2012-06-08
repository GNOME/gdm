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
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-welcome-session.h"
#include "gdm-greeter-session.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

#define GDM_GREETER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define GDM_GREETER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

#define GDM_GREETER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SESSION, GdmGreeterSessionPrivate))

struct GdmGreeterSessionPrivate
{
        gpointer dummy;
};

static void     gdm_greeter_session_class_init    (GdmGreeterSessionClass *klass);
static void     gdm_greeter_session_init  (GdmGreeterSession      *greeter_session);

G_DEFINE_TYPE (GdmGreeterSession, gdm_greeter_session, GDM_TYPE_WELCOME_SESSION)

static void
gdm_greeter_session_class_init (GdmGreeterSessionClass *klass)
{
        g_type_class_add_private (klass, sizeof (GdmGreeterSessionPrivate));
}

static void
gdm_greeter_session_init (GdmGreeterSession *greeter_session)
{

        greeter_session->priv = GDM_GREETER_SESSION_GET_PRIVATE (greeter_session);
}

GdmGreeterSession *
gdm_greeter_session_new (const char *display_name,
                         const char *seat_id,
                         const char *display_device,
                         const char *display_hostname,
                         gboolean    display_is_local)
{
        GObject *object;
        gboolean debug = FALSE;
        char *command = BINDIR "/gnome-session -f";

        gdm_settings_direct_get_boolean (GDM_KEY_DEBUG, &debug);

        if (debug) {
                command = BINDIR "/gnome-session -f --debug";
        }

        object = g_object_new (GDM_TYPE_GREETER_SESSION,
                               "command", command,
                               "server-dbus-path", GDM_GREETER_SERVER_DBUS_PATH,
                               "server-dbus-interface", GDM_GREETER_SERVER_DBUS_INTERFACE,
                               "server-env-var-name", "GDM_GREETER_DBUS_ADDRESS",
                               "x11-display-name", display_name,
                               "x11-display-seat-id", seat_id,
                               "x11-display-device", display_device,
                               "x11-display-hostname", display_hostname,
                               "x11-display-is-local", display_is_local,
                               "runtime-dir", GDM_SCREENSHOT_DIR,
                               NULL);

        return GDM_GREETER_SESSION (object);
}
