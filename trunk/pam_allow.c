#include <stdio.h>
#include <security/pam_appl.h>

/*
 * This code may be used to create a pam_allow.so.1 PAM module
 * to allow Automatic Login to work on Solaris 10 or lower.
 *
 * Compile:
 *	cc pam_allow.c -o pam_allow.so.1 -Kpic -G
 */
int 
pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}

int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}


int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}

int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}

int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}

int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_SUCCESS);
}
