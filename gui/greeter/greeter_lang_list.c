#define N_(x) x
#define _(x) x

struct _Language {
	char *name;
	char *locale;
	char *untranslated_name;
};

static Language languages [] = {
	{ N_("Catalan"), "ca_ES" },
	{ N_("Chinese (simplified)"), "zh_CN" },
	{ N_("Chinese (traditional)"), "zh_TW" },
	{ N_("Croatian"), "hr_HR", "Hrvatski" },
	{ N_("Czech"), "cs_CZ", "čeština "},
	{ N_("Danish"), "da_DK", "dansk"},
	{ N_("Dutch"), "nl_NL" },
	{ N_("English"), "en", "English" },
	{ N_("American English"), "en_US", "American English" },
	{ N_("British English"), "en_GB", "British English" },
	{ N_("Estonian"), "et_EE", "Eesti"},
	{ N_("Finnish"), "fi_FI", "Suomi" },
	{ N_("French"), "fr_FR", "Français" },
	{ N_("Galician"), "gl_ES" },
	{ N_("German"), "de_DE", "Deutsch" },
	{ N_("Greek"), "el_GR", "ελληνικά" },
	{ N_("Hebrew"), "iw_IL", "תירבע" },
	{ N_("Hungarian"), "hu_HU" },
	{ N_("Icelandic"), "is_IS" },
	{ N_("Italian"), "it_IT" },
	{ N_("Japanese"), "ja_JP", "日本語" },
	{ N_("Korean"), "ko_KR" },
	{ N_("Lithuanian"), "lt_LT" },
	{ N_("Norwegian (bokmal)"), "no_NO" },
	{ N_("Norwegian (nynorsk)"), "nn_NO" },
	{ N_("Polish"), "pl_PL" },
	{ N_("Portuguese"), "pt_PT" },
	{ N_("Brazilian Portuguese"), "pt_BR" },
	{ N_("Romanian"), "ro_RO" },
	{ N_("Russian"), "ru_RU", "русский" },
	{ N_("Slovak"), "sk_SK" },
	{ N_("Slovenian"), "sl_SI" },
	{ N_("Spanish"), "es_ES" },
	{ N_("Swedish"), "sv_SE", "Svenska" },
	{ N_("Turkish"), "tr_TR", "Türkçe" },
	{ NULL, NULL }
};
