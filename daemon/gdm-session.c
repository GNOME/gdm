/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * session.c - authenticates and authorizes users with system
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
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
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#include <security/pam_appl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "gdm-session.h"

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

#ifndef GDM_PASSWD_AUXILLARY_BUFFER_SIZE
#define GDM_PASSWD_AUXILLARY_BUFFER_SIZE 1024
#endif

#ifndef GDM_SESSION_MESSAGE_SENTINAL
#define GDM_SESSION_MESSAGE_SENTINAL "sEnTInAL"
#endif

#ifndef GDM_SESSION_WORKER_MESSAGE_SENTINAL
#define GDM_SESSION_WORKER_MESSAGE_SENTINAL "WoRkeR sEnTInAL"
#endif

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin:/usr/X11R6/bin"
#endif

#ifndef GDM_SESSION_ROOT_UID
#define GDM_SESSION_ROOT_UID 0
#endif

typedef struct _GdmSessionMessage GdmSessionMessage;
typedef struct _GdmSessionVerificationMessage GdmSessionVerificationMessage;
typedef struct _GdmSessionStartProgramMessage GdmSessionStartProgramMessage;
typedef struct _GdmSessionSetEnvironmentVariableMessage GdmSessionSetEnvironmentVariableMessage;
typedef struct _GdmSessionInfoReplyMessage GdmSessionInfoReplyMessage;
typedef struct _GdmSessionSecretInfoReplyMessage GdmSessionSecretInfoReplyMessage;
typedef struct _GdmSessionWorker GdmSessionWorker;
typedef struct _GdmSessionWorkerMessage GdmSessionWorkerMessage;
typedef struct _GdmSessionWorkerVerifiedMessage GdmSessionWorkerVerifiedMessage;
typedef struct _GdmSessionWorkerVerificationFailedMessage GdmSessionWorkerVerificationFailedMessage;
typedef struct _GdmSessionWorkerUsernameChangedMessage GdmSessionWorkerUsernameChangedMessage;
typedef struct _GdmSessionWorkerInfoRequestMessage GdmSessionWorkerInfoRequestMessage;
typedef struct _GdmSessionWorkerSecretInfoRequestMessage GdmSessionWorkerSecretInfoRequestMessage;
typedef struct _GdmSessionWorkerInfoMessage GdmSessionWorkerInfoMessage;
typedef struct _GdmSessionWorkerProblemMessage GdmSessionWorkerProblemMessage;
typedef struct _GdmSessionWorkerSessionStartedMessage GdmSessionWorkerSessionStartedMessage;
typedef struct _GdmSessionWorkerSessionStartupFailedMessage GdmSessionWorkerSessionStartupFailedMessage;
typedef struct _GdmSessionWorkerSessionExitedMessage GdmSessionWorkerSessionExitedMessage;
typedef struct _GdmSessionWorkerSessionDiedMessage GdmSessionWorkerSessionDiedMessage;

typedef int (* GdmSessionWorkerPamNewMessagesFunc) (int,
						    const struct pam_message **,
						    struct pam_response **,
						    gpointer);
typedef enum _GdmSessionMessageType {
	GDM_SESSION_MESSAGE_TYPE_INVALID = 0,
	GDM_SESSION_MESSAGE_TYPE_VERIFICATION = 0xc001deed,
	GDM_SESSION_MESSAGE_TYPE_START_PROGRAM,
	GDM_SESSION_MESSAGE_TYPE_SET_ENVIRONMENT_VARIABLE,
	GDM_SESSION_MESSAGE_TYPE_NEW_LOG_FILENAME,
	GDM_SESSION_MESSAGE_TYPE_INFO_REPLY,
	GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY
} GdmSessionMessageType;

typedef enum _GdmSessionRecordType {
	GDM_SESSION_RECORD_TYPE_LOGIN,
	GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT,
	GDM_SESSION_RECORD_TYPE_LOGOUT,
} GdmSessionRecordType;

typedef enum _GdmSessionWorkerMessageType {
	GDM_SESSION_WORKER_MESSAGE_TYPE_INVALID = 0,
	GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFIED = 0xdeadbeef,
	GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFICATION_FAILED,
	GDM_SESSION_WORKER_MESSAGE_TYPE_USERNAME_CHANGED,
	GDM_SESSION_WORKER_MESSAGE_TYPE_INFO_REQUEST,
	GDM_SESSION_WORKER_MESSAGE_TYPE_SECRET_INFO_REQUEST,
	GDM_SESSION_WORKER_MESSAGE_TYPE_INFO,
	GDM_SESSION_WORKER_MESSAGE_TYPE_PROBLEM,
	GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTED,
	GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTUP_FAILED,
	GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED,
	GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_DIED,
} GdmSessionWorkerMessageType;

struct _GdmSessionPrivate
{
	GPid pid;

	char  *service_name;
	char **arguments;
	char  *username;
	char  *hostname;
	char  *console_name;

	int standard_output_fd;
	int standard_error_fd;

	int worker_message_pipe_fd;
	GSource *worker_message_pipe_source;

	GMainContext *context;

	GPid worker_pid;
	GSource *child_watch_source;

	GdmSessionMessageType next_expected_message;

	/* variable is only alive briefly to store user
	 * responses set in callbacks to authentication queries
	 */
	char *query_answer;

	guint32 is_verified : 1;
	guint32 is_running : 1;
};

struct _GdmSessionMessage
{
	GdmSessionMessageType type;
	gsize size;
};

struct _GdmSessionVerificationMessage
{
	GdmSessionMessage header;

	char service_name[GDM_MAX_SERVICE_NAME_SIZE];
	char console_name[GDM_MAX_CONSOLE_NAME_SIZE];
	char hostname[GDM_MAX_HOSTNAME_SIZE];

	int standard_output_fd;
	int standard_error_fd;

	guint32 hostname_is_provided : 1;

	gssize username_size;
	char username[0];
};

struct _GdmSessionStartProgramMessage
{
	GdmSessionMessage header;

	gsize arguments_size;
	char arguments[0];
};

struct _GdmSessionSetEnvironmentVariableMessage
{
	GdmSessionMessage header;

	gsize environment_variable_size;
	char environment_variable[0];
};

struct _GdmSessionInfoReplyMessage
{
	GdmSessionMessage header;

	gssize answer_size;
	char answer[0];
};

struct _GdmSessionSecretInfoReplyMessage
{
	GdmSessionMessage header;

	gssize answer_size;
	char answer[0];
};

struct _GdmSessionWorker
{
	GMainLoop *event_loop;
	int exit_code;

	pam_handle_t *pam_handle;

	int message_pipe_fd;
	GSource *message_pipe_source;

	GSList *inherited_fd_list;

	GPid child_pid;
	GSource *child_watch_source;

	char *username;
	char **arguments;

	GHashTable *environment;

	int standard_output_fd;
	int standard_error_fd;

	guint32 credentials_are_established : 1;
	guint32 is_running : 1;
};

struct _GdmSessionWorkerMessage
{
	GdmSessionWorkerMessageType type;
	gsize size;
};

struct _GdmSessionWorkerInfoRequestMessage
{
	GdmSessionWorkerMessage header;

	gssize question_size;
	char question[0];
};

struct _GdmSessionWorkerUsernameChangedMessage
{
	GdmSessionWorkerMessage header;

	gssize username_size;
	char username[0];
};

struct _GdmSessionWorkerSecretInfoRequestMessage
{
	GdmSessionWorkerMessage header;

	gssize question_size;
	char question[0];
};

struct _GdmSessionWorkerInfoMessage
{
	GdmSessionWorkerMessage header;

	gssize info_size;
	char info[0];
};

struct _GdmSessionWorkerProblemMessage
{
	GdmSessionWorkerMessage header;

	gssize problem_size;
	char problem[0];
};

struct _GdmSessionWorkerSessionStartedMessage
{
	GdmSessionWorkerMessage header;
	GPid pid;
};

struct _GdmSessionWorkerSessionStartupFailedMessage
{
	GdmSessionWorkerMessage header;
	GQuark error_domain;
	int error_code;

	gssize error_message_size;
	char error_message[0];
};

struct _GdmSessionWorkerSessionExitedMessage
{
	GdmSessionWorkerMessage header;

	int exit_code;
};

struct _GdmSessionWorkerSessionDiedMessage
{
	GdmSessionWorkerMessage header;

	int signal_number;
};

struct _GdmSessionWorkerVerifiedMessage
{
	GdmSessionWorkerMessage header;
};

struct _GdmSessionWorkerVerificationFailedMessage
{
	GdmSessionWorkerMessage header;
	GQuark error_domain;
	int error_code;

	gssize error_message_size;
	char error_message[0];
};

static GdmSessionWorkerMessage *gdm_session_worker_verified_message_new (void);
static GdmSessionWorkerMessage *gdm_session_worker_verification_failed_message_new (GError *error);
static GdmSessionWorkerMessage *gdm_session_worker_info_request_message_new (const char *question);
static GdmSessionWorkerMessage *gdm_session_worker_username_changed_message_new (const char *new_username);
static GdmSessionWorkerMessage *gdm_session_worker_secret_info_request_message_new (const char *question);
static GdmSessionWorkerMessage *gdm_session_worker_info_message_new (const char *info);
static GdmSessionWorkerMessage *gdm_session_worker_problem_message_new (const char *problem);
static GdmSessionWorkerMessage *gdm_session_worker_session_started_message_new (GPid pid);
static GdmSessionWorkerMessage *gdm_session_worker_session_exited_message_new (int exit_code);
static GdmSessionWorkerMessage *gdm_session_worker_session_died_message_new (int signal_number);
static void gdm_session_worker_message_free (GdmSessionWorkerMessage *message);

static gboolean gdm_session_worker_process_asynchronous_message (GdmSessionWorker *worker,
								 GdmSessionMessage *message);

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

	if (session->priv->pid != 0)
		session_record.ut_pid = session->priv->pid;
	else
		session_record.ut_pid = session->priv->worker_pid;

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
			      G_STRUCT_OFFSET (GdmSessionClass, session_exited),
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

	g_strfreev (session->priv->arguments);
	g_free (session->priv->username);

	parent_class = G_OBJECT_CLASS (gdm_session_parent_class);

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

static void
gdm_session_init (GdmSession *session)
{
	session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session,
						     GDM_TYPE_SESSION,
						     GdmSessionPrivate);
}

static gboolean
gdm_read_message (int      socket_fd,
                  gpointer *message_data,
                  gsize    *message_data_size,
                  char   **error_message)
{
	struct msghdr message = { 0 };
	struct iovec data_block = { 0 };
	const int flags = MSG_NOSIGNAL;

	gssize num_bytes_received;

	message.msg_iov = &data_block;
	message.msg_iovlen = 1;

	data_block.iov_base = g_malloc0 (GDM_MAX_MESSAGE_SIZE);
	data_block.iov_len = (size_t) GDM_MAX_MESSAGE_SIZE;

	num_bytes_received = 0;
	do {
		num_bytes_received = recvmsg (socket_fd, &message, flags);

		if (num_bytes_received > 0) {
			g_debug ("read '%lu' bytes over socket '%d'",
				 (gulong) num_bytes_received,
				 socket_fd);
		}
	}
	while (!(num_bytes_received > 0) && (errno == EINTR));

	if ((num_bytes_received < 0) && (error_message != NULL)) {
		const char *error;
		error = g_strerror (errno);
		g_debug ("message was not received: %s", error);
		*error_message = g_strdup (error);
		return FALSE;
	} else if (num_bytes_received == 0) {
		g_debug ("message was not received: premature eof");
		*error_message = g_strdup ("premature eof");
		return FALSE;
	}

	g_debug ("message was received!");
	if (message_data != NULL) {
		*message_data = g_realloc (data_block.iov_base, num_bytes_received);
		*message_data_size = (gsize) num_bytes_received;
	} else {
		g_free (message.msg_iov);
	}

	return TRUE;
}

