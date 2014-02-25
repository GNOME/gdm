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
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-xdmcp-chooser-display.h"
#include "gdm-xdmcp-chooser-slave.h"

#include "gdm-common.h"
#include "gdm-address.h"

enum {
        HOSTNAME_SELECTED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_xdmcp_chooser_display_class_init    (GdmXdmcpChooserDisplayClass *klass);
static void     gdm_xdmcp_chooser_display_init          (GdmXdmcpChooserDisplay      *xdmcp_chooser_display);
static gboolean gdm_xdmcp_chooser_display_prepare       (GdmDisplay *display);
static gboolean gdm_xdmcp_chooser_display_finish        (GdmDisplay *display);

G_DEFINE_TYPE (GdmXdmcpChooserDisplay, gdm_xdmcp_chooser_display, GDM_TYPE_XDMCP_DISPLAY)

static void
on_hostname_selected (GdmXdmcpChooserSlave     *slave,
                      const char               *hostname,
                      GdmXdmcpChooserDisplay   *display)
{
        g_debug ("GdmXdmcpChooserDisplay: hostname selected: %s", hostname);
        g_signal_emit (display, signals [HOSTNAME_SELECTED], 0, hostname);
}

static void
gdm_xdmcp_chooser_display_class_init (GdmXdmcpChooserDisplayClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        display_class->prepare = gdm_xdmcp_chooser_display_prepare;
        display_class->finish = gdm_xdmcp_chooser_display_finish;

        signals [HOSTNAME_SELECTED] =
                g_signal_new ("hostname-selected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmXdmcpChooserDisplayClass, hostname_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
}

static void
gdm_xdmcp_chooser_display_init (GdmXdmcpChooserDisplay *xdmcp_chooser_display)
{
}

static gboolean
gdm_xdmcp_chooser_display_prepare (GdmDisplay *display)
{
        GdmXdmcpChooserSlave *slave;

        if (!GDM_DISPLAY_CLASS (gdm_xdmcp_chooser_display_parent_class)->prepare (display))
                return FALSE;

        slave = GDM_XDMCP_CHOOSER_SLAVE (gdm_display_get_slave (display));

        g_signal_connect (slave, "hostname-selected",
                          G_CALLBACK (on_hostname_selected), display);

        return TRUE;
}

static gboolean
gdm_xdmcp_chooser_display_finish (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        GDM_DISPLAY_CLASS (gdm_xdmcp_chooser_display_parent_class)->finish (display);

        gdm_display_unmanage (display);

        return TRUE;
}

GdmDisplay *
gdm_xdmcp_chooser_display_new (const char              *hostname,
                               int                      number,
                               GdmAddress              *address,
                               gint32                   session_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf ("%s:%d", hostname, number);
        object = g_object_new (GDM_TYPE_XDMCP_CHOOSER_DISPLAY,
                               "slave-type", GDM_TYPE_XDMCP_CHOOSER_SLAVE,
                               "remote-hostname", hostname,
                               "x11-display-number", number,
                               "x11-display-name", x11_display,
                               "is-local", FALSE,
                               "remote-address", address,
                               "session-number", session_number,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
