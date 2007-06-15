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

#include <X11/Xlib.h> /* for Display */

#include "gdm-common.h"
#include "filecheck.h"

#include "gdm-greeter-server.h"

#define GDM_GREETER_SERVER_DBUS_PATH      "/org/gnome/DisplayManager/GreeterServer"
#define GDM_GREETER_SERVER_DBUS_INTERFACE "org.gnome.DisplayManager.GreeterServer"

extern char **environ;

#define GDM_GREETER_SERVER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_SERVER, GdmGreeterServerPrivate))

struct GdmGreeterServerPrivate
{
	char           *user_name;
	char           *group_name;

	char           *x11_display_name;
	char           *x11_authority_file;

	gboolean        interrupted;
	gboolean        always_restart_greeter;

	DBusServer     *server;
	char           *server_address;
	DBusConnection *greeter_connection;
};

enum {
	PROP_0,
	PROP_X11_DISPLAY_NAME,
	PROP_X11_AUTHORITY_FILE,
	PROP_USER_NAME,
	PROP_GROUP_NAME,
};

enum {
	QUERY_ANSWER,
	SESSION_SELECTED,
	LANGUAGE_SELECTED,
	CONNECTED,
	DISCONNECTED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_greeter_server_class_init	(GdmGreeterServerClass *klass);
static void	gdm_greeter_server_init	(GdmGreeterServer      *greeter_server);
static void	gdm_greeter_server_finalize	(GObject         *object);

G_DEFINE_TYPE (GdmGreeterServer, gdm_greeter_server, G_TYPE_OBJECT)

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
send_dbus_string_signal (GdmGreeterServer *greeter_server,
			 const char	 *name,
			 const char	 *text)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	g_return_if_fail (greeter_server != NULL);

	message = dbus_message_new_signal (GDM_GREETER_SERVER_DBUS_PATH,
					   GDM_GREETER_SERVER_DBUS_INTERFACE,
					   name);

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &text);

	if (! send_dbus_message (greeter_server->priv->greeter_connection, message)) {
		g_debug ("Could not send %s signal", name);
	}

	dbus_message_unref (message);
}

gboolean
gdm_greeter_server_info_query (GdmGreeterServer *greeter_server,
			      const char      *text)
{
        send_dbus_string_signal (greeter_server, "InfoQuery", text);

	return TRUE;
}

gboolean
gdm_greeter_server_secret_info_query (GdmGreeterServer *greeter_server,
				     const char      *text)
{
        send_dbus_string_signal (greeter_server, "SecretInfoQuery", text);
	return TRUE;
}

gboolean
gdm_greeter_server_info (GdmGreeterServer *greeter_server,
			const char      *text)
{
        send_dbus_string_signal (greeter_server, "Info", text);
	return TRUE;
}

gboolean
gdm_greeter_server_problem (GdmGreeterServer *greeter_server,
			   const char      *text)
{
        send_dbus_string_signal (greeter_server, "Problem", text);
	return TRUE;
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

	path = g_strdup_printf ("unix:abstract=/tmp/gdm-greeter-%s", tmp);
#else
	path = g_strdup ("unix:tmpdir=/tmp/gdm-greeter");
#endif

	return path;
}

