/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@mkp.net>
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
 * This file contains the X Authentication code
 */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";

extern gchar *GdmAuthDir;
extern gid_t GdmGroupId;

extern gchar **gdm_arg_munch(gchar *);
extern void gdm_cookie_generate(GdmDisplay *d);
extern void *gdm_debug(const gchar *, ...);
extern void *gdm_error(const gchar *, ...);

void gdm_auth_secure_display(GdmDisplay *d);
void gdm_auth_user_add(GdmDisplay *d, gchar *home);
void gdm_auth_user_remove(GdmDisplay *d, gchar *home);


void gdm_auth_secure_display(GdmDisplay *d)
{
    gchar *authstr;
    gchar **argv;
    pid_t authpid;

    gdm_debug("gdm_auth_secure_display: Securing %s", d->name);

    gdm_cookie_generate(d);

    d->auth=g_strconcat(GdmAuthDir, "/", d->name, ".xauth", NULL);
    
    if(unlink(d->auth) == -1)
	gdm_debug(_("gdm_auth_secure_display: Could not unlink %s file: %s"),\
		  d->auth, strerror(errno));
    
    authstr=g_strconcat(GDM_XAUTH_PATH, " -i -f ", d->auth, \
			" add ", d->name, " . ", d->cookie, NULL);
    argv=gdm_arg_munch(authstr);
    
    switch(authpid=fork()) {
	
    case 0:
	execv(argv[0], argv);
	gdm_error(_("gdm_auth_secure_display: Error starting xauth process: %s. Running insecure!"), strerror(errno));
	return;
	
    case -1:
	gdm_error(_("gdm_auth_secure_display: Error forking xauth process. Running insecure!"));
	return;
	
    default:
	waitpid(authpid, 0, 0); /* Wait for xauth to finish */
	chown(d->auth, 0, GdmGroupId);
	chmod(d->auth, S_IRUSR|S_IWUSR|S_IRGRP);
	break;
    }

}


void gdm_auth_user_add(GdmDisplay *d, gchar *home)
{
    gchar *authfile, *authstr;
    gchar **argv;
    pid_t authpid;

    gdm_debug("gdm_auth_user_add: Adding cookie to %s", home);

    authfile=g_strconcat(home, "/.Xauthority", NULL);
    authstr=g_strconcat(GDM_XAUTH_PATH, " -i -f ", authfile, \
			   " add ", d->name, " . ", d->cookie, NULL);

    argv=gdm_arg_munch(authstr);
    g_free(authstr);

    switch(authpid=fork()) {
	
    case 0:
	execv(argv[0], argv);
	gdm_error(_("gdm_auth_user_add: Error starting xauth process: %s"), strerror(errno));
	return;
	
    case -1:
	gdm_error(_("gdm_auth_user_add: Error forking xauth process."));
	return;
	
    default:
	waitpid(authpid, 0,0); /* Wait for xauth to finish */
	chmod(authfile, S_IRUSR|S_IWUSR);
	g_free(authfile);
	break;

    }
}


void gdm_auth_user_remove(GdmDisplay *d, gchar *home)
{
    gchar *authstr;
    gchar **argv;
    pid_t authpid;

    gdm_debug("gdm_auth_user_remove: Removing cookie from %s", home);

    authstr=g_strconcat(GDM_XAUTH_PATH, " -i -f ", home, "/.Xauthority", \
			   " remove ", d->name, NULL);
    argv=gdm_arg_munch(authstr);
    g_free(authstr);

    switch(authpid=fork()) {
	
    case 0:
	execv(argv[0], argv);
	gdm_error(_("gdm_auth_user_remove: Error starting xauth process: %s"), strerror(errno));
	return;
	
    case -1:
	gdm_error(_("gdm_auth_user_remove: Error forking xauth process."));
	return;
	
    default:
	waitpid(authpid, 0, 0); /* Wait for xauth to finish */
	break;

    }
}

/* EOF */
