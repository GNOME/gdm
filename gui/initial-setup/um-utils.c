/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2009-2010  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 * Written by: Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <utmp.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "um-utils.h"

typedef struct {
        gchar *text;
        gchar *placeholder_str;
        GIcon *icon;
        gunichar placeholder;
        gulong query_id;
} IconShapeData;

static IconShapeData *
icon_shape_data_new (const gchar *text,
                     const gchar *placeholder,
                     GIcon       *icon)
{
        IconShapeData *data;

        data = g_new0 (IconShapeData, 1);

        data->text = g_strdup (text);
        data->placeholder_str = g_strdup (placeholder);
        data->placeholder = g_utf8_get_char_validated (placeholder, -1);
        data->icon = g_object_ref (icon);

        return data;
}

static void
icon_shape_data_free (gpointer user_data)
{
        IconShapeData *data = user_data;

        g_free (data->text);
        g_free (data->placeholder_str);
        g_object_unref (data->icon);
        g_free (data);
}

static void
icon_shape_renderer (cairo_t        *cr,
                     PangoAttrShape *attr,
                     gboolean        do_path,
                     gpointer        user_data)
{
        IconShapeData *data = user_data;
        gdouble x, y;

        cairo_get_current_point (cr, &x, &y);
        if (GPOINTER_TO_UINT (attr->data) == data->placeholder) {
                gdouble ascent;
                gdouble height;
                GdkPixbuf *pixbuf;
                GtkIconInfo *info;

                ascent = pango_units_to_double (attr->ink_rect.y);
                height = pango_units_to_double (attr->ink_rect.height);
                info = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
                                                       data->icon,
                                                       (gint)height,
                                                       GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_USE_BUILTIN);
                pixbuf = gtk_icon_info_load_icon (info, NULL);
                gtk_icon_info_free (info);

                cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
                cairo_reset_clip (cr);
                gdk_cairo_set_source_pixbuf (cr, pixbuf, x, y + ascent);
                cairo_paint (cr);
                g_object_unref (pixbuf);
        }
}

static PangoAttrList *
create_shape_attr_list_for_layout (PangoLayout   *layout,
                                   IconShapeData *data)
{
        PangoAttrList *attrs;
        PangoFontMetrics *metrics;
        gint ascent, descent;
        PangoRectangle ink_rect, logical_rect;
        const gchar *p;
        const gchar *text;
        gint placeholder_len;

        /* Get font metrics and prepare fancy shape size */
        metrics = pango_context_get_metrics (pango_layout_get_context (layout),
                                             pango_layout_get_font_description (layout),
                                             NULL);
        ascent = pango_font_metrics_get_ascent (metrics);
        descent = pango_font_metrics_get_descent (metrics);
        pango_font_metrics_unref (metrics);

        logical_rect.x = 0;
        logical_rect.y = - ascent;
        logical_rect.width = ascent + descent;
        logical_rect.height = ascent + descent;

        ink_rect = logical_rect;

        attrs = pango_attr_list_new ();
        text = pango_layout_get_text (layout);
        placeholder_len = strlen (data->placeholder_str);
        for (p = text; (p = strstr (p, data->placeholder_str)); p += placeholder_len) {
                PangoAttribute *attr;

                attr = pango_attr_shape_new_with_data (&ink_rect,
                                                       &logical_rect,
                                                       GUINT_TO_POINTER (g_utf8_get_char (p)),
                                                       NULL, NULL);

                attr->start_index = p - text;
                attr->end_index = attr->start_index + placeholder_len;

                pango_attr_list_insert (attrs, attr);
        }

        return attrs;
}

static gboolean
query_unlock_tooltip (GtkWidget  *widget,
                      gint        x,
                      gint        y,
                      gboolean    keyboard_tooltip,
                      GtkTooltip *tooltip,
                      gpointer    user_data)
{
        GtkWidget *label;
        PangoLayout *layout;
        PangoAttrList *attrs;
        IconShapeData *data;

        data = g_object_get_data (G_OBJECT (widget), "icon-shape-data");
        label = g_object_get_data (G_OBJECT (widget), "tooltip-label");
        if (label == NULL) {
                label = gtk_label_new (data->text);
                g_object_ref_sink (label);
                g_object_set_data_full (G_OBJECT (widget),
                                        "tooltip-label", label, g_object_unref);
        }

        layout = gtk_label_get_layout (GTK_LABEL (label));
        pango_cairo_context_set_shape_renderer (pango_layout_get_context (layout),
                                                icon_shape_renderer,
                                                data, NULL);

        attrs = create_shape_attr_list_for_layout (layout, data);
        gtk_label_set_attributes (GTK_LABEL (label), attrs);
        pango_attr_list_unref (attrs);

        gtk_tooltip_set_custom (tooltip, label);

        return TRUE;
}

