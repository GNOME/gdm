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
#include <sys/stat.h>
#include <syslog.h>
#include <security/pam_appl.h>
#include <pwd.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"
#include "errorgui.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmTimedLogin;
extern gchar *GdmUser;
extern gboolean GdmAllowRemoteAutoLogin;

/* Evil, but this way these things are passed to the child session */
static char *current_login = NULL;
static char *current_display = NULL;
static pam_handle_t *pamh = NULL;

static GdmDisplay *cur_gdm_disp = NULL;


/* Internal PAM conversation function. Interfaces between the PAM
 * authentication system and the actual greeter program */

static gint 
gdm_verify_pam_conv (int num_msg, const struct pam_message **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    gint replies = 0;
    gchar *s;
    struct pam_response *reply = NULL;
    
    reply = malloc (sizeof (struct pam_response) * num_msg);
    
    if (!reply) 
	return PAM_CONV_ERR;

    memset (reply, 0, sizeof (struct pam_response) * num_msg);
    
    for (replies = 0; replies < num_msg; replies++) {
	
	switch (msg[replies]->msg_style) {
	    
	case PAM_PROMPT_ECHO_ON:
	    /* PAM requested textual input with echo on */
	    s = gdm_slave_greeter_ctl (GDM_PROMPT, _((gchar *) msg[replies]->msg));
	    if (gdm_slave_greeter_check_interruption (s)) {
		    g_free (s);
		    free (reply);
		    return PAM_CONV_ERR;
	    }
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (ve_sure_string (s));
	    g_free (s);
	    break;
	    
	case PAM_PROMPT_ECHO_OFF:
	    /* PAM requested textual input with echo off */
	    s = gdm_slave_greeter_ctl (GDM_NOECHO, _((gchar *) msg[replies]->msg));
	    if (gdm_slave_greeter_check_interruption (s)) {
		    g_free (s);
		    free (reply);
		    return PAM_CONV_ERR;
	    }
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (ve_sure_string (s));
	    g_free (s);
	    break;
	    
	case PAM_ERROR_MSG:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG, _((gchar *) msg[replies]->msg));
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	case PAM_TEXT_INFO:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, _((gchar *) msg[replies]->msg));
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	    
	default:
	    /* PAM has been smoking serious crack */
	    free (reply);
	    return PAM_CONV_ERR;
	}
	
    }

    *resp = reply;
    return PAM_SUCCESS;
}


static struct pam_conv pamc = {
    &gdm_verify_pam_conv,
    NULL
};

static gint 
gdm_verify_standalone_pam_conv (int num_msg, const struct pam_message **msg,
				struct pam_response **resp,
				void *appdata_ptr)
{
	int replies = 0;
	char *s;
	struct pam_response *reply = NULL;

	reply = malloc (sizeof (struct pam_response) * num_msg);

	if (!reply) 
		return PAM_CONV_ERR;

	memset (reply, 0, sizeof (struct pam_response) * num_msg);

	for (replies = 0; replies < num_msg; replies++) {

		switch (msg[replies]->msg_style) {

		case PAM_PROMPT_ECHO_ON:
			/* PAM requested textual input with echo on */
			s = gdm_failsafe_question (cur_gdm_disp,
						   _((gchar *) msg[replies]->msg),
						   TRUE /* echo */);
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = strdup (ve_sure_string (s));
			g_free (s);
			break;

		case PAM_PROMPT_ECHO_OFF:
			/* PAM requested textual input with echo off */
			s = gdm_failsafe_question (cur_gdm_disp,
						   _((gchar *) msg[replies]->msg),
						   FALSE /* echo */);
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = strdup (ve_sure_string (s));
			g_free (s);
			break;

		case PAM_ERROR_MSG:
			/* PAM sent a message that should displayed to the user */
			gdm_error_box (cur_gdm_disp,
				       GNOME_MESSAGE_BOX_ERROR,
				       _((gchar *) msg[replies]->msg));
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = NULL;
			break;

		case PAM_TEXT_INFO:
			/* PAM sent a message that should displayed to the user */
			gdm_error_box (cur_gdm_disp,
				       GNOME_MESSAGE_BOX_INFO,
				       _((gchar *) msg[replies]->msg));
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = NULL;
			break;

		default:
			/* PAM has been smoking serious crack */
			free (reply);
			return PAM_CONV_ERR;
		}

	}

	*resp = reply;
	return PAM_SUCCESS;
}

