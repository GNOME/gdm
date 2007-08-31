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
#include <sys/socket.h>

#include <X11/Xmd.h>
#include <X11/Xdmcp.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gdm-address.h"
#include "gdm-host-chooser-widget.h"

#define GDM_HOST_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_HOST_CHOOSER_WIDGET, GdmHostChooserWidgetPrivate))

struct GdmHostChooserWidgetPrivate
{
        GtkWidget  *treeview;

        XdmcpBuffer buf;
        gboolean    have_ipv6;
        int         socket_fd;
        guint       io_watch_id;
        guint       scan_time_id;
        guint       ping_try_id;

        int         ping_tries;
};

enum {
        PROP_0,
};

enum {
        HOSTNAME_ACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_host_chooser_widget_class_init  (GdmHostChooserWidgetClass *klass);
static void     gdm_host_chooser_widget_init        (GdmHostChooserWidget      *host_chooser_widget);
static void     gdm_host_chooser_widget_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmHostChooserWidget, gdm_host_chooser_widget, GTK_TYPE_VBOX)

typedef struct _XdmAuth {
        ARRAY8  authentication;
        ARRAY8  authorization;
} XdmAuthRec, *XdmAuthPtr;

static XdmAuthRec authlist = {
        { (CARD16)  0, (CARD8 *) 0 },
        { (CARD16)  0, (CARD8 *) 0 }
};

#define GDM_XDMCP_PROTOCOL_VERSION 1001
#define SCAN_TIMEOUT 30000
#define PING_TIMEOUT 2000
#define PING_TRIES 3

static gboolean
decode_packet (GIOChannel           *source,
               GIOCondition          condition,
               GdmHostChooserWidget *widget)
{
        struct sockaddr_storage clnt_ss;
        GdmAddress             *address;
        gint                    ss_len;
        XdmcpHeader             header;
        int                     res;

        g_debug ("decode_packet: GIOCondition %d", (int)condition);

        if ( ! (condition & G_IO_IN)) {
                return TRUE;
        }

        ss_len = sizeof (clnt_ss);
        res = XdmcpFill (widget->priv->socket_fd, &widget->priv->buf, (XdmcpNetaddr)&clnt_ss, &ss_len);
        if G_UNLIKELY (! res) {
                g_debug (_("XMCP: Could not create XDMCP buffer!"));
                return TRUE;
        }

        res = XdmcpReadHeader (&widget->priv->buf, &header);
        if G_UNLIKELY (! res) {
                g_warning (_("XDMCP: Could not read XDMCP header!"));
                return TRUE;
        }

        if G_UNLIKELY (header.version != XDM_PROTOCOL_VERSION &&
                       header.version != GDM_XDMCP_PROTOCOL_VERSION) {
                g_warning (_("XMDCP: Incorrect XDMCP version!"));
                return TRUE;
        }

        address = gdm_address_new_from_sockaddr_storage (&clnt_ss);
        if (address == NULL) {
                g_warning (_("XMDCP: Unable to parse address"));
                return TRUE;
        }

        gdm_address_debug (address);

        return TRUE;
}

static void
do_ping (GdmHostChooserWidget *widget,
         gboolean              full)
{
        g_debug ("do ping full:%d", full);
}

static gboolean
ping_try (GdmHostChooserWidget *widget)
{
        do_ping (widget, FALSE);

        widget->priv->ping_tries --;

        if (widget->priv->ping_tries <= 0) {
                widget->priv->ping_try_id = 0;
                return FALSE;
        } else {
                return TRUE;
        }
}

static void
xdmcp_discover (GdmHostChooserWidget *widget)
{
#if 0
        gtk_widget_set_sensitive (GTK_WIDGET (manage), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (rescan), FALSE);
        gtk_list_store_clear (GTK_LIST_STORE (browser_model));
        gtk_widget_set_sensitive (GTK_WIDGET (browser), FALSE);
        gtk_label_set_label (GTK_LABEL (status_label),
                             _(scanning_message));

        while (hl) {
                gdm_chooser_host_dispose ((GdmChooserHost *) hl->data);
                hl = hl->next;
        }

        g_list_free (chooser_hosts);
        chooser_hosts = NULL;
#endif

        do_ping (widget, TRUE);

#if 0
        if (widget->priv->scan_time_id > 0) {
                g_source_remove (widget->priv->scan_time_id);
        }

        widget->priv->scan_time_id = g_timeout_add (SCAN_TIMEOUT,
                                                    chooser_scan_time_update,
                                                    widget);
#endif
        /* Note we already used up one try */
        widget->priv->ping_tries = PING_TRIES - 1;
        if (widget->priv->ping_try_id > 0) {
                g_source_remove (widget->priv->ping_try_id);
        }

        widget->priv->ping_try_id = g_timeout_add (PING_TIMEOUT,
                                                   ping_try,
                                                   widget);
}

