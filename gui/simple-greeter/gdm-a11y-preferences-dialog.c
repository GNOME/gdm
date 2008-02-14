/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>

#include "gdm-a11y-preferences-dialog.h"

#define GDM_A11Y_PREFERENCES_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_A11Y_PREFERENCES_DIALOG, GdmA11yPreferencesDialogPrivate))

#define GLADE_XML_FILE "gdm-a11y-preferences-dialog.glade"

#define KEY_A11Y_DIR              "/desktop/gnome/accessibility"
#define KEY_STICKY_KEYS_ENABLED   KEY_A11Y_DIR "/keyboard/stickykeys_enable"
#define KEY_BOUNCE_KEYS_ENABLED   KEY_A11Y_DIR "/keyboard/bouncekeys_enable"
#define KEY_SLOW_KEYS_ENABLED     KEY_A11Y_DIR "/keyboard/slowkeys_enable"
#define KEY_MOUSE_KEYS_ENABLED    KEY_A11Y_DIR "/keyboard/mousekeys_enable"

#define KEY_GDM_A11Y_DIR            "/apps/gdm/simple-greeter/accessibility"
#define KEY_SCREEN_KEYBOARD_ENABLED  KEY_GDM_A11Y_DIR "/screen_keyboard_enabled"
#define KEY_SCREEN_MAGNIFIER_ENABLED KEY_GDM_A11Y_DIR "/screen_magnifier_enabled"
#define KEY_SCREEN_READER_ENABLED    KEY_GDM_A11Y_DIR "/screen_reader_enabled"

#define KEY_GTK_THEME          "/desktop/gnome/interface/gtk_theme"
#define KEY_COLOR_SCHEME       "/desktop/gnome/interface/gtk_color_scheme"
#define KEY_METACITY_THEME     "/apps/metacity/general/theme"
#define KEY_ICON_THEME         "/desktop/gnome/interface/icon_theme"

#define KEY_GTK_FONT           "/desktop/gnome/interface/font_name"

#define HIGH_CONTRAST_THEME    "HighContrast"
#define DEFAULT_LARGE_FONT             "Sans 18"

struct GdmA11yPreferencesDialogPrivate
{
        GladeXML *xml;
        guint     a11y_dir_cnxn;
        guint     gdm_a11y_dir_cnxn;
};

enum {
        PROP_0,
};

static void     gdm_a11y_preferences_dialog_class_init  (GdmA11yPreferencesDialogClass *klass);
static void     gdm_a11y_preferences_dialog_init        (GdmA11yPreferencesDialog      *a11y_preferences_dialog);
static void     gdm_a11y_preferences_dialog_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmA11yPreferencesDialog, gdm_a11y_preferences_dialog, GTK_TYPE_DIALOG)

static void
gdm_a11y_preferences_dialog_set_property (GObject        *object,
                                          guint           prop_id,
                                          const GValue   *value,
                                          GParamSpec     *pspec)
{
        GdmA11yPreferencesDialog *self;

        self = GDM_A11Y_PREFERENCES_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_a11y_preferences_dialog_get_property (GObject        *object,
                                          guint           prop_id,
                                          GValue         *value,
                                          GParamSpec     *pspec)
{
        GdmA11yPreferencesDialog *self;

        self = GDM_A11Y_PREFERENCES_DIALOG (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_a11y_preferences_dialog_constructor (GType                  type,
                                         guint                  n_construct_properties,
                                         GObjectConstructParam *construct_properties)
{
        GdmA11yPreferencesDialog      *a11y_preferences_dialog;
        GdmA11yPreferencesDialogClass *klass;

        klass = GDM_A11Y_PREFERENCES_DIALOG_CLASS (g_type_class_peek (GDM_TYPE_A11Y_PREFERENCES_DIALOG));

        a11y_preferences_dialog = GDM_A11Y_PREFERENCES_DIALOG (G_OBJECT_CLASS (gdm_a11y_preferences_dialog_parent_class)->constructor (type,
                                                                                                                           n_construct_properties,
                                                                                                                           construct_properties));

        return G_OBJECT (a11y_preferences_dialog);
}

static void
gdm_a11y_preferences_dialog_dispose (GObject *object)
{
        GdmA11yPreferencesDialog *a11y_preferences_dialog;

        a11y_preferences_dialog = GDM_A11Y_PREFERENCES_DIALOG (object);

        G_OBJECT_CLASS (gdm_a11y_preferences_dialog_parent_class)->dispose (object);
}

static void
gdm_a11y_preferences_dialog_class_init (GdmA11yPreferencesDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_a11y_preferences_dialog_get_property;
        object_class->set_property = gdm_a11y_preferences_dialog_set_property;
        object_class->constructor = gdm_a11y_preferences_dialog_constructor;
        object_class->dispose = gdm_a11y_preferences_dialog_dispose;
        object_class->finalize = gdm_a11y_preferences_dialog_finalize;

        g_type_class_add_private (klass, sizeof (GdmA11yPreferencesDialogPrivate));
}

static void
on_response (GdmA11yPreferencesDialog *dialog,
             gint                      response_id)
{
        switch (response_id) {
        default:
                break;
        }
}

static char *
config_get_string (const char *key,
                   gboolean   *is_writable)
{
        char        *str;
        GConfClient *client;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             key,
                                                             NULL);
        }

        str = gconf_client_get_string (client, key, NULL);

        g_object_unref (client);

        return str;
}

static gboolean
config_get_bool (const char *key,
                 gboolean   *is_writable)
{
        int          enabled;
        GConfClient *client;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             key,
                                                             NULL);
        }

        enabled = gconf_client_get_bool (client, key, NULL);

        g_object_unref (client);

        return enabled;
}

