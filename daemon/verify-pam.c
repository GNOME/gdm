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

#ifdef	HAVE_LOGINDEVPERM
#include <libdevinfo.h>
#endif	/* HAVE_LOGINDEVPERM */
#ifdef  HAVE_ADT
#include <bsm/adt.h>
#include <bsm/adt_event.h>
#endif	/* HAVE_ADT */

/* Configuration option variables */
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmTimedLogin;
extern gchar *GdmUser;
extern gboolean GdmAllowRemoteAutoLogin;
extern gint GdmRetryDelay;
extern gboolean GdmDisplayLastLogin;
extern uid_t GdmUserId;
extern gid_t GdmGroupId;

extern gboolean no_console;

/* Evil, but this way these things are passed to the child session */
static pam_handle_t *pamh = NULL;

static GdmDisplay *cur_gdm_disp = NULL;

/* this is a hack */
static char *tmp_PAM_USER = NULL;

/* this is another hack */
static gboolean did_we_ask_for_password = FALSE;

static char *selected_user = NULL;

static gboolean opened_session = FALSE;
static gboolean did_setcred = FALSE;

#ifdef	HAVE_ADT
#define	PW_FALSE	1	/* no password change */
#define PW_TRUE		2	/* successful password change */
#define PW_FAILED	3	/* failed password change */

static	adt_session_data_t      *adt_ah = NULL;    /* audit session handle */


/*
 * audit_success_login - audit successful login
 *
 *	Entry	process audit context established -- i.e., pam_setcred()
 *			called.
 *		pw_change == PW_TRUE, if successful password change audit
 *				      required.
 *		pwent = authenticated user's passwd entry.
 *
 *	Exit	ADT_login (ADT_SUCCESS) audit record written
 *		pw_change == PW_TRUE, ADT_passwd (ADT_SUCCESS) audit
 *			record written.
 *		adt_ah = audit session established for audit_logout();
 *	
 */
static void
audit_success_login(int pw_change, struct passwd *pwent)
{
	adt_event_data_t	*event;	/* event to generate */

	if (adt_start_session(&adt_ah, NULL, ADT_USE_PROC_DATA) != 0) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_start_session(ADT_login): %m");
		return;
	}
	if (adt_set_user(adt_ah, pwent->pw_uid, pwent->pw_gid,
	    pwent->pw_uid, pwent->pw_gid, NULL, ADT_USER) != 0) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_set_user(ADT_login, %s): %m", pwent->pw_name);
	}
	if ((event = adt_alloc_event(adt_ah, ADT_login)) == NULL) {

		syslog(LOG_AUTH | LOG_ALERT, "adt_alloc_event(ADT_login): %m");
	} else if (adt_put_event(event, ADT_SUCCESS, ADT_SUCCESS) != 0) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_put_event(ADT_login, ADT_SUCCESS): %m");
	}

	if (pw_change == PW_TRUE) {

		/* Also audit password change */
		adt_free_event(event);
		if ((event = adt_alloc_event(adt_ah, ADT_passwd)) == NULL) {

			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_alloc_event(ADT_passwd): %m");
		} else if (adt_put_event(event, ADT_SUCCESS,
		    ADT_SUCCESS) != 0) {

			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_put_event(ADT_passwd, ADT_SUCCESS): %m");
		}
	}
	adt_free_event(event);
}


/*
 * audit_fail_login - audit failed login
 *
 *	Entry	did_setcred == FALSE, process audit context is not established.
 *			       TRUE, process audit context established.
 *		d = display structure, d->console non 0 if local,
 *			d->hostname used if remote.
 *		pw_change == PW_FALSE, if no password change requested.
 *			     PW_TRUE, if successful password change audit
 *				      required.
 *			     PW_FAILED, if failed password change audit
 *				      required.
 *		pwent = NULL, or password entry to use.
 *		pamerr = PAM error code; reason for failure.
 *
 *	Exit	ADT_login (ADT_FAILURE) audit record written
 *		pw_change == PW_TRUE, ADT_passwd (ADT_FAILURE) audit
 *			record written.
 *	
 */
