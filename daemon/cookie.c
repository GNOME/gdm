/* GDM - The Gnome Display Manager
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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <libgnome/libgnome.h>

#include "gdm.h"
#include "md5.h"
#include "cookie.h"

#define MAXBUFFERSIZE 1024

struct rngs {
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
#if defined(__i386__) || defined(__386__) || defined(_M_IX86)
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
   { "/var/spool/mail/root", MAXBUFFERSIZE,	0 },
};

#define RNGS (sizeof(rngs)/sizeof(struct rngs))

/* Some semi random spinners to spin,
 * this is 20 bytes of semi random data */
#define RANDNUMS 5
static int randnums[RANDNUMS];

/* This adds a little bit of entropy to our buffer,
   just in case /dev/random doesn't work out for us
   really since that normally adds enough bytes of nice
   randomness already */
void
gdm_random_tick (void)
{
	struct timeval tv;
	struct timezone tz;
	static GRand *rnd = NULL;

	gettimeofday (&tv, &tz);

	if (rnd == NULL)
		rnd = g_rand_new_with_seed (tv.tv_usec ^ tv.tv_sec);
	else
		g_rand_set_seed (rnd, tv.tv_usec ^ tv.tv_sec);
	randnums[0] += g_rand_int (rnd);
	randnums[1] *= g_rand_int (rnd);
	randnums[2] ^= g_rand_int (rnd);

	randnums[3] += g_random_int ();
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
		if (i > 0 &&
		    lastval != buf[idx])
			return TRUE;
		lastval = buf[idx];
	}
	return FALSE;
}

static unsigned char old_cookie[16];

void 
gdm_cookie_generate (GdmDisplay *d)
{
    int i;
    struct GdmMD5Context ctx;
    unsigned char digest[16];
    unsigned char buf[MAXBUFFERSIZE];
    int fd;
    pid_t pid;
    int r;
    char cookie[40 /* 2*16 == 32, so 40 is enough */];

    cookie[0] = '\0';

    gdm_md5_init (&ctx);

    /* spin the spinners according to current time */
    gdm_random_tick ();

    gdm_md5_update (&ctx, (unsigned char *) randnums, sizeof (int) * RANDNUMS);

    /* use the last cookie */
    gdm_md5_update (&ctx, old_cookie, 16);

    pid = getppid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
    pid = getpid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
        
    for (i = 0; i < RNGS; i++) {
	const char *file = rngs[i].path;
	if (file == NULL)
	    file = d->authfile;
	if (file == NULL)
	    continue;
	fd = open (file, O_RDONLY|O_NONBLOCK
#ifdef O_NOCTTY
			|O_NOCTTY
#endif
#ifdef O_NOFOLLOW
			|O_NOFOLLOW
#endif
		   );
	if (fd >= 0) {
	    /* Apparently this can sometimes block anyway even if it is O_NONBLOCK,
	       so use select to figure out if there is something available */
	    fd_set rfds;
	    struct timeval tv;

	    FD_ZERO (&rfds);
	    FD_SET (fd, &rfds);

	    tv.tv_sec = 0;
	    tv.tv_usec = 10*1000 /* 10 ms */;
	    r = 0;

	    if (rngs[i].seek > 0)
		lseek (fd, rngs[i].seek, SEEK_SET);

	    if (select (fd+1, &rfds, NULL, NULL, &tv) > 0) {
	        IGNORE_EINTR (r = read (fd, buf, MIN (sizeof (buf), rngs[i].length)));
	    }

	    if (r > 0)
		gdm_md5_update (&ctx, buf, r);
	    else
		r = 0;

	    IGNORE_EINTR (close (fd));

	    if (r >= rngs[i].length &&
		data_seems_random (buf, r)) 
		break;
	}
    }

    gdm_md5_final (digest, &ctx);

    for (i = 0; i < 16; i++) {
	    char sub[3];
	    g_snprintf (sub, sizeof (sub), "%02x", (guint)digest[i]);
	    strcat (cookie, sub);
    }

    g_free (d->cookie);
    d->cookie = g_strdup (cookie);
    g_free (d->bcookie);
    d->bcookie = g_new (char, 16);
    memcpy (d->bcookie, digest, 16);
    memcpy (old_cookie, digest, 16);
}


/* EOF */
