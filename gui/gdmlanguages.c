/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2001 George Lebl
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

#include "config.h"
#include <gnome.h>
#include <string.h>

#include "gdmlanguages.h"

typedef struct _Language Language;
struct _Language {
	char *name;
	char *code;
	int found;
};

/* Note: these should NOT include the encodings, this is just a translation
 * matrix for language_country names.  This is NOT a list of available
 * languages, just their names and where they are placed in the menu.
 * The available languages come from the supplied locale.alias */
static Language languages [] = {
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Catalan"), "ca_ES", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (simplified)"), "zh_CN", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (traditional)"), "zh_TW", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Croatian"), "hr_HR", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Czech"), "cs_CZ", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Danish"), "da_DK", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Dutch"), "nl_NL", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English"), "en", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|American English"), "en_US", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|British English"), "en_GB", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Finnish"), "fi_FI", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French"), "fr_FR", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Galician"), "gl_ES", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German"), "de_DE", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Greek"), "el_GR", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hebrew"), "iw_IL", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hungarian"), "hu_HU", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Icelandic"), "is_IS", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Italian"), "it_IT", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Japanese"), "ja_JP", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Korean"), "ko_KR", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Lithuanian"), "lt_LT", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (bokmal)"), "no_NO", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (nynorsk)"), "nn_NO", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Polish"), "pl_PL", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Portuguese"), "pt_PT", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Brazilian Portuguese"), "pt_BR", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Romanian"), "ro_RO", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Russian"), "ru_RU", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovak"), "sk_SK", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovenian"), "sl_SI", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish"), "es_ES", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Swedish"), "sv_SE", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Turkish"), "tr_TR", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Ukrainian"), "uk_UA", 0 },
	/* This is the POSIX/C locale for english, should really be in Other */
	{ N_("Other|POSIX/C English"), "C", 0 },
	{ NULL, NULL }
};

static GHashTable *lang_names = NULL;

static void
gdm_lang_init (void)
{
	int i;
	if (lang_names != NULL)
		return;

	lang_names = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; languages[i].name != NULL; i ++) {
		g_hash_table_insert (lang_names,
				     languages[i].code,
				     &languages[i]);
	}
}

static Language *
find_lang (const char *language, gboolean *clean)
{
	char *name, *p;
	Language *lang;

	*clean = FALSE;

	lang = g_hash_table_lookup (lang_names, language);
	if (lang != NULL) {
		*clean = TRUE;
		return lang;
	}

	name = g_strdup (language);

	p = strrchr (name, '@');
	if (p != NULL) {
		*p = '\0';
		lang = g_hash_table_lookup (lang_names, name);
		if (lang != NULL) {
			g_free (name);
			return lang;
		}
	}

	p = strrchr (name, '.');
	if (p != NULL) {
		*p = '\0';
		lang = g_hash_table_lookup (lang_names, name);
		if (lang != NULL) {
			g_free (name);
			return lang;
		}
	}

	p = strrchr (name, '_');
	if (p != NULL) {
		*p = '\0';
		lang = g_hash_table_lookup (lang_names, name);
		if (lang != NULL) {
			g_free (name);
			return lang;
		}
	}

	g_free (name);
	return NULL;
}

char *
gdm_lang_name (const char *locale, const char *language,
	       gboolean never_encoding, gboolean no_group)
{
	Language *lang;
	char *name;
	gboolean clean;
	const char *encoding;

	/* FIXME: we need to get the languages for right locales here,
	 * for now we just translate the current locale */
	if (locale != NULL)
		return NULL;

	gdm_lang_init ();

	lang = find_lang (language, &clean);
	if (lang == NULL)
		return g_strdup (language);

	encoding = strchr (language, '.');
	if (encoding != NULL)
		encoding++;

	/* if more then one language in the language file with this
	 * locale, then hell, include the encoding to differentiate them */
	if (lang->found > 1 &&
	    encoding != NULL &&
	    ! never_encoding)
		name = g_strdup_printf ("%s (%s)", _(lang->name), encoding);
	else
		name = g_strdup (_(lang->name));

	if (no_group) {
		char *p = strchr (name, '|');
		if (p != NULL) {
			strcpy (name, p+1);
		}
	}

	return name;
}

GdkFont *
gdm_lang_font (const char *locale)
{
	/* FIXME! */
	return NULL;
}

const char *
gdm_lang_group1 (void)
{
	/* This should be the same as in the front of the language strings
	 * else the languages will appear in the "Other" submenu */
	return _("A-M");
}

const char *
gdm_lang_group2 (void)
{
	/* This should be the same as in the front of the language strings
	 * else the languages will appear in the "Other" submenu */
	return _("N-Z");
}

static int
lang_collate (gconstpointer a, gconstpointer b)
{
	gboolean clean;
	Language *l1 = find_lang ((const char *)a, &clean);
	Language *l2 = find_lang ((const char *)b, &clean);
	const char *name1, *name2;

	/* paranoia */
	if (l1 == NULL || l2 == NULL)
		return 0;

	name1 = strchr (_(l1->name), '|');
	if (name1 != NULL)
		name1 ++;
	else
		name1 = _(l1->name);
	name2 = strchr (_(l2->name), '|');
	if (name2 != NULL)
		name2 ++;
	else
		name2 = _(l2->name);

	return strcoll (name1, name2);
}

GList *
gdm_lang_read_locale_file (const char *locale_file)
{
	FILE *langlist;
	char curline[256];
	GList *langs = NULL;
	GHashTable *dupcheck;
	gboolean got_C = FALSE;

	if (locale_file == NULL)
		return NULL;

	langlist = fopen (locale_file, "r");

	if (langlist == NULL)
		return NULL;

	gdm_lang_init ();

	dupcheck = g_hash_table_new (g_str_hash, g_str_equal);

	while (fgets (curline, sizeof (curline), langlist)) {
		char *name;
		char *lang;
		gboolean clean;
		Language *language;

		if (curline[0] <= ' ' ||
		    curline[0] == '#')
			continue;

		name = strtok (curline, " \t\r\n");
		if (name == NULL)
			continue;

		lang = strtok (NULL, " \t\r\n");
		if (lang == NULL)
			continue;

		if (g_hash_table_lookup (dupcheck, lang) != NULL) {
			continue;
		}
		language = find_lang (lang, &clean);

		if (language != NULL) {
			language->found++;
		} else {
			language = g_new0 (Language, 1);
			language->found = 1;
			language->name = g_strdup (name);
			language->code = g_strdup (lang);
			g_hash_table_insert (lang_names,
					     language->code,
					     language);
		}

		langs = g_list_prepend (langs, g_strdup (lang));
		g_hash_table_insert (dupcheck, g_strdup (lang),
				     GINT_TO_POINTER (1));

		if (strcmp (lang, "C") == 0)
			got_C = TRUE;
	}

	g_hash_table_foreach (dupcheck, (GHFunc) g_free, NULL);
	g_hash_table_destroy (dupcheck);

	if ( ! got_C)
		langs = g_list_prepend (langs, g_strdup ("C"));

	langs = g_list_sort (langs, lang_collate);

	fclose (langlist);

	return langs;
}

/* EOF */
