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
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gdm-remote-login-window.h"

#define GDM_REMOTE_LOGIN_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_REMOTE_LOGIN_WINDOW, GdmRemoteLoginWindowPrivate))

struct GdmRemoteLoginWindowPrivate
{
        gboolean connected;
        char    *hostname;
        char    *display;
};

enum {
        PROP_0,
};

enum {
        DISCONNECTED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_remote_login_window_class_init   (GdmRemoteLoginWindowClass *klass);
static void     gdm_remote_login_window_init         (GdmRemoteLoginWindow      *remote_login_window);
static void     gdm_remote_login_window_finalize     (GObject                    *object);

G_DEFINE_TYPE (GdmRemoteLoginWindow, gdm_remote_login_window, GTK_TYPE_WINDOW)

static gboolean
start_xephyr (GdmRemoteLoginWindow *login_window)
{
        char    *cmd;
        gboolean res;
        GError  *error;

        cmd = g_strdup_printf ("Xephyr -query %s -parent 0x%x %s",
                               login_window->priv->hostname,
                               (unsigned int)GDK_WINDOW_XID (GTK_WIDGET (login_window)->window),
                               login_window->priv->display);
        g_debug ("Running: %s", cmd);

        error = NULL;
        res = g_spawn_command_line_async (cmd, &error);

        g_free (cmd);

        if (! res) {
                g_warning ("Could not start nested X server: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
}

gboolean
gdm_remote_login_window_connect (GdmRemoteLoginWindow *login_window,
                                 const char           *hostname)
{
        gboolean res;
        char    *title;

        title = g_strdup_printf (_("Remote Login (Connecting to %s...)"), hostname);

        gtk_window_set_title (GTK_WINDOW (login_window), title);

        login_window->priv->hostname = g_strdup (hostname);
        login_window->priv->display = g_strdup (":300");

        res = start_xephyr (login_window);
        if (res) {
                title = g_strdup_printf (_("Remote Login (Connected to %s)"), hostname);
                gtk_window_set_title (GTK_WINDOW (login_window), title);
                g_free (title);
        }

        return res;
}

static void
gdm_remote_login_window_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
        GdmRemoteLoginWindow *self;

        self = GDM_REMOTE_LOGIN_WINDOW (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_remote_login_window_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
        GdmRemoteLoginWindow *self;

        self = GDM_REMOTE_LOGIN_WINDOW (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_remote_login_window_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmRemoteLoginWindow      *login_window;
        GdmRemoteLoginWindowClass *klass;

        klass = GDM_REMOTE_LOGIN_WINDOW_CLASS (g_type_class_peek (GDM_TYPE_REMOTE_LOGIN_WINDOW));

        login_window = GDM_REMOTE_LOGIN_WINDOW (G_OBJECT_CLASS (gdm_remote_login_window_parent_class)->constructor (type,
                                                                                                                      n_construct_properties,
                                                                                                                      construct_properties));


        return G_OBJECT (login_window);
}

static void
gdm_remote_login_window_class_init (GdmRemoteLoginWindowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_remote_login_window_get_property;
        object_class->set_property = gdm_remote_login_window_set_property;
        object_class->constructor = gdm_remote_login_window_constructor;
        object_class->finalize = gdm_remote_login_window_finalize;

        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmRemoteLoginWindowClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (GdmRemoteLoginWindowPrivate));
}

static void
gdm_remote_login_window_init (GdmRemoteLoginWindow *login_window)
{
        login_window->priv = GDM_REMOTE_LOGIN_WINDOW_GET_PRIVATE (login_window);

        gtk_window_set_position (GTK_WINDOW (login_window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_title (GTK_WINDOW (login_window), _("Remote Login"));
        /*gtk_window_set_decorated (GTK_WINDOW (login_window), FALSE);*/
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_stick (GTK_WINDOW (login_window));
        gtk_window_maximize (GTK_WINDOW (login_window));
}

static void
gdm_remote_login_window_finalize (GObject *object)
{
        GdmRemoteLoginWindow *login_window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_REMOTE_LOGIN_WINDOW (object));

        login_window = GDM_REMOTE_LOGIN_WINDOW (object);

        g_return_if_fail (login_window->priv != NULL);

        G_OBJECT_CLASS (gdm_remote_login_window_parent_class)->finalize (object);
}

GtkWidget *
gdm_remote_login_window_new (gboolean is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_REMOTE_LOGIN_WINDOW,
                               NULL);

        return GTK_WIDGET (object);
}