static struct pam_conv standalone_pamc = {
    &gdm_verify_standalone_pam_conv,
    NULL
};


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
    gint pamerr;
    gchar *login;
    struct passwd *pwent;
    gboolean error_msg_given = FALSE;
    gboolean started_timer = FALSE;
    gchar *auth_errmsg;

    if (pamh != NULL)
	    pam_end (pamh, PAM_SUCCESS);
    pamh = NULL;

    /* start the timer for timed logins */
    if (local ||
	(!ve_string_empty(GdmTimedLogin) && GdmAllowRemoteAutoLogin)) {
	    gdm_slave_greeter_ctl_no_ret (GDM_STARTTIMER, "");
	    started_timer = TRUE;
    }

    if (username == NULL) {
	    /* Ask gdmgreeter for the user's login. Just for good measure */
	    login = gdm_slave_greeter_ctl (GDM_LOGIN, _("Username:"));
	    if (login == NULL ||
		gdm_slave_greeter_check_interruption (login)) {
		    if (started_timer)
			    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
		    g_free (login);
		    return NULL;
	    }
    } else {
	    login = g_strdup (username);
    }

    cur_gdm_disp = d;
	    
    /* Initialize a PAM session for the user */
    if ((pamerr = pam_start ("gdm", login, &pamc, &pamh)) != PAM_SUCCESS) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Can't find /etc/pam.d/gdm!"));
	    goto pamerr;
    }
    
    /* Inform PAM of the user's tty */
    if ((pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Can't set PAM_TTY=%s"), display);
	    goto pamerr;
    }

    /* gdm is requesting the login */
    if ((pamerr = pam_set_item (pamh, PAM_RUSER, GdmUser)) != PAM_SUCCESS) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Can't set PAM_RUSER=%s"), GdmUser);
	    goto pamerr;
    }

    /* From the host of the display */
    if ((pamerr = pam_set_item (pamh, PAM_RHOST,
				d->console ? "localhost" : d->hostname)) != PAM_SUCCESS) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Can't set PAM_RHOST=%s"),
			       d->console ? "localhost" : d->hostname);
	    goto pamerr;
    }

    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't authenticate user"));
	    goto pamerr;
    }

    /* stop the timer for timed logins */
    if (started_timer)
	    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
    
    pwent = getpwnam (login);
    if ( ( ! GdmAllowRoot ||
	  ( ! GdmAllowRemoteRoot && ! local) ) &&
	pwent != NULL &&
	pwent->pw_uid == 0) {
	    gdm_error (_("Root login disallowed on display '%s'"),
		       display);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nThe system administrator"
					    " is not allowed to login "
					    "from this screen"));
	    /*gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG,
	      _("Root login disallowed"));*/
	    error_msg_given = TRUE;
	    goto pamerr;
    }

    /* check for the standard method of disallowing users */
    if (pwent != NULL &&
	pwent->pw_shell != NULL &&
	(strcmp (pwent->pw_shell, "/sbin/nologin") == 0 ||
	 strcmp (pwent->pw_shell, "/bin/true") == 0 ||
	 strcmp (pwent->pw_shell, "/bin/false") == 0)) {
	    gdm_error (_("User %s not allowed to log in"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nThe system administrator"
					    " has disabled your "
					    "account."));
	    /*gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG,
	      _("Login disabled"));*/
	    error_msg_given = TRUE;
	    goto pamerr;
    }	

    /* If the user's password has expired, ask for a new one */
    pamerr = pam_acct_mgmt (pamh, 0);

    if (pamerr == PAM_NEW_AUTHTOK_REQD)
	pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK); 

    if (pamerr != PAM_SUCCESS) {
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	    goto pamerr;
    }

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (current_login);
    current_login = g_strdup (login);
    g_free (current_display);
    current_display = g_strdup (display);

    cur_gdm_disp = NULL;
    
    return login;
    
 pamerr:
    
    /* The verbose authentication is turned on, output the error
     * message from the PAM subsystem */
    if ( ! error_msg_given &&
	gdm_slave_should_complain ()) {
	    /* I'm not sure yet if I should display this message for any other issues - heeten */
	    if (pamerr == PAM_AUTH_ERR) {
		    /* FIXME: Hmm, how are we sure that the login is username
		     * and password.  That is the most common case but not
		     * neccessairly true, this message needs to be changed
		     * to allow for such cases */
		    auth_errmsg = g_strdup_printf
			    (_("\nIncorrect username or password.  "
			       "Letters must be typed in the correct case.  "  
			       "Please be sure the Caps Lock key is not enabled"));
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, auth_errmsg);
		    g_free (auth_errmsg);
	    } else {
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG, _("Authentication failed"));
	    }

	    /*sleep (3);
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, _("Please enter your username"));*/
    }

    if (pamh != NULL)
	    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (current_login);
    current_login = NULL;
    g_free (current_display);
    current_display = NULL;

    g_free (login);
    
    cur_gdm_disp = NULL;

    return NULL;
}