static char *
get_large_font (GConfClient *client)
{
        char                 *default_font;
        double                new_size;
        char                 *new_font;
        PangoFontDescription *pfd;
        GConfValue           *value;
        new_font = NULL;

        default_font = NULL;
        value = gconf_client_get_default_from_schema (client,
                                                      KEY_GTK_FONT,
                                                      NULL);
        if (value != NULL) {
                default_font = g_strdup (gconf_value_get_string (value));
                gconf_value_free (value);
        }
        if (default_font == NULL) {
                default_font = g_strdup (DEFAULT_LARGE_FONT);
        }

        pfd = pango_font_description_from_string (default_font);
        if (pfd == NULL) {
                goto out;
        }

        if ((pango_font_description_get_set_fields (pfd) & PANGO_FONT_MASK_SIZE)) {
                new_size = pango_font_description_get_size (pfd) / (double)PANGO_SCALE;
                new_size *= PANGO_SCALE_XX_LARGE;
        } else {
                new_size = 18.0;
        }

        pango_font_description_set_size (pfd, new_size * PANGO_SCALE);
        new_font = pango_font_description_to_string (pfd);
        pango_font_description_free (pfd);

 out:
        g_free (default_font);

        return new_font;
}

static gboolean
config_get_large_print (gboolean *is_writable)
{
        gboolean     ret;
        char        *font;
        char        *large_font;
        GConfClient *client;

        ret = FALSE;

        font = config_get_string (KEY_GTK_FONT, is_writable);

        client = gconf_client_get_default ();
        large_font = get_large_font (client);
        g_object_unref (client);

        if (large_font != NULL && font != NULL && strcmp (large_font, font) == 0) {
                ret = TRUE;
        }

        g_free (large_font);
        g_free (font);

        return ret;
}

static void
config_set_large_print (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        if (enabled) {
                char *large_font;
                large_font = get_large_font (client);
                g_debug ("GdmA11yPreferencesDialog: Setting font to '%s'", large_font);
                gconf_client_set_string (client, KEY_GTK_FONT, large_font, NULL);
                g_free (large_font);
        } else {
                gconf_client_unset (client, KEY_GTK_FONT, NULL);
        }

        g_object_unref (client);
}

static gboolean
config_get_high_contrast (gboolean *is_writable)
{
        gboolean ret;
        char    *gtk_theme;

        ret = FALSE;

        gtk_theme = config_get_string (KEY_GTK_THEME, is_writable);
        if (gtk_theme != NULL && strcmp (gtk_theme, HIGH_CONTRAST_THEME) == 0) {
                ret = TRUE;
        }
        g_free (gtk_theme);

        return ret;
}

static void
config_set_high_contrast (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        if (enabled) {
                gconf_client_set_string (client, KEY_GTK_THEME, HIGH_CONTRAST_THEME, NULL);
                gconf_client_set_string (client, KEY_ICON_THEME, HIGH_CONTRAST_THEME, NULL);
                /* there isn't a high contrast metacity theme afaik */
        } else {
                gconf_client_unset (client, KEY_GTK_THEME, NULL);
                gconf_client_unset (client, KEY_ICON_THEME, NULL);
                gconf_client_unset (client, KEY_METACITY_THEME, NULL);
        }

        g_object_unref (client);
}

static gboolean
config_get_sticky_keys (gboolean *is_writable)
{
        return config_get_bool (KEY_STICKY_KEYS_ENABLED, is_writable);
}

