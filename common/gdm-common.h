/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GDM_COMMON_H
#define _GDM_COMMON_H

#include <glib-unix.h>
#include <pwd.h>
#include <errno.h>

#define        VE_IGNORE_EINTR(expr) \
        do {                         \
                errno = 0;           \
                expr;                \
        } while G_UNLIKELY (errno == EINTR);

#define GDM_CUSTOM_SESSION  "custom"

/* check if logind is running */
#define LOGIND_RUNNING() (access("/run/systemd/seats/", F_OK) >= 0)

GQuark gdm_common_error_quark (void);
#define GDM_COMMON_ERROR gdm_common_error_quark()

G_BEGIN_DECLS

int            gdm_wait_on_pid           (int pid);
int            gdm_wait_on_and_disown_pid (int pid,
                                           int timeout);
int            gdm_signal_pid            (int pid,
                                          int signal);
gboolean       gdm_get_pwent_for_name    (const char     *name,
                                          struct passwd **pwentp);

gboolean       gdm_clear_close_on_exec_flag (int fd);

const char *   gdm_make_temp_dir         (char    *template);

char          *gdm_generate_random_bytes (gsize          size,
                                          GError       **error);
gboolean       gdm_goto_login_session    (GError **error);

GPtrArray     *gdm_get_script_environment (const char *username,
                                           const char *display_name,
                                           const char *display_hostname,
                                           const char *display_x11_authority_file);
gboolean       gdm_run_script             (const char *dir,
                                           const char *username,
                                           const char *display_name,
                                           const char *display_hostname,
                                           const char *display_x11_authority_file);

G_END_DECLS

#endif /* _GDM_COMMON_H */
