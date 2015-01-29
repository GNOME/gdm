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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"

#include "gdm-xdmcp-chooser-slave.h"

#include "gdm-server.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"
#include "gdm-session.h"

#define GDM_XDMCP_CHOOSER_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_XDMCP_CHOOSER_SLAVE, GdmXdmcpChooserSlavePrivate))

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS  10
#define DEFAULT_PING_INTERVAL 15

struct GdmXdmcpChooserSlavePrivate
{
        char              *id;
        GPid               pid;

        int                ping_interval;

        guint              connection_attempts;
};

enum {
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_xdmcp_chooser_slave_class_init     (GdmXdmcpChooserSlaveClass *klass);
static void     gdm_xdmcp_chooser_slave_init           (GdmXdmcpChooserSlave      *xdmcp_chooser_slave);
static void     gdm_xdmcp_chooser_slave_finalize       (GObject                   *object);

G_DEFINE_TYPE (GdmXdmcpChooserSlave, gdm_xdmcp_chooser_slave, GDM_TYPE_SLAVE)

static void
gdm_xdmcp_chooser_slave_class_init (GdmXdmcpChooserSlaveClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_xdmcp_chooser_slave_finalize;

        g_type_class_add_private (klass, sizeof (GdmXdmcpChooserSlavePrivate));
}

static void
gdm_xdmcp_chooser_slave_init (GdmXdmcpChooserSlave *slave)
{
        slave->priv = GDM_XDMCP_CHOOSER_SLAVE_GET_PRIVATE (slave);
}

static void
gdm_xdmcp_chooser_slave_finalize (GObject *object)
{
        GdmXdmcpChooserSlave *xdmcp_chooser_slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_XDMCP_CHOOSER_SLAVE (object));

        xdmcp_chooser_slave = GDM_XDMCP_CHOOSER_SLAVE (object);

        g_return_if_fail (xdmcp_chooser_slave->priv != NULL);

        G_OBJECT_CLASS (gdm_xdmcp_chooser_slave_parent_class)->finalize (object);
}
