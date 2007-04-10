/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "gdmlanguages.h"

#include "gdm-socket-protocol.h"

#define LAST_LANGUAGE "Last"
#define DEFAULT_LANGUAGE "Default"

static GtkWidget    *tv                       = NULL;
static GtkListStore *lang_model               = NULL;
static GtkWidget    *dialog                   = NULL;
static gchar        *current_language         = NULL;
static gchar        *dialog_selected_language = NULL;
static gint          savelang                 = GTK_RESPONSE_NO;
static gboolean      always_restart           = FALSE;

#include "gdm-common.h"

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
	{ N_("A-M|Afrikaans"), "af_ZA", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Albanian"), "sq_AL", "Shqip", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Amharic"), "am_ET", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Arabic (Egypt)"), "ar_EG", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Arabic (Lebanon)"), "ar_LB", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Arabic (Saudi Arabia)"), "ar_SA", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Armenian"), "hy_AM", "Հայերեն", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Azerbaijani"), "az_AZ", "Azərbaycanca", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Basque"), "eu_ES", "Euskara", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Belarusian"), "be_BY", "Беларуская мова", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Bengali"), "bn_BD", "বাংলা", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Bengali (India)"), "bn_IN", "বাংলা", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Bulgarian"), "bg_BG", "Български", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Bosnian"), "bs_BA", "Bosanski", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Catalan"), "ca_ES", "Català", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (China Mainland)"), "zh_CN", "中文 (中国大陆)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (Hong Kong)"), "zh_HK", "中文 (香港)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (Singapore)"), "zh_SG", "中文 (新加坡)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Chinese (Taiwan)"), "zh_TW", "中文 (台灣)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Croatian"), "hr_HR", "Hrvatski", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Czech"), "cs_CZ", "čeština", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Danish"), "da_DK", "dansk", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Dutch"), "nl_NL", "Nederlands", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Dutch (Belgium)"), "nl_BE", "Nederlands (België)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (USA)"), "en_US", "American English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Australia)"), "en_AU", "Australian English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (UK)"), "en_GB", "British English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Canada)"), "en_CA", "Canadian English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Ireland)"), "en_IE", "Irish English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Denmark)"), "en_DK", "Danish English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (South Africa)"), "en_ZA", "South African English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (Malta)"), "en_MT", "Maltese English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|English (New Zealand)"), "en_NZ", "New Zealand English", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Estonian"), "et_EE", "Eesti", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Finnish"), "fi_FI", "Suomi", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French"), "fr_FR", "Français", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French (Belgium)"), "fr_BE", "Français (Belgique)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French (Canada)"), "fr_CA", "Français (Canada)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French (Luxembourg)"), "fr_LU", "Français (Luxembourg)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|French (Switzerland)"), "fr_CH", "Français (Suisse)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Galician"), "gl_ES", "Galego", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German"), "de_DE", "Deutsch", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German (Austria)"), "de_AT", "Deutsch (Österreich)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German (Luxembourg)"), "de_LU", "Deutsch (Luxemburg)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|German (Switzerland)"), "de_CH", "Deutsch (Schweiz)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Greek"), "el_GR", "Ελληνικά", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Greek (Cyprus)"), "el_CY", "Ελληνικά (Κύπρος)", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Gujarati"), "gu_IN", "ગુજરાતી", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hebrew"), "he_IL", "עברית", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hebrew"), "iw_IL", "עברית", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hindi"), "hi_IN", "हिंदी", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Hungarian"), "hu_HU", "Magyar", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Icelandic"), "is_IS", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Indonesian"), "id_ID", "Bahasa Indonesia", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Interlingua"), "ia", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Irish"), "ga_IE", "Gaeilge", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Italian"), "it_IT", "Italiano", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Japanese"), "ja_JP", "日本語", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Kannada"), "kn_IN", "﻿ಕನ್ನಡ", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Kinyarwanda"), "rw_RW", "Kinyarwanda", 0 },
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
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Malayalam"), "ml_IN", "മലയാളം", 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Maltese"), "mt_MT", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Marathi"), "mr_IN", NULL, 0 },
	/*Note translate the A-M to the A-M you used in the group label */
	{ N_("A-M|Mongolian"), "mn_MN", "Монгол", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Northern Sotho"), "nso_ZA", "Sesotho sa Leboa", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (bokmal)"), "no_NO", "Norsk (bokmål)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Norwegian (nynorsk)"), "nn_NO", "Norsk (nynorsk)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Oriya"), "or_IN", NULL, 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Punjabi"), "pa_IN", NULL, 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Persian"), "fa_IR", "فارسی", 0 },
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
	{ N_("N-Z|Serbian"), "sr_YU", "српски", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Serbia & Montenegro)"), "sr_CS", "српски", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Montenegro)"), "sr_ME", "српски", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Serbia)"), "sr_RS", "српски", 0 },	
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Latin)"), "sr_CS@Latn", "srpski (latinica)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Jekavian)"), "sr_CS@ije", "српски (ијекавски)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Serbian (Bosnia)"), "sh_BA", "srpski (Bosna i Hercegovina)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovak"), "sk_SK", "Slovenský", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Slovenian"), "sl_SI", "Slovenščina", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish"), "es_ES", "Español", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Argentina)"), "es_AR", "Español (Argentina)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Bolivia)"), "es_BO", "Español (Bolivia)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Chile)"), "es_CL", "Español (Chile)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Colombia)"), "es_CO", "Español (Colombia)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Costa Rica)"), "es_CR", "Español (Costa Rica)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Ecuador)"), "es_EC", "Español (Ecuador)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Guatemala)"), "es_GT", "Español (Guatemala)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Mexico)"), "es_MX", "Español (México)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Nicaragua)"), "es_NI", "Español (Nicaragua)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Panama)"), "es_PA", "Español (Panamá)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Peru)"), "es_PE", "Español (Perú)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Paraguay)"), "es_PY", "Español (Paraguay)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (El Salvador)"), "es_SV", "Español (El Salvador)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Uruguay)"), "es_UY", "Español (Uruguay)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Spanish (Venezuela)"), "es_VE", "Español (Venezuela)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Swedish"), "sv_SE", "Svenska", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Swedish (Finland)"), "sv_FI", "Svenska (Finland)", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Tamil"), "ta_IN", NULL, 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Telugu"), "te_IN", NULL, 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Thai"), "th_TH", "ไทย", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Turkish"), "tr_TR", "Türkçe", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Ukrainian"), "uk_UA", "Українська", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Vietnamese"), "vi_VN", "Việt Nam", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Walloon"), "wa_BE", "Walon", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Welsh"), "cy_GB", "Cymraeg", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Xhosa"), "xh_ZA", "isiXhosa", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Yiddish"), "yi", "ייִדיש", 0 },
	/*Note translate the N-Z to the N-Z you used in the group label */
	{ N_("N-Z|Zulu"), "zu_ZA", "isiZulu", 0 },
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

	for (i = 0; languages[i].name != NULL; i++) {
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
		char *mod = p+1;
		*p = '\0';

		/* attempt lookup without encoding but with the
		   modifier first */
		p = strrchr (name, '.');
		if (p != NULL) {
			char *noenc;
			*p = '\0';

			noenc = g_strconcat (name, "@", mod, NULL);
			lang = g_hash_table_lookup (lang_names, noenc);
			if (lang != NULL) {
				g_free (name);
				g_free (noenc);
				return lang;
			}
			g_free (noenc);

			*p = '.';
		}

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
	if (encoding == NULL)
	  encoding = strchr (language, '@'); /* treat a modifier without a codeset as an encoding */
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
		full = g_strdup_printf ("%s (%s)",
			name, lang->untranslated);
		g_free (name);
		name = full;
	}

	if (no_group) {
		char *p = strchr (name, '|');
		if (p != NULL) {
			p = g_strdup (p + 1);
			g_free (name);
			name = p;
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

	return g_strdup (lang->untranslated);
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
			name++;
		else
			name = _(l1->name);
		l1->collate_key = g_utf8_collate_key (name, -1);
	}

	if (l2->collate_key == NULL) {
		const char *name;
		name = strchr (_(l2->name), '|');
		if (name != NULL)
			name++;
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
	char *getsret;
	char *p;

	if (locale_file == NULL)
		return NULL;

	VE_IGNORE_EINTR (langlist = fopen (locale_file, "r"));

	if (langlist == NULL)
		return NULL;

	gdm_lang_init ();

	dupcheck = g_hash_table_new (g_str_hash, g_str_equal);

	for (;;) {
		char *name;
		char *lang;
		char **lang_list;
		int i;

		VE_IGNORE_EINTR (getsret = fgets (curline, sizeof (curline), langlist));
		if (getsret == NULL)
			break;

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
			/* add a space before an open bracket to match
			   the style used in the internal list.
			   e.g. change "English(India)" to "English (India)" */
			p = strchr (name, '(');
			if (p != NULL && p > name && *(p-1) != ' ') {
			  *p = 0;
			  language->name= g_strconcat (name, " (", p+1, NULL);
			} else
			  language->name = g_strdup (name);
 			/* only store the "lang_country" part of the locale code, so that we notice
 			 * if there is more than one encoding of this language. See bug 132629. */
			p = strchr (lang, '.');
 			if (p == NULL)
 			  p = strchr (lang, '@');
 			if (p != NULL)
 			  language->code = g_strndup (lang, (p - lang));
 			else
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

	langs = g_list_sort (langs, lang_collate);

	VE_IGNORE_EINTR (fclose (langlist));

	return langs;
}

GtkListStore *
gdm_lang_get_model (void)
{
   return lang_model;
}

void
gdm_lang_initialize_model (gchar * locale_file)
{
  GList *list, *li;
  GtkTreeIter iter;

  list = gdm_lang_read_locale_file (locale_file);

  lang_model = gtk_list_store_new (NUM_COLUMNS,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING);

  gtk_list_store_append (lang_model, &iter);
  gtk_list_store_set (lang_model, &iter,
		      TRANSLATED_NAME_COLUMN, _("Last language"),
		      UNTRANSLATED_NAME_COLUMN, NULL,
		      LOCALE_COLUMN, LAST_LANGUAGE,
		      -1);

  gtk_list_store_append (lang_model, &iter);
  gtk_list_store_set (lang_model, &iter,
		      TRANSLATED_NAME_COLUMN, _("System Default"),
		      UNTRANSLATED_NAME_COLUMN, NULL,
		      LOCALE_COLUMN, DEFAULT_LANGUAGE,
		      -1);

  for (li = list; li != NULL; li = li->next)
    {
      char *lang = li->data;
      char *name;
      char *untranslated;

      li->data = NULL;

      name = gdm_lang_name (lang,
			    FALSE /* never_encoding */,
			    TRUE /* no_group */,
			    FALSE /* untranslated */,
			    FALSE /* markup */);

      untranslated = gdm_lang_untranslated_name (lang,
						 TRUE /* markup */);

      gtk_list_store_append (lang_model, &iter);
      gtk_list_store_set (lang_model, &iter,
			  TRANSLATED_NAME_COLUMN, name,
			  UNTRANSLATED_NAME_COLUMN, untranslated,
			  LOCALE_COLUMN, lang,
			  -1);

      g_free (name);
      g_free (untranslated);
      g_free (lang);
    }
  g_list_free (list);
}

gint
gdm_lang_get_savelang_setting (void)
{
  return savelang;
}

gchar *
gdm_lang_check_language (const char *old_language)
{
  gchar *retval = NULL;

  /* Don't save language unless told otherwise */
  savelang = GTK_RESPONSE_NO;

  if (old_language == NULL)
    old_language = "";

  /* If a different language is selected */
  if (current_language != NULL && strcmp (current_language, LAST_LANGUAGE) != 0)
    {
      if (strcmp (current_language, DEFAULT_LANGUAGE) == 0)
	retval = g_strdup ("");
      else
        retval = g_strdup (current_language);

      /* User's saved language is not the chosen one */
      if (strcmp (old_language, retval) != 0)
	{
	  gchar *primary_message;
	  gchar *secondary_message;
	  char *current_name, *saved_name;

	  if (strcmp (current_language, DEFAULT_LANGUAGE) == 0)
	    current_name = g_strdup (_("System Default"));
	  else
	    current_name = gdm_lang_name (current_language,
					  FALSE /* never_encoding */,
					  TRUE /* no_group */,
					  TRUE /* untranslated */,
					  TRUE /* markup */);
	  if (strcmp (old_language, "") == 0)
	    saved_name = g_strdup (_("System Default"));
	  else
	    saved_name = gdm_lang_name (old_language,
					FALSE /* never_encoding */,
					TRUE /* no_group */,
					TRUE /* untranslated */,
					TRUE /* markup */);

	  primary_message = g_strdup_printf (_("Do you wish to make %s the default for future sessions?"),
	                                     current_name);
 	  secondary_message = g_strdup_printf (_("You have chosen %s for this session, but your default setting is "
	                                         "%s."), current_name, saved_name);
	  g_free (current_name);
	  g_free (saved_name);

	  savelang = gdm_wm_query_dialog (primary_message, secondary_message,
		_("Make _Default"), _("Just For _This Session"), TRUE);
	  g_free (primary_message);
	  g_free (secondary_message);
	}
    }
  else
    {
      retval = g_strdup (old_language);
    }

  return retval;
}

static void
selection_changed (GtkTreeSelection *selection,
		   gpointer          data)
{
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      g_free (dialog_selected_language);
      gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter, LOCALE_COLUMN, &dialog_selected_language, -1);
    }
}

