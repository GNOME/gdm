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
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gdm-greeter.h"
#include "gdm-simple-greeter.h"
#include "gdm-common.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#define GDM_SIMPLE_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeterPrivate))

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

struct GdmSimpleGreeterPrivate
{
};

enum {
	PROP_0,
};

static void	gdm_simple_greeter_class_init	(GdmSimpleGreeterClass *klass);
static void	gdm_simple_greeter_init	        (GdmSimpleGreeter      *simple_greeter);
static void	gdm_simple_greeter_finalize	(GObject	       *object);

G_DEFINE_TYPE (GdmSimpleGreeter, gdm_simple_greeter, GDM_TYPE_GREETER)

static gboolean
gdm_simple_greeter_start (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->start (greeter);

	return TRUE;
}

static gboolean
gdm_simple_greeter_stop (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->stop (greeter);

	return TRUE;
}

static gboolean
gdm_simple_greeter_info (GdmGreeter *greeter,
			 const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->info (greeter, text);

	g_debug ("SIMPLE GREETER: info: %s", text);



	return TRUE;
}

static gboolean
gdm_simple_greeter_problem (GdmGreeter *greeter,
			    const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->problem (greeter, text);

	g_debug ("SIMPLE GREETER: problem: %s", text);



	return TRUE;
}

static gboolean
gdm_simple_greeter_info_query (GdmGreeter *greeter,
			       const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->info_query (greeter, text);

	g_debug ("SIMPLE GREETER: info query: %s", text);



	return TRUE;
}

static gboolean
gdm_simple_greeter_secret_info_query (GdmGreeter *greeter,
				      const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->secret_info_query (greeter, text);

	g_debug ("SIMPLE GREETER: secret info query: %s", text);


	return TRUE;
}

static void
gdm_simple_greeter_set_property (GObject      *object,
				 guint	       prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	GdmSimpleGreeter *self;

	self = GDM_SIMPLE_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_simple_greeter_get_property (GObject    *object,
				 guint       prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	GdmSimpleGreeter *self;

	self = GDM_SIMPLE_GREETER (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
create_greeter (GdmSimpleGreeter *greeter)
{
}

static GObject *
gdm_simple_greeter_constructor (GType                  type,
				guint                  n_construct_properties,
				GObjectConstructParam *construct_properties)
{
        GdmSimpleGreeter      *greeter;
        GdmSimpleGreeterClass *klass;

        klass = GDM_SIMPLE_GREETER_CLASS (g_type_class_peek (GDM_TYPE_SIMPLE_GREETER));

        greeter = GDM_SIMPLE_GREETER (G_OBJECT_CLASS (gdm_simple_greeter_parent_class)->constructor (type,
												     n_construct_properties,
												     construct_properties));
	create_greeter (greeter);

        return G_OBJECT (greeter);
}

static void
gdm_simple_greeter_class_init (GdmSimpleGreeterClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	GdmGreeterClass *greeter_class = GDM_GREETER_CLASS (klass);

	object_class->get_property = gdm_simple_greeter_get_property;
	object_class->set_property = gdm_simple_greeter_set_property;
	object_class->constructor = gdm_simple_greeter_constructor;
	object_class->finalize = gdm_simple_greeter_finalize;

	greeter_class->start = gdm_simple_greeter_start;
	greeter_class->stop = gdm_simple_greeter_stop;
	greeter_class->info = gdm_simple_greeter_info;
	greeter_class->problem = gdm_simple_greeter_problem;
	greeter_class->info_query = gdm_simple_greeter_info_query;
	greeter_class->secret_info_query = gdm_simple_greeter_secret_info_query;

	g_type_class_add_private (klass, sizeof (GdmSimpleGreeterPrivate));
}

static void
gdm_simple_greeter_init (GdmSimpleGreeter *simple_greeter)
{

	simple_greeter->priv = GDM_SIMPLE_GREETER_GET_PRIVATE (simple_greeter);

}

static void
gdm_simple_greeter_finalize (GObject *object)
{
	GdmSimpleGreeter *simple_greeter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SIMPLE_GREETER (object));

	simple_greeter = GDM_SIMPLE_GREETER (object);

	g_return_if_fail (simple_greeter->priv != NULL);

	G_OBJECT_CLASS (gdm_simple_greeter_parent_class)->finalize (object);
}

GdmGreeter *
gdm_simple_greeter_new (void)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SIMPLE_GREETER,
			       NULL);

	return GDM_GREETER (object);
}
