/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1998, 1999, 2000 Martin K, Petersen <mkp@mkp.net>
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gdm-language-chooser-widget.h"

#include <langinfo.h>
#ifndef __LC_LAST
#define __LC_LAST       13
#endif
#include "locarchive.h"

#define ALIASES_FILE LIBLOCALEDIR "/locale.alias"
#define ARCHIVE_FILE LIBLOCALEDIR "/locale-archive"
#define ISO_CODES_DATADIR ISO_CODES_PREFIX "/share/xml/iso-codes"
#define ISO_CODES_LOCALESDIR ISO_CODES_PREFIX "/share/locale"

#define GDM_LANGUAGE_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LANGUAGE_CHOOSER_WIDGET, GdmLanguageChooserWidgetPrivate))

typedef struct _GdmChooserLocale {
        char *name;
        char *language_code;
        char *territory_code;
        char *title;
        char *language;
        char *territory;
} GdmChooserLocale;

struct GdmLanguageChooserWidgetPrivate
{
        GtkWidget          *treeview;

        GHashTable         *languages;
        GHashTable         *territories;
        GHashTable         *available_locales;

        GdmChooserLocale   *current_locale;
};

enum {
        PROP_0,
};

enum {
        LANGUAGE_ACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_language_chooser_widget_class_init  (GdmLanguageChooserWidgetClass *klass);
static void     gdm_language_chooser_widget_init        (GdmLanguageChooserWidget      *language_chooser_widget);
static void     gdm_language_chooser_widget_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmLanguageChooserWidget, gdm_language_chooser_widget, GTK_TYPE_VBOX)

enum {
        CHOOSER_LIST_TITLE_COLUMN = 0,
        CHOOSER_LIST_TRANSLATED_COLUMN,
        CHOOSER_LIST_LOCALE_COLUMN
};

static void
chooser_locale_free (GdmChooserLocale *locale)
{
        if (locale == NULL) {
                return;
        }

        g_free (locale->name);
        g_free (locale->title);
        g_free (locale->language);
        g_free (locale->territory);
        g_free (locale);
}

char *
gdm_language_chooser_widget_get_current_language_name (GdmLanguageChooserWidget *widget)
{
        char *language_name;

        g_return_val_if_fail (GDM_IS_LANGUAGE_CHOOSER_WIDGET (widget), NULL);

        language_name = NULL;
        if (widget->priv->current_locale != NULL) {

        }

        return language_name;
}

static void
gdm_language_chooser_widget_set_property (GObject        *object,
                                          guint           prop_id,
                                          const GValue   *value,
                                          GParamSpec     *pspec)
{
        GdmLanguageChooserWidget *self;

        self = GDM_LANGUAGE_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_language_chooser_widget_get_property (GObject        *object,
                                          guint           prop_id,
                                          GValue         *value,
                                          GParamSpec     *pspec)
{
        GdmLanguageChooserWidget *self;

        self = GDM_LANGUAGE_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_language_chooser_widget_constructor (GType                  type,
                                         guint                  n_construct_properties,
                                         GObjectConstructParam *construct_properties)
{
        GdmLanguageChooserWidget      *language_chooser_widget;
        GdmLanguageChooserWidgetClass *klass;

        klass = GDM_LANGUAGE_CHOOSER_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_LANGUAGE_CHOOSER_WIDGET));

        language_chooser_widget = GDM_LANGUAGE_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_language_chooser_widget_parent_class)->constructor (type,
                                                                                                                                       n_construct_properties,
                                                                                                                                       construct_properties));

        return G_OBJECT (language_chooser_widget);
}

