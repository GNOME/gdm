/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * (c) 2000 Eazel, Inc.
 * (c) 2001,2002 George Lebl
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GDM_COMMON_UNKNOWN_H
#define _GDM_COMMON_UNKNOWN_H

#include <glib.h>
#include <glib/gstdio.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

G_BEGIN_DECLS

#define        VE_IGNORE_EINTR(expr) \
        do {                         \
                errno = 0;           \
                expr;                \
        } while G_UNLIKELY (errno == EINTR);

#define NEVER_FAILS_seteuid(uid) \
        { int r = 0; \
          if (geteuid () != uid) \
            r = seteuid (uid); \
          if G_UNLIKELY (r != 0) \
        g_error ("GDM file %s: line %d (%s): Cannot run seteuid to %d: %s", \
                  __FILE__,                                             \
                  __LINE__,                                             \
                  G_GNUC_PRETTY_FUNCTION,                                       \
                  (int)uid,                                             \
                  strerror (errno));                    }
#define NEVER_FAILS_setegid(gid) \
        { int r = 0; \
          if (getegid () != gid) \
            r = setegid (gid); \
          if G_UNLIKELY (r != 0) \
        g_error ("GDM file %s: line %d (%s): Cannot run setegid to %d: %s", \
                  __FILE__,                                             \
                  __LINE__,                                             \
                  G_GNUC_PRETTY_FUNCTION,                                       \
                  (int)gid,                                             \
                  strerror (errno));                    }

/* first goes to euid-root and then sets the egid and euid, to make sure
 * this succeeds */
#define NEVER_FAILS_root_set_euid_egid(uid,gid) \
        { NEVER_FAILS_seteuid (0); \
          NEVER_FAILS_setegid (gid); \
          if (uid != 0) { NEVER_FAILS_seteuid (uid); } }


/* like fopen with "w" but unlinks and uses O_EXCL */
FILE *         gdm_safe_fopen_w  (const char *file,
                                  mode_t      perm);

/* This is for race free forks */
void           gdm_sigchld_block_push (void);
void           gdm_sigchld_block_pop (void);
void           gdm_sigterm_block_push (void);
void           gdm_sigterm_block_pop (void);
void           gdm_sigusr2_block_push (void);
void           gdm_sigusr2_block_pop (void);

void           ve_clearenv (void);

G_END_DECLS

#endif /* _GDM_COMMON_UNKNOWN_H */