static void
audit_fail_login(GdmDisplay *d, int pw_change, struct passwd *pwent,
    int pamerr)
{
	adt_session_data_t	*ah;	/* audit session handle */
	adt_event_data_t	*event;	/* event to generate */
	adt_termid_t		*tid;	/* terminal ID for failures */

	if (did_setcred == TRUE) {
		if (adt_start_session(&ah, NULL, ADT_USE_PROC_DATA) != 0) {

			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_start_session(ADT_login, ADT_FAILURE): %m");
			return;
		}
	} else {
		if (adt_start_session(&ah, NULL, 0) != 0) {
		
			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_start_session(ADT_login, ADT_FAILURE): %m");
			return;
		}
		if (d->console) {
			/* login from the local host */
			if (adt_load_hostname(NULL, &tid) != 0) {

				syslog(LOG_AUTH | LOG_ALERT,
				    "adt_loadhostname(localhost): %m");
			}
		} else {
			/* login from a remote host */
			if (adt_load_hostname(d->hostname, &tid) != 0) {

				syslog(LOG_AUTH | LOG_ALERT,
				    "adt_loadhostname(%s): %m", d->hostname);
			}
		}
		if (adt_set_user(ah,
		    pwent ? pwent->pw_uid : ADT_NO_ATTRIB,
		    pwent ? pwent->pw_gid : ADT_NO_ATTRIB,
		    pwent ? pwent->pw_uid : ADT_NO_ATTRIB,
		    pwent ? pwent->pw_gid : ADT_NO_ATTRIB,
		    tid, ADT_NEW) != 0) {

			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_set_user(%s): %m",
			    pwent ? pwent->pw_name : "ADT_NO_ATTRIB");
		}
	}
	if ((event = adt_alloc_event(ah, ADT_login)) == NULL) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_alloc_event(ADT_login, ADT_FAILURE): %m");
		goto done;
	} else if (adt_put_event(event, ADT_FAILURE,
	    ADT_FAIL_PAM + pamerr) != 0) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_put_event(ADT_login(ADT_FAIL, %s): %m",
		    pam_strerror(pamh, pamerr));
	}
	if (pw_change != PW_FALSE) {

		/* Also audit password change */
		adt_free_event(event);
		if ((event = adt_alloc_event(ah, ADT_passwd)) == NULL) {

			syslog(LOG_AUTH | LOG_ALERT,
			    "adt_alloc_event(ADT_passwd): %m");
			goto done;
		}
		if (pw_change == PW_TRUE) {
			if (adt_put_event(event, ADT_SUCCESS,
			    ADT_SUCCESS) != 0) {

				syslog(LOG_AUTH | LOG_ALERT,
				    "adt_put_event(ADT_passwd, ADT_SUCCESS): "
				    "%m");
			}
		} else if (pw_change == PW_FAILED) {
			if (adt_put_event(event, ADT_FAILURE,
			    ADT_FAIL_PAM + pamerr) != 0) {

				syslog(LOG_AUTH | LOG_ALERT,
				    "adt_put_event(ADT_passwd, ADT_FAILURE): "
				    "%m");
			}
		}
	}
	adt_free_event(event);

done:
	/* reset process audit state. this process is being reused.*/

	if ((adt_set_user(ah, ADT_NO_AUDIT, ADT_NO_AUDIT, ADT_NO_AUDIT,
	    ADT_NO_AUDIT, NULL, ADT_NEW) != 0) ||
	    (adt_set_proc(ah) != 0)) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_put_event(ADT_login(ADT_FAILURE reset, %m)");
	}
	(void) adt_end_session(ah);
}


/*
 * audit_logout - audit user logout
 *
 *	Entry	adt_ah = audit session handle established by
 *			audit_success_login().
 *
 *	Exit	ADT_logout (ADT_SUCCESS) audit record written
 *		process audit state reset.  (this process is reused for
 *			the next login.)
 *		audit session adt_ah ended.
 */
