/* GDM - The Gnome Display Manager
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
#include <gnome.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gchar *GdmPidFile;
extern gboolean GdmDebug;
extern GSList *displays;

extern char **environ;

extern pid_t extra_process;


/**
 * gdm_fail:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Logs fatal error condition and aborts master daemon.  Also sleeps
 * for 30 seconds to avoid looping if gdm is started by init.  
 */

void 
gdm_fail (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* Log to both syslog and stderr */
    syslog (LOG_CRIT, "%s", s);
    fprintf (stderr, "%s\n", s);
    fflush (stderr);

    g_free (s);

    unlink (GdmPidFile);
    closelog ();

    /* Slow down respawning if we're started from init */
    if (getppid() == 1)
	sleep (30);

    exit (EXIT_FAILURE);
}


/**
 * gdm_info:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log non-fatal information to syslog
 */

void 
gdm_info (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_INFO, "%s", s);
    
    g_free (s);
}


/**
 * gdm_error:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log non-fatal error condition to syslog
 */

void 
gdm_error (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, "%s", s);
    
    g_free (s);
}


/**
 * gdm_debug:
 * @format: printf style format string
 * @...: Optional arguments
 *
 * Log debug information to syslog if debugging is enabled.
 */

void 
gdm_debug (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (/*0 && */! GdmDebug) 
	return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* UGLY DEBUGGING HACK! */
    
    /*{ FILE *fp = fopen ("/tmp/foo.gdm", "a"); fprintf (fp, "%s\n", s); fflush (fp); fclose (fp); };*/
    
    syslog (LOG_ERR, "%s", s);	/* FIXME: LOG_DEBUG */
    
    g_free (s);
}

/* clear environment, but keep the i18n ones,
 * note that this leaks memory so only use before exec */
void
gdm_clearenv_no_lang (void)
{
	int i;
	GList *li, *envs = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		char *env = environ[i];
		if (strncmp (env, "LC_", 3) == 0 ||
		    strncmp (env, "LANG", 4) == 0 ||
		    strncmp (env, "LINGUAS", 7) == 0)
			envs = g_list_prepend (envs, g_strdup (env));
	}

	ve_clearenv ();

	for (li = envs; li != NULL; li = li->next) {
		putenv (li->data);
	}

	g_list_free (envs);
}

/* Evil function to figure out which display number is free */
int
gdm_get_free_display (int start, int server_uid)
{
	int sock;
	int i;
	struct sockaddr_in serv_addr = {0}; 

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	/* Cap this at 3000, I'm not sure we can ever seriously
	 * go that far */
	for (i = start; i < 3000; i ++) {
		GSList *li;
		struct stat s;
		char buf[256];

		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *dsp = li->data;
			if (SERVER_IS_LOCAL (dsp) &&
			    dsp->dispnum == i)
				break;
		}
		if (li != NULL) {
			/* found one */
			continue;
		}

		sock = socket (AF_INET, SOCK_STREAM, 0);

		serv_addr.sin_port = htons (6000 + i);

		errno = 0;
		if (connect (sock, (struct sockaddr *)&serv_addr,
			     sizeof (serv_addr)) >= 0 ||
		    errno != ECONNREFUSED) {
			close (sock);
			continue;
		}

		/* if starting as root, we'll be able to overwrite any
		 * stale sockets, but a user may not be able to */
		if (server_uid > 0) {
			g_snprintf (buf, sizeof (buf),
				    "/tmp/.X11-unix/X%d", i);
			if (stat (buf, &s) == 0 &&
			    s.st_uid != server_uid) {
				close (sock);
				continue;
			}
		}

		close (sock);
		return i;
	}

	return -1;
}