static void
tree_row_activated (GtkTreeView         *view,
                    GtkTreePath         *path,
                    GtkTreeViewColumn   *column,
                    gpointer            data)
{
  GtkTreeIter iter;
  if (gtk_tree_model_get_iter (GTK_TREE_MODEL (lang_model), &iter, path))
    {
      g_free (dialog_selected_language);
      gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter,
			  LOCALE_COLUMN, &dialog_selected_language,
			  -1);
      gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  }
}

static void
gdm_lang_setup_treeview (void)
{
  if (dialog == NULL)
    {
      GtkWidget *main_vbox;
      GtkWidget *button;
      GtkWidget **tmp_p;
      GtkWidget *swindow;
      GtkWidget *label;
      char *s;

      dialog = gtk_dialog_new_with_buttons (_("Select a Language"),
#ifdef TODO
					    GTK_WINDOW (parent_window),
#endif
					    NULL,
					    0,
					    GTK_STOCK_CANCEL,
					    GTK_RESPONSE_CANCEL,
					    NULL);
					    
      button = gtk_button_new_with_mnemonic (_("Change _Language"));
      GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
      gtk_widget_show (button);
      gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
                                    GTK_RESPONSE_OK);
					    
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
      gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
      gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

      main_vbox = gtk_vbox_new (FALSE, 6);
      gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                          main_vbox, TRUE, TRUE, 0);
  
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_OK);
      /* evil gcc warnings */
      tmp_p = &dialog;
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *)tmp_p);
      s = g_strdup (_("_Select the language for your session to use:"));
      label = gtk_label_new_with_mnemonic (s);
      gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
      g_free (s);
      gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
      gtk_box_pack_start (GTK_BOX (main_vbox),
			  label, FALSE, FALSE, 0);
      tv = gtk_tree_view_new ();
      gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tv), TRUE);
      gtk_label_set_mnemonic_widget (GTK_LABEL (label), tv);
      /* FIXME: we should handle this better, but things really look
       * bad if we aren't always LTR */
      gtk_widget_set_direction (tv, GTK_TEXT_DIR_LTR);
      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv), FALSE);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tv),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "text", TRANSLATED_NAME_COLUMN,
					       NULL);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tv),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "markup",
					       UNTRANSLATED_NAME_COLUMN,
					       NULL);
      swindow = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (swindow), GTK_SHADOW_IN);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
      gtk_container_add (GTK_CONTAINER (swindow), tv);
      gtk_box_pack_start (GTK_BOX (main_vbox),
			  swindow, TRUE, TRUE, 0);
      gtk_window_set_default_size (GTK_WINDOW (dialog),
				   MIN (400, gdm_wm_screen.width),
				   MIN (600, gdm_wm_screen.height));
      g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (tv))),
			"changed",
			(GCallback) selection_changed,
			NULL);
      g_signal_connect (G_OBJECT (tv),
                        "row_activated",
                        (GCallback) tree_row_activated,
                        NULL);
      gtk_tree_view_set_model (GTK_TREE_VIEW (tv),
			       GTK_TREE_MODEL (lang_model));
    }
}

