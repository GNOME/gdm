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

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "verify.h"

static const gchar RCSid[]="$Id$";


/* Configuration option variables */
extern gboolean GdmVerboseAuth;
extern gboolean GdmAllowRoot;

/* Local PAM handle */
pam_handle_t *pamh = NULL;


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
    
    for (replies = 0; replies < num_msg; replies++) {
	
	switch (msg[replies]->msg_style) {
	    
	case PAM_PROMPT_ECHO_ON:
	    /* PAM requested textual input with echo on */
	    s = gdm_slave_greeter_ctl (GDM_PROMPT, (gchar *) msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (s);
	    g_free (s);
	    break;
	    
	case PAM_PROMPT_ECHO_OFF:
	    /* PAM requested textual input with echo off */
	    s = gdm_slave_greeter_ctl (GDM_NOECHO, (gchar *) msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = strdup (s);
	    g_free (s);
	    break;
	    
	case PAM_ERROR_MSG:
	case PAM_TEXT_INFO:
	    /* PAM sent a message that should displayed to the user */
	    gdm_slave_greeter_ctl (GDM_MSGERR, (gchar *) msg[replies]->msg);
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
	/* FIXME: always error out for now! */
	return PAM_CONV_ERR;
}

static struct pam_conv standalone_pamc = {
    &gdm_verify_standalone_pam_conv,
    NULL
};


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
    gint pamerr;
    gchar *login;
    gchar **pamenv;

    /* Ask gdmgreeter for the user's login. Just for good measure */
    login = gdm_slave_greeter_ctl (GDM_PROMPT, _("Login:"));
    
    if (!login)
	return NULL;

    /* Initialize a PAM session for the user */
    if ((pamerr = pam_start ("gdm", login, &pamc, &pamh)) != PAM_SUCCESS) {
	gdm_error (_("Can't find /etc/pam.d/gdm!"));
	goto pamerr;
    }
    
    /* Inform PAM of the user's tty */
    if ((pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
	gdm_error (_("Can't set PAM_TTY=%s"), display);
	goto pamerr;
    }

    /* Start authentication session */
    if ((pamerr = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't authenticate %s"), login);
	goto pamerr;
    }

    /* If the user's password has expired, ask for a new one */
    pamerr = pam_acct_mgmt (pamh, 0);

    if (pamerr == PAM_NEW_AUTHTOK_REQD)
	pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK); 

    if (pamerr != PAM_SUCCESS) {
	gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto pamerr;
    }
    
    /* Set credentials */
    if ((pamerr = pam_setcred (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't set credentials for %s"), login);
	goto pamerr;
    }
    
    /* Register the session */
    if ((pamerr = pam_open_session (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't open session for %s"), login);
	goto pamerr;
    }
    
    /* Migrate any PAM env. variables to the user's environment */
    if ((pamenv = pam_getenvlist (pamh))) {
	gint i;

	for (i = 0 ; pamenv[i] ; i++)
	    putenv (pamenv[i]);
    }
    
    return login;
    
 pamerr:
    
    /* The verbose authentication is turned on, output the error
     * message from the PAM subsystem */
    if (GdmVerboseAuth)
	gdm_slave_greeter_ctl (GDM_MSGERR, _("Authentication failed"));
    
    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);
    
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

void
gdm_verify_setup_user (const gchar *login, const gchar *display) 
{
    gint pamerr;
    gchar **pamenv;

    if (!login)
	return;

    /* Initialize a PAM session for the user */
    if ((pamerr = pam_start ("gdm", login, &standalone_pamc, &pamh)) != PAM_SUCCESS) {
	gdm_error (_("Can't find /etc/pam.d/gdm!"));
	goto setup_pamerr;
    }
    
    /* Inform PAM of the user's tty */
    if ((pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
	gdm_error (_("Can't set PAM_TTY=%s"), display);
	goto setup_pamerr;
    }

    /* If the user's password has expired, ask for a new one */
    /* FIXME: This would require our conversation function to work */
#if 0
    pamerr = pam_acct_mgmt (pamh, 0);

    if (pamerr == PAM_NEW_AUTHTOK_REQD)
	pamerr = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK); 

    if (pamerr != PAM_SUCCESS) {
	gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto setup_pamerr;
    }
#endif
    
    /* Set credentials */
    if ((pamerr = pam_setcred (pamh, PAM_SILENT)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't set credentials for %s"), login);
	goto setup_pamerr;
    }
    
    /* Register the session */
    if ((pamerr = pam_open_session (pamh, PAM_SILENT)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't open session for %s"), login);
	goto setup_pamerr;
    }
    
    /* Migrate any PAM env. variables to the user's environment */
    if ((pamenv = pam_getenvlist (pamh))) {
	gint i;

	for (i = 0 ; pamenv[i] ; i++)
	    putenv (pamenv[i]);
    }
    
    return;
    
 setup_pamerr:
    
    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);
}


/**
 * gdm_verify_cleanup:
 *
 * Unregister the user's session
 */

void 
gdm_verify_cleanup (void)
{
    if (pamh) {
	pam_close_session (pamh, 0);
	pam_end (pamh, PAM_SUCCESS);
	pamh = NULL;
    }
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog ();
    openlog ("gdm", LOG_PID, LOG_DAEMON);
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


/* EOF */
