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
#include <locale.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <string.h>

#include <vicious.h>

#include "gdmlanguages.h"

typedef struct _Language Language;
struct _Language {
	char *name;
	char *code;
	char *untranslated;
	int found;

	/* extra fields */
	char *collate_key;
};

/* FIXME: We need to have a nicer selection of language and country,
 * I suppose that we should have a menu of languages and then for each have
 * a submenu of countries (and perhaps other variations such as utf8 vs not
 * etc...) if there is more then one. */

/* Note: these should NOT include the encodings, this is just a translation
 * matrix for language_country names.  This is NOT a list of available
 * languages, just their names and where they are placed in the menu.
 * The available languages come from the supplied locale.alias */
static Language languages [] = {
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Azerbaijani"), "az_AZ", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Amharic"), "am_ET", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Arabic (Egypt)"), "ar_EG", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Arabic (Lebanon)"), "ar_LB", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Basque"), "eu_ES", "Euskara", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Bulgarian"), "bg_BG", "Български", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Catalan"), "ca_ES", "Català", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (simplified)"), "zh_CN", "中文 (简体)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (traditional)"), "zh_TW", "繁體中文", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Croatian"), "hr_HR", "Hrvatski", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Czech"), "cs_CZ", "čeština", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Danish"), "da_DK", "dansk", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Dutch"), "nl_NL", "Nederlands", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English"), "en", "English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Estonian"), "et_EE", "Eesti", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (American)"), "en_US", "American English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Australian)"), "en_AU", "Australian English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (British)"), "en_GB", "British English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Ireland)"), "en_IE", "Irish English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Finnish"), "fi_FI", "Suomi", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French"), "fr_FR", "Français", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Galician"), "gl_ES", "Galego", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German"), "de_DE", "Deutsch", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Greek"), "el_GR", "ελληνικά", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hebrew"), "he_IL", "עברית", 0 },
	{ N_("A-M|Hebrew"), "iw_IL", "עברית", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hungarian"), "hu_HU", "Magyar", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Icelandic"), "is_IS", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Irish"), "ga_IE", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Italian"), "it_IT", "Italiano", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Japanese"), "ja_JP", "日本語", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Korean"), "ko_KR", "한국어", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Latvian"), "lv_LV", "Latviešu", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Lithuanian"), "lt_LT", "Lietuvių", 0 },
        /*Note translate the A-M to the A-M you used in the group label */
        { N_("A-M|Macedonian"), "mk_MK", NULL, 0 },
        /*Note translate the A-M to the A-M you used in the group label */
        { N_("A-M|Malay"), "ms_MY", "Bahasa Melayu", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (bokmal)"), "no_NO", "Norsk (bokmål)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (nynorsk)"), "nn_NO", "Norsk (nynorsk)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Polish"), "pl_PL", "Polski", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Portuguese"), "pt_PT", "Português", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Portuguese (Brazilian)"), "pt_BR", "Português do Brasil", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Romanian"), "ro_RO", "Română", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Russian"), "ru_RU", "русский", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovak"), "sk_SK", "Slovenský", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovenian"), "sl_SI", "Slovenščina", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish"), "es_ES", "Español", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Mexico)"), "es_MX", "Español (Mexico)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Swedish"), "sv_SE", "Svenska", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Swedish (Finland)"), "sv_FI", "Svenska (Finland)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Tamil"), "ta_IN", NULL, 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Turkish"), "tr_TR", "Türkçe", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Ukrainian"), "uk_UA", "Українська", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Vietnamese"), "vi_VN", "Việt Nam", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Walloon"), "wa_BE", "Walon", 0 },
	/* This is the POSIX/C locale for english, should really be in Other */
	{ N_("Other|POSIX/C English"), "C", "POSIX/C English", 0 },
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

gboolean
gdm_lang_name_translated (const char *language)
{
	Language *lang;
	gboolean clean;

	gdm_lang_init ();

	lang = find_lang (language, &clean);
	if (lang == NULL)
		return FALSE;

	if (strcmp (lang->name, _(lang->name)) == 0)
		return FALSE;

	return TRUE;
}

