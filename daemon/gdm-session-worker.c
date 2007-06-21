/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
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
#include <grp.h>
#include <pwd.h>

#include <security/pam_appl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-session-worker.h"
#include "gdm-marshal.h"

#define GDM_SESSION_WORKER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_WORKER, GdmSessionWorkerPrivate))

#define GDM_SESSION_DBUS_PATH      "/org/gnome/DisplayManager/Session"
#define GDM_SESSION_DBUS_INTERFACE "org.gnome.DisplayManager.Session"

#ifndef GDM_PASSWD_AUXILLARY_BUFFER_SIZE
#define GDM_PASSWD_AUXILLARY_BUFFER_SIZE 1024
#endif

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin:/usr/X11R6/bin"
#endif

#ifndef GDM_SESSION_ROOT_UID
#define GDM_SESSION_ROOT_UID 0
#endif

#define MESSAGE_REPLY_TIMEOUT (10 * 60 * 1000)

struct GdmSessionWorkerPrivate
{
	int               exit_code;

	pam_handle_t     *pam_handle;

	GPid              child_pid;
	guint             child_watch_id;

	char             *username;
	char            **arguments;

	GHashTable       *environment;

	guint32           credentials_are_established : 1;
	guint32           is_running : 1;

	char             *server_address;
	DBusGConnection  *connection;
	DBusGProxy       *server_proxy;
};

enum {
	PROP_0,
	PROP_SERVER_ADDRESS,
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
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_session_worker_class_init	(GdmSessionWorkerClass *klass);
static void	gdm_session_worker_init	        (GdmSessionWorker      *session_worker);
static void	gdm_session_worker_finalize	(GObject               *object);

typedef int (* GdmSessionWorkerPamNewMessagesFunc) (int,
						    const struct pam_message **,
						    struct pam_response **,
						    gpointer);

G_DEFINE_TYPE (GdmSessionWorker, gdm_session_worker, G_TYPE_OBJECT)

GQuark
gdm_session_worker_error_quark (void)
{
	static GQuark error_quark = 0;

	if (error_quark == 0)
		error_quark = g_quark_from_static_string ("gdm-session-worker");

	return error_quark;
}

/* adapted from glib script_execute */
static void
script_execute (const gchar *file,
		char       **argv,
		char       **envp,
		gboolean     search_path)
{
	/* Count the arguments.  */
	int argc = 0;

	while (argv[argc])
		++argc;

	/* Construct an argument list for the shell.  */
	{
		char **new_argv;

		new_argv = g_new0 (gchar*, argc + 2); /* /bin/sh and NULL */

		new_argv[0] = (char *) "/bin/sh";
		new_argv[1] = (char *) file;
		while (argc > 0) {
			new_argv[argc + 1] = argv[argc];
			--argc;
		}

		/* Execute the shell. */
		if (envp)
			execve (new_argv[0], new_argv, envp);
		else
			execv (new_argv[0], new_argv);

		g_free (new_argv);
	}
}

static char *
my_strchrnul (const char *str, char c)
{
	char *p = (char*) str;
	while (*p && (*p != c))
		++p;

	return p;
}

/* adapted from glib g_execute */
static gint
gdm_session_execute (const char *file,
		     char      **argv,
		     char      **envp,
		     gboolean    search_path)
{
	if (*file == '\0') {
		/* We check the simple case first. */
		errno = ENOENT;
		return -1;
	}

	if (!search_path || strchr (file, '/') != NULL) {
		/* Don't search when it contains a slash. */
		if (envp)
			execve (file, argv, envp);
		else
			execv (file, argv);

		if (errno == ENOEXEC)
			script_execute (file, argv, envp, FALSE);
	} else {
		gboolean got_eacces = 0;
		const char *path, *p;
		char *name, *freeme;
		gsize len;
		gsize pathlen;

		path = g_getenv ("PATH");
		if (path == NULL) {
			/* There is no `PATH' in the environment.  The default
			 * search path in libc is the current directory followed by
			 * the path `confstr' returns for `_CS_PATH'.
			 */

			/* In GLib we put . last, for security, and don't use the
			 * unportable confstr(); UNIX98 does not actually specify
			 * what to search if PATH is unset. POSIX may, dunno.
			 */

			path = "/bin:/usr/bin:.";
		}

		len = strlen (file) + 1;
		pathlen = strlen (path);
		freeme = name = g_malloc (pathlen + len + 1);

		/* Copy the file name at the top, including '\0'  */
		memcpy (name + pathlen + 1, file, len);
		name = name + pathlen;
		/* And add the slash before the filename  */
		*name = '/';

		p = path;
		do {
			char *startp;

			path = p;
			p = my_strchrnul (path, ':');

			if (p == path)
				/* Two adjacent colons, or a colon at the beginning or the end
				 * of `PATH' means to search the current directory.
				 */
				startp = name + 1;
			else
				startp = memcpy (name - (p - path), path, p - path);

			/* Try to execute this name.  If it works, execv will not return.  */
			if (envp)
				execve (startp, argv, envp);
			else
				execv (startp, argv);

			if (errno == ENOEXEC)
				script_execute (startp, argv, envp, search_path);

			switch (errno) {
			case EACCES:
				/* Record the we got a `Permission denied' error.  If we end
				 * up finding no executable we can use, we want to diagnose
				 * that we did find one but were denied access.
				 */
				got_eacces = TRUE;

				/* FALL THRU */

			case ENOENT:
#ifdef ESTALE
			case ESTALE:
#endif
#ifdef ENOTDIR
			case ENOTDIR:
#endif
				/* Those errors indicate the file is missing or not executable
				 * by us, in which case we want to just try the next path
				 * directory.
				 */
				break;

			default:
				/* Some other error means we found an executable file, but
				 * something went wrong executing it; return the error to our
				 * caller.
				 */
				g_free (freeme);
				return -1;
			}
		} while (*p++ != '\0');

		/* We tried every element and none of them worked.  */
		if (got_eacces)
			/* At least one failure was due to permissions, so report that
			 * error.
			 */
			errno = EACCES;

		g_free (freeme);
	}

	/* Return the error from the last attempt (probably ENOENT).  */
	return -1;
}

static void
send_user_verified (GdmSessionWorker *worker)
{
	GError *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "Verified",
				 &error,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send Verified: %s", error->message);
		g_error_free (error);
	}
}

