/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <config.h>
#include <gnome.h>
#include <syslog.h>
#include <pwd.h>
#include <shadow.h>

#ifdef HAVE_CRYPT
  #include <crypt.h>
#endif /* HAVE_CRYPT */

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gboolean GdmVerboseAuth;
extern gboolean GdmAllowRoot;


/**
 * gdm_verify_user:
 * @display: Name of display to register with the authentication system
 *
 * Provides a communication layer between the operating system's
 * authentication functions and the gdmgreeter. 
 *
 * Returns the user's login on success and NULL on failure.
 */

gchar *
gdm_verify_user (const gchar *display) 
{
    gchar *login, *passwd, *ppasswd;
    struct passwd *pwent;
    struct spwd *sp;

    /* Ask for the user's login */
    login = gdm_slave_greeter_ctl (GDM_PROMPT, _("Login:"));
    pwent = getpwnam (login);
        
    ppasswd = !pwent ? NULL : pwent->pw_passwd;
    
    /* Lookup shadow password */
    sp = getspnam (login);
    
    if (sp)
	ppasswd = sp->sp_pwdp;
    
    endspent();

    /* Request the user's password */
    passwd = gdm_slave_greeter_ctl (GDM_NOECHO, _("Password:"));

    /* If verbose authentication is enabled, output messages from the
     * authentication system */
    if (GdmVerboseAuth) {

	if (!pwent) {
	    gdm_error (_("Couldn't authenticate %s"), login);
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("User unknown"));
	    return NULL;
	}
    
	if (GdmAllowRoot == 0 && pwent->pw_uid == 0) {
	    gdm_error (_("Root login disallowed on display '%s'"), display);
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("Root login disallowed"));
	    return NULL;
	}	
    }

    /* Check whether password is valid */
    if (!passwd || !ppasswd || strcmp (crypt (passwd, ppasswd), ppasswd)) {

	if (GdmVerboseAuth)
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("Incorrect password"));
	
	return NULL;
    }
    
    return login;
}


/**
 * gdm_verify_cleanup:
 *
 * Unregister the user's session */

void
gdm_verify_cleanup (void)
{
}


/**
 * gdm_verify_check:
 *
 * Check that the authentication system is correctly configured.
 *
 * Aborts daemon on error 
 */

void
gdm_verify_check (void)
{
}

/* EOF */
