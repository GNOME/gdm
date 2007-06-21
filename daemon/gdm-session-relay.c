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
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) && defined (HAVE_SCHED_YIELD)
#include <sched.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-session-relay.h"

#define GDM_SESSION_RELAY_DBUS_PATH      "/org/gnome/DisplayManager/SessionRelay"
#define GDM_SESSION_RELAY_DBUS_INTERFACE "org.gnome.DisplayManager.SessionRelay"

#define GDM_SESSION_RELAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_RELAY, GdmSessionRelayPrivate))

struct GdmSessionRelayPrivate
{
	DBusServer     *server;
	char           *server_address;
	DBusConnection *session_connection;
};

enum {
	PROP_0,
};

enum {
	INFO_QUERY,
	SECRET_INFO_QUERY,
	INFO,
	PROBLEM,
	SESSION_STARTED,
	SESSION_STOPPED,
	READY,
	CONNECTED,
	DISCONNECTED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_session_relay_class_init	(GdmSessionRelayClass *klass);
static void	gdm_session_relay_init	        (GdmSessionRelay      *session_relay);
static void	gdm_session_relay_finalize	(GObject               *object);

G_DEFINE_TYPE (GdmSessionRelay, gdm_session_relay, G_TYPE_OBJECT)

static gboolean
send_dbus_message (DBusConnection *connection,
		   DBusMessage	  *message)
{
	gboolean is_connected;
	gboolean sent;

	g_return_val_if_fail (message != NULL, FALSE);

	if (connection == NULL) {
		g_debug ("There is no valid connection");
		return FALSE;
	}

	is_connected = dbus_connection_get_is_connected (connection);
	if (! is_connected) {
		g_warning ("Not connected!");
		return FALSE;
	}

	sent = dbus_connection_send (connection, message, NULL);

	return sent;
}

static void
send_dbus_string_signal (GdmSessionRelay *session_relay,
			 const char	 *name,
			 const char	 *text)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	g_return_if_fail (session_relay != NULL);

	message = dbus_message_new_signal (GDM_SESSION_RELAY_DBUS_PATH,
					   GDM_SESSION_RELAY_DBUS_INTERFACE,
					   name);

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &text);

	if (! send_dbus_message (session_relay->priv->session_connection, message)) {
		g_debug ("Could not send %s signal", name);
	}

	dbus_message_unref (message);
}

static void
send_dbus_void_signal (GdmSessionRelay *session_relay,
		       const char	 *name)
{
	DBusMessage    *message;

	g_return_if_fail (session_relay != NULL);

	message = dbus_message_new_signal (GDM_SESSION_RELAY_DBUS_PATH,
					   GDM_SESSION_RELAY_DBUS_INTERFACE,
					   name);

	if (! send_dbus_message (session_relay->priv->session_connection, message)) {
		g_debug ("Could not send %s signal", name);
	}

	dbus_message_unref (message);
}

void
gdm_session_relay_open (GdmSessionRelay *session_relay)
{
        send_dbus_void_signal (session_relay, "Open");
}

void
gdm_session_relay_answer_query (GdmSessionRelay *session_relay,
				const char      *text)
{
	g_debug ("Sending signal AnswerQuery: %s", text);
        send_dbus_string_signal (session_relay, "AnswerQuery", text);
}

void
gdm_session_relay_select_session (GdmSessionRelay *session_relay,
				  const char      *text)
{
        send_dbus_string_signal (session_relay, "SessionSelected", text);
}

void
gdm_session_relay_select_language (GdmSessionRelay *session_relay,
				   const char      *text)
{
        send_dbus_string_signal (session_relay, "LanguageSelected", text);
}

void
gdm_session_relay_select_user (GdmSessionRelay *session_relay,
			       const char      *text)
{
        send_dbus_string_signal (session_relay, "UserSelected", text);
}

void
gdm_session_relay_cancel (GdmSessionRelay *session_relay)
{
        send_dbus_void_signal (session_relay, "Cancelled");
}

/* Note: Use abstract sockets like dbus does by default on Linux. Abstract
 * sockets are only available on Linux.
 */
