/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gdm.h"
#include "misc.h"
#include "getvt.h"
#include "gdmconfig.h"

/* Virtual terminals only supported on Linux, FreeBSD, or DragonFly */

#if defined (__linux__) || defined (__FreeBSD__) || defined (__DragonFly__)

#if defined (__linux__)
#include <sys/vt.h>
#elif defined (__FreeBSD__) || defined (__DragonFly__)
#include <sys/consio.h>

static const char*
__itovty(int val)
{
	static char str[8];
	char* next = str + sizeof (str) - 1;

	*next = '\0';
	do {
		*--next = "0123456789abcdefghigklmnopqrstuv"[val % 32];
	} while (val /= 32);

	return next;
}
#endif

static int
open_vt (int vtno)
{
	char *vtname;
	int fd;

#if defined (__linux__)
	vtname = g_strdup_printf ("/dev/tty%d", vtno);
#elif defined (__FreeBSD__) || defined (__DragonFly__)
	vtname = g_strdup_printf ("/dev/ttyv%s", __itovty(vtno - 1));
#endif
	do {
		errno = 0;
		fd = open (vtname, O_RDWR
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
			   , 0);
	} while G_UNLIKELY (errno == EINTR);
	g_free (vtname);
	return fd;
}

#if defined (__linux__)

static int 
get_free_vt_linux (int *vtfd)
{
	int fd, fdv;
	int vtno;
	unsigned short vtmask;
	struct vt_stat vtstat;

	*vtfd = -1;

	do {
		errno = 0;
		fd = open ("/dev/console", O_WRONLY
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
			   , 0);
	} while G_UNLIKELY (errno == EINTR);
	if (fd < 0)
		return -1;

	if (ioctl (fd, VT_GETSTATE, &vtstat) < 0) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}

	for (vtno = gdm_get_value_int (GDM_KEY_FIRST_VT), vtmask = 1 << vtno;
			vtstat.v_state & vtmask; vtno++, vtmask <<= 1);
	if (!vtmask) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}

	fdv = open_vt (vtno);
	if (fdv < 0) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}
	*vtfd = fdv;
	return vtno;
}

#elif defined (__FreeBSD__) || defined (__DragonFly__)

static int
get_free_vt_freebsd_dragonfly (int *vtfd)
{
	int fd, fdv;
	int vtno;
	GList *to_close_vts = NULL, *li;

	*vtfd = -1;

	do {
		errno = 0;
		fd = open ("/dev/console", O_WRONLY
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
			   , 0);
	} while G_UNLIKELY (errno == EINTR);
	if (fd < 0)
		return -1;

	if ((ioctl(fd, VT_OPENQRY, &vtno) < 0) || (vtno == -1)) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}

	fdv = open_vt (vtno);
	if (fdv < 0) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}

	while (vtno < gdm_get_value_int (GDM_KEY_FIRST_VT)) {
		int oldvt = vtno;
		to_close_vts = g_list_prepend (to_close_vts,
					       GINT_TO_POINTER (fdv));

		if (ioctl(fd, VT_OPENQRY, &vtno) == -1) {
			vtno = -1;
			goto cleanup;
		}

		if (oldvt == vtno) {
			vtno = -1;
			goto cleanup;
		}

		fdv = open_vt (vtno);
		if (fdv < 0) {
			vtno = -1;
			goto cleanup;
		}
	}

	*vtfd = fdv;

cleanup:
	for (li = to_close_vts; li != NULL; li = li->next) {
		VE_IGNORE_EINTR (close (GPOINTER_TO_INT (li->data)));
	}
	return vtno;
}

#endif

char *
gdm_get_empty_vt_argument (int *fd, int *vt)
{
	if ( ! gdm_get_value_bool (GDM_KEY_VT_ALLOCATION)) {
		*fd = -1;
		return NULL;
	}

#if defined (__linux__)
	*vt = get_free_vt_linux (fd);
#elif defined (__FreeBSD__) || defined (__DragonFly__)
	*vt = get_free_vt_freebsd_dragonfly (fd);
#endif

	if (*vt < 0)
		return NULL;
	else
		return g_strdup_printf ("vt%d", *vt);
}

/* change to an existing vt */
void
gdm_change_vt (int vt)
{
	int fd;
	if (vt < 0)
		return;

	do {
		errno = 0;
		fd = open ("/dev/console", O_WRONLY
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
			   , 0);
	} while G_UNLIKELY (errno == EINTR);
	if (fd < 0)
		return;

	ioctl (fd, VT_ACTIVATE, vt);
	ioctl (fd, VT_WAITACTIVE, vt);

	VE_IGNORE_EINTR (close (fd));
}

int
gdm_get_cur_vt (void)
{
#if defined (__linux__)
	struct vt_stat s;
#elif defined (__FreeBSD__) || defined (__DragonFly__)
	int vtno;
#endif
	int fd;

	do {
		errno = 0;
		fd = open ("/dev/console", O_WRONLY
#ifdef O_NOCTTY
			   |O_NOCTTY
#endif
			   , 0);
	} while G_UNLIKELY (errno == EINTR);
	if (fd < 0)
		return -1;
#if defined (__linux__)
	ioctl (fd, VT_GETSTATE, &s);

	VE_IGNORE_EINTR (close (fd));

	/* debug */
	/*
	printf ("current_Active %d\n", (int)s.v_active);
	*/

	return s.v_active;
#elif defined (__FreeBSD__) || defined (__DragonFly__)
	if (ioctl (fd, VT_GETACTIVE, &vtno) == -1) {
		VE_IGNORE_EINTR (close (fd));
		return -1;
	}

	VE_IGNORE_EINTR (close (fd));

	/* debug */
	/*
	printf ("current_Active %d\n", vtno);
	*/

	return vtno;
#endif
}

#else /* here this is just a stub, we don't know how to do this outside
	 of linux really */

char *
gdm_get_empty_vt_argument (int *fd, int *vt)
{
	*fd = -1;
	*vt = -1;
	return NULL;
}

void
gdm_change_vt (int vt)
{
	return;
}

int
gdm_get_cur_vt (void)
{
	return -1;
}

#endif
