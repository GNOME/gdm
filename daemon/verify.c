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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <pwd.h>
#include <glib.h>
#include <config.h>
#include <gnome.h>

#ifdef HAVE_PAM
 #include <security/pam_appl.h>
#endif /* HAVE_PAM */

#ifdef HAVE_CRYPT
  #include <crypt.h>
#endif /* HAVE_CRYPT */

#ifdef HAVE_SHADOW
 #include <shadow.h>
#endif /* HAVE_SHADOW */

#include "gdm.h"

static const gchar RCSid[]="$Id$";

extern void gdm_abort (const char*, ...);
extern void gdm_fail (const gchar *, ...);
extern void gdm_debug (const char*, ...);
extern void gdm_error (const char*, ...);
extern gchar *gdm_slave_greeter_ctl (gchar cmd, gchar *str);

extern gboolean GdmVerboseAuth;
extern gboolean GdmAllowRoot;

gchar *gdm_verify_user (gchar *display);
void gdm_verify_cleanup (void);
void gdm_verify_check (void);


#ifdef HAVE_PAM

pam_handle_t *pamh;


static gint 
gdm_verify_pam_conv (int num_msg, const struct pam_message **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    gint replies = 0;
    struct pam_response *reply = NULL;
    gchar *s;
    
    reply = g_new0 (struct pam_response, num_msg);
    
    if (!reply) 
	return PAM_CONV_ERR;
    
    for (replies = 0; replies < num_msg; replies++) {
	
	switch (msg[replies]->msg_style) {
	    
	case PAM_PROMPT_ECHO_ON:
	    s = gdm_slave_greeter_ctl (GDM_PROMPT, (gchar *) msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = g_strdup (s);
	    g_free (s);
	    break;
	    
	case PAM_PROMPT_ECHO_OFF:
	    s = gdm_slave_greeter_ctl (GDM_NOECHO, (gchar *) msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = g_strdup (s);
	    g_free (s);
	    break;
	    
	case PAM_ERROR_MSG:
	case PAM_TEXT_INFO:
	    gdm_slave_greeter_ctl (GDM_MSGERR, (gchar *) msg[replies]->msg);
	    reply[replies].resp_retcode = PAM_SUCCESS;
	    reply[replies].resp = NULL;
	    break;
	    
	default:
	    g_free (reply);
	    return (PAM_CONV_ERR);
	}
	
    }

    *resp = reply;
    return (PAM_SUCCESS);
}


static struct pam_conv pamc = {
    &gdm_verify_pam_conv,
    NULL
};


gchar *
gdm_verify_user (gchar *display) 
{
    gint pamerr, i;
    gchar *login;
    gchar **pamenv;
    
    login = gdm_slave_greeter_ctl (GDM_PROMPT, _("Login:"));
    
    if (!login)
	return (NULL);
    
    if ((pamerr = pam_start ("gdm", login, &pamc, &pamh)) != PAM_SUCCESS) {
	gdm_error (_("Can't find /etc/pam.d/gdm!"));
	goto pamerr;
    }
    
    if ((pamerr = pam_set_item (pamh, PAM_TTY, display)) != PAM_SUCCESS) {
	gdm_error (_("Can't set PAM_TTY=%s"), ":0");
	goto pamerr;
    }
    
    if ((pamerr = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't authenticate %s"), login);
	goto pamerr;
    }
    
    if ((pamerr = pam_acct_mgmt (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't set acct. mgmt for %s"), login);
	goto pamerr;
    }
    
    if ((pamerr = pam_setcred (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't set credentials for %s"), login);
	goto pamerr;
    }
    
    if ((pamerr = pam_open_session (pamh, 0)) != PAM_SUCCESS) {
	gdm_error (_("Couldn't open session for %s"), login);
	goto pamerr;
    }
    
    if ((pamenv = pam_getenvlist (pamh))) {
	for (i = 0 ; pamenv[i] ; i++) {
	    putenv (pamenv[i]);
	}
    }
    
    return (login);
    
 pamerr:
    
    if (GdmVerboseAuth)
	gdm_slave_greeter_ctl (GDM_MSGERR, (gchar *) pam_strerror (pamh, pamerr));
    
    pam_end (pamh, pamerr);
    pamh = NULL;
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog();
    openlog ("gdm", LOG_PID, LOG_DAEMON);
    
    return (NULL);
}


#else /* HAVE_PAM */


gchar *
gdm_verify_user (gchar *display) 
{
    gchar *login, *passwd, *ppasswd;
    struct passwd *pwent;
    
#ifdef HAVE_SHADOW
    struct spwd *sp;
#endif
    
    login = gdm_slave_greeter_ctl (GDM_PROMPT, _("Login:"));
    pwent = getpwnam (login);
        
    ppasswd = !pwent ? NULL : pwent->pw_passwd;
    
#ifdef HAVE_SHADOW

    sp = getspnam (login);
    
    if (sp)
	ppasswd = sp->sp_pwdp;
    
    endspent();

#endif /* HAVE_SHADOW */
    
    passwd = gdm_slave_greeter_ctl (GDM_NOECHO, _("Password:"));

    if (GdmVerboseAuth) {

	if (!pwent) {
	    gdm_error (_("Couldn't authenticate %s"), login);
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("User unknown"));
	    return (NULL);
	}
    
	if (pwent->pw_uid == 0) {
	    gdm_error (_("Root login disallowed on display '%s'"), display);
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("Root login disallowed"));
	    return (NULL);
	}	
    }

    if (!passwd || !ppasswd || strcmp (crypt (passwd, ppasswd), ppasswd)) {

	if (GdmVerboseAuth)
	    gdm_slave_greeter_ctl (GDM_MSGERR, _("Incorrect password"));
	
	return (NULL);
    }
    
    return (login);
}

#endif /* HAVE_PAM */


void
gdm_verify_check (void)
{

#ifdef HAVE_PAM
    struct stat statbuf;

    if (stat ("/etc/pam.d/gdm", &statbuf) && stat ("/etc/pam.conf", &statbuf))
	gdm_fail (_("gdm_verify_check: Can't find PAM configuration file for gdm"));

#endif /* HAVE_PAM */

}


void 
gdm_verify_cleanup (void)
{

#ifdef HAVE_PAM

    gdm_debug ("gdm_verify_cleanup: Closing session %d", pamh);
    
    if (pamh) {
	pam_close_session (pamh, 0);
	pam_end (pamh, PAM_SUCCESS);
	pamh = NULL;
    }
    
    /* Workaround to avoid gdm messages being logged as PAM_pwdb */
    closelog();
    openlog ("gdm", LOG_PID, LOG_DAEMON);

#endif /* HAVE_PAM */

}

/* EOF */