static void
send_startup_failed (GdmSessionWorker *worker,
		     const char       *message)
{
	GError *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "StartupFailed",
				 &error,
				 G_TYPE_STRING, message,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send StartupFailed: %s", error->message);
		g_error_free (error);
	}
}

static void
send_session_exited (GdmSessionWorker *worker,
		     int               code)
{
	GError  *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "SessionExited",
				 &error,
				 G_TYPE_INT, code,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SessionExited: %s", error->message);
		g_error_free (error);
	}
}

static void
send_session_died (GdmSessionWorker *worker,
		   int               num)
{
	GError  *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "SessionDied",
				 &error,
				 G_TYPE_INT, num,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SessionDied: %s", error->message);
		g_error_free (error);
	}
}

static void
send_username_changed (GdmSessionWorker *worker)
{
	GError  *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "UsernameChanged",
				 &error,
				 G_TYPE_STRING, worker->priv->username,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send UsernameChanged: %s", error->message);
		g_error_free (error);
	}
}

static void
send_user_verification_error (GdmSessionWorker *worker,
			      const char       *message)
{
	GError  *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "VerificationFailed",
				 &error,
				 G_TYPE_STRING, message,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send VerificationFailed: %s", error->message);
		g_error_free (error);
	}
}

static void
send_session_started (GdmSessionWorker *worker,
		      GPid              pid)
{
	GError  *error;
	gboolean res;

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "SessionStarted",
				 &error,
				 G_TYPE_INT, (int)pid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send SessionStarted: %s", error->message);
		g_error_free (error);
	}
}