static void
gdm_language_chooser_widget_dispose (GObject *object)
{
        GdmLanguageChooserWidget *widget;

        widget = GDM_LANGUAGE_CHOOSER_WIDGET (object);

        if (widget->priv->languages != NULL) {
                g_hash_table_destroy (widget->priv->languages);
                widget->priv->languages = NULL;
        }

        if (widget->priv->territories != NULL) {
                g_hash_table_destroy (widget->priv->territories);
                widget->priv->territories = NULL;
        }

        if (widget->priv->available_locales != NULL) {
                g_hash_table_destroy (widget->priv->available_locales);
                widget->priv->available_locales = NULL;
        }

        widget->priv->current_locale = NULL;

        G_OBJECT_CLASS (gdm_language_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_language_chooser_widget_class_init (GdmLanguageChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_language_chooser_widget_get_property;
        object_class->set_property = gdm_language_chooser_widget_set_property;
        object_class->constructor = gdm_language_chooser_widget_constructor;
        object_class->dispose = gdm_language_chooser_widget_dispose;
        object_class->finalize = gdm_language_chooser_widget_finalize;

        signals [LANGUAGE_ACTIVATED] = g_signal_new ("language-activated",
                                                     G_TYPE_FROM_CLASS (object_class),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GdmLanguageChooserWidgetClass, language_activated),
                                                     NULL,
                                                     NULL,
                                                     g_cclosure_marshal_VOID__VOID,
                                                     G_TYPE_NONE,
                                                     0);

        g_type_class_add_private (klass, sizeof (GdmLanguageChooserWidgetPrivate));
}

static void
on_language_selected (GtkTreeSelection     *selection,
                      GdmLanguageChooserWidget *widget)
{
        GtkTreeModel     *model = NULL;
        GtkTreeIter       iter = {0};
        GdmChooserLocale *locale;

        locale = NULL;

        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                gtk_tree_model_get (model, &iter, CHOOSER_LIST_LOCALE_COLUMN, &locale, -1);
        }

        widget->priv->current_locale = locale;
}

static gboolean
locale_exists (const char *loc)
{
        gboolean ret;
        char    *old;

        old = g_strdup (setlocale (LC_MESSAGES, NULL));
        if (setlocale (LC_MESSAGES, loc) != NULL) {
                ret = TRUE;
        } else {
                ret = FALSE;
        }

        setlocale (LC_MESSAGES, old);
        g_free (old);

        return ret;
}

static char *
utf8_convert (const char *str,
              int         len)
{
        char *utf8;

        utf8 = g_locale_to_utf8 (str, len, NULL, NULL, NULL);

        /* if we couldn't convert text from locale then
         * assume utf-8 and hope for the best */
        if (utf8 == NULL) {
                char *p;
                char *q;

                if (len < 0) {
                        utf8 = g_strdup (str);
                } else {
                        utf8 = g_strndup (str, len);
                }

                p = utf8;
                while (*p != '\0' && !g_utf8_validate ((const char *)p, -1, (const char **)&q)) {
                        *q = '?';
                        p = q + 1;
                }
        }

        return utf8;
}

/* Magic number at the beginning of a locale data file for CATEGORY.  */
#define LIMAGIC(category) \
  (category == LC_COLLATE                                               \
   ? ((unsigned int) (0x20051014 ^ (category)))                         \
   : ((unsigned int) (0x20031115 ^ (category))))


/* This seems to be specified by ISO/IEC 14652 */
static void
get_lc_identification (GdmChooserLocale *locale,
                       void             *data,
                       gsize             size)
{
        struct {
                unsigned int magic;
                unsigned int nstrings;
                unsigned int strindex[0];
        } *filedata = data;

#ifdef LC_IDENTIFICATION
        if (filedata->magic == LIMAGIC (LC_IDENTIFICATION)
            && (sizeof *filedata + (filedata->nstrings * sizeof (unsigned int)) <= size)) {

#define GET_HANDLE(idx) ((char *) data + filedata->strindex[_NL_ITEM_INDEX (_NL_IDENTIFICATION_##idx)])

                locale->title = utf8_convert (GET_HANDLE (TITLE), -1);
                locale->language = utf8_convert (GET_HANDLE (LANGUAGE), -1);
                locale->territory = utf8_convert (GET_HANDLE (TERRITORY), -1);
        }
#endif
}

static char *
get_short_name_for_locale (const char *name)
{
        char *short_name;
        char *p;

        p = strrchr (name, '.');
        if (p != NULL) {
                short_name = utf8_convert (name, p - name);
        } else {
                short_name = utf8_convert (name, -1);
        }

        return short_name;
}

