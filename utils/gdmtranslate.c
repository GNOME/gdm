#include "config.h"
#include <stdio.h>
#include <locale.h>
#include <string.h>

#define gboolean char
#define FALSE 0
#define TRUE 1

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else /* !ENABLE_NLS */
#define _(String) (String)
#endif

int
main (int argc, char *argv[])
{
	char *string;
	gboolean is_utf8 = FALSE;

	if (argc == 3 &&
	    strcmp (argv[1], "--utf8") == 0) {
		string = argv[2];
		is_utf8 = TRUE;
	} else if (argc == 2) {
		string = argv[1];
		is_utf8 = FALSE;
	} else {
		fprintf (stderr, "usage: gdmtranslate [--utf8] <string to translate>\n");
		return 0;
	}

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	if (is_utf8)
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	printf ("%s\n", _(string));

	return 0;
}