static gboolean
gdm_session_worker_get_username (GdmSessionWorker  *worker,
				 char             **username)
{
	gconstpointer item;

	g_assert (worker->priv->pam_handle != NULL);

	if (pam_get_item (worker->priv->pam_handle, PAM_USER, &item) == PAM_SUCCESS) {
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
	char    *username;
	gboolean res;

	username = NULL;
	res = gdm_session_worker_get_username (worker, &username);
	if (res) {
		if ((worker->priv->username == username) ||
		    ((worker->priv->username != NULL) && (username != NULL) &&
		     (strcmp (worker->priv->username, username) == 0)))
			goto out;

		g_debug ("setting username to '%s'", username);

		g_free (worker->priv->username);
		worker->priv->username = username;
		username = NULL;

		send_username_changed (worker);
	}

 out:
	g_free (username);
}

static gboolean
gdm_session_worker_ask_question (GdmSessionWorker *worker,
				 const char       *question,
				 char            **answer)
{
	GError  *error;
	gboolean res;

	g_assert (answer != NULL);

	error = NULL;
	res = dbus_g_proxy_call_with_timeout (worker->priv->server_proxy,
					      "InfoQuery",
					      MESSAGE_REPLY_TIMEOUT,
					      &error,
					      G_TYPE_STRING, question,
					      G_TYPE_INVALID,
					      G_TYPE_STRING, answer,
					      G_TYPE_INVALID);
	if (! res) {
		/* FIXME: handle timeout */
		g_warning ("Unable to send InfoQuery: %s", error->message);
		g_error_free (error);
	}

	return res;
}

static gboolean
gdm_session_worker_ask_for_secret (GdmSessionWorker *worker,
				   const char       *secret,
				   char            **answer)
{
	GError  *error;
	gboolean res;

	g_debug ("Secret info query: %s", secret);

	g_assert (answer != NULL);

	error = NULL;
	res = dbus_g_proxy_call_with_timeout (worker->priv->server_proxy,
					      "SecretInfoQuery",
					      MESSAGE_REPLY_TIMEOUT,
					      &error,
					      G_TYPE_STRING, secret,
					      G_TYPE_INVALID,
					      G_TYPE_STRING, answer,
					      G_TYPE_INVALID);
	if (! res) {
		/* FIXME: handle timeout */
		g_warning ("Unable to send SecretInfoQuery: %s", error->message);
		g_error_free (error);
	}

	return res;
}

static gboolean
gdm_session_worker_report_info (GdmSessionWorker *worker,
				const char       *info)
{
	GError *error;
	gboolean res;

	g_debug ("Info: %s", info);

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "Info",
				 &error,
				 G_TYPE_STRING, info,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send Info: %s", error->message);
		g_error_free (error);
	}

	return res;
}

