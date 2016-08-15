/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright  (C) 2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
 */
#include <security/_pam_macros.h>
#include <security/pam_ext.h>
#include <security/pam_misc.h>
#include <security/pam_modules.h>
#include <security/pam_modutil.h>

int
pam_sm_authenticate (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_setcred (pam_handle_t *pamh,
                int           flags,
                int           argc,
                const char  **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_acct_mgmt (pam_handle_t  *pamh,
                  int            flags,
                  int            argc,
                  const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_chauthtok (pam_handle_t  *pamh,
                  int            flags,
                  int            argc,
                  const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_open_session (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_close_session (pam_handle_t  *pamh,
                      int            flags,
                      int            argc,
                      const char   **argv)
{
        return PAM_SUCCESS;
}