static void
parse_short_name (const char *short_name,
                  char      **language_codep,
                  char      **territory_codep)
{
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;

        error = NULL;
        re = g_regex_new ("([a-zA-Z]+)(_([a-zA-Z]+))?", 0, 0, &error);
        if (re == NULL) {
                g_critical (error->message);
        }

        g_regex_match (re, short_name, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse locale: %s", short_name);
                return;
        }

        if (language_codep != NULL) {
                *language_codep = g_match_info_fetch (match_info, 1);
        }

        if (territory_codep != NULL) {
                *territory_codep = g_match_info_fetch (match_info, 3);
        }

        g_match_info_free (match_info);
        g_regex_unref (re);
}

struct nameent
{
        char    *name;
        uint32_t locrec_offset;
};

static int
nameentcmp (const void *a, const void *b)
{
        return strcoll (((const struct nameent *) a)->name,
                        ((const struct nameent *) b)->name);
}

static void
collect_locales_from_archive (GdmLanguageChooserWidget *widget)
{
        GMappedFile        *mapped;
        GError             *error;
        char               *addr;
        struct locarhead   *head;
        struct namehashent *namehashtab;
        struct nameent     *names;
        uint32_t            used;
        uint32_t            cnt;
        gsize               len;

        error = NULL;
        mapped = g_mapped_file_new (ARCHIVE_FILE, FALSE, &error);
        if (mapped == NULL) {
                g_warning ("Mapping failed for %s: %s", ARCHIVE_FILE, error->message);
                g_error_free (error);
                return;
        }

        addr = g_mapped_file_get_contents (mapped);
        len = g_mapped_file_get_length (mapped);

        head = (struct locarhead *) addr;
        if (head->namehash_offset + head->namehash_size > len
            || head->string_offset + head->string_size > len
            || head->locrectab_offset + head->locrectab_size > len
            || head->sumhash_offset + head->sumhash_size > len) {
                goto out;
        }

        namehashtab = (struct namehashent *) (addr + head->namehash_offset);

        names = (struct nameent *) g_new0 (struct nameent, head->namehash_used);
        for (cnt = used = 0; cnt < head->namehash_size; ++cnt) {
                if (namehashtab[cnt].locrec_offset != 0) {
                        names[used].name = addr + namehashtab[cnt].name_offset;
                        names[used++].locrec_offset = namehashtab[cnt].locrec_offset;
                }
        }

        /* Sort the names.  */
        qsort (names, used, sizeof (struct nameent), nameentcmp);

        for (cnt = 0; cnt < used; ++cnt) {
                struct locrecent *locrec;
                char             *short_name;
                GdmChooserLocale *locale;

                short_name = get_short_name_for_locale (names[cnt].name);

                if (g_hash_table_lookup (widget->priv->available_locales, short_name) != NULL) {
                        g_free (short_name);
                        continue;
                }

                locale = g_new0 (GdmChooserLocale, 1);
                locale->name = short_name;

                parse_short_name (short_name, &locale->language_code, &locale->territory_code);

                locrec = (struct locrecent *) (addr + names[cnt].locrec_offset);

#ifdef LC_IDENTIFICATION
                get_lc_identification (locale,
                                       addr + locrec->record[LC_IDENTIFICATION].offset,
                                       locrec->record[LC_IDENTIFICATION].len);
#endif

                g_hash_table_insert (widget->priv->available_locales, g_strdup (short_name), locale);
        }

        g_free (names);

 out:

        g_mapped_file_free (mapped);
}

static int
select_dirs (const struct dirent *dirent)
{
        int result = 0;

        if (strcmp (dirent->d_name, ".") != 0 && strcmp (dirent->d_name, "..") != 0) {
                mode_t mode = 0;

#ifdef _DIRENT_HAVE_D_TYPE
                if (dirent->d_type != DT_UNKNOWN && dirent->d_type != DT_LNK) {
                        mode = DTTOIF (dirent->d_type);
                } else
#endif
                        {
                                struct stat st;
                                char       *path;

                                path = g_build_filename (LIBLOCALEDIR, dirent->d_name, NULL);
                                if (g_stat (path, &st) == 0) {
                                        mode = st.st_mode;
                                }
                                g_free (path);
                        }

                result = S_ISDIR (mode);
        }

        return result;
}