static DBusHandlerResult
handle_answer_query (GdmGreeterServer *greeter_server,
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

	g_debug ("AnswerQuery: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (greeter_server, signals [QUERY_ANSWER], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_select_session (GdmGreeterServer *greeter_server,
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

	g_debug ("SelectSession: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (greeter_server, signals [SESSION_SELECTED], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_select_language (GdmGreeterServer *greeter_server,
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

	g_debug ("SelectLanguage: %s", text);

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_signal_emit (greeter_server, signals [LANGUAGE_SELECTED], 0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
greeter_handle_child_message (DBusConnection *connection,
			      DBusMessage    *message,
			      void           *user_data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (user_data);

	if (dbus_message_is_method_call (message, GDM_GREETER_SERVER_DBUS_INTERFACE, "AnswerQuery")) {
		return handle_answer_query (greeter_server, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_GREETER_SERVER_DBUS_INTERFACE, "SelectSession")) {
		return handle_select_session (greeter_server, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_GREETER_SERVER_DBUS_INTERFACE, "SelectSession")) {
		return handle_select_language (greeter_server, connection, message);
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
			       "  <interface name=\"org.gnome.DisplayManager.GreeterServer\">\n"
			       "    <method name=\"AnswerQuery\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SelectSession\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SelectLanguage\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <signal name=\"Info\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"Problem\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"InfoQuery\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"SecretInfoQuery\">\n"
			       "      <arg name=\"text\" type=\"s\"/>\n"
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
greeter_server_message_handler (DBusConnection  *connection,
				DBusMessage     *message,
				void            *user_data)
{
	g_debug ("greeter_server_message_handler: destination=%s obj_path=%s interface=%s method=%s",
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
                return greeter_handle_child_message (connection, message, user_data);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
greeter_server_unregister_handler (DBusConnection  *connection,
				   void            *user_data)
{
	g_debug ("greeter_server_unregister_handler");
}

static DBusHandlerResult
connection_filter_function (DBusConnection *connection,
			    DBusMessage    *message,
			    void	   *user_data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (user_data);
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
		greeter_server->priv->greeter_connection = NULL;
	} else if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {


	} else {
		return greeter_server_message_handler (connection, message, user_data);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (data);
	struct passwd   *pwent;

	if (greeter_server->priv->user_name == NULL) {
		return FALSE;
	}

	pwent = getpwnam (greeter_server->priv->user_name);
	if (pwent == NULL) {
		return FALSE;
	}

	if (pwent->pw_uid == uid) {
		return TRUE;
	}

	return FALSE;
}

static void
handle_connection (DBusServer      *server,
		   DBusConnection  *new_connection,
		   void            *user_data)
{
        GdmGreeterServer *greeter_server = GDM_GREETER_SERVER (user_data);

	g_debug ("Handing new connection");

	if (greeter_server->priv->greeter_connection == NULL) {
		DBusObjectPathVTable vtable = { &greeter_server_unregister_handler,
						&greeter_server_message_handler,
						NULL, NULL, NULL, NULL
		};

		greeter_server->priv->greeter_connection = new_connection;
		dbus_connection_ref (new_connection);
		dbus_connection_setup_with_g_main (new_connection, NULL);

		g_debug ("greeter connection is %p", new_connection);

		dbus_connection_add_filter (new_connection,
					    connection_filter_function,
					    greeter_server,
					    NULL);

		dbus_connection_set_unix_user_function (new_connection,
							allow_user_function,
							greeter_server,
							NULL);

		dbus_connection_register_object_path (new_connection,
						      GDM_GREETER_SERVER_DBUS_PATH,
						      &vtable,
						      greeter_server);

		g_signal_emit (greeter_server, signals[CONNECTED], 0);

	}
}

gboolean
gdm_greeter_server_start (GdmGreeterServer *greeter_server)
{
	DBusError   error;
	gboolean    ret;
	char       *address;
	const char *auth_mechanisms[] = {"EXTERNAL", NULL};

	ret = FALSE;

	g_debug ("Creating D-Bus server for greeter");

	address = generate_address ();

	dbus_error_init (&error);
	greeter_server->priv->server = dbus_server_listen (address, &error);
	g_free (address);

	if (greeter_server->priv->server == NULL) {
		g_warning ("Cannot create D-BUS server for the greeter: %s", error.message);
		goto out;
	}

	dbus_server_setup_with_g_main (greeter_server->priv->server, NULL);
	dbus_server_set_auth_mechanisms (greeter_server->priv->server, auth_mechanisms);
	dbus_server_set_new_connection_function (greeter_server->priv->server,
						 handle_connection,
						 greeter_server,
						 NULL);
	ret = TRUE;

	g_free (greeter_server->priv->server_address);
	greeter_server->priv->server_address = dbus_server_get_address (greeter_server->priv->server);

	g_debug ("D-Bus server listening on %s", greeter_server->priv->server_address);

 out:

	return ret;
}

gboolean
gdm_greeter_server_stop (GdmGreeterServer *greeter_server)
{
	gboolean ret;

	ret = FALSE;

	g_debug ("Stopping greeter server...");

	return ret;
}

char *
gdm_greeter_server_get_address (GdmGreeterServer *greeter_server)
{
        return g_strdup (greeter_server->priv->server_address);
}

static void
_gdm_greeter_server_set_x11_display_name (GdmGreeterServer *greeter_server,
					 const char *name)
{
        g_free (greeter_server->priv->x11_display_name);
        greeter_server->priv->x11_display_name = g_strdup (name);
}

static void
_gdm_greeter_server_set_x11_authority_file (GdmGreeterServer *greeter_server,
					   const char *file)
{
        g_free (greeter_server->priv->x11_authority_file);
        greeter_server->priv->x11_authority_file = g_strdup (file);
}

static void
_gdm_greeter_server_set_user_name (GdmGreeterServer *greeter_server,
				  const char *name)
{
        g_free (greeter_server->priv->user_name);
        greeter_server->priv->user_name = g_strdup (name);
}

static void
_gdm_greeter_server_set_group_name (GdmGreeterServer *greeter_server,
				    const char *name)
{
        g_free (greeter_server->priv->group_name);
        greeter_server->priv->group_name = g_strdup (name);
}

static void
gdm_greeter_server_set_property (GObject      *object,
				guint	      prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GdmGreeterServer *self;

	self = GDM_GREETER_SERVER (object);

	switch (prop_id) {
	case PROP_X11_DISPLAY_NAME:
		_gdm_greeter_server_set_x11_display_name (self, g_value_get_string (value));
		break;
	case PROP_X11_AUTHORITY_FILE:
		_gdm_greeter_server_set_x11_authority_file (self, g_value_get_string (value));
		break;
	case PROP_USER_NAME:
		_gdm_greeter_server_set_user_name (self, g_value_get_string (value));
		break;
	case PROP_GROUP_NAME:
		_gdm_greeter_server_set_group_name (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_greeter_server_get_property (GObject    *object,
				guint       prop_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GdmGreeterServer *self;

	self = GDM_GREETER_SERVER (object);

	switch (prop_id) {
	case PROP_X11_DISPLAY_NAME:
		g_value_set_string (value, self->priv->x11_display_name);
		break;
	case PROP_X11_AUTHORITY_FILE:
		g_value_set_string (value, self->priv->x11_authority_file);
		break;
	case PROP_USER_NAME:
		g_value_set_string (value, self->priv->user_name);
		break;
	case PROP_GROUP_NAME:
		g_value_set_string (value, self->priv->group_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_greeter_server_constructor (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
        GdmGreeterServer      *greeter_server;
        GdmGreeterServerClass *klass;

        klass = GDM_GREETER_SERVER_CLASS (g_type_class_peek (GDM_TYPE_GREETER_SERVER));

        greeter_server = GDM_GREETER_SERVER (G_OBJECT_CLASS (gdm_greeter_server_parent_class)->constructor (type,
										       n_construct_properties,
										       construct_properties));

        return G_OBJECT (greeter_server);
}

static void
gdm_greeter_server_class_init (GdmGreeterServerClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_greeter_server_get_property;
	object_class->set_property = gdm_greeter_server_set_property;
        object_class->constructor = gdm_greeter_server_constructor;
	object_class->finalize = gdm_greeter_server_finalize;

	g_type_class_add_private (klass, sizeof (GdmGreeterServerPrivate));

	g_object_class_install_property (object_class,
					 PROP_X11_DISPLAY_NAME,
					 g_param_spec_string ("x11-display-name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_X11_AUTHORITY_FILE,
					 g_param_spec_string ("x11-authority-file",
							      "authority file",
							      "authority file",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_USER_NAME,
					 g_param_spec_string ("user-name",
							      "user name",
							      "user name",
							      "gdm",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_GROUP_NAME,
					 g_param_spec_string ("group-name",
							      "group name",
							      "group name",
							      "gdm",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	signals [QUERY_ANSWER] =
		g_signal_new ("query-answer",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterServerClass, query_answer),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [SESSION_SELECTED] =
		g_signal_new ("session-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterServerClass, session_selected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [LANGUAGE_SELECTED] =
		g_signal_new ("language-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterServerClass, language_selected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals [CONNECTED] =
		g_signal_new ("connected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterServerClass, connected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [DISCONNECTED] =
		g_signal_new ("disconnected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmGreeterServerClass, disconnected),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
gdm_greeter_server_init (GdmGreeterServer *greeter_server)
{

	greeter_server->priv = GDM_GREETER_SERVER_GET_PRIVATE (greeter_server);
}

static void
gdm_greeter_server_finalize (GObject *object)
{
	GdmGreeterServer *greeter_server;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_GREETER_SERVER (object));

	greeter_server = GDM_GREETER_SERVER (object);

	g_return_if_fail (greeter_server->priv != NULL);

	gdm_greeter_server_stop (greeter_server);

	G_OBJECT_CLASS (gdm_greeter_server_parent_class)->finalize (object);
}

GdmGreeterServer *
gdm_greeter_server_new (const char *display_name)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_GREETER_SERVER,
			       "x11-display-name", display_name,
			       NULL);

	return GDM_GREETER_SERVER (object);
}
