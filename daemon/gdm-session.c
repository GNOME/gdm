/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * session.c - authenticates and authorizes users with system
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * TODO:                - close should be nicer and shutdown
 *                        pam etc
 *                      - message validation code is a noop right now.
 *                        either fix the validate functions, or drop them
 *                        (and the sentinal magic)
 *                      - audit libs (linux and solaris) support
 */

#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-session.h"
#include "gdm-session-worker-job.h"

#define GDM_SESSION_DBUS_PATH      "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_INTERFACE "org.gnome.DisplayManager.Session"


#ifndef GDM_BAD_SESSION_RECORDS_FILE
#define	GDM_BAD_SESSION_RECORDS_FILE "/var/log/btmp"
#endif

#ifndef GDM_NEW_SESSION_RECORDS_FILE
#define	GDM_NEW_SESSION_RECORDS_FILE "/var/log/wtmp"
#endif

#ifndef GDM_MAX_OPEN_FILE_DESCRIPTORS
#define GDM_MAX_OPEN_FILE_DESCRIPTORS 1024
#endif

#ifndef GDM_OPEN_FILE_DESCRIPTORS_DIR
#define GDM_OPEN_FILE_DESCRIPTORS_DIR "/proc/self/fd"
#endif

#ifndef GDM_MAX_MESSAGE_SIZE
#define GDM_MAX_MESSAGE_SIZE (8192)
#endif

#ifndef GDM_MAX_SERVICE_NAME_SIZE
#define GDM_MAX_SERVICE_NAME_SIZE 512
#endif

#ifndef GDM_MAX_CONSOLE_NAME_SIZE
#define GDM_MAX_CONSOLE_NAME_SIZE 1024
#endif

#ifndef GDM_MAX_HOSTNAME_SIZE
#define GDM_MAX_HOSTNAME_SIZE 1024
#endif

#ifndef GDM_MAX_LOG_FILENAME_SIZE
#define GDM_MAX_LOG_FILENAME_SIZE 1024
#endif


typedef enum _GdmSessionRecordType {
	GDM_SESSION_RECORD_TYPE_LOGIN,
	GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT,
	GDM_SESSION_RECORD_TYPE_LOGOUT,
} GdmSessionRecordType;

struct _GdmSessionPrivate
{
	GdmSessionWorkerJob *job;
	GPid                 session_pid;

	char                *service_name;
	char                *username;
	char                *hostname;
	char                *console_name;

	DBusMessage         *message_pending_reply;

	GHashTable          *environment;

	DBusServer          *server;
	char                *server_address;
	DBusConnection      *worker_connection;

	guint32              is_verified : 1;
	guint32              is_running : 1;
};

enum {
	USER_VERIFIED = 0,
	USER_VERIFICATION_ERROR,
	INFO,
	PROBLEM,
	INFO_QUERY,
	SECRET_INFO_QUERY,
	SESSION_STARTED,
	SESSION_STARTUP_ERROR,
	SESSION_EXITED,
	SESSION_DIED,
	OPENED,
	CLOSED,
	LAST_SIGNAL
};

static guint gdm_session_signals [LAST_SIGNAL];

G_DEFINE_TYPE (GdmSession, gdm_session, G_TYPE_OBJECT);

GQuark
gdm_session_error_quark (void)
{
	static GQuark error_quark = 0;

	if (error_quark == 0)
		error_quark = g_quark_from_static_string ("gdm-session");

	return error_quark;
}

static gboolean
send_dbus_message (DBusConnection *connection,
		   DBusMessage	  *message)
{
	gboolean is_connected;
	gboolean sent;

	g_return_val_if_fail (message != NULL, FALSE);

	if (connection == NULL) {
		g_warning ("There is no valid connection");
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
send_dbus_string_signal (GdmSession *session,
			 const char *name,
			 const char *text)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	g_return_if_fail (session != NULL);

	message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
					   GDM_SESSION_DBUS_INTERFACE,
					   name);

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &text);

	if (! send_dbus_message (session->priv->worker_connection, message)) {
		g_debug ("Could not send %s signal", name);
	}

	dbus_message_unref (message);
}

static void
send_dbus_void_signal (GdmSession *session,
		       const char *name)
{
	DBusMessage    *message;

	g_return_if_fail (session != NULL);

	message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
					   GDM_SESSION_DBUS_INTERFACE,
					   name);

	if (! send_dbus_message (session->priv->worker_connection, message)) {
		g_debug ("Could not send %s signal", name);
	}

	dbus_message_unref (message);
}

