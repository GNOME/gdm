/*
 * 	 gdmopen.c by the Queen of England, based upon original open.
 *	 Simplified for the purposes of gdm.  All useless (to me)
 *	 functionality stripped.  Also returns what the command returns.
 *	 Return of 66 means error with open.
 *
 *	 Original header:
 *
 *       open.c open a vt to run a new command (or shell).
 *       
 *	 Copyright (c) 1994 by Jon Tombs <jon@gtex02.us.es>
 *
 *       This program is free software; you can redistribute it and/or
 *       modify it under the terms of the GNU General Public License
 *       as published by the Free Software Foundation; either version
 *       2 of the License, or (at your option) any later version.
 */

#include "config.h"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vt.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef FALSE
#define FALSE 0
#define TRUE !FALSE
#endif


/*
 * Where your VTs are hidden
 */
#ifdef __linux__
#define VTNAME "/dev/tty%d"
#endif

#ifdef ESIX_5_3_2_D
#define	VTBASE		"/dev/vt%02d"
#endif

const char *GDMOPENversion = "gdmopen " VERSION " simplified (was: open: 1.4 (c) Jon Tombs 1994)";

#ifndef VTNAME
#error vt device name must be defined in open.c
#endif

static pid_t child_pid = -1; 
struct vt_stat vt;
static int vtno;
static int fd = 0;
static int do_dealloc = FALSE;

static void
sighandler (int sig)
{
	if (child_pid > 1) {
		if (kill (child_pid, sig) == 0)
			waitpid (child_pid, NULL, 0);
	}

	if (do_dealloc) {
		/* Switch back... */
		(void) ioctl(fd, VT_ACTIVATE, vt.v_active);
		/* wait to be really sure we have switched */
		(void) ioctl(fd, VT_WAITACTIVE, vt.v_active);

		/* Now deallocate our new one  */
		(void) ioctl(fd, VT_DISALLOCATE, vtno);
	}

	/* Kill myself with this signal */
	signal (sig, SIG_DFL);
	kill (getpid (), sig);
}

int 
main (int argc, char *argv[])
{
	char vtname[256];
	int status;

	if (getuid () != geteuid () ||
	    getuid () != 0) {
		fprintf (stderr, "gdmopen: Only root wants to run me\n");
		return 66;
	}

	signal (SIGTERM, sighandler);
	signal (SIGINT, sighandler);
	signal (SIGHUP, sighandler);

	if (argc <= 1) {
		fprintf (stderr, "gdmopen: must supply a command!\n");
		return 66;
	}

	fd = open ("/dev/console", O_WRONLY, 0);
	if (fd < 0) {
		perror ("gdmopen: Failed to open /dev/console");	
		return 66;
	}

	errno = 0;
	if ((ioctl(fd, VT_OPENQRY, &vtno) < 0) || (vtno == -1)) {
		perror ("gdmopen: Cannot find a free VT");
		close (fd);
		return 66;
	}

	if (ioctl(fd, VT_GETSTATE, &vt) < 0) {
		perror ("gdmopen: can't get VTstate");
		close(fd);
		return 66;
	}

	snprintf (vtname, sizeof (vtname), VTNAME, vtno);

	chown (vtname, 0, -1);

	child_pid = fork();
	if (child_pid == 0) {
		char VT_NUMBER[256];

		snprintf (VT_NUMBER, sizeof (VT_NUMBER), "VT_NUMBER=%d", vtno);
		putenv (VT_NUMBER);

		signal (SIGTERM, SIG_DFL);
		signal (SIGINT, SIG_DFL);
		signal (SIGHUP, SIG_DFL);

		/* leave current vt */
		if (
#ifdef   ESIX_5_3_2_D
		setpgrp() < 0
#else
		setsid() < 0
#endif      
		) {
			fprintf(stderr, "open: Unable to set new session (%s)\n",
				strerror(errno));
		}
		close(0);
		close(1);
		close(2);
		close(fd);	 

		/* and grab new one */
		fd = open (vtname, O_RDWR);
		if (fd < 0) { /* Shouldn't happen */
			_exit (66); /* silently die */
		}
		dup(fd);
		dup(fd);

		/* 
		* Can't tell anyone if any of these fail, so throw away
		* the return values 
		 */
		(void) ioctl(fd, VT_ACTIVATE, vtno);
		/* wait to be really sure we have switched */
		(void) ioctl(fd, VT_WAITACTIVE, vtno);

#ifdef __linux__
		/* Turn on fonts */
		write (0, "\033(K", 3);
#endif /* __linux__ */

		execvp (argv[1], &argv[1]);

		_exit (66); /* failed */
	}

	if (child_pid < 0) {
		perror ("gdmopen: fork() error");
		return 66;
	}

	do_dealloc = TRUE;

	waitpid (child_pid, &status, 0);
	child_pid = -1;

	do_dealloc = FALSE;

	/* Switch back... */
	(void) ioctl(fd, VT_ACTIVATE, vt.v_active);
	/* wait to be really sure we have switched */
	(void) ioctl(fd, VT_WAITACTIVE, vt.v_active);

	/* Now deallocate our new one  */
	(void) ioctl(fd, VT_DISALLOCATE, vtno);

	close (fd);

	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return 66;
}