static void
xdmcp_init (GdmHostChooserWidget *widget)
{
        static XdmcpHeader header;
        int                sockopts;
        int                res;
        GIOChannel        *ioc;

        sockopts = 1;

        widget->priv->socket_fd = -1;

        /* Open socket for communication */
#ifdef ENABLE_IPV6
        widget->priv->socket_fd = socket (AF_INET6, SOCK_DGRAM, 0);
        if (widget->priv->socket_fd != -1) {
                widget->priv->have_ipv6 = TRUE;
        }
#endif
        if (! widget->priv->have_ipv6) {
                widget->priv->socket_fd = socket (AF_INET, SOCK_DGRAM, 0);
                if (widget->priv->socket_fd == -1) {
                        g_critical ("Could not create socket!");
                }
        }

        res = setsockopt (widget->priv->socket_fd,
                          SOL_SOCKET,
                          SO_BROADCAST,
                          (char *) &sockopts,
                          sizeof (sockopts));
        if (res < 0) {
                g_critical ("Could not set socket options!");
        }

        /* Assemble XDMCP BROADCAST_QUERY packet in static buffer */
        header.opcode  = (CARD16) BROADCAST_QUERY;
        header.length  = 1;
        header.version = XDM_PROTOCOL_VERSION;
        XdmcpWriteHeader (&widget->priv->buf, &header);
        XdmcpWriteARRAY8 (&widget->priv->buf, &authlist.authentication);

        /* Assemble XDMCP QUERY packet in static buffer */
        header.opcode  = (CARD16) QUERY;
        header.length  = 1;
        header.version = XDM_PROTOCOL_VERSION;
        XdmcpWriteHeader (&widget->priv->buf, &header);
        XdmcpWriteARRAY8 (&widget->priv->buf, &authlist.authentication);

        /*gdm_chooser_add_hosts (hosts);*/

        ioc = g_io_channel_unix_new (widget->priv->socket_fd);
        g_io_channel_set_encoding (ioc, NULL, NULL);
        g_io_channel_set_buffered (ioc, FALSE);
        widget->priv->io_watch_id = g_io_add_watch(ioc,
                                                   G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                                   (GIOFunc)decode_packet,
                                                   widget);
        g_io_channel_unref (ioc);

        xdmcp_discover (widget);
}

void
gdm_host_chooser_widget_refresh (GdmHostChooserWidget *widget)
{
        g_return_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (widget));
}

char *
gdm_host_chooser_widget_get_current_hostname (GdmHostChooserWidget *widget)
{
        char *hostname;

        g_return_val_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (widget), NULL);

        hostname = NULL;

        return hostname;
}

static void
gdm_host_chooser_widget_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserWidget *self;

        self = GDM_HOST_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_host_chooser_widget_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmHostChooserWidget *self;

        self = GDM_HOST_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_host_chooser_widget_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmHostChooserWidget      *host_chooser_widget;
        GdmHostChooserWidgetClass *klass;

        klass = GDM_HOST_CHOOSER_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_HOST_CHOOSER_WIDGET));

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (host_chooser_widget);
}

static void
gdm_host_chooser_widget_dispose (GObject *object)
{
        GdmHostChooserWidget *host_chooser_widget;

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (object);

        g_debug ("Disposing host_chooser_widget");

        G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_host_chooser_widget_class_init (GdmHostChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_host_chooser_widget_get_property;
        object_class->set_property = gdm_host_chooser_widget_set_property;
        object_class->constructor = gdm_host_chooser_widget_constructor;
        object_class->dispose = gdm_host_chooser_widget_dispose;
        object_class->finalize = gdm_host_chooser_widget_finalize;

        signals [HOSTNAME_ACTIVATED] = g_signal_new ("hostname-activated",
                                                     G_TYPE_FROM_CLASS (object_class),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GdmHostChooserWidgetClass, hostname_activated),
                                                     NULL,
                                                     NULL,
                                                     g_cclosure_marshal_VOID__STRING,
                                                     G_TYPE_NONE,
                                                     1, G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (GdmHostChooserWidgetPrivate));
}

static void
on_row_activated (GtkTreeView          *tree_view,
                  GtkTreePath          *tree_path,
                  GtkTreeViewColumn    *tree_column,
                  GdmHostChooserWidget *widget)
{
}


static void
gdm_host_chooser_widget_init (GdmHostChooserWidget *widget)
{
        GtkWidget *scrolled;

        widget->priv = GDM_HOST_CHOOSER_WIDGET_GET_PRIVATE (widget);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget->priv->treeview), FALSE);
        g_signal_connect (widget->priv->treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->treeview);

        xdmcp_init (widget);
}

static void
gdm_host_chooser_widget_finalize (GObject *object)
{
        GdmHostChooserWidget *host_chooser_widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_HOST_CHOOSER_WIDGET (object));

        host_chooser_widget = GDM_HOST_CHOOSER_WIDGET (object);

        g_return_if_fail (host_chooser_widget->priv != NULL);

        G_OBJECT_CLASS (gdm_host_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_host_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_HOST_CHOOSER_WIDGET,
                               NULL);

        return GTK_WIDGET (object);
}