static void
audit_logout(void)
{
	adt_event_data_t	*event;	/* event to generate */

	if (adt_ah == NULL) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_ah(ADT_logout): NULL");
		return;
	}
	if ((event = adt_alloc_event(adt_ah, ADT_logout)) == NULL) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_alloc_event(ADT_logout): %m");
	} else if (adt_put_event(event, ADT_SUCCESS, ADT_SUCCESS) != 0) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_put_event(ADT_logout, ADT_SUCCESS): %m");
	}
	adt_free_event(event);

	/* reset process audit state. this process is being reused.*/

	if ((adt_set_user(adt_ah, ADT_NO_AUDIT, ADT_NO_AUDIT, ADT_NO_AUDIT,
	    ADT_NO_AUDIT, NULL, ADT_NEW) != 0) ||
	    (adt_set_proc(adt_ah) != 0)) {

		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_set_proc(ADT_logout reset): %m");
	}
	(void) adt_end_session(adt_ah);
}
#endif	/* HAVE_ADT */

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
		   translated in pam I suppose.  This way we can "change"
		   some of these messages to be more sane. */
		hash = g_hash_table_new (g_str_hash, g_str_equal);
		/* login: is whacked always translate to Username: */
		g_hash_table_insert (hash, "login:", _("Username:"));
		g_hash_table_insert (hash, "Username:", _("Username:"));
		g_hash_table_insert (hash, "username:", _("Username:"));
		g_hash_table_insert (hash, "Password:", _("Password:"));
		g_hash_table_insert (hash, "password:", _("Password:"));
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
		/* FIXME: what about messages which have some variables in them, perhaps try to do those as well */
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
    const void *p;
    const char *login;

    if (pamh == NULL)
	return PAM_CONV_ERR;
    
    reply = malloc (sizeof (struct pam_response) * num_msg);
    
    if (reply == NULL)
	return PAM_CONV_ERR;

    /* Should never happen unless PAM is on crack and keeps asking questions
       after we told it to piss off.  So tell it to piss off again and
       maybe it will listen */
    if ( ! gdm_slave_action_pending ())
        return PAM_CONV_ERR;

    memset (reply, 0, sizeof (struct pam_response) * num_msg);

    /* Here we set the login if it wasn't already set,
     * this is kind of anal, but this way we guarantee that
     * the greeter always is up to date on the login */
    if (pam_get_item (pamh, PAM_USER, &p) == PAM_SUCCESS ||
	tmp_PAM_USER != NULL) {
	    login = (const char *)p;

	    /* FIXME: this is a HACK HACK HACK, for some reason needed */
	    if (tmp_PAM_USER != NULL)
		    login = tmp_PAM_USER;
	    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);
    }

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);
    
    for (replies = 0; replies < num_msg; replies++) {
	gboolean islogin = FALSE;
	const char *m = msg[replies]->msg;
	m = perhaps_translate_message (m);
	
	switch (msg[replies]->msg_style) {
	    
	/* PAM requested textual input with echo on */
	case PAM_PROMPT_ECHO_ON:
 	    if (strcmp(m, _("Username:")) == 0) {
		    if ( ! ve_string_empty (selected_user)) {
			    /* Sometimes we are just completely on crack,
			       and pam asks for the username even if we
			       already gave it.  PAM is on better crack,
			       then I can afford. */
			    s = g_strdup (selected_user);
		    } else {
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
		    }
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

		    if (GdmDisplayLastLogin) {
			    char *info = gdm_get_last_info (s);
			    gdm_slave_greeter_ctl_no_ret (GDM_ERRBOX, info);
			    g_free (info);
		    }
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
	    gdm_slave_greeter_ctl_no_ret (GDM_ERRDLG, m);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	case PAM_TEXT_INFO:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl_no_ret (GDM_MSG, m);
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

	if (pamh == NULL)
		return PAM_CONV_ERR;
    
	reply = malloc (sizeof (struct pam_response) * num_msg);

	if (reply == NULL)
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
	/* init things */
	pamh = NULL;
	opened_session = FALSE;
	did_setcred = FALSE;

	/* Initialize a PAM session for the user */
	if ((*pamerr = pam_start (service, login, conv, &pamh)) != PAM_SUCCESS) {
		pamh = NULL; /* be anal */
		if (gdm_slave_action_pending ())
			gdm_error (_("Unable to establish service %s: %s\n"),
				   service, pam_strerror (NULL, *pamerr));
		return FALSE;
	}

	/* Inform PAM of the user's tty */
#ifdef	sun
	if (d->console == 0)
		(void) pam_set_item (pamh, PAM_TTY, "/dev/console");
	else 
#endif	/* sun */
	if ((*pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
		if (gdm_slave_action_pending ())
			gdm_error (_("Can't set PAM_TTY=%s"), display);
		return FALSE;
	}

	if ( ! d->console) {
		/* Only set RHOST if host is remote */
		/* From the host of the display */
		if ((*pamerr = pam_set_item (pamh, PAM_RHOST,
					     d->hostname)) != PAM_SUCCESS) {
			if (gdm_slave_action_pending ())
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
    gint pamerr;
    const void *p;
    char *login;
    struct passwd *pwent = NULL;
    gboolean error_msg_given;
    gboolean credentials_set;
    gboolean started_timer;
    int null_tok;
#ifdef HAVE_ADT
    int pw_change = PW_FALSE;   /* if got to trying to change password */
#endif	/* HAVE_ADT */

verify_user_again:

    pamerr = 0;
    login = NULL;
    error_msg_given = FALSE;
    credentials_set = FALSE;
    started_timer = FALSE;
    null_tok = 0;

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
	    
authenticate_again:

    /* hack */
    g_free (tmp_PAM_USER);
    tmp_PAM_USER = g_strdup (login);

    /* Initialize a PAM session for the user */
    if ( ! create_pamh (d, "gdm", login, &pamc, display, &pamerr)) {
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    goto pamerr;
    }

    pam_set_item (pamh, PAM_USER_PROMPT, _("Username:"));

#if 0
    /* FIXME: this makes things wait at the wrong places! such as
       when running the configurator.  We wish to ourselves cancel logins
       without a delay, so ... evil */
#ifdef PAM_FAIL_DELAY
    pam_fail_delay (pamh, GdmRetryDelay * 1000000);
#endif /* PAM_FAIL_DELAY */
#endif

    did_we_ask_for_password = FALSE;

    gdm_verify_select_user (NULL);
    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, null_tok)) != PAM_SUCCESS) {
	    if ( ! ve_string_empty (selected_user)) {
		    pam_handle_t *tmp_pamh;

		    /* Face browser was used to select a user,
		       just completely rewhack everything since it
		       seems various PAM implementations are
		       having goats with just setting PAM_USER
		       and trying to pam_authenticate again */

		    g_free (login);
		    login = selected_user;
		    selected_user = NULL;

		    gdm_sigterm_block_push ();
		    gdm_sigchld_block_push ();
		    tmp_pamh = pamh;
		    pamh = NULL;
		    gdm_sigchld_block_pop ();
		    gdm_sigterm_block_pop ();

		    /* FIXME: what about errors */
		    /* really this has been a sucess, not a failure */
		    pam_end (tmp_pamh, PAM_SUCCESS);

		    gdm_slave_greeter_ctl_no_ret (GDM_SETLOGIN, login);

		    goto authenticate_again;
	    }
	    if (started_timer)
		    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");
	    if (gdm_slave_action_pending ()) {
		    /* FIXME: see note above about PAM_FAIL_DELAY */
/* #ifndef PAM_FAIL_DELAY */
		    gdm_sleep_no_signal (GdmRetryDelay);
		    /* wait up to 100ms randomly */
		    usleep (g_random_int_range (0, 100000));
/* #endif */ /* PAM_FAIL_DELAY */
		    gdm_error (_("Couldn't authenticate user"));
	    }
	    goto pamerr;
    }

    /* stop the timer for timed logins */
    if (started_timer)
	    gdm_slave_greeter_ctl_no_ret (GDM_STOPTIMER, "");

    g_free (login);
    login = NULL;
    
    if ((pamerr = pam_get_item (pamh, PAM_USER, &p)) != PAM_SUCCESS) {
	    login = NULL;
	    /* is not really an auth problem, but it will
	       pretty much look as such, it shouldn't really
	       happen */
	    if (gdm_slave_action_pending ())
		    gdm_error (_("Couldn't authenticate user"));
	    goto pamerr;
    }
    login = g_strdup ((const char *)p);
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

	    goto verify_user_again;
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

#ifdef HAVE_ADT
	/*
	 * map console login not allowed as a pam_acct_mgmt() failure
	 * indeed that's where these checks should be made.
	 */
    pamerr = PAM_PERM_DENIED;
#endif	/* HAVE_ADT */
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
#ifdef  HAVE_ADT
		/* Password change failed */
		pw_change = PW_FAILED;
#endif	/* HAVE_ADT */
	    goto pamerr;
	}
#ifdef  HAVE_ADT
		/* Password changed */
		pw_change = PW_TRUE;
#endif	/* HAVE_ADT */
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
	if (gdm_slave_action_pending ())
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
#ifdef  HAVE_ADT
		/*
		 * map group setup error as a pam_setcred() failure
		 * indeed that's where this should be done.
		 */
		pamerr = PAM_SYSTEM_ERR;
#endif	/* HAVE_ADT */
	    goto pamerr;
    }

    did_setcred = TRUE;
    /* Set credentials */
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
        did_setcred = FALSE;
	if (gdm_slave_action_pending ())
	    gdm_error (_("Couldn't set credentials for %s"), login);
	goto pamerr;
    }

    credentials_set = TRUE;

    opened_session = TRUE;
    /* Register the session */
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
            opened_session = FALSE;
	    /* we handle this above */
            did_setcred = FALSE;
	    if (gdm_slave_action_pending ())
		    gdm_error (_("Couldn't open session for %s"), login);
	    goto pamerr;
    }

    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    cur_gdm_disp = NULL;

    g_free (tmp_PAM_USER);
    tmp_PAM_USER = NULL;

