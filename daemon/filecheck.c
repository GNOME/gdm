/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";

gboolean gdm_file_check(gchar *caller, uid_t user, gchar *dir, gchar *file, gboolean absentok);

extern gint GdmUserMaxFile;
extern gint GdmRelaxPerms;

gboolean
gdm_file_check(gchar *caller, uid_t user, gchar *dir, gchar *file, gboolean absentok)
{
    struct stat statbuf;
    gchar *str;

    /* Stat directory */
    if(stat(dir, &statbuf) == -1) 
	return(FALSE);

    /* Check if dir is owned by the user */
    if(statbuf.st_uid != user) {
	syslog(LOG_WARNING, _("%s: %s is not owned by uid %d."), caller, dir, user);
	return(FALSE);
    }
    
    /* Check if group has write permission */
    if(GdmRelaxPerms<1 && (statbuf.st_mode & S_IWGRP) == S_IWGRP) {
	syslog(LOG_WARNING, _("%s: %s is writable by group."), caller, dir);
	return(FALSE);
    }

    /* Check if other has write permission */
    if(GdmRelaxPerms<2 && (statbuf.st_mode & S_IWOTH) == S_IWOTH) {
	syslog(LOG_WARNING, _("%s: %s is writable by other."), caller, dir);
	return(FALSE);
    }

    str=g_strconcat(dir, "/", file, NULL);

    /* Stat file */
    if(stat(str, &statbuf) == -1) {
	g_free(str);

	/* Return true if file is absent and that is ok */
	if(absentok)
	    return(TRUE);
	else
	    return(FALSE);
    }

    /* Check that it is a regular file ... */
    if(! S_ISREG(statbuf.st_mode)) {
	syslog(LOG_WARNING,_("%s: %s is not a regular file."), caller, str);
	g_free(str);
	return(FALSE);
    }

    /* ... owned by the user ... */
    if(statbuf.st_uid != user) {
	syslog(LOG_WARNING, _("%s: %s is not owned by uid %d."), caller, str, user);
	g_free(str);
	return(FALSE);
    }

    /* ... unwritable by group ... */
    if(GdmRelaxPerms<1 && (statbuf.st_mode & S_IWGRP) == S_IWGRP) {
	syslog(LOG_WARNING, _("%s: %s is writable by group."), caller, str);
	g_free(str);
	return(FALSE);
    }

    /* ... unwritable by others ... */
    if(GdmRelaxPerms<2 && (statbuf.st_mode & S_IWOTH) == S_IWOTH) {
	syslog(LOG_WARNING, _("%s: %s is writable by group/other."), caller, str);
	g_free(str);
	return(FALSE);
    }

    /* ... and smaller than sysadmin specified limit. */
    if(statbuf.st_size > GdmUserMaxFile) {
	syslog(LOG_WARNING, _("%s: %s is bigger than sysadmin specified maximum file size."), caller, str);
	g_free(str);
	return(FALSE);
    }

    g_free(str);

    /* Yeap, this file is ok */
    return(TRUE);
}

/* EOF */
