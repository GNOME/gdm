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

#ifdef HAVE_CRYPT
  #include <crypt.h>
#endif /* HAVE_CRYPT */

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gboolean GdmVerboseAuth;
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;


/**
 * gdm_verify_user:
 * @username: Name of user or NULL if we should ask
 * @display: Name of display to register with the authentication system
 * @local: boolean if local
 *
 * Provides a communication layer between the operating system's
 * authentication functions and the gdmgreeter. 
 *
 * Returns the user's login on success and NULL on failure.
 */

gchar *
gdm_verify_user (const char *username,
		 const gchar *display,
		 gboolean local) 
{
    gchar *login, *passwd, *ppasswd;
    struct passwd *pwent;

    if (local)
	    gdm_slave_greeter_ctl_no_ret (GDM_STARTTIMER, "");

    /* Ask for the user's login */
    if (username == NULL) {
	    login = gdm_slave_greeter_ctl (GDM_LOGIN, _("Login:"));
	    if (login == NULL ||
		gdm_slave_greeter_check_interruption (login)) {
		    if (local)
			    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
		    g_free (login);
		    return NULL;
	    }
    } else {
	    login = g_strdup (username);
    }

    pwent = getpwnam (login);
        
    ppasswd = (pwent == NULL) ? NULL : g_strdup (pwent->pw_passwd);
    
    /* Request the user's password */
    if (pwent != NULL &&
        ve_string_empty (ppasswd)) {
	    /* eeek a passwordless account */
	    passwd = g_strdup ("");
    } else {
	    passwd = gdm_slave_greeter_ctl (GDM_NOECHO, _("Password: "));
	    if (passwd == NULL)
		    passwd = g_strdup ("");
	    if (gdm_slave_greeter_check_interruption (passwd)) {
		    if (local)
			    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
		    g_free (login);
		    g_free (passwd);
		    g_free (ppasswd);
		    return NULL;
	    }
    }

    if (local)
	    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");

    if (pwent == NULL) {
	    gdm_error (_("Couldn't authenticate %s"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR, _("Login incorrect"));
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

    /* Check whether password is valid */
    if (ppasswd == NULL || (ppasswd[0] != '\0' &&
			    strcmp (crypt (passwd, ppasswd), ppasswd) != 0)) {
	    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR, _("Login incorrect"));
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

    if ( ( ! GdmAllowRoot ||
	  ( ! GdmAllowRemoteRoot && ! local) ) &&
	pwent->pw_uid == 0) {
	    gdm_error (_("Root login disallowed on display '%s'"), display);
	    if (GdmVerboseAuth) {
		    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR,
						  _("Root login disallowed"));
	    } else {
		    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR,
						  _("Login incorrect"));
	    }
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

    /* check for the standard method of disallowing users */
    if (pwent->pw_shell != NULL &&
	strcmp (pwent->pw_shell, "/bin/false") == 0) {
	    gdm_error (_("User %s not allowed to log in"), login);
	    if (GdmVerboseAuth) {
		    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR,
						  _("Login disabled"));
	    } else {
		    gdm_slave_greeter_ctl_no_ret (GDM_MSGERR,
						  _("Login incorrect"));
	    }
	    return NULL;
    }	

    g_free (passwd);
    g_free (ppasswd);
    
    return login;
}

/**
 * gdm_verify_setup_user:
 * @login: The name of the user
 * @display: The name of the display
 *
 * This is used for auto loging in.  This just sets up the login
 * session for this user
 */

void
gdm_verify_setup_user (const gchar *login, const gchar *display) 
{
}


/**
 * gdm_verify_cleanup:
 *
 * Unregister the user's session
 */

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

/* used in pam */
void
gdm_verify_env_setup (void)
{
}

/* EOF */
