struct _Language {
	char *name;
	char *locale;
	char *untranslated_name;
};

static Language languages [] = {
	{ N_("Catalan"), "ca_ES", "Català" },
	{ N_("Chinese (simplified)"), "zh_CN", "中文 (简体)" },
	{ N_("Chinese (traditional)"), "zh_TW", "繁體中文" },
	{ N_("Croatian"), "hr_HR", "Hrvatski" },
	{ N_("Czech"), "cs_CZ", "čeština"},
	{ N_("Danish"), "da_DK", "dansk"},
	{ N_("Dutch"), "nl_NL", "Nederlands" },
	{ N_("English"), "en", "English" },
	{ N_("American English"), "en_US", "American English" },
	{ N_("British English"), "en_GB", "British English" },
	{ N_("Estonian"), "et_EE", "Eesti"},
	{ N_("Finnish"), "fi_FI", "Suomi" },
	{ N_("French"), "fr_FR", "Français" },
	{ N_("Galician"), "gl_ES", "Galego" },
	{ N_("German"), "de_DE", "Deutsch" },
	{ N_("Greek"), "el_GR", "ελληνικά" },
	{ N_("Hebrew"), "iw_IL", "תירבע" },
	{ N_("Hungarian"), "hu_HU" },
	{ N_("Icelandic"), "is_IS" },
	{ N_("Italian"), "it_IT", "Italiano" },
	{ N_("Japanese"), "ja_JP", "日本語" },
	{ N_("Korean"), "ko_KR", "한국어" },
	{ N_("Lithuanian"), "lt_LT", "Lietuvių" },
	{ N_("Norwegian (bokmal)"), "no_NO" },
	{ N_("Norwegian (nynorsk)"), "nn_NO" },
	{ N_("Polish"), "pl_PL", "Polski" },
	{ N_("Portuguese"), "pt_PT", "Português" },
	{ N_("Brazilian Portuguese"), "pt_BR", "Português do Brasil" },
	{ N_("Romanian"), "ro_RO", "Română" },
	{ N_("Russian"), "ru_RU", "русский" },
	{ N_("Slovak"), "sk_SK", "Slovenský"},
	{ N_("Slovenian"), "sl_SI", "Slovenščina" },
	{ N_("Spanish"), "es_ES", "Español" },
	{ N_("Swedish"), "sv_SE", "Svenska" },
	{ N_("Turkish"), "tr_TR", "Türkçe" },
	{ NULL, NULL }
};
