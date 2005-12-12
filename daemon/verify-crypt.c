/* GDM - The GNOME Display Manager
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
#include <glib/gi18n.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <unistd.h>

#if defined (CAN_CLEAR_ADMCHG) && defined (HAVE_USERSEC_H)
#  include <usersec.h>
#endif /* CAN_CLEAR_ADMCHG && HAVE_USERSEC_H */

#ifdef HAVE_CRYPT
#  include <crypt.h>
#endif /* HAVE_CRYPT */

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"
#include "errorgui.h"
#include "gdmconfig.h"

static char *selected_user = NULL;

void
gdm_verify_select_user (const char *user)
{
	g_free (selected_user);
	if (ve_string_empty (user))
		selected_user = NULL;
	else
		selected_user = g_strdup (user);
}

static void
print_cant_auth_errbox (void)
{
	gboolean is_capslock = FALSE;
	const char *basemsg;
	char *msg;
	char *ret;

	ret = gdm_slave_greeter_ctl (GDM_QUERY_CAPSLOCK, "");
	if ( ! ve_string_empty (ret))
		is_capslock = TRUE;
	g_free (ret);

	basemsg = _("\nIncorrect username or password.  "
		    "Letters must be typed in the correct "
		    "case.");
	if (is_capslock) {
		msg = g_strconcat (basemsg, "  ",
				   _("Please make sure the "
				     "Caps Lock key is not "
				     "enabled."),
				   NULL);
	} else {
		msg = g_strdup (basemsg);
	}
	gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, msg);
	g_free (msg);
}

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
gdm_verify_user (GdmDisplay *d,
		 const char *username,
		 const gchar *display,
		 gboolean local) 
{
    gchar *login, *passwd, *ppasswd;
    struct passwd *pwent;
#if defined (HAVE_PASSWDEXPIRED) && defined (HAVE_CHPASS) \
    || defined (HAVE_LOGINRESTRICTIONS)
    gchar *message = NULL;
#endif
#if defined (HAVE_PASSWDEXPIRED) && defined (HAVE_CHPASS)
    gchar *info_msg = NULL, *response = NULL;
    gint reEnter, ret;
#endif

    if (local)
	    gdm_slave_greeter_ctl_no_ret (GDM_STARTTIMER, "");

    if (username == NULL) {
authenticate_again:
	    /* Ask for the user's login */
	    gdm_verify_select_user (NULL);
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, _("Please enter your username"));
	    login = gdm_slave_greeter_ctl (GDM_PROMPT, _("Username:"));
	    if (login == NULL ||
		gdm_slave_greeter_check_interruption ()) {
		    if ( ! ve_string_empty (selected_user)) {
			    /* user selected */
			    g_free (login);
			    login = selected_user;
			    selected_user = NULL;
		    } else {
			    /* some other interruption */
			    if (local)
				    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
			    g_free (login);
			    return NULL;
		    }
	    }
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, "");

	    if (gdm_get_value_bool (GDM_KEY_DISPLAY_LAST_LOGIN)) {
		    char *info = gdm_get_last_info (login);
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, info);
		    g_free (info);
	    }
    } else {
	    login = g_strdup (username);
    }
    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);

    pwent = getpwnam (login);
        
    ppasswd = (pwent == NULL) ? NULL : g_strdup (pwent->pw_passwd);
    
    /* Request the user's password */
    if (pwent != NULL &&
        ve_string_empty (ppasswd)) {
	    /* eeek a passwordless account */
	    passwd = g_strdup ("");
    } else {
	    passwd = gdm_slave_greeter_ctl (GDM_NOECHO, _("Password:"));
	    if (passwd == NULL)
		    passwd = g_strdup ("");
	    if (gdm_slave_greeter_check_interruption ()) {
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
	    gdm_sleep_no_signal (gdm_get_value_int (GDM_KEY_RETRY_DELAY));
	    gdm_error (_("Couldn't authenticate user \"%s\""), login);

	    print_cant_auth_errbox ();

	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

    /* Check whether password is valid */
    if (ppasswd == NULL || (ppasswd[0] != '\0' &&
			    strcmp (crypt (passwd, ppasswd), ppasswd) != 0)) {
	    gdm_sleep_no_signal (gdm_get_value_int (GDM_KEY_RETRY_DELAY));
	    gdm_error (_("Couldn't authenticate user \"%s\""), login);

	    print_cant_auth_errbox ();

	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

    if ( ( ! gdm_get_value_bool (GDM_KEY_ALLOW_ROOT)||
	  ( ! gdm_get_value_bool (GDM_KEY_ALLOW_REMOTE_ROOT) && ! local) ) &&
	pwent->pw_uid == 0) {
	    gdm_error (_("Root login disallowed on display '%s'"), display);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("The system administrator "
					    "is not allowed to login "
					    "from this screen"));
	    /*gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG,
	      _("Root login disallowed"));*/
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }

#ifdef HAVE_LOGINRESTRICTIONS

    /* Check with the 'loginrestrictions' function
       if the user has been disallowed */
    if (loginrestrictions (login, 0, NULL, &message) != 0) {
	    gdm_error (_("User %s not allowed to log in"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nThe system administrator "
					    "has disabled your "
					    "account."));
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    if (message != NULL)
		    free (message);
	    return NULL;
    }
    
    if (message != NULL)
	    free (message);
    message = NULL;

#else /* ! HAVE_LOGINRESTRICTIONS */

    /* check for the standard method of disallowing users */
    if (pwent->pw_shell != NULL &&
	(strcmp (pwent->pw_shell, "/sbin/nologin") == 0 ||
	 strcmp (pwent->pw_shell, "/bin/true") == 0 ||
	 strcmp (pwent->pw_shell, "/bin/false") == 0)) {
	    gdm_error (_("User %s not allowed to log in"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nThe system administrator "
					    "has disabled your "
					    "account."));
	    /*gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG,
	      _("Login disabled"));*/
	    g_free (login);
	    g_free (passwd);
	    g_free (ppasswd);
	    return NULL;
    }	

#endif /* HAVE_LOGINRESTRICTIONS */

    g_free (passwd);
    g_free (ppasswd);

    if ( ! gdm_slave_check_user_wants_to_log_in (login)) {
	    g_free (login);
	    login = NULL;
	    goto authenticate_again;
    }

    if ( ! gdm_setup_gids (login, pwent->pw_gid)) {
	    gdm_error (_("Cannot set user group for %s"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nCannot set your user group; "
					    "you will not be able to log in. "
					    "Please contact your system administrator."));
	    g_free (login);
	    return NULL;
    }
    
#if defined (HAVE_PASSWDEXPIRED) && defined (HAVE_CHPASS)

    switch (passwdexpired (login, &info_msg)) {
    case 1 :
	    gdm_error (_("Password of %s has expired"), login);
	    gdm_error_box (d, GTK_MESSAGE_ERROR,
			   _("You are required to change your password.\n"
			     "Please choose a new one."));
	    g_free (info_msg);

	    do {
		    ret = chpass (login, response, &reEnter, &message);
		    g_free (response);

		    if (ret != 1) {
			    if (ret != 0) {
				    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
								  _("\nCannot change your password; "
								    "you will not be able to log in. "
								    "Please try again later or contact "
								    "your system administrator."));
			    } else if ((reEnter != 0) && (message)) {
				    response = gdm_slave_greeter_ctl (GDM_NOECHO, message);
				    if (response == NULL)
					    response = g_strdup ("");
			    }
		    }

		    g_free (message);
		    message = NULL;

	    } while ( ((reEnter != 0) && (ret == 0))
		      || (ret ==1) );

	    g_free (response);
	    g_free (message);

	    if ((ret != 0) || (reEnter != 0)) {
		    return NULL;
	    }

#if defined (CAN_CLEAR_ADMCHG)
	    /* The password is changed by root, clear the ADM_CHG
	       flag in the passwd file */
	    ret = setpwdb (S_READ | S_WRITE);
	    if (!ret) {
		    upwd = getuserpw (login);
		    if (upwd == NULL) {
			    ret = -1;
		    } else {
			    upwd->upw_flags &= ~PW_ADMCHG;
			    ret = putuserpw (upwd);
			    if (!ret) {
				    ret = endpwdb ();
			    }
		    }
	    }

	    if (ret) {
		    gdm_error_box (d, GTK_MESSAGE_WARNING,
				   _("Your password has been changed but "
				     "you may have to change it again. "
				     "Please try again later or contact "
				     "your system administrator."));
	    }

#else /* !CAN_CLEAR_ADMCHG */
	    gdm_error_box (d, GTK_MESSAGE_WARNING,
			   _("Your password has been changed but you "
			     "may have to change it again. Please try again "
			     "later or contact your system administrator."));

#endif /* CAN_CLEAR_ADMCHG */

	    break;

    case 2 :
	    gdm_error (_("Password of %s has expired"), login);
	    gdm_error_box (d, GTK_MESSAGE_ERROR,
			   _("Your password has expired.\n"
			     "Only a system administrator can now change it"));
	    g_free (info_msg);
	    return NULL;
	    break;    

    case -1 :
	    gdm_error (_("Internal error on passwdexpired"));
	    gdm_error_box (d, GTK_MESSAGE_ERROR,
			   _("An internal error occurred. You will not be able to log in.\n"
			     "Please try again later or contact your system administrator."));
	    g_free (info_msg);
	    return NULL;
	    break;    

    default :
	    g_free (info_msg);
	    break;
    }

#endif /* HAVE_PASSWDEXPIRED && HAVE_CHPASS */

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

gboolean
gdm_verify_setup_user (GdmDisplay *d,
		       const gchar *login, const gchar *display,
		       char **new_login) 
{
	struct passwd *pwent;

	*new_login = NULL;

	pwent = getpwnam (login);
	if (pwent == NULL) {
		gdm_error (_("Cannot get passwd structure for %s"), login);
		return FALSE;
	}

	if ( ! gdm_setup_gids (login, pwent->pw_gid)) {
		gdm_error (_("Cannot set user group for %s"), login);
		gdm_error_box (d,
			       GTK_MESSAGE_ERROR,
			       _("\nCannot set your user group; "
				 "you will not be able to log in. "
				 "Please contact your system administrator."));
		return FALSE;
	}

	return TRUE;
}


/**
 * gdm_verify_cleanup:
 *
 * Unregister the user's session
 */

void
gdm_verify_cleanup (GdmDisplay *d)
{
	gid_t groups[1] = { 0 };

	/* Clear the group setup */
	setgid (0);
	/* this will get rid of any suplementary groups etc... */
	setgroups (1, groups);
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
gboolean
gdm_verify_setup_env (GdmDisplay *d)
{
	return TRUE;
}

/* EOF */
