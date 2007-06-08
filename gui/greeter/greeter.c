/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-common.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-themed-greeter.h"

#include "gdmcommon.h"

#define SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

static DBusGProxy       *settings_proxy = NULL;
static DBusGConnection  *connection     = NULL;
static GdmThemedGreeter *greeter        = NULL;
static DBusGProxy       *server_proxy   = NULL;

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
proxy_destroyed (GObject *object,
		 gpointer data)
{
	g_debug ("GREETER Proxy disconnected");
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
	gdm_common_atspi_launch ();
	gtk_init (&argc, &argv);

	gdm_common_log_init ();

	/*gdm_common_log_set_debug (gdm_settings_client_get_bool (GDM_KEY_DEBUG));*/
	gdm_common_log_set_debug (TRUE);

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

	greeter = gdm_themed_greeter_new ();
	g_signal_connect (greeter,
			  "query-answer",
			  G_CALLBACK (on_query_answer),
			  NULL);
	gtk_main ();

	if (greeter != NULL) {
		g_object_unref (greeter);
	}

	return 0;
}
