/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";


gchar **gdm_arg_munch (gchar *p);
gint gdm_exec_script (GdmDisplay *d, gchar *dir);
void gdm_fail (const gchar *format, ...);
void gdm_abort (const gchar *format, ...);
void gdm_info (const gchar *format, ...);
void gdm_error (const gchar *format, ...);
void gdm_debug (const gchar *format, ...);
void gdm_remanage (const gchar *format, ...);


extern gchar *GdmPidFile;
extern gchar *GdmRootPath;
extern gint GdmDebug;


/* Written by Alan Cox <alan@redhat.com> */
gchar **
gdm_arg_munch (gchar *p)
{
    gchar *x = strdup (p);
    gint quoted = 0;
    gint argn = 0;
    static gchar *argv[16];
	
    while (*x) {
	while (*x && isspace (*x))
	    x++;
	
	if (*x=='"') {
	    quoted = 1;
	    x++;
	}
	
	argv[argn] = x;
	
	while (*x) {
	    
	    if (*x=='"' && quoted)
		break;
	    
	    if (*x==' ' && !quoted)
		break;
	    
	    x++;
	}
	
	if (*x)
	    *x++ = 0;
	
	argn++;
	
	if (argn==16)
	    break;
    }
    
    while (argn<=16) {
	argv[argn] = NULL;
	argn++;
    }
    
    return (argv);
}


/* Execute a script and wait for it to finish. Returns exit status of
 * the executed script.
 */
gint
gdm_exec_script (GdmDisplay *d, gchar *dir)
{
    pid_t pid;
    gchar *script, *defscript, *scr;
    gchar **argv;
    gint status;

    if (!d || !dir)
	return (EXIT_SUCCESS);

    script = g_strconcat (dir, "/", d->name, NULL);
    defscript = g_strconcat (dir, "/Default", NULL);

    if (! access (script, R_OK|X_OK))
	scr = script;
    else if (! access (defscript, R_OK|X_OK)) 
	scr = defscript;
    else
	return (EXIT_SUCCESS);

    switch (pid = fork()) {
	    
    case 0:
	setenv ("PATH", GdmRootPath, TRUE);
	argv = gdm_arg_munch (scr);
	execv (argv[0], argv);
	syslog (LOG_ERR, _("gdm_exec_script: Failed starting: %s"), scr);
	return (EXIT_SUCCESS);
	    
    case -1:
	syslog (LOG_ERR, _("gdm_exec_script: Can't fork script process!"));
	return (EXIT_SUCCESS);
	
    default:
	waitpid (pid, &status, 0);	/* Wait for script to finish */

	if (WIFEXITED (status))
	    return (WEXITSTATUS (status));
	else
	    return (EXIT_SUCCESS);
    }
}


/* Log error and abort master daemon */
void 
gdm_fail (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    syslog (LOG_ERR, s);
    fprintf (stderr, s);
    fprintf (stderr, "\n");
    fflush (stderr);

    g_free (s);
    unlink (GdmPidFile);
    closelog();

    exit (EXIT_FAILURE);
}


/* Log error and abort the slave daemon */
void 
gdm_abort (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, s);
    
    g_free (s);

    exit (DISPLAY_ABORT);
}


/* Remanage display */
void 
gdm_remanage (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, s);
    
    g_free (s);

    exit (DISPLAY_REMANAGE);
}


/* Log non fatal error/message */
void 
gdm_info (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_INFO, s);
    
    g_free (s);
}


/* Log error condition */
void 
gdm_error (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);
    
    syslog (LOG_ERR, s);
    
    g_free (s);
}


/* Log debug messages */
void 
gdm_debug (const gchar *format, ...)
{
    va_list args;
    gchar *s;

    if (GdmDebug) {
	va_start (args, format);
	s = g_strdup_vprintf (format, args);
	va_end (args);

	/* FIXME */
	syslog (LOG_ERR, s);
    
	g_free (s);
    }
}


/* EOF */
