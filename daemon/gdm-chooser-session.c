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

#include "gdm-common.h"

#include "gdm-session.h"
#include "gdm-welcome-session.h"
#include "gdm-chooser-session.h"

#define GDM_CHOOSER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/ChooserServer"
#define GDM_CHOOSER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.ChooserServer"

#define GDM_CHOOSER_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_CHOOSER_SESSION, GdmChooserSessionPrivate))

struct GdmChooserSessionPrivate
{
        gpointer dummy;
};

static void     gdm_chooser_session_class_init    (GdmChooserSessionClass *klass);
static void     gdm_chooser_session_init          (GdmChooserSession      *chooser_session);

G_DEFINE_TYPE (GdmChooserSession, gdm_chooser_session, GDM_TYPE_WELCOME_SESSION)

static void
gdm_chooser_session_class_init (GdmChooserSessionClass *klass)
{
        g_type_class_add_private (klass, sizeof (GdmChooserSessionPrivate));
}

static void
gdm_chooser_session_init (GdmChooserSession *chooser_session)
{
        chooser_session->priv = GDM_CHOOSER_SESSION_GET_PRIVATE (chooser_session);
}

GdmChooserSession *
gdm_chooser_session_new (const char *display_name,
                         const char *display_device,
                         const char *display_hostname)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_CHOOSER_SESSION,
                               "command", LIBEXECDIR "/gdm-simple-chooser",
                               "verification-mode", GDM_SESSION_VERIFICATION_MODE_CHOOSER,
                               "x11-display-name", display_name,
                               "x11-display-device", display_device,
                               "x11-display-hostname", display_hostname,
                               NULL);

        return GDM_CHOOSER_SESSION (object);
}