static gboolean
gdm_session_worker_report_problem (GdmSessionWorker *worker,
				   const char       *problem)
{
	GError *error;
	gboolean res;

	g_debug ("Problem: %s", problem);

	error = NULL;
	res = dbus_g_proxy_call (worker->priv->server_proxy,
				 "Problem",
				 &error,
				 G_TYPE_STRING, problem,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (! res) {
		g_warning ("Unable to send Problem: %s", error->message);
		g_error_free (error);
	}

	return res;
}

static char *
convert_to_utf8 (const char *str)
{
	char *utf8;
	utf8 = g_locale_to_utf8 (str,
				 -1,
				 NULL,
				 NULL,
				 NULL);

	/* if we couldn't convert text from locale then
	 * assume utf-8 and hope for the best */
	if (utf8 == NULL) {
		char *p;
		char *q;

		utf8 = g_strdup (str);

		p = utf8;
		while (*p != '\0' && !g_utf8_validate ((const char *)p, -1, (const char **)&q)) {
			*q = '?';
			p = q + 1;
		}
	}

	return utf8;
}

static gboolean
gdm_session_worker_process_pam_message (GdmSessionWorker          *worker,
					const struct pam_message  *query,
					char                     **response_text)
{
	char    *user_answer;
	gboolean res;
	char    *utf8_msg;

	if (response_text != NULL) {
		*response_text = NULL;
	}

	g_debug ("received pam message of type %u with payload '%s'",
		 query->msg_style, query->msg);

	utf8_msg = convert_to_utf8 (query->msg);

	user_answer = NULL;
	res = FALSE;
	switch (query->msg_style) {
	case PAM_PROMPT_ECHO_ON:
		res = gdm_session_worker_ask_question (worker, utf8_msg, &user_answer);
		break;
	case PAM_PROMPT_ECHO_OFF:
		res = gdm_session_worker_ask_for_secret (worker, utf8_msg, &user_answer);
		break;
	case PAM_TEXT_INFO:
		res = gdm_session_worker_report_info (worker, utf8_msg);
		break;
	case PAM_ERROR_MSG:
		res = gdm_session_worker_report_problem (worker, utf8_msg);
		break;
	default:
		g_debug ("unknown query of type %u\n", query->msg_style);
		break;
	}

	if (user_answer != NULL) {
		/* we strdup and g_free to make sure we return malloc'd
		 * instead of g_malloc'd memory
		 */
		if (res && response_text != NULL) {
			*response_text = strdup (user_answer);
		}

		g_free (user_answer);

		g_debug ("trying to get updated username");
		gdm_session_worker_update_username (worker);
		res = TRUE;
	}

	g_free (utf8_msg);

	return res;
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

	g_debug ("%d new messages received from PAM\n", number_of_messages);

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
		char    *response_text;

		response_text = NULL;
		got_response = gdm_session_worker_process_pam_message (worker,
								       messages[i],
								       &response_text);
		if (!got_response) {
			goto out;
		}

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

	g_debug ("PAM conversation returning %d", return_value);

	return return_value;
}

static void
gdm_session_worker_uninitialize_pam (GdmSessionWorker *worker,
				     int               error_code)
{
	g_debug ("uninitializing PAM");

	if (worker->priv->pam_handle == NULL)
		return;

	if (worker->priv->credentials_are_established) {
		pam_setcred (worker->priv->pam_handle, PAM_DELETE_CRED);
		worker->priv->credentials_are_established = FALSE;
	}

	if (worker->priv->is_running) {
		pam_close_session (worker->priv->pam_handle, 0);
		worker->priv->is_running = FALSE;
	}

	pam_end (worker->priv->pam_handle, error_code);
	worker->priv->pam_handle = NULL;
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

	g_assert (worker->priv->pam_handle == NULL);

	g_debug ("initializing PAM");

	pam_conversation.conv = (GdmSessionWorkerPamNewMessagesFunc) gdm_session_worker_pam_new_messages_handler;
	pam_conversation.appdata_ptr = worker;

	error_code = pam_start (service,
				username,
				&pam_conversation,
				&worker->priv->pam_handle);

	if (error_code != PAM_SUCCESS) {
		g_debug ("could not initialize pam");
		/* we don't use pam_strerror here because it requires a valid
		 * pam handle, and if pam_start fails pam_handle is undefined
		 */
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
			     _("error initiating conversation with authentication system - %s"),
			     error_code == PAM_ABORT? _("general failure") :
			     error_code == PAM_BUF_ERR? _("out of memory") :
			     error_code == PAM_SYSTEM_ERR? _("application programmer error") :
			     _("unscoped error"));

		goto out;
	}

	if (username == NULL) {
		error_code = pam_set_item (worker->priv->pam_handle, PAM_USER_PROMPT, _("Username:"));

		if (error_code != PAM_SUCCESS) {
			g_set_error (error,
				     GDM_SESSION_WORKER_ERROR,
				     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
				     _("error informing authentication system of preferred username prompt - %s"),
				     pam_strerror (worker->priv->pam_handle, error_code));
			goto out;
		}
	}

	if (hostname != NULL) {
		error_code = pam_set_item (worker->priv->pam_handle, PAM_RHOST, hostname);

		if (error_code != PAM_SUCCESS) {
			g_set_error (error,
				     GDM_SESSION_WORKER_ERROR,
				     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
				     _("error informing authentication system of user's hostname - %s"),
				     pam_strerror (worker->priv->pam_handle, error_code));
			goto out;
		}
	}

	error_code = pam_set_item (worker->priv->pam_handle, PAM_TTY, console_name);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
			     _("error informing authentication system of user's console - %s"),
			     pam_strerror (worker->priv->pam_handle, error_code));
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
	error_code = pam_authenticate (worker->priv->pam_handle, authentication_flags);

	if (error_code != PAM_SUCCESS) {
		g_debug ("authentication returned %d: %s", error_code, pam_strerror (worker->priv->pam_handle, error_code));

		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_AUTHENTICATING,
			     "%s", pam_strerror (worker->priv->pam_handle, error_code));
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
	error_code = pam_acct_mgmt (worker->priv->pam_handle, authentication_flags);

	/* it's possible that the user needs to change their password or pin code
	 */
	if (error_code == PAM_NEW_AUTHTOK_REQD)
		error_code = pam_chauthtok (worker->priv->pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);

	if (error_code != PAM_SUCCESS) {
		g_debug ("user is not authorized to log in: %s",
			 pam_strerror (worker->priv->pam_handle, error_code));
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_AUTHORIZING,
			     "%s", pam_strerror (worker->priv->pam_handle, error_code));
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
					     const char       *key,
					     const char       *value)
{
	/* FIXME: maybe we should use use pam_putenv instead of our
	 * own hash table, so pam can override our choices if it knows
	 * better?
	 */
	g_hash_table_replace (worker->priv->environment,
			      g_strdup (key),
			      g_strdup (value));
}

