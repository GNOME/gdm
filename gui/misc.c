/* GDM - The Gnome Display Manager - misc functions
 * Copyright (C) 1998, 1999, 2000 Martin K, Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>

#include "misc.h"

static const gchar RCSid[]="$Id$";

void
entry_set_red (GtkWidget *w, gboolean state)
{
	if (state) {
		GtkStyle *ns;
		GdkColor red = { 0, 65535, 0, 0 };

		ns = gtk_style_copy (w->style);
		gtk_style_ref (ns);

		ns->fg[GTK_STATE_NORMAL] = red;
		ns->text[GTK_STATE_NORMAL] = red;

		gtk_widget_set_style (w, ns);
		gtk_style_unref (ns);

		gtk_widget_queue_draw (w);
	} else {
		gtk_widget_set_rc_style (w);
	}
}

/* Do strcasecmp but ignore locale */
int
strcasecmp_no_locale (const char *s1, const char *s2)
{
	int i;

	/* Error, but don't make them equal then */
	g_return_val_if_fail (s1 != NULL, G_MAXINT);
	g_return_val_if_fail (s2 != NULL, G_MININT);

	for (i = 0; s1[i] != '\0' && s2[i] != '\0'; i++) {
		char a = s1[i];
		char b = s2[i];

		if (a >= 'A' && a <= 'Z')
			a -= 'A' - 'a';
		if (b >= 'A' && b <= 'Z')
			b -= 'A' - 'a';

		if (a < b)
			return -1;
		else if (a > b)
			return 1;
	}

	/* find out which string is smaller */
	if (s2[i] != '\0')
		return -1; /* s1 is smaller */
	else if (s1[i] != '\0')
		return 1; /* s2 is smaller */
	else
		return 0; /* equal */
}

char **
gdm_split (const char *s)
{
	int argc;
	char **temp_argv;
	char **ret;
	int i;

	if (s == NULL)
		return NULL;

	if (poptParseArgvString (s, &argc, &temp_argv) != 0) {
		return g_strsplit (s, " ", -1);
	}

	ret = g_new (char *, argc+1);
	for (i = 0; i < argc; i++) {
		ret[i] = g_strdup (temp_argv[i]);
	}
	ret[i] = NULL;

	free (temp_argv);

	return ret;
}

char *
gdm_first_word (const char *s)
{
	int argc;
	char **temp_argv;
	char *ret;

	if (s == NULL)
		return NULL;

	if (poptParseArgvString (s, &argc, &temp_argv) != 0) {
		char *p;
		ret = g_strdup (s);
		p = strchr (ret, ' ');
		if (p != NULL)
			*p = '\0';
		return ret;
	}

	ret = g_strdup (temp_argv[0]);

	free (temp_argv);

	return ret;
}


/* EOF */