gboolean
gdm_text_message_dialog (const char *msg)
{
	char *dialog; /* do we have dialog?*/

	if (access (EXPANDED_SBINDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	dialog = gnome_is_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("gdialog");
	if (dialog != NULL) {
		char *argv[7];

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = dialog;
		argv[2] = "--msgbox";
		argv[3] = (char *)msg;
		argv[4] = "11";
		argv[5] = "70";
		argv[6] = NULL;

		if (gdm_exec_wait (argv) < 0) {
			g_free (dialog);
			return FALSE;
		}

		g_free (dialog);
	} else {
		char *argv[5];

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = "/bin/sh";
		argv[2] = "-c";
		argv[3] = g_strdup_printf
			("clear ; "
			 "echo \"%s\" ; read ; clear",
			 msg);
		argv[4] = NULL;

		if (gdm_exec_wait (argv) < 0) {
			g_free (argv[3]);
			return FALSE;
		}
		g_free (argv[3]);
	}
	return TRUE;
}

gboolean
gdm_text_yesno_dialog (const char *msg, gboolean *ret)
{
	char *dialog; /* do we have dialog?*/

	if (access (EXPANDED_SBINDIR "/gdmopen", X_OK) != 0)
		return FALSE;

	if (ret != NULL)
		*ret = FALSE;

	dialog = gnome_is_program_in_path ("dialog");
	if (dialog == NULL)
		dialog = gnome_is_program_in_path ("gdialog");
	if (dialog != NULL) {
		char *argv[7];
		int retint;

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = dialog;
		argv[2] = "--yesno";
		argv[3] = (char *)msg;
		argv[4] = "11";
		argv[5] = "70";
		argv[6] = NULL;

		retint = gdm_exec_wait (argv);
		if (retint < 0) {
			g_free (dialog);
			return FALSE;
		}

		if (ret != NULL)
			*ret = (retint == 0) ? TRUE : FALSE;

		g_free (dialog);

		return TRUE;
	} else {
		char tempname[] = "/tmp/gdm-yesno-XXXXXX";
		int tempfd;
		FILE *fp;
		char buf[256];
		char *argv[5];

		tempfd = mkstemp (tempname);
		if (tempfd < 0)
			return FALSE;

		close (tempfd);

		argv[0] = EXPANDED_SBINDIR "/gdmopen";
		argv[1] = "/bin/sh";
		argv[2] = "-c";
		argv[3] = g_strdup_printf
			("clear ; "
			 "echo \"%s\" ; echo ; echo \"%s\" ; "
			 "read RETURN ; echo $RETURN > %s ; clear'",
			 msg,
			 /* Translators, don't translate the 'y' and 'n' */
			 _("y = Yes or n = No? >"),
			 tempname);
		argv[4] = NULL;

		if (gdm_exec_wait (argv) < 0) {
			g_free (argv[3]);
			return FALSE;
		}
		g_free (argv[3]);

		if (ret != NULL) {
			fp = fopen (tempname, "r");
			if (fp != NULL) {
				if (fgets (buf, sizeof (buf), fp) != NULL &&
				    (buf[0] == 'y' || buf[0] == 'Y'))
					*ret = TRUE;
				fclose (fp);
			} else {
				return FALSE;
			}
		}

		unlink (tempname);

		return TRUE;
	}
}

int
gdm_exec_wait (char * const *argv)
{
	int status;
	pid_t pid;

	if (argv == NULL ||
	    argv[0] == NULL ||
	    access (argv[0], X_OK) != 0)
		return -1;

	/* Note a fun and unavoidable (also almost
	 * impossible to happen race.  If the parent gets
	 * whacked before it executes any code, it will
	 * not whack the child.  Oh well. */
	extra_process = pid = fork ();
	if (pid == 0) {
		int i;

		for (i = 0; i < sysconf (_SC_OPEN_MAX); i++)
			close (i);

		/* No error checking here - if it's messed the best response
		 * is to ignore & try to continue */
		open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		open ("/dev/null", O_RDWR); /* open stderr - fd 2 */
		
		execv (argv[0], argv);

		_exit (-1);
	}

	if (pid < 0)
		return -1;

	waitpid (pid, &status, 0);

	extra_process = -1;

	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return -1;
}

/* EOF */