static char *
generate_address (void)
{
	char *path;
#if defined (__linux__)
	int   i;
	char  tmp[9];

	for (i = 0; i < 8; i++) {
		if (g_random_int_range (0, 2) == 0) {
			tmp[i] = g_random_int_range ('a', 'z' + 1);
		} else {
			tmp[i] = g_random_int_range ('A', 'Z' + 1);
		}
	}
	tmp[8] = '\0';

	path = g_strdup_printf ("unix:abstract=/tmp/gdm-session-%s", tmp);
#else
	path = g_strdup ("unix:tmpdir=/tmp/gdm-session");
#endif

	return path;
}

static DBusHandlerResult
handle_info_query (GdmSessionRelay *session_relay,
		   DBusConnection  *connection,
		   DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	g_debug ("InfoQuery: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [INFO_QUERY], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_secret_info_query (GdmSessionRelay *session_relay,
			  DBusConnection  *connection,
			  DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	g_debug ("SecretInfoQuery: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [SECRET_INFO_QUERY], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_info (GdmSessionRelay *session_relay,
	     DBusConnection  *connection,
	     DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	g_debug ("Info: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [INFO], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_problem (GdmSessionRelay *session_relay,
	     DBusConnection  *connection,
	     DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	g_debug ("Problem: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [PROBLEM], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_session_started (GdmSessionRelay *session_relay,
			DBusConnection  *connection,
			DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;

	dbus_error_init (&error);

	g_debug ("SessionStarted");

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [SESSION_STARTED], 0);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_session_stopped (GdmSessionRelay *session_relay,
			DBusConnection  *connection,
			DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;

	dbus_error_init (&error);

	g_debug ("SessionStopped");

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [SESSION_STOPPED], 0);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_ready (GdmSessionRelay *session_relay,
	      DBusConnection  *connection,
	      DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;

	dbus_error_init (&error);

	g_debug ("Ready");

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (session_relay, signals [READY], 0);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_reset (GdmSessionRelay *session_relay,
	      DBusConnection  *connection,
	      DBusMessage     *message)
{
	DBusMessage *reply;
	DBusError    error;

	dbus_error_init (&error);

	g_debug ("Reset");

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	/* FIXME: */
	/*g_signal_emit (session_relay, signals [RESET], 0);*/

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_handle_child_message (DBusConnection *connection,
			      DBusMessage    *message,
			      void           *user_data)
{
        GdmSessionRelay *session_relay = GDM_SESSION_RELAY (user_data);

	if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "InfoQuery")) {
		return handle_info_query (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "SecretInfoQuery")) {
		return handle_secret_info_query (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "Info")) {
		return handle_info (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "Problem")) {
		return handle_problem (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "SessionStarted")) {
		return handle_session_started (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "SessionStopped")) {
		return handle_session_started (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "Ready")) {
		return handle_ready (session_relay, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_RELAY_DBUS_INTERFACE, "Reset")) {
		return handle_reset (session_relay, connection, message);
	}

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult
do_introspect (DBusConnection *connection,
	       DBusMessage    *message)
{
	DBusMessage *reply;
	GString	    *xml;
	char	    *xml_string;

	g_debug ("Do introspect");

	/* standard header */
	xml = g_string_new ("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
			    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
			    "<node>\n"
			    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
			    "	 <method name=\"Introspect\">\n"
			    "	   <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
			    "	 </method>\n"
			    "  </interface>\n");

	/* interface */
	xml = g_string_append (xml,
			       "  <interface name=\"org.gnome.DisplayManager.SessionRelay\">\n"
			       "    <method name=\"UserVerified\">\n"
			       "    </method>\n"
			       "    <method name=\"InfoQuery\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SecretInfoQuery\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"Info\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"Problem\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SessionStarted\">\n"
			       "    </method>\n"
			       "    <method name=\"SessionStopped\">\n"
			       "    </method>\n"
			       "    <method name=\"Ready\">\n"
			       "    </method>\n"
			       "    <method name=\"Reset\">\n"
			       "    </method>\n"
			       "    <signal name=\"Open\">\n"
			       "    </signal>\n"
			       "    <signal name=\"AnswerQuery\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"LanguageSelected\">\n"
			       "      <arg name=\"language\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"SessionSelected\">\n"
			       "      <arg name=\"session\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"UserSelected\">\n"
			       "      <arg name=\"session\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"Cancelled\">\n"
			       "      <arg name=\"session\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "  </interface>\n");

	reply = dbus_message_new_method_return (message);

	xml = g_string_append (xml, "</node>\n");
	xml_string = g_string_free (xml, FALSE);

	dbus_message_append_args (reply,
				  DBUS_TYPE_STRING, &xml_string,
				  DBUS_TYPE_INVALID);

	g_free (xml_string);

	if (reply == NULL) {
		g_error ("No memory");
	}

	if (! dbus_connection_send (connection, reply, NULL)) {
		g_error ("No memory");
	}

	dbus_message_unref (reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_relay_message_handler (DBusConnection  *connection,
				DBusMessage     *message,
				void            *user_data)
{
	g_debug ("session_relay_message_handler: destination=%s obj_path=%s interface=%s method=%s",
		 dbus_message_get_destination (message),
		 dbus_message_get_path (message),
		 dbus_message_get_interface (message),
		 dbus_message_get_member (message));


        if (dbus_message_is_method_call (message, "org.freedesktop.DBus", "AddMatch")) {
                DBusMessage *reply;

                reply = dbus_message_new_method_return (message);

                if (reply == NULL) {
                        g_error ("No memory");
                }

                if (! dbus_connection_send (connection, reply, NULL)) {
                        g_error ("No memory");
                }

                dbus_message_unref (reply);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
                   strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {

                /*dbus_connection_unref (connection);*/

                return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call (message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
		return do_introspect (connection, message);
        } else {
                return session_handle_child_message (connection, message, user_data);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
session_relay_unregister_handler (DBusConnection  *connection,
				   void            *user_data)
{
	g_debug ("session_relay_unregister_handler");
}

static DBusHandlerResult
connection_filter_function (DBusConnection *connection,
			    DBusMessage    *message,
			    void	   *user_data)
{
        GdmSessionRelay *session_relay = GDM_SESSION_RELAY (user_data);
	const char      *path;

	path = dbus_message_get_path (message);

	g_debug ("obj_path=%s interface=%s method=%s",
		 dbus_message_get_path (message),
		 dbus_message_get_interface (message),
		 dbus_message_get_member (message));

	if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected")
	    && strcmp (path, DBUS_PATH_LOCAL) == 0) {

		g_debug ("Disconnected");

		dbus_connection_unref (connection);
		session_relay->priv->session_connection = NULL;
	} else if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {


	} else {
		return session_relay_message_handler (connection, message, user_data);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
	if (uid == 0) {
		return TRUE;
	}

	return FALSE;
}

static void
handle_connection (DBusServer      *server,
		   DBusConnection  *new_connection,
		   void            *user_data)
{
        GdmSessionRelay *session_relay = GDM_SESSION_RELAY (user_data);

	g_debug ("Handing new connection");

	if (session_relay->priv->session_connection == NULL) {
		DBusObjectPathVTable vtable = { &session_relay_unregister_handler,
						&session_relay_message_handler,
						NULL, NULL, NULL, NULL
		};

		session_relay->priv->session_connection = new_connection;
		dbus_connection_ref (new_connection);
		dbus_connection_setup_with_g_main (new_connection, NULL);

		g_debug ("session connection is %p", new_connection);

		dbus_connection_add_filter (new_connection,
					    connection_filter_function,
					    session_relay,
					    NULL);

		dbus_connection_set_unix_user_function (new_connection,
							allow_user_function,
							session_relay,
							NULL);

		dbus_connection_register_object_path (new_connection,
						      GDM_SESSION_RELAY_DBUS_PATH,
						      &vtable,
						      session_relay);

		g_signal_emit (session_relay, signals[CONNECTED], 0);

	}
}

gboolean
gdm_session_relay_start (GdmSessionRelay *session_relay)
{
	DBusError   error;
	gboolean    ret;
	char       *address;
	const char *auth_mechanisms[] = {"EXTERNAL", NULL};

	ret = FALSE;

	g_debug ("Creating D-Bus relay for session");

	address = generate_address ();

	dbus_error_init (&error);
	session_relay->priv->server = dbus_server_listen (address, &error);
	g_free (address);

	if (session_relay->priv->server == NULL) {
		g_warning ("Cannot create D-BUS relay for the session: %s", error.message);
		goto out;
	}

	dbus_server_setup_with_g_main (session_relay->priv->server, NULL);
	dbus_server_set_auth_mechanisms (session_relay->priv->server, auth_mechanisms);
	dbus_server_set_new_connection_function (session_relay->priv->server,
						 handle_connection,
						 session_relay,
						 NULL);
	ret = TRUE;

	g_free (session_relay->priv->server_address);
	session_relay->priv->server_address = dbus_server_get_address (session_relay->priv->server);

	g_debug ("D-Bus relay listening on %s", session_relay->priv->server_address);

 out:

	return ret;
}

gboolean
gdm_session_relay_stop (GdmSessionRelay *session_relay)
{
	gboolean ret;

	ret = FALSE;

	g_debug ("Stopping session relay...");

	return ret;
}

char *
gdm_session_relay_get_address (GdmSessionRelay *session_relay)
{
        return g_strdup (session_relay->priv->server_address);
}

static void
gdm_session_relay_set_property (GObject      *object,
				guint	      prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GdmSessionRelay *self;

	self = GDM_SESSION_RELAY (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_session_relay_get_property (GObject    *object,
				guint       prop_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GdmSessionRelay *self;

	self = GDM_SESSION_RELAY (object);

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_session_relay_constructor (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
        GdmSessionRelay      *session_relay;
        GdmSessionRelayClass *klass;

        klass = GDM_SESSION_RELAY_CLASS (g_type_class_peek (GDM_TYPE_SESSION_RELAY));

        session_relay = GDM_SESSION_RELAY (G_OBJECT_CLASS (gdm_session_relay_parent_class)->constructor (type,
													    n_construct_properties,
													    construct_properties));

        return G_OBJECT (session_relay);
}

static void
gdm_session_relay_class_init (GdmSessionRelayClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_session_relay_get_property;
	object_class->set_property = gdm_session_relay_set_property;
        object_class->constructor = gdm_session_relay_constructor;
	object_class->finalize = gdm_session_relay_finalize;

	g_type_class_add_private (klass, sizeof (GdmSessionRelayPrivate));

	signals [INFO_QUERY] =
		g_signal_new ("info-query",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, info_query),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [SECRET_INFO_QUERY] =
		g_signal_new ("secret-info-query",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, secret_info_query),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [INFO] =
		g_signal_new ("info",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, info),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [PROBLEM] =
		g_signal_new ("problem",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, problem),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [SESSION_STARTED] =
		g_signal_new ("session-started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, session_started),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [READY] =
		g_signal_new ("ready",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, ready),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [CONNECTED] =
		g_signal_new ("connected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, connected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [DISCONNECTED] =
		g_signal_new ("disconnected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionRelayClass, disconnected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
gdm_session_relay_init (GdmSessionRelay *session_relay)
{

	session_relay->priv = GDM_SESSION_RELAY_GET_PRIVATE (session_relay);
}

static void
gdm_session_relay_finalize (GObject *object)
{
	GdmSessionRelay *session_relay;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SESSION_RELAY (object));

	session_relay = GDM_SESSION_RELAY (object);

	g_return_if_fail (session_relay->priv != NULL);

	gdm_session_relay_stop (session_relay);

	G_OBJECT_CLASS (gdm_session_relay_parent_class)->finalize (object);
}

GdmSessionRelay *
gdm_session_relay_new (void)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SESSION_RELAY,
			       NULL);

	return GDM_SESSION_RELAY (object);
}
