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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"
#include "gdm-log.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-greeter.h"
#include "gdm-simple-greeter.h"

#define SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

#define GPM_DBUS_NAME      "org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH      "/org/freedesktop/PowerManagement"
#define GPM_DBUS_INTERFACE "org.freedesktop.PowerManagement"

static DBusGConnection  *connection     = NULL;
static GdmGreeter       *greeter        = NULL;
static DBusGProxy       *server_proxy   = NULL;
static DBusGProxy       *gpm_proxy      = NULL;

static void
on_info (DBusGProxy *proxy,
	 const char *text,
	 gpointer    data)
{
	g_debug ("GREETER INFO: %s", text);

	gdm_greeter_info (GDM_GREETER (greeter), text);
}

static void
on_problem (DBusGProxy *proxy,
	    const char *text,
	    gpointer    data)
{
	g_debug ("GREETER PROBLEM: %s", text);

	gdm_greeter_problem (GDM_GREETER (greeter), text);
}

static void
on_reset (DBusGProxy *proxy,
	  gpointer    data)
{
	g_debug ("GREETER RESET");

	gdm_greeter_reset (GDM_GREETER (greeter));
}

static void
on_info_query (DBusGProxy *proxy,
	       const char *text,
	       gpointer    data)
{
	g_debug ("GREETER Info query: %s", text);

	gdm_greeter_info_query (GDM_GREETER (greeter), text);
}

static void
on_secret_info_query (DBusGProxy *proxy,
		      const char *text,
		      gpointer    data)
{
	g_debug ("GREETER Secret info query: %s", text);

	gdm_greeter_secret_info_query (GDM_GREETER (greeter), text);
}

static void
on_query_answer (GdmGreeter *greeter,
		 const char *text,
		 gpointer    data)
{
	gboolean res;
	GError  *error;

	g_debug ("GREETER answer: %s", text);

	error = NULL;
	res = dbus_g_proxy_call (server_proxy,
				 "AnswerQuery",
				 &error,
				 G_TYPE_STRING, text,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send AnswerQuery: %s", error->message);
		g_error_free (error);
	}
}

static void
on_select_session (GdmGreeter *greeter,
		   const char *text,
		   gpointer    data)
{
	gboolean res;
	GError  *error;

	g_debug ("GREETER session selected: %s", text);

	error = NULL;
	res = dbus_g_proxy_call (server_proxy,
				 "SelectSession",
				 &error,
				 G_TYPE_STRING, text,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SelectSession: %s", error->message);
		g_error_free (error);
	}
}

static void
on_select_language (GdmGreeter *greeter,
		    const char *text,
		    gpointer    data)
{
	gboolean res;
	GError  *error;

	g_debug ("GREETER session selected: %s", text);

	error = NULL;
	res = dbus_g_proxy_call (server_proxy,
				 "SelectLanguage",
				 &error,
				 G_TYPE_STRING, text,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SelectLanguage: %s", error->message);
		g_error_free (error);
	}
}

static void
on_select_user (GdmGreeter *greeter,
		const char *text,
		gpointer    data)
{
	gboolean res;
	GError  *error;

	g_debug ("GREETER user selected: %s", text);

	error = NULL;
	res = dbus_g_proxy_call (server_proxy,
				 "SelectUser",
				 &error,
				 G_TYPE_STRING, text,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SelectUser: %s", error->message);
		g_error_free (error);
	}
}

static void
on_cancelled (GdmGreeter *greeter,
	      gpointer    data)
{
	gboolean res;
	GError  *error;

	g_debug ("GREETER cancelled");

	error = NULL;
	res = dbus_g_proxy_call (server_proxy,
				 "Cancel",
				 &error,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send Cancelled: %s", error->message);
		g_error_free (error);
	}
}

static void
proxy_destroyed (GObject *object,
		 gpointer data)
{
	g_debug ("GREETER Proxy disconnected");
}

