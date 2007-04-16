/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 * Copyright (C) Rik Faith <faith@precisioninsight.com>
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

/*
 * Functions for generating MIT-MAGIC-COOKIEs.
 *
 * This code was derived (i.e. stolen) from mcookie.c written by Rik Faith
 *
 * Note that this code goes to much greater lengths to be as random as possible.
 * Thus being more secure on systems without /dev/random and friends.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "gdm.h"
#include "md5.h"
#include "cookie.h"

#include "gdm-common.h"
#include "gdm-daemon-config.h"

#define MAXBUFFERSIZE 1024

static struct rngs {
	const char *path; /* null is the authfile name */
	int        length;
	off_t      seek;
} rngs[] = {
	{ "/dev/random",              16,	0 },
#ifdef __OpenBSD__
	{ "/dev/srandom",             16,	0 },
#endif
	{ "/dev/urandom",            128,	0 },
	{ "/proc/stat",    MAXBUFFERSIZE,	0 },
	{ "/proc/interrupts", MAXBUFFERSIZE,	0 },
	{ "/proc/loadavg", MAXBUFFERSIZE,	0 },
	{ "/proc/meminfo", MAXBUFFERSIZE,	0 },
#if defined (__i386__) || defined (__386__) || defined (_M_IX86)
	/* On i386, we should not read the first 16megs */
	{ "/dev/mem",      MAXBUFFERSIZE,	0x100000 },
#else
	{ "/dev/mem",      MAXBUFFERSIZE,	0 },
#endif
	/* this will load the old authfile for the display */
	{ NULL /* null means the authfile */, MAXBUFFERSIZE,	0 },
	{ "/proc/net/dev", MAXBUFFERSIZE,	0 },
	{ "/dev/audio",    MAXBUFFERSIZE,	0 },
	{ "/etc/shadow",   MAXBUFFERSIZE,	0 },
	{ "/var/log/messages",   MAXBUFFERSIZE,	0 },
};

/* Some semi random spinners to spin,
 * this is 20 bytes of semi random data */
#define RANDNUMS 5
static guint32 randnums[RANDNUMS];

/* stolen from XDM which in turn stole this
   from the C standard */
static guint32
next_rand_15 (guint32 num)
{
	num = num * 1103515245 + 12345;
	return (unsigned int)(num/65536) % 32768;
}

/* really quite apparently only 31 bits of entropy result (from tests),
   but oh well */
static guint32
next_rand_32 (guint32 num)
{
	int i;
	guint32 ret;
	guint8 *p = (guint8 *)&ret;

	for (i = 0; i < 4; i++)  {
		num = next_rand_15 (num);
		p[i] = (num & 0xff00) >> 8;
	}
	return ret;
}

/* This adds a little bit of entropy to our buffer,
   just in case /dev/random doesn't work out for us
   really since that normally adds enough bytes of nice
   randomness already */
void
gdm_random_tick (void)
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday (&tv, &tz);

	/* the higher order bits of the seconds
	   are quite uninteresting */
	randnums[0] ^= next_rand_32 ((tv.tv_sec << 20) ^ tv.tv_usec);

	/* different method of combining */
	randnums[1] ^= next_rand_32 (tv.tv_sec) ^ next_rand_32 (tv.tv_usec);

	/* probably unneeded, but just being anal */
	randnums[2] ^= (tv.tv_sec << 20) ^ tv.tv_usec;

	/* probably unneeded, to guess above
	   the number of invocation is likely needed
	   anyway */
	randnums[3]++;

	/* also hope that other places call
	   g_random_int.  Note that on systems
	   without /dev/urandom, this will yet again
	   measure time the first time it's called and
	   we'll add entropy based on the speed of the
	   computer.  Yay */
	randnums[4] ^= g_random_int ();
}

/* check a few values and if we get the same
   value, it's not really random.  Likely
   we got perhaps a string of zeros or some
   such. */
static gboolean
data_seems_random (const char buf[], int size)
{
	int i, lastval = 0;
	if (size < 16)
		return FALSE;
	for (i = 0; i < 10; i++) {
		int idx = g_random_int_range (0, size);
		if G_LIKELY (i > 0 &&
			     lastval != buf[idx])
			return TRUE;
		lastval = buf[idx];
	}
	return FALSE;
}

static unsigned char old_cookie[16];

void
gdm_cookie_generate (char **cookiep,
		     char **bcookiep)
{
	int i;
	struct GdmMD5Context ctx;
	unsigned char digest[16];
	unsigned char buf[MAXBUFFERSIZE];
	int fd;
	pid_t pid;
	int r;
	char cookie[40]; /* 2*16 == 32, so 40 is enough */

	cookie[0] = '\0';

	gdm_md5_init (&ctx);

	/* spin the spinners according to current time */
	gdm_random_tick ();

	gdm_md5_update (&ctx, (unsigned char *) randnums, sizeof (int) * RANDNUMS);

	/* use the last cookie */
	gdm_md5_update (&ctx, old_cookie, 16);

	/* use some uninitialized stack space */
	gdm_md5_update (&ctx, (unsigned char *) cookie, sizeof (cookie));

	pid = getppid ();
	gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
	pid = getpid ();
	gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));

	for (i = 0; i < G_N_ELEMENTS (rngs); i++) {
		const char *file = rngs[i].path;

		if G_UNLIKELY (file == NULL)
			continue;
		do {
			int flags;

			flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOCTTY
			flags |= O_NOCTTY;
#endif
#ifdef O_NOFOLLOW
			flags |= O_NOFOLLOW;
#endif

			errno = 0;
			fd = open (file, flags);

		} while G_UNLIKELY (errno == EINTR);

		if G_LIKELY (fd >= 0) {
			/* Apparently this can sometimes block anyway even if it is O_NONBLOCK,
			   so use select to figure out if there is something available */
			fd_set rfds;
			struct timeval tv;

			FD_ZERO (&rfds);
			FD_SET (fd, &rfds);

			tv.tv_sec = 0;
			tv.tv_usec = 10*1000 /* 10 ms */;
			r = 0;

			if G_UNLIKELY (rngs[i].seek > 0)
				lseek (fd, rngs[i].seek, SEEK_SET);

			if G_LIKELY (select (fd+1, &rfds, NULL, NULL, &tv) > 0) {
				VE_IGNORE_EINTR (r = read (fd, buf, MIN (sizeof (buf), rngs[i].length)));
			}

			if G_LIKELY (r > 0)
				gdm_md5_update (&ctx, buf, r);
			else
				r = 0;

			VE_IGNORE_EINTR (close (fd));

			if G_LIKELY (r >= rngs[i].length &&
				     data_seems_random ((char *) buf, r))
				break;
		}
	}

	gdm_md5_final (digest, &ctx);

	for (i = 0; i < 16; i++) {
		char sub[3];
		g_snprintf (sub, sizeof (sub), "%02x", (guint)digest[i]);
		strcat (cookie, sub);
	}

	if (cookiep != NULL) {
		*cookiep = g_strdup (cookie);
	}

	if (bcookiep != NULL) {
		*bcookiep = g_new (char, 16);
		memcpy (*bcookiep, digest, 16);
	}

	memcpy (old_cookie, digest, 16);
}
