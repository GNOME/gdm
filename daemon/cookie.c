/* GDM - The Gnome Display Manager
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
#include <gnome.h>

#include "gdm.h"
#include "md5.h"
#include "cookie.h"

#define MAXBUFFERSIZE 512

struct rngs {
   const char *path;
   int        length;
} rngs[] = {
   { "/dev/random",              16 },
   { "/dev/urandom",            128 },
   { "/proc/stat",    MAXBUFFERSIZE },
   { "/proc/loadavg", MAXBUFFERSIZE },
   { "/dev/audio",    MAXBUFFERSIZE },
};

#define RNGS (sizeof(rngs)/sizeof(struct rngs))


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
    struct timeval tv;
    struct timezone tz;
    char cookie[40 /* 2*16 == 32, so 40 is enough */];

    cookie[0] = '\0';

    gdm_md5_init (&ctx);
    gettimeofday (&tv, &tz);
    gdm_md5_update (&ctx, (unsigned char *) &tv, sizeof (tv));
    pid = getppid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
    pid = getpid();
    gdm_md5_update (&ctx, (unsigned char *) &pid, sizeof (pid));
        
    for (i = 0; i < RNGS; i++) {
	if ((fd = open (rngs[i].path, O_RDONLY|O_NONBLOCK)) >= 0) {

	    r = read (fd, buf, sizeof (buf));

	    if (r > 0)
		gdm_md5_update (&ctx, buf, r);
	    else
		r = 0;

	    close (fd);

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