static GdmSessionMessage *
gdm_session_verification_message_new (const char *service_name,
                                      const char *username,
                                      const char *hostname,
                                      const char *console_name,
                                      int         standard_output_fd,
                                      int         standard_error_fd)
{
	GdmSessionVerificationMessage *message;
	gsize size, username_size;

	username_size = (username != NULL? strlen (username) + 1 : 0);
	size = sizeof (GdmSessionVerificationMessage) +
		username_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_MESSAGE_TYPE_VERIFICATION;
	message->header.size = size;

	g_strlcpy (message->service_name, service_name,
		   sizeof (message->service_name));

	if (username != NULL) {
		g_strlcpy (message->username, username, username_size);
		message->username_size = username_size;
	} else {
		message->username_size = -1;
	}

	if (hostname != NULL) {
		g_strlcpy (message->hostname, hostname, sizeof (message->hostname));
		message->hostname_is_provided = TRUE;
	} else {
		message->hostname_is_provided = FALSE;
	}

	g_strlcpy (message->console_name, console_name, sizeof (message->console_name));

	message->standard_output_fd = standard_output_fd;
	message->standard_error_fd = standard_error_fd;

	g_strlcpy ((char *) (char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL),
		   GDM_SESSION_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL));

	return (GdmSessionMessage *) message;
}

static char *
gdm_session_flatten_arguments (const char * const *argv,
			       gsize              *arguments_size)
{
	char *arguments;
	gsize i, total_size, argument_size;

	total_size = 0;
	for (i = 0; argv[i] != NULL; i++) {
		argument_size = strlen (argv[i]) + 1;
		total_size += argument_size;
	}
	total_size++;

	arguments = g_slice_alloc0 (total_size);

	total_size = 0;
	for (i = 0; argv[i] != NULL; i++) {
		argument_size = strlen (argv[i]) + 1;
		g_strlcpy (arguments + total_size,
			   argv[i], argument_size);

		total_size += argument_size;
	}
	total_size++;

	if (arguments_size)
		*arguments_size = total_size;

	return arguments;
}

static char **
gdm_session_unflatten_arguments (const char *arguments)
{
	GPtrArray *array;
	char      *argument;

	array = g_ptr_array_new ();

	argument = (char *) arguments;
	while (*argument != '\0') {
		g_ptr_array_add (array, g_strdup (argument));
		argument += strlen (argument) + 1;
	}
	g_ptr_array_add (array, NULL);

	return (char **) g_ptr_array_free (array, FALSE);
}

static GdmSessionMessage *
gdm_session_start_program_message_new (const char * const * args)
{
	GdmSessionStartProgramMessage *message;
	char *arguments;
	gsize size;
	gsize arguments_size;

	g_assert (args != NULL);

	arguments_size = 0;
	arguments = gdm_session_flatten_arguments (args, &arguments_size);

	size = sizeof (GdmSessionStartProgramMessage) +
		arguments_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_MESSAGE_TYPE_START_PROGRAM;
	message->header.size = size;

	memcpy (message->arguments, arguments, arguments_size);
	g_slice_free1 (arguments_size, arguments);
	message->arguments_size = arguments_size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL),
		   GDM_SESSION_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL));

	return (GdmSessionMessage *) message;
}

static GdmSessionMessage *
gdm_session_set_environment_variable_message_new (const char *name,
						  const char *value)
{
	GdmSessionSetEnvironmentVariableMessage *message;
	char *environment_variable;
	gsize size;
	gsize environment_variable_size;
	int length;

	g_assert (name != NULL);
	g_assert (strchr (name, '=') == NULL);
	g_assert (value != NULL);

	environment_variable = g_strdup_printf ("%s=%s%n", name, value, &length);
	environment_variable_size = length + 1;

	size = sizeof (GdmSessionSetEnvironmentVariableMessage) +
		environment_variable_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_MESSAGE_TYPE_SET_ENVIRONMENT_VARIABLE;
	message->header.size = size;

	g_strlcpy (message->environment_variable,
		   environment_variable, environment_variable_size);
	g_free (environment_variable);
	message->environment_variable_size = environment_variable_size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL),
		   GDM_SESSION_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL));

	return (GdmSessionMessage *) message;
}

static GdmSessionMessage *
gdm_session_info_reply_message_new (const char *answer)
{
	GdmSessionInfoReplyMessage *message;
	gsize size;
	gsize answer_size;

	answer_size = (answer != NULL? strlen (answer) + 1 : 0);
	size = sizeof (GdmSessionInfoReplyMessage) +
		answer_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_MESSAGE_TYPE_INFO_REPLY;
	message->header.size = size;

	if (answer != NULL) {
		g_strlcpy (message->answer, answer, answer_size);
		message->answer_size = answer_size;
	} else {
		message->answer_size = -1;
	}

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL),
		   GDM_SESSION_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL));

	return (GdmSessionMessage *) message;
}

static GdmSessionMessage *
gdm_session_secret_info_reply_message_new (const char *answer)
{
	GdmSessionSecretInfoReplyMessage *message;
	gsize size;
	gsize answer_size;

	answer_size = (answer != NULL? strlen (answer) + 1 : 0);
	size = sizeof (GdmSessionSecretInfoReplyMessage) +
		answer_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY;
	message->header.size = size;

	if (answer != NULL) {
		g_strlcpy (message->answer, answer, answer_size);
		message->answer_size = answer_size;
	} else {
		message->answer_size = -1;
	}

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL),
		   GDM_SESSION_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_MESSAGE_SENTINAL));

	return (GdmSessionMessage *) message;
}

static void
gdm_session_message_free (GdmSessionMessage *message)
{
	g_slice_free1 (message->size, message);
}


GdmSession *
gdm_session_new (void)
{
	GdmSession *session;

	session = g_object_new (GDM_TYPE_SESSION, NULL);

	return session;
}

static void
gdm_session_clear_child_watch_source (GdmSession *session)
{
	session->priv->child_watch_source = NULL;
}

static void
gdm_session_clear_message_pipe_source (GdmSession *session)
{
	session->priv->worker_message_pipe_source = NULL;
}

static void
gdm_session_on_child_exited (GPid             pid,
                             int             status,
                             GdmSession *session)
{
	GError *error;

	if (WIFEXITED (status)) {
		if (!session->priv->is_verified) {
			error = g_error_new (GDM_SESSION_ERROR,
					     GDM_SESSION_ERROR_WORKER_DIED,
					     _("worker exited with status %d"),
					     WEXITSTATUS (status));

			g_signal_emit (session,
				       gdm_session_signals[USER_VERIFICATION_ERROR],
				       0, error);
			g_error_free (error);
		} else if (session->priv->is_running) {
			g_signal_emit (session,
				       gdm_session_signals[SESSION_EXITED],
				       0, WEXITSTATUS (status));
		}
	} else if (WIFSIGNALED (status)) {
		if (!session->priv->is_verified) {
			error = g_error_new (GDM_SESSION_ERROR,
					     GDM_SESSION_ERROR_WORKER_DIED,
					     _("worker got signal '%s' and was subsequently killed"),
					     g_strsignal (WTERMSIG (status)));
			g_signal_emit (session,
				       gdm_session_signals[USER_VERIFICATION_ERROR],
				       0, error);
			g_error_free (error);
		} else if (session->priv->is_running) {
			g_signal_emit (session,
				       gdm_session_signals[SESSION_EXITED],
				       0, WTERMSIG (status));
		}
	} else {
		/* WIFSTOPPED (status) || WIFCONTINUED (status) */
	}

	g_spawn_close_pid (pid);
	session->priv->worker_pid = 0;
}

