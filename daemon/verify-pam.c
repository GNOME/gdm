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

#ifdef HAVE_DEFOPEN
#include <deflt.h>
#endif

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

/* this is a hack */
static char *tmp_PAM_USER = NULL;

/* this is another hack */
static gboolean did_we_ask_for_password = FALSE;

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

static const char *
perhaps_translate_message (const char *msg)
{
	char *s;
	const char *ret;
	static GHashTable *hash = NULL;
	if (hash == NULL) {
		/* Here we come with some fairly standard messages so that
		   we have as much as possible translated.  Should really be
		   translated in pam I suppose */
		hash = g_hash_table_new (g_str_hash, g_str_equal);
		/* login: is whacked always translate to Username: */
		g_hash_table_insert (hash, "login:", _("Username:"));
		g_hash_table_insert (hash, "Username:", _("Username:"));
		g_hash_table_insert (hash, "Password:", _("Password:"));
		g_hash_table_insert (hash, "You are required to change your password immediately (password aged)", _("You are required to change your password immediately (password aged)"));
		g_hash_table_insert (hash, "You are required to change your password immediately (root enforced)", _("You are required to change your password immediately (root enforced)"));
		g_hash_table_insert (hash, "Your account has expired; please contact your system administrator", _("Your account has expired; please contact your system administrator"));
		g_hash_table_insert (hash, "No password supplied", _("No password supplied"));
		g_hash_table_insert (hash, "Password unchanged", _("Password unchanged"));
		g_hash_table_insert (hash, "Can not get username", _("Can not get username"));
		g_hash_table_insert (hash, "Retype new UNIX password:", _("Retype new UNIX password:"));
		g_hash_table_insert (hash, "Enter new UNIX password:", _("Enter new UNIX password:"));
		g_hash_table_insert (hash, "(current) UNIX password:", _("(current) UNIX password:"));
		g_hash_table_insert (hash, "Error while changing NIS password.", _("Error while changing NIS password."));
		g_hash_table_insert (hash, "You must choose a longer password", _("You must choose a longer password"));
		g_hash_table_insert (hash, "Password has been already used. Choose another.", _("Password has been already used. Choose another."));
		g_hash_table_insert (hash, "You must wait longer to change your password", _("You must wait longer to change your password"));
		g_hash_table_insert (hash, "Sorry, passwords do not match", _("Sorry, passwords do not match"));
	}
	s = g_strstrip (g_strdup (msg));
	ret = g_hash_table_lookup (hash, s);
	g_free (s);
	if (ret != NULL)
		return ret;
	else
		return msg;
}

/* Internal PAM conversation function. Interfaces between the PAM
 * authentication system and the actual greeter program */