char *
gdm_lang_name (const char *language,
	       gboolean never_encoding,
	       gboolean no_group,
	       gboolean untranslated,
	       gboolean markup)
{
	Language *lang;
	char *name;
	gboolean clean;
	const char *encoding;

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

	if (lang->untranslated != NULL && untranslated) {
		char *full;
		if (markup) {
			full = g_strdup_printf
				("%s (<span lang=\"%s\">%s</span>)",
				 name,
				 lang->code,
				 lang->untranslated);
		} else {
			full = g_strdup_printf ("%s (%s)",
						name, lang->untranslated);
		}
		g_free (name);
		name = full;
	}

	if (no_group) {
		char *p = strchr (name, '|');
		if (p != NULL) {
			strcpy (name, p+1);
		}
	}

	return name;
}

/* NULL if not found */
char *
gdm_lang_untranslated_name (const char *language,
			    gboolean markup)
{
	Language *lang;
	gboolean clean;

	gdm_lang_init ();

	lang = find_lang (language, &clean);
	if (lang == NULL)
		return NULL;

	if (markup && lang->untranslated != NULL) {
		return g_strdup_printf ("<span lang=\"%s\">%s</span>",
					lang->code,
					lang->untranslated);
	} else {
		return g_strdup (lang->untranslated);
	}
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

	/* paranoia */
	if (l1 == NULL || l2 == NULL)
		return 0;

	if (l1->collate_key == NULL) {
		const char *name;
		name = strchr (_(l1->name), '|');
		if (name != NULL)
			name ++;
		else
			name = _(l1->name);
		l1->collate_key = g_utf8_collate_key (name, -1);
	}

	if (l2->collate_key == NULL) {
		const char *name;
		name = strchr (_(l2->name), '|');
		if (name != NULL)
			name ++;
		else
			name = _(l2->name);
		l2->collate_key = g_utf8_collate_key (name, -1);
	}

	return strcmp (l1->collate_key, l2->collate_key);
}

GList *
gdm_lang_read_locale_file (const char *locale_file)
{
	FILE *langlist;
	char curline[256];
	GList *langs = NULL;
	GHashTable *dupcheck;
	gboolean got_english = FALSE;
	Language *language;
	gboolean clean;
	char *curlocale;

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
		char **lang_list;
		int i;

		if (curline[0] <= ' ' ||
		    curline[0] == '#')
			continue;

		name = strtok (curline, " \t\r\n");
		if (name == NULL)
			continue;

		lang = strtok (NULL, " \t\r\n");
		if (lang == NULL)
			continue;

		lang_list = g_strsplit (lang, ",", -1);
		if (lang_list == NULL)
			continue;

		lang = NULL;
		for (i = 0; lang_list[i] != NULL; i++) {
			if (ve_locale_exists (lang_list[i])) {
				lang = lang_list[i];
				break;
			}
		}
		if (lang == NULL ||
		    g_hash_table_lookup (dupcheck, lang) != NULL) {
			g_strfreev (lang_list);
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
			language->untranslated = NULL;
			g_hash_table_insert (lang_names,
					     language->code,
					     language);
		}

		langs = g_list_prepend (langs, g_strdup (lang));
		g_hash_table_insert (dupcheck, g_strdup (lang),
				     GINT_TO_POINTER (1));

		/* if we have an english locale */
		if (strncmp (lang, "en_", 3) == 0 ||
		    strcmp (lang, "C") == 0)
			got_english = TRUE;

		g_strfreev (lang_list);
	}

	g_hash_table_foreach (dupcheck, (GHFunc) g_free, NULL);
	g_hash_table_destroy (dupcheck);

	/* If we haven't found any english locale, add american
	 * english as that's as much of a fallback as we can get */
	if ( ! got_english)
		langs = g_list_prepend (langs, g_strdup ("en_US"));

	curlocale = setlocale (LC_MESSAGES, NULL);
	if (curlocale != NULL &&
	    strcmp (curlocale, "C") != 0 &&
	    find_lang (curlocale, &clean) == NULL) {
		langs = g_list_prepend (langs, g_strdup (curlocale));
	}

	langs = g_list_sort (langs, lang_collate);

	fclose (langlist);

	return langs;
}

/* EOF */