static void
gdm_session_worker_update_environment_from_passwd_entry (GdmSessionWorker *worker,
							 struct passwd    *passwd_entry)
{
	gdm_session_worker_set_environment_variable (worker, "LOGNAME", worker->priv->username);
	gdm_session_worker_set_environment_variable (worker, "USER", worker->priv->username);
	gdm_session_worker_set_environment_variable (worker, "USERNAME", worker->priv->username);
	gdm_session_worker_set_environment_variable (worker, "HOME", passwd_entry->pw_dir);
	gdm_session_worker_set_environment_variable (worker, "SHELL", passwd_entry->pw_shell);
}

static gboolean
gdm_session_worker_environment_variable_is_set (GdmSessionWorker *worker,
						const char       *name)
{
	return g_hash_table_lookup (worker->priv->environment, name) != NULL;
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

	if (worker->priv->username == NULL) {
		error_code = PAM_USER_UNKNOWN;
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
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
	errno = getpwnam_r (worker->priv->username,
			    &passwd_buffer,
			    aux_buffer,
			    (size_t) aux_buffer_size,
			    &passwd_entry);

	if (errno != 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     "%s",
			     g_strerror (errno));
		goto out;
	}

	if (passwd_entry == NULL) {
		error_code = PAM_USER_UNKNOWN;
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     _("user account not available on system"));
		goto out;
	}

	gdm_session_worker_update_environment_from_passwd_entry (worker, passwd_entry);

	/* Let's give the user a default PATH if he doesn't already have one
	 */
	if (!gdm_session_worker_environment_variable_is_set (worker, "PATH")) {
		gdm_session_worker_set_environment_variable (worker, "PATH", GDM_SESSION_DEFAULT_PATH);
	}

	/* pam_setcred wants to be called as the authenticated user
	 * but pam_open_session needs to be called as super-user.
	 *
	 * Set the real uid and gid to the user and give the user a
	 * temporary super-user effective id.
	 */
	if (setreuid (passwd_entry->pw_uid, GDM_SESSION_ROOT_UID) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	if (setgid (passwd_entry->pw_gid) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	if (initgroups (passwd_entry->pw_name, passwd_entry->pw_gid) < 0) {
		error_code = PAM_SYSTEM_ERR;
		g_set_error (error, GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     "%s", g_strerror (errno));
		goto out;
	}

	error_code = pam_setcred (worker->priv->pam_handle, PAM_ESTABLISH_CRED);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_GIVING_CREDENTIALS,
			     "%s",
			     pam_strerror (worker->priv->pam_handle, error_code));
		goto out;
	}

	worker->priv->credentials_are_established = TRUE;

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
				const char        *hostname,
				const char        *console_name,
				const char        *username,
				gboolean           password_is_required,
				GError           **error)
{
	GError   *pam_error;
	gboolean  res;

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
		g_debug ("Unable to verify user");
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

	send_user_verified (worker);

	return TRUE;
}

static void
gdm_session_worker_update_environment_from_pam (GdmSessionWorker *worker)
{
	char **environment;
	gsize i;

	environment = pam_getenvlist (worker->priv->pam_handle);

	for (i = 0; environment[i] != NULL; i++) {
		char **key_and_value;

		key_and_value = g_strsplit (environment[i], "=", 2);

		gdm_session_worker_set_environment_variable (worker, key_and_value[0], key_and_value[1]);

		g_strfreev (key_and_value);
	}

	for (i = 0; environment[i]; i++) {
		free (environment[i]);
	}

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
	g_hash_table_foreach (worker->priv->environment,
			      (GHFunc) gdm_session_worker_fill_environment_array,
			      environment);
	g_ptr_array_add (environment, NULL);

	return (char **) g_ptr_array_free (environment, FALSE);
}