static gboolean
gdm_session_validate_message_size (GdmSession        *session,
				   GdmSessionWorkerMessage  *message,
				   GError                **error)
{
	gsize expected_size;

	return TRUE;

	switch (message->type) {
	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFIED:
		expected_size = (gsize) sizeof (GdmSessionWorkerVerifiedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFICATION_FAILED:
		expected_size = (gsize) sizeof (GdmSessionWorkerVerificationFailedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_USERNAME_CHANGED:
		expected_size = (gsize) sizeof (GdmSessionWorkerUsernameChangedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO_REQUEST:
		expected_size = (gsize) sizeof (GdmSessionWorkerInfoRequestMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SECRET_INFO_REQUEST:
		expected_size = (gsize) sizeof (GdmSessionWorkerSecretInfoRequestMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO:
		expected_size = (gsize) sizeof (GdmSessionWorkerInfoMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_PROBLEM:
		expected_size = (gsize) sizeof (GdmSessionWorkerProblemMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTED:
		expected_size = (gsize) sizeof (GdmSessionWorkerSessionStartedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTUP_FAILED:
		expected_size = (gsize) sizeof (GdmSessionWorkerSessionStartupFailedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED:
		expected_size = (gsize) sizeof (GdmSessionWorkerSessionExitedMessage);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_DIED:
		expected_size = (gsize) sizeof (GdmSessionWorkerSessionDiedMessage);
		break;

	default:
		g_debug ("do not know about message type '0x%x'", (guint) message->type);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("do not know about message type '0x%x'"),
			     (guint) message->type);
		return FALSE;
	}

	if (message->size != expected_size) {
		g_debug ("message size was '%lu', but message was supposed "
			 "to be '%lu'", (gulong) message->size, (gulong) expected_size);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     "message size was '%lu', but message was supposed "
			     "to be '%lu'", (gulong) message->size,
			     (gulong) expected_size);
		return FALSE;
	}

	return TRUE;
}

static GdmSessionWorkerMessage *
gdm_session_get_incoming_message (GdmSession  *session,
				  GError     **error)
{
	GError                  *size_error;
	char                    *error_message;
	GdmSessionWorkerMessage *message;
	gsize                    message_size;
	gboolean                 res;

	g_debug ("attemping to read message from worker...");
	error_message = NULL;
	res = gdm_read_message (session->priv->worker_message_pipe_fd,
				(gpointer) &message,
				&message_size,
				&error_message);
	if (! res) {
		g_debug ("could not read message from worker: %s", error_message);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     "%s", error_message);
		g_free (error_message);
		return NULL;
	}
	g_debug ("message type is '0x%x'", message->type);

	if (message_size != message->size) {
		g_debug ("message reports to be '%ld' bytes but is actually '%ld'",
			 (glong) message->size, (glong) message_size);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("specified message size does not match size of message "
			       "received"));
		return NULL;
	}
	g_debug ("message size is '%lu'", (gulong) message_size);

	g_debug ("validating that message size is right for message "
		 "type...");
	size_error = NULL;
	res = gdm_session_validate_message_size (session, message, &size_error);
	if (! res) {
		g_propagate_error (error, size_error);
		return NULL;
	}
	g_debug ("message size is valid");

	switch (message->type) {
	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFIED:
		g_debug ("received valid 'verified' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFICATION_FAILED:
		g_debug ("received valid 'verification failed' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_USERNAME_CHANGED:
		g_debug ("received valid 'username changed' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO_REQUEST:
		g_debug ("received valid 'info request' message from worker");
		g_debug ("***********MESSAGE QUESTION IS '%s'********",
			 ((GdmSessionWorkerInfoRequestMessage *) message)->question);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SECRET_INFO_REQUEST:
		g_debug ("received valid 'secret info request' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO:
		g_debug ("received valid 'info' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_PROBLEM:
		g_debug ("received valid 'problem' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTED:
		g_debug ("received valid 'session started' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTUP_FAILED:
		g_debug ("received valid 'session startup failed' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED:
		g_debug ("received valid 'session exited' message from worker");
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_DIED:
		g_debug ("received valid 'session died' message from worker");
		break;

	default:
		g_debug ("received unknown message of type '0x%x'",
			 (guint) message->type);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("received unknown message"));
		gdm_session_worker_message_free (message);
		message = NULL;
		break;
	}

	return message;
}

static void
gdm_session_handle_verified_message (GdmSession *session,
				     GdmSessionWorkerVerifiedMessage *message)
{
	g_debug ("Emitting 'user-verified' signal");

	session->priv->is_verified = TRUE;
	g_signal_emit (session,
		       gdm_session_signals[USER_VERIFIED],
		       0);
}

static void
gdm_session_handle_verification_failed_message (GdmSession             *session,
						GdmSessionWorkerVerificationFailedMessage *message)
{
	GError *error;

	g_debug ("Emitting 'verification-failed' signal");

	error = g_error_new (message->error_domain,
			     message->error_code,
			     "%s", message->error_message);

	g_signal_emit (session,
		       gdm_session_signals[USER_VERIFICATION_ERROR],
		       0, error);
	g_error_free (error);
}

static void
gdm_session_handle_username_changed_message (GdmSession *session,
					     GdmSessionWorkerUsernameChangedMessage *message)
{
	g_debug ("changing username from '%s' to '%s'",
		 session->priv->username != NULL? session->priv->username : "<unset>",
		 (message->username_size >= 0)? message->username : "<unset>");
	g_free (session->priv->username);
	session->priv->username = (message->username_size >= 0)? g_strdup (message->username) : NULL;
}

static void
gdm_session_handle_info_request_message (GdmSession*session,
					 GdmSessionWorkerInfoRequestMessage *message)
{
	g_assert (session->priv->query_answer == NULL);

	session->priv->next_expected_message = GDM_SESSION_MESSAGE_TYPE_INFO_REPLY;

	g_debug ("Emitting 'info-query' signal");
	g_signal_emit (session,
		       gdm_session_signals[INFO_QUERY],
		       0, message->question);
}

static void
gdm_session_handle_secret_info_request_message (GdmSession *session,
						GdmSessionWorkerSecretInfoRequestMessage *message)
{
	g_assert (session->priv->query_answer == NULL);

	session->priv->next_expected_message = GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY;

	g_debug ("Emitting 'secret-info-query' signal");

	g_signal_emit (session,
		       gdm_session_signals[SECRET_INFO_QUERY],
		       0, message->question);
}

static void
gdm_session_handle_info_message (GdmSession *session,
				 GdmSessionWorkerInfoMessage *message)
{
	g_debug ("Emitting 'info' signal");
	g_signal_emit (session,
		       gdm_session_signals[INFO],
		       0, message->info);

}

static void
gdm_session_handle_problem_message (GdmSession *session,
				    GdmSessionWorkerProblemMessage *message)
{
	g_debug ("Emitting 'problem' signal");
	g_signal_emit (session,
		       gdm_session_signals[PROBLEM],
		       0, message->problem);
}

static void
gdm_session_handle_session_started_message (GdmSession *session,
					    GdmSessionWorkerSessionStartedMessage *message)
{
	g_debug ("Emitting 'session-started' signal with pid '%d'",
		 (int) message->pid);

	session->priv->pid = message->pid;
	session->priv->is_running = TRUE;

	g_signal_emit (session,
		       gdm_session_signals[SESSION_STARTED],
		       0, (int) message->pid);
}

static void
gdm_session_handle_session_startup_failed_message (GdmSession *session,
						   GdmSessionWorkerSessionStartupFailedMessage *message)
{
	GError *error;

	g_debug ("Emitting 'session-startup-error' signal");

	error = g_error_new (message->error_domain,
			     message->error_code,
			     "%s", message->error_message);
	g_signal_emit (session,
		       gdm_session_signals[SESSION_STARTUP_ERROR],
		       0, error);
	g_error_free (error);
}

static void
gdm_session_handle_session_exited_message (GdmSession *session,
					   GdmSessionWorkerSessionExitedMessage *message)
{
	g_debug ("Emitting 'session-exited' signal with exit code '%d'",
		 message->exit_code);

	session->priv->is_running = FALSE;
	g_signal_emit (session,
		       gdm_session_signals[SESSION_EXITED],
		       0, message->exit_code);
}

static void
gdm_session_handle_session_died_message (GdmSession *session,
                                         GdmSessionWorkerSessionDiedMessage *message)
{
	g_debug ("Emitting 'session-died' signal with signal number '%d'",
		 message->signal_number);

	session->priv->is_running = FALSE;
	g_signal_emit (session,
		       gdm_session_signals[SESSION_DIED],
		       0, message->signal_number);
}

static void
gdm_session_incoming_message_handler (GdmSession *session)
{
	GdmSessionWorkerMessage *message;
	GError *error;

	error = NULL;
	message = gdm_session_get_incoming_message (session, &error);

	if (message == NULL) {
		g_assert (error != NULL);
		g_warning ("could not receive message from parent: %s\n", error->message);
		g_error_free (error);

		/* FIXME: figure out what to do here
		 */
		return;
	}

	switch (message->type) {
	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFIED:
		g_debug ("worker successfully verified user");
		gdm_session_handle_verified_message (session,
						     (GdmSessionWorkerVerifiedMessage *)
						     message);

		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFICATION_FAILED:
		g_debug ("worker could not verify user");
		gdm_session_handle_verification_failed_message (session,
								(GdmSessionWorkerVerificationFailedMessage *)
								message);

		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_USERNAME_CHANGED:
		g_debug ("received changed username from worker");
		gdm_session_handle_username_changed_message (session,
							     (GdmSessionWorkerUsernameChangedMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO_REQUEST:
		g_debug ("received info request from worker");
		gdm_session_handle_info_request_message (session,
							 (GdmSessionWorkerInfoRequestMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SECRET_INFO_REQUEST:
		g_debug ("received secret info request from worker");
		gdm_session_handle_secret_info_request_message (session,
								(GdmSessionWorkerSecretInfoRequestMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_INFO:
		g_debug ("received new info from worker");
		gdm_session_handle_info_message (session,
						 (GdmSessionWorkerInfoMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_PROBLEM:
		g_debug ("received problem from worker");
		gdm_session_handle_problem_message (session,
						    (GdmSessionWorkerProblemMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTED:
		g_debug ("received session started message from worker");
		gdm_session_handle_session_started_message (session,
							    (GdmSessionWorkerSessionStartedMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTUP_FAILED:
		g_debug ("received session startup failed message from worker");
		gdm_session_handle_session_startup_failed_message (session,
								   (GdmSessionWorkerSessionStartupFailedMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED:
		g_debug ("received session exited message from worker");
		gdm_session_handle_session_exited_message (session,
							   (GdmSessionWorkerSessionExitedMessage *) message);
		break;

	case GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_DIED:
		g_debug ("received session died message from worker");
		gdm_session_handle_session_died_message (session,
							 (GdmSessionWorkerSessionDiedMessage *) message);
		break;

	default:
		g_debug ("received unknown message of type '0x%x' from worker",
			 message->type);
		break;
	}

	gdm_session_worker_message_free (message);
}

static gboolean
gdm_session_data_on_message_pipe_handler (GIOChannel   *channel,
					  GIOCondition  condition,
					  GdmSession   *session)
{
	if ((condition & G_IO_IN) || (condition & G_IO_PRI)) {
		g_debug ("got message from message pipe");
		gdm_session_incoming_message_handler (session);
	}

	if ((condition & G_IO_ERR) || (condition & G_IO_HUP)) {
		g_debug ("got disconnected message from message pipe");
		g_error("not implemented");
		/*gdm_session_disconnected_handler (session);*/
		return FALSE;
	}

	return TRUE;
}

static void
gdm_session_worker_clear_child_watch_source (GdmSessionWorker *worker)
{
	worker->child_watch_source = NULL;
}

static void
gdm_session_watch_child (GdmSession *session)
{
	GIOChannel *io_channel;
	GIOFlags    channel_flags;

	g_assert (session->priv->child_watch_source == NULL);

	session->priv->child_watch_source = g_child_watch_source_new (session->priv->worker_pid);
	g_source_set_callback (session->priv->child_watch_source,
			       (GChildWatchFunc) gdm_session_on_child_exited,
			       session,
			       (GDestroyNotify) gdm_session_clear_child_watch_source);
	g_source_attach (session->priv->child_watch_source, session->priv->context);
	g_source_unref (session->priv->child_watch_source);

	io_channel = g_io_channel_unix_new (session->priv->worker_message_pipe_fd);

	channel_flags = g_io_channel_get_flags (io_channel);
	g_io_channel_set_flags (io_channel,
				channel_flags | G_IO_FLAG_NONBLOCK,
				NULL);

	g_assert (session->priv->worker_message_pipe_source == NULL);

	session->priv->worker_message_pipe_source = g_io_create_watch (io_channel,
								       G_IO_IN | G_IO_HUP);
	g_io_channel_unref (io_channel);

	g_source_set_callback (session->priv->worker_message_pipe_source,
			       (GIOFunc) gdm_session_data_on_message_pipe_handler,
			       session,
			       (GDestroyNotify) gdm_session_clear_message_pipe_source);
	g_source_attach (session->priv->worker_message_pipe_source, session->priv->context);
	g_source_unref (session->priv->worker_message_pipe_source);
}

static void
gdm_session_unwatch_child (GdmSession *session)
{
	if (session->priv->child_watch_source == NULL) {
		return;
	}

	g_source_destroy (session->priv->child_watch_source);
	session->priv->child_watch_source = NULL;

	g_assert (session->priv->worker_message_pipe_source != NULL);

	g_source_destroy (session->priv->worker_message_pipe_source);
	session->priv->worker_message_pipe_source = NULL;
}

static void
gdm_session_worker_clear_message_pipe_source (GdmSessionWorker *worker)
{
	worker->message_pipe_source = NULL;
}

static void
gdm_session_worker_disconnected_handler (GdmSessionWorker *worker)
{
	g_debug ("exiting...");
	worker->exit_code = 127;
	g_main_loop_quit (worker->event_loop);
}

static gboolean
gdm_session_worker_validate_message_size (GdmSessionWorker   *worker,
					  GdmSessionMessage  *message,
					  GError            **error)
{
	gsize expected_size;

	return TRUE;

	switch (message->type) {
	case GDM_SESSION_MESSAGE_TYPE_VERIFICATION:
		expected_size = (gsize) sizeof (GdmSessionVerificationMessage);
		break;
	case GDM_SESSION_MESSAGE_TYPE_START_PROGRAM:
		expected_size = (gsize) sizeof (GdmSessionStartProgramMessage);
		break;
	case GDM_SESSION_MESSAGE_TYPE_SET_ENVIRONMENT_VARIABLE:
		expected_size = (gsize) sizeof (GdmSessionSetEnvironmentVariableMessage);
		break;
	case GDM_SESSION_MESSAGE_TYPE_INFO_REPLY:
		expected_size = (gsize) sizeof (GdmSessionInfoReplyMessage);
		break;
	case GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY:
		expected_size = (gsize) sizeof (GdmSessionSecretInfoReplyMessage);
		break;

	default:
		g_debug ("do not know about message type '0x%x'",
			 (guint) message->type);

		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("do not know about message type '0x%x'"),
			     (guint) message->type);
		return FALSE;
	}

	if (message->size != expected_size) {
		g_debug ("message size was '%lu', but message was supposed "
			 "to be '%lu'",
			 (gulong) message->size,
			 (gulong) expected_size);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     "message size was '%lu', but message was supposed "
			     "to be '%lu'", (gulong) message->size,
			     (gulong) expected_size);
		return FALSE;
	}

	return TRUE;
}

static GdmSessionMessage *
gdm_session_worker_get_incoming_message (GdmSessionWorker  *worker,
					 GError           **error)
{
	GError            *size_error;
	char              *error_message;
	GdmSessionMessage *message;
	gsize              message_size;
	gboolean           res;

	g_debug ("attemping to read message from parent...");
	error_message = NULL;
	res = gdm_read_message (worker->message_pipe_fd,
				(gpointer) &message,
				&message_size,
				&error_message);
	if (! res) {
		g_debug ("could not read message from parent: %s", error_message);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     "%s", error_message);
		g_free (error_message);
		return NULL;
	}
	g_debug ("message type is '0x%x'", message->type);

	if (message_size != message->size) {
		g_debug ("message reports to be '%ld' bytes but is actually '%ld'",
			 (glong) message->size,
			 (glong) message_size);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("specified message size does not match size of message "
			       "received"));
		return NULL;
	}
	g_debug ("message size is '%lu'", (gulong) message_size);

	g_debug ("validating that message size is right for message "
		 "type...");

	size_error = NULL;
	res = gdm_session_worker_validate_message_size (worker, message, &size_error);
	if (! res) {
		g_propagate_error (error, size_error);
		return NULL;
	}
	g_debug ("message size is valid");

	switch (message->type) {
	case GDM_SESSION_MESSAGE_TYPE_VERIFICATION:
		g_debug ("user session verification request received");
		break;

	case GDM_SESSION_MESSAGE_TYPE_START_PROGRAM:
		g_debug ("session arguments received");
		break;

	case GDM_SESSION_MESSAGE_TYPE_SET_ENVIRONMENT_VARIABLE:
		g_debug ("environment variable received");
		break;

	case GDM_SESSION_MESSAGE_TYPE_INFO_REPLY:
		g_debug ("user session info reply received");
		break;

	case GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY:
		g_debug ("user session secret info reply received");
		break;

	default:
		g_debug ("do not know about message type '0x%x'",
			 (guint) message->type);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_COMMUNICATING,
			     _("received unknown message type"));
		gdm_session_message_free (message);
		message = NULL;
		break;
	}

	return message;
}

static gboolean
gdm_write_message (int      socket_fd,
                   gpointer message_data,
                   gsize    message_data_size,
                   char   **error_message)
{
	struct msghdr message = { 0 };
	struct iovec  data_block = { 0 };
	const int     flags = MSG_NOSIGNAL;
	gboolean      message_was_sent;

	message.msg_iov = &data_block;
	message.msg_iovlen = 1;

	g_assert (message_data_size <= GDM_MAX_MESSAGE_SIZE);

	data_block.iov_base = message_data;
	data_block.iov_len = (size_t) message_data_size;

	message_was_sent = FALSE;
	do {
		gssize num_bytes_sent;
		num_bytes_sent = sendmsg (socket_fd, &message, flags);

		if (num_bytes_sent > 0) {
			g_debug ("sent '%lu' bytes over socket '%d'",
				 (gulong) num_bytes_sent,
				 socket_fd);
			message_was_sent = TRUE;
		}
	}

	while (! message_was_sent && (errno == EINTR));

	if (! message_was_sent && (error_message != NULL)) {
		const char *error;
		error = g_strerror (errno);
		g_debug ("message was not sent: %s", error);
		*error_message = g_strdup (error);
	} else if (message_was_sent) {
		g_debug ("message was sent!");
	}

	g_free (message.msg_control);

	return message_was_sent;
}

static char *
gdm_session_worker_ask_question (GdmSessionWorker *worker,
				 const char       *question)
{
	GdmSessionWorkerMessage    *message;
	GdmSessionMessage          *reply;
	GdmSessionInfoReplyMessage *info_reply;
	char                       *answer;
	GError                     *error;

	message = gdm_session_worker_info_request_message_new (question);
	gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
	gdm_session_worker_message_free (message);

	error = NULL;
	do {
		gboolean res;

		reply = gdm_session_worker_get_incoming_message (worker, &error);

		if (reply == NULL) {
			g_warning ("could not receive message from parent: %s", error->message);
			g_error_free (error);

			/* FIXME: figure out what to do here
			 */
			return NULL;
		}

		res = gdm_session_worker_process_asynchronous_message (worker, reply);
		if (res) {
			gdm_session_message_free (reply);
			reply = NULL;
		}
	}
	while (reply == NULL);

	/* FIXME: we have to do something better here.  Messages aren't gauranteed to
	 * be delivered synchronously.  We need to either fix that, or fix this to not
	 * expect them to be.
	 */
	if (reply->type != GDM_SESSION_MESSAGE_TYPE_INFO_REPLY) {
		g_debug ("discarding unexpected message of type 0x%x", reply->type);
		gdm_session_message_free (reply);
		reply = NULL;
		return NULL;
	}

	info_reply = (GdmSessionInfoReplyMessage *) reply;
	if (info_reply->answer_size >= 0)
		answer = g_strdup (info_reply->answer);
	else
		answer = NULL;
	gdm_session_message_free (reply);
	reply = NULL;

	return answer;
}

static char *
gdm_session_worker_ask_for_secret (GdmSessionWorker *worker,
				   const char       *secret)
{
	GdmSessionWorkerMessage *message;
	GdmSessionMessage       *reply;
	GdmSessionSecretInfoReplyMessage *secret_info_reply;
	char                    *answer;
	GError                  *error;

	message = gdm_session_worker_secret_info_request_message_new (secret);
	gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
	gdm_session_worker_message_free (message);

	error = NULL;
	do {
		gboolean res;

		reply = gdm_session_worker_get_incoming_message (worker, &error);

		if (reply == NULL) {
			g_warning ("could not receive message from parent: %s", error->message);
			g_error_free (error);

			/* FIXME: figure out what to do here
			 */
			return NULL;
		}

		res = gdm_session_worker_process_asynchronous_message (worker, reply);
		if (res) {
			gdm_session_message_free (reply);
			reply = NULL;
		}
	}
	while (reply == NULL);

	/* FIXME: we have to do something better here.  Messages
	 * aren't gauranteed to be delivered synchronously.  We need
	 * to either fix that, or fix this to not expect them to be.
	 */
	if (reply->type != GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY) {
		g_debug ("discarding unexpected message of type 0x%x", reply->type);
		gdm_session_message_free (reply);
		return NULL;
	}
	secret_info_reply = (GdmSessionSecretInfoReplyMessage *) reply;

	answer = g_strdup (secret_info_reply->answer);
	gdm_session_message_free (reply);

	g_debug ("answer to secret question '%s' is '%s'", secret, answer);
	return answer;
}

static void
gdm_session_worker_report_info (GdmSessionWorker *worker,
				const char       *info)
{
	GdmSessionWorkerMessage *message;
	message = gdm_session_worker_info_message_new (info);
	gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
	gdm_session_worker_message_free (message);
}

static void
gdm_session_worker_report_problem (GdmSessionWorker *worker,
				   const char       *problem)
{
	GdmSessionWorkerMessage *message;
	message = gdm_session_worker_problem_message_new (problem);
	gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
	gdm_session_worker_message_free (message);
}

static gboolean
gdm_session_worker_get_username (GdmSessionWorker  *worker,
				 char             **username)
{
	gconstpointer item;

	g_assert (worker->pam_handle != NULL);

	if (pam_get_item (worker->pam_handle, PAM_USER, &item) == PAM_SUCCESS) {
		if (username) {
			*username = g_strdup ((char *) item);
			g_debug ("username is '%s'",
				 *username != NULL ? *username :
				 "<unset>");
		}
		return TRUE;
	}

	return FALSE;
}

static void
gdm_session_worker_update_username (GdmSessionWorker *worker)
{
	char *username;
	gboolean res;

	username = NULL;
	res = gdm_session_worker_get_username (worker, &username);
	if (res) {
		GdmSessionWorkerMessage *message;

		if ((worker->username == username) ||
		    ((worker->username != NULL) && (username != NULL) &&
		     (strcmp (worker->username, username) == 0)))
			goto out;

		g_debug ("setting username to '%s'", username);

		g_free (worker->username);
		worker->username = username;
		username = NULL;

		message = gdm_session_worker_username_changed_message_new (worker->username);
		gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
		gdm_session_worker_message_free (message);
	}

 out:
	g_free (username);
}

static gboolean
gdm_session_worker_process_pam_message (GdmSessionWorker          *worker,
					const struct pam_message  *query,
					char                     **response_text)
{
	char    *user_answer;
	gboolean was_processed;

	g_debug ("received pam message of type %u with payload '%s'",
		 query->msg_style, query->msg);

	user_answer = NULL;
	was_processed = FALSE;
	switch (query->msg_style) {
	case PAM_PROMPT_ECHO_ON:
		user_answer = gdm_session_worker_ask_question (worker, query->msg);
		break;
	case PAM_PROMPT_ECHO_OFF:
		user_answer = gdm_session_worker_ask_for_secret (worker, query->msg);
		break;
	case PAM_TEXT_INFO:
		gdm_session_worker_report_info (worker, query->msg);
		was_processed = TRUE;
		break;
	case PAM_ERROR_MSG:
		gdm_session_worker_report_problem (worker, query->msg);
		was_processed = TRUE;
		break;
	default:
		g_debug ("unknown query of type %u\n", query->msg_style);
		break;
	}

	if (user_answer != NULL) {
		/* we strdup and g_free to make sure we return malloc'd
		 * instead of g_malloc'd memory
		 */
		if (response_text != NULL) {
			*response_text = strdup (user_answer);
		}

		g_free (user_answer);

		g_debug ("trying to get updated username");
		gdm_session_worker_update_username (worker);
		was_processed = TRUE;
	}

	return was_processed;
}

static int
gdm_session_worker_pam_new_messages_handler (int                        number_of_messages,
					     const struct pam_message **messages,
					     struct pam_response      **responses,
					     GdmSessionWorker          *worker)
{
	struct pam_response *replies;
	int                  return_value;
	int                  i;

	g_debug ("%d new messages received from pam\n", number_of_messages);

	return_value = PAM_CONV_ERR;

	if (number_of_messages < 0) {
		return PAM_CONV_ERR;
	}

	if (number_of_messages == 0) {
		if (responses) {
			*responses = NULL;
		}

		return PAM_SUCCESS;
	}

	/* we want to generate one reply for every question
	 */
	replies = (struct pam_response *) calloc (number_of_messages,
						  sizeof (struct pam_response));
	for (i = 0; i < number_of_messages; i++) {
		gboolean got_response;
		char *response_text;

		response_text = NULL;
		got_response = gdm_session_worker_process_pam_message (worker,
								       messages[i],
								       &response_text);
		if (!got_response)
			goto out;

		g_debug ("answered pam message %d with response '%s'",
			 i, response_text);
		replies[i].resp = response_text;
		replies[i].resp_retcode = PAM_SUCCESS;
	}

	return_value = PAM_SUCCESS;

 out:
	if (return_value != PAM_SUCCESS) {
		for (i = 0; i < number_of_messages; i++) {
			if (replies[i].resp != NULL) {
				memset (replies[i].resp, 0, strlen (replies[i].resp));
				free (replies[i].resp);
			}
			memset (&replies[i], 0, sizeof (replies[i]));
		}
		free (replies);
		replies = NULL;
	}

	if (responses) {
		*responses = replies;
	}

	return return_value;
}

static void
gdm_session_worker_uninitialize_pam (GdmSessionWorker *worker,
				     int               error_code)
{
	g_debug ("uninitializing PAM");

	if (worker->pam_handle == NULL)
		return;

	if (worker->credentials_are_established) {
		pam_setcred (worker->pam_handle, PAM_DELETE_CRED);
		worker->credentials_are_established = FALSE;
	}

	if (worker->is_running) {
		pam_close_session (worker->pam_handle, 0);
		worker->is_running = FALSE;
	}

	pam_end (worker->pam_handle, error_code);
	worker->pam_handle = NULL;
}

static gboolean
gdm_session_worker_initialize_pam (GdmSessionWorker *worker,
				   const char       *service,
				   const char       *username,
				   const char       *hostname,
				   const char       *console_name,
				   GError          **error)
{
	struct pam_conv pam_conversation;
	int             error_code;

	g_assert (worker->pam_handle == NULL);

	g_debug ("initializing PAM");

	pam_conversation.conv = (GdmSessionWorkerPamNewMessagesFunc) gdm_session_worker_pam_new_messages_handler;
	pam_conversation.appdata_ptr = worker;

	error_code = pam_start (service,
				username,
				&pam_conversation,
				&worker->pam_handle);

	if (error_code != PAM_SUCCESS) {
		g_debug ("could not initialize pam");
		/* we don't use pam_strerror here because it requires a valid
		 * pam handle, and if pam_start fails pam_handle is undefined
		 */
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_AUTHENTICATING,
			     _("error initiating conversation with authentication system - %s"),
			     error_code == PAM_ABORT? _("general failure") :
			     error_code == PAM_BUF_ERR? _("out of memory") :
			     error_code == PAM_SYSTEM_ERR? _("application programmer error") :
			     _("unscoped error"));

		goto out;
	}

	if (username == NULL) {
		error_code = pam_set_item (worker->pam_handle, PAM_USER_PROMPT, _("Username:"));

		if (error_code != PAM_SUCCESS) {
			g_set_error (error,
				     GDM_SESSION_ERROR,
				     GDM_SESSION_ERROR_AUTHENTICATING,
				     _("error informing authentication system of preferred username prompt - %s"),
				     pam_strerror (worker->pam_handle, error_code));
			goto out;
		}
	}

	if (hostname != NULL) {
		error_code = pam_set_item (worker->pam_handle, PAM_RHOST, hostname);

		if (error_code != PAM_SUCCESS) {
			g_set_error (error,
				     GDM_SESSION_ERROR,
				     GDM_SESSION_ERROR_AUTHENTICATING,
				     _("error informing authentication system of user's hostname - %s"),
				     pam_strerror (worker->pam_handle, error_code));
			goto out;
		}
	}

	error_code = pam_set_item (worker->pam_handle, PAM_TTY, console_name);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_AUTHENTICATING,
			     _("error informing authentication system of user's console - %s"),
			     pam_strerror (worker->pam_handle, error_code));
		goto out;
	}

 out:
	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}

static gboolean
gdm_session_worker_authenticate_user (GdmSessionWorker *worker,
				      gboolean          password_is_required,
				      GError          **error)
{
	int error_code;
	int authentication_flags;

	g_debug ("authenticating user");

	authentication_flags = 0;

	if (password_is_required) {
		authentication_flags |= PAM_DISALLOW_NULL_AUTHTOK;
	}

	/* blocking call, does the actual conversation
	 */
	error_code = pam_authenticate (worker->pam_handle, authentication_flags);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_AUTHENTICATING,
			     "%s", pam_strerror (worker->pam_handle, error_code));
		goto out;
	}

 out:
	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}

static gboolean
gdm_session_worker_authorize_user (GdmSessionWorker *worker,
				   gboolean          password_is_required,
				   GError          **error)
{
	int error_code;
	int authentication_flags;

	g_debug ("determining if authenticated user is authorized to session");

	authentication_flags = 0;

	if (password_is_required) {
		authentication_flags |= PAM_DISALLOW_NULL_AUTHTOK;
	}

	/* check that the account isn't disabled or expired
	 */
	error_code = pam_acct_mgmt (worker->pam_handle, authentication_flags);

	/* it's possible that the user needs to change their password or pin code
	 */
	if (error_code == PAM_NEW_AUTHTOK_REQD)
		error_code = pam_chauthtok (worker->pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);

	if (error_code != PAM_SUCCESS) {
		g_debug ("user is not authorized to log in: %s",
			 pam_strerror (worker->pam_handle, error_code));
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_AUTHORIZING,
			     "%s", pam_strerror (worker->pam_handle, error_code));
		goto out;
	}

 out:
	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}

static void
gdm_session_worker_set_environment_variable (GdmSessionWorker *worker,
					     const char       *environment_variable)
{
	char **key_and_value;

	key_and_value = g_strsplit (environment_variable, "=", 2);

	/* FIXME: maybe we should use use pam_putenv instead of our
	 * own hash table, so pam can override our choices if it knows
	 * better?
	 */
	g_hash_table_replace (worker->environment,
			      key_and_value[0], key_and_value[1]);

	/* We are calling g_free instead of g_strfreev because the
	 * hash table is taking over ownership of the individual
	 * elements above
	 */
	g_free (key_and_value);
}

static void
gdm_session_worker_update_environment_from_passwd_entry (GdmSessionWorker *worker,
							 struct passwd    *passwd_entry)
{
	char *environment_variable;

	environment_variable = g_strdup_printf ("LOGNAME=%s", worker->username);
	gdm_session_worker_set_environment_variable (worker, environment_variable);
	g_free (environment_variable);

	environment_variable = g_strdup_printf ("USER=%s", worker->username);
	gdm_session_worker_set_environment_variable (worker, environment_variable);
	g_free (environment_variable);

	environment_variable = g_strdup_printf ("USERNAME=%s", worker->username);
	gdm_session_worker_set_environment_variable (worker, environment_variable);
	g_free (environment_variable);

	environment_variable = g_strdup_printf ("HOME=%s", passwd_entry->pw_dir);
	gdm_session_worker_set_environment_variable (worker, environment_variable);
	g_free (environment_variable);

	environment_variable = g_strdup_printf ("SHELL=%s", passwd_entry->pw_shell);
	gdm_session_worker_set_environment_variable (worker, environment_variable);
	g_free (environment_variable);
}

static gboolean
gdm_session_worker_environment_variable_is_set (GdmSessionWorker *worker,
						const char       *name)
{
	return g_hash_table_lookup (worker->environment, name) != NULL;
}

static gboolean
gdm_session_worker_give_user_credentials (GdmSessionWorker  *worker,
					  GError           **error)
{
	int error_code;
	struct passwd *passwd_entry, passwd_buffer;
	char *aux_buffer;
	long required_aux_buffer_size;
	gsize aux_buffer_size;

	aux_buffer = NULL;
	aux_buffer_size = 0;

	if (worker->username == NULL) {
		error_code = PAM_USER_UNKNOWN;
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     _("no user account available"));
		goto out;
	}

	required_aux_buffer_size = sysconf (_SC_GETPW_R_SIZE_MAX);

	if (required_aux_buffer_size < 0)
		aux_buffer_size = GDM_PASSWD_AUXILLARY_BUFFER_SIZE;
	else
		aux_buffer_size = (gsize) required_aux_buffer_size;

	aux_buffer = g_slice_alloc0 (aux_buffer_size);

	/* we use the _r variant of getpwnam()
	 * (with its weird semantics) so that the
	 * passwd_entry doesn't potentially get stomped on
	 * by a PAM module
	 */
	passwd_entry = NULL;
	errno = getpwnam_r (worker->username, &passwd_buffer,
			    aux_buffer, (size_t) aux_buffer_size,
			    &passwd_entry);

	if (errno != 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	if (passwd_entry == NULL) {
		error_code = PAM_USER_UNKNOWN;
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     _("user account not available on system"));
		goto out;
	}

	gdm_session_worker_update_environment_from_passwd_entry (worker, passwd_entry);

	/* Let's give the user a default PATH if he doesn't already have one
	 */
	if (!gdm_session_worker_environment_variable_is_set (worker, "PATH"))
		gdm_session_worker_set_environment_variable (worker,
							     "PATH=" GDM_SESSION_DEFAULT_PATH);

	/* pam_setcred wants to be called as the authenticated user
	 * but pam_open_session needs to be called as super-user.
	 *
	 * Set the real uid and gid to the user and give the user a
	 * temporary super-user effective id.
	 */
	if (setreuid (passwd_entry->pw_uid, GDM_SESSION_ROOT_UID) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	if (setgid (passwd_entry->pw_gid) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	if (initgroups (passwd_entry->pw_name, passwd_entry->pw_gid) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	error_code = pam_setcred (worker->pam_handle, PAM_ESTABLISH_CRED);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_GIVING_CREDENTIALS,
			     "%s", pam_strerror (worker->pam_handle, error_code));
		goto out;
	}

	worker->credentials_are_established = TRUE;

 out:
	if (aux_buffer != NULL) {
		g_assert (aux_buffer_size > 0);
		g_slice_free1 (aux_buffer_size, aux_buffer);
	}

	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}

static gboolean
gdm_session_worker_verify_user (GdmSessionWorker  *worker,
				const char        *service_name,
				const char        *username,
				const char        *hostname,
				const char        *console_name,
				gboolean           password_is_required,
				GError           **error)
{
	GError                  *pam_error;
	GdmSessionWorkerMessage *reply;
	char                    *error_message;
	gboolean                 res;

	g_debug ("Verifying user: %s host: %s service: %s tty: %s", username, hostname, service_name, console_name);

	pam_error = NULL;
	res = gdm_session_worker_initialize_pam (worker,
						 service_name,
						 username,
						 hostname,
						 console_name,
						 &pam_error);
	if (! res) {
		g_propagate_error (error, pam_error);
		return FALSE;
	}

	/* find out who the user is and ensure they are who they say they are
	 */
	res = gdm_session_worker_authenticate_user (worker,
						    password_is_required,
						    &pam_error);
	if (! res) {
		g_propagate_error (error, pam_error);
		return FALSE;
	}

	/* we're authenticated.  Let's make sure we've been given
	 * a valid username for the system
	 */
	g_debug ("trying to get updated username");
	gdm_session_worker_update_username (worker);

	/* make sure the user is allowed to log in to this system
	 */
	res = gdm_session_worker_authorize_user (worker,
						 password_is_required,
						 &pam_error);
	if (! res) {
		g_propagate_error (error, pam_error);
		return FALSE;
	}

	/* get kerberos tickets, setup group lists, etc
	 */
	res = gdm_session_worker_give_user_credentials (worker, &pam_error);
	if (! res) {
		g_propagate_error (error, pam_error);
		return FALSE;
	}

	g_debug ("verification process completed, creating reply...");
	reply = gdm_session_worker_verified_message_new ();

	error_message = NULL;
	res = gdm_write_message (worker->message_pipe_fd,
				 reply,
				 reply->size,
				 &error_message);
	if (! res) {
		g_warning ("could not send 'verified' reply to parent: %s\n",
			   error_message);
		g_free (error_message);
	}

	gdm_session_worker_message_free (reply);

	return TRUE;
}

static void
gdm_session_worker_update_environment_from_pam (GdmSessionWorker *worker)
{
	char **environment;
	gsize i;

	environment = pam_getenvlist (worker->pam_handle);

	for (i = 0; environment[i] != NULL; i++)
		gdm_session_worker_set_environment_variable (worker, environment[i]);

	for (i = 0; environment[i]; i++)
		free (environment[i]);
	free (environment);
}

static void
gdm_session_worker_fill_environment_array (const char *key,
					   const char *value,
					   GPtrArray  *environment)
{
	char *variable;

	if (value == NULL)
		return;

	variable = g_strdup_printf ("%s=%s", key, value);

	g_ptr_array_add (environment, variable);
}

static char **
gdm_session_worker_get_environment (GdmSessionWorker *worker)
{
	GPtrArray *environment;

	environment = g_ptr_array_new ();
	g_hash_table_foreach (worker->environment,
			      (GHFunc) gdm_session_worker_fill_environment_array,
			      environment);
	g_ptr_array_add (environment, NULL);

	return (char **) g_ptr_array_free (environment, FALSE);
}

static void
gdm_session_worker_on_child_exited (GPid              pid,
				    int               status,
				    GdmSessionWorker *worker)
{
	GdmSessionWorkerMessage *message;

	message = NULL;

	if (WIFEXITED (status))
		message = gdm_session_worker_session_exited_message_new (WEXITSTATUS (status));
	else if (WIFSIGNALED (status))
		message = gdm_session_worker_session_died_message_new (WTERMSIG (status));

	if (message != NULL) {
		gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
		gdm_session_worker_message_free (message);
	}

	g_spawn_close_pid (pid);
	worker->child_pid = 0;

	worker->exit_code = 0;
	g_main_loop_quit (worker->event_loop);
}

static void
gdm_session_worker_watch_child (GdmSessionWorker *worker)
{
	worker->child_watch_source = g_child_watch_source_new (worker->child_pid);
	g_source_set_callback (worker->child_watch_source,
			       (GSourceFunc) (GChildWatchFunc)
			       gdm_session_worker_on_child_exited,
			       worker,
			       (GDestroyNotify)
			       gdm_session_worker_clear_child_watch_source);
	g_source_attach (worker->child_watch_source,
			 g_main_loop_get_context (worker->event_loop));
	g_source_unref (worker->child_watch_source);
}

static gboolean
gdm_session_worker_open_user_session (GdmSessionWorker  *worker,
				      GError           **error)
{
	int                      error_code;
	pid_t                    session_pid;
	GdmSessionWorkerMessage *message;
	char                    *error_message;

	g_assert (!worker->is_running);
	g_assert (geteuid () == 0);
	error_code = pam_open_session (worker->pam_handle, 0);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_OPENING_SESSION,
			     "%s", pam_strerror (worker->pam_handle, error_code));
		goto out;
	}
	worker->is_running = TRUE;

	g_debug ("querying pam for user environment");
	gdm_session_worker_update_environment_from_pam (worker);

	g_debug ("opening user session with program '%s'",
		 worker->arguments[0]);

	session_pid = fork ();

	if (session_pid < 0) {
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_OPENING_SESSION,
			     "%s", g_strerror (errno));
		error_code = PAM_ABORT;
		goto out;
	}

	if (session_pid == 0) {
		char **environment;
		char *home_dir;
		int fd;

		worker->inherited_fd_list =
			g_slist_append (NULL,
					GINT_TO_POINTER (worker->standard_output_fd));
		worker->inherited_fd_list =
			g_slist_append (worker->inherited_fd_list,
					GINT_TO_POINTER (worker->standard_error_fd));

#if 0
		gdm_session_worker_close_open_fds (worker);
#endif

		if (setuid (getuid ()) < 0) {
			g_debug ("could not reset uid - %s", g_strerror (errno));
			_exit (1);
		}

		if (setsid () < 0) {
			g_debug ("could not set pid '%u' as leader of new session and process group - %s",
				 (guint) getpid (), g_strerror (errno));
			_exit (2);
		}

#if 0
		fd = gdm_open_dev_null (O_RDWR);

		if (worker->standard_output_fd >= 0) {
			dup2 (worker->standard_output_fd, STDOUT_FILENO);
			close (worker->standard_output_fd);
			worker->standard_output_fd = -1;
		} else {
			dup2 (fd, STDOUT_FILENO);
		}

		if (worker->standard_error_fd >= 0) {
			dup2 (worker->standard_error_fd, STDERR_FILENO);
			close (worker->standard_error_fd);
			worker->standard_error_fd = -1;
		} else {
			dup2 (fd, STDERR_FILENO);
		}

		dup2 (fd, STDIN_FILENO);
		close (fd);
#endif

		environment = gdm_session_worker_get_environment (worker);

		g_assert (geteuid () == getuid ());

		home_dir = g_hash_table_lookup (worker->environment,
						"HOME");

		if ((home_dir == NULL) || g_chdir (home_dir) < 0) {
			g_chdir ("/");
		}

		execve (worker->arguments[0], worker->arguments, environment);

		g_debug ("child '%s' could not be started - %s",
			 worker->arguments[0], g_strerror (errno));
		g_strfreev (environment);

		_exit (127);
	}

	worker->child_pid = session_pid;

	g_debug ("session opened creating reply...");
	g_assert (sizeof (GPid) <= sizeof (int));

	message = gdm_session_worker_session_started_message_new ((GPid) session_pid);

	error_message = NULL;
	if (!gdm_write_message (worker->message_pipe_fd,
				message, message->size,
				&error_message)) {
		g_warning ("could not send 'session started' reply to parent: %s\n",
			   error_message);
		g_free (error_message);
	}
	gdm_session_worker_message_free (message);

	gdm_session_worker_watch_child (worker);

 out:
	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}

static GdmSessionWorkerMessage *
gdm_session_worker_session_startup_failed_message_new (GError *error)
{
	GdmSessionWorkerSessionStartupFailedMessage *message;
	gsize error_message_size, size;

	error_message_size = (error != NULL? strlen (error->message) + 1 : 0);
	size = sizeof (GdmSessionWorkerSessionStartupFailedMessage) +
		error_message_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTUP_FAILED;
	message->header.size = size;

	if (error != NULL) {
		message->error_domain = error->domain;
		message->error_code = error->code;
		g_strlcpy (message->error_message, error->message,
			   error_message_size);
		message->error_message_size = error_message_size;
	} else {
		message->error_message_size = -1;
	}

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static void
gdm_session_worker_handle_verification_message (GdmSessionWorker              *worker,
						GdmSessionVerificationMessage *message)
{
	GError                  *verification_error;
	GdmSessionWorkerMessage *reply;
	gboolean                 res;

	worker->standard_output_fd = message->standard_output_fd;
	worker->standard_error_fd = message->standard_error_fd;

	verification_error = NULL;
	res = gdm_session_worker_verify_user (worker,
					      message->service_name,
					      (message->username_size >= 0) ? message->username : NULL,
					      message->hostname_is_provided ? message->hostname : NULL,
					      message->console_name,
					      TRUE /* password is required */,
					      &verification_error);
	if (! res) {
		g_assert (verification_error != NULL);

		g_message ("%s", verification_error->message);

		reply = gdm_session_worker_verification_failed_message_new (verification_error);

		g_error_free (verification_error);

		gdm_write_message (worker->message_pipe_fd, reply, reply->size, NULL);
		gdm_session_worker_message_free (reply);
		goto out;
	}

	/* Did start_program get called early? if so, process it now,
	 * otherwise we'll do it asynchronously later.
	 */
	if ((worker->arguments != NULL) &&
	    !gdm_session_worker_open_user_session (worker, &verification_error)) {
		g_assert (verification_error != NULL);

		g_message ("%s", verification_error->message);

		reply = gdm_session_worker_session_startup_failed_message_new (verification_error);

		g_error_free (verification_error);

		gdm_write_message (worker->message_pipe_fd, reply, reply->size, NULL);
		gdm_session_worker_message_free (reply);
		goto out;
	}

 out:
	;
}

static void
gdm_session_worker_handle_start_program_message (GdmSessionWorker  *worker,
						 GdmSessionStartProgramMessage *message)
{
	GError  *start_error;
	gboolean res;

	if (worker->arguments != NULL)
		g_strfreev (worker->arguments);

	worker->arguments = gdm_session_unflatten_arguments (message->arguments);

	/* Did start_program get called early? if so, we will process the request
	 * later, synchronously after getting credentials
	 */
	if (!worker->credentials_are_established)
		return;

	start_error = NULL;
	res = gdm_session_worker_open_user_session (worker, &start_error);
	if (! res) {
		GdmSessionWorkerMessage *message;

		g_assert (start_error != NULL);

		g_warning ("%s", start_error->message);

		message = gdm_session_worker_session_startup_failed_message_new (start_error);

		g_error_free (start_error);

		gdm_write_message (worker->message_pipe_fd, message, message->size, NULL);
		gdm_session_worker_message_free (message);
	}
}

static void
gdm_session_worker_handle_set_environment_variable_message (GdmSessionWorker  *worker,
							    GdmSessionSetEnvironmentVariableMessage *message)
{
	gdm_session_worker_set_environment_variable (worker,
						     message->environment_variable);
}

static gboolean
gdm_session_worker_process_asynchronous_message (GdmSessionWorker *worker,
						 GdmSessionMessage *message)
{
	switch (message->type) {
	case GDM_SESSION_MESSAGE_TYPE_VERIFICATION:
		g_debug ("received verification request from parent");
		gdm_session_worker_handle_verification_message (worker,
								(GdmSessionVerificationMessage *) message);
		return TRUE;

	case GDM_SESSION_MESSAGE_TYPE_START_PROGRAM:
		g_debug ("received new session arguments from parent");
		gdm_session_worker_handle_start_program_message (worker,
								 (GdmSessionStartProgramMessage *)
								 message);
		return TRUE;

	case GDM_SESSION_MESSAGE_TYPE_SET_ENVIRONMENT_VARIABLE:
		g_debug ("received new environment variable from parent");
		gdm_session_worker_handle_set_environment_variable_message (worker,
									    (GdmSessionSetEnvironmentVariableMessage *)
									    message);
		return TRUE;

	default:
		g_debug ("received unknown message with type '0x%x' from parent",
			 message->type);
		return FALSE;
	}

	return FALSE;
}


















static void
gdm_session_worker_unwatch_child (GdmSessionWorker *worker)
{
	if (worker->child_watch_source == NULL)
		return;

	g_source_destroy (worker->child_watch_source);
	worker->child_watch_source = NULL;
}

static int
gdm_get_max_open_fds (void)
{
	struct rlimit open_fd_limit;
	const int fallback_limit = GDM_MAX_OPEN_FILE_DESCRIPTORS;

	if (getrlimit (RLIMIT_NOFILE, &open_fd_limit) < 0) {
		g_debug ("could not get file descriptor limit: %s",
			 g_strerror (errno));
		g_debug ("returning fallback file descriptor limit of %d",
			 fallback_limit);
		return fallback_limit;
	}

	if (open_fd_limit.rlim_cur == RLIM_INFINITY) {
		g_debug ("currently no file descriptor limit, returning fallback limit of %d",
			 fallback_limit);
		return fallback_limit;
	}

	return (int) open_fd_limit.rlim_cur;
}

static void
gdm_session_worker_close_all_fds (GdmSessionWorker *worker)
{
	int max_open_fds, fd;

	max_open_fds = gdm_get_max_open_fds ();
	g_debug ("closing all file descriptors except those that are specifically "
		 "excluded");

	for (fd = 0; fd < max_open_fds; fd++) {
		GSList *node;
		for (node = worker->inherited_fd_list;
		     node != NULL;
		     node = node->next) {
			if (fd == GPOINTER_TO_INT (node->data))
				break;
		}

		if (node == NULL) {
			g_debug ("closing file descriptor '%d'", fd);
			close (fd);
		}
	}

	g_debug ("closed first '%d' file descriptors", max_open_fds);
}

static void
gdm_session_worker_close_open_fds (GdmSessionWorker *worker)
{
	/* using DIR instead of GDir because we need access to dirfd so
	 * that we can iterate through the fds and close them in one sweep.
	 * (if we just closed all of them then we would close the one we're using
	 * for reading the directory!)
	 */
	DIR *dir;
	struct dirent *entry;
	int fd, opendir_fd;
	gboolean should_use_fallback;

	should_use_fallback = FALSE;
	opendir_fd = -1;
	return;

	dir = opendir (GDM_OPEN_FILE_DESCRIPTORS_DIR);

	if (dir != NULL) {
		opendir_fd = dirfd (dir);
		g_debug ("opened '"GDM_OPEN_FILE_DESCRIPTORS_DIR"' on file descriptor '%d'", opendir_fd);
	}

	if ((dir == NULL) || (opendir_fd < 0)) {
		g_debug ("could not open "GDM_OPEN_FILE_DESCRIPTORS_DIR": %s", g_strerror (errno));
		should_use_fallback = TRUE;
	} else {
		g_debug ("reading files in '"GDM_OPEN_FILE_DESCRIPTORS_DIR"'");
		while ((entry = readdir (dir)) != NULL) {
			GSList *node;
			glong filename_as_number;
			char *byte_after_number;

			if (entry->d_name[0] == '.')
				continue;

			g_debug ("scanning filename '%s' for file descriptor number",
				 entry->d_name);
			fd = -1;
			filename_as_number = strtol (entry->d_name, &byte_after_number, 10);

			g_assert (byte_after_number != NULL);

			if ((*byte_after_number != '\0') ||
			    (filename_as_number < 0) ||
			    (filename_as_number >= G_MAXINT)) {
				g_debug ("filename '%s' does not appear to represent a "
					 "file descriptor: %s",
					 entry->d_name, strerror (errno));
				should_use_fallback = TRUE;
			} else {
				fd = (int) filename_as_number;
				g_debug ("filename '%s' represents file descriptor '%d'",
					 entry->d_name, fd);
				should_use_fallback = FALSE;
			}

			for (node = worker->inherited_fd_list;
			     node != NULL;
			     node = node->next) {
				if (fd == GPOINTER_TO_INT (node->data))
					break;
			}

			if ((node == NULL) &&
			    (fd != opendir_fd)) {
				g_debug ("closing file descriptor '%d'", fd);
				close (fd);
			} else {
				g_debug ("will not close file descriptor '%d' because it "
					 "is still neded", fd);
			}
		}
		g_debug ("closing directory '"GDM_OPEN_FILE_DESCRIPTORS_DIR"'");
		closedir (dir);
	}

	/* if /proc isn't mounted or something else is screwy,
	 * fall back to closing everything
	 */
	if (should_use_fallback) {
		gdm_session_worker_close_all_fds (worker);
	}
}



static GdmSessionWorkerMessage *
gdm_session_worker_verified_message_new (void)
{
	GdmSessionWorkerVerifiedMessage *message;
	gsize size;

	size = sizeof (GdmSessionWorkerVerifiedMessage) +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFIED;
	message->header.size = size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_verification_failed_message_new (GError *error)
{
	GdmSessionWorkerVerificationFailedMessage *message;
	gsize error_message_size, size;

	error_message_size = (error != NULL? strlen (error->message) + 1 : 0);
	size = sizeof (GdmSessionWorkerVerificationFailedMessage) +
		error_message_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_VERIFICATION_FAILED;
	message->header.size = size;

	if (error != NULL) {
		message->error_domain = error->domain;
		message->error_code = error->code;
		g_strlcpy (message->error_message, error->message,
			   error_message_size);
		message->error_message_size = error_message_size;
	} else {
		message->error_message_size = -1;
	}

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_info_request_message_new (const char *question)
{
	GdmSessionWorkerInfoRequestMessage *message;
	gsize question_size, size;

	g_assert (question != NULL);

	question_size = strlen (question) + 1;
	size = sizeof (GdmSessionWorkerInfoRequestMessage) +
		question_size +
		sizeof (GDM_SESSION_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_INFO_REQUEST;
	message->header.size = size;

	g_strlcpy (message->question, question, question_size);

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_username_changed_message_new (const char *new_username)
{
	GdmSessionWorkerUsernameChangedMessage *message;
	gsize username_size, size;

	username_size = (new_username != NULL? strlen (new_username) + 1 : 0);

	size = sizeof (GdmSessionWorkerUsernameChangedMessage) +
		username_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_USERNAME_CHANGED;
	message->header.size = size;

	if (new_username != NULL) {
		g_strlcpy (message->username, new_username, username_size);
		message->username_size = username_size;
	} else {
		message->username_size = -1;
	}

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_secret_info_request_message_new (const char *question)
{
	GdmSessionWorkerSecretInfoRequestMessage *message;
	gsize question_size, size;

	g_assert (question != NULL);

	question_size = strlen (question) + 1;

	size = sizeof (GdmSessionWorkerSecretInfoRequestMessage) +
		question_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_SECRET_INFO_REQUEST;
	message->header.size = size;

	g_strlcpy (message->question, question, question_size);
	message->question_size = question_size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_info_message_new (const char *info)
{
	GdmSessionWorkerInfoMessage *message;
	gsize info_size, size;

	g_assert (info != NULL);

	info_size = strlen (info) + 1;

	size = sizeof (GdmSessionWorkerInfoRequestMessage) +
		info_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_INFO;
	message->header.size = size;

	g_strlcpy (message->info, info, info_size);
	message->info_size = info_size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_problem_message_new (const char *problem)
{
	GdmSessionWorkerProblemMessage *message;
	gsize problem_size, size;

	g_assert (problem != NULL);

	problem_size = strlen (problem) + 1;

	size = sizeof (GdmSessionWorkerProblemMessage) +
		problem_size +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_PROBLEM;
	message->header.size = size;

	g_strlcpy (message->problem, problem, problem_size);
	message->problem_size = problem_size;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_session_started_message_new (GPid pid)
{
	GdmSessionWorkerSessionStartedMessage *message;
	gsize size;

	size = sizeof (GdmSessionWorkerSessionStartedMessage) +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_STARTED;
	message->header.size = size;

	message->pid = pid;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}


static GdmSessionWorkerMessage *
gdm_session_worker_session_exited_message_new (int exit_code)
{
	GdmSessionWorkerSessionExitedMessage *message;
	gsize size;

	size = sizeof (GdmSessionWorkerSessionExitedMessage) +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED;
	message->header.size = size;

	message->exit_code = exit_code;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static GdmSessionWorkerMessage *
gdm_session_worker_session_died_message_new (int signal_number)
{
	GdmSessionWorkerSessionDiedMessage *message;
	gsize size;

	size = sizeof (GdmSessionWorkerSessionDiedMessage) +
		sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL);

	message = g_slice_alloc0 (size);

	message->header.type = GDM_SESSION_WORKER_MESSAGE_TYPE_SESSION_EXITED;
	message->header.size = size;

	message->signal_number = signal_number;

	g_strlcpy ((char *) ((guint *) message) + size -
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL),
		   GDM_SESSION_WORKER_MESSAGE_SENTINAL,
		   sizeof (GDM_SESSION_WORKER_MESSAGE_SENTINAL));

	return (GdmSessionWorkerMessage *) message;
}

static void
gdm_session_worker_message_free (GdmSessionWorkerMessage *message)
{
	g_slice_free1 (message->size, message);
}

static void
gdm_session_worker_incoming_message_handler (GdmSessionWorker *worker)
{
	GdmSessionMessage *message;
	GError *error;

	error = NULL;
	message = gdm_session_worker_get_incoming_message (worker, &error);

	if (message == NULL) {
		g_assert (error != NULL);
		g_warning ("could not receive message from parent: %s\n",
			   error->message);
		g_error_free (error);

		/* FIXME: figure out what to do here
		 */
		return;
	}

	gdm_session_worker_process_asynchronous_message (worker, message);

	gdm_session_message_free (message);
}

static gboolean
gdm_session_worker_data_on_message_pipe_handler (GIOChannel     *channel,
						 GIOCondition    condition,
						 GdmSessionWorker *worker)
{
	if ((condition & G_IO_IN) || (condition & G_IO_PRI)) {
		g_debug ("got message from message pipe");
		gdm_session_worker_incoming_message_handler (worker);
	}

	if ((condition & G_IO_ERR) || (condition & G_IO_HUP)) {
		g_debug ("got disconnected message from message pipe");
		gdm_session_worker_disconnected_handler (worker);
		return FALSE;
	}

	return TRUE;
}

static void
gdm_session_worker_watch_message_pipe (GdmSessionWorker *worker)
{
	GIOChannel *io_channel;

	io_channel = g_io_channel_unix_new (worker->message_pipe_fd);
	g_io_channel_set_close_on_unref (io_channel, TRUE);

	worker->message_pipe_source = g_io_create_watch (io_channel,
							 G_IO_IN | G_IO_HUP);
	g_io_channel_unref (io_channel);

	g_source_set_callback (worker->message_pipe_source,
			       (GSourceFunc) (GIOFunc)
			       gdm_session_worker_data_on_message_pipe_handler,
			       worker,
			       (GDestroyNotify)
			       gdm_session_worker_clear_message_pipe_source);
	g_source_attach (worker->message_pipe_source,
			 g_main_loop_get_context (worker->event_loop));
	g_source_unref (worker->message_pipe_source);
}

static void
gdm_session_worker_wait_for_messages (GdmSessionWorker *worker)
{
	gdm_session_worker_watch_message_pipe (worker);

	g_main_loop_run (worker->event_loop);
}

static gboolean
gdm_open_bidirectional_pipe (int    *fd1,
                             int    *fd2,
                             char  **error_message)
{
	int pipe_fds[2];

	g_assert (fd1 != NULL);
	g_assert (fd2 != NULL);

	if (socketpair (AF_UNIX, SOCK_DGRAM, 0, pipe_fds) < 0) {
		if (error_message != NULL)
			*error_message = g_strdup_printf ("%s",
							  g_strerror (errno));
		return FALSE;
	}

	if (fcntl (pipe_fds[0], F_SETFD, FD_CLOEXEC) < 0) {
		if (error_message != NULL)
			*error_message = g_strdup_printf ("%s",
							  g_strerror (errno));
		close (pipe_fds[0]);
		close (pipe_fds[1]);
		return FALSE;
	}

	if (fcntl (pipe_fds[1], F_SETFD, FD_CLOEXEC) < 0) {
		if (error_message != NULL)
			*error_message = g_strdup_printf ("%s",
							  g_strerror (errno));
		close (pipe_fds[0]);
		close (pipe_fds[1]);
		return FALSE;
	}

	*fd1 = pipe_fds[0];
	*fd2 = pipe_fds[1];

	return TRUE;
}

static GdmSessionWorker *
gdm_session_worker_new (void)
{
	GdmSessionWorker *worker;
	GMainContext *context;

	worker = g_slice_new0 (GdmSessionWorker);

	context = g_main_context_new ();
	worker->event_loop = g_main_loop_new (context, FALSE);
	g_main_context_unref (context);

	worker->pam_handle = NULL;

	worker->message_pipe_fd = -1;
	worker->message_pipe_source = NULL;

	worker->inherited_fd_list = NULL;

	worker->username = NULL;
	worker->arguments = NULL;

	worker->environment = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     (GDestroyNotify) g_free,
						     (GDestroyNotify) g_free);
	worker->standard_output_fd = -1;
	worker->standard_error_fd = -1;

	worker->exit_code = 127;

	worker->credentials_are_established = FALSE;
	worker->is_running = FALSE;

	return worker;
}

static void
gdm_session_worker_free (GdmSessionWorker *worker)
{
	if (worker == NULL)
		return;

	gdm_session_worker_unwatch_child (worker);

	if (worker->message_pipe_fd != -1) {
		close (worker->message_pipe_fd);
		worker->message_pipe_fd = -1;
	}

	if (worker->username != NULL) {
		g_free (worker->username);
		worker->username = NULL;
	}

	if (worker->arguments != NULL) {
		g_strfreev (worker->arguments);
		worker->arguments = NULL;
	}

	if (worker->environment != NULL) {
		g_hash_table_destroy (worker->environment);
		worker->environment = NULL;
	}

	if (worker->standard_output_fd >= 0) {
		close (worker->standard_output_fd);
		worker->standard_output_fd = -1;
	}

	if (worker->standard_error_fd >= 0) {
		close (worker->standard_error_fd);
		worker->standard_error_fd = -1;
	}

	if (worker->event_loop != NULL) {
		g_main_loop_unref (worker->event_loop);
		worker->event_loop = NULL;
	}

	g_slice_free (GdmSessionWorker, worker);
}

static gboolean
gdm_session_create_worker (GdmSession  *session,
			   int       standard_output_fd,
			   int       standard_error_fd,
			   int      *worker_fd,
			   GPid      *worker_pid,
			   GError   **error)
{
	GdmSessionWorker *worker;
	int session_message_pipe_fd, worker_message_pipe_fd;
	char *error_message;
	pid_t child_pid;

	session_message_pipe_fd = -1;
	worker_message_pipe_fd = -1;
	error_message = NULL;
	if (!gdm_open_bidirectional_pipe (&session_message_pipe_fd,
					  &worker_message_pipe_fd,
					  &error_message)) {
		g_debug ("unable open message pipe: %s", error_message);
		g_set_error (error,
			     GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_OPENING_MESSAGE_PIPE,
			     "%s", error_message);
		g_free (error_message);
		return FALSE;
	}

	g_assert (session_message_pipe_fd >= 0);
	g_assert (worker_message_pipe_fd >= 0);

	child_pid = fork ();

	if (child_pid < 0) {
		g_set_error (error, GDM_SESSION_ERROR,
			     GDM_SESSION_ERROR_FORKING,
			     "%s", g_strerror (errno));
		return FALSE;
	}

	if (child_pid == 0) {
		GError *child_error;
		int fd;

		setsid ();

		worker = gdm_session_worker_new ();
		worker->message_pipe_fd = worker_message_pipe_fd;
		worker->inherited_fd_list =
			g_slist_append (worker->inherited_fd_list,
					GINT_TO_POINTER (worker->message_pipe_fd));

		if (standard_output_fd >= 0)
			worker->inherited_fd_list =
				g_slist_append (worker->inherited_fd_list,
						GINT_TO_POINTER (standard_output_fd));
		if (standard_error_fd >= 0)
			worker->inherited_fd_list =
				g_slist_append (worker->inherited_fd_list,
						GINT_TO_POINTER (standard_error_fd));

#if 0
		gdm_session_worker_close_open_fds (worker);

		fd = gdm_open_dev_null (O_RDWR);
		dup2 (fd, STDIN_FILENO);

		if (standard_output_fd >= 0 &&
		    standard_output_fd != STDOUT_FILENO)
			dup2 (standard_output_fd, STDOUT_FILENO);
		else
			dup2 (fd, STDOUT_FILENO);

		if (standard_error_fd >= 0 &&
		    standard_error_fd != STDERR_FILENO)
			dup2 (standard_error_fd, STDERR_FILENO);
		else
			dup2 (fd, STDERR_FILENO);
		close (fd);
#endif

		g_debug ("waiting for messages from parent");
		child_error = NULL;

		gdm_session_worker_wait_for_messages (worker);

		g_debug ("exiting with code '%d'", worker->exit_code);
		gdm_session_worker_free (worker);
		_exit (worker->exit_code);
	}
	close (worker_message_pipe_fd);

	g_assert (child_pid != 0);
	if (worker_pid != NULL)
		*worker_pid = (GPid) child_pid;

	if (worker_fd != NULL)
		*worker_fd = session_message_pipe_fd;

	return TRUE;
}

static gboolean
gdm_session_open_with_worker (GdmSession   *session,
                              const char  *service_name,
                              const char  *username,
                              const char  *hostname,
                              const char  *console_name,
                              int standard_output_fd,
                              int standard_error_fd,
                              GError      **error)
{
	GdmSessionMessage *message;
	GError *worker_error;
	int worker_message_pipe_fd;
	GPid worker_pid;
	gboolean worker_is_created;

	worker_error = NULL;
	worker_is_created = gdm_session_create_worker (session,
						       standard_output_fd,
						       standard_error_fd,
						       &worker_message_pipe_fd,
						       &worker_pid,
						       &worker_error);
	if (!worker_is_created) {
		g_debug ("worker could not be created");
		g_propagate_error (error, worker_error);
		return FALSE;
	}

	session->priv->service_name = g_strdup (service_name);

	session->priv->arguments = NULL;
	session->priv->username = g_strdup (username);

	session->priv->hostname = g_strdup (hostname);
	session->priv->console_name = g_strdup (console_name);
	session->priv->standard_output_fd = standard_output_fd;
	session->priv->standard_error_fd = standard_error_fd;

	session->priv->worker_message_pipe_fd = worker_message_pipe_fd;
	session->priv->worker_pid = worker_pid;
	session->priv->next_expected_message = GDM_SESSION_MESSAGE_TYPE_VERIFICATION;
	session->priv->query_answer = NULL;

	gdm_session_watch_child (session);

	message = gdm_session_verification_message_new (service_name,
							username,
							hostname,
							console_name,
							standard_output_fd,
							standard_error_fd);
	gdm_write_message (session->priv->worker_message_pipe_fd, message, message->size,
			   NULL);
	gdm_session_message_free (message);

	return TRUE;
}

gboolean
gdm_session_open (GdmSession   *session,
                  const char  *service_name,
                  const char  *hostname,
                  const char  *console_name,
                  int standard_output_fd,
                  int standard_error_fd,
                  GError      **error)
{
	g_return_val_if_fail (session != NULL, FALSE);
	g_return_val_if_fail (service_name != NULL, FALSE);
	g_return_val_if_fail (console_name != NULL, FALSE);

	return gdm_session_open_with_worker (session, service_name,
					     NULL, hostname, console_name,
					     standard_output_fd,
					     standard_error_fd, error);
}

gboolean
gdm_session_open_for_user (GdmSession    *session,
                           const char *service_name,
                           const char  *username,
                           const char *hostname,
                           const char *console_name,
                           int standard_output_fd,
                           int standard_error_fd,
                           GError     **error)
{
	g_return_val_if_fail (session != NULL, FALSE);
	g_return_val_if_fail (service_name != NULL, FALSE);
	g_return_val_if_fail (username != NULL, FALSE);
	g_return_val_if_fail (console_name != NULL, FALSE);

	return gdm_session_open_with_worker (session,
					     service_name,
					     username, hostname, console_name,
					     standard_output_fd,
					     standard_error_fd, error);
}

void
gdm_session_start_program (GdmSession         *session,
                           const char * const *args)
{
	GdmSessionMessage *message;
	int argc, i;

	g_return_if_fail (session != NULL);
	g_return_if_fail (session != NULL);
	g_return_if_fail (gdm_session_is_running (session) == FALSE);
	g_return_if_fail (args != NULL);
	g_return_if_fail (args[0] != NULL);

	argc = g_strv_length ((char **) args);
	session->priv->arguments = g_new0 (char *, (argc + 1));

	for (i = 0; args[i] != NULL; i++)
		session->priv->arguments[i] = g_strdup (args[i]);

	message = gdm_session_start_program_message_new (args);
	gdm_write_message (session->priv->worker_message_pipe_fd, message, message->size,
			   NULL);
	gdm_session_message_free (message);
}

void
gdm_session_close (GdmSession *session)
{
	g_return_if_fail (session != NULL);

	gdm_session_unwatch_child (session);
	session->priv->next_expected_message = GDM_SESSION_MESSAGE_TYPE_VERIFICATION;

	if (session->priv->worker_message_pipe_fd > 0) {
		close (session->priv->worker_message_pipe_fd);
		session->priv->worker_message_pipe_fd = -1;
	}

	if (session->priv->worker_pid > 0) {
		if (session->priv->is_running)
			gdm_session_write_record (session,
						  GDM_SESSION_RECORD_TYPE_LOGOUT);
		kill (-session->priv->worker_pid, SIGTERM);
		waitpid (session->priv->worker_pid, NULL, 0);
		session->priv->worker_pid = -1;
	}

	session->priv->is_running = FALSE;
	session->priv->is_verified = FALSE;

	if (session->priv->service_name) {
		g_free (session->priv->service_name);
		session->priv->service_name = NULL;
	}

	if (session->priv->arguments) {
		g_strfreev (session->priv->arguments);
		session->priv->arguments = NULL;
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
	return session->priv->is_running;
}

void
gdm_session_set_environment_variable (GdmSession      *session,
                                      const char     *key,
                                      const char     *value)
{
	GdmSessionMessage *message;

	g_return_if_fail (session != NULL);
	g_return_if_fail (session != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	message = gdm_session_set_environment_variable_message_new (key, value);
	gdm_write_message (session->priv->worker_message_pipe_fd, message, message->size,
			   NULL);
	gdm_session_message_free (message);
}

void
gdm_session_answer_query  (GdmSession        *session,
                           const char     *answer)
{
	GdmSessionMessage *reply;

	g_return_if_fail (session != NULL);

	if (session->priv->query_answer != NULL)
		g_free (session->priv->query_answer);

	session->priv->query_answer = g_strdup (answer);

	reply = NULL;
	switch (session->priv->next_expected_message) {
	case GDM_SESSION_MESSAGE_TYPE_INFO_REPLY:
		reply = gdm_session_info_reply_message_new (session->priv->query_answer);
		break;

	case GDM_SESSION_MESSAGE_TYPE_SECRET_INFO_REPLY:
		reply = gdm_session_secret_info_reply_message_new (session->priv->query_answer);
		break;

	default:
		break;
	}
	g_free (session->priv->query_answer);
	session->priv->query_answer = NULL;

	if (reply != NULL) {
		gdm_write_message (session->priv->worker_message_pipe_fd, reply, reply->size,
				   NULL);
		gdm_session_message_free (reply);
	}

	session->priv->next_expected_message = GDM_SESSION_MESSAGE_TYPE_INVALID;
}

char *
gdm_session_get_username (GdmSession *session)
{
	g_return_val_if_fail (session != NULL, NULL);

	return g_strdup (session->priv->username);
}
