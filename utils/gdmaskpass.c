/* Simple pam thingie to ask the root password, for use in XKeepsCrashing
 * script. */
#include "config.h"
#include <gnome.h>
#include <stdio.h>
#include <locale.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

static struct pam_conv conv = {
	misc_conv,
	NULL
};

int
main (int argc, char *argv[])
{
	pam_handle_t *pamh;
	const char *username = "root";
	int retval;
	int tries = 3;

	if (getuid () != geteuid () ||
	    getuid () != 0) {
		fprintf (stderr, _("gdmaskpass only runs as root\n"));
		return 1;
	}

	if (argc >= 2) {
		username = argv[1];
	}

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	for (tries = 3; tries > 0; tries --) {
		if ((retval = pam_start ("gdm", username, &conv, &pamh)) != PAM_SUCCESS) {
			pam_end (pamh, retval);
			pamh = NULL;
			printf (_("Authentication failure!\n"));
			continue;
		}
		if ((retval = pam_authenticate (pamh, 0)) != PAM_SUCCESS) {
			pam_end (pamh, retval);
			pamh = NULL;
			printf (_("Authentication failure!\n"));
			continue;
		}
		break;
	}

	if (pamh != NULL)
		pam_end (pamh, retval);

	return (retval == PAM_SUCCESS) ? 0 : 1;
}
