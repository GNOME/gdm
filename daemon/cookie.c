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

#define MAXBUFFERSIZE 512

struct rngs {
   const char *path;
   int        length;
   off_t      seek;
} rngs[] = {
   { "/dev/random",              32,	0 },
#ifdef __OpenBSD__
   { "/dev/srandom",            32,	0 },
#endif
   { "/dev/urandom",            128,	0 },
   { "/proc/stat",    MAXBUFFERSIZE,	0 },
   { "/proc/loadavg", MAXBUFFERSIZE,	0 },
   { "/dev/mem",      MAXBUFFERSIZE,	0x100000 },
   { "/dev/audio",    MAXBUFFERSIZE,	0 },
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
	int i;
	struct timeval tv;
	struct timezone tz;

	gettimeofday (&tv, &tz);

	srand (tv.tv_usec + tv.tv_sec);
	for (i = 0; i < RANDNUMS; i++)
		randnums[i] += rand ();
}

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

    pid = getppid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
    pid = getpid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
        
    for (i = 0; i < RNGS; i++) {
	fd = open (rngs[i].path, O_RDONLY|O_NONBLOCK
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

	    if (r >= rngs[i].length) 
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
}


/* EOF */
