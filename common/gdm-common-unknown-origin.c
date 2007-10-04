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

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <setjmp.h>
#include <dirent.h>

#ifdef HAVE_CRT_EXTERNS_H
#include <crt_externs.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>

#include "gdm-common.h"

static int sigchld_blocked = 0;
static sigset_t sigchldblock_mask, sigchldblock_oldmask;

static int sigterm_blocked = 0;
static sigset_t sigtermblock_mask, sigtermblock_oldmask;

static int sigusr2_blocked = 0;
static sigset_t sigusr2block_mask, sigusr2block_oldmask;

void
gdm_sigchld_block_push (void)
{
        sigchld_blocked++;

        if (sigchld_blocked == 1) {
                /* Set signal mask */
                sigemptyset (&sigchldblock_mask);
                sigaddset (&sigchldblock_mask, SIGCHLD);
                sigprocmask (SIG_BLOCK, &sigchldblock_mask, &sigchldblock_oldmask);
        }
}

void
gdm_sigchld_block_pop (void)
{
        sigchld_blocked --;

        if (sigchld_blocked == 0) {
                /* Reset signal mask back */
                sigprocmask (SIG_SETMASK, &sigchldblock_oldmask, NULL);
        }
}

void
gdm_sigterm_block_push (void)
{
        sigterm_blocked++;

        if (sigterm_blocked == 1) {
                /* Set signal mask */
                sigemptyset (&sigtermblock_mask);
                sigaddset (&sigtermblock_mask, SIGTERM);
                sigaddset (&sigtermblock_mask, SIGINT);
                sigaddset (&sigtermblock_mask, SIGHUP);
                sigprocmask (SIG_BLOCK, &sigtermblock_mask, &sigtermblock_oldmask);
        }
}

void
gdm_sigterm_block_pop (void)
{
        sigterm_blocked --;

        if (sigterm_blocked == 0) {
                /* Reset signal mask back */
                sigprocmask (SIG_SETMASK, &sigtermblock_oldmask, NULL);
        }
}

void
gdm_sigusr2_block_push (void)
{
        sigset_t oldmask;

        if (sigusr2_blocked == 0) {
                /* Set signal mask */
                sigemptyset (&sigusr2block_mask);
                sigaddset (&sigusr2block_mask, SIGUSR2);
                sigprocmask (SIG_BLOCK, &sigusr2block_mask, &oldmask);
        }

        sigusr2_blocked++;

        sigusr2block_oldmask = oldmask;
}

void
gdm_sigusr2_block_pop (void)
{
        sigset_t oldmask;

        oldmask = sigusr2block_oldmask;

        sigusr2_blocked--;

        if (sigusr2_blocked == 0) {
                /* Reset signal mask back */
                sigprocmask (SIG_SETMASK, &sigusr2block_oldmask, NULL);
        }
}

/* Like fopen with "w" */
FILE *
gdm_safe_fopen_w (const char *file,
                  mode_t      perm)
{
        int fd;
        FILE *ret;
        VE_IGNORE_EINTR (g_unlink (file));
        do {
                int flags;

                errno = 0;
                flags = O_EXCL | O_CREAT | O_TRUNC | O_WRONLY;
#ifdef O_NOCTTY
                flags |= O_NOCTTY;
#endif
#ifdef O_NOFOLLOW
                flags |= O_NOFOLLOW;
#endif

                fd = g_open (file, flags, perm);
        } while (errno == EINTR);

        if (fd < 0) {
                return NULL;
        }

        ret = fdopen (fd, "w");
        return ret;
}

/**
 * ve_clearenv:
 *
 * Description: Clears out the environment completely.
 * In case there is no native implementation of clearenv,
 * this could cause leaks depending on the implementation
 * of environment.
 *
 **/
void
ve_clearenv (void)
{
#ifdef HAVE_CLEARENV
        clearenv ();
#else

#ifdef HAVE__NSGETENVIRON
#define environ (*_NSGetEnviron())
#else
        extern char **environ;
#endif

        if (environ != NULL)
                environ[0] = NULL;
#endif
}