#ifdef  HAVE_LOGINDEVPERM
	if (d->console)
		(void) di_devperm_login("/dev/console", pwent->pw_uid,
		    pwent->pw_gid, NULL);
#endif	/* HAVE_LOGINDEVPERM */
#ifdef  HAVE_ADT
	audit_success_login(pw_change, pwent);
#endif  /* HAVE_ADT */

    return login;
    
 pamerr:
#ifdef  HAVE_ADT
	audit_fail_login(d, pw_change, pwent, pamerr);
#endif	/* HAVE_ADT */

    /* The verbose authentication is turned on, output the error
     * message from the PAM subsystem */
    if ( ! error_msg_given &&
	gdm_slave_action_pending ()) {
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

    did_setcred = FALSE;
    opened_session = FALSE;

    if (pamh != NULL) {
	    pam_handle_t *tmp_pamh;
	    gdm_sigterm_block_push ();
	    gdm_sigchld_block_push ();
	    tmp_pamh = pamh;
	    pamh = NULL;
	    gdm_sigchld_block_pop ();
	    gdm_sigterm_block_pop ();

	    /* Throw away the credentials */
	    if (credentials_set)
		    pam_setcred (tmp_pamh, PAM_DELETE_CRED);
	    pam_end (tmp_pamh, pamerr);
    }
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    g_free (login);

    g_free (tmp_PAM_USER);
    tmp_PAM_USER = NULL;
    
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
    struct passwd *pwent = NULL;
    const void *p;
    const char *after_login;
    int null_tok = 0;
    
#ifdef HAVE_ADT
    int pw_change = PW_FALSE;   /* if got to trying to change password */
#endif	/* HAVE_ADT */

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
	    if (gdm_slave_action_pending ()) {
		    gdm_error (_("Couldn't authenticate user"));
		    gdm_error_box (cur_gdm_disp,
				   GTK_MESSAGE_ERROR,
				   _("Authentication failed"));
	    }
	    goto setup_pamerr;
    }

    if ((pamerr = pam_get_item (pamh, PAM_USER, &p)) != PAM_SUCCESS) {
	    /* is not really an auth problem, but it will
	       pretty much look as such, it shouldn't really
	       happen */
	    gdm_error (_("Couldn't authenticate user"));
	    gdm_error_box (cur_gdm_disp,
			   GTK_MESSAGE_ERROR,
			   _("Authentication failed"));
	    goto setup_pamerr;
    }
    after_login = (const char *)p;

    if (after_login != NULL /* should never be */ &&
	strcmp (after_login, login) != 0) {
	    *new_login = g_strdup (after_login);
    }

