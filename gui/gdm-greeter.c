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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-greeter.h"
#include "gdm-greeter-glue.h"


#define GDM_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER, GdmGreeterPrivate))

#define GDM_DBUS_PATH         "/org/gnome/DisplayGreeter"
#define GDM_GREETER_DBUS_PATH GDM_DBUS_PATH "/Greeter"
#define GDM_GREETER_DBUS_NAME "org.gnome.DisplayGreeter.Greeter"

struct GdmGreeterPrivate
{
        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;
};

enum {
	PROP_0
};

enum {
	FOO,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_greeter_class_init	(GdmGreeterClass *klass);
static void	gdm_greeter_init	(GdmGreeter	 *greeter);
static void	gdm_greeter_finalize	(GObject	 *object);

static gpointer greeter_object = NULL;

G_DEFINE_TYPE (GdmGreeter, gdm_greeter, G_TYPE_OBJECT)

GQuark
gdm_greeter_error_quark (void)
{
	static GQuark ret = 0;
	if (ret == 0) {
		ret = g_quark_from_static_string ("gdm_greeter_error");
	}

	return ret;
}


static void
gdm_greeter_set_property (GObject      *object,
			  guint	        prop_id,
			  const GValue  *value,
			  GParamSpec    *pspec)
{
	GdmGreeter *self;

	self = GDM_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_greeter_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	GdmGreeter *self;

	self = GDM_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_greeter_constructor (GType                  type,
			 guint                  n_construct_properties,
			 GObjectConstructParam *construct_properties)
{
        GdmGreeter      *greeter;
        GdmGreeterClass *klass;

        klass = GDM_GREETER_CLASS (g_type_class_peek (GDM_TYPE_GREETER));

        greeter = GDM_GREETER (G_OBJECT_CLASS (gdm_greeter_parent_class)->constructor (type,
										       n_construct_properties,
										       construct_properties));

        return G_OBJECT (greeter);
}

static void
gdm_greeter_class_init (GdmGreeterClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_greeter_get_property;
	object_class->set_property = gdm_greeter_set_property;
	object_class->constructor = gdm_greeter_constructor;
	object_class->finalize = gdm_greeter_finalize;

	g_type_class_add_private (klass, sizeof (GdmGreeterPrivate));

        dbus_g_object_type_install_info (GDM_TYPE_GREETER, &dbus_glib_gdm_greeter_object_info);
}

static void
gdm_greeter_init (GdmGreeter *greeter)
{

	greeter->priv = GDM_GREETER_GET_PRIVATE (greeter);

}

static void
gdm_greeter_finalize (GObject *object)
{
	GdmGreeter *greeter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_GREETER (object));

	greeter = GDM_GREETER (object);

	g_return_if_fail (greeter->priv != NULL);


	G_OBJECT_CLASS (gdm_greeter_parent_class)->finalize (object);
}

GdmGreeter *
gdm_greeter_new (void)
{
	if (greeter_object != NULL) {
		g_object_ref (greeter_object);
	} else {
		greeter_object = g_object_new (GDM_TYPE_GREETER, NULL);
		g_object_add_weak_pointer (greeter_object,
					   (gpointer *) &greeter_object);
	}

	return GDM_GREETER (greeter_object);
}
