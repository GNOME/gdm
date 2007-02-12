/*
 * 	 gdmmkstemp.c by the Queen of England
 *	 A utility to do mkstemp from a script.
 *       
 *	 Copyright (c) 2001 by Queen of England
 *
 *       This program is free software; you can redistribute it and/or
 *       modify it under the terms of the GNU General Public License
 *       as published by the Free Software Foundation; either version
 *       2 of the License, or (at your option) any later version.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


int 
main (int argc, char *argv[])
{
	char template[] = "/tmp/gdm-XXXXXX";
	int i;

	i = mkstemp (template);
	if (i < 0)
		return 1;
	
	fchmod (i, 0600);

	printf ("%s\n", template);
	close (i);
	return 0;
}