void
setup_tooltip_with_embedded_icon (GtkWidget   *widget,
                                  const gchar *text,
                                  const gchar *placeholder,
                                  GIcon       *icon)
{
        IconShapeData *data;

        data = g_object_get_data (G_OBJECT (widget), "icon-shape-data");
        if (data) {
                gtk_widget_set_has_tooltip (widget, FALSE);
                g_signal_handler_disconnect (widget, data->query_id);
                g_object_set_data (G_OBJECT (widget), "icon-shape-data", NULL);
                g_object_set_data (G_OBJECT (widget), "tooltip-label", NULL);
        }

        if (!placeholder) {
                gtk_widget_set_tooltip_text (widget, text);
                return;
        }

        data = icon_shape_data_new (text, placeholder, icon);
        g_object_set_data_full (G_OBJECT (widget),
                                "icon-shape-data",
                                data,
                                icon_shape_data_free);

        gtk_widget_set_has_tooltip (widget, TRUE);
        data->query_id = g_signal_connect (widget, "query-tooltip",
                                           G_CALLBACK (query_unlock_tooltip), NULL);

}

gboolean
show_tooltip_now (GtkWidget *widget,
                  GdkEvent  *event)
{
        GtkSettings *settings;
        gint timeout;

        settings = gtk_widget_get_settings (widget);

        g_object_get (settings, "gtk-tooltip-timeout", &timeout, NULL);
        g_object_set (settings, "gtk-tooltip-timeout", 1, NULL);
        gtk_tooltip_trigger_tooltip_query (gtk_widget_get_display (widget));
        g_object_set (settings, "gtk-tooltip-timeout", timeout, NULL);

        return FALSE;
}

static gboolean
query_tooltip (GtkWidget  *widget,
               gint        x,
               gint        y,
               gboolean    keyboard_mode,
               GtkTooltip *tooltip,
               gpointer    user_data)
{
        gchar *tip;

        if (GTK_ENTRY_ICON_SECONDARY == gtk_entry_get_icon_at_pos (GTK_ENTRY (widget), x, y)) {
                tip = gtk_entry_get_icon_tooltip_text (GTK_ENTRY (widget),
                                                       GTK_ENTRY_ICON_SECONDARY);
                gtk_tooltip_set_text (tooltip, tip);
                g_free (tip);

                return TRUE;
        }
        else {
                return FALSE;
        }
}

static void
icon_released (GtkEntry             *entry,
              GtkEntryIconPosition  pos,
              GdkEvent             *event,
              gpointer              user_data)
{
        GtkSettings *settings;
        gint timeout;

        settings = gtk_widget_get_settings (GTK_WIDGET (entry));

        g_object_get (settings, "gtk-tooltip-timeout", &timeout, NULL);
        g_object_set (settings, "gtk-tooltip-timeout", 1, NULL);
        gtk_tooltip_trigger_tooltip_query (gtk_widget_get_display (GTK_WIDGET (entry)));
        g_object_set (settings, "gtk-tooltip-timeout", timeout, NULL);
}


void
set_entry_validation_error (GtkEntry    *entry,
                            const gchar *text)
{
        g_object_set (entry, "caps-lock-warning", FALSE, NULL);
        gtk_entry_set_icon_from_stock (entry,
                                       GTK_ENTRY_ICON_SECONDARY,
                                       GTK_STOCK_DIALOG_ERROR);
        gtk_entry_set_icon_activatable (entry,
                                        GTK_ENTRY_ICON_SECONDARY,
                                        TRUE);
        g_signal_connect (entry, "icon-release",
                          G_CALLBACK (icon_released), FALSE);
        g_signal_connect (entry, "query-tooltip",
                          G_CALLBACK (query_tooltip), NULL);
        g_object_set (entry, "has-tooltip", TRUE, NULL);
        gtk_entry_set_icon_tooltip_text (entry,
                                         GTK_ENTRY_ICON_SECONDARY,
                                         text);
}

void
clear_entry_validation_error (GtkEntry *entry)
{
        gboolean warning;

        g_object_get (entry, "caps-lock-warning", &warning, NULL);

        if (warning)
                return;

        g_object_set (entry, "has-tooltip", FALSE, NULL);
        gtk_entry_set_icon_from_pixbuf (entry,
                                        GTK_ENTRY_ICON_SECONDARY,
                                        NULL);
        g_object_set (entry, "caps-lock-warning", TRUE, NULL);
}

