#include "config.h"

#include <libgnome/libgnome.h>
#include "gdm.h"
#include "misc.h"
#include "getvt.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

extern int GdmFirstVT;
extern gboolean GdmVTAllocation;

#ifdef __linux__

#include <sys/vt.h>

static int
open_vt (int vtno)
{
	char *vtname = g_strdup_printf ("/dev/tty%d", vtno);
	int fd = open (vtname, O_RDWR);
	g_free (vtname);
	return fd;
}

static int 
get_free_vt (int *vtfd)
{
	int fd, fdv;
	int vtno;
	GList *to_close_vts = NULL, *li;

	*vtfd = -1;

	fd = open ("/dev/console", O_WRONLY, 0);
	if (fd < 0)
		return -1;

	if ((ioctl(fd, VT_OPENQRY, &vtno) < 0) || (vtno == -1)) {
		close (fd);
		return -1;
	}

	fdv = open_vt (vtno);
	if (fdv < 0) {
		close (fd);
		return -1;
	}

	while (vtno < GdmFirstVT) {
		int oldvt = vtno;
		to_close_vts = g_list_prepend (to_close_vts,
					       GINT_TO_POINTER (fdv));

		if ((ioctl(fd, VT_OPENQRY, &vtno) < 0) || (vtno == -1)) {
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
		close (GPOINTER_TO_INT (li->data));
	}
	return vtno;
}

char *
gdm_get_empty_vt_argument (int *fd, int *vt)
{
	if ( ! GdmVTAllocation) {
		*fd = -1;
		return NULL;
	}

	*vt = get_free_vt (fd);
	if (*vt < 0)
		return NULL;
	else
		return g_strdup_printf ("vt%d", *vt);
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

#endif
