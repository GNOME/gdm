/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <glib.h>

#include "gdm-session.h"

static GMainLoop *loop;

static void
on_open (GdmSession *session,
	 const char *username)
{
	GError *error;
	gboolean res;

	g_debug ("Got opened: begin auth for %s", username);

	error = NULL;
	res = gdm_session_begin_verification (session,
					      username,
					      &error);
	if (! res) {
		g_warning ("Unable to begin verification: %s", error->message);
		g_error_free (error);
	}
}

static void
on_session_started (GdmSession *session,
                    GPid        pid)
{
	g_print ("session started on pid %d\n", (int) pid);
}

static void
on_session_exited (GdmSession *session,
                   int         exit_code)
{
	g_print ("session exited with code %d\n", exit_code);
	exit (0);
}

static void
on_session_died (GdmSession *session,
                 int         signal_number)
{
	g_print ("session died with signal %d, (%s)",
		 signal_number,
		 g_strsignal (signal_number));
	exit (1);
}

static void
on_user_verified (GdmSession *session)
{
	char *username;
	const char *command = "/usr/bin/gedit /tmp/foo.log";

	username = gdm_session_get_username (session);

	g_print ("%s%ssuccessfully authenticated\n",
		 username ? username : "", username ? " " : "");
	g_free (username);

	gdm_session_start_program (session, command);
}

static void
on_user_verification_error (GdmSession *session,
                            GError     *error)
{
	char *username;

	username = gdm_session_get_username (session);

	g_print ("%s%scould not be successfully authenticated: %s\n",
		 username? username : "", username? " " : "",
		 error->message);

	g_free (username);
	exit (1);
}

static void
on_info_query (GdmSession *session,
               const char *query_text)
{
	char answer[1024];

	g_print ("%s ", query_text);

	fgets (answer, sizeof (answer), stdin);
	answer[strlen(answer) - 1] = '\0';

	if (answer[0] == '\0') {
		gdm_session_close (session);
		g_main_loop_quit (loop);
	} else {
		gdm_session_answer_query (session, answer);
	}
}

static void
on_info (GdmSession *session,
         const char *info)
{
	g_print ("\n** NOTE: %s\n", info);
}

static void
on_problem (GdmSession *session,
            const char *problem)
{
	g_print ("\n** WARNING: %s\n", problem);
}

static void
on_secret_info_query (GdmSession *session,
                      const char *query_text)
{
	char answer[1024];
	struct termio io_info;

	g_print ("%s", query_text);

	ioctl (0, TCGETA, &io_info);
	io_info.c_lflag &= ~ECHO;
	ioctl (0, TCSETA, &io_info);

	fgets (answer, sizeof (answer), stdin);
	answer[strlen(answer) - 1] = '\0';

	ioctl (0, TCGETA, &io_info);
	io_info.c_lflag |= ECHO;
	ioctl (0, TCSETA, &io_info);

	g_print ("\n");

	gdm_session_answer_query (session, answer);
}

static void
import_environment (GdmSession *session)
{
	if (g_getenv ("PATH") != NULL)
		gdm_session_set_environment_variable (session, "PATH",
						      g_getenv ("PATH"));

	if (g_getenv ("DISPLAY") != NULL)
		gdm_session_set_environment_variable (session, "DISPLAY",
						      g_getenv ("DISPLAY"));

	if (g_getenv ("XAUTHORITY") != NULL)
		gdm_session_set_environment_variable (session, "XAUTHORITY",
						      g_getenv ("XAUTHORITY"));
}

int
main (int   argc,
      char *argv[])
{
	GdmSession *session;
	char       *username;
	int         exit_code;

	exit_code = 0;

	g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

	g_type_init ();

	do {
		g_message ("creating instance of 'user session' object...");
		session = gdm_session_new ();
		g_message ("'user session' object created successfully");

		if (argc <= 1) {
			username = NULL;
		} else {
			username = argv[1];
		}

		gdm_session_open (session,
				  "gdm",
				  "",
				  ttyname (STDIN_FILENO),
				  NULL);

		g_signal_connect (session, "opened",
				  G_CALLBACK (on_open),
				  username);

		g_signal_connect (session, "info",
				  G_CALLBACK (on_info),
				  NULL);

		g_signal_connect (session, "problem",
				  G_CALLBACK (on_problem),
				  NULL);

		g_signal_connect (session, "info-query",
				  G_CALLBACK (on_info_query),
				  NULL);

		g_signal_connect (session, "secret-info-query",
				  G_CALLBACK (on_secret_info_query),
				  NULL);

		g_signal_connect (session, "user-verified",
				  G_CALLBACK (on_user_verified),
				  NULL);

		g_signal_connect (session, "user-verification-error",
				  G_CALLBACK (on_user_verification_error),
				  NULL);

		g_signal_connect (session, "session-started",
				  G_CALLBACK (on_session_started),
				  NULL);

		g_signal_connect (session, "session-exited",
				  G_CALLBACK (on_session_exited),
				  NULL);

		g_signal_connect (session, "session-died",
				  G_CALLBACK (on_session_died),
				  NULL);

		import_environment (session);

		loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (loop);
		g_main_loop_unref (loop);

		g_message ("destroying previously created 'user session' object...");
		g_object_unref (session);
		g_message ("'user session' object destroyed successfully");
	} while (1);

	return exit_code;
}