void
popup_menu_below_button (GtkMenu   *menu,
                         gint      *x,
                         gint      *y,
                         gboolean  *push_in,
                         GtkWidget *button)
{
        GtkRequisition menu_req;
        GtkTextDirection direction;
        GtkAllocation allocation;

	gtk_widget_get_preferred_size (GTK_WIDGET (menu), NULL, &menu_req);

        direction = gtk_widget_get_direction (button);

        gdk_window_get_origin (gtk_widget_get_window (button), x, y);
        gtk_widget_get_allocation (button, &allocation);
        *x += allocation.x;
        *y += allocation.y + allocation.height;

        if (direction == GTK_TEXT_DIR_LTR)
                *x += MAX (allocation.width - menu_req.width, 0);
        else if (menu_req.width > allocation.width)
                *x -= menu_req.width - allocation.width;

        *push_in = TRUE;
}

void
rounded_rectangle (cairo_t *cr,
                   gdouble  aspect,
                   gdouble  x,
                   gdouble  y,
                   gdouble  corner_radius,
                   gdouble  width,
                   gdouble  height)
{
        gdouble radius;
        gdouble degrees;

        radius = corner_radius / aspect;
        degrees = G_PI / 180.0;

        cairo_new_sub_path (cr);
        cairo_arc (cr,
                   x + width - radius,
                   y + radius,
                   radius,
                   -90 * degrees,
                   0 * degrees);
        cairo_arc (cr,
                   x + width - radius,
                   y + height - radius,
                   radius,
                   0 * degrees,
                   90 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + height - radius,
                   radius,
                   90 * degrees,
                   180 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + radius,
                   radius,
                   180 * degrees,
                   270 * degrees);
        cairo_close_path (cr);
}

void
down_arrow (GtkStyleContext *context,
            cairo_t         *cr,
            gint             x,
            gint             y,
            gint             width,
            gint             height)
{
        GtkStateFlags flags;
        GdkRGBA fg_color;
        GdkRGBA outline_color;
        gdouble vertical_overshoot;
        gint diameter;
        gdouble radius;
        gdouble x_double, y_double;
        gdouble angle;
        gint line_width;

        flags = gtk_style_context_get_state (context);

        gtk_style_context_get_color (context, flags, &fg_color);
        gtk_style_context_get_border_color (context, flags, &outline_color);

        line_width = 1;
        angle = G_PI / 2;
        vertical_overshoot = line_width / 2.0 * (1. / tan (G_PI / 8));
        if (line_width % 2 == 1)
                vertical_overshoot = ceil (0.5 + vertical_overshoot) - 0.5;
        else
                vertical_overshoot = ceil (vertical_overshoot);
        diameter = (gint) MAX (3, width - 2 * vertical_overshoot);
        diameter -= (1 - (diameter + line_width) % 2);
        radius = diameter / 2.;
        x_double = floor ((x + width / 2) - (radius + line_width) / 2.) + (radius + line_width) / 2.;

        y_double = (y + height / 2) - 0.5;

        cairo_save (cr);

        cairo_translate (cr, x_double, y_double);
        cairo_rotate (cr, angle);

        cairo_move_to (cr, - radius / 2., - radius);
        cairo_line_to (cr,   radius / 2.,   0);
        cairo_line_to (cr, - radius / 2.,   radius);

        cairo_close_path (cr);

        cairo_set_line_width (cr, line_width);

        gdk_cairo_set_source_rgba (cr, &fg_color);

        cairo_fill_preserve (cr);

        gdk_cairo_set_source_rgba (cr, &outline_color);
        cairo_stroke (cr);

        cairo_restore (cr);
}


#define MAXNAMELEN  (UT_NAMESIZE - 1)

static gboolean
is_username_used (const gchar *username)
{
        struct passwd *pwent;

        if (username == NULL || username[0] == '\0') {
                return FALSE;
        }

        pwent = getpwnam (username);

        return pwent != NULL;
}

gboolean
is_valid_name (const gchar *name)
{
        gboolean valid;

        valid = (strlen (name) > 0);

        return valid;
}

