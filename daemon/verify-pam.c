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
#include <libgnome/libgnome.h>
#include <gtk/gtkmessagedialog.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <security/pam_appl.h>
#include <pwd.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"
#include "errorgui.h"

/* Configuration option variables */
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmTimedLogin;
extern gchar *GdmUser;
extern gboolean GdmAllowRemoteAutoLogin;
extern gint GdmRetryDelay;

/* Evil, but this way these things are passed to the child session */
static pam_handle_t *pamh = NULL;

static GdmDisplay *cur_gdm_disp = NULL;

static gboolean opened_session = FALSE;
static gboolean did_setcred = FALSE;

/* Internal PAM conversation function. Interfaces between the PAM
 * authentication system and the actual greeter program */

static gint 
gdm_verify_pam_conv (int num_msg, const struct pam_message **msg,
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
	    s = gdm_slave_greeter_ctl (GDM_PROMPT, msg[replies]->msg);

	    if (gdm_slave_greeter_check_interruption ()) {
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
	    if (strcmp (msg[replies]->msg, "Password: ") == 0)
		    /* hack to make sure we translate "Password: " */
		    s = gdm_slave_greeter_ctl (GDM_NOECHO, _("Password: "));
	    else
		    s = gdm_slave_greeter_ctl (GDM_NOECHO, msg[replies]->msg);
	    if (gdm_slave_greeter_check_interruption ()) {
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
	    gdm_slave_greeter_ctl (GDM_ERRDLG, msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	case PAM_TEXT_INFO:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl (GDM_MSG, msg[replies]->msg);
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

/* Extra message to give on queries */
static char *extra_standalone_message = NULL;

static gint 
gdm_verify_standalone_pam_conv (int num_msg, const struct pam_message **msg,
				struct pam_response **resp,
				void *appdata_ptr)
{
	int replies = 0;
	char *s, *text;
	struct pam_response *reply = NULL;

	reply = malloc (sizeof (struct pam_response) * num_msg);

	if (!reply) 
		return PAM_CONV_ERR;

	memset (reply, 0, sizeof (struct pam_response) * num_msg);

	for (replies = 0; replies < num_msg; replies++) {
		switch (msg[replies]->msg_style) {

		case PAM_PROMPT_ECHO_ON:
			if (extra_standalone_message != NULL)
				text = g_strdup_printf
					("%s\n%s", extra_standalone_message,
					 msg[replies]->msg);
			else
				text = g_strdup (msg[replies]->msg);

			/* PAM requested textual input with echo on */
			s = gdm_failsafe_question (cur_gdm_disp, text,
						   TRUE /* echo */);
			g_free (text);

			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = strdup (ve_sure_string (s));
			g_free (s);
			break;

		case PAM_PROMPT_ECHO_OFF:
			if (extra_standalone_message != NULL)
				text = g_strdup_printf
					("%s\n%s", extra_standalone_message,
					 msg[replies]->msg);
			else
				text = g_strdup (msg[replies]->msg);

			/* PAM requested textual input with echo off */
			s = gdm_failsafe_question (cur_gdm_disp, text,
						   FALSE /* echo */);

			g_free (text);

			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = strdup (ve_sure_string (s));
			g_free (s);
			break;

		case PAM_ERROR_MSG:
			/* PAM sent a message that should displayed to the user */
			gdm_error_box (cur_gdm_disp,
				       GTK_MESSAGE_ERROR,
				       msg[replies]->msg);
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = NULL;
			break;

		case PAM_TEXT_INFO:
			/* PAM sent a message that should displayed to the user */
			gdm_error_box (cur_gdm_disp,
				       GTK_MESSAGE_INFO,
				       msg[replies]->msg);
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

/* Creates a pam handle for the auto login */
static gboolean
create_pamh (GdmDisplay *d,
	     const char *service,
	     const char *login,
	     struct pam_conv *conv,
	     const char *display,
	     int *pamerr)
{
	if (login == NULL ||
	    display == NULL) {
		gdm_error (_("Cannot setup pam handle with null login "
			     "and/or display"));
		return FALSE;
	}

	if (pamh != NULL) {
		gdm_error ("create_pamh: Stale pamh around, cleaning up");
		pam_end (pamh, PAM_SUCCESS);
	}
	pamh = NULL;
	opened_session = FALSE;
	did_setcred = FALSE;

	/* Initialize a PAM session for the user */
	if ((*pamerr = pam_start (service, login, conv, &pamh)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't find /etc/pam.d/%s!"), service);
		return FALSE;
	}

	/* Inform PAM of the user's tty */
	if ((*pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_TTY=%s"), display);
		return FALSE;
	}

	if ( ! d->console) {
		/* Only set RHOST if host is remote */
		/* From the host of the display */
		if ((*pamerr = pam_set_item (pamh, PAM_RHOST,
					     d->hostname)) != PAM_SUCCESS) {
			if (gdm_slave_should_complain ())
				gdm_error (_("Can't set PAM_RHOST=%s"),
					   d->hostname);
			return FALSE;
		}
	}

	return TRUE;
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
    gint pamerr = 0;
    char *login;
    const char *service = "gdm";
    struct passwd *pwent;
    gboolean error_msg_given = FALSE;
    gboolean credentials_set = FALSE;
    gboolean started_timer = FALSE;
    char *auth_errmsg;

    /* start the timer for timed logins */
    if ( ! ve_string_empty (GdmTimedLogin) &&
	(local || GdmAllowRemoteAutoLogin)) {
	    gdm_slave_greeter_ctl_no_ret (GDM_STARTTIMER, "");
	    started_timer = TRUE;
    }

    if (username == NULL) {
	    /* Ask gdmgreeter for the user's login. Just for good measure */
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, _("Please enter your username"));
	    login = gdm_slave_greeter_ctl (GDM_LOGIN, _("Username:"));
	    if (login == NULL ||
		gdm_slave_greeter_check_interruption ()) {
		    if (started_timer)
			    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
		    g_free (login);
		    return NULL;
	    }
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, "");
    } else {
	    login = g_strdup (username);
    }

    if (local &&
	gdm_is_a_no_password_user (login)) {
	    service = "gdm-autologin";
    } else {
	    service = "gdm";
    }

    cur_gdm_disp = d;

    /* Initialize a PAM session for the user */
    if ( ! create_pamh (d, service, login, &pamc, display, &pamerr)) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    goto pamerr;
    }
	    
#ifdef PAM_FAIL_DELAY
    pam_fail_delay (pamh, GdmRetryDelay * 1000);
#endif /* PAM_FAIL_DELAY */

    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
#ifndef PAM_FAIL_DELAY
	    gdm_sleep_no_signal (GdmRetryDelay);
#endif /* PAM_FAIL_DELAY */
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
					  _("\nThe system administrator "
					    "is not allowed to login "
					    "from this screen"));
	    /*gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG,
	      _("Root login disallowed"));*/
	    error_msg_given = TRUE;
	    goto pamerr;
    }

    /* Check if the user's account is healthy. */
    pamerr = pam_acct_mgmt (pamh, 0);
    switch (pamerr) {
    case PAM_SUCCESS :
	break;
    case PAM_NEW_AUTHTOK_REQD :
	if ((pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK)) != PAM_SUCCESS) {
	    gdm_error (_("Authentication token change failed for user %s"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, 
		    _("\nThe change of the authentication token failed. "
		      "Please try again later or contact the system administrator."));
	    error_msg_given = TRUE;
	    goto pamerr;
	}
        break;
    case PAM_ACCT_EXPIRED :
	gdm_error (_("User %s no longer permitted to access the system"), login);
	gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, 
		_("\nThe system administrator has disabled your account."));
	error_msg_given = TRUE;
	goto pamerr;
    case PAM_PERM_DENIED :
	gdm_error (_("User %s not permitted to gain access at this time"), login);
	gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, 
		_("\nThe system administrator has disabled access to the system temporarily."));
	error_msg_given = TRUE;
	goto pamerr;
    default :
	if (gdm_slave_should_complain ())
	    gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto pamerr;
    }

    pwent = getpwnam (login);
    if (/* paranoia */ pwent == NULL ||
       	! gdm_setup_gids (login, pwent->pw_gid)) {
	    gdm_error (_("Cannot set user group for %s"), login);
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX,
					  _("\nCannot set your user group, "
					    "you will not be able to log in, "
					    "please contact your system administrator."));
	    goto pamerr;
    }

    /* Set credentials */
    did_setcred = TRUE;
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
        did_setcred = FALSE;
	if (gdm_slave_should_complain ())
	    gdm_error (_("Couldn't set credentials for %s"), login);
	goto pamerr;
    }

    credentials_set = TRUE;

    /* Register the session */
    opened_session = TRUE;
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
	    opened_session = FALSE;
	    /* we handle this above */
	    did_setcred = FALSE;
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't open session for %s"), login);
	    goto pamerr;
    }

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    cur_gdm_disp = NULL;
    
    return login;
    
 pamerr:
    
    /* The verbose authentication is turned on, output the error
     * message from the PAM subsystem */
    if ( ! error_msg_given &&
	gdm_slave_should_complain ()) {
	    /* I'm not sure yet if I should display this message for any other issues - heeten */
	    if (pamerr == PAM_AUTH_ERR ||
		pamerr == PAM_USER_UNKNOWN) {
		    /* FIXME: Hmm, how are we sure that the login is username
		     * and password.  That is the most common case but not
		     * necessarily true, this message needs to be changed
		     * to allow for such cases */
		    auth_errmsg = 
			    _("\nIncorrect username or password. "
			      "Letters must be typed in the correct case. "  
			      "Please make sure the Caps Lock key is not enabled.");
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, auth_errmsg);
	    } else {
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG, _("Authentication failed"));
	    }
    }

    did_setcred = FALSE;
    opened_session = FALSE;

    if (pamh != NULL) {
	    /* Throw away the credentials */
	    if (credentials_set)
		    pam_setcred (pamh, PAM_DELETE_CRED);
	    pam_end (pamh, pamerr);
    }
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (login);
    
    cur_gdm_disp = NULL;

    return NULL;
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
gdm_verify_setup_user (GdmDisplay *d, const gchar *login, const gchar *display) 
{
    gint pamerr = 0;
    struct passwd *pwent;

    if (login == NULL)
	    return FALSE;

    cur_gdm_disp = d;

    g_free (extra_standalone_message);
    extra_standalone_message = g_strdup_printf ("%s (%s)",
						_("Automatic login"),
						login);

    /* Initialize a PAM session for the user */
    if ( ! create_pamh (d, "gdm-autologin", login, &standalone_pamc,
			display, &pamerr)) {
	    goto setup_pamerr;
    }

    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
	    if (gdm_slave_should_complain ()) {
		    gdm_error (_("Couldn't authenticate user"));
		    gdm_error_box (cur_gdm_disp,
				   GTK_MESSAGE_ERROR,
				   _("Authentication failed"));
	    }
	    goto setup_pamerr;
    }

    /* Check if the user's account is healthy. */
    pamerr = pam_acct_mgmt (pamh, 0);
    switch (pamerr) {
    case PAM_SUCCESS :
	break;
    case PAM_NEW_AUTHTOK_REQD :
	/* XXX: this is for automatic and timed logins,
	 * we shouldn't be asking for new pw since we never
	 * authenticated the user.  I suppose just ignoring
	 * this would be OK */
	/*
	if ((pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK)) != PAM_SUCCESS) {
	    gdm_error (_("Authentication token change failed for user %s"), login);
	    gdm_error_box (cur_gdm_disp,
			   GTK_MESSAGE_ERROR,
		    _("\nThe change of the authentication token failed. "
		      "Please try again later or contact the system administrator."));
	    goto setup_pamerr;
	}
	*/
        break;
    case PAM_ACCT_EXPIRED :
	gdm_error (_("User %s no longer permitted to access the system"), login);
	gdm_error_box (cur_gdm_disp,
		       GTK_MESSAGE_ERROR,
		       _("\nThe system administrator has disabled your account."));
	goto setup_pamerr;
    case PAM_PERM_DENIED :
	gdm_error (_("User %s not permitted to gain access at this time"), login);
	gdm_error_box (cur_gdm_disp,
		       GTK_MESSAGE_ERROR,
		       _("\nThe system administrator has disabled your access to the system temporary."));
	goto setup_pamerr;
    default :
	if (gdm_slave_should_complain ())
	    gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto setup_pamerr;
    }

    pwent = getpwnam (login);
    if (/* paranoia */ pwent == NULL ||
       	! gdm_setup_gids (login, pwent->pw_gid)) {
	    gdm_error (_("Cannot set user group for %s"), login);
	    gdm_error_box (cur_gdm_disp,
			   GTK_MESSAGE_ERROR,
			   _("\nCannot set your user group, "
			     "you will not be able to log in, "
			     "please contact your system administrator."));
	    goto setup_pamerr;
    }

    /* Set credentials */
    did_setcred = TRUE;
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
	    did_setcred = FALSE;
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't set credentials for %s"), login);
	    goto setup_pamerr;
    }

    /* Register the session */
    opened_session = TRUE;
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
	    opened_session = FALSE;
	    did_setcred = FALSE;
	    /* Throw away the credentials */
	    pam_setcred (pamh, PAM_DELETE_CRED);

	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't open session for %s"), login);
	    goto setup_pamerr;
    }

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    cur_gdm_disp = NULL;

    g_free (extra_standalone_message);
    extra_standalone_message = NULL;
    
    return TRUE;
    
 setup_pamerr:

    did_setcred = FALSE;
    opened_session = FALSE;
    
    if (pamh != NULL)
	    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    cur_gdm_disp = NULL;

    g_free (extra_standalone_message);
    extra_standalone_message = NULL;

    return FALSE;
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
	cur_gdm_disp = d;

	if (pamh != NULL) {
		gint pamerr;
		pam_handle_t *tmp_pamh;
		gboolean old_opened_session;
		gboolean old_did_setcred;

		gdm_debug ("Running gdm_verify_cleanup and pamh != NULL");

		gdm_sigterm_block_push ();
		gdm_sigchld_block_push ();
		tmp_pamh = pamh;
		pamh = NULL;
		old_opened_session = opened_session;
		opened_session = FALSE;
		old_did_setcred = did_setcred;
		did_setcred = FALSE;
		gdm_sigchld_block_pop ();
		gdm_sigterm_block_pop ();

		pamerr = PAM_SUCCESS;

		/* Close the users session */
		if (old_opened_session) {
			gdm_debug ("Running pam_close_session");
			pamerr = pam_close_session (tmp_pamh, 0);
		}

		/* Throw away the credentials */
		if (old_did_setcred) {
			gdm_debug ("Running pam_setcred with PAM_DELETE_CRED");
			pamerr = pam_setcred (tmp_pamh, PAM_DELETE_CRED);
		}

		pam_end (tmp_pamh, pamerr);

		/* Workaround to avoid gdm messages being logged as PAM_pwdb */
		closelog ();
		openlog ("gdm", LOG_PID, LOG_DAEMON);
	}

	gdm_reset_limits ();

	/* Clear the group setup */
	setgid (0);
	/* this will get rid of any suplementary groups etc... */
	setgroups (1, groups);

	cur_gdm_disp = NULL;
}