#ifdef  HAVE_ADT
	/* to set up for same auditing calls as in gdm_verify_user */
	pwent = getpwnam (login);
#endif	/* HAVE_ADT */

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
#if	0	/* don't change password */
	if ((pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK)) != PAM_SUCCESS) {
	    gdm_error (_("Authentication token change failed for user %s"), login);
	    gdm_error_box (cur_gdm_disp,
			   GTK_MESSAGE_ERROR,
		    _("\nThe change of the authentication token failed. "
		      "Please try again later or contact the system administrator."));
#ifdef  HAVE_ADT
		pw_change = PW_FAILED;
#endif	/* HAVE_ADT */
	    goto setup_pamerr;
	}
#ifdef  HAVE_ADT
    pw_change = PW_TRUE;
#endif	/* HAVE_ADT */
#endif	/* 0 */
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
	if (gdm_slave_action_pending ())
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
#ifdef  HAVE_ADT
		/*
		 * map group setup error as a pam_setcred() failure
		 * indeed that's where this should be done.
		 */
		pamerr = PAM_SYSTEM_ERR;
#endif	/* HAVE_ADT */
	    goto setup_pamerr;
    }

    did_setcred = TRUE;

    /* Set credentials */
    pamerr = pam_setcred (pamh, PAM_ESTABLISH_CRED);
    if (pamerr != PAM_SUCCESS) {
	    did_setcred = FALSE;
	    if (gdm_slave_action_pending ())
		    gdm_error (_("Couldn't set credentials for %s"), login);
	    goto setup_pamerr;
    }

    opened_session = TRUE;

    /* Register the session */
    pamerr = pam_open_session (pamh, 0);
    if (pamerr != PAM_SUCCESS) {
	    did_setcred = FALSE;
	    opened_session = FALSE;
	    /* Throw away the credentials */
	    pam_setcred (pamh, PAM_DELETE_CRED);

	    if (gdm_slave_action_pending ())
		    gdm_error (_("Couldn't open session for %s"), login);
	    goto setup_pamerr;
    }


    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    cur_gdm_disp = NULL;

    g_free (extra_standalone_message);
    extra_standalone_message = NULL;