gint
gdm_lang_ask_restart (gchar *language)
{
	GtkWidget *dialog;
	gchar *firstmsg;
	gchar *secondmsg;
	gchar *login;
	gint response = GTK_RESPONSE_NO;

	if (always_restart)
		return GTK_RESPONSE_YES;

	login = _("the login screen");
	firstmsg = g_strdup_printf (_("Do you wish to restart %s with the chosen language?"),
	                            login);
	secondmsg = g_strdup_printf (_("You will restart %s with the %s locale."),
	                             login,
	                             language);

	response = gdm_wm_query_dialog (firstmsg, secondmsg, _("_Yes"), _("_No"), FALSE);

	g_free (firstmsg);
	g_free (secondmsg);

	return response;
}

static gboolean
gdm_lang_get_restart_state (void)
{
  return  always_restart;
}

static void
gdm_lang_set_restart_state (gboolean do_restart)
{
  always_restart = do_restart;
}

static void
gdm_lang_restart_handler (GtkMenuItem *menu_item, gpointer user_data)
{
  if ((int)user_data == ALWAYS_RESTART)
    always_restart = TRUE;
  else
    always_restart = FALSE;
}

static gchar *
gdm_lang_get (void)
{
   return (current_language);
}

void
gdm_lang_set_restart_dialog (char *language)
{
   /*
    * Don't do anything if the language is already set to
    * this value.
    */
   if (current_language == NULL ||
      (current_language != NULL &&
       strcmp (current_language, language) != 0))
     {
       gint response = GTK_RESPONSE_YES;

       if (strcmp (language, LAST_LANGUAGE))
         response = gdm_lang_ask_restart (language);

       gdm_lang_set (language);

       if (strcmp (language, LAST_LANGUAGE) &&
          (response == GTK_RESPONSE_YES))
         {
           printf ("%c%c%c%c%s\n", STX,
                   BEL,
                   GDM_INTERRUPT_SELECT_LANG,
                   response == GTK_RESPONSE_YES ? 1 : 0,
                   language);
           fflush (stdout);

         }
     }
}

