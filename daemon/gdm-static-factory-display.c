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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-static-factory-display.h"
#include "gdm-static-factory-display-glue.h"
#include "gdm-product-display.h"

#include "gdm-display-store.h"

#define GDM_STATIC_FACTORY_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_STATIC_FACTORY_DISPLAY, GdmStaticFactoryDisplayPrivate))

#define DEFAULT_SLAVE_COMMAND LIBEXECDIR"/gdm-factory-slave"

struct GdmStaticFactoryDisplayPrivate
{
	GdmDisplayStore *display_store;
};

enum {
	PROP_0,
	PROP_DISPLAY_STORE,
};

static void	gdm_static_factory_display_class_init	(GdmStaticFactoryDisplayClass *klass);
static void	gdm_static_factory_display_init	        (GdmStaticFactoryDisplay      *static_factory_display);
static void	gdm_static_factory_display_finalize	(GObject	              *object);

static guint32 display_number = 100;

G_DEFINE_TYPE (GdmStaticFactoryDisplay, gdm_static_factory_display, GDM_TYPE_DISPLAY)

static guint32
get_next_display_number (void)
{
	guint32 num;

	num = display_number++;

	if ((gint32)display_number < 0) {
		display_number = 100;
	}

	return num;
}

gboolean
gdm_static_factory_display_create_product_display (GdmStaticFactoryDisplay *display,
						   char                   **id,
						   GError                 **error)
{
	gboolean    ret;
	GdmDisplay *product;
	guint32     num;

	g_return_val_if_fail (GDM_IS_STATIC_FACTORY_DISPLAY (display), FALSE);

	ret = FALSE;

	num = get_next_display_number ();

	product = gdm_product_display_new (num);

	if (! gdm_display_create_authority (product)) {
		g_object_unref (product);
		product = NULL;
		goto out;
	}

	gdm_display_store_add (display->priv->display_store, product);

	if (! gdm_display_manage (product)) {
		g_object_unref (product);
		product = NULL;
		goto out;
	}

	if (! gdm_display_get_id (product, id, NULL)) {
		g_object_unref (product);
		product = NULL;
		goto out;
	}

	ret = TRUE;
 out:
	return ret;
}

static gboolean
gdm_static_factory_display_create_authority (GdmDisplay *display)
{
	g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

	GDM_DISPLAY_CLASS (gdm_static_factory_display_parent_class)->create_authority (display);

	return TRUE;
}

static gboolean
gdm_static_factory_display_manage (GdmDisplay *display)
{
	g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

	GDM_DISPLAY_CLASS (gdm_static_factory_display_parent_class)->manage (display);

	return TRUE;
}

static gboolean
gdm_static_factory_display_unmanage (GdmDisplay *display)
{
	g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

	GDM_DISPLAY_CLASS (gdm_static_factory_display_parent_class)->unmanage (display);

	return TRUE;
}

static void
gdm_static_factory_display_set_display_store (GdmStaticFactoryDisplay *display,
					      GdmDisplayStore         *display_store)
{
	if (display->priv->display_store != NULL) {
		g_object_unref (display->priv->display_store);
		display->priv->display_store = NULL;
	}

	if (display_store != NULL) {
		display->priv->display_store = g_object_ref (display_store);
	}
}

static void
gdm_static_factory_display_set_property (GObject      *object,
					 guint	       prop_id,
					 const GValue *value,
					 GParamSpec   *pspec)
{
	GdmStaticFactoryDisplay *self;

	self = GDM_STATIC_FACTORY_DISPLAY (object);

	switch (prop_id) {
	case PROP_DISPLAY_STORE:
		gdm_static_factory_display_set_display_store (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_static_factory_display_get_property (GObject    *object,
					 guint       prop_id,
					 GValue	    *value,
					 GParamSpec *pspec)
{
	GdmStaticFactoryDisplay *self;

	self = GDM_STATIC_FACTORY_DISPLAY (object);

	switch (prop_id) {
	case PROP_DISPLAY_STORE:
		g_value_set_object (value, self->priv->display_store);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_static_factory_display_constructor (GType                  type,
					guint                  n_construct_properties,
					GObjectConstructParam *construct_properties)
{
        GdmStaticFactoryDisplay      *display;
        GdmStaticFactoryDisplayClass *klass;

        klass = GDM_STATIC_FACTORY_DISPLAY_CLASS (g_type_class_peek (GDM_TYPE_STATIC_FACTORY_DISPLAY));

        display = GDM_STATIC_FACTORY_DISPLAY (G_OBJECT_CLASS (gdm_static_factory_display_parent_class)->constructor (type,
														     n_construct_properties,
														     construct_properties));
	g_object_set (display,
		      "slave-command", DEFAULT_SLAVE_COMMAND,
		      NULL);

        return G_OBJECT (display);
}


static void
gdm_static_factory_display_class_init (GdmStaticFactoryDisplayClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

	object_class->get_property = gdm_static_factory_display_get_property;
	object_class->set_property = gdm_static_factory_display_set_property;
        object_class->constructor = gdm_static_factory_display_constructor;
	object_class->finalize = gdm_static_factory_display_finalize;

	display_class->create_authority = gdm_static_factory_display_create_authority;
	display_class->manage = gdm_static_factory_display_manage;
	display_class->unmanage = gdm_static_factory_display_unmanage;

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_STORE,
                                         g_param_spec_object ("display-store",
							      "display store",
							      "display store",
							      GDM_TYPE_DISPLAY_STORE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (GdmStaticFactoryDisplayPrivate));

	dbus_g_object_type_install_info (GDM_TYPE_STATIC_FACTORY_DISPLAY, &dbus_glib_gdm_static_factory_display_object_info);
}

static void
gdm_static_factory_display_init (GdmStaticFactoryDisplay *static_factory_display)
{

	static_factory_display->priv = GDM_STATIC_FACTORY_DISPLAY_GET_PRIVATE (static_factory_display);
}

static void
gdm_static_factory_display_finalize (GObject *object)
{
	GdmStaticFactoryDisplay *static_factory_display;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_STATIC_FACTORY_DISPLAY (object));

	static_factory_display = GDM_STATIC_FACTORY_DISPLAY (object);

	g_return_if_fail (static_factory_display->priv != NULL);

	G_OBJECT_CLASS (gdm_static_factory_display_parent_class)->finalize (object);
}

GdmDisplay *
gdm_static_factory_display_new (int              display_number,
				GdmDisplayStore *store)
{
	GObject *object;
	char    *x11_display;

	x11_display = g_strdup_printf (":%d", display_number);
	object = g_object_new (GDM_TYPE_STATIC_FACTORY_DISPLAY,
			       "number", display_number,
			       "x11-display", x11_display,
			       "display-store", store,
			       NULL);
	g_free (x11_display);

	return GDM_DISPLAY (object);
}