static void
collect_locales_from_directory (GdmLanguageChooserWidget *widget)
{
        struct dirent **dirents;
        int             ndirents;
        int             cnt;

        ndirents = scandir (LIBLOCALEDIR, &dirents, select_dirs, alphasort);

        for (cnt = 0; cnt < ndirents; ++cnt) {
                char             *path;
                char             *short_name;
                GdmChooserLocale *locale;
                gboolean          res;

                /* first find short name */
                short_name = get_short_name_for_locale (dirents[cnt]->d_name);

                if (g_hash_table_lookup (widget->priv->available_locales, short_name) != NULL) {
                        g_free (short_name);
                        continue;
                }

                locale = g_new0 (GdmChooserLocale, 1);
                locale->name = short_name;

                parse_short_name (short_name, &locale->language_code, &locale->territory_code);

                /* try to get additional information from LC_IDENTIFICATION */
                path = g_build_filename (LIBLOCALEDIR, dirents[cnt]->d_name, "LC_IDENTIFICATION", NULL);
                res = g_file_test (path, G_FILE_TEST_IS_REGULAR);
                if (res) {
                        GMappedFile      *mapped;
                        GError           *error;

                        error = NULL;
                        mapped = g_mapped_file_new (path, FALSE, &error);
                        if (mapped == NULL) {
                                g_warning ("Mapping failed for %s: %s", path, error->message);
                                g_error_free (error);
                        } else {
                                get_lc_identification (locale,
                                                       g_mapped_file_get_contents (mapped),
                                                       g_mapped_file_get_length (mapped));
                                g_mapped_file_free (mapped);
                        }
                }
                g_free (path);

                g_hash_table_insert (widget->priv->available_locales, g_strdup (short_name), locale);
        }

        if (ndirents > 0) {
                free (dirents);
        }
}

static void
collect_locales_from_aliases (GdmLanguageChooserWidget *widget)
{
        /* FIXME: */
}

static void
collect_locales (GdmLanguageChooserWidget *widget)
{
        collect_locales_from_archive (widget);
        collect_locales_from_directory (widget);
        collect_locales_from_aliases (widget);
}


static void
on_row_activated (GtkTreeView          *tree_view,
                  GtkTreePath          *tree_path,
                  GtkTreeViewColumn    *tree_column,
                  GdmLanguageChooserWidget *widget)
{
        g_signal_emit (widget, signals[LANGUAGE_ACTIVATED], 0);
}

static const char *
get_translated_language (GdmLanguageChooserWidget *widget,
                         const char               *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (widget->priv->languages, code);

        if (name != NULL) {
                return dgettext ("iso_639", name);
        }

        return NULL;
}

static const char *
get_language (GdmLanguageChooserWidget *widget,
              const char               *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (widget->priv->languages, code);

        return name;
}

static const char *
get_territory (GdmLanguageChooserWidget *widget,
               const char               *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (widget->priv->territories, code);

        return name;
}

static const char *
get_translated_territory (GdmLanguageChooserWidget *widget,
                          const char               *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (widget->priv->territories, code);

        if (name != NULL) {
                return dgettext ("iso_3166", name);
        }

        return NULL;
}

static void
languages_parse_start_tag (GMarkupParseContext      *ctx,
                            const char               *element_name,
                            const char              **attr_names,
                            const char              **attr_values,
                            GdmLanguageChooserWidget *widget,
                            GError                  **error)
{
        const char *ccode_longB;
        const char *ccode_longT;
        const char *ccode;
        const char *lang_name;

        if (! g_str_equal (element_name, "iso_639_entry") || attr_names == NULL || attr_values == NULL) {
                return;
        }

        ccode = NULL;
        ccode_longB = NULL;
        ccode_longT = NULL;
        lang_name = NULL;

        while (*attr_names && *attr_values) {
                if (g_str_equal (*attr_names, "iso_639_1_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 2) {
                                        return;
                                }
                                ccode = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "iso_639_2B_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ccode_longB = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "iso_639_2T_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ccode_longT = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "name")) {
                        lang_name = *attr_values;
                }

                ++attr_names;
                ++attr_values;
        }

        if (lang_name == NULL) {
                return;
        }

        if (ccode != NULL) {
                g_hash_table_insert (widget->priv->languages,
                                     g_strdup (ccode),
                                     g_strdup (lang_name));
        }
        if (ccode_longB != NULL) {
                g_hash_table_insert (widget->priv->languages,
                                     g_strdup (ccode_longB),
                                     g_strdup (lang_name));
        }
        if (ccode_longT != NULL) {
                g_hash_table_insert (widget->priv->languages,
                                     g_strdup (ccode_longT),
                                     g_strdup (lang_name));
        }
}

