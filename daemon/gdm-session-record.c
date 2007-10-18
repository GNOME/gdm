/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(HAVE_UTMPX_H)
#include <utmpx.h>
#endif

#if defined(HAVE_UTMP_H)
#include <utmp.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "gdm-session-record.h"

#ifndef GDM_BAD_SESSION_RECORDS_FILE
#define GDM_BAD_SESSION_RECORDS_FILE "/var/log/btmp"
#endif

#ifndef GDM_NEW_SESSION_RECORDS_FILE
#define GDM_NEW_SESSION_RECORDS_FILE "/var/log/wtmp"
#endif

void
gdm_session_record_write (GdmSessionRecordType  record_type,
                          GPid                  session_pid,
                          const char           *user_name,
                          const char           *host_name,
                          const char           *x11_display_name,
                          const char           *display_device)
{
        UTMP        session_record = { 0 };
        UTMP       *u;
        GTimeVal    now = { 0 };
        char       *hostname;
        const char *username;

        u = NULL;

        g_debug ("Writing %s utmp/wtmp record",
                 record_type == GDM_SESSION_RECORD_TYPE_LOGIN  ? "session" :
                 record_type == GDM_SESSION_RECORD_TYPE_LOGOUT ? "logout"  :
                 "failed session attempt");

        if (record_type != GDM_SESSION_RECORD_TYPE_LOGOUT) {
                /*
                 * It is possible that PAM failed before it mapped the user
                 * input into a valid username, so we fallback to try using
                 * "(unknown)"
                 */
                if (user_name != NULL) {
                         username = user_name;
                } else {
                         username = "(unknown)";
                }

#if defined(HAVE_UT_UT_USER)
                strncpy (session_record.ut_user,
                         username,
                         sizeof (session_record.ut_user));
                g_debug ("using ut_user %.*s",
                         sizeof (session_record.ut_user),
                         session_record.ut_user);
#elif defined(HAVE_UT_UT_NAME)
                strncpy (session_record.ut_name,
                         username
                         sizeof (session_record.ut_name));
                g_debug ("using ut_name %.*s",
                         sizeof (session_record.ut_name),
                         session_record.ut_name);
#endif
        }

#if defined(HAVE_UT_UT_TYPE)
        /*
         * Set type to DEAD_PROCESS when logging out, otherwise
         * set to USER_PROCESS.
         */
        if (record_type == GDM_SESSION_RECORD_TYPE_LOGOUT) {
                session_record.ut_type = DEAD_PROCESS;
                g_debug ("using ut_type DEAD_PROCESS");
        } else {
                session_record.ut_type = USER_PROCESS;
                g_debug ("using ut_type USER_PROCESS");
        }
#endif

#if defined(HAVE_UT_UT_PID)
        /* Set pid */
        if (session_pid != 0) {
                session_record.ut_pid = session_pid;
        }
        g_debug ("using ut_pid %d", (int) session_record.ut_pid);
#endif

#if defined(HAVE_UT_UT_TV)
        /* Set time in TV format */
        g_get_current_time (&now);
        session_record.ut_tv.tv_sec  = now.tv_sec;
        session_record.ut_tv.tv_usec = now.tv_usec;
        g_debug ("using ut_tv time %ld",
                 (glong) session_record.ut_tv.tv_sec);
#elif defined(HAVE_UT_UT_TIME)
        /* Set time in time format */
        time (&session_record.ut_time);
        g_debug ("using ut_time %ld",
                 (glong) session_record.ut_time);
#endif

#if defined(HAVE_UT_UT_ID)
        /* Set ut_id to the $DISPLAY value */
        strncpy (session_record.ut_id,
                 x11_display_name,
                 sizeof (session_record.ut_id));
        g_debug ("using ut_id %.*s",
                 sizeof (session_record.ut_id),
                 session_record.ut_id);
#endif

#if defined(HAVE_UT_UT_HOST)
        hostname = NULL;

        /*
         * Set ut_host to hostname:$DISPLAY if remote, otherwise set
         * to $DISPLAY
         */
        if ((host_name != NULL) && g_str_has_prefix (x11_display_name, ":")) {
                hostname = g_strdup_printf ("%s%s",
                                            host_name,
                                            x11_display_name);
        } else {
                hostname = g_strdup (x11_display_name);
        }

        if (hostname != NULL) {
                strncpy (session_record.ut_host,
                         hostname, sizeof (session_record.ut_host));
                g_debug ("using ut_host %.*s",
                         sizeof (session_record.ut_host),
                         session_record.ut_host);
                g_free (hostname);

#ifdef HAVE_UT_UT_SYSLEN
                session_record.ut_syslen = MIN (strlen (hostname),
                                                sizeof (session_record.ut_host));
#endif
        }
#endif

        /*
         * Set ut_line to the device name associated with this display
         * but remove the "/dev/" prefix.  If no device, then use the
         * $DISPLAY value.
         */
        if (g_str_has_prefix (display_device, "/dev/")) {
                strncpy (session_record.ut_line,
                         display_device + strlen ("/dev/"),
                         sizeof (session_record.ut_line));
        } else if (g_str_has_prefix (x11_display_name, ":")) {
                strncpy (session_record.ut_line,
                         x11_display_name,
                         sizeof (session_record.ut_line));
        }
        g_debug ("using ut_line %.*s",
                 sizeof (session_record.ut_line),
                 session_record.ut_line);

        switch (record_type) {
        case GDM_SESSION_RECORD_TYPE_LOGIN:

                /* Handle wtmp */
                g_debug ("Writing wtmp session record to " GDM_NEW_SESSION_RECORDS_FILE);
#if defined(HAVE_UPDWTMPX)
                updwtmpx (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
#elif defined(HAVE_UPDWTMP)
                updwtmp (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
#elif defined(HAVE_LOGWTMP) && defined(HAVE_UT_UT_HOST) && !defined(HAVE_LOGIN)
#if defined(HAVE_UT_UT_USER)
                logwtmp (record.ut_line, record.ut_user, record.ut_host);
#elif defined(HAVE_UT_UT_NAME)
                logwtmp (record.ut_line, record.ut_name, record.ut_host);
#endif
#endif

#if defined(HAVE_GETUTXENT)
                /*
                 * Handle utmp
                 * Update if entry already exists
                 */
                while ((u = getutxent ()) != NULL) {
                        if (u->ut_type == USER_PROCESS &&
                           (session_record.ut_line != NULL &&
                           (strncmp (u->ut_line, session_record.ut_line,
                                     sizeof (u->ut_line)) == 0 ||
                            u->ut_pid == session_record.ut_pid))) {
                                g_debug ("Updating existing utmp record");
                                pututxline (&session_record);
                                break;
                        }
                }
                endutxent ();

                /* Add new entry if update did not work */
                if (u == NULL) {
                        g_debug ("Adding new utmp record");
                        pututxline (&session_record);
                }
#elif defined(HAVE_LOGIN)
                login (&session_record);
#endif

                break;

        case GDM_SESSION_RECORD_TYPE_LOGOUT:

                /* Handle wtmp */
                g_debug ("Writing wtmp logout record to " GDM_NEW_SESSION_RECORDS_FILE);
#if defined(HAVE_UPDWTMPX)
                updwtmpx (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
#elif defined (HAVE_UPDWTMP)
                updwtmp (GDM_NEW_SESSION_RECORDS_FILE, &session_record);
#elif defined(HAVE_LOGWTMP)
                logwtmp (record.ut_line, "", "");
#endif

                /* Hande utmp */
#if defined(HAVE_GETUTXENT)
                setutxent ();

                while ((u = getutxent ()) != NULL &&
                       (u = getutxid (&session_record)) != NULL) {

                        g_debug ("Removing utmp record");
                        if (u->ut_pid == session_pid &&
                            u->ut_type == DEAD_PROCESS) {
                                /* Already done */
                                break;
                        }

                        u->ut_type = DEAD_PROCESS;
#if defined(HAVE_UT_UT_TV)
                        u->ut_tv.tv_sec = session_record.ut_tv.tv_sec;
#elif defined(HAVE_UT_UT_TIME)
                        u->ut_time = session_record.ut_time;
#endif
                        u->ut_exit.e_termination = 0;
                        u->ut_exit.e_exit = 0;

                        pututxline (u);

                        break;
                }

                endutxent ();
#elif defined(HAVE_LOGOUT)
                logout (session_record.ut_line);
#endif

                break;

        case GDM_SESSION_RECORD_TYPE_FAILED_ATTEMPT:
                /* Handle btmp */
                g_debug ("Writing btmp failed session attempt record to "
                         GDM_BAD_SESSION_RECORDS_FILE);
#if defined(HAVE_UPDWTMPX)
                updwtmpx (GDM_BAD_SESSION_RECORDS_FILE, &session_record);
#elif defined(HAVE_UPDWTMP)
                updwtmp (GDM_BAD_SESSION_RECORDS_FILE, &session_record);
#endif
                break;
        }
}