gboolean
is_valid_username (const gchar *username, gchar **tip)
{
        gboolean empty;
        gboolean in_use;
        gboolean too_long;
        gboolean valid;
        const gchar *c;

        if (username == NULL || username[0] == '\0') {
                empty = TRUE;
                in_use = FALSE;
                too_long = FALSE;
        } else {
                empty = FALSE;
                in_use = is_username_used (username);
                too_long = strlen (username) > MAXNAMELEN;
        }
        valid = TRUE;

        if (!in_use && !empty && !too_long) {
                /* First char must be a letter, and it must only composed
                 * of ASCII letters, digits, and a '.', '-', '_'
                 */
                for (c = username; *c; c++) {
                        if (! ((*c >= 'a' && *c <= 'z') ||
                               (*c >= 'A' && *c <= 'Z') ||
                               (*c >= '0' && *c <= '9') ||
                               (*c == '_') || (*c == '.') ||
                               (*c == '-' && c != username)))
                           valid = FALSE;
                }
        }

        valid = !empty && !in_use && !too_long && valid;

        if (!empty && (in_use || too_long || !valid)) {
                if (in_use) {
                        *tip = g_strdup_printf (_("A user with the username '%s' already exists"),
                                               username);
                }
                else if (too_long) {
                        *tip = g_strdup_printf (_("The username is too long"));
                }
                else if (username[0] == '-') {
                        *tip = g_strdup (_("The username cannot start with a '-'"));
                }
                else {
                        *tip = g_strdup (_("The username must consist of:\n"
                                          " \xe2\x9e\xa3 letters from the English alphabet\n"
                                          " \xe2\x9e\xa3 digits\n"
                                          " \xe2\x9e\xa3 any of the characters '.', '-' and '_'"));
                }
        }
        else {
                *tip = NULL;
        }

        return valid;
}