static void
session_worker_child_watch (GPid              pid,
			    int               status,
			    GdmSessionWorker *worker)
{
	g_debug ("child (pid:%d) done (%s:%d)",
		 (int) pid,
		 WIFEXITED (status) ? "status"
		 : WIFSIGNALED (status) ? "signal"
		 : "unknown",
		 WIFEXITED (status) ? WEXITSTATUS (status)
		 : WIFSIGNALED (status) ? WTERMSIG (status)
		 : -1);

	if (WIFEXITED (status)) {
		int code = WEXITSTATUS (status);

		send_session_exited (worker, code);
	} else if (WIFSIGNALED (status)) {
		int num = WTERMSIG (status);

		send_session_died (worker, num);
	}

	worker->priv->child_pid = -1;
}

static void
gdm_session_worker_watch_child (GdmSessionWorker *worker)
{

	worker->priv->child_watch_id = g_child_watch_add (worker->priv->child_pid,
							  (GChildWatchFunc)session_worker_child_watch,
							  worker);

}

static gboolean
gdm_session_worker_open_user_session (GdmSessionWorker  *worker,
				      GError           **error)
{
	int    error_code;
	pid_t  session_pid;

	g_assert (!worker->priv->is_running);
	g_assert (geteuid () == 0);
	error_code = pam_open_session (worker->priv->pam_handle, 0);

	if (error_code != PAM_SUCCESS) {
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
			     "%s", pam_strerror (worker->priv->pam_handle, error_code));
		goto out;
	}
	worker->priv->is_running = TRUE;

	g_debug ("querying pam for user environment");
	gdm_session_worker_update_environment_from_pam (worker);

	g_debug ("opening user session with program '%s'",
		 worker->priv->arguments[0]);

	session_pid = fork ();

	if (session_pid < 0) {
		g_set_error (error,
			     GDM_SESSION_WORKER_ERROR,
			     GDM_SESSION_WORKER_ERROR_OPENING_SESSION,
			     "%s", g_strerror (errno));
		error_code = PAM_ABORT;
		goto out;
	}

	if (session_pid == 0) {
		char **environment;
		char  *home_dir;

		if (setuid (getuid ()) < 0) {
			g_debug ("could not reset uid - %s", g_strerror (errno));
			_exit (1);
		}

		if (setsid () < 0) {
			g_debug ("could not set pid '%u' as leader of new session and process group - %s",
				 (guint) getpid (), g_strerror (errno));
			_exit (2);
		}

		environment = gdm_session_worker_get_environment (worker);

		g_assert (geteuid () == getuid ());

		home_dir = g_hash_table_lookup (worker->priv->environment,
						"HOME");

		if ((home_dir == NULL) || g_chdir (home_dir) < 0) {
			g_chdir ("/");
		}

		gdm_session_execute (worker->priv->arguments[0],
				     worker->priv->arguments,
				     environment,
				     TRUE);

		g_debug ("child '%s' could not be started - %s",
			 worker->priv->arguments[0],
			 g_strerror (errno));
		g_strfreev (environment);

		_exit (127);
	}

	worker->priv->child_pid = session_pid;

	g_debug ("session opened creating reply...");
	g_assert (sizeof (GPid) <= sizeof (int));

	send_session_started (worker, session_pid);

	gdm_session_worker_watch_child (worker);

 out:
	if (error_code != PAM_SUCCESS) {
		gdm_session_worker_uninitialize_pam (worker, error_code);
		return FALSE;
	}

	return TRUE;
}



static gboolean
gdm_session_worker_open (GdmSessionWorker    *worker,
			 const char          *service_name,
			 const char          *hostname,
			 const char          *console_name,
			 const char          *username,
			 GError             **error)
{
	GError   *verification_error;
	gboolean  res;
	gboolean  ret;

	ret = FALSE;

	verification_error = NULL;
	res = gdm_session_worker_verify_user (worker,
					      service_name,
					      hostname,
					      console_name,
					      username,
					      TRUE /* password is required */,
					      &verification_error);
	if (! res) {
		g_assert (verification_error != NULL);

		g_debug ("%s", verification_error->message);

		g_propagate_error (error, verification_error);

		goto out;
	}

	/* Did start_program get called early? if so, process it now,
	 * otherwise we'll do it asynchronously later.
	 */
	if ((worker->priv->arguments != NULL) &&
	    !gdm_session_worker_open_user_session (worker, &verification_error)) {
		g_assert (verification_error != NULL);

		g_debug ("%s", verification_error->message);

		g_propagate_error (error, verification_error);

		goto out;
	}

	ret = TRUE;

 out:
	return ret;
}