static void
config_set_sticky_keys (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_STICKY_KEYS_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static gboolean
config_get_bounce_keys (gboolean *is_writable)
{
        return config_get_bool (KEY_BOUNCE_KEYS_ENABLED, is_writable);
}

static void
config_set_bounce_keys (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_BOUNCE_KEYS_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static gboolean
config_get_slow_keys (gboolean *is_writable)
{
        return config_get_bool (KEY_SLOW_KEYS_ENABLED, is_writable);
}

static void
config_set_slow_keys (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_SLOW_KEYS_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static gboolean
config_get_screen_keyboard (gboolean *is_writable)
{
        return config_get_bool (KEY_SCREEN_KEYBOARD_ENABLED, is_writable);
}

static void
config_set_screen_keyboard (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_SCREEN_KEYBOARD_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static gboolean
config_get_screen_reader (gboolean *is_writable)
{
        return config_get_bool (KEY_SCREEN_READER_ENABLED, is_writable);
}

static void
config_set_screen_reader (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_SCREEN_READER_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static gboolean
config_get_screen_magnifier (gboolean *is_writable)
{
        return config_get_bool (KEY_SCREEN_MAGNIFIER_ENABLED, is_writable);
}

static void
config_set_screen_magnifier (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client, KEY_SCREEN_MAGNIFIER_ENABLED, enabled, NULL);
        g_object_unref (client);
}

static void
on_sticky_keys_checkbutton_toggled (GtkToggleButton          *button,
                                 GdmA11yPreferencesDialog *dialog)
{
        config_set_sticky_keys (gtk_toggle_button_get_active (button));
}

static void
on_bounce_keys_checkbutton_toggled (GtkToggleButton          *button,
                                 GdmA11yPreferencesDialog *dialog)
{
        config_set_bounce_keys (gtk_toggle_button_get_active (button));
}

static void
on_slow_keys_checkbutton_toggled (GtkToggleButton          *button,
                                  GdmA11yPreferencesDialog *dialog)
{
        config_set_slow_keys (gtk_toggle_button_get_active (button));
}

static void
on_high_contrast_checkbutton_toggled (GtkToggleButton          *button,
                                      GdmA11yPreferencesDialog *dialog)
{
        config_set_high_contrast (gtk_toggle_button_get_active (button));
}

static void
on_screen_keyboard_checkbutton_toggled (GtkToggleButton          *button,
                                        GdmA11yPreferencesDialog *dialog)
{
        config_set_screen_keyboard (gtk_toggle_button_get_active (button));
}

static void
on_screen_reader_checkbutton_toggled (GtkToggleButton          *button,
                                      GdmA11yPreferencesDialog *dialog)
{
        config_set_screen_reader (gtk_toggle_button_get_active (button));
}

static void
on_screen_magnifier_checkbutton_toggled (GtkToggleButton          *button,
                                         GdmA11yPreferencesDialog *dialog)
{
        config_set_screen_magnifier (gtk_toggle_button_get_active (button));
}

static void
on_large_print_checkbutton_toggled (GtkToggleButton          *button,
                                    GdmA11yPreferencesDialog *dialog)
{
        config_set_large_print (gtk_toggle_button_get_active (button));
}

static void
ui_set_sticky_keys (GdmA11yPreferencesDialog *dialog,
                    gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "sticky_keys_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_bounce_keys (GdmA11yPreferencesDialog *dialog,
                    gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "bounce_keys_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_slow_keys (GdmA11yPreferencesDialog *dialog,
                  gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "slow_keys_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_high_contrast (GdmA11yPreferencesDialog *dialog,
                      gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "high_contrast_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_screen_keyboard (GdmA11yPreferencesDialog *dialog,
                        gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_keyboard_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_screen_reader (GdmA11yPreferencesDialog *dialog,
                      gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_reader_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_screen_magnifier (GdmA11yPreferencesDialog *dialog,
                         gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_magnifier_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_large_print (GdmA11yPreferencesDialog *dialog,
                    gboolean                  enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (dialog->priv->xml, "large_print_checkbutton");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
key_changed_cb (GConfClient *client,
                guint        cnxn_id,
                GConfEntry  *entry,
                GdmA11yPreferencesDialog *dialog)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);
        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_STICKY_KEYS_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_sticky_keys (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_BOUNCE_KEYS_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_bounce_keys (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_SLOW_KEYS_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_slow_keys (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_SCREEN_KEYBOARD_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_screen_keyboard (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_SCREEN_READER_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_screen_reader (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_SCREEN_MAGNIFIER_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        ui_set_screen_magnifier (dialog, enabled);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else {
                g_debug ("Config key not handled: %s", key);
        }
}

static void
setup_dialog (GdmA11yPreferencesDialog *dialog)
{
        GtkWidget   *widget;
        gboolean     enabled;
        gboolean     is_writable;
        GConfClient *client;

        widget = glade_xml_get_widget (dialog->priv->xml, "sticky_keys_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_sticky_keys_checkbutton_toggled),
                          NULL);
        enabled = config_get_sticky_keys (&is_writable);
        ui_set_sticky_keys (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "bounce_keys_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_bounce_keys_checkbutton_toggled),
                          NULL);
        enabled = config_get_bounce_keys (&is_writable);
        ui_set_bounce_keys (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "slow_keys_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_slow_keys_checkbutton_toggled),
                          NULL);
        enabled = config_get_slow_keys (&is_writable);
        ui_set_slow_keys (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "high_contrast_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_high_contrast_checkbutton_toggled),
                          NULL);
        enabled = config_get_high_contrast (&is_writable);
        ui_set_high_contrast (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_keyboard_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_screen_keyboard_checkbutton_toggled),
                          NULL);
        enabled = config_get_screen_keyboard (&is_writable);
        ui_set_screen_keyboard (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_reader_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_screen_reader_checkbutton_toggled),
                          NULL);
        enabled = config_get_screen_reader (&is_writable);
        ui_set_screen_reader (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "screen_magnifier_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_screen_magnifier_checkbutton_toggled),
                          NULL);
        enabled = config_get_screen_magnifier (&is_writable);
        ui_set_screen_magnifier (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }

        widget = glade_xml_get_widget (dialog->priv->xml, "large_print_checkbutton");
        g_signal_connect (widget,
                          "toggled",
                          G_CALLBACK (on_large_print_checkbutton_toggled),
                          NULL);
        enabled = config_get_large_print (&is_writable);
        ui_set_large_print (dialog, enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (widget, FALSE);
        }


        client = gconf_client_get_default ();
        gconf_client_add_dir (client,
                              KEY_A11Y_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        dialog->priv->a11y_dir_cnxn = gconf_client_notify_add (client,
                                                               KEY_A11Y_DIR,
                                                               (GConfClientNotifyFunc)key_changed_cb,
                                                               dialog,
                                                               NULL,
                                                               NULL);

        gconf_client_add_dir (client,
                              KEY_GDM_A11Y_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        dialog->priv->gdm_a11y_dir_cnxn = gconf_client_notify_add (client,
                                                                   KEY_GDM_A11Y_DIR,
                                                                   (GConfClientNotifyFunc)key_changed_cb,
                                                                   dialog,
                                                                   NULL,
                                                                   NULL);
        g_object_unref (client);
}

static void
gdm_a11y_preferences_dialog_init (GdmA11yPreferencesDialog *dialog)
{
        GtkWidget *widget;

        dialog->priv = GDM_A11Y_PREFERENCES_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE,
                                           "main_box",
                                           PACKAGE);
        g_assert (dialog->priv->xml != NULL);

        widget = glade_xml_get_widget (dialog->priv->xml, "main_box");
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), widget);

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
        gtk_container_set_border_width (GTK_CONTAINER (widget), 12);
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_window_set_title (GTK_WINDOW (dialog), _("Accessibility Preferences"));
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-accessibility");
        gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                NULL);
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);

        setup_dialog (dialog);

        gtk_widget_show_all (GTK_WIDGET (dialog));
}

static void
gdm_a11y_preferences_dialog_finalize (GObject *object)
{
        GdmA11yPreferencesDialog *dialog;
        GConfClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_A11Y_PREFERENCES_DIALOG (object));

        dialog = GDM_A11Y_PREFERENCES_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        client = gconf_client_get_default ();

        if (dialog->priv->a11y_dir_cnxn > 0) {
                gconf_client_notify_remove (client, dialog->priv->a11y_dir_cnxn);
        }
        if (dialog->priv->gdm_a11y_dir_cnxn > 0) {
                gconf_client_notify_remove (client, dialog->priv->gdm_a11y_dir_cnxn);
        }

        g_object_unref (client);

        G_OBJECT_CLASS (gdm_a11y_preferences_dialog_parent_class)->finalize (object);
}

GtkWidget *
gdm_a11y_preferences_dialog_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_A11Y_PREFERENCES_DIALOG,
                               NULL);

        return GTK_WIDGET (object);
}
