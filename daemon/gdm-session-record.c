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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
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

#if !defined(GDM_NEW_SESSION_RECORDS_FILE)
#    if defined(WTMPX_FILE)
#        define GDM_NEW_SESSION_RECORDS_FILE WTMPX_FILE
#    elif defined(_PATH_WTMPX)
#        define GDM_NEW_SESSION_RECORDS_FILE _PATH_WTMPX
#    elif defined(WTMPX_FILENAME)
#        define GDM_NEW_SESSION_RECORDS_FILE WTMPX_FILENAME
#    elif defined(WTMP_FILE)
#        define GDM_NEW_SESSION_RECORDS_FILE WTMP_FILE
#    elif defined(_PATH_WTMP) /* BSD systems */
#        define GDM_NEW_SESSION_RECORDS_FILE _PATH_WTMP
#    else
#        define GDM_NEW_SESSION_RECORDS_FILE "/var/log/wtmp"
#    endif
#endif

static void
record_set_type (UTMP                  *u,
                 GdmSessionRecordEvent  event)
{
#ifdef HAVE_UT_UT_TYPE
        switch (event) {
        case GDM_SESSION_RECORD_LOGIN:
        case GDM_SESSION_RECORD_FAILED:
                u->ut_type = USER_PROCESS;
                g_debug ("using ut_type USER_PROCESS");
                break;
        case GDM_SESSION_RECORD_LOGOUT:
                u->ut_type = DEAD_PROCESS;
                g_debug ("using ut_type DEAD_PROCESS");
                break;
        }
#endif
}

static void
record_set_username (UTMP       *u,
                     const char *username)
{
#if defined(HAVE_UT_UT_USER)
        memccpy (u->ut_user, username, '\0', sizeof (u->ut_user));
        g_debug ("using ut_user %.*s",
                 (int) sizeof (u->ut_user),
                 u->ut_user);
#elif defined(HAVE_UT_UT_NAME)
        memccpy (u->ut_name, username, '\0', sizeof (u->ut_name));
        g_debug ("using ut_name %.*s",
                 (int) sizeof (u->ut_name),
                 u->ut_name);
#endif
}

static void
record_set_timestamp (UTMP *u)
{
#if defined(HAVE_UT_UT_TV)
        gint64 now;

        /* Set time in TV format */
        now = g_get_real_time();
        u->ut_tv.tv_sec  = now / G_USEC_PER_SEC;
        u->ut_tv.tv_usec = now % G_USEC_PER_SEC;
        g_debug ("using ut_tv time %ld",
                 (glong) u->ut_tv.tv_sec);
#elif defined(HAVE_UT_UT_TIME)
        /* Set time in time format */
        time (&u->ut_time);
        g_debug ("using ut_time %ld",
                 (glong) u->ut_time);
#endif
}

static void
record_set_pid (UTMP *u,
                GPid  pid)
{
#if defined(HAVE_UT_UT_PID)
        /* Set pid */
        if (pid != 0) {
                u->ut_pid = pid;
        }
        g_debug ("using ut_pid %d", (int) u->ut_pid);
#endif
}

static void
record_set_host (UTMP       *u,
                 const char *remote_host)
{
        const char *hostname;
#if defined(HAVE_UT_UT_HOST)
        if (remote_host != NULL)
                hostname = remote_host;
        else
                hostname = "local";
        memccpy (u->ut_host, hostname, '\0', sizeof (u->ut_host));
        g_debug ("using ut_host %.*s", (int) sizeof (u->ut_host), u->ut_host);
#ifdef HAVE_UT_UT_SYSLEN
        u->ut_syslen = MIN (strlen (hostname), sizeof (u->ut_host));
#endif
#endif
}

static void
record_set_line (UTMP       *u,
                 const char *tty,
                 const char *seat_id)
{
        const char *line;

        if (tty != NULL) {
                if (g_str_has_prefix (tty, "/dev/"))
                        line = tty + strlen("/dev/");
                else
                        line = tty;
        } else if (seat_id != NULL)
                line = seat_id;
        else
                line = "headless";

        memccpy (u->ut_line, line, '\0', sizeof (u->ut_line));
        g_debug ("using ut_line %.*s", (int) sizeof (u->ut_line), u->ut_line);
}