static gint 
gdm_verify_pam_conv (int num_msg, const struct pam_message **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    int replies = 0;
    int i;
    char *s;
    struct pam_response *reply = NULL;
    const char *login;
    
    reply = malloc (sizeof (struct pam_response) * num_msg);
    
    if (!reply) 
	return PAM_CONV_ERR;

    memset (reply, 0, sizeof (struct pam_response) * num_msg);

    /* Here we set the login if it wasn't already set,
     * this is kind of anal, but this way we guarantee that
     * the greeter always is up to date on the login */
    if (pam_get_item (pamh, PAM_USER, (const void **)&login) == PAM_SUCCESS ||
	tmp_PAM_USER != NULL) {
	    /* FIXME: this is a HACK HACK HACK, for some reason needed */
	    if (tmp_PAM_USER != NULL)
		    login = tmp_PAM_USER;
	    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);
    }
    
    for (replies = 0; replies < num_msg; replies++) {
	gboolean islogin = FALSE;
	const char *m = msg[replies]->msg;
	m = perhaps_translate_message (m);
	
	switch (msg[replies]->msg_style) {
	    
	/* PAM requested textual input with echo on */
	case PAM_PROMPT_ECHO_ON:
 	    if (strcmp(m, _("Username:")) == 0) {
		    /* this is an evil hack, but really there is no way we'll
		       know this is a username prompt.  However we SHOULD NOT
		       rely on this working.  The pam modules can set their
		       prompt to whatever they wish to */
		    gdm_slave_greeter_ctl_no_ret
			    (GDM_MSG, _("Please enter your username"));
		    s = gdm_slave_greeter_ctl (GDM_PROMPT, m);
		    /* this will clear the message */
		    gdm_slave_greeter_ctl_no_ret (GDM_MSG, "");
		    islogin = TRUE;
	    } else {
		    s = gdm_slave_greeter_ctl (GDM_PROMPT, m);
	    }

	    if (gdm_slave_greeter_check_interruption ()) {
		    g_free (s);
		    for (i = 0; i < replies; i++)
			    if (reply[replies].resp != NULL)
				    free (reply[replies].resp);
		    free (reply);
		    return PAM_CONV_ERR;
	    }

	    if (islogin) {
		    /* note that we should NOT rely on this for anything
		       that is REALLY needed.  The reason this is here
		       is for the face browser to work correctly.  The
		       best way would really be to ask all questions at
		       once, but that would 1) need some protocol rethinking,
		       2) the gdmgreeter program is really not equipped
		       for this, would need theme format change */
		    /* Second problem is that pam will not actually set
		       PAM_USER itself after it asks for it.  This is
		       pretty evil, evil, evil, evil, but somewhat necessary
		       then. */
		    /* FIXME: this is a HACK HACK HACK */
		    g_free (tmp_PAM_USER);
		    tmp_PAM_USER = g_strdup (s);
	    }

	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (ve_sure_string (s));
	    g_free (s);
	    break;
	    
	case PAM_PROMPT_ECHO_OFF:
	    if (strcmp (m, _("Password:")) == 0)
		    did_we_ask_for_password = TRUE;
	    /* PAM requested textual input with echo off */
	    s = gdm_slave_greeter_ctl (GDM_NOECHO, m);
	    if (gdm_slave_greeter_check_interruption ()) {
		    g_free (s);
		    for (i = 0; i < replies; i++)
			    if (reply[replies].resp != NULL)
				    free (reply[replies].resp);
		    free (reply);
		    return PAM_CONV_ERR;
	    }
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (ve_sure_string (s));
	    g_free (s);
	    break;
	    
	case PAM_ERROR_MSG:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl (GDM_ERRDLG, m);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	case PAM_TEXT_INFO:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl (GDM_MSG, m);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	    
	default:
	    /* PAM has been smoking serious crack */
	    for (i = 0; i < replies; i++)
		    if (reply[replies].resp != NULL)
			    free (reply[replies].resp);
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
	int i;
	char *s, *text;
	struct pam_response *reply = NULL;

	reply = malloc (sizeof (struct pam_response) * num_msg);

	if (!reply) 
		return PAM_CONV_ERR;

	memset (reply, 0, sizeof (struct pam_response) * num_msg);

	for (replies = 0; replies < num_msg; replies++) {
		const char *m = msg[replies]->msg;
		m = perhaps_translate_message (m);
		switch (msg[replies]->msg_style) {

		case PAM_PROMPT_ECHO_ON:
			if (extra_standalone_message != NULL)
				text = g_strdup_printf
					("%s\n%s", extra_standalone_message,
					 m);
			else
				text = g_strdup (m);

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
					 m);
			else
				text = g_strdup (m);

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
				       m);
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = NULL;
			break;

		case PAM_TEXT_INFO:
			/* PAM sent a message that should displayed to the user */
			gdm_error_box (cur_gdm_disp,
				       GTK_MESSAGE_INFO,
				       m);
			reply[replies].resp_retcode = PAM_SUCCESS;
			reply[replies].resp = NULL;
			break;

		default:
			/* PAM has been smoking serious crack */
			for (i = 0; i < replies; i++)
				if (reply[replies].resp != NULL)
					free (reply[replies].resp);
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
	if (display == NULL) {
		gdm_error (_("Cannot setup pam handle with null display"));
		return FALSE;
	}

	if (pamh != NULL) {
		gdm_error ("create_pamh: Stale pamh around, cleaning up");
		pam_end (pamh, PAM_SUCCESS);
	}
	pamh = NULL;

	/* Initialize a PAM session for the user */
	if ((*pamerr = pam_start (service, login, conv, &pamh)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Unable to establish service %s: %s\n"),
				   service, pam_strerror (NULL, *pamerr));
		return FALSE;
	}

	/* Inform PAM of the user's tty */
	if ((*pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_TTY=%s"), display);
		return FALSE;
	}

#if 0
	/* Apparently we should not set RUSER, docs are unclear but the sun
           guys are saying don't do it */
	/* gdm is requesting the login */
	if ((*pamerr = pam_set_item (pamh, PAM_RUSER, GdmUser)) != PAM_SUCCESS) {
		if (gdm_slave_should_complain ())
			gdm_error (_("Can't set PAM_RUSER=%s"), GdmUser);
		return FALSE;
	}
#endif

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
    char *login = NULL;
    struct passwd *pwent;
    gboolean error_msg_given = FALSE;
    gboolean credentials_set = FALSE;
    gboolean started_timer = FALSE;
    int null_tok = 0;

    /* start the timer for timed logins */
    if ( ! ve_string_empty (GdmTimedLogin) &&
	(local || GdmAllowRemoteAutoLogin)) {
	    gdm_slave_greeter_ctl_no_ret (GDM_STARTTIMER, "");
	    started_timer = TRUE;
    }

    if (username != NULL) {
	    login = g_strdup (username);
	    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);
    }

    cur_gdm_disp = d;

    /* Initialize a PAM session for the user */
    if ( ! create_pamh (d, "gdm", username, &pamc, display, &pamerr)) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    goto pamerr;
    }

    pam_set_item (pamh, PAM_USER_PROMPT, _("Username:"));

    /* A Solaris thing: */
#ifdef HAVE_DEFOPEN
    if (defopen(DEFLT"/login") == 0) {
	    char *passreq;
	    int def_flgs;

	    def_flgs = defcntl(DC_GETFLAGS, 0);
	    TURNOFF(def_flgs, DC_CASE);
	    (void) defcntl (DC_SETFLAGS, def_flgs);	 /* ignore case */

	    if (((passreq = defread("PASSREQ=")) != NULL) &&
		g_ascii_strcasecmp (passreq, "YES") == 0)
		    null_tok |= PAM_DISALLOW_NULL_AUTHTOK;

	    (void) defopen(NULL); /* close defaults file  */
    }
#endif
	    
#ifdef PAM_FAIL_DELAY
    pam_fail_delay (pamh, GdmRetryDelay * 1000);
#endif /* PAM_FAIL_DELAY */

authenticate_again:
    /* hack */
    g_free (tmp_PAM_USER);
    tmp_PAM_USER = NULL;

    did_we_ask_for_password = FALSE;

    gdm_verify_select_user (NULL);
    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, null_tok)) != PAM_SUCCESS) {
	    if ( ! ve_string_empty (selected_user)) {
		    /* FIXME: what about errors */
		    pam_set_item (pamh, PAM_USER, selected_user);
		    /* Note that the GDM_SETUSER will be sent in the
		       authenticate conversation.  This is a more robust
		       solution. */
		    goto authenticate_again;
	    }
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_should_complain ()) {
#ifndef PAM_FAIL_DELAY
		    sleep (GdmRetryDelay);
#endif /* PAM_FAIL_DELAY */
		    gdm_error (_("Couldn't authenticate user"));
	    }
	    goto pamerr;
    }

    /* stop the timer for timed logins */
    if (started_timer)
	    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");

    g_free (login);
    login = NULL;
    
    if ((pamerr = pam_get_item (pamh, PAM_USER, (const void **)&login))
	!= PAM_SUCCESS) {
	    login = NULL;
	    /* is not really an auth problem, but it will
	       pretty much look as such, it shouldn't really
	       happen */
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't authenticate user"));
	    goto pamerr;
    }
    login = g_strdup (login);
    /* kind of anal, the greeter likely already knows, but it could have
       been changed */
    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);

    if ( ! gdm_slave_check_user_wants_to_log_in (login)) {
	    /* cleanup stuff */
	    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, "");
	    g_free (login);
	    login = NULL;
	    gdm_slave_greeter_ctl_no_ret (GDM_RESETOK, "");

	    gdm_verify_cleanup (d);
	    /* kind of a hack */
	    return gdm_verify_user (d, username, display, local);
    }

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
    pamerr = pam_acct_mgmt (pamh, null_tok);
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
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
	if (gdm_slave_should_complain ())
	    gdm_error (_("Couldn't set credentials for %s"), login);
	goto pamerr;
    }

    credentials_set = TRUE;

    /* Register the session */
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
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
		    gboolean is_capslock = FALSE;
		    const char *basemsg;
		    char *msg;
		    char *ret;
			    
		    ret = gdm_slave_greeter_ctl (GDM_QUERY_CAPSLOCK, "");
		    if ( ! ve_string_empty (ret))
			    is_capslock = TRUE;
		    g_free (ret);

		    /* Only give this message if we actually asked for
		       password, otherwise it would be silly to say that
		       the password may have been wrong */
		    if (did_we_ask_for_password) {
			    basemsg = _("\nIncorrect username or password.  "
					"Letters must be typed in the correct "
					"case.");
		    } else {
			    basemsg = _("\nAuthentication failed.  "
					"Letters must be typed in the correct "
					"case.");
		    }
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
	    } else {
		    gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG, _("Authentication failed"));
	    }
    }

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
gdm_verify_setup_user (GdmDisplay *d, const gchar *login, const gchar *display,
		       char **new_login) 
{
    gint pamerr = 0;
    struct passwd *pwent;
    const char *after_login;
    int null_tok = 0;
    
    *new_login = NULL;

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

    /* A Solaris thing: */
#ifdef HAVE_DEFOPEN
    if (defopen(DEFLT"/login") == 0) {
	    char *passreq;
	    int def_flgs;

	    def_flgs = defcntl(DC_GETFLAGS, 0);
	    TURNOFF(def_flgs, DC_CASE);
	    (void) defcntl (DC_SETFLAGS, def_flgs);	 /* ignore case */

	    if (((passreq = defread("PASSREQ=")) != NULL) &&
		g_ascii_strcasecmp (passreq, "YES") == 0)
		    null_tok |= PAM_DISALLOW_NULL_AUTHTOK;

	    (void) defopen(NULL); /* close defaults file  */
    }
#endif

    /* Start authentication session */
    did_we_ask_for_password = FALSE;
    if ((pamerr = pam_authenticate (pamh, null_tok)) != PAM_SUCCESS) {
	    if (gdm_slave_should_complain ()) {
		    gdm_error (_("Couldn't authenticate user"));
		    gdm_error_box (cur_gdm_disp,
				   GTK_MESSAGE_ERROR,
				   _("Authentication failed"));
	    }
	    goto setup_pamerr;
    }

    if ((pamerr = pam_get_item (pamh, PAM_USER, (const void **)&after_login))
	!= PAM_SUCCESS) {
	    /* is not really an auth problem, but it will
	       pretty much look as such, it shouldn't really
	       happen */
	    gdm_error (_("Couldn't authenticate user"));
	    gdm_error_box (cur_gdm_disp,
			   GTK_MESSAGE_ERROR,
			   _("Authentication failed"));
	    goto setup_pamerr;
    }

    if (after_login != NULL /* should never be */ &&
	strcmp (after_login, login) != 0) {
	    *new_login = g_strdup (after_login);
    }

    /* Check if the user's account is healthy. */
    pamerr = pam_acct_mgmt (pamh, null_tok);
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
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
	    if (gdm_slave_should_complain ())
		    gdm_error (_("Couldn't set credentials for %s"), login);
	    goto setup_pamerr;
    }

    /* Register the session */
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
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

		/* Close the users session */
		pamerr = pam_close_session (pamh, 0);

		/* Throw away the credentials */
		pamerr = pam_setcred (pamh, PAM_DELETE_CRED);

		if (pamh != NULL)
			pam_end (pamh, pamerr);
		pamh = NULL;

		/* Workaround to avoid gdm messages being logged as PAM_pwdb */
		closelog ();
		openlog ("gdm", LOG_PID, LOG_DAEMON);
	}

	/* Clear the group setup */
	setgid (0);
	/* this will get rid of any suplementary groups etc... */
	setgroups (1, groups);

	cur_gdm_disp = NULL;

	/* reset limits */
	gdm_reset_limits ();
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