static void
territories_parse_start_tag (GMarkupParseContext      *ctx,
                             const char               *element_name,
                             const char              **attr_names,
                             const char              **attr_values,
                             GdmLanguageChooserWidget *widget,
                             GError                  **error)
{
        const char *acode_2;
        const char *acode_3;
        const char *ncode;
        const char *territory_name;

        if (! g_str_equal (element_name, "iso_3166_entry") || attr_names == NULL || attr_values == NULL) {
                return;
        }

        acode_2 = NULL;
        acode_3 = NULL;
        ncode = NULL;
        territory_name = NULL;

        while (*attr_names && *attr_values) {
                if (g_str_equal (*attr_names, "alpha_2_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 2) {
                                        return;
                                }
                                acode_2 = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "alpha_3_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                acode_3 = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "numeric_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ncode = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "name")) {
                        territory_name = *attr_values;
                }

                ++attr_names;
                ++attr_values;
        }

        if (territory_name == NULL) {
                return;
        }

        if (acode_2 != NULL) {
                g_hash_table_insert (widget->priv->territories,
                                     g_strdup (acode_2),
                                     g_strdup (territory_name));
        }
        if (acode_3 != NULL) {
                g_hash_table_insert (widget->priv->territories,
                                     g_strdup (acode_3),
                                     g_strdup (territory_name));
        }
        if (ncode != NULL) {
                g_hash_table_insert (widget->priv->territories,
                                     g_strdup (ncode),
                                     g_strdup (territory_name));
        }
}

static void
languages_init (GdmLanguageChooserWidget *widget)
{
        GError  *error;
        gboolean res;
        char    *buf;
        gsize    buf_len;

        bindtextdomain ("iso_639", ISO_CODES_LOCALESDIR);
        bind_textdomain_codeset ("iso_639", "UTF-8");

        widget->priv->languages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        error = NULL;
        res = g_file_get_contents (ISO_CODES_DATADIR "/iso_639.xml",
                                   &buf,
                                   &buf_len,
                                   &error);
        if (res) {
                GMarkupParseContext *ctx;
                GMarkupParser        parser = { languages_parse_start_tag, NULL, NULL, NULL, NULL };

                ctx = g_markup_parse_context_new (&parser, 0, widget, NULL);

                error = NULL;
                res = g_markup_parse_context_parse (ctx, buf, buf_len, &error);

                if (! res) {
                        g_warning ("Failed to parse '%s': %s\n",
                                   ISO_CODES_DATADIR "/iso_639.xml",
                                   error->message);
                        g_error_free (error);
                }

                g_markup_parse_context_free (ctx);
                g_free (buf);
        } else {
                g_warning ("Failed to load '%s': %s\n",
                           ISO_CODES_DATADIR "/iso_639.xml",
                           error->message);
                g_error_free (error);
        }
}

static void
territories_init (GdmLanguageChooserWidget *widget)
{
        GError  *error;
        gboolean res;
        char    *buf;
        gsize    buf_len;

        bindtextdomain ("iso_3166", ISO_CODES_LOCALESDIR);
        bind_textdomain_codeset ("iso_3166", "UTF-8");

        widget->priv->territories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        error = NULL;
        res = g_file_get_contents (ISO_CODES_DATADIR "/iso_3166.xml",
                                   &buf,
                                   &buf_len,
                                   &error);
        if (res) {
                GMarkupParseContext *ctx;
                GMarkupParser        parser = { territories_parse_start_tag, NULL, NULL, NULL, NULL };

                ctx = g_markup_parse_context_new (&parser, 0, widget, NULL);

                error = NULL;
                res = g_markup_parse_context_parse (ctx, buf, buf_len, &error);

                if (! res) {
                        g_warning ("Failed to parse '%s': %s\n",
                                   ISO_CODES_DATADIR "/iso_3166.xml",
                                   error->message);
                        g_error_free (error);
                }

                g_markup_parse_context_free (ctx);
                g_free (buf);
        } else {
                g_warning ("Failed to load '%s': %s\n",
                           ISO_CODES_DATADIR "/iso_3166.xml",
                           error->message);
                g_error_free (error);
        }
}