static gboolean
gdm_session_worker_start_program (GdmSessionWorker *worker,
				  const char       *command)
{
	GError  *start_error;
	GError  *error;
	gboolean res;

	if (worker->priv->arguments != NULL)
		g_strfreev (worker->priv->arguments);

	error = NULL;
	if (! g_shell_parse_argv (command, NULL, &worker->priv->arguments, &error)) {
		g_warning ("Unable to parse command: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* Did start_program get called early? if so, we will process the request
	 * later, synchronously after getting credentials
	 */
	if (!worker->priv->credentials_are_established) {
		return FALSE;
	}

	start_error = NULL;
	res = gdm_session_worker_open_user_session (worker, &start_error);
	if (! res) {
		g_assert (start_error != NULL);

		g_warning ("%s", start_error->message);

		send_startup_failed (worker, start_error->message);
		return FALSE;
	}

	return TRUE;
}

static void
gdm_session_worker_set_server_address (GdmSessionWorker *worker,
				       const char       *address)
{
	g_free (worker->priv->server_address);
	worker->priv->server_address = g_strdup (address);
}

static void
gdm_session_worker_set_property (GObject      *object,
				guint	      prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GdmSessionWorker *self;

	self = GDM_SESSION_WORKER (object);

	switch (prop_id) {
	case PROP_SERVER_ADDRESS:
		gdm_session_worker_set_server_address (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_session_worker_get_property (GObject    *object,
				guint       prop_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GdmSessionWorker *self;

	self = GDM_SESSION_WORKER (object);

	switch (prop_id) {
	case PROP_SERVER_ADDRESS:
		g_value_set_string (value, self->priv->server_address);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
on_set_environment_variable (DBusGProxy *proxy,
			     const char *key,
			     const char *value,
			     gpointer    data)
{
	GdmSessionWorker *worker = GDM_SESSION_WORKER (data);

	g_debug ("set env: %s = %s", key, value);

	gdm_session_worker_set_environment_variable (worker, key, value);
}

static void
on_start_program (DBusGProxy *proxy,
		  const char *text,
		  gpointer    data)
{
	GdmSessionWorker *worker = GDM_SESSION_WORKER (data);

	g_debug ("start program: %s", text);

	gdm_session_worker_start_program (worker, text);
}

static void
on_begin_verification (DBusGProxy *proxy,
		       const char *service,
		       const char *console,
		       const char *hostname,
		       gpointer    data)
{
	GdmSessionWorker *worker = GDM_SESSION_WORKER (data);
	GError *error;
	gboolean res;

	g_debug ("begin verification: %s %s", service, console);

	error = NULL;
	res = gdm_session_worker_open (worker, service, console, hostname, NULL, &error);
	if (! res) {
		g_debug ("Verification failed: %s", error->message);
		g_error_free (error);
	}
}

static void
on_begin_verification_for_user (DBusGProxy *proxy,
				const char *service,
				const char *console,
				const char *hostname,
				const char *username,
				gpointer    data)
{
	GdmSessionWorker *worker = GDM_SESSION_WORKER (data);
	GError *error;
	gboolean res;

	g_debug ("begin verification: %s %s", service, console);

	error = NULL;
	res = gdm_session_worker_open (worker, service, console, hostname, username, &error);
	if (! res) {
		g_debug ("Verification failed: %s", error->message);
		g_error_free (error);
	}
}

static void
proxy_destroyed (DBusGProxy       *bus_proxy,
		 GdmSessionWorker *worker)
{
	g_debug ("Disconnected");

	/* do cleanup */
	exit (1);
}

static GObject *
gdm_session_worker_constructor (GType                  type,
				guint                  n_construct_properties,
				GObjectConstructParam *construct_properties)
{
        GdmSessionWorker      *worker;
        GdmSessionWorkerClass *klass;
	GError                *error;

        klass = GDM_SESSION_WORKER_CLASS (g_type_class_peek (GDM_TYPE_SESSION_WORKER));

        worker = GDM_SESSION_WORKER (G_OBJECT_CLASS (gdm_session_worker_parent_class)->constructor (type,
												    n_construct_properties,
												    construct_properties));

	g_debug ("connecting to address: %s", worker->priv->server_address);

        error = NULL;
        worker->priv->connection = dbus_g_connection_open (worker->priv->server_address, &error);
        if (worker->priv->connection == NULL) {
                if (error != NULL) {
                        g_warning ("error opening connection: %s", error->message);
                        g_error_free (error);
                } else {
			g_warning ("Unable to open connection");
		}
		exit (1);
        }

	/*dbus_connection_set_exit_on_disconnect (dbus_g_connection_get_connection (worker->priv->connection), TRUE);*/

	g_debug ("creating proxy for peer: %s", GDM_SESSION_DBUS_PATH);
        worker->priv->server_proxy = dbus_g_proxy_new_for_peer (worker->priv->connection,
								GDM_SESSION_DBUS_PATH,
								GDM_SESSION_DBUS_INTERFACE);
	if (worker->priv->server_proxy == NULL) {
		g_warning ("Unable to create proxy for peer");
		exit (1);
	}

	g_signal_connect (worker->priv->server_proxy, "destroy", G_CALLBACK (proxy_destroyed), NULL);

	dbus_g_object_register_marshaller (gdm_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_INVALID);
	dbus_g_object_register_marshaller (gdm_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_INVALID);

	/* FIXME: not sure why introspection isn't working */
	dbus_g_proxy_add_signal (worker->priv->server_proxy, "StartProgram", G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (worker->priv->server_proxy, "SetEnvironmentVariable", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (worker->priv->server_proxy, "BeginVerification", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (worker->priv->server_proxy, "BeginVerificationForUser", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_connect_signal (worker->priv->server_proxy,
				     "StartProgram",
				     G_CALLBACK (on_start_program),
				     worker,
				     NULL);
	dbus_g_proxy_connect_signal (worker->priv->server_proxy,
				     "SetEnvironmentVariable",
				     G_CALLBACK (on_set_environment_variable),
				     worker,
				     NULL);
	dbus_g_proxy_connect_signal (worker->priv->server_proxy,
				     "BeginVerification",
				     G_CALLBACK (on_begin_verification),
				     worker,
				     NULL);
	dbus_g_proxy_connect_signal (worker->priv->server_proxy,
				     "BeginVerificationForUser",
				     G_CALLBACK (on_begin_verification_for_user),
				     worker,
				     NULL);

        return G_OBJECT (worker);
}

static void
gdm_session_worker_class_init (GdmSessionWorkerClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_session_worker_get_property;
	object_class->set_property = gdm_session_worker_set_property;
        object_class->constructor = gdm_session_worker_constructor;
	object_class->finalize = gdm_session_worker_finalize;

	g_type_class_add_private (klass, sizeof (GdmSessionWorkerPrivate));

	g_object_class_install_property (object_class,
					 PROP_SERVER_ADDRESS,
					 g_param_spec_string ("server-address",
							      "server address",
							      "server address",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gdm_session_worker_init (GdmSessionWorker *worker)
{

	worker->priv = GDM_SESSION_WORKER_GET_PRIVATE (worker);
	worker->priv->environment = g_hash_table_new_full (g_str_hash,
							   g_str_equal,
							   (GDestroyNotify) g_free,
							   (GDestroyNotify) g_free);
}

static void
gdm_session_worker_unwatch_child (GdmSessionWorker *worker)
{
	if (worker->priv->child_watch_id == 0)
		return;

	g_source_remove (worker->priv->child_watch_id);
	worker->priv->child_watch_id = 0;
}


static void
gdm_session_worker_finalize (GObject *object)
{
	GdmSessionWorker *worker;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SESSION_WORKER (object));

	worker = GDM_SESSION_WORKER (object);

	g_return_if_fail (worker->priv != NULL);

	gdm_session_worker_unwatch_child (worker);

	if (worker->priv->username != NULL) {
		g_free (worker->priv->username);
		worker->priv->username = NULL;
	}

	if (worker->priv->arguments != NULL) {
		g_strfreev (worker->priv->arguments);
		worker->priv->arguments = NULL;
	}

	if (worker->priv->environment != NULL) {
		g_hash_table_destroy (worker->priv->environment);
		worker->priv->environment = NULL;
	}

	G_OBJECT_CLASS (gdm_session_worker_parent_class)->finalize (object);
}

GdmSessionWorker *
gdm_session_worker_new (const char *address)
{
	GObject *object;

	object = g_object_new (GDM_TYPE_SESSION_WORKER,
			       "server-address", address,
			       NULL);

	return GDM_SESSION_WORKER (object);
}
