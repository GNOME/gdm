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

#include <glade/glade-xml.h>

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

#define GLADE_XML_FILE "gdm-simple-greeter.glade"

#define GDM_SIMPLE_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeterPrivate))

enum {
	RESPONSE_RESTART,
	RESPONSE_REBOOT,
	RESPONSE_CLOSE
};

struct GdmSimpleGreeterPrivate
{
	GladeXML       *xml;
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
	GtkWidget  *entry;
	GtkWidget  *label;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->info_query (greeter, text);

	g_debug ("SIMPLE GREETER: info query: %s", text);

	entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-entry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);

	label = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-label");
	gtk_label_set_text (GTK_LABEL (label), text);

        if (! GTK_WIDGET_HAS_FOCUS (entry)) {
                gtk_widget_grab_focus (entry);
        }

	return TRUE;
}

static gboolean
gdm_simple_greeter_secret_info_query (GdmGreeter *greeter,
				      const char *text)
{
	GtkWidget  *entry;
	GtkWidget  *label;

	g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

	GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->secret_info_query (greeter, text);

	g_debug ("SIMPLE GREETER: secret info query: %s", text);

	entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-entry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);

	label = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-label");
	gtk_label_set_text (GTK_LABEL (label), text);

        if (! GTK_WIDGET_HAS_FOCUS (entry)) {
                gtk_widget_grab_focus (entry);
        }

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
ok_button_clicked (GtkButton        *button,
		   GdmSimpleGreeter *greeter)
{
	gboolean    res;
	GtkWidget  *entry;
	const char *text;

	entry = glade_xml_get_widget (greeter->priv->xml, "auth-entry");
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	res = gdm_greeter_answer_query (GDM_GREETER (greeter), text);
}

static void
cancel_button_clicked (GtkButton        *button,
		       GdmSimpleGreeter *greeter)
{

}

static void
create_greeter (GdmSimpleGreeter *greeter)
{
	GError    *error;
	GtkWidget *dialog;
	GtkWidget *button;

#if 0
	error = NULL;
	g_spawn_command_line_async ("gtk-window-decorator", &error);
	if (error != NULL) {
		g_warning ("Error starting WM: %s", error->message);
		g_error_free (error);
	}

	error = NULL;
	g_spawn_command_line_async ("compiz", &error);
	if (error != NULL) {
		g_warning ("Error starting WM: %s", error->message);
		g_error_free (error);
	}
#else
	error = NULL;
	g_spawn_command_line_async ("metacity", &error);
	if (error != NULL) {
		g_warning ("Error starting WM: %s", error->message);
		g_error_free (error);
	}
#endif

        greeter->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE, NULL, PACKAGE);
	if (greeter->priv->xml == NULL) {
		/* FIXME: */
	}

	dialog = glade_xml_get_widget (greeter->priv->xml, "auth-window");
	if (dialog == NULL) {
		/* FIXME: */
	}

	button = glade_xml_get_widget (greeter->priv->xml, "ok-button");
	if (dialog != NULL) {
		gtk_widget_grab_default (button);
		g_signal_connect (button, "clicked", G_CALLBACK (ok_button_clicked), greeter);
	}

	button = glade_xml_get_widget (greeter->priv->xml, "cancel-button");
	if (dialog != NULL) {
		g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), greeter);
	}

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_window_set_deletable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_decorated (GTK_WINDOW (dialog), FALSE);
	gtk_widget_show (dialog);
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
