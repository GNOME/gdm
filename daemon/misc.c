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

#include <config.h>
#include <gnome.h>
#include <syslog.h>

#include "gdm.h"
#include "misc.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gchar *GdmPidFile;
extern gint GdmDebug;


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
    syslog (LOG_ERR, s);
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
    
    syslog (LOG_INFO, s);
    
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
    
    syslog (LOG_ERR, s);
    
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

    if (/*0 &&*/ ! GdmDebug) 
	return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    /* UGLY DEBUGGING HACK! */
    /*
    { FILE *fp = fopen ("/tmp/foo.gdm", "a"); fprintf (fp, "%s\n", s); fflush (fp); fclose (fp); };
    */
    
    syslog (LOG_ERR, s);	/* FIXME: LOG_DEBUG */
    
    g_free (s);
}


/**
 * gdm_setenv:
 * @var: Variable to set/unset
 * @value: Value to assign to the variable, NULL for unsetting
 *
 * Wrapper around putenv() because POSIX/SUS is incredibly stupid.
 * 
 *   char *foo; char *bar;
 *   putenv (foo); putenv (bar); free (foo); 
 *
 * is legal, while
 *
 *   char *foo;
 *   putenv (foo); free (foo); 
 *
 * isn't.
 *
 * Afterall, providing the programmer with a nice, consistent
 * interface is what the standard C Library is all about. - Duh!
 *
 * Note from George:
 * You cannot free the last env as it could have been something else
 * and could still be in the env!  We just have to leak, there is no
 * recourse.
 * -George
 */

#ifndef HAVE_SETENV
gint 
gdm_setenv (const gchar *var, const gchar *value)
{
#if 0
    static gchar *lastenv = NULL; /* Holds last successful assignment pointer */
#endif
    gchar *envstr;		  /* Temporary environment string */
    gint result;		  /* Return value from the putenv() call */

    /* `var' is a prerequisite */
    if (!var)
	return -1;

    /* `value' is a prerequisite */
    if (!value)
	return -1;

    envstr = g_strconcat (var, "=", value, NULL);

    /* If string space allocation failed then abort */
    if (!envstr)
	return -1;

    /* Stuff the resulting string into the environment */
    result = putenv (envstr);

#if 0
    /* If putenv() succeeded and lastenv is set, free the old pointer */
    if (result == 0 && lastenv)
	g_free (lastenv);

    /* Save the current string pointer for the next gdm_setenv call */
    lastenv = envstr;
#endif
    
    return result;
}
#endif

#ifndef HAVE_UNSETENV
gint 
gdm_unsetenv (const gchar *var)
{
#if 0
    static gchar *lastenv = NULL; /* Holds last successful assignment pointer */
#endif
    gchar *envstr;		  /* Temporary environment string */
    gint result;		  /* Return value from the putenv() call */

    /* `var' is a prerequisite */
    if (!var)
	return -1;

    envstr = g_strdup (var);

    /* If string space allocation failed then abort */
    if (!envstr)
	return -1;

    /* Stuff the resulting string into the environment */
    result = putenv (envstr);

#if 0
    /* If putenv() succeeded and lastenv is set, free the old pointer */
    if (result == 0 && lastenv)
	g_free (lastenv);

    /* Save the current string pointer for the next gdm_setenv call */
    lastenv = envstr;
#endif
    
    return result;
}
#endif

/* EOF */