/**
 * gdm_verify_check:
 *
 * Check that the authentication system is correctly configured.
 * Not very smart, perhaps we should just whack this.
 *
 * Aborts daemon on error 
 */

void
gdm_verify_check (void)
{
	pam_handle_t *ph = NULL;

	if (pam_start ("gdm", NULL, &standalone_pamc, &ph) != PAM_SUCCESS) {
		closelog ();
		openlog ("gdm", LOG_PID, LOG_DAEMON);

		gdm_text_message_dialog
			(_("Can't find PAM configuration for gdm."));
		gdm_fail ("gdm_verify_check: %s",
			  _("Can't find PAM configuration for gdm."));
	}

	if (ph != NULL)
		pam_end (ph, PAM_SUCCESS);

	closelog ();
	openlog ("gdm", LOG_PID, LOG_DAEMON);
}

/* used in pam */
gboolean
gdm_verify_setup_env (GdmDisplay *d)
{
	gchar **pamenv;

	if (pamh == NULL)
		return FALSE;

	/* Migrate any PAM env. variables to the user's environment */
	/* This leaks, oh well */
	if ((pamenv = pam_getenvlist (pamh))) {
		gint i;

		for (i = 0 ; pamenv[i] ; i++) {
			putenv (g_strdup (pamenv[i]));
		}
	}

	return TRUE;
}

/* EOF */