void
gdm_lang_set (char *language)
{
   char *locale;
   GtkTreeSelection *selection;
   GtkTreeIter iter = {0};

   g_free (current_language);
   current_language = g_strdup (language);

   if (dialog == NULL)
     gdm_lang_setup_treeview ();

   if (language == NULL)
      return;
 
   lang_set_custom_callback (language);

   selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
   gtk_tree_selection_unselect_all (selection);

   if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (lang_model), &iter)) {
      do {
         gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter, LOCALE_COLUMN, &locale, -1);
         if (locale != NULL && strcmp (locale, language) == 0) {
            GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (lang_model), &iter);

            gtk_tree_selection_select_iter (selection, &iter);
            gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv), path, NULL, FALSE, 0.0, 0.0);
            gtk_tree_path_free (path);
            break;
         }
      } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (lang_model), &iter));
   }
}

/*
 * The button with this handler appears in the F10 menu, so it
 * cannot depend on callback data being passed in.
 */
void
gdm_lang_handler (gpointer user_data)
{
  if (dialog == NULL)
    gdm_lang_setup_treeview ();

  gtk_widget_show_all (dialog);
  gdm_wm_center_window (GTK_WINDOW (dialog));

  gdm_wm_no_login_focus_push ();
  if (tv != NULL)
    {
      GtkTreeSelection *selection;
	  
      gtk_widget_show_now (dialog);
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
      if (selection == NULL)
	gtk_tree_selection_select_path (selection, gtk_tree_path_new_first ());
      else
        {
          GtkTreeIter iter;
          GtkTreePath *path;
          GtkTreeModel *tm = GTK_TREE_MODEL (lang_model);

          gtk_tree_selection_get_selected (selection, &tm, &iter);
          path = gtk_tree_model_get_path (GTK_TREE_MODEL (lang_model), &iter);
          gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv), path, NULL, FALSE, 0.0, 0.0);
          gtk_tree_path_free (path);
        }
    }
  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_OK:
      if (dialog_selected_language)
        gdm_lang_set_restart_dialog (dialog_selected_language);

      break;
    case GTK_RESPONSE_CANCEL:
    default:
      break;
    }

  gdm_wm_no_login_focus_pop ();

  if (dialog)
    gtk_widget_hide (dialog);
}