static void
gdm_session_write_record (GdmSession           *session,
			  GdmSessionRecordType  record_type)
{
	struct utmp session_record = { 0 };
	GTimeVal now = { 0 };
	char *hostname;

	g_debug ("writing %s record",
		 record_type == GDM_SESSION_RECORD_TYPE_LOGIN? "session" :
		 record_type == GDM_SESSION_RECORD_TYPE_LOGOUT? "logout" :
		 "failed session attempt");

	if (record_type != GDM_SESSION_RECORD_TYPE_LOGOUT) {
		/* it's possible that PAM failed before it mapped the user input
		 * into a valid username, so we fallback to try using "(unknown)"
		 */
		if (session->priv->username != NULL)
			strncpy (session_record.ut_user, session->priv->username,
				 sizeof (session_record.ut_user));
		else
			strncpy (session_record.ut_user, "(unknown)",
				 sizeof (session_record.ut_user));
	}

	g_debug ("using username %.*s",
		 sizeof (session_record.ut_user),
		 session_record.ut_user);

	/* FIXME: I have no idea what to do for ut_id.
	 */
	strncpy (session_record.ut_id,
		 session->priv->console_name +
		 strlen (session->priv->console_name) -
		 sizeof (session_record.ut_id),
		 sizeof (session_record.ut_id));

	g_debug ("using id %.*s", sizeof (session_record.ut_id), session_record.ut_id);

	if (g_str_has_prefix (session->priv->console_name, "/dev/")) {
		strncpy (session_record.ut_line,
			 session->priv->console_name + strlen ("/dev/"),
			 sizeof (session_record.ut_line));
	} else if (g_str_has_prefix (session->priv->console_name, ":")) {
		strncpy (session_record.ut_line,
			 session->priv->console_name,
			 sizeof (session_record.ut_line));
	}

	g_debug ("using line %.*s",
		 sizeof (session_record.ut_line),
		 session_record.ut_line);

	/* FIXME: this is a bit of a mess. Figure out how
	 * wrong the logic is
	 */
	hostname = NULL;
	if ((session->priv->hostname != NULL) &&
	    g_str_has_prefix (session->priv->console_name, ":"))
		hostname = g_strdup_printf ("%s%s", session->priv->hostname,
					    session->priv->console_name);
	else if ((session->priv->hostname != NULL) &&
		 !strstr (session->priv->console_name, ":"))
		hostname = g_strdup (session->priv->hostname);
	else if (!g_str_has_prefix (session->priv->console_name, ":") &&
		 strstr (session->priv->console_name, ":"))
		hostname = g_strdup (session->priv->console_name);

	if (hostname) {
		g_debug ("using hostname %.*s",
			 sizeof (session_record.ut_host),
			 session_record.ut_host);
		strncpy (session_record.ut_host,
			 hostname, sizeof (session_record.ut_host));
		g_free (hostname);
	}

	g_get_current_time (&now);
	session_record.ut_tv.tv_sec = now.tv_sec;
	session_record.ut_tv.tv_usec = now.tv_usec;

	g_debug ("using time %ld",
		 (glong) session_record.ut_tv.tv_sec);

	session_record.ut_type = USER_PROCESS;
	g_debug ("using type USER_PROCESS");

	if (session->priv->session_pid != 0) {
		session_record.ut_pid = session->priv->session_pid;
	}

	g_debug ("using pid %d", (int) session_record.ut_pid);

	switch (record_type) {
	case GDM_SESSION_RECORD_TYPE_LOGIN:
		g_debug ("writing session record to " GDM_NEW_SESSION_RECORDS_FILE);
		updwtmp (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
		break;

	case GDM_SESSION_RECORD_TYPE_LOGOUT:
		g_debug ("writing logout record to " GDM_NEW_SESSION_RECORDS_FILE);
		updwtmp (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
		break;

	case GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT:
		g_debug ("writing failed session attempt record to "
			 GDM_BAD_SESSION_RECORDS_FILE);
		updwtmp (GDM_BAD_SESSION_RECORDS_FILE, &session_record);
		break;
	}
}

static void
gdm_session_user_verification_error_handler (GdmSession      *session,
					     GError          *error)
{
	gdm_session_write_record (session,
				  GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT);
}

static void
gdm_session_started_handler (GdmSession      *session,
                             GPid             pid)
{

	gdm_session_write_record (session,
				  GDM_SESSION_RECORD_TYPE_LOGIN);
}

static void
gdm_session_startup_error_handler (GdmSession      *session,
                                   GError          *error)
{
	gdm_session_write_record (session,
				  GDM_SESSION_RECORD_TYPE_LOGIN);
}

static void
gdm_session_exited_handler (GdmSession *session,
                            int        exit_code)
{
	gdm_session_write_record (session, GDM_SESSION_RECORD_TYPE_LOGOUT);
}

static void
gdm_session_class_install_signals (GdmSessionClass *session_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (session_class);

	gdm_session_signals[OPENED] =
		g_signal_new ("opened",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, opened),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	gdm_session_signals[CLOSED] =
		g_signal_new ("closed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, closed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	session_class->user_verified = NULL;
	gdm_session_signals[USER_VERIFIED] =
		g_signal_new ("user-verified",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, user_verified),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	session_class->user_verified = NULL;

	gdm_session_signals[USER_VERIFICATION_ERROR] =
		g_signal_new ("user-verification-error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, user_verification_error),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	session_class->user_verification_error = gdm_session_user_verification_error_handler;

	gdm_session_signals[INFO_QUERY] =
		g_signal_new ("info-query",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmSessionClass, info_query),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	session_class->info_query = NULL;

	gdm_session_signals[SECRET_INFO_QUERY] =
		g_signal_new ("secret-info-query",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmSessionClass, secret_info_query),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	session_class->secret_info_query = NULL;

	gdm_session_signals[INFO] =
		g_signal_new ("info",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, info),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	session_class->info = NULL;

	gdm_session_signals[PROBLEM] =
		g_signal_new ("problem",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, problem),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	session_class->problem = NULL;

	gdm_session_signals[SESSION_STARTED] =
		g_signal_new ("session-started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, session_started),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
	session_class->session_started = gdm_session_started_handler;

	gdm_session_signals[SESSION_STARTUP_ERROR] =
		g_signal_new ("session-startup-error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, session_startup_error),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	session_class->session_startup_error = gdm_session_startup_error_handler;

	gdm_session_signals[SESSION_EXITED] =
		g_signal_new ("session-exited",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, session_exited),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
	session_class->session_exited = gdm_session_exited_handler;

	gdm_session_signals[SESSION_DIED] =
		g_signal_new ("session-died",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdmSessionClass, session_died),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
	session_class->session_died = NULL;
}

static void
gdm_session_finalize (GObject *object)
{
	GdmSession   *session;
	GObjectClass *parent_class;

	session = GDM_SESSION (object);

	g_free (session->priv->username);

	parent_class = G_OBJECT_CLASS (gdm_session_parent_class);

	if (session->priv->environment != NULL) {
		g_hash_table_destroy (session->priv->environment);
		session->priv->environment = NULL;
	}

	if (parent_class->finalize != NULL)
		parent_class->finalize (object);
}

static void
gdm_session_class_init (GdmSessionClass *session_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (session_class);

	object_class->finalize = gdm_session_finalize;

	gdm_session_class_install_signals (session_class);

	g_type_class_add_private (session_class,
				  sizeof (GdmSessionPrivate));
}

static DBusHandlerResult
gdm_session_handle_verified (GdmSession     *session,
			     DBusConnection *connection,
			     DBusMessage    *message)
{
	DBusMessage *reply;

	g_debug ("Emitting 'user-verified' signal");

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	session->priv->is_verified = TRUE;
	g_signal_emit (session,
		       gdm_session_signals[USER_VERIFIED],
		       0);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_verification_failed (GdmSession     *session,
					DBusConnection *connection,
					DBusMessage    *message)
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

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'verification-failed' signal");

	g_signal_emit (session,
		       gdm_session_signals[USER_VERIFICATION_ERROR],
		       0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_username_changed (GdmSession     *session,
				     DBusConnection *connection,
				     DBusMessage    *message)
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

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("changing username from '%s' to '%s'",
		 session->priv->username != NULL ? session->priv->username : "<unset>",
		 (strlen (text)) ? text : "<unset>");

	g_free (session->priv->username);
	session->priv->username = (strlen (text) > 0) ? g_strdup (text) : NULL;

	return DBUS_HANDLER_RESULT_HANDLED;
}

static void
answer_pending_query (GdmSession *session,
		      const char *answer)
{
	DBusMessage    *reply;
	DBusMessageIter iter;

	reply = dbus_message_new_method_return (session->priv->message_pending_reply);
	dbus_message_iter_init_append (reply, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &answer);

	dbus_connection_send (session->priv->worker_connection, reply, NULL);
	dbus_message_unref (reply);

	dbus_message_unref (session->priv->message_pending_reply);
	session->priv->message_pending_reply = NULL;
}

static void
set_pending_query (GdmSession  *session,
		   DBusMessage *message)
{
	g_assert (session->priv->message_pending_reply == NULL);

	session->priv->message_pending_reply = dbus_message_ref (message);
}

static DBusHandlerResult
gdm_session_handle_info_query (GdmSession     *session,
			       DBusConnection *connection,
			       DBusMessage    *message)
{
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	set_pending_query (session, message);

	g_debug ("Emitting 'info-query' signal");
	g_signal_emit (session,
		       gdm_session_signals[INFO_QUERY],
		       0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_secret_info_query (GdmSession     *session,
				      DBusConnection *connection,
				      DBusMessage    *message)
{
	DBusError    error;
	const char  *text;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_STRING, &text,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	set_pending_query (session, message);

	g_debug ("Emitting 'secret-info-query' signal");

	g_signal_emit (session,
		       gdm_session_signals[SECRET_INFO_QUERY],
		       0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_info (GdmSession     *session,
			 DBusConnection *connection,
			 DBusMessage    *message)
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

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'info' signal");
	g_signal_emit (session,
		       gdm_session_signals[INFO],
		       0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_problem (GdmSession     *session,
			    DBusConnection *connection,
			    DBusMessage    *message)
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

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'problem' signal");
	g_signal_emit (session,
		       gdm_session_signals[PROBLEM],
		       0, text);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_started (GdmSession     *session,
				    DBusConnection *connection,
				    DBusMessage    *message)
{
	DBusMessage *reply;
	DBusError    error;
	int          pid;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_INT32, &pid,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'session-started' signal with pid '%d'",
		 pid);

	session->priv->session_pid = pid;
	session->priv->is_running = TRUE;

	g_signal_emit (session,
		       gdm_session_signals[SESSION_STARTED],
		       0, pid);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_startup_failed (GdmSession     *session,
				   DBusConnection *connection,
				   DBusMessage    *message)
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

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'session-startup-error' signal");

	g_signal_emit (session,
		       gdm_session_signals[SESSION_STARTUP_ERROR],
		       0, text);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_exited (GdmSession     *session,
				   DBusConnection *connection,
				   DBusMessage    *message)
{
	DBusMessage *reply;
	DBusError    error;
	int          code;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_INT32, &code,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'session-exited' signal with exit code '%d'",
		 code);

	session->priv->is_running = FALSE;
	g_signal_emit (session,
		       gdm_session_signals[SESSION_EXITED],
		       0, code);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gdm_session_handle_session_died (GdmSession     *session,
				 DBusConnection *connection,
				 DBusMessage    *message)
{
	DBusMessage *reply;
	DBusError    error;
	int          code;

	dbus_error_init (&error);
	if (! dbus_message_get_args (message, &error,
				     DBUS_TYPE_INT32, &code,
				     DBUS_TYPE_INVALID)) {
		g_warning ("ERROR: %s", error.message);
	}

	reply = dbus_message_new_method_return (message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);

	g_debug ("Emitting 'session-died' signal with signal number '%d'",
		 code);

	session->priv->is_running = FALSE;
	g_signal_emit (session,
		       gdm_session_signals[SESSION_DIED],
		       0, code);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
session_worker_message (DBusConnection *connection,
			DBusMessage    *message,
			void           *user_data)
{
        GdmSession *session = GDM_SESSION (user_data);

	if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Verified")) {
		return gdm_session_handle_verified (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "VerificationFailed")) {
		return gdm_session_handle_verification_failed (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "UsernameChanged")) {
		return gdm_session_handle_username_changed (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "InfoQuery")) {
		return gdm_session_handle_info_query (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SecretInfoQuery")) {
		return gdm_session_handle_secret_info_query (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Info")) {
		return gdm_session_handle_info (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "Problem")) {
		return gdm_session_handle_problem (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionStarted")) {
		return gdm_session_handle_session_started (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "StartupFailed")) {
		return gdm_session_handle_startup_failed (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionExited")) {
		return gdm_session_handle_session_exited (session, connection, message);
	} else if (dbus_message_is_method_call (message, GDM_SESSION_DBUS_INTERFACE, "SessionDied")) {
		return gdm_session_handle_session_died (session, connection, message);
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
			       "  <interface name=\"org.gnome.DisplayManager.Session\">\n"
			       "    <method name=\"Verified\">\n"
			       "    </method>\n"
			       "    <method name=\"VerificationFailed\">\n"
			       "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"InfoQuery\">\n"
			       "      <arg name=\"query\" direction=\"in\" type=\"s\"/>\n"
			       "      <arg name=\"answer\" direction=\"out\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SecretInfoQuery\">\n"
			       "      <arg name=\"query\" direction=\"in\" type=\"s\"/>\n"
			       "      <arg name=\"answer\" direction=\"out\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"Info\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"Problem\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"UsernameChanged\">\n"
			       "      <arg name=\"text\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"StartupFailed\">\n"
			       "      <arg name=\"message\" direction=\"in\" type=\"s\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SessionStarted\">\n"
			       "      <arg name=\"pid\" direction=\"in\" type=\"i\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SessionExited\">\n"
			       "      <arg name=\"code\" direction=\"in\" type=\"i\"/>\n"
			       "    </method>\n"
			       "    <method name=\"SessionDied\">\n"
			       "      <arg name=\"signal\" direction=\"in\" type=\"i\"/>\n"
			       "    </method>\n"
			       "    <signal name=\"BeginVerification\">\n"
			       "      <arg name=\"service_name\" type=\"s\"/>\n"
			       "      <arg name=\"hostname\" type=\"s\"/>\n"
			       "      <arg name=\"console\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"BeginVerificationForUser\">\n"
			       "      <arg name=\"service_name\" type=\"s\"/>\n"
			       "      <arg name=\"hostname\" type=\"s\"/>\n"
			       "      <arg name=\"console\" type=\"s\"/>\n"
			       "      <arg name=\"username\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"StartProgram\">\n"
			       "      <arg name=\"command\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"SetEnvironmentVariable\">\n"
			       "      <arg name=\"name\" type=\"s\"/>\n"
			       "      <arg name=\"value\" type=\"s\"/>\n"
			       "    </signal>\n"
			       "    <signal name=\"Reset\">\n"
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
session_message_handler (DBusConnection  *connection,
			 DBusMessage     *message,
			 void            *user_data)
{
	g_debug ("session_message_handler: destination=%s obj_path=%s interface=%s method=%s",
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
                return session_worker_message (connection, message, user_data);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
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

static void
session_unregister_handler (DBusConnection  *connection,
			    void            *user_data)
{
	g_debug ("session_unregister_handler");
}

static DBusHandlerResult
connection_filter_function (DBusConnection *connection,
			    DBusMessage    *message,
			    void	   *user_data)
{
        GdmSession *session = GDM_SESSION (user_data);
	const char *path;

	path = dbus_message_get_path (message);

	g_debug ("obj_path=%s interface=%s method=%s",
		 dbus_message_get_path (message),
		 dbus_message_get_interface (message),
		 dbus_message_get_member (message));

	if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected")
	    && strcmp (path, DBUS_PATH_LOCAL) == 0) {

		g_debug ("Disconnected");

		dbus_connection_unref (connection);
		session->priv->worker_connection = NULL;

		g_signal_emit (session, gdm_session_signals [CLOSED], 0);
	} else if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {


	} else {
		return session_message_handler (connection, message, user_data);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
	if (0 == uid) {
		return TRUE;
	}

	g_debug ("User not allowed");

	return FALSE;
}

static void
handle_connection (DBusServer      *server,
		   DBusConnection  *new_connection,
		   void            *user_data)
{
        GdmSession *session = GDM_SESSION (user_data);

	g_debug ("Handing new connection");

	if (session->priv->worker_connection == NULL) {
		DBusObjectPathVTable vtable = { &session_unregister_handler,
						&session_message_handler,
						NULL, NULL, NULL, NULL
		};

		session->priv->worker_connection = new_connection;
		dbus_connection_ref (new_connection);
		dbus_connection_setup_with_g_main (new_connection, NULL);

		g_debug ("worker connection is %p", new_connection);

		dbus_connection_add_filter (new_connection,
					    connection_filter_function,
					    session,
					    NULL);

		dbus_connection_set_unix_user_function (new_connection,
							allow_user_function,
							session,
							NULL);

		dbus_connection_register_object_path (new_connection,
						      "/",
						      &vtable,
						      session);

		g_debug ("Emitting opened signal");
		g_signal_emit (session, gdm_session_signals [OPENED], 0);
	}
}

static gboolean
setup_server (GdmSession *session)
{
	DBusError   error;
	gboolean    ret;
	char       *address;
	const char *auth_mechanisms[] = {"EXTERNAL", NULL};

	ret = FALSE;

	g_debug ("Creating D-Bus server for session");

	address = generate_address ();

	dbus_error_init (&error);
	session->priv->server = dbus_server_listen (address, &error);
	g_free (address);

	if (session->priv->server == NULL) {
		g_warning ("Cannot create D-BUS server for the session: %s", error.message);
		goto out;
	}

	dbus_server_setup_with_g_main (session->priv->server, NULL);
	dbus_server_set_auth_mechanisms (session->priv->server, auth_mechanisms);
	dbus_server_set_new_connection_function (session->priv->server,
						 handle_connection,
						 session,
						 NULL);
	ret = TRUE;

	g_free (session->priv->server_address);
	session->priv->server_address = dbus_server_get_address (session->priv->server);

	g_debug ("D-Bus server listening on %s", session->priv->server_address);

 out:

	return ret;
}

static void
gdm_session_init (GdmSession *session)
{
	session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session,
						     GDM_TYPE_SESSION,
						     GdmSessionPrivate);

	session->priv->environment = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    (GDestroyNotify) g_free,
							    (GDestroyNotify) g_free);

	setup_server (session);
}

GdmSession *
gdm_session_new (void)
{
	GdmSession *session;

	session = g_object_new (GDM_TYPE_SESSION, NULL);

	return session;
}

static void
worker_stopped (GdmSessionWorkerJob *job,
		GdmSession          *session)
{
	g_debug ("Worker job stopped");
}

static void
worker_started (GdmSessionWorkerJob *job,
		GdmSession          *session)
{
	g_debug ("Worker job started");
}

static void
worker_exited (GdmSessionWorkerJob *job,
	       int                  code,
	       GdmSession          *session)
{
	g_debug ("Worker job exited: %d", code);

	if (!session->priv->is_verified) {
		GError *error;

		error = g_error_new (GDM_SESSION_ERROR,
				     GDM_SESSION_ERROR_WORKER_DIED,
				     _("worker exited with status %d"),
				     code);

		g_signal_emit (session,
			       gdm_session_signals [USER_VERIFICATION_ERROR],
			       0, error);
		g_error_free (error);
	} else if (session->priv->is_running) {
		g_signal_emit (session,
			       gdm_session_signals [SESSION_EXITED],
			       0, code);
	}
}

static void
worker_died (GdmSessionWorkerJob *job,
	     int                  signum,
	     GdmSession          *session)
{
	g_debug ("Worker job died: %d", signum);

	if (!session->priv->is_verified) {
		GError *error;
		error = g_error_new (GDM_SESSION_ERROR,
				     GDM_SESSION_ERROR_WORKER_DIED,
				     _("worker got signal '%s' and was subsequently killed"),
				     g_strsignal (signum));
		g_signal_emit (session,
			       gdm_session_signals[USER_VERIFICATION_ERROR],
			       0, error);
		g_error_free (error);
	} else if (session->priv->is_running) {
		g_signal_emit (session,
			       gdm_session_signals[SESSION_EXITED],
			       0, signum);
	}
}

static gboolean
start_worker (GdmSession *session)
{
	gboolean res;

	session->priv->job = gdm_session_worker_job_new ();
	gdm_session_worker_job_set_server_address (session->priv->job, session->priv->server_address);
	g_signal_connect (session->priv->job,
			  "stopped",
			  G_CALLBACK (worker_stopped),
			  session);
	g_signal_connect (session->priv->job,
			  "started",
			  G_CALLBACK (worker_started),
			  session);
	g_signal_connect (session->priv->job,
			  "exited",
			  G_CALLBACK (worker_exited),
			  session);
	g_signal_connect (session->priv->job,
			  "died",
			  G_CALLBACK (worker_died),
			  session);

	res = gdm_session_worker_job_start (session->priv->job);

	return res;
}

gboolean
gdm_session_open (GdmSession  *session,
                  const char  *service_name,
                  const char  *hostname,
                  const char  *console_name,
                  GError     **error)
{
	gboolean res;

	g_return_val_if_fail (session != NULL, FALSE);
	g_return_val_if_fail (service_name != NULL, FALSE);
	g_return_val_if_fail (console_name != NULL, FALSE);
	g_return_val_if_fail (hostname != NULL, FALSE);

	res = start_worker (session);

	session->priv->service_name = g_strdup (service_name);
	session->priv->hostname = g_strdup (hostname);
	session->priv->console_name = g_strdup (console_name);

	return res;
}

static void
send_begin_verification (GdmSession *session)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
					   GDM_SESSION_DBUS_INTERFACE,
					   "BeginVerification");

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->service_name);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->hostname);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->console_name);

	if (! send_dbus_message (session->priv->worker_connection, message)) {
		g_debug ("Could not send %s signal", "BeginVerification");
	}

	dbus_message_unref (message);
}

static void
send_begin_verification_for_user (GdmSession *session)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
					   GDM_SESSION_DBUS_INTERFACE,
					   "BeginVerificationForUser");

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->service_name);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->hostname);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->console_name);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &session->priv->username);

	if (! send_dbus_message (session->priv->worker_connection, message)) {
		g_debug ("Could not send %s signal", "BeginVerificationForUser");
	}

	dbus_message_unref (message);
}

gboolean
gdm_session_begin_verification (GdmSession  *session,
				const char  *username,
				GError     **error)
{
	g_return_val_if_fail (session != NULL, FALSE);
	g_return_val_if_fail (dbus_connection_get_is_connected (session->priv->worker_connection), FALSE);

	session->priv->username = g_strdup (username);

	if (username == NULL) {
		send_begin_verification (session);
	} else {
		send_begin_verification_for_user (session);
	}

	return TRUE;
}

static void
send_environment_variable (const char *key,
			   const char *value,
			   GdmSession *session)
{
	DBusMessage    *message;
	DBusMessageIter iter;

	message = dbus_message_new_signal (GDM_SESSION_DBUS_PATH,
					   GDM_SESSION_DBUS_INTERFACE,
					   "SetEnvironmentVariable");

	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &key);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &value);

	if (! send_dbus_message (session->priv->worker_connection, message)) {
		g_debug ("Could not send %s signal", "SetEnvironmentVariable");
	}

	dbus_message_unref (message);
}

static void
send_environment (GdmSession *session)
{

	g_hash_table_foreach (session->priv->environment,
			      (GHFunc) send_environment_variable,
			      session);
}

void
gdm_session_start_program (GdmSession *session,
			   const char *command)
{
	g_return_if_fail (session != NULL);
	g_return_if_fail (session != NULL);
	g_return_if_fail (gdm_session_is_running (session) == FALSE);
	g_return_if_fail (command != NULL);

	send_environment (session);

	send_dbus_string_signal (session, "StartProgram", command);
}

void
gdm_session_close (GdmSession *session)
{
	g_return_if_fail (session != NULL);

	if (session->priv->job != NULL) {
		if (session->priv->is_running) {
			gdm_session_write_record (session,
						  GDM_SESSION_RECORD_TYPE_LOGOUT);
		}

		gdm_session_worker_job_stop (session->priv->job);
	}

	session->priv->is_running = FALSE;
	session->priv->is_verified = FALSE;

	if (session->priv->service_name) {
		g_free (session->priv->service_name);
		session->priv->service_name = NULL;
	}

	if (session->priv->hostname) {
		g_free (session->priv->hostname);
		session->priv->hostname = NULL;
	}

	if (session->priv->username) {
		g_free (session->priv->username);
		session->priv->username = NULL;
	}
}

gboolean
gdm_session_is_running (GdmSession *session)
{
	g_return_val_if_fail (session != NULL, FALSE);

	return session->priv->is_running;
}

void
gdm_session_set_environment_variable (GdmSession *session,
                                      const char *key,
                                      const char *value)
{
	g_return_if_fail (session != NULL);
	g_return_if_fail (session != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	g_hash_table_replace (session->priv->environment,
			      g_strdup (key),
			      g_strdup (value));
}

void
gdm_session_answer_query  (GdmSession *session,
                           const char *answer)
{
	g_return_if_fail (session != NULL);

	answer_pending_query (session, answer);
}

char *
gdm_session_get_username (GdmSession *session)
{
	g_return_val_if_fail (session != NULL, NULL);

	return g_strdup (session->priv->username);
}