static void
write_login_record (UTMP *record)
{
        /* Handle wtmp */
        g_debug ("Writing wtmp session record to " GDM_NEW_SESSION_RECORDS_FILE);
#if defined(HAVE_UPDWTMPX)
        updwtmpx (GDM_NEW_SESSION_RECORDS_FILE, record);
#elif defined(HAVE_UPDWTMP)
        updwtmp (GDM_NEW_SESSION_RECORDS_FILE, record);
#elif defined(HAVE_LOGWTMP) && defined(HAVE_UT_UT_HOST)
#if defined(HAVE_UT_UT_USER)
        logwtmp (record->ut_line, record->ut_user, record->ut_host);
#elif defined(HAVE_UT_UT_NAME)
        logwtmp (record->ut_line, record->ut_name, record->ut_host);
#endif
#endif

        /* Handle utmp */
#if defined(HAVE_GETUTXENT)
        g_debug ("Adding or updating utmp record for login");
        setutxent();
        pututxline (record);
        endutxent();
#elif defined(HAVE_LOGIN)
	login (record);
#endif
}

static void
write_logout_record (UTMP *record)
{
        /* Handle wtmp */
        g_debug ("Writing wtmp logout record to " GDM_NEW_SESSION_RECORDS_FILE);
#if defined(HAVE_UPDWTMPX)
        updwtmpx (GDM_NEW_SESSION_RECORDS_FILE, record);
#elif defined (HAVE_UPDWTMP)
        updwtmp (GDM_NEW_SESSION_RECORDS_FILE, record);
#elif defined(HAVE_LOGWTMP)
        logwtmp (record->ut_line, "", "");
#endif

        /* Handle utmp */
#if defined(HAVE_GETUTXENT)
        g_debug ("Adding or updating utmp record for logout");
        setutxent();
        pututxline (record);
        endutxent();
#elif defined(HAVE_LOGOUT)
        logout (record->ut_line);
#endif
}

static void
write_failed_record (UTMP *record)
{
#if defined(HAVE_UPDWTMPX) || defined(HAVE_UPDWTMP)
        /* Handle btmp */
        g_debug ("Writing btmp failed session attempt record to "
                 GDM_BAD_SESSION_RECORDS_FILE);
#endif

#if defined(HAVE_UPDWTMPX)
        updwtmpx (GDM_BAD_SESSION_RECORDS_FILE, record);
#elif defined(HAVE_UPDWTMP)
        updwtmp(GDM_BAD_SESSION_RECORDS_FILE, record);
#endif
}

void
gdm_session_record (GdmSessionRecordEvent  event,
                    GdmSession            *session,
                    GPid                   pid)
{
        UTMP record = { 0 };
        const char *username = NULL;
        g_autofree char *hostname = NULL;
        g_autofree char *tty = NULL;
        g_autofree char *seat_id = NULL;
        void (*write_record)(UTMP *);

        username = gdm_session_get_username (session);
        if (username == NULL)
                return;

        if (pid < 0)
                pid = gdm_session_get_pid (session);
        if (pid < 0)
                return;

        switch (event) {
        case GDM_SESSION_RECORD_LOGIN:
                g_debug ("Writing login record");
                write_record = write_login_record;
                break;
        case GDM_SESSION_RECORD_LOGOUT:
                g_debug ("Writing logout record");
                write_record = write_logout_record;
                break;
        case GDM_SESSION_RECORD_FAILED:
                g_debug ("Writing failed session attempt record");
                write_record = write_failed_record;
                break;
        }

        g_object_get (G_OBJECT (session),
                      "display-hostname", &hostname,
                      "display-device", &tty,
                      "display-seat-id", &seat_id,
                      NULL);

        record_set_type (&record, event);
        record_set_username (&record, username);
        record_set_timestamp (&record);
        record_set_pid (&record, pid);
        record_set_host (&record, hostname);
        record_set_line (&record, tty, seat_id);

        write_record (&record);
}