void
generate_username_choices (const gchar  *name,
                           GtkListStore *store)
{
        gboolean in_use;
        char *lc_name, *ascii_name, *stripped_name;
        char **words1;
        char **words2 = NULL;
        char **w1, **w2;
        char *c;
        char *unicode_fallback = "?";
        GString *first_word, *last_word;
        GString *item0, *item1, *item2, *item3, *item4;
        int len;
        int nwords1, nwords2, i;
        GHashTable *items;
        GtkTreeIter iter;

        gtk_list_store_clear (store);

        ascii_name = g_convert_with_fallback (name, -1, "ASCII//TRANSLIT", "UTF-8",
                                              unicode_fallback, NULL, NULL, NULL);

        lc_name = g_ascii_strdown (ascii_name, -1);

        /* Remove all non ASCII alphanumeric chars from the name,
         * apart from the few allowed symbols.
         *
         * We do remove '.', even though it is usually allowed,
         * since it often comes in via an abbreviated middle name,
         * and the dot looks just wrong in the proposals then.
         */
        stripped_name = g_strnfill (strlen (lc_name) + 1, '\0');
        i = 0;
        for (c = lc_name; *c; c++) {
                if (!(g_ascii_isdigit (*c) || g_ascii_islower (*c) ||
                    *c == ' ' || *c == '-' || *c == '_' ||
                    /* used to track invalid words, removed below */
                    *c == '?') )
                        continue;

                    stripped_name[i] = *c;
                    i++;
        }

        if (strlen (stripped_name) == 0) {
                g_free (ascii_name);
                g_free (lc_name);
                g_free (stripped_name);
                return;
        }

        /* we split name on spaces, and then on dashes, so that we can treat
         * words linked with dashes the same way, i.e. both fully shown, or
         * both abbreviated
         */
        words1 = g_strsplit_set (stripped_name, " ", -1);
        len = g_strv_length (words1);

        /* The default item is a concatenation of all words without ? */
        item0 = g_string_sized_new (strlen (stripped_name));

        g_free (ascii_name);
        g_free (lc_name);
        g_free (stripped_name);

        /* Concatenate the whole first word with the first letter of each
         * word (item1), and the last word with the first letter of each
         * word (item2). item3 and item4 are symmetrical respectively to
         * item1 and item2.
         *
         * Constant 5 is the max reasonable number of words we may get when
         * splitting on dashes, since we can't guess it at this point,
         * and reallocating would be too bad.
         */
        item1 = g_string_sized_new (strlen (words1[0]) + len - 1 + 5);
        item3 = g_string_sized_new (strlen (words1[0]) + len - 1 + 5);

        item2 = g_string_sized_new (strlen (words1[len - 1]) + len - 1 + 5);
        item4 = g_string_sized_new (strlen (words1[len - 1]) + len - 1 + 5);

        /* again, guess at the max size of names */
        first_word = g_string_sized_new (20);
        last_word = g_string_sized_new (20);

        nwords1 = 0;
        nwords2 = 0;
        for (w1 = words1; *w1; w1++) {
                if (strlen (*w1) == 0)
                        continue;

                /* skip words with string '?', most likely resulting
                 * from failed transliteration to ASCII
                 */
                if (strstr (*w1, unicode_fallback) != NULL)
                        continue;

                nwords1++; /* count real words, excluding empty string */

                item0 = g_string_append (item0, *w1);

                words2 = g_strsplit_set (*w1, "-", -1);
                /* reset last word if a new non-empty word has been found */
                if (strlen (*words2) > 0)
                        last_word = g_string_set_size (last_word, 0);

                for (w2 = words2; *w2; w2++) {
                        if (strlen (*w2) == 0)
                                continue;

                        nwords2++;

                        /* part of the first "toplevel" real word */
                        if (nwords1 == 1) {
                                item1 = g_string_append (item1, *w2);
                                first_word = g_string_append (first_word, *w2);
                        }
                        else {
                                item1 = g_string_append_unichar (item1,
                                                                 g_utf8_get_char (*w2));
                                item3 = g_string_append_unichar (item3,
                                                                 g_utf8_get_char (*w2));
                        }

                        /* not part of the last "toplevel" word */
                        if (w1 != words1 + len - 1) {
                                item2 = g_string_append_unichar (item2,
                                                                 g_utf8_get_char (*w2));
                                item4 = g_string_append_unichar (item4,
                                                                 g_utf8_get_char (*w2));
                        }

                        /* always save current word so that we have it if last one reveals empty */
                        last_word = g_string_append (last_word, *w2);
                }

                g_strfreev (words2);
        }
        item2 = g_string_append (item2, last_word->str);
        item3 = g_string_append (item3, first_word->str);
        item4 = g_string_prepend (item4, last_word->str);

        items = g_hash_table_new (g_str_hash, g_str_equal);

        in_use = is_username_used (item0->str);
        if (!in_use && !g_ascii_isdigit (item0->str[0])) {
                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter, 0, item0->str, -1);
                g_hash_table_insert (items, item0->str, item0->str);
        }

        in_use = is_username_used (item1->str);
        if (nwords2 > 0 && !in_use && !g_ascii_isdigit (item1->str[0])) {
                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter, 0, item1->str, -1);
                g_hash_table_insert (items, item1->str, item1->str);
        }

        /* if there's only one word, would be the same as item1 */
        if (nwords2 > 1) {
                /* add other items */
                in_use = is_username_used (item2->str);
                if (!in_use && !g_ascii_isdigit (item2->str[0]) &&
                    !g_hash_table_lookup (items, item2->str)) {
                        gtk_list_store_append (store, &iter);
                        gtk_list_store_set (store, &iter, 0, item2->str, -1);
                        g_hash_table_insert (items, item2->str, item2->str);
                }

                in_use = is_username_used (item3->str);
                if (!in_use && !g_ascii_isdigit (item3->str[0]) &&
                    !g_hash_table_lookup (items, item3->str)) {
                        gtk_list_store_append (store, &iter);
                        gtk_list_store_set (store, &iter, 0, item3->str, -1);
                        g_hash_table_insert (items, item3->str, item3->str);
                }

                in_use = is_username_used (item4->str);
                if (!in_use && !g_ascii_isdigit (item4->str[0]) &&
                    !g_hash_table_lookup (items, item4->str)) {
                        gtk_list_store_append (store, &iter);
                        gtk_list_store_set (store, &iter, 0, item4->str, -1);
                        g_hash_table_insert (items, item4->str, item4->str);
                }

                /* add the last word */
                in_use = is_username_used (last_word->str);
                if (!in_use && !g_ascii_isdigit (last_word->str[0]) &&
                    !g_hash_table_lookup (items, last_word->str)) {
                        gtk_list_store_append (store, &iter);
                        gtk_list_store_set (store, &iter, 0, last_word->str, -1);
                        g_hash_table_insert (items, last_word->str, last_word->str);
                }

                /* ...and the first one */
                in_use = is_username_used (first_word->str);
                if (!in_use && !g_ascii_isdigit (first_word->str[0]) &&
                    !g_hash_table_lookup (items, first_word->str)) {
                        gtk_list_store_append (store, &iter);
                        gtk_list_store_set (store, &iter, 0, first_word->str, -1);
                        g_hash_table_insert (items, first_word->str, first_word->str);
                }
        }

        g_hash_table_destroy (items);
        g_strfreev (words1);
        g_string_free (first_word, TRUE);
        g_string_free (last_word, TRUE);
        g_string_free (item0, TRUE);
        g_string_free (item1, TRUE);
        g_string_free (item2, TRUE);
        g_string_free (item3, TRUE);
        g_string_free (item4, TRUE);
}