static void
activate_power_manager (void)
{
	DBusGConnection *connection;
	GError          *error;
	gboolean         res;
	guint            result;

	g_debug ("Activating power management");

	error = NULL;
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	gpm_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);
	error = NULL;
	res = dbus_g_proxy_call (gpm_proxy,
				 "StartServiceByName",
				 &error,
				 G_TYPE_STRING, GPM_DBUS_NAME,
				 G_TYPE_UINT, 0,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &result,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Could not start service: %s", error->message);
		g_error_free (error);
	} else {
		g_debug ("Result %u", result);
	}
}

int
main (int argc, char *argv[])
{
	GError           *error;
	const char       *address;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	setlocale (LC_ALL, "");

	g_type_init ();

        if (! gdm_settings_client_init (GDMCONFDIR "/gdm.schemas", "/")) {
                exit (1);
        }

	/*
	 * gdm_common_atspi_launch () needs gdk initialized.
	 * We cannot start gtk before the registry is running
	 * because the atk-bridge will crash.
	 */
	gdk_init (&argc, &argv);
	/*gdm_common_atspi_launch ();*/
	gtk_init (&argc, &argv);

	gdm_log_init ();

	/*gdm_common_log_set_debug (gdm_settings_client_get_bool (GDM_KEY_DEBUG));*/
	gdm_log_set_debug (TRUE);

	address = g_getenv ("GDM_GREETER_DBUS_ADDRESS");
	if (address == NULL) {
		g_warning ("GDM_GREETER_DBUS_ADDRESS not set");
		exit (1);
	}

	g_debug ("GREETER connecting to address: %s", address);

        error = NULL;
        connection = dbus_g_connection_open (address, &error);
        if (connection == NULL) {
                if (error != NULL) {
                        g_warning ("error opening connection: %s", error->message);
                        g_error_free (error);
                } else {
			g_warning ("Unable to open connection");
		}
		exit (1);
        }

	g_debug ("GREETER creating proxy for peer: %s", SERVER_DBUS_PATH);
        server_proxy = dbus_g_proxy_new_for_peer (connection,
						  SERVER_DBUS_PATH,
						  SERVER_DBUS_INTERFACE);
	if (server_proxy == NULL) {
		g_warning ("Unable to create proxy for peer");
		exit (1);
	}

	g_signal_connect (server_proxy, "destroy", G_CALLBACK (proxy_destroyed), NULL);

	/* FIXME: not sure why introspection isn't working */
	dbus_g_proxy_add_signal (server_proxy, "InfoQuery", G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (server_proxy, "SecretInfoQuery", G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (server_proxy, "Info", G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (server_proxy, "Problem", G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (server_proxy, "Reset", G_TYPE_INVALID);

	dbus_g_proxy_connect_signal (server_proxy,
				     "InfoQuery",
				     G_CALLBACK (on_info_query),
				     NULL,
				     NULL);
	dbus_g_proxy_connect_signal (server_proxy,
				     "SecretInfoQuery",
				     G_CALLBACK (on_secret_info_query),
				     NULL,
				     NULL);
	dbus_g_proxy_connect_signal (server_proxy,
				     "Info",
				     G_CALLBACK (on_info),
				     NULL,
				     NULL);
	dbus_g_proxy_connect_signal (server_proxy,
				     "Problem",
				     G_CALLBACK (on_problem),
				     NULL,
				     NULL);
	dbus_g_proxy_connect_signal (server_proxy,
				     "Reset",
				     G_CALLBACK (on_reset),
				     NULL,
				     NULL);

	greeter = gdm_simple_greeter_new ();
	g_signal_connect (greeter,
			  "query-answer",
			  G_CALLBACK (on_query_answer),
			  NULL);
	g_signal_connect (greeter,
			  "session-selected",
			  G_CALLBACK (on_select_session),
			  NULL);
	g_signal_connect (greeter,
			  "language-selected",
			  G_CALLBACK (on_select_language),
			  NULL);
	g_signal_connect (greeter,
			  "user-selected",
			  G_CALLBACK (on_select_user),
			  NULL);
	g_signal_connect (greeter,
			  "cancelled",
			  G_CALLBACK (on_cancelled),
			  NULL);

	activate_power_manager ();

	gtk_main ();

	if (greeter != NULL) {
		g_object_unref (greeter);
	}

	return 0;
}
