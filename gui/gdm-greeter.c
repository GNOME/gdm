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

#include "gdm-greeter.h"

#define GDM_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER, GdmGreeterPrivate))

struct GdmGreeterPrivate
{
	gpointer dummy;
};

enum {
	PROP_0,
};

enum {
	QUERY_ANSWER,
	SESSION_SELECTED,
	LANGUAGE_SELECTED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };


static void	gdm_greeter_class_init	(GdmGreeterClass *klass);
static void	gdm_greeter_init	(GdmGreeter	 *greeter);
static void	gdm_greeter_finalize	(GObject	 *object);

G_DEFINE_ABSTRACT_TYPE (GdmGreeter, gdm_greeter, G_TYPE_OBJECT)

GQuark
gdm_greeter_error_quark (void)
{
	static GQuark ret = 0;
	if (ret == 0) {
		ret = g_quark_from_static_string ("gdm_greeter_error");
	}

	return ret;
}

static gboolean
gdm_greeter_real_start (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_start (GdmGreeter *greeter)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->start (greeter);
	g_object_unref (greeter);

	return ret;
}

static gboolean
gdm_greeter_real_stop (GdmGreeter *greeter)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_stop (GdmGreeter *greeter)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->stop (greeter);
	g_object_unref (greeter);

	return ret;
}

static gboolean
gdm_greeter_real_info (GdmGreeter *greeter,
		       const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_info (GdmGreeter *greeter,
		  const char *text)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->info (greeter, text);
	g_object_unref (greeter);

	return ret;
}

static gboolean
gdm_greeter_real_problem (GdmGreeter *greeter,
			  const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_problem (GdmGreeter *greeter,
		     const char *text)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->problem (greeter, text);
	g_object_unref (greeter);

	return ret;
}

static gboolean
gdm_greeter_real_info_query (GdmGreeter *greeter,
			     const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_info_query (GdmGreeter *greeter,
			const char *text)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->info_query (greeter, text);
	g_object_unref (greeter);

	return ret;
}

static gboolean
gdm_greeter_real_secret_info_query (GdmGreeter *greeter,
				    const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	return TRUE;
}

gboolean
gdm_greeter_secret_info_query (GdmGreeter *greeter,
			       const char *text)
{
	gboolean ret;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_object_ref (greeter);
	ret = GDM_GREETER_GET_CLASS (greeter)->secret_info_query (greeter, text);
	g_object_unref (greeter);

	return ret;
}

gboolean
gdm_greeter_answer_query (GdmGreeter *greeter,
			  const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_debug ("Answer query: %s", text);

	g_signal_emit (greeter, signals[QUERY_ANSWER], 0, text);

	return TRUE;
}

gboolean
gdm_greeter_select_session (GdmGreeter *greeter,
			    const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_debug ("Select session: %s", text);

	g_signal_emit (greeter, signals[SESSION_SELECTED], 0, text);

	return TRUE;
}

gboolean
gdm_greeter_select_language (GdmGreeter *greeter,
			     const char *text)
{
	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	g_debug ("Select language: %s", text);

	g_signal_emit (greeter, signals[LANGUAGE_SELECTED], 0, text);

	return TRUE;
}

static void
gdm_greeter_set_property (GObject	 *object,
			  guint		  prop_id,
			  const GValue	 *value,
			  GParamSpec	 *pspec)
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
gdm_greeter_get_property (GObject	 *object,
			  guint  	  prop_id,
			  GValue	 *value,
			  GParamSpec	 *pspec)
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
gdm_greeter_dispose (GObject *object)
{
	GdmGreeter *greeter;

	greeter = GDM_GREETER (object);

	g_debug ("Disposing greeter");
	gdm_greeter_stop (greeter);

	G_OBJECT_CLASS (gdm_greeter_parent_class)->dispose (object);
}

static void
gdm_greeter_class_init (GdmGreeterClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_greeter_get_property;
	object_class->set_property = gdm_greeter_set_property;
        object_class->constructor = gdm_greeter_constructor;
	object_class->dispose = gdm_greeter_dispose;
	object_class->finalize = gdm_greeter_finalize;

	klass->start = gdm_greeter_real_start;
	klass->stop = gdm_greeter_real_stop;
	klass->info = gdm_greeter_real_info;
	klass->problem = gdm_greeter_real_problem;
	klass->info_query = gdm_greeter_real_info_query;
	klass->secret_info_query = gdm_greeter_real_secret_info_query;


	signals [QUERY_ANSWER] =
		g_signal_new ("query-answer",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmGreeterClass, query_answer),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);
	signals [LANGUAGE_SELECTED] =
		g_signal_new ("language-selected",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmGreeterClass, language_selected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);
	signals [SESSION_SELECTED] =
		g_signal_new ("session-selected",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmGreeterClass, session_selected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (GdmGreeterPrivate));
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