static void
add_locale_to_model (const char               *name,
                     GdmChooserLocale         *locale,
                     GdmLanguageChooserWidget *widget)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        char         *title;
        char         *translated;
        const char   *lang;
        const char   *translated_lang;

        lang = get_language (widget, locale->language_code);
        translated_lang = get_translated_language (widget, locale->language_code);

#if 0
        g_debug ("adding to model: %s title='%s' language='%s' territory='%s' language_code='%s' territory_code='%s'",
                 locale->name,
                 locale->title,
                 locale->language,
                 locale->territory,
                 locale->language_code,
                 locale->territory_code);
#endif

        if (locale->territory_code == NULL || locale->territory_code[0] == '\0') {
                title = g_strdup_printf ("%s",
                                         lang ? lang : locale->language);
                translated = g_strdup_printf ("%s",
                                              translated_lang ? lang : locale->language);
        } else {
                const char *terr;
                const char *translated_terr;

                terr = get_territory (widget, locale->territory_code);
                translated_terr = get_translated_territory (widget, locale->territory_code);

                title = g_strdup_printf ("%s (%s)",
                                         lang ? lang : locale->language,
                                         terr);
                translated = g_strdup_printf ("%s (%s)",
                                              translated_lang ? lang : locale->language,
                                              translated_terr);
        }

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget->priv->treeview));

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model),
                            &iter,
                            CHOOSER_LIST_TITLE_COLUMN, title,
                            CHOOSER_LIST_TRANSLATED_COLUMN, translated,
                            CHOOSER_LIST_LOCALE_COLUMN, locale,
                            -1);
        g_free (title);
        g_free (translated);
}

static void
populate_model (GdmLanguageChooserWidget *widget)
{
        g_hash_table_foreach (widget->priv->available_locales,
                              (GHFunc)add_locale_to_model,
                              widget);
}

static void
gdm_language_chooser_widget_init (GdmLanguageChooserWidget *widget)
{
        GtkWidget         *scrolled;
        GtkTreeSelection  *selection;
        GtkTreeViewColumn *column;
        GtkTreeModel      *model;

        widget->priv = GDM_LANGUAGE_CHOOSER_WIDGET_GET_PRIVATE (widget);

        widget->priv->available_locales = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)chooser_locale_free);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget->priv->treeview), FALSE);
        g_signal_connect (widget->priv->treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->treeview);

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
        g_signal_connect (selection, "changed",
                          G_CALLBACK (on_language_selected),
                          widget);

        model = (GtkTreeModel *)gtk_list_store_new (3,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING,
                                                    G_TYPE_POINTER);
        gtk_tree_view_set_model (GTK_TREE_VIEW (widget->priv->treeview), model);

        column = gtk_tree_view_column_new_with_attributes ("Language",
                                                           gtk_cell_renderer_text_new (),
                                                           "markup", CHOOSER_LIST_TITLE_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);

        column = gtk_tree_view_column_new_with_attributes ("Translated Language",
                                                           gtk_cell_renderer_text_new (),
                                                           "markup", CHOOSER_LIST_TRANSLATED_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                              CHOOSER_LIST_TITLE_COLUMN,
                                              GTK_SORT_ASCENDING);

        collect_locales (widget);
        languages_init (widget);
        territories_init (widget);

        populate_model (widget);
}

static void
gdm_language_chooser_widget_finalize (GObject *object)
{
        GdmLanguageChooserWidget *language_chooser_widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LANGUAGE_CHOOSER_WIDGET (object));

        language_chooser_widget = GDM_LANGUAGE_CHOOSER_WIDGET (object);

        g_return_if_fail (language_chooser_widget->priv != NULL);

        G_OBJECT_CLASS (gdm_language_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_language_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_LANGUAGE_CHOOSER_WIDGET,
                               NULL);

        return GTK_WIDGET (object);
}
