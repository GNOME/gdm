/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <syslog.h>
#include <systemd/sd-daemon.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "gdm-log.h"

static gboolean initialized = FALSE;
static gboolean is_sd_booted = FALSE;
static gboolean debug_enabled = FALSE;

static gint
get_syslog_priority_from_log_level (GLogLevelFlags log_level)
{
        switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_FLAG_FATAL:
                return LOG_EMERG;
        case G_LOG_LEVEL_ERROR:
                /* fatal error - a critical error, in the syslog world */
                return LOG_CRIT;
        case G_LOG_LEVEL_CRITICAL:
                /* critical warning - an error, in the syslog world */
                return LOG_ERR;
        case G_LOG_LEVEL_WARNING:
        case G_LOG_LEVEL_MESSAGE:
                return LOG_NOTICE;
        case G_LOG_LEVEL_INFO:
                return LOG_INFO;
        case G_LOG_LEVEL_DEBUG:
        default:
                return LOG_DEBUG;
        }
}

static void
gdm_log_default_handler (const gchar    *log_domain,
                         GLogLevelFlags  log_level,
                         const gchar    *message,
                         gpointer        unused_data)
{
        int priority;

        gdm_log_init ();

        if ((log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_DEBUG &&
            !debug_enabled) {
                return;
        }

        /* Process the message prefix and priority */
        priority = get_syslog_priority_from_log_level (log_level);

        if (is_sd_booted) {
                fprintf (stderr,
                         "<%d>%s%s%s\n",
                         priority,
                         log_domain != NULL? log_domain : "",
                         log_domain != NULL? ": " : "",
                         message);
                fflush (stderr);
        } else {
                syslog (priority,
                        "%s%s%s\n",
                        log_domain != NULL? log_domain : "",
                        log_domain != NULL? ": " : "",
                        message);
        }
}

void
gdm_log_toggle_debug (void)
{
        gdm_log_set_debug (!debug_enabled);
}

void
gdm_log_set_debug (gboolean debug)
{
        g_assert (initialized);
        if (debug_enabled == debug) {
                return;
        }

        if (debug) {
                debug_enabled = debug;
                g_debug ("Enabling debugging");
        } else {
                g_debug ("Disabling debugging");
                debug_enabled = debug;
        }
}

void
gdm_log_init (void)
{
        if (initialized)
                return;

        initialized = TRUE;

        g_log_set_default_handler (gdm_log_default_handler, NULL);
}

void
gdm_log_shutdown (void)
{
        if (!initialized)
                return;
        if (!is_sd_booted)
                closelog ();
        initialized = FALSE;
}