int
gdm_lang_op_lang (const gchar *args)
{
  gchar *language = gdm_lang_check_language (args);

  if (gdm_lang_get_savelang_setting () == GTK_RESPONSE_CANCEL)
    printf ("%c%s\n", STX, GDM_RESPONSE_CANCEL);
  else
    printf ("%c%s\n", STX, language);
  fflush (stdout);
  g_free (language);

  return 0;
}

int
gdm_lang_op_slang (const gchar *args)
{
  if (gdm_lang_get_savelang_setting () == GTK_RESPONSE_YES)
    printf ("%cY\n", STX);
  else
    printf ("%c\n", STX);
  fflush (stdout);

  return 0;
}

int
gdm_lang_op_setlang (const gchar *args)
{
  if (args)
    gdm_lang_set ((char*)args);
  printf ("%c\n", STX);
  fflush (stdout);

  return 0;
}

int
gdm_lang_op_always_restart (const gchar *args)
{
  if (!args)
    ;
  else if (g_ascii_strcasecmp (args, "y") == 0)
    gdm_lang_set_restart_state (TRUE);
  else if (g_ascii_strcasecmp (args, "n") == 0)
    gdm_lang_set_restart_state (FALSE);
  if (args)
    gdm_common_error ("test %s", args);

  printf ("%c\n", STX);
  fflush (stdout);

  return 0;
}