/* Ensures a pamh existance */
static gboolean
ensure_pamh (GdmDisplay *d,
	     const char *login,
	     const char *display,
	     int *pamerr)
{
	if (login == NULL ||
	    display == NULL) {
		gdm_error (_("Cannot setup pam handle with null login "
			     "and/or display"));
		return FALSE;
	}

	g_assert (pamerr != NULL);

	if (pamh != NULL) {
		const char *item;

		/* Make sure this is the right pamh */
		if (pam_get_item (pamh, PAM_USER, (const void **)&item)
		    != PAM_SUCCESS)
			goto ensure_create;

		if (item == NULL ||
		    strcmp (item, login) != 0)
			goto ensure_create;

		if (pam_get_item (pamh, PAM_TTY, (const void **)&item)
		    != PAM_SUCCESS)
			goto ensure_create;

		if (item == NULL ||
		    strcmp (item, display) != 0)
			goto ensure_create;

		/* ensure some parameters */
		if (pam_set_item (pamh, PAM_CONV, &standalone_pamc)
		    != PAM_SUCCESS) {
			goto ensure_create;
		}
		if (pam_set_item (pamh, PAM_RUSER, GdmUser)
		    != PAM_SUCCESS) {
			goto ensure_create;
		}
		if (d->console) {
			if (pam_set_item (pamh, PAM_RHOST, "localhost")
			    != PAM_SUCCESS) {
				goto ensure_create;
			}
		} else {
			if (pam_set_item (pamh, PAM_RHOST, d->hostname)
			    != PAM_SUCCESS) {
				goto ensure_create;
			}
		}
		return TRUE;
	}

ensure_create:
	if (pamh != NULL)
		pam_end (pamh, PAM_SUCCESS);
	pamh = NULL;

	/* Initialize a PAM session for the user */
	if ((*pamerr = pam_start ("gdm", login, &standalone_pamc, &pamh)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't find /etc/pam.d/gdm!"));
		return FALSE;
	}

	/* Inform PAM of the user's tty */
	if ((*pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_TTY=%s"), display);
		return FALSE;
	}

	/* gdm is requesting the login */
	if ((*pamerr = pam_set_item (pamh, PAM_RUSER, GdmUser)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_RUSER=%s"), GdmUser);
		return FALSE;
	}

	/* From the host of the display */
	if ((*pamerr = pam_set_item (pamh, PAM_RHOST,
				     d->console ? "localhost" : d->hostname)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_RHOST=%s"),
				   d->console ? "localhost" : d->hostname);
		return FALSE;
	}

	return TRUE;
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
gdm_verify_setup_user (GdmDisplay *d, const gchar *login, const gchar *display) 
{
    gint pamerr;

    if (!login)
	return;

    cur_gdm_disp = d;

    /* Initialize a PAM session for the user */
    if ( ! ensure_pamh (d, login, display, &pamerr)) {
	    goto setup_pamerr;
    }

    /* If the user's password has expired, ask for a new one */
    /* This is for automatic logins, we shouldn't bother the user
     * though I'm unsure */
#if 0
    pamerr = pam_acct_mgmt (pamh, 0);

    if (pamerr == PAM_NEW_AUTHTOK_REQD)
	pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK); 

    if (pamerr != PAM_SUCCESS) {
	gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto setup_pamerr;
    }
#endif

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (current_login);
    current_login = g_strdup (login);
    g_free (current_display);
    current_display = g_strdup (display);
    
    cur_gdm_disp = NULL;
    
    return;
    
 setup_pamerr:
    
    if (pamh != NULL)
	    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (current_login);
    current_login = NULL;
    g_free (current_display);
    current_display = NULL;

    cur_gdm_disp = NULL;
}


/**
 * gdm_verify_cleanup:
 *
 * Unregister the user's session
 */

void 
gdm_verify_cleanup (GdmDisplay *d)
{
	gint pamerr;

	if (current_login == NULL)
		return;

	cur_gdm_disp = d;

	/* Initialize a PAM session for the user */
	if ( ! ensure_pamh (d, current_login, current_display, &pamerr)) {
		goto cleanup_pamerr;
	}

	/* FIXME: theoretically this closes even sessions that
	 * don't exist, which I suppose is OK */
	pam_close_session (pamh, 0);

cleanup_pamerr:
	if (pamh != NULL)
		pam_end (pamh, pamerr);
	pamh = NULL;

	/* Workaround to avoid gdm messages being logged as PAM_pwdb */
	closelog ();
	openlog ("gdm", LOG_PID, LOG_DAEMON);

	g_free (current_login);
	current_login = NULL;
	g_free (current_display);
	current_display = NULL;

	cur_gdm_disp = NULL;
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
    struct stat statbuf;

    if (stat ("/etc/pam.d/gdm", &statbuf) && stat ("/etc/pam.conf", &statbuf))
	gdm_fail (_("gdm_verify_check: Can't find PAM configuration file for gdm"));
}

/* used in pam */
gboolean
gdm_verify_open_session (GdmDisplay *d)
{
	gchar **pamenv;
	gint pamerr;

	if (current_login == NULL)
		return FALSE;

	cur_gdm_disp = d;

	/* Initialize a PAM session for the user */
	if ( ! ensure_pamh (d, current_login, current_display, &pamerr)) {
		goto open_pamerr;
	}

	/* Register the session */
	if ((pamerr = pam_open_session (pamh, 0)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Couldn't open session for %s"),
				   current_login);
		goto open_pamerr;
	}

	/* Set credentials */
	if ((pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Couldn't set credentials for %s"),
				   current_login);
		goto open_pamerr;
	}

	/* Migrate any PAM env. variables to the user's environment */
	/* This leaks, oh well */
	if ((pamenv = pam_getenvlist (pamh))) {
		gint i;

		for (i = 0 ; pamenv[i] ; i++) {
			putenv (g_strdup (pamenv[i]));
		}
	}

open_pamerr:
	if (pamerr != PAM_SUCCESS &&
	    pamh != NULL) {
		pam_end (pamh, pamerr);
		pamh = NULL;
	}

	/* Workaround to avoid gdm messages being logged as PAM_pwdb */
	closelog ();
	openlog ("gdm", LOG_PID, LOG_DAEMON);

	cur_gdm_disp = NULL;

	return (pamerr == PAM_SUCCESS);
}

/* EOF */