#ifdef  HAVE_LOGINDEVPERM
	if (d->console)
		(void) di_devperm_login("/dev/console", pwent->pw_uid,
		    pwent->pw_gid, NULL);
#endif	/* HAVE_LOGINDEVPERM */
#ifdef  HAVE_ADT
    audit_success_login(pw_change, pwent);
#endif	/* HAVE_ADT */
    
    return TRUE;
    
 setup_pamerr:

#ifdef  HAVE_ADT
    audit_fail_login(d, pw_change, pwent, pamerr);
#endif	/* HAVE_ADT */

    did_setcred = FALSE;
    opened_session = FALSE;
    if (pamh != NULL) {
	    pam_handle_t *tmp_pamh;

	    gdm_sigterm_block_push ();
	    gdm_sigchld_block_push ();
	    tmp_pamh = pamh;
	    pamh = NULL;
	    gdm_sigchld_block_pop ();
	    gdm_sigterm_block_pop ();

	    pam_end (tmp_pamh, pamerr);
    }
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

#ifdef	HAVE_ADT
		/*
		 * User exiting.
		 * If logged in, audit logout before cleaning up
		 */
		if (old_opened_session && old_did_setcred) {
			audit_logout();
		}
#endif	/* HAVE_ADT */
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

#ifdef  HAVE_LOGINDEVPERM
        if (old_opened_session && old_did_setcred && d->console)
        {
            (void) di_devperm_logout("/dev/console");
            /* give it back to gdm user */
            (void) di_devperm_login("/dev/console", GdmUserId, GdmGroupId,
                NULL);
        }
#endif  /* HAVE_LOGINDEVPERM */

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
		ph = NULL; /* be anal */

		closelog ();
		openlog ("gdm", LOG_PID, LOG_DAEMON);

		if ( ! no_console)
			gdm_text_message_dialog
				(C_(N_("Can't find PAM configuration for gdm.")));
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
