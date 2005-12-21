/* GDM - The GNOME Display Manager
 * Copyright (C) 2005 Sun Microsystems, Inc.
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
 * program to either force pages into memory or force them
 * out (-o option)
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>

int out = 0;

int
doout(char *s)
{
	int fd;
	void *map;
	struct stat buf;

	if (((fd = open(s, O_RDONLY)) < 0) ||
	    (fstat(fd, &buf) < 0) ||
	    ((map = mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) ==
	    MAP_FAILED)) {
		(void)close(fd);
		return (-1);
	}

	(void)close(fd);
	(void)msync(map, buf.st_size, MS_INVALIDATE);
	(void)munmap(map, buf.st_size);
	return (0);
}

#define SIZE 1024*128

int
doin(char *s)
{
	int fd;
	char buffer[SIZE];
	
	if ((fd = open(s, O_RDONLY)) < 0) {
		fprintf(stderr, "fopen: %s %s\n", strerror(errno), s);
		return (-1);
	}

	while (read(fd, buffer, SIZE) != 0)
		;

	(void)close(fd);

	return (0);
}



int
main(int argc, char *argv[])
{
	FILE *fp = 0;
	int c, errflg = 0;
	extern int optind, optopt;
	extern char *optarg;
	int i;

	while ((c = getopt(argc, argv, "o:")) != -1) {
		switch(c) {

		case 'o':
			out = 1;
			break;
		default:
			errflg++;
			break;

		}
	}

	if (errflg) {
		fprintf(stderr, "usage: %s [-o] filename [filename]\n",
			argv[0]);
		exit(1);
	}


	for (; optind < argc; optind++) {
		if ((argv[optind][0] == '@') && ((fp = fopen(argv[optind], "r")) == 0)) {
			char path[1024];

			if ((fp = fopen(&(argv[optind][1]), "r")) == 0) {
				fprintf(stderr, "fopen: %s %s\n", strerror(errno), &argv[optind][1]);
				continue;
			}
			while (fgets(path, sizeof(path), fp) != 0) {
				path[strlen(path) -1] = '\0';

				if (!out) {
					doin(path);
				} else {
					doout(path);
				}
			}
			fclose (fp);
			fp = 0;

		} else {
			if (fp != 0) {
				fclose (fp);
				fp = 0;
			}

			if (!out) {
				doin(argv[optind]);
			} else {
				doout(argv[optind]);
			}
		}
	}
	exit(0);
}

