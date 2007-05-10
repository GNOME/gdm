/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-xdmcp-display.h"
#include "gdm-xdmcp-display-glue.h"

#include "gdm-common.h"
#include "gdm-address.h"

#include "cookie.h"
#include "auth.h"

#define GDM_XDMCP_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_XDMCP_DISPLAY, GdmXdmcpDisplayPrivate))

struct GdmXdmcpDisplayPrivate
{
	char                   *remote_hostname;
	GdmAddress             *remote_address;
	gint32                  session_number;
};

enum {
	PROP_0,
	PROP_REMOTE_HOSTNAME,
	PROP_REMOTE_ADDRESS,
	PROP_SESSION_NUMBER,
};

static void	gdm_xdmcp_display_class_init	(GdmXdmcpDisplayClass *klass);
static void	gdm_xdmcp_display_init	        (GdmXdmcpDisplay      *xdmcp_display);
static void	gdm_xdmcp_display_finalize	(GObject	      *object);

G_DEFINE_TYPE (GdmXdmcpDisplay, gdm_xdmcp_display, GDM_TYPE_DISPLAY)

gint32
gdm_xdmcp_display_get_session_number (GdmXdmcpDisplay *display)
{
	g_return_val_if_fail (GDM_IS_XDMCP_DISPLAY (display), 0);

	return display->priv->session_number;
}

gboolean
gdm_xdmcp_display_get_remote_hostname (GdmXdmcpDisplay *display,
				       char           **hostname,
				       GError         **error)
{
	g_return_val_if_fail (GDM_IS_XDMCP_DISPLAY (display), FALSE);

	if (hostname != NULL) {
		*hostname = g_strdup (display->priv->remote_hostname);
	}

	return TRUE;
}

GdmAddress *
gdm_xdmcp_display_get_remote_address (GdmXdmcpDisplay *display)
{
	g_return_val_if_fail (GDM_IS_XDMCP_DISPLAY (display), NULL);

	return display->priv->remote_address;
}

static gboolean
gdm_xdmcp_display_create_authority (GdmDisplay *display)
{
	FILE    *af;
	int      closeret;
	gboolean ret;
	char    *authfile;
	int      display_num;
	char    *name;
	char    *cookie;
	char    *bcookie;
	GSList  *authlist;
	char    *basename;

	ret = FALSE;
	name = NULL;

	g_object_get (display,
		      "name", &name,
		      "number", &display_num,
		      NULL);

	g_debug ("Setting up access for %s", name);

	/* gdm and xserver authfile can be the same, server will run as root */
	basename = g_strconcat (name, ".Xauth", NULL);
	authfile = g_build_filename (AUTHDIR, basename, NULL);
	g_free (basename);

	af = gdm_safe_fopen_w (authfile, 0644);
	if (af == NULL) {
		g_warning (_("Cannot safely open %s"), authfile);
		g_free (authfile);
		goto out;
	}

	/* Create new random cookie */
	gdm_cookie_generate (&cookie, &bcookie);
	authlist = NULL;
	if (! gdm_auth_add_entry_for_display (display_num, bcookie, &authlist, af)) {
		goto out;
	}

	g_debug ("gdm_auth_secure_display: Setting up access");

	VE_IGNORE_EINTR (closeret = fclose (af));
	if (closeret < 0) {
		g_warning (_("Could not write new authorization entry: %s"),
			   g_strerror (errno));
		goto out;
	}

	g_debug ("Set up access for %s - %d entries",
		 name,
		 g_slist_length (authlist));

	/* FIXME: save authlist */

	g_object_set (display,
		      "authority-file", authfile,
		      "cookie", cookie,
		      "binary-cookie", bcookie,
		      NULL);

	ret = TRUE;

 out:
	g_free (name);

	return ret;
}

static gboolean
gdm_xdmcp_display_manage (GdmDisplay *display)
{
	g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

	GDM_DISPLAY_CLASS (gdm_xdmcp_display_parent_class)->manage (display);

	return TRUE;
}

static gboolean
gdm_xdmcp_display_unmanage (GdmDisplay *display)
{
	g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

	GDM_DISPLAY_CLASS (gdm_xdmcp_display_parent_class)->unmanage (display);

	return TRUE;
}

static void
gdm_xdmcp_display_set_property (GObject	     *object,
				guint	      prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GdmXdmcpDisplay *self;

	self = GDM_XDMCP_DISPLAY (object);

	switch (prop_id) {
	case PROP_REMOTE_HOSTNAME:
		self->priv->remote_hostname = g_value_dup_string (value);
		break;
	case PROP_REMOTE_ADDRESS:
		self->priv->remote_address = g_value_get_boxed (value);
		break;
	case PROP_SESSION_NUMBER:
		self->priv->session_number = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_xdmcp_display_get_property (GObject	   *object,
				guint  	    prop_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GdmXdmcpDisplay *self;

	self = GDM_XDMCP_DISPLAY (object);

	switch (prop_id) {
	case PROP_REMOTE_HOSTNAME:
		g_value_set_string (value, self->priv->remote_hostname);
		break;
	case PROP_REMOTE_ADDRESS:
		g_value_set_boxed (value, self->priv->remote_address);
		break;
	case PROP_SESSION_NUMBER:
		g_value_set_int (value, self->priv->session_number);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_xdmcp_display_class_init (GdmXdmcpDisplayClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

	object_class->get_property = gdm_xdmcp_display_get_property;
	object_class->set_property = gdm_xdmcp_display_set_property;
	object_class->finalize = gdm_xdmcp_display_finalize;

	display_class->create_authority = gdm_xdmcp_display_create_authority;
	display_class->manage = gdm_xdmcp_display_manage;
	display_class->unmanage = gdm_xdmcp_display_unmanage;

	g_type_class_add_private (klass, sizeof (GdmXdmcpDisplayPrivate));

	g_object_class_install_property (object_class,
					 PROP_REMOTE_HOSTNAME,
					 g_param_spec_string ("remote-hostname",
							      "remote-hostname",
							      "remote-hostname",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_REMOTE_ADDRESS,
					 g_param_spec_boxed ("remote-address",
							     "Remote address",
							     "Remote address",
							     GDM_TYPE_ADDRESS,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_NUMBER,
                                         g_param_spec_int ("session-number",
							   "session-number",
							   "session-number",
							   G_MININT,
							   G_MAXINT,
							   0,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	dbus_g_object_type_install_info (GDM_TYPE_XDMCP_DISPLAY, &dbus_glib_gdm_xdmcp_display_object_info);
}

static void
gdm_xdmcp_display_init (GdmXdmcpDisplay *xdmcp_display)
{

	xdmcp_display->priv = GDM_XDMCP_DISPLAY_GET_PRIVATE (xdmcp_display);
}

static void
gdm_xdmcp_display_finalize (GObject *object)
{
	GdmXdmcpDisplay *xdmcp_display;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_XDMCP_DISPLAY (object));

	xdmcp_display = GDM_XDMCP_DISPLAY (object);

	g_return_if_fail (xdmcp_display->priv != NULL);

	G_OBJECT_CLASS (gdm_xdmcp_display_parent_class)->finalize (object);
}

GdmDisplay *
gdm_xdmcp_display_new (int                      number,
		       const char              *name,
		       const char              *hostname,
		       GdmAddress              *address,
		       gint32                   session_number)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_XDMCP_DISPLAY,
			       "is-local", FALSE,
			       "number", number,
			       "name", name,
			       "remote-hostname", hostname,
			       "remote-address", address,
			       "session-number", session_number,
			       NULL);

	return GDM_DISPLAY (object);
}
